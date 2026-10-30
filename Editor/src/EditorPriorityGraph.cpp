// The "Priority Graph" window: a canvas of nodes (inputs, curves, combiners, outputs, included graphs) that scores the options of a character, a library of graphs
// (save, duplicate, templates, import / export), their assignment to roles and characters, and a live preview with a "what if" mode. The editor only draws: the model,
// the JSON, the validation and the evaluation are in GB/AI/PriorityGraph.* and GB/AI/PriorityLibrary.* (headless, tested), and the bridge to the tribe in GB/Game/TribePriorities.*.
// How to use it: docs/GRAFO_PRIORIDADES.md.
#include "EditorApp.h"
#include "GB/AI/PriorityLibrary.h"
#include "GB/Game/TribePriorities.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <map>
#include <memory>
#include <set>

namespace GB::Editor
{
	struct PriorityGraphUi
	{
		std::shared_ptr<PriorityLibrary> Lib = std::make_shared<PriorityLibrary>();
		bool Loaded = false;
		std::string Dir, Current = "builtin", SelNode, Message;
		bool MessageError = false;
		int SelEdge = -1;
		ImVec2 Pan{ 40.0f, 40.0f };
		float Zoom = 1.0f;
		std::string DragNode, ConnectFrom;
		bool Panning = false;
		ImVec2 PopupAt{ 0.0f, 0.0f };
		bool WhatIf = false;
		std::map<std::string, float> Over;
		float WhatScore = 1.0f;
		int ShownOption = -1;
		char NewId[64] = "";
		char RoleName[64] = "";
		char ImportText[16384] = "";
		std::string ExportText;
	};

	namespace
	{
		ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
		ImVec2 Sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
		ImVec2 Mul(ImVec2 a, float s) { return ImVec2(a.x * s, a.y * s); }

		constexpr float kNodeW = 150.0f, kNodeH = 46.0f, kPin = 5.0f;

		ImU32 TypeColor(const std::string& type)
		{
			if (type == "input") return IM_COL32(52, 96, 160, 255);
			if (type == "const") return IM_COL32(86, 86, 96, 255);
			if (type == "curve") return IM_COL32(56, 130, 84, 255);
			if (type == "combine") return IM_COL32(170, 110, 40, 255);
			if (type == "clamp") return IM_COL32(40, 130, 130, 255);
			if (type == "output") return IM_COL32(170, 60, 60, 255);
			if (type == "subgraph") return IM_COL32(130, 76, 160, 255);
			return IM_COL32(150, 140, 50, 255);   // param, return
		}

		bool HasInPin(const PgNode& n) { return n.Type != "input" && n.Type != "const"; }
		bool HasOutPin(const PgNode& n) { return n.Type != "output" && n.Type != "return"; }

		std::string Title(const PgNode& n)
		{
			if (!n.Label.empty()) return n.Label;
			if (n.Type == "input") return n.Source;
			if (n.Type == "output") return "-> " + n.Source;
			if (n.Type == "subgraph") return "[" + n.Source + "]";
			if (n.Type == "curve" || n.Type == "combine") return n.Fn;
			if (n.Type == "param" || n.Type == "return") return n.Type + " " + n.Id;
			return n.Type;
		}

		std::string Subtitle(const PgNode& n)
		{
			char b[96] = "";
			auto p = [&](const char* k, double d) { const auto it = n.Params.find(k); return it == n.Params.end() ? d : it->second; };
			if (n.Type == "const") std::snprintf(b, sizeof b, "%g", p("value", 0));
			else if (n.Type == "output") std::snprintf(b, sizeof b, "%s", n.Fn.empty() ? "replace" : n.Fn.c_str());
			else if (n.Type == "curve") std::snprintf(b, sizeof b, "curve");
			else if (n.Type == "combine") std::snprintf(b, sizeof b, "combine");
			else std::snprintf(b, sizeof b, "%s", n.Type.c_str());
			return b;
		}

		// the parameters each curve reads, with their defaults (the same as EvaluatePriorityGraph)
		std::vector<std::pair<const char*, double>> CurveParams(const std::string& fn)
		{
			if (fn == "linear") return { { "a", 1.0 }, { "b", 0.0 } };
			if (fn == "ramp" || fn == "inverse") return { { "lo", 0.0 }, { "hi", 1.0 } };
			if (fn == "sigmoid") return { { "k", 10.0 }, { "mid", 0.5 } };
			if (fn == "threshold") return { { "at", 0.5 }, { "low", 0.0 }, { "high", 1.0 } };
			if (fn == "urgency") return { { "from", 60.0 }, { "power", 1.0 } };
			if (fn == "falloff") return { { "half", 30.0 } };
			return {};
		}

		void Range(const std::string& input, float& lo, float& hi)
		{
			lo = 0.0f; hi = 3.0f;
			for (const PgInputInfo& i : PriorityInputCatalog())
				if (i.Name == input) { lo = i.Min; hi = i.Max; return; }
			if (input.rfind("store.", 0) == 0 || input.rfind("skill.", 0) == 0) hi = 100.0f;
			if (input.rfind("trait.", 0) == 0) hi = 1.0f;
		}

		bool Combo(const char* label, std::string& value, const std::vector<std::string>& items)
		{
			bool changed = false;
			if (ImGui::BeginCombo(label, value.c_str()))
			{
				for (const std::string& s : items)
					if (ImGui::Selectable(s.c_str(), s == value)) { value = s; changed = true; }
				ImGui::EndCombo();
			}
			return changed;
		}

