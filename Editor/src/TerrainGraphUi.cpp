#include "TerrainGraphUi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		constexpr float kNodeW = 190.0f, kNodeBaseH = 54.0f, kPortRow = 17.0f, kPin = 6.0f;
		ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
		ImU32 Color(const std::string& type)
		{
			if (type == "output") return IM_COL32(170, 60, 60, 255);
			if (type.find("erosion") != std::string::npos) return IM_COL32(56, 130, 84, 255);
			if (type.find("river") != std::string::npos || type.find("lake") != std::string::npos) return IM_COL32(40, 130, 130, 255);
			if (type == "heightfield_noise") return IM_COL32(86, 86, 160, 255);
			return IM_COL32(150, 140, 50, 255);
		}
		std::map<std::string, double> DefaultParams(const std::string& type)
		{
			if (type == "heightfield_noise") return { { "Octaves", 5 }, { "Frequency", 0.015 }, { "Amplitude", 20 }, { "Lacunarity", 2 }, { "Gain", 0.5 }, { "Seed", 0 } };
			if (type == "erosion_hydraulic") return { { "Droplets", 60000 }, { "Lifetime", 40 }, { "Inertia", 0.05 }, { "Capacity", 4 }, { "MinSlope", 0.01 }, { "ErodeSpeed", 0.3 }, { "DepositSpeed", 0.3 }, { "Evaporation", 0.012 }, { "Gravity", 4 }, { "BrushRadius", 2 }, { "Strength", 1 }, { "Seed", 0 } };
			if (type == "erosion_thermal") return { { "Iterations", 40 }, { "TalusAngleDegrees", 33 }, { "Strength", 0.5 } };
			if (type == "biome_classify") return { { "WaterLevel", 0 }, { "ShoreBand", 1 }, { "SnowHeight", 1e9 }, { "RockSlopeDegrees", 30 }, { "ForestMoisture", 0.5 }, { "Seed", 0 } };
			if (type == "depression_fill") return { { "Epsilon", 0.001 } };
			if (type == "lake_site") return { { "Coarse", 65 }, { "Margin", 12 } };
			if (type == "lake_basin") return { { "Coarse", 65 }, { "Margin", 12 }, { "Radius", 22 }, { "Depth", 3 }, { "LevelAbove", 1 }, { "BankSlope", 0.3 }, { "Irregularity", 0 } };
			if (type == "river_trace") return { { "MinStartDistance", 0.3 }, { "Smooth", 3 }, { "Spacing", 6 } };
			if (type == "scatter") return { { "Count", 120 }, { "Variants", 1 }, { "MinSpacing", 4 }, { "MinHeight", -1e9 }, { "MaxHeight", 1e9 }, { "MaxSlopeDegrees", 28 }, { "DensityThreshold", 0.5 }, { "Seed", 0 } };
			return {};
		}
		bool IsIntegerParameter(const std::string& name)
		{
			return name == "Octaves" || name == "Droplets" || name == "Lifetime" || name == "BrushRadius" ||
				name == "Iterations" || name == "Coarse" || name == "Margin" || name == "Smooth" ||
				name == "Count" || name == "Variants" || name == "Seed";
		}
	}

	TerrainGraphUi::TerrainGraphUi() : m_Graph(std::make_shared<TerrainGraph>()), m_Registry(MakeDefaultTerrainNodeRegistry())
	{
		m_Graph->Id = "terrain";
		m_Graph->Name = "Terrain";
		m_Graph->Nodes = {
			{ "noise", "heightfield_noise", "Base height", 40.0f, 90.0f },
			{ "erosion", "erosion_hydraulic", "Hydraulic erosion", 300.0f, 90.0f },
			{ "output", "output", "Height output", 580.0f, 90.0f, "height" }
		};
		m_Graph->Nodes[0].Params = DefaultParams("heightfield_noise");
		m_Graph->Nodes[1].Params = DefaultParams("erosion_hydraulic");
		m_Graph->Edges = { { "noise", "erosion", "height" }, { "erosion", "output", "height" } };
	}

	std::vector<TerrainPortInfo> TerrainGraphUi::InputsFor(const std::string& type) const
	{
		if (type == "height_input" || type == "heightfield_noise") return {};
		if (type == "river_trace") return { { "height", "field" }, { "flow", "field" }, { "outlets", "field" } };
		if (type == "biome_classify" || type == "scatter") return { { "height", "field" }, { "moisture", "field" } };
		if (type == "depression_fill") return { { "height", "field" }, { "outlets", "field" } };
		if (type == "output" || type == "erosion_hydraulic" || type == "erosion_thermal" || type == "flow_accumulation" ||
			type == "lake_basin" || type == "lake_site") return { { "height", "field" } };
		return { { "in", "field" } };
	}

	std::vector<TerrainPortInfo> TerrainGraphUi::OutputsFor(const std::string& type) const
	{
		if (type == "output") return {};
		if (type == "flow_accumulation" || type == "river_trace") return { { "flow", "field" } };
		if (type == "lake_site") return { { "outlets", "field" } };
		if (type == "biome_classify") return { { "moisture", "field" } };
		return { { "out", "field" } };
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
		TgNode node;
		node.Id = UniqueId(type);
		node.Type = type;
		node.Label = type;
		node.X = position.x;
		node.Y = position.y;
		node.Params = DefaultParams(type);
		if (type == "output") node.Source = "height";
		m_Graph->Nodes.push_back(std::move(node));
		m_Message = "Added " + type;
		m_MessageError = false;
	}

	bool TerrainGraphUi::ConsumeApplyRequest()
	{
		const bool requested = m_ApplyRequested;
		m_ApplyRequested = false;
		return requested;
	}

	void TerrainGraphUi::Draw(bool* open)
	{
		if (!ImGui::Begin("Terrain Graph", open)) { ImGui::End(); return; }
		ImGui::TextUnformatted("Procedural terrain graph");
		ImGui::SameLine();
		ImGui::TextDisabled("drag output to input | right click: add | middle drag: pan | wheel: zoom");
		ImGui::SameLine();
		if (ImGui::Button("Apply / Regenerate"))
		{
			const std::vector<TgIssue> applyIssues = ValidateTerrainGraph(*m_Graph, m_Registry);
			if (applyIssues.empty())
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

		const std::vector<TgIssue> issues = ValidateTerrainGraph(*m_Graph, m_Registry);
		ImGui::BeginChild("terrain_graph_canvas", ImVec2(-320.0f, 0.0f), true);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 size(std::max(1.0f, available.x), std::max(1.0f, available.y));
		ImGui::InvisibleButton("##terrain_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, Add(origin, size), true);
		draw->AddRectFilled(origin, Add(origin, size), IM_COL32(22, 24, 30, 255));

		auto inputs = [&](const TgNode& node) { return InputsFor(node.Type); };
		auto outputs = [&](const TgNode& node) { return OutputsFor(node.Type); };
		auto height = [&](const TgNode& node) { return std::max(kNodeBaseH, 38.0f + static_cast<float>(std::max(inputs(node).size(), outputs(node).size())) * kPortRow) * Zoom; };
		auto screen = [&](const TgNode& node) { return CanvasToScreen(origin, node.X, node.Y); };
		auto pin = [&](const TgNode& node, size_t index, bool output)
		{
			const size_t count = output ? outputs(node).size() : inputs(node).size();
			const float y = (count <= 1 ? height(node) / Zoom * 0.5f : 28.0f + static_cast<float>(index) * kPortRow) * Zoom;
			const ImVec2 p = screen(node);
			return ImVec2(p.x + (output ? kNodeW * Zoom : 0.0f), p.y + y);
		};
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		int hoveredEdge = -1;

		for (size_t edgeIndex = 0; edgeIndex < m_Graph->Edges.size(); ++edgeIndex)
		{
			const TgEdge& edge = m_Graph->Edges[edgeIndex];
			const TgNode* fromNode = m_Graph->FindNode(edge.From);
			const TgNode* toNode = m_Graph->FindNode(edge.To);
			if (!fromNode || !toNode || outputs(*fromNode).empty()) continue;
			size_t inputIndex = 0;
			const auto targetInputs = inputs(*toNode);
			for (size_t i = 0; i < targetInputs.size(); ++i) if (targetInputs[i].Name == edge.Port) { inputIndex = i; break; }
			const ImVec2 from = pin(*fromNode, 0, true), to = pin(*toNode, inputIndex, false);
			const float bend = std::max(40.0f, std::fabs(to.x - from.x) * 0.5f);
			const float distance = DistanceToBezier(from, to, mouse);
			if (distance <= 9.0f) hoveredEdge = static_cast<int>(edgeIndex);
			draw->AddBezierCubic(from, ImVec2(from.x + bend, from.y), ImVec2(to.x - bend, to.y), to,
				static_cast<int>(edgeIndex) == SelEdge || edge.From == SelNode || edge.To == SelNode
					? IM_COL32(255, 220, 100, 255) : IM_COL32(180, 190, 210, 220), 2.0f);
		}
		if (!ConnectFrom.empty())
		{
			if (const TgNode* source = m_Graph->FindNode(ConnectFrom))
				draw->AddLine(pin(*source, 0, true), mouse, IM_COL32(255, 220, 100, 255), 2.0f);
		}

		TgNode* hoveredNode = nullptr;
		for (TgNode& node : m_Graph->Nodes)
		{
			const ImVec2 p = screen(node), q(p.x + kNodeW * Zoom, p.y + height(node));
			if (mouse.x >= p.x && mouse.x <= q.x && mouse.y >= p.y && mouse.y <= q.y) hoveredNode = &node;
			draw->AddRectFilled(p, q, Color(node.Type), 5.0f);
			draw->AddRect(p, q, node.Id == SelNode ? IM_COL32(255, 255, 255, 255) : IM_COL32(50, 50, 60, 255), 5.0f, 0, 2.0f);
			draw->AddText(ImVec2(p.x + 8.0f, p.y + 6.0f), IM_COL32_WHITE, node.Label.empty() ? node.Type.c_str() : node.Label.c_str());
			draw->AddText(ImVec2(p.x + 8.0f, p.y + 22.0f), IM_COL32(220, 220, 220, 255), node.Type.c_str());
			const auto inPorts = inputs(node), outPorts = outputs(node);
			for (size_t i = 0; i < inPorts.size(); ++i)
			{
				const ImVec2 at = pin(node, i, false);
				draw->AddCircleFilled(at, kPin, IM_COL32(220, 220, 230, 255));
				draw->AddText(ImVec2(at.x + 9.0f, at.y - 7.0f), IM_COL32(220, 220, 230, 255), inPorts[i].Name.c_str());
			}
			for (size_t i = 0; i < outPorts.size(); ++i)
			{
				const ImVec2 at = pin(node, i, true);
				const ImVec2 text(at.x - ImGui::CalcTextSize(outPorts[i].Name.c_str()).x - 9.0f, at.y - 7.0f);
				draw->AddCircleFilled(at, kPin, IM_COL32(220, 220, 230, 255));
				draw->AddText(text, IM_COL32(220, 220, 230, 255), outPorts[i].Name.c_str());
			}
		}

		if (hovered)
		{
			if (ImGui::GetIO().MouseWheel != 0.0f) ZoomAt(origin, mouse, ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredNode)
			{
				if (hoveredEdge >= 0)
				{
					SelEdge = hoveredEdge;
					SelNode.clear();
				}
				else
				{
				SelNode = hoveredNode->Id;
				bool portHit = false;
				for (size_t i = 0; i < outputs(*hoveredNode).size(); ++i)
				{
					const ImVec2 at = pin(*hoveredNode, i, true);
					const float dx = mouse.x - at.x, dy = mouse.y - at.y;
					if (dx * dx + dy * dy <= 100.0f) { ConnectFrom = hoveredNode->Id; portHit = true; break; }
				}
				for (size_t i = 0; !portHit && i < inputs(*hoveredNode).size(); ++i)
				{
					const ImVec2 at = pin(*hoveredNode, i, false);
					const float dx = mouse.x - at.x, dy = mouse.y - at.y;
					if (dx * dx + dy * dy <= 100.0f) portHit = true;
				}
				if (!portHit) DragNode = hoveredNode->Id;
				}
			}
			else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredEdge >= 0)
			{
				SelEdge = hoveredEdge;
				SelNode.clear();
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !DragNode.empty())
			{
				if (TgNode* node = m_Graph->FindNode(DragNode))
				{
					const ImVec2 at = ScreenToCanvas(origin, mouse);
					node->X = at.x - kNodeW * 0.5f;
					node->Y = at.y - height(*node) / Zoom * 0.5f;
				}
			}
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ConnectFrom.empty())
			{
				TgNode* targetNode = nullptr;
				size_t targetIndex = 0;
				float targetDistance = 15.0f;
				for (TgNode& candidate : m_Graph->Nodes)
				{
					if (candidate.Id == ConnectFrom) continue;
					const auto targetInputs = inputs(candidate);
					for (size_t i = 0; i < targetInputs.size(); ++i)
					{
						const ImVec2 at = pin(candidate, i, false);
						const float dx = mouse.x - at.x, dy = mouse.y - at.y;
						const float distance = std::sqrt(dx * dx + dy * dy);
						if (distance <= targetDistance) { targetDistance = distance; targetNode = &candidate; targetIndex = i; }
					}
				}
				if (!targetNode)
				{
					for (TgNode& candidate : m_Graph->Nodes)
					{
						if (candidate.Id == ConnectFrom || inputs(candidate).size() != 1) continue;
						const ImVec2 topLeft = screen(candidate);
						const ImVec2 bottomRight(topLeft.x + kNodeW * Zoom, topLeft.y + height(candidate));
						if (mouse.x >= topLeft.x && mouse.x <= bottomRight.x && mouse.y >= topLeft.y && mouse.y <= bottomRight.y)
						{
							targetNode = &candidate;
							targetIndex = 0;
							break;
						}
					}
				}
				if (targetNode)
				{
					const auto targetInputs = inputs(*targetNode);
					if (targetIndex < targetInputs.size())
					{
						const std::string port = targetInputs[targetIndex].Name;
						const TgNode* sourceNode = m_Graph->FindNode(ConnectFrom);
						const auto sourceOutputs = sourceNode ? outputs(*sourceNode) : std::vector<TerrainPortInfo>();
						const bool compatible = sourceOutputs.empty() || sourceOutputs[0].Name == "out" ||
							sourceOutputs[0].Name == port;
						if (!compatible)
						{
							m_Message = "Incompatible ports: " + sourceOutputs[0].Name + " -> " + port;
							m_MessageError = true;
							ConnectFrom.clear();
						}
						else
						{
							const bool duplicate = std::any_of(m_Graph->Edges.begin(), m_Graph->Edges.end(), [&](const TgEdge& edge)
							{
								return edge.To == targetNode->Id && edge.Port == port;
							});
							if (!duplicate)
							{
								const auto insertAt = std::find_if(m_Graph->Edges.begin(), m_Graph->Edges.end(), [&](const TgEdge& edge)
								{
									if (edge.To != targetNode->Id) return false;
									const auto position = std::find_if(targetInputs.begin(), targetInputs.end(), [&](const TerrainPortInfo& candidate)
									{
										return candidate.Name == edge.Port;
									});
									return position != targetInputs.end() && static_cast<size_t>(position - targetInputs.begin()) > targetIndex;
								});
								m_Graph->Edges.insert(insertAt, { ConnectFrom, targetNode->Id, port });
								m_Message = "Connected " + ConnectFrom + " -> " + targetNode->Id + "." + port;
								m_MessageError = false;
							}
							else
							{
								for (TgEdge& edge : m_Graph->Edges)
									if (edge.To == targetNode->Id && edge.Port == port) edge.From = ConnectFrom;
								m_Message = "Replaced connection to " + targetNode->Id + "." + port;
								m_MessageError = false;
							}
						}
					}
				}
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
			if (SelEdge >= 0 && SelEdge < static_cast<int>(m_Graph->Edges.size()) &&
				ImGui::IsKeyPressed(ImGuiKey_Delete))
			{
				m_Graph->Edges.erase(m_Graph->Edges.begin() + SelEdge);
				SelEdge = -1;
				m_Message = "Connection deleted";
				m_MessageError = false;
			}
		}
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			DragNode.clear();
			if (!ConnectFrom.empty())
			{
				ConnectFrom.clear();
				m_Message = "Connection cancelled";
				m_MessageError = true;
			}
		}
		if (SelEdge >= 0 && SelEdge < static_cast<int>(m_Graph->Edges.size()) &&
			ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			m_Graph->Edges.erase(m_Graph->Edges.begin() + SelEdge);
			SelEdge = -1;
			m_Message = "Connection deleted";
			m_MessageError = false;
		}
		if (ImGui::BeginPopup("terrain_add_node"))
		{
			for (const std::string& type : m_Registry.KnownTypes())
				if (ImGui::MenuItem(type.c_str())) { AddNode(type, PopupAt); ImGui::CloseCurrentPopup(); }
			ImGui::EndPopup();
		}
		draw->PopClipRect();
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("terrain_graph_inspector", ImVec2(0.0f, 0.0f), true);
		if (TgNode* node = m_Graph->FindNode(SelNode))
		{
			ImGui::Text("Node: %s", node->Id.c_str());
			ImGui::TextDisabled("%s", node->Type.c_str());
			char label[128]; std::snprintf(label, sizeof label, "%s", node->Label.c_str());
			if (ImGui::InputText("label", label, sizeof label)) node->Label = label;
			if (node->Type == "output")
			{
				char source[64]; std::snprintf(source, sizeof source, "%s", node->Source.c_str());
				if (ImGui::InputText("output", source, sizeof source)) node->Source = source;
			}
			ImGui::Separator();
			ImGui::TextUnformatted("Inputs");
			for (const TerrainPortInfo& port : InputsFor(node->Type)) ImGui::BulletText("%s (%s)", port.Name.c_str(), port.Type.c_str());
			ImGui::TextUnformatted("Outputs");
			for (const TerrainPortInfo& port : OutputsFor(node->Type)) ImGui::BulletText("%s (%s)", port.Name.c_str(), port.Type.c_str());
			for (auto& [name, value] : node->Params)
			{
				float v = static_cast<float>(value);
				if (IsIntegerParameter(name))
				{
					int integerValue = static_cast<int>(value);
					if (ImGui::DragInt(name.c_str(), &integerValue, 1.0f)) value = integerValue;
				}
				else if (ImGui::DragFloat(name.c_str(), &v, 0.01f)) value = v;
			}
			if (ImGui::Button("Delete node"))
			{
				const std::string id = node->Id;
				m_Graph->Edges.erase(std::remove_if(m_Graph->Edges.begin(), m_Graph->Edges.end(), [&](const TgEdge& edge) { return edge.From == id || edge.To == id; }), m_Graph->Edges.end());
				m_Graph->Nodes.erase(std::remove_if(m_Graph->Nodes.begin(), m_Graph->Nodes.end(), [&](const TgNode& item) { return item.Id == id; }), m_Graph->Nodes.end());
				ResetCanvasInteraction();
			}
		}
		ImGui::Separator();
		ImGui::Text("Validation");
		if (issues.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Graph is valid");
		for (const TgIssue& issue : issues) ImGui::TextColored(issue.Error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(1, 0.85f, 0.4f, 1), "%s%s: %s", issue.Error ? "error " : "warning ", issue.Node.c_str(), issue.Message.c_str());
		if (!m_Message.empty()) ImGui::TextColored(m_MessageError ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.5f, 0.9f, 0.5f, 1), "%s", m_Message.c_str());
		ImGui::EndChild();
		ImGui::End();
	}
}
