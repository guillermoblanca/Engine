#include "TerrainGraphUi.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <map>
#include <set>

namespace GB::Editor
{
	namespace
	{
		constexpr float kNodeW = 190.0f, kNodeBaseH = 54.0f, kPortRow = 17.0f, kPin = 6.0f;
		ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
		ImU32 HeaderColor(uint32_t rgb)
		{
			if (rgb == 0) rgb = 0x968C32;
			return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
		}
		ImU32 PinColor(TgPortKind kind)
		{
			switch (kind)
			{
			case TgPortKind::Field: return IM_COL32(220, 220, 230, 255);
			case TgPortKind::Placements: return IM_COL32(150, 220, 110, 255);
			case TgPortKind::Paths: return IM_COL32(240, 180, 90, 255);
			case TgPortKind::Water: return IM_COL32(90, 190, 240, 255);
			default: return IM_COL32(200, 160, 230, 255);
			}
		}
		bool Compatible(TgPortKind a, TgPortKind b) { return a == b || a == TgPortKind::Any || b == TgPortKind::Any; }
	}

	TerrainGraphUi::TerrainGraphUi() : m_Graph(std::make_shared<TerrainGraph>()), m_Registry(MakeDefaultTerrainNodeRegistry())
	{
		m_Graph->Id = "terrain";
		m_Graph->Name = "Terrain";
		AddNode("heightfield_noise", ImVec2(40.0f, 90.0f));
		AddNode("erosion_hydraulic", ImVec2(300.0f, 90.0f));
		AddNode("output", ImVec2(580.0f, 90.0f));
		m_Graph->Edges = { { "heightfield_noise", "erosion_hydraulic", "height", "height" }, { "erosion_hydraulic", "output", "in", "height" } };
		m_Message.clear();
	}

	const TgNodeSchema& TerrainGraphUi::SchemaOf(const std::string& type) const
	{
		const TgNodeSchema* s = m_Registry.Schema(type);
		return s ? *s : m_Unknown;
	}

	int TerrainGraphUi::OutputIndex(const TgNode& node, const std::string& port) const
	{
		const TgNodeSchema& s = SchemaOf(node.Type);
		const int index = s.OutputIndex(port);
		return index >= 0 ? index : (s.Outputs.empty() ? -1 : 0);
	}

	std::string TerrainGraphUi::UniqueId(const std::string& type) const
	{
		std::string base = type;
		std::replace(base.begin(), base.end(), '.', '_');
		std::string id = base;
		for (int i = 2; m_Graph->FindNode(id); ++i) id = base + std::to_string(i);
		return id;
	}

	void TerrainGraphUi::AddNode(const std::string& type, const ImVec2& position)
	{
		const TgNodeSchema& schema = SchemaOf(type);
		TgNode node;
		node.Id = UniqueId(type);
		node.Type = type;
		node.Label = type;
		node.X = position.x;
		node.Y = position.y;
		for (const TgParamInfo& p : schema.Params) node.Params[p.Name] = p.Default;
		if (schema.UsesSource) node.Source = schema.SourceDefault;
		m_Graph->Nodes.push_back(std::move(node));
		m_Message = "Added " + type;
		m_MessageError = false;
	}

	void TerrainGraphUi::LoadGraph(const TerrainGraph& graph)
	{
		*m_Graph = graph;
		ResetCanvasInteraction();
		m_Message = "Loaded " + (graph.Name.empty() ? graph.Id : graph.Name);
		m_MessageError = false;
	}

	bool TerrainGraphUi::ConsumeApplyRequest()
	{
		const bool requested = m_ApplyRequested;
		m_ApplyRequested = false;
		return requested;
	}