		std::string UniqueNodeId(const PriorityGraph& g, const std::string& base)
		{
			std::string id = base;
			for (int i = 2; g.FindNode(id); ++i) id = base + std::to_string(i);
			return id;
		}

		float DistanceToCurve(ImVec2 a, ImVec2 b, ImVec2 p)
		{
			const float dx = std::max(40.0f, std::fabs(b.x - a.x) * 0.5f);
			const ImVec2 c1(a.x + dx, a.y), c2(b.x - dx, b.y);
			float best = 1e9f;
			ImVec2 prev = a;
			for (int i = 1; i <= 24; ++i)
			{
				const float t = static_cast<float>(i) / 24.0f, u = 1.0f - t;
				const ImVec2 q(u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x, u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y);
				const float vx = q.x - prev.x, vy = q.y - prev.y, len2 = vx * vx + vy * vy;
				float s = len2 > 0 ? ((p.x - prev.x) * vx + (p.y - prev.y) * vy) / len2 : 0.0f;
				s = std::clamp(s, 0.0f, 1.0f);
				const float ex = prev.x + vx * s - p.x, ey = prev.y + vy * s - p.y;
				best = std::min(best, std::sqrt(ex * ex + ey * ey));
				prev = q;
			}
			return best;
		}

		void Say(PriorityGraphUi& ui, const std::string& text, bool error = false)
		{
			ui.Message = text;
			ui.MessageError = error && !text.empty();
		}
		// runs an operation of the library that returns why it failed
		void Report(PriorityGraphUi& ui, const std::string& error, const std::string& okText)
		{
			if (error.empty()) Say(ui, okText); else Say(ui, error, true);
		}
	}

