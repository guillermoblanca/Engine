// The tree of knowledge (K, `game tech`; the observer's "Tree of knowledge" button): every technology of the world as a graph, as management games
// show their research, over the whole window with the world dimmed behind. Columns go left to right by what each requires (T181: no eras), so a line always runs from what is needed to what it opens. Each node says its state with its colour and a mark
// (+ known, ~ being learnt with its bar, light: can be found now, dim: needs others first), the player's aim has an ochre frame and the way to it is
// lit. A click shows its card at the bottom: who is on it, what it needs, what it opens and, for the player, "Seek this" (`game research <tech>`).
// Nothing here decides: the button queues the command. Every word goes through Lang.
#include "EditorApp.h"
#include "GameTheme.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <map>
#include <optional>
#include <set>

namespace GB::Editor
{
	namespace
	{
		namespace T = GameTheme;
		constexpr float kNodeW = 164.0f, kNodeH = 50.0f, kColGap = 40.0f, kRowGap = 10.0f, kPad = 16.0f, kHeader = 26.0f, kIcon = 42.0f;
	}

	void EditorApp::DrawTechTree(const Tribe& t, bool playing, float ox, float oy, float w, float h, bool& open, std::string& pick)
	{
		if (!open) return;
		const ClanKnowledge& k = t.Knowledge();
		const TechTree* tree = k.Techs();
		if (!tree) { open = false; return; }
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const std::set<std::string>& known = k.Clan();
		std::map<std::string, const ClanKnowledge::ResearchLine*> learning;
		const std::vector<ClanKnowledge::ResearchLine> lines = k.Research();
		for (const ClanKnowledge::ResearchLine& r : lines) learning[r.Tech] = &r;
		const std::string aim = t.ResearchAim();
		auto techName = [&](const std::string& id) { const TechDef* d = tree->Find(id); return Lang::Cap(L.Name("tech", id, d ? d->Name : std::string())); };

		// ---- the layout: a column per step of what is required, eras one after the other; rows ordered by where their requirements stand
		std::map<std::string, int> col;
		std::function<int(const std::string&, int)> column = [&](const std::string& id, int guard) -> int
		{
			if (const auto it = col.find(id); it != col.end()) return it->second;
			const TechDef* d = tree->Find(id);
			if (!d || guard > 32) return 0;
			int c = 0;
			for (const std::string& r : d->Requires) if (tree->Find(r)) c = std::max(c, column(r, guard + 1) + 1);
			col[id] = c;
			return c;
		};
		for (const auto& [id, d] : tree->Techs) column(id, 0);   // (T181: no eras: the columns are only what each requires)
		int columns = 0;
		std::map<int, std::vector<std::string>> byCol;
		for (const auto& [id, c] : col) { byCol[c].push_back(id); columns = std::max(columns, c + 1); }
		std::map<std::string, float> row;
		for (auto& [c, ids] : byCol)
		{
			// by the mean row of what they require (the first column by name), so the lines cross as little as they can
			auto key = [&](const std::string& id)
			{
				const TechDef* d = tree->Find(id);
				float sum = 0.0f;
				int n = 0;
				for (const std::string& r : d->Requires) if (row.count(r)) { sum += row[r]; ++n; }
				return n ? sum / static_cast<float>(n) : 1000.0f;
			};
			std::stable_sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b) { const float ka = key(a), kb = key(b); return ka != kb ? ka < kb : a < b; });
			for (size_t i = 0; i < ids.size(); ++i) row[ids[i]] = static_cast<float>(i);
		}
		int rows = 0;
		for (const auto& [c, ids] : byCol) rows = std::max(rows, static_cast<int>(ids.size()));
		// the way to the player's aim (and to the picked one): it and all it requires, lit
		auto wayTo = [&](const std::string& target)
		{
			std::set<std::string> way;
			std::function<void(const std::string&, int)> walk = [&](const std::string& id, int guard)
			{
				if (guard > 32 || way.count(id) || !tree->Find(id)) return;
				way.insert(id);
				for (const std::string& r : tree->Find(id)->Requires) walk(r, guard + 1);
			};
			if (!target.empty()) walk(target, 0);
			return way;
		};
		const std::set<std::string> aimWay = wayTo(aim), pickWay = wayTo(pick);
		int done = 0;
		for (const auto& [id, d] : tree->Techs) done += known.count(id) ? 1 : 0;

		// ---- the window: the world dims, the tree over it with its card at the bottom
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + w, oy + h), IM_COL32(0, 0, 0, 120));
		const float margin = 24.0f;
		ImGui::SetNextWindowPos(ImVec2(ox + margin, oy + margin), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(w - 2.0f * margin, h - 2.0f * margin), ImGuiCond_Always);
		const std::string title = L.Text("Tree of knowledge") + "  -  " + L.Text("{0} of {1} known", done, static_cast<int>(tree->Techs.size())) + "###techtree";
		if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
		{
			// the legend and the aim
			auto chip = [&](const std::string& text, ImU32 fill, ImU32 ink)
			{
				const ImVec2 size = ImGui::CalcTextSize(text.c_str()), at = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + size.x + 10.0f, at.y + size.y + 4.0f), fill, 4.0f);
				ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 5.0f, at.y + 2.0f), ink, text.c_str());
				ImGui::Dummy(ImVec2(size.x + 10.0f, size.y + 4.0f));
			};
			chip("+ " + L.Text("known"), T::Good, T::Ink); ImGui::SameLine();
			chip("~ " + L.Text("being learnt"), T::Ochre, T::Ink); ImGui::SameLine();
			chip(L.Text("can be found now"), T::Bone, T::Ink); ImGui::SameLine();
			chip(L.Text("needs others first"), T::HideRaised, T::BoneDim); ImGui::SameLine(0.0f, 24.0f);
			if (!aim.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("The clan seeks: {0}", techName(aim)).c_str());
			else ImGui::TextDisabled("%s", L.Text(playing ? "The clan chooses what to seek: pick one and press Seek this" : "The clan chooses what to seek").c_str());

			// the canvas, scrolled when the tree does not fit; drag it with the mouse
			const float cardH = playing ? 118.0f : 146.0f;   // (the god's card has the vision's line too)
			const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, std::max(120.0f, ImGui::GetContentRegionAvail().y - cardH));
			ImGui::BeginChild("canvas", canvasSize, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			const ImVec2 o = ImGui::GetCursorScreenPos();
			const float contentW = kPad * 2.0f + static_cast<float>(columns) * (kNodeW + kColGap) - kColGap;
			const float contentH = kHeader + kPad * 2.0f + static_cast<float>(rows) * (kNodeH + kRowGap) - kRowGap;
			ImDrawList* dl = ImGui::GetWindowDrawList();
			auto nodeMin = [&](const std::string& id) { return ImVec2(o.x + kPad + static_cast<float>(col[id]) * (kNodeW + kColGap), o.y + kHeader + kPad + row[id] * (kNodeH + kRowGap)); };
			// the lines: from the right of what is required to the left of what it opens; known ways green, the lit way ochre
			for (const auto& [id, d] : tree->Techs)
				for (const std::string& r : d.Requires)
				{
					if (!col.count(r)) continue;
					const ImVec2 a = nodeMin(r), b = nodeMin(id);
					const ImVec2 p0(a.x + kNodeW, a.y + kNodeH * 0.5f), p1(b.x, b.y + kNodeH * 0.5f);
					const bool lit = (pickWay.count(id) && pickWay.count(r)) || (aimWay.count(id) && aimWay.count(r));
					const ImU32 c = lit ? T::Ochre : known.count(r) && known.count(id) ? T::Good : known.count(r) ? T::BoneDim : T::HideLine;
					const float bend = (p1.x - p0.x) * 0.5f;
					dl->AddBezierCubic(p0, ImVec2(p0.x + bend, p0.y), ImVec2(p1.x - bend, p1.y), p1, c, lit ? 3.0f : 1.6f);
				}
			// the nodes
			for (const auto& [id, d] : tree->Techs)
			{
				const ImVec2 a = nodeMin(id), b(a.x + kNodeW, a.y + kNodeH);
				const bool have = known.count(id) > 0;
				const auto line = learning.find(id);
				const bool now = line != learning.end();
				bool open2 = true;
				for (const std::string& r : d.Requires) open2 = open2 && known.count(r) > 0;
				const ImU32 fill = have ? T::Good : now ? T::Ochre : open2 ? T::Bone : T::HideRaised;
				const ImU32 ink = have || now || open2 ? T::Ink : T::BoneDim;
				ImGui::SetCursorScreenPos(a);
				ImGui::PushID(id.c_str());
				if (ImGui::InvisibleButton("node", ImVec2(kNodeW, kNodeH))) pick = pick == id ? std::string() : id;
				const bool hovered = ImGui::IsItemHovered();
				ImGui::PopID();
				dl->AddRectFilled(a, b, fill, 6.0f);
				if (id == aim) dl->AddRect(ImVec2(a.x - 3.0f, a.y - 3.0f), ImVec2(b.x + 3.0f, b.y + 3.0f), T::Ochre, 8.0f, 0, 3.0f);
				dl->AddRect(a, b, id == pick ? T::Bone : hovered ? T::OchreHover : T::Outline, 6.0f, 0, id == pick || hovered ? 2.0f : 1.0f);
				// its own icon (tech.<id>, assets/ui/sheets/techs_*.jpg) when the atlas has it, else what it opens
				glm::vec2 uv0, uv1;
				std::string icon = IconUv("tech." + id, uv0, uv1) ? "tech." + id : std::string();
				if (icon.empty()) for (const std::string& r : d.Recipes) if (const Recipe* rec = m_Recipes.Find(r)) { icon = rec->Output; break; }
				if (icon.empty() && !d.Buildings.empty()) icon = "build";
				float textX = a.x + 8.0f;
				if (!icon.empty() && IconUv(icon, uv0, uv1))
				{
					dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_IconTexture->GetID())), ImVec2(a.x + 4.0f, a.y + 4.0f), ImVec2(a.x + 4.0f + kIcon, a.y + 4.0f + kIcon), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y),
						have || now || open2 ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 110));
					textX = a.x + kIcon + 9.0f;
				}
				const std::string name = (have ? "+ " : now ? "~ " : "") + techName(id);
				dl->PushClipRect(ImVec2(textX, a.y), ImVec2(b.x - 4.0f, b.y), true);
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX, a.y + 5.0f), ink, name.c_str(), nullptr, b.x - textX - 6.0f);
				dl->PopClipRect();
				if (now)
				{
					const float v = std::clamp(line->second->Progress, 0.0f, 1.0f);
					// (beside the icon, not over it)
					dl->AddRectFilled(ImVec2(textX, b.y - 9.0f), ImVec2(b.x - 6.0f, b.y - 4.0f), T::HideRaised, 2.0f);
					dl->AddRectFilled(ImVec2(textX, b.y - 9.0f), ImVec2(textX + (b.x - 6.0f - textX) * v, b.y - 4.0f), T::Ink, 2.0f);
				}
				if (hovered) ImGui::SetTooltip("%s", (techName(id) + ": " + L.Text(have ? "known" : now ? "being learnt" : open2 ? "can be found now" : "needs others first")).c_str());
			}
			// the space the tree takes, so the child scrolls; dragging the background moves it
			ImGui::SetCursorScreenPos(o);
			ImGui::Dummy(ImVec2(contentW, contentH));
			if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && !ImGui::IsAnyItemActive())
			{
				const ImVec2 delta = ImGui::GetIO().MouseDelta;
				ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
				ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
			}
			ImGui::EndChild();

			// ---- the card of the picked technology (or how to use the tree)
			ImGui::BeginChild("card", ImVec2(0.0f, 0.0f));
			const TechDef* d = pick.empty() ? nullptr : tree->Find(pick);
			if (!d) ImGui::TextDisabled("%s", L.Text("Click a technology to see who is on it, what it needs and what it opens. Drag to move around the tree.").c_str());
			else
			{
				const bool have = known.count(pick) > 0;
				const auto line = learning.find(pick);
				std::string missing;
				for (const std::string& r : d->Requires) if (!known.count(r)) missing += (missing.empty() ? "" : ", ") + techName(r);
				// its icon large on the left, the rest beside it
				if (Icon("tech." + pick, 64.0f)) ImGui::SameLine(0.0f, 10.0f);
				ImGui::BeginGroup();
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(T::Ochre));
				ImGui::TextUnformatted(techName(pick).c_str());
				ImGui::PopStyleColor();
				ImGui::SameLine();
				std::string state = have ? L.Text("known") : line != learning.end() ? L.Text("being learnt") : missing.empty() ? L.Text("can be found now") : L.Text("needs {0}", missing);
				if (line != learning.end())
				{
					std::string who;
					for (const std::string& n : line->second->Who) who += (who.empty() ? "" : ", ") + n;
					state += " (" + L.Num(line->second->Progress * 100.0f, 0) + " %" + (who.empty() ? std::string() : "; " + L.Text("on it: {0}", who)) + ")";
				}
				ImGui::TextDisabled("%s", state.c_str());
				// what it opens: the recipes' outputs and the buildings, with their icons
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(L.Text("Opens:").c_str());
				bool any = false;
				for (const std::string& r : d->Recipes)
					if (const Recipe* rec = m_Recipes.Find(r))
					{
						any = true;
						const ItemDef* item = m_Items.Find(rec->Output);
						ImGui::SameLine();
						if (Icon(rec->Output, 20.0f)) ImGui::SameLine(0.0f, 3.0f);
						ImGui::TextUnformatted(L.Name("item", rec->Output, item && !item->Name.empty() ? item->Name : std::string()).c_str());
					}
				for (const std::string& bid : d->Buildings)
				{
					any = true;
					const auto bdef = m_Buildings.Defs.find(bid);
					ImGui::SameLine();
					if (Icon("build", 20.0f)) ImGui::SameLine(0.0f, 3.0f);
					ImGui::TextUnformatted(Lang::Cap(L.Name("building", bid, bdef != m_Buildings.Defs.end() ? bdef->second.Name : std::string())).c_str());
					if (ImGui::IsItemHovered() && bdef != m_Buildings.Defs.end())
					{
						std::string tip;
						for (const std::string& l : BuildingDoes(bdef->second)) tip += (tip.empty() ? "" : "\n") + l;
						ImGui::SetTooltip("%s", tip.c_str());
					}
				}
				if (!any) { ImGui::SameLine(); ImGui::TextDisabled("%s", L.Text("knowledge for the clan (rites, stories, ways of doing)").c_str()); }
				// what the clan's goals are now, in order (the aim first)
				const std::vector<std::string>& goals = k.Goals();
				const auto at = std::find(goals.begin(), goals.end(), pick);
				if (at != goals.end()) ImGui::TextDisabled("%s", L.Text("In the clan's plans: step {0} of {1}", static_cast<int>(at - goals.begin()) + 1, static_cast<int>(goals.size())).c_str());
				// for the player: seek it (it and what it needs come first), or let the clan choose again
				if (playing && !have)
				{
					if (pick == aim)
					{
						if (ImGui::Button((L.Text("Stop seeking it") + "###unaim").c_str())) run("game research none");
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan chooses what to seek again").c_str());
					}
					else
					{
						const GameTheme::PrimaryScope primary;
						if (ImGui::Button((L.Text("Seek this") + "###aim").c_str())) run("game research " + pick);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan works out what it needs first and then this; no vote, no cost").c_str());
					}
				}
				// T242: for the god (the observer): [Vision] sends a dream of it to the clan (`director vision`), with its cost, why not, and the
				// warning of the material or of the inventor the clan lacks; the vision the clan holds says who dreamt it, with [Let it go]
				// T269 (MODO_COLONIA C-6): in the colony the player chooses the project, with no vote and no cost (`director research`)
				const Clan* mine = !playing && m_Director.Colony() ? m_Clans.Find(HandClan()) : nullptr;
				if (mine && mine->Folk == &t && !have)
				{
					const std::string id = std::to_string(mine->Id);
					if (pick == mine->Folk->ResearchAim())
					{
						if (ImGui::Button((L.Text("Stop seeking it") + "###unaim").c_str())) run("director research " + id + " none");
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan chooses what to seek again").c_str());
					}
					else
					{
						const GameTheme::PrimaryScope primary;
						if (ImGui::Button((L.Text("Seek this") + "###aim").c_str())) run("director research " + id + " " + pick);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan works out what it needs first and then this; no vote, no cost").c_str());
					}
				}
				else if (!playing && !have && !m_Director.Colony())
				{
					const Clan* clan = m_Clans.Find(m_ActiveClan);
					const std::string id = std::to_string(m_ActiveClan);
					const std::string clanName = clan ? L.Name("clan", clan->Name, clan->Name) : id;
					const DirectorVision* held = clan ? m_Director.VisionOf(clan->Id) : nullptr;
					if (held && held->Tech == pick)
					{
						ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("{0} dreamt of it: the {1} seek it", held->Dreamer, clanName).c_str());
						ImGui::SameLine();
						if (ImGui::SmallButton((L.Text("Let it go") + "###visionoff").c_str())) run("director vision " + id + " none");
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The {0} choose what to seek again (what was paid is not given back)", clanName).c_str());
					}
					else if (clan)
					{
						const DirectorVisionPreview p = m_Director.PreviewVision(clan->Id, pick);
						std::string why;
						if (!p.Ok) why = held ? L.Text("A vision already guides the {0} ({1}): let it go first", clanName, techName(held->Tech)) : Lang::Cap(L.Line(p.Why));
						ImGui::BeginDisabled(!p.Ok);
						{
							std::optional<T::PrimaryScope> primary;
							if (p.Ok) primary.emplace();
							if (ImGui::Button((L.Text("Vision ({0})", L.Num(p.Cost, 0)) + "###vision").c_str())) run("director vision " + id + " " + pick);
						}
						ImGui::EndDisabled();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
							ImGui::SetTooltip("%s", (!why.empty() ? why : L.Text("{0} dreams of it and tells it at the fire: the {1} seek it and what it needs first ({2} of {3} would agree)", p.Dreamer, clanName, p.Yes, p.Voters)
								+ "\n" + L.Text("It orients, it does not teach: they still have to work it out")).c_str());
						ImGui::SameLine();
						if (!p.Missing.empty())
						{
							std::string lack;
							for (const std::string& m : p.Missing) lack += (lack.empty() ? "" : ", ") + L.Name("item", m, m);
							ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("They lack {0}: they will not learn it until they find it", lack).c_str());
						}
						else if (!p.Dreamer.empty() && !p.Inventor) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("Nobody of the {0} can work it out: only from another clan", clanName).c_str());
						else if (!why.empty()) ImGui::TextDisabled("%s", why.c_str());
						else ImGui::TextDisabled("%s", L.Text("Dreamer: {0}", p.Dreamer).c_str());
					}
				}
				ImGui::EndGroup();
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