	void TerrainGraphUi::Connect(const std::string& from, const std::string& fromPort, TgNode& to, size_t input)
	{
		const TgNodeSchema& target = SchemaOf(to.Type);
		const TgNode* source = m_Graph->FindNode(from);
		if (!source || input >= target.Inputs.size()) return;
		const TgNodeSchema& origin = SchemaOf(source->Type);
		const int out = origin.OutputIndex(fromPort);
		if (out < 0) return;
		const TgPortInfo& o = origin.Outputs[static_cast<size_t>(out)];
		const TgPortInfo& i = target.Inputs[input];
		if (!Compatible(o.Kind, i.Kind))
		{
			m_Message = std::string("Incompatible ports: ") + TgPortKindName(o.Kind) + " -> " + TgPortKindName(i.Kind);
			m_MessageError = true;
			return;
		}
		// one edge per input: a new one replaces it
		m_Graph->Edges.erase(std::remove_if(m_Graph->Edges.begin(), m_Graph->Edges.end(), [&](const TgEdge& e)
		{
			if (e.To != to.Id) return false;
			if (e.Port == i.Name) return true;
			return target.InputIndex(e.Port) < 0;   // (a version 1 edge bound by order: rewired by name from now on)
		}), m_Graph->Edges.end());
		m_Graph->Edges.push_back({ from, to.Id, i.Name, o.Name });
		m_Message = "Connected " + from + "." + o.Name + " -> " + to.Id + "." + i.Name;
		m_MessageError = false;
	}