	void EditorApp::DrawPriorityGraphPanel()
	{
		if (!m_PgUi) m_PgUi = std::make_shared<PriorityGraphUi>();
		PriorityGraphUi& ui = *m_PgUi;
		PriorityLibrary& lib = *ui.Lib;
		if (!ui.Loaded)
		{
			ui.Loaded = true;
			ui.Dir = std::string(GB_ASSET_DIR) + "/data/priority_graphs";
			const std::string e = lib.LoadDirectory(ui.Dir);
			if (!e.empty()) Say(ui, e, true);
		}
		Tribe& tribe = ActiveTribe();
		if (tribe.Priorities != ui.Lib) tribe.Priorities = ui.Lib;   // the library takes effect on the tribe being looked at (builtin changes nothing)
		if (m_Tribe.Priorities != ui.Lib) m_Tribe.Priorities = ui.Lib;

		ImGui::SetNextWindowSize(ImVec2(1180.0f, 680.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Priority Graph", &m_ShowPriorityGraph)) { ImGui::End(); return; }

		const TribeMember* who = nullptr;
		for (const TribeMember& m : tribe.Members()) if (m.Name == m_AbilityActor) who = &m;

		// ---------------------------------------------------------------- left: library, assignments, messages
		ImGui::BeginChild("pg_left", ImVec2(250.0f, 0.0f), true);
		ImGui::TextUnformatted("Library");
		ImGui::BeginChild("pg_list", ImVec2(0.0f, 170.0f), true);
		for (const std::string& id : lib.Ids())
		{
			const PriorityGraph* g = lib.Find(id);
			const bool fragment = std::any_of(g->Nodes.begin(), g->Nodes.end(), [](const PgNode& n) { return n.Type == "return"; });
			std::string label = id + (g->Template ? "  [template]" : fragment ? "  [fragment]" : "");
			const std::string used = lib.IdFor(who ? who->Role : "", who ? who->Name : "");
			if (id == used) label += "  *";
			if (ImGui::Selectable(label.c_str(), id == ui.Current)) { ui.Current = id; ui.SelNode.clear(); ui.SelEdge = -1; }
			if (ImGui::IsItemHovered() && !g->Description.empty()) ImGui::SetTooltip("%s", g->Description.c_str());
		}
		ImGui::EndChild();
		ImGui::TextDisabled("* = the one the selected character uses");
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##newid", "id for new / copy", ui.NewId, sizeof ui.NewId);
		const std::string newId = ui.NewId;
		if (ImGui::Button("New")) { Report(ui, lib.Duplicate("builtin", newId), "created '" + newId + "' from builtin"); if (lib.Find(newId)) ui.Current = newId; }
		ImGui::SameLine();
		if (ImGui::Button("Duplicate")) { Report(ui, lib.Duplicate(ui.Current, newId), "duplicated into '" + newId + "'"); if (lib.Find(newId)) ui.Current = newId; }
		ImGui::SameLine();
		if (ImGui::Button("Rename")) { Report(ui, lib.Rename(ui.Current, newId), "renamed"); if (lib.Find(newId)) ui.Current = newId; }
		if (ImGui::Button("As template")) { Report(ui, lib.SaveAsTemplate(ui.Current, newId), "template '" + newId + "' saved in the library"); }
		ImGui::SameLine();
		if (ImGui::Button("From template")) { Report(ui, lib.Instantiate(ui.Current, newId), "graph '" + newId + "' created from the template"); if (lib.Find(newId)) ui.Current = newId; }
		if (ImGui::Button("Delete")) { const std::string e = lib.Remove(ui.Current); Report(ui, e, "deleted"); if (e.empty()) ui.Current = "builtin"; }
		ImGui::SameLine();
		if (ImGui::Button("Export")) { std::string json; Report(ui, lib.ExportJson(ui.Current, json), "exported: copy the text of the window"); ui.ExportText = json; ImGui::OpenPopup("pg_export"); }
		ImGui::SameLine();
		if (ImGui::Button("Import")) ImGui::OpenPopup("pg_import");
		if (ImGui::Button("Save graph"))
		{
			if (PriorityGraph* g = lib.Edit(ui.Current)) ++g->Revision;
			Report(ui, lib.SaveGraph(ui.Dir, ui.Current), "saved " + ui.Dir + "/" + ui.Current + ".json");
		}
		ImGui::SameLine();
		if (ImGui::Button("Save all")) Report(ui, lib.SaveDirectory(ui.Dir), "saved the library and the assignments in " + ui.Dir);
		if (ImGui::Button("Reload folder")) { const std::string e = lib.LoadDirectory(ui.Dir); Report(ui, e, "reloaded " + ui.Dir); }
		if (ImGui::BeginPopup("pg_export"))
		{
			ImGui::InputTextMultiline("##exp", ui.ExportText.data(), ui.ExportText.size() + 1, ImVec2(520.0f, 300.0f), ImGuiInputTextFlags_ReadOnly);
			if (ImGui::Button("Copy to the clipboard")) ImGui::SetClipboardText(ui.ExportText.c_str());
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("pg_import"))
		{
			ImGui::TextUnformatted("Paste the JSON of a graph:");
			ImGui::InputTextMultiline("##imp", ui.ImportText, sizeof ui.ImportText, ImVec2(520.0f, 300.0f));
			static bool overwrite = false;
			ImGui::Checkbox("Overwrite if the id exists", &overwrite);
			if (ImGui::Button("Import"))
			{
				std::string id;
				const std::string e = lib.ImportJson(ui.ImportText, overwrite, &id);
				Report(ui, e, "imported '" + id + "'");
				if (e.empty()) { ui.Current = id; ImGui::CloseCurrentPopup(); }
			}
			ImGui::EndPopup();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Who uses it");
		if (ImGui::Button("Default")) Report(ui, lib.AssignDefault(ui.Current), "everyone without their own graph uses '" + ui.Current + "'");
		if (who)
		{
			ImGui::SameLine();
			if (ImGui::Button(("Character " + who->Name).c_str())) Report(ui, lib.AssignMember(who->Name, ui.Current), who->Name + " uses '" + ui.Current + "'");
			if (ImGui::Button(("Role " + who->Role).c_str())) Report(ui, lib.AssignRole(who->Role, ui.Current), "role " + who->Role + " uses '" + ui.Current + "'");
		}
		else ImGui::TextDisabled("(select a character in the Tribe panel to assign to it or to its role)");
		ImGui::SetNextItemWidth(120.0f);
		ImGui::InputTextWithHint("##role", "role", ui.RoleName, sizeof ui.RoleName);
		ImGui::SameLine();
		if (ImGui::Button("Assign to role") && ui.RoleName[0]) Report(ui, lib.AssignRole(ui.RoleName, ui.Current), std::string("role ") + ui.RoleName + " uses '" + ui.Current + "'");
		ImGui::TextDisabled("default: %s", lib.DefaultGraph.empty() ? "(none)" : lib.DefaultGraph.c_str());
		std::string clearRole, clearMember;
		for (const auto& [role, id] : lib.Roles) { ImGui::Text("role %s: %s", role.c_str(), id.c_str()); ImGui::SameLine(); if (ImGui::SmallButton(("x##r" + role).c_str())) clearRole = role; }
		for (const auto& [member, id] : lib.Members) { ImGui::Text("%s: %s", member.c_str(), id.c_str()); ImGui::SameLine(); if (ImGui::SmallButton(("x##m" + member).c_str())) clearMember = member; }
		if (!clearRole.empty()) lib.AssignRole(clearRole, "");
		if (!clearMember.empty()) lib.AssignMember(clearMember, "");

		ImGui::Separator();
		if (!ui.Message.empty()) ImGui::TextColored(ui.MessageError ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s", ui.Message.c_str());
		ImGui::EndChild();
		ImGui::SameLine();

		PriorityGraph* graph = lib.Edit(ui.Current);   // (clears the compiled graphs: they are rebuilt below)
		if (!graph) { ImGui::TextUnformatted("No graph selected."); ImGui::End(); return; }

		CompiledPriorityGraph compiled;
		std::vector<PgIssue> issues;
		const bool valid = CompilePriorityGraph(*graph, lib.Resolver(), compiled, &issues);

		// the numbers on the nodes: the "what if" inputs, or the option of the character picked below
		std::map<std::string, float> nodeValues;
		if (valid)
		{
			PgInputs inputs;
			bool have = false;
			if (ui.WhatIf)
			{
				inputs.Values = ui.Over;
				inputs.Values["option.score"] = ui.WhatScore;
				inputs.Resolver = [](const std::string& n, float& out) { if (n.rfind("factor.", 0) == 0 || n.rfind("store.", 0) == 0 || n.rfind("skill.", 0) == 0 || n.rfind("trait.", 0) == 0) { out = 0.0f; return true; } return false; };
				have = true;
			}
			else if (who && ui.ShownOption >= 0 && ui.ShownOption < static_cast<int>(who->Mind.Options.size()))
			{
				UtilityOption o = who->Mind.Options[static_cast<size_t>(ui.ShownOption)];
				for (const ScoreFactor& f : o.Factors) if (f.Name == "before graph") { o.Score = f.Value; break; }
				inputs = PriorityInputsFor(tribe, *who, &o);
				have = true;
			}
			if (have)
			{
				const PgResult r = EvaluatePriorityGraph(compiled, inputs);
				for (size_t i = 0; i < compiled.Nodes.size(); ++i) nodeValues[compiled.Nodes[i].Id] = r.Values[i];
			}
		}

		// ---------------------------------------------------------------- centre: the canvas
		const float rightWidth = 320.0f;
		ImGui::BeginChild("pg_canvas_box", ImVec2(-rightWidth - 8.0f, 0.0f), false);
		ImGui::Text("%s  (%s)  v%d rev %d%s", graph->Name.empty() ? graph->Id.c_str() : graph->Name.c_str(), graph->Id.c_str(), graph->Version, graph->Revision, graph->Template ? "  template" : "");
		ImGui::SameLine();
		ImGui::TextDisabled("  wheel: zoom | drag empty space: pan | drag a node | drag from a right pin to a node: connect | right click: add | Del: delete");
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float canvasH = std::max(200.0f, ImGui::GetContentRegionAvail().y - 140.0f);
		const ImVec2 size(ImGui::GetContentRegionAvail().x, canvasH);
		ImGui::InvisibleButton("##pg_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, Add(origin, size), true);
		draw->AddRectFilled(origin, Add(origin, size), IM_COL32(22, 24, 30, 255));
		ImGuiIO& io = ImGui::GetIO();
		const float zoom = ui.Zoom;
		auto toScreen = [&](float x, float y) { return Add(Add(origin, ui.Pan), ImVec2(x * zoom, y * zoom)); };
		auto nodeRect = [&](const PgNode& n, ImVec2& a, ImVec2& b) { a = toScreen(n.X, n.Y); b = Add(a, ImVec2(kNodeW * zoom, kNodeH * zoom)); };
		auto inPin = [&](const PgNode& n) { ImVec2 a, b; nodeRect(n, a, b); return ImVec2(a.x, (a.y + b.y) * 0.5f); };
		auto outPin = [&](const PgNode& n) { ImVec2 a, b; nodeRect(n, a, b); return ImVec2(b.x, (a.y + b.y) * 0.5f); };

		// grid
		{
			const float step = 40.0f * zoom;
			const float ox = std::fmod(ui.Pan.x, step), oy = std::fmod(ui.Pan.y, step);
			for (float x = ox; x < size.x; x += step) draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + size.y), IM_COL32(255, 255, 255, 12));
			for (float y = oy; y < size.y; y += step) draw->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x + size.x, origin.y + y), IM_COL32(255, 255, 255, 12));
		}

		// input
		const ImVec2 mouse = io.MousePos;
		auto nodeAt = [&](ImVec2 p) -> PgNode*
		{
			for (auto it = graph->Nodes.rbegin(); it != graph->Nodes.rend(); ++it)
			{
				ImVec2 a, b;
				nodeRect(*it, a, b);
				if (p.x >= a.x - kPin * zoom && p.x <= b.x + kPin * zoom && p.y >= a.y && p.y <= b.y) return &*it;
			}
			return nullptr;
		};
		if (hovered)
		{
			if (io.MouseWheel != 0.0f)
			{
				const float old = ui.Zoom;
				ui.Zoom = std::clamp(ui.Zoom * (io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f), 0.35f, 2.5f);
				const ImVec2 rel = Sub(Sub(mouse, origin), ui.Pan);   // keep the point under the mouse where it is
				ui.Pan = Sub(ui.Pan, Mul(rel, ui.Zoom / old - 1.0f));
			}
			if (ImGui::IsMouseClicked(0))
			{
				PgNode* n = nodeAt(mouse);
				ui.SelEdge = -1;
				if (n && HasOutPin(*n))
				{
					const ImVec2 pin = outPin(*n);
					const float dx = mouse.x - pin.x, dy = mouse.y - pin.y;
					if (dx * dx + dy * dy <= (kPin * zoom + 4.0f) * (kPin * zoom + 4.0f)) ui.ConnectFrom = n->Id;
				}
				if (ui.ConnectFrom.empty())
				{
					if (n) { ui.SelNode = n->Id; ui.DragNode = n->Id; }
					else
					{
						int hit = -1;
						for (size_t i = 0; i < graph->Edges.size(); ++i)
						{
							const PgNode* a = graph->FindNode(graph->Edges[i].From);
							const PgNode* b = graph->FindNode(graph->Edges[i].To);
							if (a && b && DistanceToCurve(outPin(*a), inPin(*b), mouse) < 6.0f) { hit = static_cast<int>(i); break; }
						}
						ui.SelEdge = hit;
						if (hit >= 0) ui.SelNode.clear(); else { ui.SelNode.clear(); ui.Panning = true; }
					}
				}
			}
			if (ImGui::IsMouseClicked(2)) ui.Panning = true;
			if (ImGui::IsMouseClicked(1) && !nodeAt(mouse))
			{
				ui.PopupAt = mouse;
				ImGui::OpenPopup("pg_add");
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::IsAnyItemActive())
			{
				if (!ui.SelNode.empty())
				{
					const std::string id = ui.SelNode;
					graph->Nodes.erase(std::remove_if(graph->Nodes.begin(), graph->Nodes.end(), [&](const PgNode& n) { return n.Id == id; }), graph->Nodes.end());
					graph->Edges.erase(std::remove_if(graph->Edges.begin(), graph->Edges.end(), [&](const PgEdge& e) { return e.From == id || e.To == id; }), graph->Edges.end());
					ui.SelNode.clear();
				}
				else if (ui.SelEdge >= 0 && ui.SelEdge < static_cast<int>(graph->Edges.size()))
				{
					graph->Edges.erase(graph->Edges.begin() + ui.SelEdge);
					ui.SelEdge = -1;
				}
			}
		}
		if (ImGui::IsMouseDown(0) && !ui.DragNode.empty())
		{
			if (PgNode* n = graph->FindNode(ui.DragNode)) { n->X += io.MouseDelta.x / zoom; n->Y += io.MouseDelta.y / zoom; }
		}
		if (ui.Panning && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(2))) ui.Pan = Add(ui.Pan, ImVec2(io.MouseDelta.x, io.MouseDelta.y));
		if (!ImGui::IsMouseDown(0)) { ui.DragNode.clear(); }
		if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2)) ui.Panning = false;
		if (ImGui::IsMouseReleased(0) && !ui.ConnectFrom.empty())
		{
			PgNode* target = nodeAt(mouse);
			if (target && target->Id != ui.ConnectFrom && HasInPin(*target))
			{
				const bool dup = std::any_of(graph->Edges.begin(), graph->Edges.end(), [&](const PgEdge& e) { return e.From == ui.ConnectFrom && e.To == target->Id; });
				if (dup) Say(ui, "those two nodes are already connected", true);
				else
				{
					PgEdge e;
					e.From = ui.ConnectFrom;
					e.To = target->Id;
					if (target->Type == "subgraph")
						if (const PriorityGraph* child = lib.Find(target->Source))
							for (const PgNode& c : child->Nodes) if (c.Type == "param") { e.Port = c.Id; break; }
					graph->Edges.push_back(e);
					ui.SelEdge = static_cast<int>(graph->Edges.size()) - 1;
					ui.SelNode.clear();
				}
			}
			ui.ConnectFrom.clear();
		}

		// edges
		for (size_t i = 0; i < graph->Edges.size(); ++i)
		{
			const PgEdge& e = graph->Edges[i];
			const PgNode* a = graph->FindNode(e.From);
			const PgNode* b = graph->FindNode(e.To);
			if (!a || !b) continue;
			const ImVec2 p1 = outPin(*a), p2 = inPin(*b);
			const float dx = std::max(40.0f, std::fabs(p2.x - p1.x) * 0.5f);
			const bool sel = static_cast<int>(i) == ui.SelEdge;
			draw->AddBezierCubic(p1, ImVec2(p1.x + dx, p1.y), ImVec2(p2.x - dx, p2.y), p2, sel ? IM_COL32(255, 220, 90, 255) : IM_COL32(170, 180, 200, 200), sel ? 3.0f : 2.0f);
			const ImVec2 mid = Mul(Add(p1, p2), 0.5f);
			char w[48];
			if (b->Type == "subgraph") std::snprintf(w, sizeof w, "%s x%g", e.Port.c_str(), e.Weight); else std::snprintf(w, sizeof w, "x%g", e.Weight);
			const ImVec2 ts = ImGui::CalcTextSize(w);
			draw->AddRectFilled(Sub(mid, ImVec2(2, 1)), Add(mid, ImVec2(ts.x + 2, ts.y + 1)), IM_COL32(22, 24, 30, 220));
			draw->AddText(mid, IM_COL32(220, 220, 230, 255), w);
		}
		if (!ui.ConnectFrom.empty())
			if (const PgNode* a = graph->FindNode(ui.ConnectFrom))
			{
				const ImVec2 p1 = outPin(*a);
				draw->AddBezierCubic(p1, ImVec2(p1.x + 60, p1.y), ImVec2(mouse.x - 60, mouse.y), mouse, IM_COL32(255, 220, 90, 255), 2.0f);
			}

		// nodes
		for (const PgNode& n : graph->Nodes)
		{
			ImVec2 a, b;
			nodeRect(n, a, b);
			const bool sel = n.Id == ui.SelNode;
			draw->AddRectFilled(a, b, IM_COL32(40, 43, 52, 255), 6.0f * zoom);
			draw->AddRectFilled(a, ImVec2(b.x, a.y + 16.0f * zoom), TypeColor(n.Type), 6.0f * zoom, ImDrawFlags_RoundCornersTop);
			draw->AddRect(a, b, sel ? IM_COL32(255, 220, 90, 255) : IM_COL32(110, 115, 130, 255), 6.0f * zoom, 0, sel ? 2.5f : 1.0f);
			if (zoom > 0.55f)
			{
				const std::string t = Title(n);
				draw->AddText(ImVec2(a.x + 6.0f * zoom, a.y + 1.0f), IM_COL32(245, 245, 250, 255), t.substr(0, 22).c_str());
				std::string sub = Subtitle(n);
				const auto v = nodeValues.find(n.Id);
				if (v != nodeValues.end()) { char vb[40]; std::snprintf(vb, sizeof vb, "  = %.3f", static_cast<double>(v->second)); sub += vb; }
				draw->AddText(ImVec2(a.x + 6.0f * zoom, a.y + 22.0f * zoom), v != nodeValues.end() ? IM_COL32(150, 230, 160, 255) : IM_COL32(170, 175, 190, 255), sub.c_str());
			}
			if (HasInPin(n)) draw->AddCircleFilled(inPin(n), kPin * zoom, IM_COL32(200, 205, 220, 255));
			if (HasOutPin(n)) draw->AddCircleFilled(outPin(n), kPin * zoom, IM_COL32(120, 200, 255, 255));
		}
		draw->PopClipRect();

		if (ImGui::BeginPopup("pg_add"))
		{
			const ImVec2 at = Sub(Sub(ui.PopupAt, origin), ui.Pan);
			auto place = [&](const char* idBase, const char* type, const std::string& source, const std::string& fn, std::map<std::string, double> params = {})
			{
				PgNode n;
				n.Id = UniqueNodeId(*graph, idBase);
				n.Type = type; n.Source = source; n.Fn = fn; n.Params = std::move(params);
				n.X = std::round(at.x / zoom / 10.0f) * 10.0f;
				n.Y = std::round(at.y / zoom / 10.0f) * 10.0f;
				graph->Nodes.push_back(n);
				ui.SelNode = n.Id;
				ImGui::CloseCurrentPopup();
			};
			if (ImGui::BeginMenu("Input"))
			{
				std::string last;
				for (const PgInputInfo& i : PriorityInputCatalog())
				{
					if (i.Group != last) { if (!last.empty()) ImGui::Separator(); ImGui::TextDisabled("%s", i.Group.c_str()); last = i.Group; }
					if (ImGui::MenuItem(i.Name.c_str())) place("in", "input", i.Name, "");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", i.Help.c_str());
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Constant")) place("const", "const", "", "", { { "value", 1.0 } });
			if (ImGui::BeginMenu("Curve"))
			{
				for (const std::string& fn : PriorityFunctions("curve"))
					if (ImGui::MenuItem(fn.c_str())) { std::map<std::string, double> p; for (const auto& [k, d] : CurveParams(fn)) p[k] = d; place("curve", "curve", "", fn, p); }
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Combine"))
			{
				for (const std::string& fn : PriorityFunctions("combine")) if (ImGui::MenuItem(fn.c_str())) place(fn.c_str(), "combine", "", fn);
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Clamp")) place("clamp", "clamp", "", "", { { "lo", 0.0 }, { "hi", 1.0 } });
			if (ImGui::BeginMenu("Output (an option)"))
			{
				if (ImGui::MenuItem("* (any option)")) place("out", "output", "*", "replace");
				for (const std::string& k : PriorityKnownKinds()) if (ImGui::MenuItem(k.c_str())) place("out", "output", k, "replace");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Include a graph"))
			{
				for (const std::string& id : lib.Ids())
				{
					const PriorityGraph* g = lib.Find(id);
					if (id != ui.Current && std::any_of(g->Nodes.begin(), g->Nodes.end(), [](const PgNode& n) { return n.Type == "return"; }) && ImGui::MenuItem(id.c_str())) place("sub", "subgraph", id, "");
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Parameter (for a fragment)")) place("param", "param", "", "", { { "default", 0.0 } });
			if (ImGui::MenuItem("Return (for a fragment)")) place("return", "return", "", "");
			ImGui::EndPopup();
		}

		// problems, below the canvas
		ImGui::BeginChild("pg_issues", ImVec2(0.0f, 0.0f), true);
		if (issues.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "The graph is valid.");
		for (const PgIssue& i : issues)
		{
			ImGui::TextColored(i.Error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s%s%s", i.Error ? "error: " : "warning: ", i.Node.empty() ? "" : ("'" + i.Node + "': ").c_str(), i.Message.c_str());
		}
		if (!valid) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "An invalid graph is ignored by the tribe until it is fixed.");
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::SameLine();

		// ---------------------------------------------------------------- right: inspector and preview
		ImGui::BeginChild("pg_right", ImVec2(0.0f, 0.0f), true);
		if (ImGui::CollapsingHeader("Graph", ImGuiTreeNodeFlags_DefaultOpen))
		{
			char buf[256];
			std::snprintf(buf, sizeof buf, "%s", graph->Name.c_str());
			if (ImGui::InputText("name", buf, sizeof buf)) graph->Name = buf;
			std::snprintf(buf, sizeof buf, "%s", graph->Description.c_str());
			if (ImGui::InputTextMultiline("description", buf, sizeof buf, ImVec2(-1.0f, 50.0f))) graph->Description = buf;
			std::snprintf(buf, sizeof buf, "%s", graph->Author.c_str());
			if (ImGui::InputText("author", buf, sizeof buf)) graph->Author = buf;
			ImGui::Checkbox("template", &graph->Template);
		}
		if (ImGui::CollapsingHeader("Selected", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (PgNode* n = graph->FindNode(ui.SelNode))
			{
				ImGui::Text("node '%s' (%s)", n->Id.c_str(), n->Type.c_str());
				char buf[128];
				std::snprintf(buf, sizeof buf, "%s", n->Label.c_str());
				if (ImGui::InputText("label", buf, sizeof buf)) n->Label = buf;
				auto drag = [&](const char* key, double def, float speed = 0.01f)
				{
					float v = static_cast<float>(n->Params.count(key) ? n->Params[key] : def);
					if (ImGui::DragFloat(key, &v, speed)) n->Params[key] = static_cast<double>(v);
				};
				if (n->Type == "input")
				{
					std::snprintf(buf, sizeof buf, "%s", n->Source.c_str());
					if (ImGui::InputText("source", buf, sizeof buf)) n->Source = buf;
					if (ImGui::BeginCombo("pick", "choose an input"))
					{
						for (const PgInputInfo& i : PriorityInputCatalog()) if (i.Name.find('<') == std::string::npos && ImGui::Selectable(i.Name.c_str())) n->Source = i.Name;
						ImGui::EndCombo();
					}
					if (!IsKnownPriorityInput(n->Source)) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "unknown input");
					else for (const PgInputInfo& i : PriorityInputCatalog()) if (i.Name == n->Source) ImGui::TextWrapped("%s", i.Help.c_str());
					drag("scale", 1.0); drag("offset", 0.0); drag("default", 0.0);
				}
				else if (n->Type == "const") drag("value", 0.0);
				else if (n->Type == "curve")
				{
					if (Combo("curve", n->Fn, PriorityFunctions("curve"))) for (const auto& [k, d] : CurveParams(n->Fn)) if (!n->Params.count(k)) n->Params[k] = d;
					for (const auto& [k, d] : CurveParams(n->Fn)) drag(k, d, k == std::string("from") || k == std::string("half") ? 1.0f : 0.01f);
					drag("default", 0.0);
				}
				else if (n->Type == "combine") { Combo("combiner", n->Fn, PriorityFunctions("combine")); drag("default", n->Fn == "mul" ? 1.0 : 0.0); }
				else if (n->Type == "clamp") { drag("lo", 0.0); drag("hi", 1.0); }
				else if (n->Type == "output")
				{
					std::vector<std::string> kinds = { "*" };
					for (const std::string& k : PriorityKnownKinds()) kinds.push_back(k);
					Combo("option", n->Source, kinds);
					if (n->Fn.empty()) n->Fn = "replace";
					Combo("mode", n->Fn, PriorityFunctions("output"));
					ImGui::TextDisabled("replace: the value is the score | multiply: score x value | add: score + value");
					drag("default", 0.0);
				}
				else if (n->Type == "subgraph")
				{
					std::vector<std::string> frags;
					for (const std::string& id : lib.Ids()) { const PriorityGraph* g = lib.Find(id); if (id != ui.Current && std::any_of(g->Nodes.begin(), g->Nodes.end(), [](const PgNode& c) { return c.Type == "return"; })) frags.push_back(id); }
					Combo("graph", n->Source, frags);
					if (const PriorityGraph* child = lib.Find(n->Source))
					{
						std::string ports;
						for (const PgNode& c : child->Nodes) if (c.Type == "param") ports += (ports.empty() ? "" : ", ") + c.Id;
						ImGui::TextWrapped("parameters: %s", ports.c_str());
					}
				}
				else if (n->Type == "param") drag("default", 0.0);
				if (ImGui::Button("Delete node"))
				{
					const std::string id = n->Id;
					graph->Edges.erase(std::remove_if(graph->Edges.begin(), graph->Edges.end(), [&](const PgEdge& e) { return e.From == id || e.To == id; }), graph->Edges.end());
					graph->Nodes.erase(std::remove_if(graph->Nodes.begin(), graph->Nodes.end(), [&](const PgNode& c) { return c.Id == id; }), graph->Nodes.end());
					ui.SelNode.clear();
				}
			}
			else if (ui.SelEdge >= 0 && ui.SelEdge < static_cast<int>(graph->Edges.size()))
			{
				PgEdge& e = graph->Edges[static_cast<size_t>(ui.SelEdge)];
				ImGui::Text("connection %s -> %s", e.From.c_str(), e.To.c_str());
				float w = static_cast<float>(e.Weight);
				if (ImGui::DragFloat("weight", &w, 0.01f)) e.Weight = static_cast<double>(w);
				if (const PgNode* to = graph->FindNode(e.To); to && to->Type == "subgraph")
					if (const PriorityGraph* child = lib.Find(to->Source))
					{
						std::vector<std::string> ports;
						for (const PgNode& c : child->Nodes) if (c.Type == "param") ports.push_back(c.Id);
						Combo("port", e.Port, ports);
					}
				if (ImGui::Button("Delete connection")) { graph->Edges.erase(graph->Edges.begin() + ui.SelEdge); ui.SelEdge = -1; }
			}
			else ImGui::TextDisabled("Click a node or a connection.");
		}
		if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!who) ImGui::TextDisabled("Select a character (Tribe panel or a click in the viewport) to see its options.");
			else
			{
				ImGui::Text("%s (%s)", who->Name.c_str(), who->Role.c_str());
				ImGui::TextDisabled("uses: %s", lib.IdFor(who->Role, who->Name).empty() ? "(none)" : lib.IdFor(who->Role, who->Name).c_str());
				const auto& options = who->Mind.Options;
				if (options.empty()) ImGui::TextDisabled("It has not decided yet.");
				if (ui.ShownOption >= static_cast<int>(options.size())) ui.ShownOption = -1;
				if (ui.ShownOption < 0 && who->Mind.Chosen >= 0) ui.ShownOption = who->Mind.Chosen;
				struct Row { int Index; float Now, Mine, Value; std::string Mode; bool Has; };
				std::vector<Row> rows;
				if (valid)
					for (size_t i = 0; i < options.size(); ++i)
					{
						UtilityOption o = options[i];
						for (const ScoreFactor& f : o.Factors) if (f.Name == "before graph") { o.Score = f.Value; break; }
						const PgResult r = EvaluatePriorityGraph(compiled, PriorityInputsFor(tribe, *who, &o));
						const PgOutput* out = r.For(o.Kind);
						Row row{ static_cast<int>(i), options[i].Score, out ? ApplyPgMode(out->Mode, o.Score, out->Value) : o.Score, out ? out->Value : 0.0f, out ? out->Mode : "", out != nullptr };
						if (row.Mine < 0.0f || !std::isfinite(row.Mine)) row.Mine = 0.0f;
						rows.push_back(row);
					}
				int best = -1;
				for (const Row& r : rows) if (best < 0 || r.Mine > rows[static_cast<size_t>(best)].Mine) best = r.Index;
				if (ImGui::BeginTable("pg_opts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
				{
					ImGui::TableSetupColumn("option"); ImGui::TableSetupColumn("tribe"); ImGui::TableSetupColumn("this graph"); ImGui::TableSetupColumn("out");
					ImGui::TableHeadersRow();
					for (const Row& r : rows)
					{
						const UtilityOption& o = options[static_cast<size_t>(r.Index)];
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						const std::string label = (r.Index == who->Mind.Chosen ? "> " : "  ") + o.Kind + (o.Target.empty() ? "" : " " + o.Target.substr(0, 14));
						if (ImGui::Selectable((label + "##o" + std::to_string(r.Index)).c_str(), r.Index == ui.ShownOption, ImGuiSelectableFlags_SpanAllColumns)) { ui.ShownOption = r.Index; ui.WhatIf = false; }
						if (ImGui::IsItemHovered())
						{
							std::string why = o.Note + "\n";
							for (const ScoreFactor& f : o.Factors) { char b[96]; std::snprintf(b, sizeof b, "%s %.2f  ", f.Name.c_str(), static_cast<double>(f.Value)); why += b; }
							ImGui::SetTooltip("%s", why.c_str());
						}
						ImGui::TableNextColumn(); ImGui::Text("%.2f", static_cast<double>(r.Now));
						ImGui::TableNextColumn(); ImGui::TextColored(r.Index == best ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1, 1, 1, 1), "%.2f%s", static_cast<double>(r.Mine), r.Index == best ? " *" : "");
						ImGui::TableNextColumn(); if (r.Has) ImGui::Text("%s %.2f", r.Mode.c_str(), static_cast<double>(r.Value)); else ImGui::TextDisabled("-");
					}
					ImGui::EndTable();
				}
				ImGui::TextDisabled("> = what it chose | * = what this graph would pick (ignoring the hysteresis)");
				if (who->Mind.Chosen >= 0) ImGui::TextWrapped("Why: %s", tribe.Because(*who).c_str());
			}
			ImGui::Separator();
			if (ImGui::Checkbox("What if (sliders)", &ui.WhatIf) && ui.WhatIf && who && ui.ShownOption >= 0 && ui.ShownOption < static_cast<int>(who->Mind.Options.size()))
			{
				UtilityOption o = who->Mind.Options[static_cast<size_t>(ui.ShownOption)];
				for (const ScoreFactor& f : o.Factors) if (f.Name == "before graph") { o.Score = f.Value; break; }
				ui.Over = PriorityInputsFor(tribe, *who, &o).Values;
				ui.WhatScore = o.Score;
			}
			if (ui.WhatIf)
			{
				std::set<std::string> sources;
				for (const PgNode& n : graph->Nodes) if (n.Type == "input" && n.Source != "option.score") sources.insert(n.Source);
				ImGui::SliderFloat("option.score", &ui.WhatScore, 0.0f, 3.0f);
				for (const std::string& s : sources)
				{
					float lo, hi;
					Range(s, lo, hi);
					float& v = ui.Over[s];
					ImGui::SliderFloat(s.c_str(), &v, lo, hi);
				}
				if (valid)
				{
					PgInputs wi;
					wi.Values = ui.Over;
					wi.Values["option.score"] = ui.WhatScore;
					wi.Resolver = [](const std::string& n, float& out) { if (n.rfind("factor.", 0) == 0 || n.rfind("store.", 0) == 0 || n.rfind("skill.", 0) == 0 || n.rfind("trait.", 0) == 0) { out = 0.0f; return true; } return false; };
					const PgResult r = EvaluatePriorityGraph(compiled, wi);
					ImGui::TextUnformatted("Result for an option with that score:");
					for (const PgOutput& o : r.Outputs) ImGui::Text("  %s: %s %.3f -> %.3f", o.Kind.c_str(), o.Mode.c_str(), static_cast<double>(o.Value), static_cast<double>(ApplyPgMode(o.Mode, ui.WhatScore, o.Value)));
				}
			}
		}
		ImGui::EndChild();
		ImGui::End();
	}
}
