// The editor's animation panels: the montage timeline (notifies, sections, scrubbing) and the animation state graph (nodes, transitions,
// parameters). Both edit data through the commands of EditorAnimCommands.cpp, so what the panel does can be done from a script.
#include "EditorApp.h"
#include "GB/Animation/AnimGraph.h"
#include "GB/Animation/Montage.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <sstream>

namespace GB::Editor
{
	void EditorApp::DrawTimelinePanel()
	{
		if (m_TimelineFocus > 0) { ImGui::SetNextWindowFocus(); --m_TimelineFocus; }   // `panel Timeline tab x` brings it to the front of its dock
		if (ImGui::Begin("Timeline", &m_Panels.Timeline))
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim)
			{
				ImGui::TextWrapped("No animated character. Start one with: player <character.fbx>. The montages of its clips can then be edited here (or with the montage_* commands).");
				ImGui::End();
				return;
			}
			MontagePlayer& mp = anim->Montages();
			const std::vector<std::string> names = mp.Names();
			if (names.empty()) { ImGui::TextWrapped("This character has no montages."); ImGui::End(); return; }
			if (!mp.Has(m_TimelineMontage)) { m_TimelineMontage = names.front(); m_TimelineNotify = -1; }
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("Montage", m_TimelineMontage.c_str()))
			{
				for (const std::string& n : names)
					if (ImGui::Selectable(n.c_str(), n == m_TimelineMontage)) { m_TimelineMontage = n; m_TimelineNotify = -1; }
				ImGui::EndCombo();
			}
			MontageDesc d = *mp.Get(m_TimelineMontage);
			bool changed = false;
			const float rate = std::max(d.PlayRate, 0.01f);
			const float clipSeconds = mp.Duration(d.Name) * rate;   // the clip's own length
			const bool playing = mp.IsPlaying(d.Name);
			ImGui::SameLine();
			if (ImGui::Button(playing ? "Restart" : "Play")) { mp.SetPaused(false); m_Player.PlayMontage(d.Name); }
			ImGui::SameLine();
			if (ImGui::Button(mp.Paused() ? "Resume" : "Pause")) mp.SetPaused(!mp.Paused());
			ImGui::SameLine();
			if (ImGui::Button("Stop")) { mp.SetPaused(false); m_Player.StopMontage(); }
			ImGui::SameLine();
			ImGui::Text("%.2f s clip, %.2f s at x%.2f", static_cast<double>(clipSeconds), static_cast<double>(mp.Duration(d.Name)), static_cast<double>(d.PlayRate));

			ImGui::SetNextItemWidth(90.0f); changed |= ImGui::DragFloat("blend in", &d.BlendIn, 0.01f, 0.0f, 3.0f, "%.2f s");
			ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); changed |= ImGui::DragFloat("blend out", &d.BlendOut, 0.01f, 0.0f, 3.0f, "%.2f s");
			ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); changed |= ImGui::DragFloat("rate", &d.PlayRate, 0.01f, 0.1f, 4.0f, "x%.2f");
			ImGui::SameLine(); changed |= ImGui::Checkbox("loop", &d.Loop);
			ImGui::SameLine(); changed |= ImGui::Checkbox("strip travel", &d.StripTravel);
			ImGui::SameLine(); ImGui::SetNextItemWidth(110.0f);
			if (ImGui::BeginCombo("slot", d.Slot.c_str()))
			{
				for (const std::string& s : mp.SlotNames())
					if (ImGui::Selectable(s.c_str(), s == d.Slot)) { d.Slot = s; changed = true; }
				ImGui::EndCombo();
			}

			// the timeline
			const float width = std::max(ImGui::GetContentRegionAvail().x, 200.0f), height = 96.0f;
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##timeline", ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const float x0 = origin.x + 10.0f, x1 = origin.x + width - 10.0f;
			auto xOf = [&](float seconds) { return x0 + (clipSeconds > 1e-4f ? seconds / clipSeconds : 0.0f) * (x1 - x0); };
			auto timeOf = [&](float x) { return std::clamp((x - x0) / std::max(x1 - x0, 1.0f), 0.0f, 1.0f) * clipSeconds; };
			draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(24, 26, 32, 255));
			const float rulerY = origin.y + 22.0f, laneY = origin.y + 62.0f;
			draw->AddLine(ImVec2(x0, rulerY), ImVec2(x1, rulerY), IM_COL32(120, 125, 140, 255));
			const float step = clipSeconds > 12.0f ? 2.0f : clipSeconds > 4.0f ? 1.0f : 0.5f;
			for (float t = 0.0f; t <= clipSeconds + 1e-3f; t += step)
			{
				draw->AddLine(ImVec2(xOf(t), rulerY - 4.0f), ImVec2(xOf(t), rulerY), IM_COL32(150, 155, 170, 255));
				char label[16];
				std::snprintf(label, sizeof label, "%.1f", static_cast<double>(t));
				draw->AddText(ImVec2(xOf(t) + 2.0f, origin.y + 2.0f), IM_COL32(150, 155, 170, 255), label);
			}
			// the blends
			const float inEnd = std::min(d.BlendIn * rate, clipSeconds), outStart = std::max(clipSeconds - d.BlendOut * rate, 0.0f);
			draw->AddTriangleFilled(ImVec2(x0, rulerY + 4.0f), ImVec2(xOf(inEnd), rulerY + 4.0f), ImVec2(xOf(inEnd), rulerY + 18.0f), IM_COL32(80, 150, 220, 110));
			draw->AddTriangleFilled(ImVec2(x1, rulerY + 4.0f), ImVec2(xOf(outStart), rulerY + 4.0f), ImVec2(xOf(outStart), rulerY + 18.0f), IM_COL32(220, 150, 80, 110));
			// the sections
			std::vector<MontageSection> sections = d.Sections;
			std::sort(sections.begin(), sections.end(), [](const MontageSection& a, const MontageSection& b) { return a.Time < b.Time; });
			for (size_t i = 0; i < sections.size(); ++i)
			{
				const float a = xOf(sections[i].Time * clipSeconds), b = i + 1 < sections.size() ? xOf(sections[i + 1].Time * clipSeconds) : x1;
				draw->AddRectFilled(ImVec2(a, rulerY + 22.0f), ImVec2(b - 1.0f, rulerY + 32.0f), IM_COL32(70 + 40 * static_cast<int>(i % 3), 110, 90, 200));
				draw->AddText(ImVec2(a + 3.0f, rulerY + 19.0f), IM_COL32(235, 240, 235, 255), sections[i].Name.c_str());
			}
			// the notifies
			for (size_t i = 0; i < d.Notifies.size(); ++i)
			{
				const float x = xOf(d.Notifies[i].Time * clipSeconds);
				const bool selected = static_cast<int>(i) == m_TimelineNotify;
				const ImU32 color = selected ? IM_COL32(255, 210, 60, 255) : IM_COL32(230, 150, 60, 255);
				draw->AddLine(ImVec2(x, laneY - 14.0f), ImVec2(x, laneY + 10.0f), color);
				draw->AddQuadFilled(ImVec2(x, laneY - 8.0f), ImVec2(x + 6.0f, laneY - 2.0f), ImVec2(x, laneY + 4.0f), ImVec2(x - 6.0f, laneY - 2.0f), color);
				draw->AddText(ImVec2(x + 8.0f, laneY - 6.0f), IM_COL32(235, 235, 235, 255), d.Notifies[i].Name.c_str());
			}
			// the playhead
			const float head = mp.Time(d.Name);
			if (head >= 0.0f) draw->AddLine(ImVec2(xOf(head), origin.y + 14.0f), ImVec2(xOf(head), origin.y + height), IM_COL32(255, 70, 70, 255), 2.0f);

			// the mouse: the ruler scrubs, a marker is selected and dragged, a double click adds a notify, a right click deletes one
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			auto notifyAt = [&](float x) { int best = -1; float bestD = 9.0f; for (size_t i = 0; i < d.Notifies.size(); ++i) { const float dx = std::fabs(xOf(d.Notifies[i].Time * clipSeconds) - x); if (dx < bestD) { bestD = dx; best = static_cast<int>(i); } } return best; };
			if (hovered || active)
			{
				if (ImGui::IsMouseClicked(0))
				{
					const int hit = mouse.y > rulerY + 8.0f ? notifyAt(mouse.x) : -1;
					m_TimelineNotify = hit;
					m_TimelineDrag = hit >= 0;
					if (hit < 0 && mouse.y <= rulerY + 8.0f) m_TimelineDrag = false;
				}
				if (ImGui::IsMouseDoubleClicked(0) && mouse.y > rulerY + 8.0f && notifyAt(mouse.x) < 0)
				{
					d.Notifies.push_back({ timeOf(mouse.x) / std::max(clipSeconds, 1e-4f), "notify" });
					m_TimelineNotify = static_cast<int>(d.Notifies.size()) - 1;
					changed = true;
				}
				if (ImGui::IsMouseClicked(1) && mouse.y > rulerY + 8.0f)
				{
					const int hit = notifyAt(mouse.x);
					if (hit >= 0) { d.Notifies.erase(d.Notifies.begin() + hit); m_TimelineNotify = -1; changed = true; }
				}
			}
			if (active && ImGui::IsMouseDown(0))
			{
				if (m_TimelineDrag && m_TimelineNotify >= 0 && m_TimelineNotify < static_cast<int>(d.Notifies.size()))
				{
					d.Notifies[static_cast<size_t>(m_TimelineNotify)].Time = timeOf(mouse.x) / std::max(clipSeconds, 1e-4f);
					changed = true;
				}
				else if (mouse.y <= rulerY + 8.0f)
				{
					if (!mp.IsPlaying(d.Name)) m_Player.PlayMontage(d.Name);   // scrubbing shows the pose of that moment, paused
					mp.SetPaused(true);
					mp.Seek(d.Name, timeOf(mouse.x));
				}
			}
			if (!ImGui::IsMouseDown(0)) m_TimelineDrag = false;

			// the selected notify, and the sections
			if (m_TimelineNotify >= 0 && m_TimelineNotify < static_cast<int>(d.Notifies.size()))
			{
				MontageNotify& n = d.Notifies[static_cast<size_t>(m_TimelineNotify)];
				char buf[64];
				std::snprintf(buf, sizeof buf, "%s", n.Name.c_str());
				ImGui::SetNextItemWidth(160.0f);
				if (ImGui::InputText("notify", buf, sizeof buf)) { n.Name = buf; changed = true; }
				ImGui::SameLine(); ImGui::SetNextItemWidth(100.0f);
				float seconds = n.Time * clipSeconds;
				if (ImGui::DragFloat("at", &seconds, 0.01f, 0.0f, clipSeconds, "%.2f s")) { n.Time = seconds / std::max(clipSeconds, 1e-4f); changed = true; }
				ImGui::SameLine();
				if (ImGui::Button("Delete notify")) { d.Notifies.erase(d.Notifies.begin() + m_TimelineNotify); m_TimelineNotify = -1; changed = true; }
			}
			else
				ImGui::TextDisabled("double-click the lane to add a notify, drag a marker to move it, right-click to delete it; click the ruler to scrub");
			if (ImGui::Button("Add section here"))
			{
				const float at = head >= 0.0f ? head : 0.0f;
				d.Sections.push_back({ at / std::max(clipSeconds, 1e-4f), "section" + std::to_string(d.Sections.size() + 1) });
				changed = true;
			}
			for (size_t i = 0; i < d.Sections.size(); ++i)
			{
				ImGui::SameLine();
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::SmallButton((d.Sections[i].Name + " x").c_str())) { d.Sections.erase(d.Sections.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; ImGui::PopID(); break; }
				ImGui::PopID();
			}
			// the file and the log
			char file[160];
			std::snprintf(file, sizeof file, "%s", m_TimelineFile.c_str());
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::InputText("##montagefile", file, sizeof file)) m_TimelineFile = file;
			ImGui::SameLine();
			if (ImGui::Button("Save all")) Execute("montage_save " + m_TimelineFile);
			ImGui::SameLine();
			if (ImGui::Button("Load")) Execute("montage_load " + m_TimelineFile);
			for (const MontageEvent& e : mp.Events())
			{
				m_TimelineLog.push_back(e.What == MontageEvent::Kind::Notify ? "notify " + e.Name + " (" + e.Montage + ")" : e.What == MontageEvent::Kind::Finished ? e.Montage + " finished" : e.Montage + " interrupted");
				if (m_TimelineLog.size() > 6) m_TimelineLog.erase(m_TimelineLog.begin());
			}
			std::string log;
			for (const std::string& l : m_TimelineLog) log += (log.empty() ? "" : "  |  ") + l;
			ImGui::TextDisabled("%s", log.empty() ? "events appear here" : log.c_str());
			if (changed) mp.Add(d);
		}
		ImGui::End();
	}

	void EditorApp::DrawGraphPanel()
	{
		if (m_GraphFocus > 0) { ImGui::SetNextWindowFocus(); --m_GraphFocus; }
		if (ImGui::Begin("Graph", &m_Panels.Graph))
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim)
			{
				ImGui::TextWrapped("No animated character. Start one with: player <character.fbx>. Its animation state graph can then be edited here (or with the graph_* commands).");
				ImGui::End();
				return;
			}
			GraphRunner& runner = anim->Graph();
			AnimGraph& g = runner.Graph();
			if (!runner.HasGraph())
			{
				ImGui::TextWrapped("This character has no state graph: it is driven by player_overlay and the montages directly.");
				if (ImGui::Button("Make one from its locomotion sets")) runner.SetGraph(MakeDefaultGraph(*anim));
				ImGui::SameLine();
				char file[160];
				std::snprintf(file, sizeof file, "%s", m_GraphFile.c_str());
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::InputText("##graphfile0", file, sizeof file)) m_GraphFile = file;
				ImGui::SameLine();
				if (ImGui::Button("Load")) Execute("graph_load " + m_GraphFile);
				ImGui::End();
				return;
			}
			const float inspectorWidth = 330.0f;
			ImGui::BeginChild("##graphcanvas", ImVec2(-inspectorWidth - 8.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			{
				ImDrawList* draw = ImGui::GetWindowDrawList();
				const ImVec2 area = ImGui::GetWindowPos();
				const ImVec2 size = ImGui::GetWindowSize();
				ImGui::InvisibleButton("##canvasbg", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
				const bool bgHovered = ImGui::IsItemHovered();
				const ImVec2 origin(area.x + m_GraphPanX, area.y + m_GraphPanY + 22.0f);
				const ImVec2 nodeSize(132.0f, 40.0f);
				auto center = [&](const GraphState& st) { return ImVec2(origin.x + st.Position.x + nodeSize.x * 0.5f, origin.y + st.Position.y + nodeSize.y * 0.5f); };
				auto edgePoint = [&](const GraphState& st, const ImVec2& toward)
				{
					const ImVec2 c = center(st);
					const float dx = toward.x - c.x, dy = toward.y - c.y;
					const float sx = std::fabs(dx) > 1e-3f ? (nodeSize.x * 0.5f) / std::fabs(dx) : 1e9f, sy = std::fabs(dy) > 1e-3f ? (nodeSize.y * 0.5f) / std::fabs(dy) : 1e9f;
					const float k = std::min(sx, sy);
					return ImVec2(c.x + dx * k, c.y + dy * k);
				};
				// the edges
				ImVec2 clickedLabel(-1.0f, -1.0f);
				int hitTransition = -1;
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				for (size_t i = 0; i < g.Transitions.size(); ++i)
				{
					const GraphTransition& t = g.Transitions[i];
					const int to = g.FindState(t.To), from = g.FindState(t.From);
					if (to < 0) continue;
					const GraphState& dst = g.States[static_cast<size_t>(to)];
					ImVec2 a, b;
					if (from >= 0)
					{
						const GraphState& src = g.States[static_cast<size_t>(from)];
						// two states joined both ways: the arrows are bent apart so they do not lie on top of each other
						const ImVec2 cs = center(src), cd = center(dst);
						const ImVec2 normal(-(cd.y - cs.y), cd.x - cs.x);
						const float len = std::max(std::sqrt(normal.x * normal.x + normal.y * normal.y), 1.0f);
						const ImVec2 shift(normal.x / len * 8.0f, normal.y / len * 8.0f);
						a = edgePoint(src, ImVec2(cd.x + shift.x, cd.y + shift.y)); a.x += shift.x; a.y += shift.y;
						b = edgePoint(dst, ImVec2(cs.x + shift.x, cs.y + shift.y)); b.x += shift.x; b.y += shift.y;
					}
					else   // from any state: a short arrow coming down into the node
					{
						const ImVec2 c = center(dst);
						a = ImVec2(c.x - 30.0f, c.y - nodeSize.y * 0.5f - 26.0f);
						b = ImVec2(c.x - 30.0f, c.y - nodeSize.y * 0.5f);
					}
					const bool selected = static_cast<int>(i) == m_GraphTransition;
					const bool fired = runner.LastTransition() == (t.From == "*" || t.From.empty() ? runner.LastTransition() : t.From + " -> " + t.To) && !runner.LastTransition().empty() && runner.LastTransition().find("-> " + t.To) != std::string::npos && (t.From == "*" || runner.LastTransition().rfind(t.From, 0) == 0);
					const ImU32 color = selected ? IM_COL32(255, 210, 60, 255) : fired ? IM_COL32(110, 200, 120, 255) : IM_COL32(150, 155, 170, 255);
					draw->AddLine(a, b, color, selected ? 2.5f : 1.5f);
					const ImVec2 dir(b.x - a.x, b.y - a.y);
					const float dl = std::max(std::sqrt(dir.x * dir.x + dir.y * dir.y), 1.0f);
					const ImVec2 u(dir.x / dl, dir.y / dl), nrm(-u.y, u.x);
					draw->AddTriangleFilled(b, ImVec2(b.x - u.x * 9.0f + nrm.x * 4.5f, b.y - u.y * 9.0f + nrm.y * 4.5f), ImVec2(b.x - u.x * 9.0f - nrm.x * 4.5f, b.y - u.y * 9.0f - nrm.y * 4.5f), color);
					std::string label;
					for (const GraphCondition& c : t.Conditions) label += (label.empty() ? "" : " & ") + DescribeCondition(c);
					if (t.OnFinish) label += (label.empty() ? "" : " & ") + std::string("finished");
					if (label.empty()) label = "always";
					const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
					const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
					draw->AddRectFilled(ImVec2(mid.x - 2.0f, mid.y - 1.0f), ImVec2(mid.x + textSize.x + 2.0f, mid.y + textSize.y + 1.0f), IM_COL32(20, 22, 28, 210));
					draw->AddText(mid, selected ? IM_COL32(255, 220, 90, 255) : IM_COL32(190, 195, 205, 255), label.c_str());
					if (bgHovered && mouse.x >= mid.x - 2.0f && mouse.x <= mid.x + textSize.x + 2.0f && mouse.y >= mid.y - 1.0f && mouse.y <= mid.y + textSize.y + 1.0f) hitTransition = static_cast<int>(i);
				}
				(void)clickedLabel;
				// the nodes
				std::string hitState;
				for (GraphState& st : g.States)
				{
					const ImVec2 p0(origin.x + st.Position.x, origin.y + st.Position.y), p1(p0.x + nodeSize.x, p0.y + nodeSize.y);
					const bool current = st.Name == runner.Current(), selected = st.Name == m_GraphState;
					const bool isMontage = st.What == GraphState::Kind::Montage;
					draw->AddRectFilled(p0, p1, isMontage ? IM_COL32(70, 52, 86, 255) : IM_COL32(44, 62, 84, 255), 6.0f);
					draw->AddRect(p0, p1, current ? IM_COL32(110, 220, 120, 255) : selected ? IM_COL32(255, 210, 60, 255) : IM_COL32(120, 125, 140, 255), 6.0f, 0, current || selected ? 2.5f : 1.0f);
					draw->AddText(ImVec2(p0.x + 8.0f, p0.y + 5.0f), IM_COL32(240, 240, 245, 255), (st.Name + (g.Start == st.Name || (g.Start.empty() && &st == &g.States[0]) ? "  (start)" : "")).c_str());
					draw->AddText(ImVec2(p0.x + 8.0f, p0.y + 21.0f), IM_COL32(160, 170, 190, 255), ((isMontage ? "montage " : "set ") + (st.Target.empty() ? std::string("default") : st.Target)).c_str());
					if (bgHovered && mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y) hitState = st.Name;
				}
				// the mouse: select, drag a node, pan with the middle or right button
				if (bgHovered && ImGui::IsMouseClicked(0))
				{
					m_GraphDragging.clear();
					if (!hitState.empty()) { m_GraphState = hitState; m_GraphTransition = -1; m_GraphDragging = hitState; }
					else if (hitTransition >= 0) { m_GraphTransition = hitTransition; }
					else { m_GraphState.clear(); m_GraphTransition = -1; }
				}
				if (!m_GraphDragging.empty() && ImGui::IsMouseDown(0))
				{
					const int i = g.FindState(m_GraphDragging);
					if (i >= 0) { g.States[static_cast<size_t>(i)].Position.x += ImGui::GetIO().MouseDelta.x; g.States[static_cast<size_t>(i)].Position.y += ImGui::GetIO().MouseDelta.y; }
				}
				if (!ImGui::IsMouseDown(0)) m_GraphDragging.clear();
				if (bgHovered && (ImGui::IsMouseDragging(1) || ImGui::IsMouseDragging(2)))
				{
					m_GraphPanX += ImGui::GetIO().MouseDelta.x;
					m_GraphPanY += ImGui::GetIO().MouseDelta.y;
				}
				char status[160];
				std::snprintf(status, sizeof status, "%s   state: %s (%.1f s)   last: %s", runner.Enabled ? "running" : "OFF", runner.Current().c_str(), static_cast<double>(runner.StateTime()), runner.LastTransition().empty() ? "-" : runner.LastTransition().c_str());
				draw->AddText(ImVec2(area.x + 8.0f, area.y + 4.0f), IM_COL32(170, 200, 170, 255), status);
			}
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::BeginChild("##graphinspector", ImVec2(0.0f, 0.0f), true);
			{
				bool restart = false;
				ImGui::Checkbox("Run", &runner.Enabled);
				ImGui::SameLine();
				if (ImGui::SmallButton("Restart")) runner.Restart();
				ImGui::SameLine();
				if (ImGui::SmallButton("Validate")) { const auto problems = g.Validate(anim); Execute("graph_validate"); (void)problems; }
				// the parameters
				if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (GraphParam& p : g.Params)
					{
						ImGui::PushID(p.Name.c_str());
						if (p.What == GraphParam::Kind::Trigger) { if (ImGui::SmallButton("fire")) p.Value = 1.0f; ImGui::SameLine(); ImGui::Text("%s%s", p.Name.c_str(), p.Value != 0.0f ? "  (set)" : ""); }
						else if (p.What == GraphParam::Kind::Bool) { bool b = p.Value != 0.0f; if (ImGui::Checkbox(p.Name.c_str(), &b)) p.Value = b ? 1.0f : 0.0f; }
						else { ImGui::SetNextItemWidth(120.0f); ImGui::DragFloat(p.Name.c_str(), &p.Value, 0.05f); }
						ImGui::PopID();
					}
					std::string autoText;
					for (const GraphParam& p : runner.AutoParams()) { char b[48]; std::snprintf(b, sizeof b, "%s%s=%.2f", autoText.empty() ? "" : "  ", p.Name.c_str(), static_cast<double>(p.Value)); autoText += b; }
					ImGui::TextDisabled("from the motion: %s", autoText.c_str());
				}
				// a state
				const int si = g.FindState(m_GraphState);
				if (si >= 0 && ImGui::CollapsingHeader("State", ImGuiTreeNodeFlags_DefaultOpen))
				{
					GraphState& st = g.States[static_cast<size_t>(si)];
					char name[64];
					std::snprintf(name, sizeof name, "%s", st.Name.c_str());
					ImGui::SetNextItemWidth(150.0f);
					if (ImGui::InputText("name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue) && name[0] && g.FindState(name) < 0)
					{
						const std::string old = st.Name;
						st.Name = name;
						for (GraphTransition& t : g.Transitions) { if (t.From == old) t.From = name; if (t.To == old) t.To = name; }
						if (g.Start == old) g.Start = name;
						m_GraphState = name;
						restart = true;
					}
					int kind = st.What == GraphState::Kind::Montage ? 1 : 0;
					ImGui::SetNextItemWidth(150.0f);
					if (ImGui::Combo("kind", &kind, "locomotion set\0montage\0")) { st.What = kind == 1 ? GraphState::Kind::Montage : GraphState::Kind::Locomotion; st.Target.clear(); }
					ImGui::SetNextItemWidth(150.0f);
					if (ImGui::BeginCombo("target", st.Target.empty() ? "(default)" : st.Target.c_str()))
					{
						const std::vector<std::string> options = st.What == GraphState::Kind::Montage ? anim->Montages().Names() : anim->SetNames();
						for (const std::string& o : options) if (ImGui::Selectable(o.c_str(), o == st.Target)) st.Target = o;
						ImGui::EndCombo();
					}
					bool isStart = g.Start == st.Name || (g.Start.empty() && si == 0);
					if (ImGui::Checkbox("start state", &isStart) && isStart) g.Start = st.Name;
					if (ImGui::Button("Delete state"))
					{
						const std::string old = st.Name;
						g.Transitions.erase(std::remove_if(g.Transitions.begin(), g.Transitions.end(), [&](const GraphTransition& t) { return t.From == old || t.To == old; }), g.Transitions.end());
						if (g.Start == old) g.Start.clear();
						g.States.erase(g.States.begin() + si);
						m_GraphState.clear();
						m_GraphTransition = -1;
						restart = true;
					}
				}
				// a transition
				if (m_GraphTransition >= 0 && m_GraphTransition < static_cast<int>(g.Transitions.size()) && ImGui::CollapsingHeader("Transition", ImGuiTreeNodeFlags_DefaultOpen))
				{
					GraphTransition& t = g.Transitions[static_cast<size_t>(m_GraphTransition)];
					auto stateCombo = [&](const char* label, std::string& value, bool allowAny)
					{
						ImGui::SetNextItemWidth(150.0f);
						if (ImGui::BeginCombo(label, value.empty() ? "*" : value.c_str()))
						{
							if (allowAny && ImGui::Selectable("*", value == "*")) value = "*";
							for (const GraphState& s : g.States) if (ImGui::Selectable(s.Name.c_str(), s.Name == value)) value = s.Name;
							ImGui::EndCombo();
						}
					};
					stateCombo("from", t.From, true);
					stateCombo("to", t.To, false);
					ImGui::SetNextItemWidth(100.0f); ImGui::DragInt("priority", &t.Priority, 0.2f, -10, 10);
					ImGui::SetNextItemWidth(100.0f); ImGui::DragFloat("blend", &t.Blend, 0.01f, 0.0f, 3.0f, "%.2f s");
					ImGui::SetNextItemWidth(100.0f); ImGui::DragFloat("min time", &t.MinStateTime, 0.01f, 0.0f, 10.0f, "%.2f s");
					ImGui::Checkbox("only when the montage ended", &t.OnFinish);
					std::vector<std::string> paramNames;
					for (const GraphParam& p : runner.AutoParams()) paramNames.push_back(p.Name);
					for (const GraphParam& p : g.Params) paramNames.push_back(p.Name);
					static const char* opNames = ">\0<\0>=\0<=\0==\0!=\0";
					for (size_t ci = 0; ci < t.Conditions.size(); ++ci)
					{
						GraphCondition& c = t.Conditions[ci];
						ImGui::PushID(static_cast<int>(ci));
						ImGui::SetNextItemWidth(110.0f);
						if (ImGui::BeginCombo("##param", c.Param.c_str()))
						{
							for (const std::string& n : paramNames) if (ImGui::Selectable(n.c_str(), n == c.Param)) c.Param = n;
							ImGui::EndCombo();
						}
						ImGui::SameLine(); ImGui::SetNextItemWidth(50.0f);
						int op = static_cast<int>(c.What);
						if (ImGui::Combo("##op", &op, opNames)) c.What = static_cast<GraphCondition::Op>(op);
						ImGui::SameLine(); ImGui::SetNextItemWidth(60.0f);
						ImGui::DragFloat("##value", &c.Value, 0.05f);
						ImGui::SameLine();
						if (ImGui::SmallButton("x")) { t.Conditions.erase(t.Conditions.begin() + static_cast<std::ptrdiff_t>(ci)); ImGui::PopID(); break; }
						ImGui::PopID();
					}
					if (ImGui::SmallButton("+ condition")) t.Conditions.push_back({ paramNames.empty() ? "Speed" : paramNames.front(), GraphCondition::Op::Greater, 0.0f });
					ImGui::SameLine();
					if (ImGui::SmallButton("Delete transition")) { g.Transitions.erase(g.Transitions.begin() + m_GraphTransition); m_GraphTransition = -1; }
				}
				// new things, files
				ImGui::Separator();
				if (ImGui::SmallButton("+ locomotion state"))
				{
					GraphState s;
					s.Name = "state" + std::to_string(g.States.size() + 1);
					s.Position = { 30.0f + 20.0f * static_cast<float>(g.States.size()), 150.0f };
					g.States.push_back(s);
					m_GraphState = s.Name;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("+ montage state"))
				{
					GraphState s;
					s.Name = "action" + std::to_string(g.States.size() + 1);
					s.What = GraphState::Kind::Montage;
					const auto montages = anim->Montages().Names();
					if (!montages.empty()) s.Target = montages.front();
					s.Position = { 30.0f + 20.0f * static_cast<float>(g.States.size()), 150.0f };
					g.States.push_back(s);
					m_GraphState = s.Name;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("+ transition from selected") && !m_GraphState.empty())
				{
					GraphTransition t;
					t.From = m_GraphState;
					t.To = g.States.size() > 1 ? g.States[(static_cast<size_t>(std::max(g.FindState(m_GraphState), 0)) + 1) % g.States.size()].Name : m_GraphState;
					g.Transitions.push_back(t);
					m_GraphTransition = static_cast<int>(g.Transitions.size()) - 1;
				}
				if (ImGui::SmallButton("+ trigger")) { g.Params.push_back({ "Trigger" + std::to_string(g.Params.size() + 1), GraphParam::Kind::Trigger, 0.0f }); }
				ImGui::SameLine();
				if (ImGui::SmallButton("+ number")) { g.Params.push_back({ "Value" + std::to_string(g.Params.size() + 1), GraphParam::Kind::Float, 0.0f }); }
				char file[160];
				std::snprintf(file, sizeof file, "%s", m_GraphFile.c_str());
				ImGui::SetNextItemWidth(170.0f);
				if (ImGui::InputText("##graphfile", file, sizeof file)) m_GraphFile = file;
				ImGui::SameLine();
				if (ImGui::SmallButton("Save")) Execute("graph_save " + m_GraphFile);
				ImGui::SameLine();
				if (ImGui::SmallButton("Load")) Execute("graph_load " + m_GraphFile);
				if (restart) runner.Restart();
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