	void TerrainGraphUi::Draw(bool* open)
	{
		if (!ImGui::Begin("Terrain Graph", open)) { ImGui::End(); return; }
		if (ImGui::Button("Apply / Regenerate"))
		{
			if (!HasTgErrors(ValidateTerrainGraph(*m_Graph, m_Registry)))
			{
				m_ApplyRequested = true;
				m_Message = "Graph queued for regeneration";
				m_MessageError = false;
			}
			else
			{
				m_Message = "Fix graph validation errors before applying";
				m_MessageError = true;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Presets")) ImGui::OpenPopup("terrain_presets");
		if (ImGui::BeginPopup("terrain_presets"))
		{
			for (const std::string& name : TerrainGraphPresetNames())
				if (ImGui::MenuItem(name.c_str()))
				{
					TerrainGraph preset;
					if (MakeTerrainGraphPreset(name, preset)) LoadGraph(preset);
				}
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("drag an output pin to an input | right click: add | middle drag: pan | wheel: zoom | Del: delete edge");

		const std::vector<TgIssue> issues = ValidateTerrainGraph(*m_Graph, m_Registry);
		ImGui::BeginChild("terrain_graph_canvas", ImVec2(-340.0f, 0.0f), true);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 size(std::max(1.0f, available.x), std::max(1.0f, available.y));
		ImGui::InvisibleButton("##terrain_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, Add(origin, size), true);
		draw->AddRectFilled(origin, Add(origin, size), IM_COL32(22, 24, 30, 255));

		auto height = [&](const TgNode& node)
		{
			const TgNodeSchema& s = SchemaOf(node.Type);
			return std::max(kNodeBaseH, 38.0f + static_cast<float>(std::max(s.Inputs.size(), s.Outputs.size())) * kPortRow) * Zoom;
		};
		auto screen = [&](const TgNode& node) { return CanvasToScreen(origin, node.X, node.Y); };
		auto pin = [&](const TgNode& node, size_t index, bool output)
		{
			const TgNodeSchema& s = SchemaOf(node.Type);
			const size_t count = output ? s.Outputs.size() : s.Inputs.size();
			const float y = (count <= 1 ? height(node) / Zoom * 0.5f : 30.0f + static_cast<float>(index) * kPortRow) * Zoom;
			const ImVec2 p = screen(node);
			return ImVec2(p.x + (output ? kNodeW * Zoom : 0.0f), p.y + y);
		};
		// the input an edge lands on: by name, else (a version 1 edge) the next one in order among the node's unnamed edges
		auto inputIndex = [&](const TgEdge& edge, const TgNode& to)
		{
			const TgNodeSchema& s = SchemaOf(to.Type);
			const int named = s.InputIndex(edge.Port);
			if (named >= 0) return named;
			std::vector<bool> taken(s.Inputs.size(), false);
			for (const TgEdge& e : m_Graph->Edges)
				if (e.To == to.Id && s.InputIndex(e.Port) >= 0) taken[static_cast<size_t>(s.InputIndex(e.Port))] = true;
			int slot = -1;
			for (const TgEdge& e : m_Graph->Edges)
			{
				if (e.To != to.Id || s.InputIndex(e.Port) >= 0) continue;
				do { ++slot; } while (slot < static_cast<int>(taken.size()) && taken[static_cast<size_t>(slot)]);
				if (&e == &edge) return slot < static_cast<int>(taken.size()) ? slot : -1;
			}
			return -1;
		};
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		int hoveredEdge = -1;

		for (size_t edgeIndex = 0; edgeIndex < m_Graph->Edges.size(); ++edgeIndex)
		{
			const TgEdge& edge = m_Graph->Edges[edgeIndex];
			const TgNode* fromNode = m_Graph->FindNode(edge.From);
			const TgNode* toNode = m_Graph->FindNode(edge.To);
			if (!fromNode || !toNode) continue;
			const int out = OutputIndex(*fromNode, edge.FromPort), in = inputIndex(edge, *toNode);
			if (out < 0 || in < 0) continue;
			const ImVec2 from = pin(*fromNode, static_cast<size_t>(out), true), to = pin(*toNode, static_cast<size_t>(in), false);
			const float bend = std::max(40.0f, std::fabs(to.x - from.x) * 0.5f);
			if (DistanceToBezier(from, to, mouse) <= 9.0f) hoveredEdge = static_cast<int>(edgeIndex);
			const bool lit = static_cast<int>(edgeIndex) == SelEdge || edge.From == SelNode || edge.To == SelNode;
			draw->AddBezierCubic(from, ImVec2(from.x + bend, from.y), ImVec2(to.x - bend, to.y), to,
				lit ? IM_COL32(255, 220, 100, 255) : PinColor(SchemaOf(fromNode->Type).Outputs[static_cast<size_t>(out)].Kind), 2.0f);
		}
		if (!ConnectFrom.empty())
			if (const TgNode* source = m_Graph->FindNode(ConnectFrom))
			{
				const int out = OutputIndex(*source, m_ConnectPort);
				if (out >= 0) draw->AddLine(pin(*source, static_cast<size_t>(out), true), mouse, IM_COL32(255, 220, 100, 255), 2.0f);
			}

		std::set<std::string> broken;
		for (const TgIssue& issue : issues) if (issue.Error && !issue.Node.empty()) broken.insert(issue.Node);
		TgNode* hoveredNode = nullptr;
		for (TgNode& node : m_Graph->Nodes)
		{
			const TgNodeSchema& s = SchemaOf(node.Type);
			const ImVec2 p = screen(node), q(p.x + kNodeW * Zoom, p.y + height(node));
			if (mouse.x >= p.x && mouse.x <= q.x && mouse.y >= p.y && mouse.y <= q.y) hoveredNode = &node;
			draw->AddRectFilled(p, q, IM_COL32(44, 46, 54, 245), 5.0f);
			draw->AddRectFilled(p, ImVec2(q.x, p.y + 20.0f * Zoom), HeaderColor(s.Color), 5.0f, ImDrawFlags_RoundCornersTop);
			const ImU32 border = node.Id == SelNode ? IM_COL32(255, 255, 255, 255) : broken.count(node.Id) ? IM_COL32(240, 90, 80, 255) : IM_COL32(60, 60, 70, 255);
			draw->AddRect(p, q, border, 5.0f, 0, 2.0f);
			draw->AddText(ImVec2(p.x + 8.0f, p.y + 3.0f), IM_COL32_WHITE, node.Label.empty() ? node.Type.c_str() : node.Label.c_str());
			if (s.UsesSource && !node.Source.empty())
				draw->AddText(ImVec2(p.x + 8.0f, q.y - 16.0f), IM_COL32(255, 210, 120, 255), ("-> " + node.Source).c_str());
			for (size_t i = 0; i < s.Inputs.size(); ++i)
			{
				const ImVec2 at = pin(node, i, false);
				draw->AddCircleFilled(at, kPin, PinColor(s.Inputs[i].Kind));
				if (!s.Inputs[i].Optional) draw->AddCircle(at, kPin + 2.0f, IM_COL32(255, 255, 255, 120));
				draw->AddText(ImVec2(at.x + 9.0f, at.y - 7.0f), IM_COL32(220, 220, 230, 255), s.Inputs[i].Name.c_str());
			}
			for (size_t i = 0; i < s.Outputs.size(); ++i)
			{
				const ImVec2 at = pin(node, i, true);
				const ImVec2 text(at.x - ImGui::CalcTextSize(s.Outputs[i].Name.c_str()).x - 9.0f, at.y - 7.0f);
				draw->AddCircleFilled(at, kPin, PinColor(s.Outputs[i].Kind));
				draw->AddText(text, IM_COL32(220, 220, 230, 255), s.Outputs[i].Name.c_str());
			}
		}

		if (hovered)
		{
			if (ImGui::GetIO().MouseWheel != 0.0f) ZoomAt(origin, mouse, ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				if (hoveredNode)
				{
					SelNode = hoveredNode->Id;
					SelEdge = -1;
					const TgNodeSchema& s = SchemaOf(hoveredNode->Type);
					bool portHit = false;
					for (size_t i = 0; i < s.Outputs.size() && !portHit; ++i)
					{
						const ImVec2 at = pin(*hoveredNode, i, true);
						const float dx = mouse.x - at.x, dy = mouse.y - at.y;
						if (dx * dx + dy * dy <= 100.0f) { ConnectFrom = hoveredNode->Id; m_ConnectPort = s.Outputs[i].Name; portHit = true; }
					}
					for (size_t i = 0; i < s.Inputs.size() && !portHit; ++i)
					{
						const ImVec2 at = pin(*hoveredNode, i, false);
						const float dx = mouse.x - at.x, dy = mouse.y - at.y;
						if (dx * dx + dy * dy <= 100.0f) portHit = true;
					}
					if (!portHit) DragNode = hoveredNode->Id;
				}
				else if (hoveredEdge >= 0) { SelEdge = hoveredEdge; SelNode.clear(); }
				else { SelNode.clear(); SelEdge = -1; }
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !DragNode.empty())
				if (TgNode* node = m_Graph->FindNode(DragNode))
				{
					const ImVec2 delta = ImGui::GetIO().MouseDelta;
					node->X += delta.x / Zoom;
					node->Y += delta.y / Zoom;
				}
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ConnectFrom.empty())
			{
				TgNode* targetNode = nullptr;
				size_t targetIndex = 0;
				float best = 15.0f;
				for (TgNode& candidate : m_Graph->Nodes)
				{
					if (candidate.Id == ConnectFrom) continue;
					const TgNodeSchema& s = SchemaOf(candidate.Type);
					for (size_t i = 0; i < s.Inputs.size(); ++i)
					{
						const ImVec2 at = pin(candidate, i, false);
						const float d = std::sqrt((mouse.x - at.x) * (mouse.x - at.x) + (mouse.y - at.y) * (mouse.y - at.y));
						if (d <= best) { best = d; targetNode = &candidate; targetIndex = i; }
					}
				}
				if (!targetNode && hoveredNode && hoveredNode->Id != ConnectFrom)   // dropped on a node's body: its first compatible free input
				{
					const TgNode* source = m_Graph->FindNode(ConnectFrom);
					const TgNodeSchema& s = SchemaOf(hoveredNode->Type);
					const TgNodeSchema& o = SchemaOf(source ? source->Type : std::string());
					const int out = o.OutputIndex(m_ConnectPort);
					for (size_t i = 0; i < s.Inputs.size() && !targetNode && out >= 0; ++i)
						if (Compatible(o.Outputs[static_cast<size_t>(out)].Kind, s.Inputs[i].Kind)) { targetNode = hoveredNode; targetIndex = i; }
				}
				if (targetNode) Connect(ConnectFrom, m_ConnectPort, *targetNode, targetIndex);
				else { m_Message = "Drop on an input port"; m_MessageError = true; }
				ConnectFrom.clear();
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
			{
				const ImVec2 delta = ImGui::GetIO().MouseDelta;
				Pan.x += delta.x; Pan.y += delta.y;
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				PopupAt = ScreenToCanvas(origin, mouse);
				ImGui::OpenPopup("terrain_add_node");
			}
		}
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { DragNode.clear(); ConnectFrom.clear(); }
		if (SelEdge >= 0 && SelEdge < static_cast<int>(m_Graph->Edges.size()) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			m_Graph->Edges.erase(m_Graph->Edges.begin() + SelEdge);
			SelEdge = -1;
			m_Message = "Connection deleted";
			m_MessageError = false;
		}
		if (ImGui::BeginPopup("terrain_add_node"))
		{
			std::map<std::string, std::vector<std::string>> byCategory;
			for (const std::string& type : m_Registry.KnownTypes()) byCategory[SchemaOf(type).Category].push_back(type);
			for (const auto& [category, types] : byCategory)
				if (ImGui::BeginMenu(category.c_str()))
				{
					for (const std::string& type : types)
					{
						if (ImGui::MenuItem(type.c_str())) { AddNode(type, PopupAt); ImGui::CloseCurrentPopup(); }
						if (ImGui::IsItemHovered() && !SchemaOf(type).Summary.empty()) ImGui::SetTooltip("%s", SchemaOf(type).Summary.c_str());
					}
					ImGui::EndMenu();
				}
			ImGui::EndPopup();
		}
		draw->PopClipRect();
		ImGui::EndChild();
		ImGui::SameLine();
		DrawInspector(issues);
		ImGui::End();
	}

	void TerrainGraphUi::DrawInspector(const std::vector<TgIssue>& issues)
	{
		ImGui::BeginChild("terrain_graph_inspector", ImVec2(0.0f, 0.0f), true);
		if (TgNode* node = m_Graph->FindNode(SelNode))
		{
			const TgNodeSchema& s = SchemaOf(node->Type);
			ImGui::Text("%s", node->Id.c_str());
			ImGui::TextDisabled("%s  (%s)", node->Type.c_str(), s.Category.c_str());
			if (!s.Summary.empty()) ImGui::TextWrapped("%s", s.Summary.c_str());
			char label[128]; std::snprintf(label, sizeof label, "%s", node->Label.c_str());
			if (ImGui::InputText("label", label, sizeof label)) node->Label = label;
			if (s.UsesSource)
			{
				char source[64]; std::snprintf(source, sizeof source, "%s", node->Source.c_str());
				if (ImGui::InputText(node->Type == "output" ? "publishes as" : "reads", source, sizeof source)) node->Source = source;
			}
			ImGui::Separator();
			for (const TgPortInfo& port : s.Inputs)
			{
				ImGui::BulletText("in  %s (%s%s)", port.Name.c_str(), TgPortKindName(port.Kind), port.Optional ? ", optional" : "");
				if (!port.Tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", port.Tooltip.c_str());
			}
			for (const TgPortInfo& port : s.Outputs) ImGui::BulletText("out %s (%s)", port.Name.c_str(), TgPortKindName(port.Kind));
			ImGui::Separator();
			for (const TgParamInfo& p : s.Params)
			{
				double& value = node->Params.try_emplace(p.Name, p.Default).first->second;
				if (p.Integer)
				{
					int v = static_cast<int>(value);
					if (ImGui::SliderInt(p.Name.c_str(), &v, static_cast<int>(p.Min), static_cast<int>(p.Max))) value = v;
				}
				else
				{
					float v = static_cast<float>(value);
					const float span = static_cast<float>(p.Max - p.Min);
					if (ImGui::DragFloat(p.Name.c_str(), &v, span > 0.0f ? span / 400.0f : 0.01f, static_cast<float>(p.Min), static_cast<float>(p.Max), "%.4g")) value = v;
				}
				if (!p.Tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.Tooltip.c_str());
			}
			for (auto& [name, value] : node->Params)   // parameters the schema does not list (an older graph): still editable
			{
				if (s.FindParam(name)) continue;
				float v = static_cast<float>(value);
				if (ImGui::DragFloat((name + " (?)").c_str(), &v, 0.01f)) value = v;
			}
			if (ImGui::Button("Delete node"))
			{
				const std::string id = node->Id;
				m_Graph->Edges.erase(std::remove_if(m_Graph->Edges.begin(), m_Graph->Edges.end(), [&](const TgEdge& edge) { return edge.From == id || edge.To == id; }), m_Graph->Edges.end());
				m_Graph->Nodes.erase(std::remove_if(m_Graph->Nodes.begin(), m_Graph->Nodes.end(), [&](const TgNode& item) { return item.Id == id; }), m_Graph->Nodes.end());
				ResetCanvasInteraction();
			}
		}
		else ImGui::TextDisabled("Select a node");
		ImGui::Separator();
		ImGui::Text("Validation");
		if (issues.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Graph is valid");
		for (const TgIssue& issue : issues)
			ImGui::TextColored(issue.Error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(1, 0.85f, 0.4f, 1), "%s%s: %s", issue.Error ? "error " : "warning ", issue.Node.c_str(), issue.Message.c_str());
		if (!m_Message.empty()) ImGui::TextColored(m_MessageError ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.5f, 0.9f, 0.5f, 1), "%s", m_Message.c_str());
		ImGui::EndChild();
	}
}
