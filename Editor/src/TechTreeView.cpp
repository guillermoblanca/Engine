// The tree of knowledge (K, `game tech`; `observer tree`, opened from the Knowledge sheet and tab), over the whole window with the world dimmed
// behind. Every word goes through Lang; nothing here decides: the buttons queue commands.
// T341 (PROPUESTA_ARBOL_2026-11-19 K-b): each technology says the ONE state of GB/Game/TechStatus.h (the same as the queue and the sheet) with a
//   shape and a word, not only a colour: known (a tick, faded), studying (thick ochre frame and its bar), queued (its number in a badge), on their
//   own (a thin bar and "~"), studiable (a "+"), blocked (an amber dashed frame and why: "no flint at hand", "needs tier 2"), another clan's (a
//   blue frame and who) and unknown: a "?" with no name, only "after X" (D2). In the god's view (the observer of the free mode) every name shows.
// T338 (K-a, D6): only the technologies with a tier are drawn and counted ("5 of 54"); tier 4 (era 2) is one "?" block with its number.
// T339 (K-d, D1): one way to direct research, the queue of T324: the card's buttons queue `game research add|top|remove <tech>`; the old aim
//   (`game research <tech>`, `director research`) is no longer offered here. The god keeps [Vision] (`director vision`).
// T340 (K-e, K-f, mock-ups a1 and a2): columns are the tiers (T323: TechDef::Tier; the clan opens them with the goals of KnowledgeRules::Tiers,
//   GB/Game/TechTiers.h) and rows the seven branches (D77: TechDef::Branch). The layout is worked out every frame from the room there is, so the
//   tree never scrolls at 1600x900 or 1280x720: the cards narrow, then a crowded cell wraps into two rows, then the rows get lower. A tier that is
//   not open shows its unknown technologies as small "?" boxes. Lines only for the technology under the mouse or picked: what it needs in green,
//   what it opens in ochre. The study queue is a strip at the bottom (the same component as the Knowledge sheet, small); the picked
//   technology's card opens on the right, over the far tiers: what it opens (new actions, recipes, buildings with their picture), what it needs
//   (what is missing at hand, with the shortcut of a stock goal: D7), how it is learnt (D5: three words) and who is on it.
// T342: the god's view (every name) is laid out by the same rules as the player's, so it fits too (it scrolled at 1280x720 and cut names at
//   1600): the tiers not open yet are small boxes (each with its icon, its name and state in the tooltip; a neutral frame unless the tier holds it
//   back) and era 2 is one block with its count (the names in its tooltip). A name too long for its card is cut with ".." (its tooltip has it whole).
#include "EditorApp.h"
#include "GameTheme.h"
#include "GB/Game/TechStatus.h"
#include "GB/Game/TechTiers.h"
#include "GB/Game/TechVerbs.h"
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

		// a dashed rectangle (the shapes of "blocked" and "unknown": a frame that is not whole)
		void DashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float thick)
		{
			const float dash = 6.0f, gap = 4.0f;
			auto line = [&](ImVec2 p, ImVec2 q)
			{
				const float len = std::hypot(q.x - p.x, q.y - p.y);
				if (len <= 0.0f) return;
				const ImVec2 d((q.x - p.x) / len, (q.y - p.y) / len);
				for (float s = 0.0f; s < len; s += dash + gap)
				{
					const float e = std::min(s + dash, len);
					dl->AddLine(ImVec2(p.x + d.x * s, p.y + d.y * s), ImVec2(p.x + d.x * e, p.y + d.y * e), c, thick);
				}
			};
			line(a, ImVec2(b.x, a.y));
			line(ImVec2(b.x, a.y), b);
			line(b, ImVec2(a.x, b.y));
			line(ImVec2(a.x, b.y), a);
		}

		// `s` cut to `width` with ".." (whole UTF-8 characters; the font has no ellipsis); `cut` says whether it was
		std::string Fit(std::string s, float width, bool* cut = nullptr)
		{
			if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
			if (cut) *cut = true;
			while (!s.empty() && ImGui::CalcTextSize((s + "..").c_str()).x > width)
			{
				s.pop_back();
				while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
				if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0xC0) s.pop_back();
			}
			while (!s.empty() && s.back() == ' ') s.pop_back();
			return s + "..";
		}

		// `s` in lines of `width`, at most `maxLines` (the last one cut with ".." when it does not fit)
		std::vector<std::string> Wrap(const std::string& s, float width, int maxLines, bool* cut = nullptr)
		{
			std::vector<std::string> words, lines;
			for (size_t i = 0; i <= s.size();)
			{
				const size_t j = std::min(s.find(' ', i), s.size());
				if (j > i) words.push_back(s.substr(i, j - i));
				i = j + 1;
			}
			std::string line;
			for (const std::string& w : words)
			{
				const std::string next = line.empty() ? w : line + " " + w;
				if (line.empty() || ImGui::CalcTextSize(next.c_str()).x <= width) line = next;
				else { lines.push_back(line); line = w; }
			}
			if (!line.empty()) lines.push_back(line);
			if (static_cast<int>(lines.size()) > maxLines)
			{
				std::string rest;
				for (size_t i = static_cast<size_t>(maxLines - 1); i < lines.size(); ++i) rest += (rest.empty() ? "" : " ") + lines[i];
				lines.resize(static_cast<size_t>(maxLines - 1));
				lines.push_back(rest);
			}
			for (std::string& l : lines) l = Fit(l, width, cut);
			if (static_cast<int>(lines.size()) > maxLines && cut) *cut = true;
			return lines;
		}

		// the order of the cards in a cell: what the player can act on first, the unknown last
		int StateOrder(TechState s)
		{
			switch (s)
			{
			case TechState::Studying: return 0;
			case TechState::Queued: return 1;
			case TechState::Studiable: return 2;
			case TechState::OwnPace: return 3;
			case TechState::OtherClan: return 4;
			case TechState::Blocked: return 5;
			case TechState::Known: return 6;
			case TechState::Unknown: return 7;
			}
			return 8;
		}

		// the icon of a branch (D77; icons the atlas already has, PROPUESTA_ARBOL s5)
		const char* BranchIcon(const std::string& b)
		{
			if (b == "tools") return "stone_axe";
			if (b == "hunt") return "hunt";
			if (b == "field") return "till";
			if (b == "food") return "campfire";
			if (b == "crafts") return "basket_wicker";
			if (b == "homes") return "building.shelter";
			if (b == "lore") return "rite";
			return "build";
		}
	}

	TechStatus EditorApp::TechStatusOf(const Tribe& t) const
	{
		TechStatus::Input in;
		for (const std::unique_ptr<Clan>& c : m_Clans.All())
			if (c && c->Folk == &t) { in.Neighbours = TechStatus::Neighbours(m_Clans, c->Id); break; }
		return TechStatus(t, in);
	}

	std::string EditorApp::TechStateText(const TechStatus& s, const std::string& tech) const
	{
		const Lang& L = m_Lang;
		const TechStatus::Entry& e = s.Of(tech);
		auto techName = [&](const std::string& id) { return Lang::Cap(L.Name("tech", id, id)); };
		switch (e.State)
		{
		case TechState::Known: return L.Text("known");
		case TechState::Studying: return L.Text("being studied, {0} %", L.Num(e.Progress * 100.0f, 0));
		case TechState::Queued: return e.Why == TechWhy::None ? L.Text("number {0} in the queue", e.Queue) : L.Text("number {0} in the queue: {1}", e.Queue, TechWhyText(s, tech));
		case TechState::OwnPace: return L.Text("~{0} % on their own", L.Num(e.Progress * 100.0f, 0));
		case TechState::Studiable: return L.Text("can be studied now");
		case TechState::Blocked: return TechWhyText(s, tech);
		case TechState::OtherClan: return L.Text("the {0} know it", L.Name("clan", e.From, e.From));
		case TechState::Unknown: return e.Hint.empty() ? L.Text("not known yet") : L.Text("after {0}", techName(e.Hint));
		}
		return {};
	}

	std::string EditorApp::TechWhyText(const TechStatus& s, const std::string& tech) const
	{
		const Lang& L = m_Lang;
		const TechStatus::Entry& e = s.Of(tech);
		auto list = [&](const std::string& kind)
		{
			std::string out;
			for (const std::string& m : e.Missing)
			{
				const std::string name = kind != "tech" ? L.Name(kind, m, m) : s.Named(m) ? Lang::Cap(L.Name("tech", m, m)) : std::string("?");
				out += (out.empty() ? "" : ", ") + name;
			}
			return out;
		};
		switch (e.Why)
		{
		case TechWhy::None: return {};
		case TechWhy::Requires: return L.Text("waits for {0}", list("tech"));
		case TechWhy::Tier: return L.Text("needs tier {0}", e.Tier);   // T340: "falta escalon 2" (TechTiers::Missing)
		case TechWhy::Material: return L.Text("no {0} at hand", list("item"));
		case TechWhy::Station: return L.Text("needs {0}", list("station"));
		case TechWhy::NotByTrying: return L.Text("only learnt from others");
		}
		return {};
	}

	std::string EditorApp::TierGoalsText(const Tribe& t, int tier, bool numbers) const
	{
		const Lang& L = m_Lang;
		const TechTree* tree = t.Knowledge().Techs();
		if (!tree) return {};
		for (const TierStep& step : tree->Rules.Tiers)
		{
			if (step.Tier != tier) continue;
			std::string out;
			for (const TierGoal& g : step.Goals)
			{
				int v = 0;
				TechTiers::Met(t, g, &v);
				const int target = g.Type == "fire" ? 1 : g.Count;
				std::string word;
				if (g.Type == "population") word = L.Text("{0} people", g.Count);
				else if (g.Type == "building")
				{
					const auto def = m_Buildings.Defs.find(g.Building);
					word = L.Text("the {0}", L.Name("building", g.Building, def != m_Buildings.Defs.end() ? def->second.Name : g.Building));
				}
				else if (g.Type == "buildings") word = L.Text("{0} buildings", g.Count);
				else if (g.Type == "techs") word = L.Text("{0} technologies", g.Count);
				else if (g.Type == "fire") word = L.Text("a lit fire");
				else if (g.Type == "seasons") word = L.Text(g.Count == 1 ? "a season lived" : "{0} seasons lived", g.Count);
				else if (g.Type == "winters") word = L.Text(g.Count == 1 ? "a winter lived" : "{0} winters lived", g.Count);
				else word = g.Type;
				if (numbers) word += " " + std::to_string(std::min(v, target)) + "/" + std::to_string(target);
				const bool either = step.Need > 0 && step.Need < static_cast<int>(step.Goals.size());
				out += out.empty() ? word : (either ? L.Text(" or ") : L.Text(" and ")) + word;
			}
			return out;
		}
		return {};
	}

	std::vector<std::pair<std::string, int>> EditorApp::StudyPeople(const Tribe& t) const
	{
		std::vector<std::pair<std::string, int>> out;
		for (const TribeMember& m : t.Members())
		{
			if (m.Dead) continue;
			const int n = t.WorkTable().Number(t, m.Name, "research");
			if (n < 0) continue;   // (no "research" job in the data)
			out.push_back({ m.Name, n });
		}
		std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { const int x = a.second == 0 ? 99 : a.second, y = b.second == 0 ? 99 : b.second; return x < y; });
		return out;
	}

	void EditorApp::DrawTechTree(const Tribe& t, bool playing, float ox, float oy, float w, float h, bool& open, std::string& pick)
	{
		if (!open) return;
		const ClanKnowledge& k = t.Knowledge();
		const TechTree* tree = k.Techs();
		if (!tree) { open = false; return; }
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const TechStatus status = TechStatusOf(t);
		// T339: the queue is the player's way to direct research: in the game and in the colony, on the player's own clan
		const Clan* hand = m_Clans.Find(HandClan());
		const bool ownQueue = hand && hand->Folk == &t && (playing || m_Director.Colony());
		// the god's view (the observer of the free mode) sees every name; the player only what the clan could name (D2)
		const bool reveal = !playing && !m_Director.Colony();
		auto named = [&](const std::string& id) { return reveal || status.Named(id); };
		auto techName = [&](const std::string& id) { const TechDef* d = tree->Find(id); return Lang::Cap(L.Name("tech", id, d ? d->Name : std::string())); };
		const int openTier = TechTiers::Open(t);
		const bool tiers = !tree->Rules.Tiers.empty() && openTier < kNoTierGate;
		const std::string aim = t.ResearchAim();
		if (!pick.empty() && !tree->Find(pick)) pick.clear();

		// ---- what goes where: a column per tier (the era block apart, D6), a lane per branch (D77)
		const int eraTier = TechStatus::kEraBlockTier;
		std::vector<std::string> lanes;
		for (const std::string& b : TechBranches()) lanes.push_back(b);
		const size_t namedLanes = lanes.size();
		int maxTier = 0, eraHidden = 0;
		std::vector<std::string> eraNamed;
		for (const auto& [id, e] : status.All())
		{
			if (!e.Shown) continue;
			const TechDef* d = tree->Find(id);
			if (!d) continue;
			if (std::find(lanes.begin(), lanes.end(), d->Branch) == lanes.end()) lanes.push_back(d->Branch);   // (a branch the list has not, or none: a lane of its own)
			const int tier = std::clamp(e.Tier, 0, eraTier);
			if (tier >= eraTier && (!reveal || tiers)) { if (reveal || e.State == TechState::Known || named(id)) eraNamed.push_back(id); else ++eraHidden; continue; }
			maxTier = std::max(maxTier, tier);
		}
		// lanes no technology uses are left out (a test's tree, a pack with fewer branches)
		std::map<int, std::map<size_t, std::vector<std::string>>> cells;   // column -> lane -> ids
		auto laneOf = [&](const std::string& branch) { return static_cast<size_t>(std::find(lanes.begin(), lanes.end(), branch) - lanes.begin()); };
		for (const auto& [id, e] : status.All())
		{
			if (!e.Shown) continue;
			const TechDef* d = tree->Find(id);
			if (!d) continue;
			const int tier = std::clamp(e.Tier, 0, eraTier);
			if (tier >= eraTier && (!reveal || tiers)) continue;   // (era 2: its block)
			cells[tier][laneOf(d->Branch)].push_back(id);
		}
		std::vector<size_t> usedLanes;
		for (size_t l = 0; l < lanes.size(); ++l)
		{
			bool any = false;
			for (const auto& [c, byLane] : cells) any = any || byLane.count(l);
			if (any) usedLanes.push_back(l);
		}
		if (usedLanes.empty()) usedLanes.push_back(0);
		const int columns = std::max(maxTier + 1, tiers ? eraTier : 1);   // tiers 0..3 always (a closed one shows what it keeps), more in the god's view
		const bool eraBlock = tiers && (eraHidden > 0 || !eraNamed.empty());
		// a card is "small" (a box) when its tier is not open and the clan cannot act on it: a "?" when it is unknown (the shape of what comes, not
		// its name), its icon with the amber dashed frame when only the tier holds it back ("needs tier 2" in its tooltip); what is queued or another
		// clan knows keeps its card
		auto small = [&](const std::string& id)
		{
			const TechStatus::Entry& e = status.Of(id);
			// (the god's view the same: what the player could not name is a box too, with its icon; T342)
			return tiers && e.Tier > openTier && (!status.Named(id) || (e.State == TechState::Blocked && e.Why == TechWhy::Tier));
		};
		for (auto& [c, byLane] : cells)
			for (auto& [l, ids] : byLane)
				std::stable_sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b)
				{
					const bool sa = small(a), sb = small(b);
					if (sa != sb) return sb;
					const int oa = StateOrder(status.Of(a).State), ob = StateOrder(status.Of(b).State);
					return oa != ob ? oa < ob : a < b;
				});

		// the way to the picked one and to the one under the mouse are drawn later; what each one opens (the inverse of `requires`)
		std::map<std::string, std::vector<std::string>> opens;
		for (const auto& [id, d] : tree->Techs) for (const std::string& r : d.Requires) opens[r].push_back(id);

		// ---- the window: the world dims, the tree over it
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + w, oy + h), IM_COL32(0, 0, 0, 120));
		const float margin = std::min(24.0f, w * 0.015f + 4.0f);
		ImGui::SetNextWindowPos(ImVec2(ox + margin, oy + margin), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(w - 2.0f * margin, h - 2.0f * margin), ImGuiCond_Always);
		std::string title = L.Text("Tree of knowledge") + "  -  " + L.Text("{0} of {1} known", status.KnownCount(), status.Total()) + "  \xC2\xB7  " + L.Text("{0} can be studied", status.StudiableCount());
		if (!reveal && status.UnnamedCount() > 0) title += "  \xC2\xB7  " + L.Text("? {0} to discover", status.UnnamedCount());
		if (ImGui::Begin((title + "###techtree").c_str(), &open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImDrawList* wdl = ImGui::GetWindowDrawList();
			const float lineH = ImGui::GetTextLineHeight();
			// the tier line: the open one and what the next asks for (T323, the clan's own goals, in the player's words; `game tiers` keeps its text)
			if (tiers)
			{
				std::string line = L.Text("Tier {0} open", openTier);
				const std::string next = TierGoalsText(t, openTier + 1, true);
				if (!next.empty()) line += "  \xC2\xB7  " + L.Text("tier {0} asks for {1}", openTier + 1, next);
				else if (openTier >= eraTier) line += "  \xC2\xB7  " + L.Text("every tier is open");
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", line.c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Inventing a technology needs its tier open; learning it from someone who knows it does not").c_str());
				if (!ownQueue && !aim.empty()) { ImGui::SameLine(0.0f, 24.0f); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::BoneDim), "%s", L.Text("The clan seeks: {0}", techName(aim)).c_str()); }
			}
			else if (!ownQueue && !aim.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("The clan seeks: {0}", techName(aim)).c_str());
			// the legend: each state with the shape its cards have (wrapped when the window is narrow)
			{
				const float right = ImGui::GetContentRegionAvail().x + ImGui::GetCursorScreenPos().x;
				bool first = true;
				auto legend = [&](const std::string& text, ImU32 frame, bool dashed, float thick, const char* mark)
				{
					const ImVec2 size = ImGui::CalcTextSize(text.c_str());
					if (!first)
					{
						ImGui::SameLine(0.0f, 12.0f);
						if (ImGui::GetCursorScreenPos().x + size.x + 30.0f > right) ImGui::NewLine();
					}
					first = false;
					const ImVec2 at = ImGui::GetCursorScreenPos();
					const ImVec2 a(at.x, at.y + 1.0f), b(at.x + 22.0f, at.y + size.y + 1.0f);
					wdl->AddRectFilled(a, b, T::HideRaised, 3.0f);
					if (dashed) DashedRect(wdl, a, b, frame, thick);
					else wdl->AddRect(a, b, frame, 3.0f, 0, thick);
					if (mark) wdl->AddText(ImVec2(a.x + 11.0f - ImGui::CalcTextSize(mark).x * 0.5f, a.y), frame, mark);
					wdl->AddText(ImVec2(b.x + 5.0f, at.y + 1.0f), T::BoneDim, text.c_str());
					ImGui::Dummy(ImVec2(size.x + 28.0f, size.y + 2.0f));
				};
				legend(L.Text("known"), T::Good, false, 1.0f, "v");
				legend(L.Text("being studied"), T::Ochre, false, 3.0f, nullptr);
				legend(L.Text("in the queue"), T::Ochre, false, 1.0f, "1");
				legend(L.Text("on their own"), T::BoneDim, false, 1.0f, "~");
				legend(L.Text("can be studied now"), T::Bone, false, 1.0f, "+");
				legend(L.Text("something is missing"), T::Warn, true, 1.5f, "!");
				legend(L.Text("another clan knows it"), T::News, false, 1.5f, nullptr);
				legend(L.Text("not known yet"), T::HideLine, true, 1.0f, "?");
			}

			// ---- the room: the canvas over the strip of the queue
			const float stripH = ownQueue ? lineH * 3.0f + 62.0f : 0.0f;
			const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
			const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, std::max(160.0f, ImGui::GetContentRegionAvail().y - stripH));
			const float headerH = lineH * 2.0f + 14.0f, labelW = std::min(126.0f, canvasSize.x * 0.1f + 8.0f);
			const float eraW = eraBlock ? std::clamp(std::max(ImGui::CalcTextSize(L.Text("signs of a village").c_str()).x, ImGui::CalcTextSize(L.Text("to discover").c_str()).x + 16.0f) + 16.0f, 92.0f, 150.0f) : 0.0f;
			const float colPad = 6.0f, itemGap = 6.0f, smallW = 26.0f;
			const float availW = canvasSize.x - 2.0f * ImGui::GetStyle().WindowPadding.x - labelW - eraW - 4.0f;   // (the child's own padding)
			// the header of each column (its tier, what opens it, the "?" it keeps): its width bounds the column's
			struct Head { std::string Title, Sub, Count, Tip; ImU32 Ink = T::Bone, Top = T::HideLine; int Met = 0, Need = 0; };
			std::vector<Head> heads(static_cast<size_t>(columns));
			for (int c = 0; c < columns; ++c)
			{
				Head& hd = heads[static_cast<size_t>(c)];
				if (!tiers) { hd.Title = L.Text("Technologies"); continue; }
				hd.Title = L.Text("Tier {0}", c);
				const int unnamed = reveal ? 0 : status.UnnamedIn(c);
				if (unnamed > 0) hd.Count = "? " + std::to_string(unnamed);
				if (c == 0) { hd.Sub = L.Text("at the start"); hd.Ink = T::Good; hd.Top = T::Good; }
				else if (c <= openTier) { hd.Sub = L.Text("open"); hd.Ink = T::Ochre; hd.Top = T::Ochre; }
				else
				{
					hd.Ink = T::BoneDim;
					// "closed" in words in its title (not only the dim shade), what opens it below (the next one), or the one it comes after
					hd.Title += " \xC2\xB7 " + L.Text("closed");
					hd.Sub = c == openTier + 1 ? TierGoalsText(t, c, false) : L.Text("after tier {0}", c - 1);
					hd.Tip = L.Text("Tier {0} opens with {1}", c, TierGoalsText(t, c, true));
					for (const TierStep& step : tree->Rules.Tiers)
						if (step.Tier == c)
						{
							hd.Need = step.Need > 0 ? step.Need : static_cast<int>(step.Goals.size());
							for (const TierGoal& g : step.Goals) hd.Met += TechTiers::Met(t, g) ? 1 : 0;
						}
					if (c == openTier + 1) hd.Top = T::Faded(T::Ochre, 0.5f);
				}
				if (c == openTier && c > 0) hd.Sub = L.Text("open: this is where the clan is");
			}
			// the "? N" goes on the line where it widens the head least
			auto countOnSub = [&](const Head& hd)
			{
				if (hd.Count.empty()) return false;
				const float t = ImGui::CalcTextSize(hd.Title.c_str()).x, s = ImGui::CalcTextSize(hd.Sub.c_str()).x, n = ImGui::CalcTextSize(hd.Count.c_str()).x + 12.0f;
				return std::max(t, s + n) < std::max(t + n, s);
			};
			auto headMin = [&](int c)
			{
				const Head& hd = heads[static_cast<size_t>(c)];
				const float n = hd.Count.empty() ? 0.0f : ImGui::CalcTextSize(hd.Count.c_str()).x + 12.0f;
				const bool onSub = countOnSub(hd);
				const float title = ImGui::CalcTextSize(hd.Title.c_str()).x + (onSub ? 0.0f : n), sub = ImGui::CalcTextSize(hd.Sub.c_str()).x + (onSub ? n : 0.0f);
				return std::min(std::max(title, sub), 230.0f) + 2.0f * colPad + 4.0f;   // (+4: the gap the shade leaves at the column's right)
			};
			// the widths: every cell in one row with cards of 150; then narrower cards, then the most crowded column wraps
			float nodeW = 196.0f;
			std::vector<int> cap(static_cast<size_t>(columns), 1);
			auto count = [&](int c, size_t l) -> std::pair<int, int>   // (cards, "?" boxes)
			{
				const auto col = cells.find(c);
				if (col == cells.end()) return { 0, 0 };
				const auto cell = col->second.find(l);
				if (cell == col->second.end()) return { 0, 0 };
				int wide = 0, boxes = 0;
				for (const std::string& id : cell->second) (small(id) ? boxes : wide) += 1;
				return { wide, boxes };
			};
			for (int c = 0; c < columns; ++c) for (size_t l : usedLanes) { const auto [a, b] = count(c, l); cap[static_cast<size_t>(c)] = std::max(cap[static_cast<size_t>(c)], a + b); }
			auto colWidth = [&](int c)
			{
				float widest = 0.0f;
				const int kc = cap[static_cast<size_t>(c)];
				for (size_t l : usedLanes)
				{
					const auto [wide, boxes] = count(c, l);
					// the cards first, then the boxes, `kc` to a row
					for (int start = 0; start < wide + boxes; start += kc)
					{
						float row = 0.0f;
						for (int i = start; i < std::min(start + kc, wide + boxes); ++i) row += (i < wide ? nodeW : smallW) + (i > start ? itemGap : 0.0f);
						widest = std::max(widest, row);
					}
				}
				return std::max(widest + 2.0f * colPad, headMin(c));
			};
			auto total = [&]() { float s = 0.0f; for (int c = 0; c < columns; ++c) s += colWidth(c); return s; };
			const float laneGap = 4.0f, rowGap = 3.0f;
			const float laneAreaH = canvasSize.y - 2.0f * ImGui::GetStyle().WindowPadding.y - headerH - 6.0f;
			auto rowsIn = [&](size_t l)
			{
				int rows = 1;
				for (int c = 0; c < columns; ++c) { const auto [a, b] = count(c, l); rows = std::max(rows, (a + b + cap[static_cast<size_t>(c)] - 1) / cap[static_cast<size_t>(c)]); }
				return rows;
			};
			// the height the lanes need with cards of two lines (a name and its state)
			auto heightAt = [&](float cardH)
			{
				int rows = 0;
				for (size_t l : usedLanes) rows += rowsIn(l);
				return static_cast<float>(rows) * cardH + rowGap * static_cast<float>(rows - static_cast<int>(usedLanes.size())) + laneGap * static_cast<float>(usedLanes.size());
			};
			const float twoLines = lineH * 2.0f + 6.0f;
			// narrower cards down to 172; then a crowded column wraps while the rows still fit; then narrower cards down to 124; then it wraps anyway
			for (int guard = 0; guard < 96 && total() > availW; ++guard)
			{
				if (nodeW > 172.0f) { nodeW -= 6.0f; continue; }
				int best = -1;
				float bestW = 0.0f;
				for (int c = 0; c < columns; ++c) if (cap[static_cast<size_t>(c)] > 1 && colWidth(c) > bestW) { bestW = colWidth(c); best = c; }
				if (best >= 0)
				{
					--cap[static_cast<size_t>(best)];
					if (heightAt(twoLines) <= laneAreaH || nodeW <= 124.0f) continue;
					++cap[static_cast<size_t>(best)];
				}
				if (nodeW > 124.0f) { nodeW -= 6.0f; continue; }
				if (best < 0) break;
				--cap[static_cast<size_t>(best)];
			}
			// what width is left widens the cards again (long names on fewer lines), up to 230
			for (int guard = 0; guard < 64 && nodeW < 230.0f; ++guard)
			{
				nodeW += 2.0f;
				if (total() > availW) { nodeW -= 2.0f; break; }
			}
			std::vector<float> colW(static_cast<size_t>(columns));
			float used = 0.0f;
			for (int c = 0; c < columns; ++c) { colW[static_cast<size_t>(c)] = colWidth(c); used += colW[static_cast<size_t>(c)]; }
			// what is left goes to the open tier (its cards breathe) or, with none, to the last column
			if (used < availW && columns > 0) colW[static_cast<size_t>(std::clamp(tiers ? openTier : columns - 1, 0, columns - 1))] += availW - used;
			// the rows of each lane, and the height of a card from what is left
			std::vector<int> laneRows;
			int rowsTotal = 0;
			for (size_t l : usedLanes)
			{
				const int rows = rowsIn(l);
				laneRows.push_back(rows);
				rowsTotal += rows;
			}
			float nodeH = (laneAreaH - laneGap * static_cast<float>(usedLanes.size()) - rowGap * static_cast<float>(rowsTotal - static_cast<int>(usedLanes.size()))) / static_cast<float>(std::max(rowsTotal, 1));
			nodeH = std::clamp(nodeH, 30.0f, 64.0f);
			const float contentH = headerH + 8.0f + static_cast<float>(rowsTotal) * nodeH + rowGap * static_cast<float>(rowsTotal - static_cast<int>(usedLanes.size())) + laneGap * static_cast<float>(usedLanes.size());
			const bool overflow = contentH > canvasSize.y + 1.0f;   // (only with very little room: then, and only then, it scrolls)

			ImGui::BeginChild("canvas", canvasSize, ImGuiChildFlags_Borders, overflow ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			const ImVec2 o = ImGui::GetCursorScreenPos();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float innerW = ImGui::GetContentRegionAvail().x;
			// where every card goes
			struct Slot { std::string Id; bool Small = false; ImVec2 A, B; };
			std::vector<Slot> slots;
			std::map<std::string, size_t> slotOf;
			std::vector<float> colX(static_cast<size_t>(columns) + 1);
			colX[0] = o.x + labelW;
			for (int c = 0; c < columns; ++c) colX[static_cast<size_t>(c) + 1] = colX[static_cast<size_t>(c)] + colW[static_cast<size_t>(c)];
			std::vector<float> laneY(usedLanes.size() + 1);
			laneY[0] = o.y + headerH + 4.0f;
			for (size_t i = 0; i < usedLanes.size(); ++i) laneY[i + 1] = laneY[i] + static_cast<float>(laneRows[i]) * (nodeH + rowGap) - rowGap + laneGap;
			for (size_t li = 0; li < usedLanes.size(); ++li)
				for (int c = 0; c < columns; ++c)
				{
					const auto col = cells.find(c);
					if (col == cells.end()) continue;
					const auto cell = col->second.find(usedLanes[li]);
					if (cell == col->second.end()) continue;
					const int kc = cap[static_cast<size_t>(c)];
					float x = colX[static_cast<size_t>(c)] + colPad;
					int inRow = 0, row = 0;
					for (const std::string& id : cell->second)
					{
						if (inRow == kc) { inRow = 0; ++row; x = colX[static_cast<size_t>(c)] + colPad; }
						const bool s = small(id);
						const float y = laneY[li] + laneGap * 0.5f + static_cast<float>(row) * (nodeH + rowGap);
						Slot slot{ id, s, ImVec2(x, y), ImVec2(x + (s ? smallW : nodeW), y + nodeH) };
						if (s) { const float side = std::min(smallW, nodeH - 4.0f); slot.A.y = y + (nodeH - side) * 0.5f; slot.B = ImVec2(x + side, slot.A.y + side); }
						slotOf[id] = slots.size();
						slots.push_back(slot);
						x += (s ? smallW : nodeW) + itemGap;
						++inRow;
					}
				}
			// the one under the mouse (worked out before drawing, so its lines go under the cards)
			std::string hover;
			if (ImGui::IsWindowHovered())
				for (const Slot& s : slots) if (ImGui::IsMouseHoveringRect(s.A, s.B)) { hover = s.Id; break; }

			// ---- the grid: the columns' heads and shades, the lanes' names
			const float gridBottom = laneY.back();
			for (int c = 0; c < columns; ++c)
			{
				const Head& hd = heads[static_cast<size_t>(c)];
				const float x0 = colX[static_cast<size_t>(c)], x1 = colX[static_cast<size_t>(c) + 1] - 3.0f;
				const bool closed = tiers && c > openTier;
				dl->AddRectFilled(ImVec2(x0, o.y), ImVec2(x1, gridBottom), closed ? T::Faded(T::Hide, 0.55f) : T::Faded(T::HideRaised, 0.45f), 4.0f);
				dl->AddLine(ImVec2(x0, o.y + 1.0f), ImVec2(x1, o.y + 1.0f), hd.Top, 3.0f);
				const float tx = x0 + colPad, inner = x1 - x0 - 2.0f * colPad;
				const bool onSub = countOnSub(hd);
				const float countW = hd.Count.empty() ? 0.0f : ImGui::CalcTextSize(hd.Count.c_str()).x + 12.0f;
				dl->AddText(ImVec2(tx, o.y + 5.0f), hd.Ink, Fit(hd.Title, inner - (onSub ? 0.0f : countW)).c_str());
				if (!hd.Count.empty()) dl->AddText(ImVec2(x1 - colPad - ImGui::CalcTextSize(hd.Count.c_str()).x, o.y + (onSub ? 6.0f + lineH : 5.0f)), T::BoneDim, hd.Count.c_str());
				bool cut = false;
				dl->AddText(ImVec2(tx, o.y + 6.0f + lineH), closed ? T::BoneDim : hd.Ink, Fit(hd.Sub, inner - (onSub ? countW : 0.0f), &cut).c_str());
				// the goals of the next tier as segments (met of needed)
				if (hd.Need > 0)
				{
					const float segW = std::min(28.0f, (inner - 4.0f * static_cast<float>(hd.Need - 1)) / static_cast<float>(hd.Need)), y = o.y + 9.0f + 2.0f * lineH;
					for (int i = 0; i < hd.Need; ++i)
						dl->AddRectFilled(ImVec2(tx + static_cast<float>(i) * (segW + 4.0f), y), ImVec2(tx + static_cast<float>(i) * (segW + 4.0f) + segW, y + 3.0f), i < hd.Met ? T::Ochre : T::HideLine, 1.0f);
				}
				if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(x0, o.y), ImVec2(x1, o.y + headerH)))
					ImGui::SetTooltip("%s", (hd.Title + (hd.Count.empty() ? std::string() : "  " + hd.Count) + "\n" + (hd.Tip.empty() ? hd.Sub : hd.Tip)
						+ (closed ? "\n" + L.Text("What is not open shows as \"?\": another clan can still teach it") : std::string())).c_str());
			}
			for (size_t li = 0; li < usedLanes.size(); ++li)
			{
				const std::string& b = lanes[usedLanes[li]];
				const float y0 = laneY[li], y1 = laneY[li + 1] - laneGap;
				if (li > 0) dl->AddLine(ImVec2(o.x, y0 - laneGap * 0.5f + 1.0f), ImVec2(colX.back(), y0 - laneGap * 0.5f + 1.0f), T::Faded(T::HideLine, 0.5f), 1.0f);
				const std::string word = b.empty() ? L.Text("others") : Lang::Cap(L.Name("branch", b, b));
				glm::vec2 uv0(0.0f), uv1(0.0f);
				float tx = o.x + 4.0f;
				if (usedLanes[li] < namedLanes && IconUv(BranchIcon(b), uv0, uv1))
				{
					const float s = std::min(22.0f, y1 - y0);
					const float iy = (y0 + y1) * 0.5f - s * 0.5f;
					dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_IconTexture->GetID())), ImVec2(tx, iy), ImVec2(tx + s, iy + s), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
					tx += s + 6.0f;
				}
				const std::vector<std::string> lines = Wrap(word, o.x + labelW - 6.0f - tx, 2);
				float ty = (y0 + y1) * 0.5f - static_cast<float>(lines.size()) * lineH * 0.5f;
				for (const std::string& l : lines) { dl->AddText(ImVec2(tx, ty), T::Bone, l.c_str()); ty += lineH; }
			}

			// ---- the lines: only the picked one's and the one under the mouse: what it needs (green), what it opens (ochre)
			auto lines = [&](const std::string& id, bool strong)
			{
				const auto at = slotOf.find(id);
				if (at == slotOf.end()) return;
				const Slot& me = slots[at->second];
				auto curve = [&](const Slot& from, const Slot& to, ImU32 c)
				{
					const ImVec2 p0(from.B.x, (from.A.y + from.B.y) * 0.5f), p1(to.A.x, (to.A.y + to.B.y) * 0.5f);
					const float bend = std::max(30.0f, (p1.x - p0.x) * 0.5f);
					dl->AddBezierCubic(p0, ImVec2(p0.x + bend, p0.y), ImVec2(p1.x - bend, p1.y), p1, c, strong ? 2.5f : 1.8f);
				};
				if (const TechDef* d = tree->Find(id))
					for (const std::string& r : d->Requires) if (const auto s = slotOf.find(r); s != slotOf.end()) curve(slots[s->second], me, T::Good);
				for (const std::string& n : opens[id]) if (const auto s = slotOf.find(n); s != slotOf.end()) curve(me, slots[s->second], small(n) ? T::Faded(T::Ochre, 0.6f) : T::Ochre);
			};
			if (!pick.empty()) lines(pick, true);
			if (!hover.empty() && hover != pick) lines(hover, false);

			// ---- the cards
			for (const Slot& s : slots)
			{
				const std::string& id = s.Id;
				const TechStatus::Entry& e = status.Of(id);
				const ImVec2 a = s.A, b = s.B;
				ImGui::SetCursorScreenPos(a);
				ImGui::PushID(id.c_str());
				ImGui::InvisibleButton("node", ImVec2(b.x - a.x, b.y - a.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				const bool clickL = ImGui::IsItemClicked(ImGuiMouseButton_Left), clickR = ImGui::IsItemClicked(ImGuiMouseButton_Right);
				ImGui::PopID();
				const bool hovered = hover == id;
				if (clickL) pick = pick == id ? std::string() : id;
				// a right click: into the queue, or out of it (the player's clan; only what it can name)
				if (clickR && ownQueue && named(id) && e.State != TechState::Known) run(e.Queue > 0 ? "game research remove " + id : "game research add " + id);
				const bool live = e.State != TechState::Unknown && e.State != TechState::Blocked;
				if (s.Small)
				{
					dl->AddRectFilled(a, b, T::Hide, 3.0f);
					glm::vec2 uv0(0.0f), uv1(0.0f);
					const ImU32 frame = !reveal || e.State == TechState::Blocked ? T::Warn : T::HideLine;   // (the god sees every name: amber only for what the tier holds back)
					if (named(id) && IconUv("tech." + id, uv0, uv1))
					{
						DashedRect(dl, a, b, frame, 1.0f);
						dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_IconTexture->GetID())), ImVec2(a.x + 2.0f, a.y + 2.0f), ImVec2(b.x - 2.0f, b.y - 2.0f), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y), IM_COL32(255, 255, 255, 150));
					}
					else
					{
						DashedRect(dl, a, b, named(id) ? frame : T::HideLine, 1.0f);
						// (no icon: "!" for the player, the first letters of its name for the god)
						std::string mark = !named(id) ? std::string("?") : !reveal ? std::string("!") : techName(id).substr(0, 2);
						if (mark.size() == 2 && (static_cast<unsigned char>(mark[1]) & 0x80)) mark = mark.substr(0, 1);
						const ImVec2 q = ImGui::CalcTextSize(mark.c_str());
						dl->AddText(ImVec2((a.x + b.x - q.x) * 0.5f, (a.y + b.y - q.y) * 0.5f), named(id) ? frame == T::Warn ? T::Warn : T::Bone : T::BoneDim, mark.c_str());
					}
					if (hovered || id == pick) dl->AddRect(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f), id == pick ? T::Bone : T::OchreHover, 4.0f, 0, 1.5f);
					if (hovered) ImGui::SetTooltip("%s", ((named(id) ? techName(id) + (reveal ? ": " + TechStateText(status, id) : std::string()) : L.Text("Something the clan does not know yet")) + "\n" + L.Text("needs tier {0}", e.Tier)).c_str());
					continue;
				}
				// the body and the frame: the shape says the state (thick, dashed, blue...), the colour backs it
				dl->AddRectFilled(a, b, e.State == TechState::Known ? T::Faded(T::Good, 0.30f) : live || reveal ? T::HideRaised : T::Hide, 6.0f);
				switch (e.State)
				{
				case TechState::Known: dl->AddRect(a, b, T::Good, 6.0f, 0, 1.0f); break;
				case TechState::Studying: dl->AddRect(a, b, T::Ochre, 6.0f, 0, 3.0f); break;
				case TechState::Queued: dl->AddRect(a, b, T::Ochre, 6.0f, 0, 1.5f); break;
				case TechState::OwnPace: dl->AddRect(a, b, T::BoneDim, 6.0f, 0, 1.0f); break;
				case TechState::Studiable: dl->AddRect(a, b, T::Bone, 6.0f, 0, 1.5f); break;
				case TechState::Blocked: DashedRect(dl, a, b, T::Warn, 1.5f); break;
				case TechState::OtherClan: dl->AddRect(a, b, T::News, 6.0f, 0, 2.0f); break;
				case TechState::Unknown: DashedRect(dl, a, b, T::HideLine, 1.0f); break;
				}
				if (id == pick || hovered) dl->AddRect(ImVec2(a.x - 3.0f, a.y - 3.0f), ImVec2(b.x + 3.0f, b.y + 3.0f), id == pick ? T::Bone : T::OchreHover, 8.0f, 0, 1.5f);
				// "it needs" / "it opens" over the cards of the picked one's neighbours
				if (!pick.empty() && pick != id)
				{
					const TechDef* pd = tree->Find(pick);
					const bool needs = pd && std::find(pd->Requires.begin(), pd->Requires.end(), id) != pd->Requires.end();
					const std::vector<std::string>& po = opens[pick];
					const bool opened = std::find(po.begin(), po.end(), id) != po.end();
					if (needs || opened)
					{
						const std::string tag = needs ? L.Text("it needs this") : L.Text("it opens this");
						const ImVec2 ts = ImGui::CalcTextSize(tag.c_str());
						const ImVec2 ta((a.x + b.x - ts.x) * 0.5f - 4.0f, a.y - ts.y * 0.5f - 1.0f);
						dl->AddRectFilled(ta, ImVec2(ta.x + ts.x + 8.0f, ta.y + ts.y + 2.0f), needs ? T::Good : T::Ochre, 6.0f);
						dl->AddText(ImVec2(ta.x + 4.0f, ta.y + 1.0f), T::Ink, tag.c_str());
					}
				}
				const float nh = b.y - a.y;
				if (!named(id))
				{
					// unknown: a "?" and where it comes after; no icon, no name (D2)
					const float big = std::min(ImGui::GetFontSize() * 2.0f, nh - 6.0f);
					dl->AddText(ImGui::GetFont(), big, ImVec2(a.x + 10.0f, a.y + (nh - big) * 0.5f), T::BoneDim, "?");
					const std::string hint = e.Hint.empty() ? L.Text("not known yet") : L.Text("after {0}", techName(e.Hint));
					const int maxLines = std::max(1, static_cast<int>((nh - 6.0f) / lineH));
					const std::vector<std::string> hl = Wrap(hint, b.x - a.x - 40.0f, std::min(maxLines, 2));
					float ty = a.y + (nh - static_cast<float>(hl.size()) * lineH) * 0.5f;
					for (const std::string& l : hl) { dl->AddText(ImVec2(a.x + 34.0f, ty), T::BoneDim, l.c_str()); ty += lineH; }
					if (hovered) ImGui::SetTooltip("%s", (L.Text("Something the clan does not know yet") + "\n" + hint).c_str());
					continue;
				}
				const TechDef& d = *tree->Find(id);
				// its own icon (tech.<id>) when the atlas has it, else what it opens
				glm::vec2 uv0(0.0f), uv1(0.0f);
				std::string icon = IconUv("tech." + id, uv0, uv1) ? "tech." + id : std::string();
				if (icon.empty()) for (const std::string& r : d.Recipes) if (const Recipe* rec = m_Recipes.Find(r)) { icon = rec->Output; break; }
				if (icon.empty() && !d.Buildings.empty()) icon = "build";
				float textX = a.x + 7.0f;
				const float iconS = std::min(36.0f, nh - 12.0f);
				if (!icon.empty() && IconUv(icon, uv0, uv1) && iconS >= 16.0f)
				{
					dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_IconTexture->GetID())), ImVec2(a.x + 5.0f, a.y + (nh - iconS) * 0.5f), ImVec2(a.x + 5.0f + iconS, a.y + (nh + iconS) * 0.5f), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y),
						live || e.State == TechState::Known ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 120));
					textX = a.x + iconS + 10.0f;
				}
				// the name (two lines when it needs them and the card has room), and the state in a word below it
				const float textW = b.x - 15.0f - textX;
				const int fits = std::max(1, static_cast<int>((nh - 4.0f) / lineH));
				// (a known one on a low card gives its state's line to a long name: the green fill and the tick already say it)
				const bool knownLong = e.State == TechState::Known && fits == 2 && ImGui::CalcTextSize(techName(id).c_str()).x > textW;
				const int nameLines = fits >= 3 || knownLong ? 2 : 1;
				bool cut = false;
				const std::vector<std::string> nl = Wrap(techName(id), textW, nameLines, &cut);
				// (in the queue: only its place on the card; what it waits for is in the tooltip and the strip)
				const std::string word = TechStateText(status, id);
				std::string shortWord = e.State == TechState::Queued ? L.Text("number {0} in the queue", e.Queue) : e.State == TechState::Studiable ? L.Text("can be studied") : word;
				if (e.State == TechState::Blocked && e.Why == TechWhy::Material && fits < 4)   // (a low card: "lacks flint"; "at hand" is in the tooltip)
				{
					std::string items;
					for (const std::string& m : e.Missing) items += (items.empty() ? "" : ", ") + L.Name("item", m, m);
					shortWord = L.Text("lacks {0}", items);
				}
				const int wordLines = std::min(2, fits - static_cast<int>(nl.size()));
				const std::vector<std::string> wl = wordLines > 0 ? Wrap(shortWord, b.x - 5.0f - textX, wordLines, &cut) : std::vector<std::string>();
				const float blockH = static_cast<float>(nl.size() + wl.size()) * lineH;
				float ty = a.y + std::max(3.0f, (nh - blockH) * 0.5f - 1.0f);
				for (const std::string& l : nl) { dl->AddText(ImVec2(textX, ty), live || e.State == TechState::Known ? T::Bone : T::BoneDim, l.c_str()); ty += lineH; }
				const ImU32 wordInk = e.State == TechState::Blocked ? T::Warn : e.State == TechState::OtherClan ? T::News : e.State == TechState::Studying || e.State == TechState::Queued ? T::Ochre : T::BoneDim;
				for (const std::string& l : wl) { dl->AddText(ImVec2(textX, ty), wordInk, l.c_str()); ty += lineH; }
				// the mark: a tick in the corner, the place in the queue in a badge, "+", "!" or "~"
				const ImVec2 corner(b.x - 9.0f, a.y + 9.0f);
				switch (e.State)
				{
				case TechState::Known:
					dl->AddLine(ImVec2(corner.x - 5.0f, corner.y), ImVec2(corner.x - 1.5f, corner.y + 4.0f), T::Good, 2.0f);
					dl->AddLine(ImVec2(corner.x - 1.5f, corner.y + 4.0f), ImVec2(corner.x + 5.0f, corner.y - 4.0f), T::Good, 2.0f);
					break;
				case TechState::Studying:
				case TechState::Queued:
				{
					const std::string n = std::to_string(e.Queue);
					dl->AddCircleFilled(ImVec2(a.x + 2.0f, a.y + 2.0f), 9.0f, T::Ochre, 16);
					const ImVec2 ns = ImGui::CalcTextSize(n.c_str());
					dl->AddText(ImVec2(a.x + 2.0f - ns.x * 0.5f, a.y + 2.0f - ns.y * 0.5f), T::Ink, n.c_str());
					break;
				}
				case TechState::Studiable: dl->AddText(ImVec2(corner.x - 4.0f, corner.y - 7.0f), T::Ochre, "+"); break;
				case TechState::Blocked: dl->AddText(ImVec2(corner.x - 3.0f, corner.y - 7.0f), T::Warn, "!"); break;
				case TechState::OwnPace: dl->AddText(ImVec2(corner.x - 4.0f, corner.y - 7.0f), T::BoneDim, "~"); break;
				default: break;
				}
				// the bar: thick and ochre while studied, thin and pale when they learn it on their own
				if (e.State == TechState::Studying || ((e.State == TechState::OwnPace || e.State == TechState::Queued) && e.Progress > 0.0f))
				{
					const float v = std::clamp(e.Progress, 0.0f, 1.0f), th = e.State == TechState::Studying ? 4.0f : 2.0f;
					const ImU32 fill = e.State == TechState::OwnPace ? T::BoneDim : T::Ochre;
					dl->AddRectFilled(ImVec2(a.x + 6.0f, b.y - 3.0f - th), ImVec2(b.x - 6.0f, b.y - 3.0f), T::Hide, 2.0f);
					dl->AddRectFilled(ImVec2(a.x + 6.0f, b.y - 3.0f - th), ImVec2(a.x + 6.0f + (b.x - 12.0f - a.x) * v, b.y - 3.0f), fill, 2.0f);
				}
				if (hovered)
				{
					std::string tip = techName(id) + ": " + word;
					if (ownQueue && e.State != TechState::Known) tip += "\n" + L.Text(e.Queue > 0 ? "click: its card  \xC2\xB7  right click: out of the queue" : "click: its card  \xC2\xB7  right click: add it to the queue");
					else tip += "\n" + L.Text("click: its card");
					ImGui::SetTooltip("%s", tip.c_str());
				}
			}
			// D6: era 2 as one block, after the last column, over every lane
			if (eraBlock)
			{
				const ImVec2 a(colX.back() + 2.0f, o.y), b(std::min(colX.back() + eraW, o.x + innerW) - 2.0f, gridBottom);
				dl->AddRectFilled(a, b, T::Faded(T::Hide, 0.55f), 4.0f);
				dl->AddLine(ImVec2(a.x, a.y + 1.0f), ImVec2(b.x, a.y + 1.0f), T::HideLine, 3.0f);
				dl->AddText(ImVec2(a.x + colPad, a.y + 5.0f), T::BoneDim, Fit(L.Text("Era 2"), b.x - a.x - 2.0f * colPad).c_str());
				dl->AddText(ImVec2(a.x + colPad, a.y + 6.0f + lineH), T::BoneDim, Fit(L.Text("signs of a village"), b.x - a.x - 2.0f * colPad).c_str());
				const ImVec2 ba(a.x + 8.0f, laneY.front() + 8.0f), bb(b.x - 8.0f, gridBottom - 8.0f);
				DashedRect(dl, ba, bb, T::HideLine, 1.0f);
				const float big = ImGui::GetFontSize() * 2.0f;
				const std::string n = reveal ? std::to_string(eraNamed.size()) : "? " + std::to_string(eraHidden);   // (the god: how many, their names in the tooltip)
				const float nw = ImGui::CalcTextSize(n.c_str()).x * 2.0f;
				const float my = (ba.y + bb.y) * 0.5f;
				dl->AddText(ImGui::GetFont(), big, ImVec2((ba.x + bb.x - nw) * 0.5f, my - big), T::BoneDim, n.c_str());
				const std::vector<std::string> wl = Wrap(reveal ? L.Text("technologies") : L.Text("to discover"), bb.x - ba.x - 8.0f, 2);
				float ty = my + 4.0f;
				for (const std::string& l : wl) { dl->AddText(ImVec2((ba.x + bb.x - ImGui::CalcTextSize(l.c_str()).x) * 0.5f, ty), T::BoneDim, l.c_str()); ty += lineH; }
				if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b))
				{
					std::string tip = reveal ? L.Text("{0} technologies of the next age", static_cast<int>(eraNamed.size())) : L.Text("{0} technologies of the next age: they show once the village gets there", eraHidden);
					if (const std::string goals = TierGoalsText(t, eraTier, true); !goals.empty()) tip += "\n" + L.Text("Tier {0} opens with {1}", eraTier, goals);
					for (const std::string& id : eraNamed) tip += "\n" + techName(id) + ": " + TechStateText(status, id);
					ImGui::SetTooltip("%s", tip.c_str());
				}
			}
			// the space the tree takes (it only scrolls when it cannot fit at all)
			ImGui::SetCursorScreenPos(o);
			ImGui::Dummy(ImVec2(innerW, std::min(contentH, overflow ? contentH : canvasSize.y - 8.0f)));
			ImGui::EndChild();

			// ---- the strip of the study queue (the player's clan), under the tree
			if (ownQueue) DrawStudyStrip(t, status, canvasSize.x);
			else if (!pick.empty() || reveal)
			{
				ImGui::TextDisabled("%s", L.Text("Click a technology to see who is on it, what it needs and what it opens.").c_str());
			}

			// ---- the card of the picked technology, on the right over the far tiers (a2)
			if (!pick.empty())
			{
				const float cw = std::clamp(canvasSize.x * 0.40f, 380.0f, 580.0f);
				ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + canvasSize.x - cw - 4.0f, canvasPos.y + 4.0f));
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(T::HidePanel));
				ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(T::HideLine));
				ImGui::BeginChild("card", ImVec2(cw, canvasSize.y - 8.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
				DrawTechCard(t, status, playing, ownQueue, pick);
				ImGui::EndChild();
				ImGui::PopStyleColor(2);
			}
		}
		ImGui::End();
	}

	// the strip of the study queue under the tree (a1): who studies, the queue as cards (drag one onto another: `game research move`), and what
	// they learn on their own. The Knowledge sheet (ColonyTabs.cpp, a3) shows the same at full size.
	void EditorApp::DrawStudyStrip(const Tribe& t, const TechStatus& status, float width)
	{
		const Lang& L = m_Lang;
		const TechTree* tree = t.Knowledge().Techs();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto techName = [&](const std::string& id) { const TechDef* d = tree ? tree->Find(id) : nullptr; return Lang::Cap(L.Name("tech", id, d ? d->Name : std::string())); };
		const float lineH = ImGui::GetTextLineHeight();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImGui::Spacing();
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("Study queue").c_str());
		ImGui::SameLine(0.0f, 18.0f);
		std::string who;
		for (const auto& [name, n] : StudyPeople(t)) if (n > 0) who += (who.empty() ? "" : ", ") + name;
		ImGui::TextDisabled("%s", (who.empty() ? L.Text("nobody has Knowledge in Work: nobody studies") : L.Text("studied by whoever has Knowledge in Work: {0}", who)).c_str());
		if (m_Director.Colony())
		{
			ImGui::SameLine(0.0f, 12.0f);
			if (ImGui::SmallButton((L.Text("Adjust") + "###stripwork").c_str())) run("colony tab work");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Who works on knowledge, in the Work sheet").c_str());
		}
		const std::vector<std::string>& queue = t.Research().All();
		const float cardH = lineH * 2.0f + 18.0f, hintW = std::min(300.0f, width * 0.24f);
		const float cardW = std::clamp((width - hintW - 8.0f) / std::max(1.0f, static_cast<float>(std::min<size_t>(queue.size(), 4))) - 8.0f, 200.0f, 290.0f);
		const int fit = std::max(1, static_cast<int>((width - hintW) / (cardW + 8.0f)));
		const ImVec2 row = ImGui::GetCursorScreenPos();
		float x = row.x;
		for (size_t i = 0; i < queue.size() && static_cast<int>(i) < fit; ++i)
		{
			const std::string& id = queue[i];
			const TechStatus::Entry& e = status.Of(id);
			const ImVec2 a(x, row.y), b(x + cardW, row.y + cardH);
			ImGui::SetCursorScreenPos(a);
			ImGui::PushID(id.c_str());
			ImGui::InvisibleButton("qcard", ImVec2(cardW - 30.0f, cardH));
			const bool hovered = ImGui::IsItemHovered();
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("GB_RESEARCH", id.c_str(), id.size() + 1);
				ImGui::TextUnformatted(techName(id).c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GB_RESEARCH"))
				{
					const std::string moved(static_cast<const char*>(p->Data));
					if (moved != id) run("game research move " + moved + " " + std::to_string(i + 1));
				}
				ImGui::EndDragDropTarget();
			}
			const bool studying = e.State == TechState::Studying;
			dl->AddRectFilled(a, b, T::HideRaised, 6.0f);
			dl->AddRect(a, b, studying ? T::Ochre : T::HideLine, 6.0f, 0, studying ? 2.5f : 1.0f);
			// the handle, the number, the icon
			for (int g = 0; g < 3; ++g) dl->AddCircleFilled(ImVec2(a.x + 7.0f, a.y + cardH * 0.5f - 5.0f + 5.0f * static_cast<float>(g)), 1.3f, T::BoneDim);
			const ImVec2 nc(a.x + 22.0f, a.y + cardH * 0.5f);
			dl->AddCircleFilled(nc, 9.0f, T::Ochre, 16);
			const std::string n = std::to_string(i + 1);
			dl->AddText(ImVec2(nc.x - ImGui::CalcTextSize(n.c_str()).x * 0.5f, nc.y - lineH * 0.5f), T::Ink, n.c_str());
			float tx = a.x + 36.0f;
			glm::vec2 uv0(0.0f), uv1(0.0f);
			if (status.Named(id) && IconUv("tech." + id, uv0, uv1))
			{
				const float s = cardH - 14.0f;
				dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_IconTexture->GetID())), ImVec2(tx, a.y + 7.0f), ImVec2(tx + s, a.y + 7.0f + s), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
				tx += s + 6.0f;
			}
			const float right = b.x - 34.0f;
			const std::string pct = std::to_string(static_cast<int>(std::lround(e.Progress * 100.0f))) + " %";
			const float pw = e.Progress > 0.0f ? ImGui::CalcTextSize(pct.c_str()).x + 6.0f : 0.0f;
			dl->AddText(ImVec2(tx, a.y + 5.0f), T::Bone, Fit(status.Named(id) ? techName(id) : std::string("?"), right - tx - pw).c_str());
			if (pw > 0.0f) dl->AddText(ImVec2(right - pw + 6.0f, a.y + 5.0f), T::Bone, pct.c_str());
			const std::string state = studying ? (e.Who.empty() ? L.Text("being studied") : L.Text("being studied: {0}", [&] { std::string list; for (const std::string& m : e.Who) list += (list.empty() ? "" : ", ") + m; return list; }()))
				: e.Why != TechWhy::None ? TechWhyText(status, id) : L.Text("next");
			dl->AddText(ImVec2(tx, a.y + 7.0f + lineH), studying ? T::Ochre : e.Why != TechWhy::None ? T::Warn : T::BoneDim, Fit(state, right - tx).c_str());
			if (e.Progress > 0.0f)
			{
				const float v = std::clamp(e.Progress, 0.0f, 1.0f);
				dl->AddRectFilled(ImVec2(tx, b.y - 6.0f), ImVec2(right, b.y - 3.0f), T::Hide, 1.0f);
				dl->AddRectFilled(ImVec2(tx, b.y - 6.0f), ImVec2(tx + (right - tx) * v, b.y - 3.0f), T::Ochre, 1.0f);
			}
			if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ImGui::SetTooltip("%s", (techName(id) + ": " + state + "\n" + L.Text("Drag it onto another card to move it there")).c_str());
			// out of the queue
			ImGui::SetCursorScreenPos(ImVec2(b.x - 28.0f, a.y + (cardH - ImGui::GetFrameHeight()) * 0.5f));
			if (ImGui::Button("\xC3\x97###qoff", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) run("game research remove " + id);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Take {0} out of the queue", techName(id)).c_str());
			ImGui::PopID();
			x += cardW + 8.0f;
		}
		if (static_cast<int>(queue.size()) > fit)
		{
			const std::string more = "+" + std::to_string(queue.size() - static_cast<size_t>(fit));
			dl->AddText(ImVec2(x, row.y + cardH * 0.5f - lineH * 0.5f), T::BoneDim, more.c_str());
			x += ImGui::CalcTextSize(more.c_str()).x + 10.0f;
		}
		// the hint: how to fill it
		{
			const ImVec2 a(x, row.y), b(std::min(row.x + width - 4.0f, x + std::max(hintW, row.x + width - 4.0f - x)), row.y + cardH);
			if (b.x - a.x > 80.0f)
			{
				DashedRect(dl, a, b, T::HideLine, 1.0f);
				const std::vector<std::string> wl = Wrap(queue.empty() ? L.Text("The queue is empty: right click a light card to add it, or open its card") : L.Text("+ right click a light card to add it; click it for its card"), b.x - a.x - 16.0f, 2);
				float ty = (a.y + b.y) * 0.5f - static_cast<float>(wl.size()) * lineH * 0.5f;
				for (const std::string& l : wl) { dl->AddText(ImVec2((a.x + b.x - ImGui::CalcTextSize(l.c_str()).x) * 0.5f, ty), T::BoneDim, l.c_str()); ty += lineH; }
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + cardH + 6.0f));
		ImGui::Dummy(ImVec2(0.0f, 0.0f));   // (an item after the cursor moved: ImGui asserts on a window grown by SetCursorScreenPos alone)
		// what they learn on their own, and what another clan knows
		std::string own;
		for (const auto& [id, e] : status.All())
		{
			if (!e.Shown) continue;
			if (e.State == TechState::OwnPace) own += (own.empty() ? "" : "  \xC2\xB7  ") + techName(id) + " ~" + std::to_string(static_cast<int>(std::lround(e.Progress * 100.0f))) + " %";
			else if (e.State == TechState::OtherClan) own += (own.empty() ? "" : "  \xC2\xB7  ") + L.Text("the {0} know {1}", L.Name("clan", e.From, e.From), techName(id));
		}
		if (!own.empty())
		{
			const std::string line = L.Text("On their own, outside the queue:") + "  " + own;
			bool cut = false;
			ImGui::TextDisabled("%s", Fit(line, width - 8.0f, &cut).c_str());
			if (cut && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", line.c_str());
		}
	}

	// the card of a technology (a2): who it is, the buttons, what it opens, what it needs (and the shortcut that mends it, D7), how it is learnt
	// (D5: three words, the number in the tooltip) and who is on it
	void EditorApp::DrawTechCard(const Tribe& t, const TechStatus& status, bool playing, bool ownQueue, std::string& pick)
	{
		const Lang& L = m_Lang;
		const TechTree* tree = t.Knowledge().Techs();
		const TechDef* d = tree ? tree->Find(pick) : nullptr;
		if (!d) return;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto techName = [&](const std::string& id) { const TechDef* x = tree->Find(id); return Lang::Cap(L.Name("tech", id, x ? x->Name : std::string())); };
		const bool reveal = !playing && !m_Director.Colony();
		auto named = [&](const std::string& id) { return reveal || status.Named(id); };
		const TechStatus::Entry& e = status.Of(pick);
		const float lineH = ImGui::GetTextLineHeight();
		const float box = ImGui::GetFrameHeight();
		// the close, top right
		{
			const ImVec2 back = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(ImVec2(back.x + ImGui::GetContentRegionAvail().x - box, back.y));
			if (ImGui::Button("\xC3\x97###cardclose", ImVec2(box, box))) pick.clear();
			ImGui::SetCursorScreenPos(back);
			if (pick.empty()) return;
		}
		auto section = [&](const std::string& text)
		{
			ImGui::Spacing();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", text.c_str());
		};
		if (!named(pick))
		{
			// one the clan cannot name: no name, no buttons; where it comes after
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::BoneDim), "?  %s", L.Text("Something the clan does not know yet").c_str());
			ImGui::TextWrapped("%s", (e.Hint.empty() ? L.Text("The clan must learn more before it can even think of it") : L.Text("It comes after {0}", techName(e.Hint))).c_str());
			if (e.Tier > TechTiers::Open(t)) ImGui::TextWrapped("%s", L.Text("needs tier {0}", e.Tier).c_str());
			return;
		}
		const bool have = e.State == TechState::Known;
		// ---- the head: the icon, the name large, its branch and tier, the state in a sentence
		const float wrapW = ImGui::GetContentRegionAvail().x - box - 6.0f;
		if (Icon("tech." + pick, 44.0f)) ImGui::SameLine(0.0f, 10.0f);
		ImGui::BeginGroup();
		{
			const std::string name = techName(pick);
			const float big = ImGui::GetFontSize() * 1.5f;
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), big, at, T::Bone, name.c_str());
			ImGui::Dummy(ImVec2(ImGui::CalcTextSize(name.c_str()).x * 1.5f, big));
			auto chip = [&](const std::string& text)
			{
				ImGui::SameLine(0.0f, 8.0f);
				const ImVec2 p = ImGui::GetCursorScreenPos(), s = ImGui::CalcTextSize(text.c_str());
				ImGui::Dummy(ImVec2(s.x + 10.0f, s.y + 2.0f));
				ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + s.x + 10.0f, p.y + s.y + 2.0f), T::HideLine, 6.0f);
				ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 5.0f, p.y + 1.0f), T::BoneDim, text.c_str());
			};
			if (!d->Branch.empty()) chip(Lang::Cap(L.Name("branch", d->Branch, d->Branch)));
			if (d->Tier >= 0) chip(L.Text("Tier {0}", d->Tier));
			int knowers = 0;
			for (const TribeMember& m : t.Members()) if (!m.Dead && t.Knowledge().Knows(m.Name, pick)) ++knowers;
			std::string state = Lang::Cap(TechStateText(status, pick));
			if (!have) state += "  \xC2\xB7  " + (knowers > 0 ? L.Text("{0} of the clan know it", knowers) : L.Text("nobody of the clan knows it"));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW - 54.0f);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(e.State == TechState::Blocked ? T::Warn : e.State == TechState::OtherClan ? T::News : T::BoneDim), "%s", state.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::EndGroup();
		// ---- the buttons: the queue (the player's clan) or the god's vision
		if (ownQueue && !have)
		{
			const std::string name = techName(pick);
			if (e.Queue == 0)
			{
				{
					const GameTheme::PrimaryScope primary;
					if (ImGui::Button((L.Text("+ Add to the queue ({0}.)", static_cast<int>(t.Research().All().size()) + 1) + "###qadd").c_str())) run("game research add " + pick);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Add {0} to the end of the queue", name).c_str());
				ImGui::SameLine();
				if (ImGui::Button((L.Text("Study it first") + "###qtop").c_str())) run("game research top " + pick);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Study {0} before the rest", name).c_str());
			}
			else
			{
				if (e.Queue > 1)
				{
					if (ImGui::Button((L.Text("Study it first") + "###qtop").c_str())) run("game research top " + pick);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Study {0} before the rest", name).c_str());
					ImGui::SameLine();
				}
				if (ImGui::Button((L.Text("Take it out of the queue") + "###qoff").c_str())) run("game research remove " + pick);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Take {0} out of the queue", name).c_str());
			}
			if (e.Why != TechWhy::None && e.State != TechState::Studying && e.State != TechState::Studiable)
			{
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("In the queue it waits until this changes: {0}", TechWhyText(status, pick)).c_str());
				ImGui::PopTextWrapPos();
			}
		}
		// T242: for the god (the observer of the free mode): [Vision] sends a dream of it to the clan (`director vision`), with its cost, why
		// not, and the warning of the material or of the inventor the clan lacks; the vision the clan holds says who dreamt it, with [Let it go]
		else if (!playing && !have && !m_Director.Colony())
		{
			const Clan* clan = m_Clans.Find(m_ActiveClan);
			const std::string id = std::to_string(m_ActiveClan);
			const std::string clanName = clan ? L.Name("clan", clan->Name, clan->Name) : id;
			const DirectorVision* held = clan ? m_Director.VisionOf(clan->Id) : nullptr;
			ImGui::PushTextWrapPos(0.0f);
			if (held && held->Tech == pick)
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("{0} dreamt of it: the {1} seek it", held->Dreamer, clanName).c_str());
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
			ImGui::PopTextWrapPos();
		}
		ImGui::Separator();

		// ---- what it opens: tiles of two columns (a new action, a recipe, a building with its picture)
		section(L.Text("What it opens"));
		{
			struct Tile { std::string Kind, Name, Icon, Thumb, Tip; bool Verb = false; };
			std::vector<Tile> tiles;
			for (const std::string& v : d->Verbs)
			{
				const size_t colon = v.find(':');
				const std::string word = colon == std::string::npos ? v : v.substr(colon + 1);
				tiles.push_back({ L.Text("NEW ACTION"), Lang::Cap(L.Name("verb", v, word)), word, std::string(), L.Text("The clan will know how to {0}", L.Name("verb", v, word)), true });
			}
			for (const std::string& r : d->Recipes)
				if (const Recipe* rec = m_Recipes.Find(r))
				{
					const ItemDef* item = m_Items.Find(rec->Output);
					tiles.push_back({ L.Text("RECIPE"), Lang::Cap(L.Name("item", rec->Output, item && !item->Name.empty() ? item->Name : rec->Output)), rec->Output, std::string(), std::string() });
				}
			for (const std::string& bid : d->Buildings)
			{
				const auto bdef = m_Buildings.Defs.find(bid);
				std::string tip;
				if (bdef != m_Buildings.Defs.end()) for (const std::string& l : BuildingDoes(bdef->second)) tip += (tip.empty() ? "" : "\n") + l;
				tiles.push_back({ L.Text("BUILDING"), Lang::Cap(L.Name("building", bid, bdef != m_Buildings.Defs.end() ? bdef->second.Name : bid)), "build", bid, tip });
			}
			if (tiles.empty()) ImGui::TextDisabled("%s", L.Text("knowledge for the clan (rites, stories, ways of doing)").c_str());
			const float full = ImGui::GetContentRegionAvail().x;
			const float tw = (full - 6.0f) * 0.5f, th = lineH * 2.0f + 12.0f;
			ImDrawList* dl = ImGui::GetWindowDrawList();
			for (size_t i = 0; i < tiles.size(); ++i)
			{
				const Tile& tile = tiles[i];
				if (i % 2 == 1) ImGui::SameLine(0.0f, 6.0f);
				const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + tw, a.y + th);
				ImGui::PushID(static_cast<int>(i));
				ImGui::InvisibleButton("tile", ImVec2(tw, th));
				const bool hov = ImGui::IsItemHovered();
				ImGui::PopID();
				dl->AddRectFilled(a, b, T::HideRaised, 4.0f);
				dl->AddRect(a, b, tile.Verb ? T::Ochre : T::HideLine, 4.0f, 0, tile.Verb ? 1.5f : 1.0f);
				float tx = a.x + 6.0f;
				const float s = th - 10.0f;
				bool pictured = false;
				if (!tile.Thumb.empty())
					if (const BuildingThumb* bt = BuildingThumbOf(tile.Thumb); bt && bt->Colour)
					{
						dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(bt->Colour->GetID())), ImVec2(tx, a.y + 5.0f), ImVec2(tx + s, a.y + 5.0f + s));
						pictured = true;
					}
				glm::vec2 uv0(0.0f), uv1(0.0f);
				if (!pictured && IconUv(tile.Icon, uv0, uv1))
				{
					dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_IconTexture->GetID())), ImVec2(tx, a.y + 5.0f), ImVec2(tx + s, a.y + 5.0f + s), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
					pictured = true;
				}
				if (pictured) tx += s + 6.0f;
				dl->AddText(ImVec2(tx, a.y + 5.0f), tile.Verb ? T::Ochre : T::BoneDim, Fit(tile.Kind, b.x - tx - 4.0f).c_str());
				bool cut = false;
				dl->AddText(ImVec2(tx, a.y + 6.0f + lineH), T::Bone, Fit(tile.Name, b.x - tx - 4.0f, &cut).c_str());
				if (hov && (cut || !tile.Tip.empty())) ImGui::SetTooltip("%s", (tile.Name + (tile.Tip.empty() ? std::string() : "\n" + tile.Tip)).c_str());
			}
			// and after it: what it opens, named when the clan can name it, the rest counted by tier
			const auto& all = tree->Techs;
			std::map<int, int> hidden;
			std::string namedNext;
			for (const auto& [id, x] : all)
			{
				if (std::find(x.Requires.begin(), x.Requires.end(), pick) == x.Requires.end() || !status.Shown(id)) continue;
				if (named(id)) namedNext += (namedNext.empty() ? "" : ", ") + techName(id);
				else ++hidden[std::max(0, status.Of(id).Tier)];
			}
			std::string after = namedNext;
			for (const auto& [tier, n] : hidden) after += (after.empty() ? "" : "  \xC2\xB7  ") + L.Text("? {0} in tier {1}", n, tier);
			if (!after.empty())
			{
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("%s", (L.Text("And after it:") + " " + after).c_str());
				ImGui::PopTextWrapPos();
			}
		}
		ImGui::Separator();

		// ---- what it needs | how it is learnt, side by side
		const float half = (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + half);
		section(L.Text("What it needs"));
		auto mark = [&](bool ok, const std::string& text, const std::string& right = std::string())
		{
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ok ? T::Good : T::Warn), "%s", ok ? "v" : "!");
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::TextUnformatted(text.c_str());
			if (!right.empty()) { ImGui::SameLine(0.0f, 8.0f); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ok ? T::BoneDim : T::Warn), "%s", right.c_str()); }
		};
		for (const std::string& r : d->Requires)
		{
			const bool known = status.Of(r).State == TechState::Known;
			mark(known, named(r) ? techName(r) : std::string("?"), known ? L.Text("known") : L.Text("not yet"));
		}
		if (d->Tier >= 0 && !tree->Rules.Tiers.empty())
		{
			const bool tierOpen = d->Tier <= TechTiers::Open(t);
			mark(tierOpen, L.Text("Tier {0}", d->Tier), tierOpen ? L.Text("open") : L.Text("closed"));
			if (!tierOpen) ImGui::TextDisabled("%s", L.Text("It opens with {0}", TierGoalsText(t, d->Tier, true)).c_str());
		}
		for (const std::string& m : d->Physical.Materials)
		{
			const int n = t.Stored(m);
			const bool missing = std::find(e.Missing.begin(), e.Missing.end(), m) != e.Missing.end() && e.Why == TechWhy::Material;
			const ItemDef* item = m_Items.Find(m);
			const std::string name = Lang::Cap(L.Name("item", m, item && !item->Name.empty() ? item->Name : m));
			mark(!missing, L.Text("{0} at hand", name), L.Text("there are {0}", n));
			// D7: what is missing at hand, mended with a stock goal (an order that already exists)
			if (missing && ownQueue && !have)
			{
				ImGui::PushID(m.c_str());
				if (ImGui::SmallButton((L.Text("Keep {0} {1}", 5, L.Name("item", m, m)) + "###stock").c_str())) run("game stock " + m + " 5");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("A stock goal: the clan gathers or makes it until it has that many").c_str());
				ImGui::PopID();
			}
		}
		for (const std::string& s : d->Physical.Stations)
		{
			const bool missing = e.Why == TechWhy::Station;
			mark(!missing, L.Text("{0} nearby", Lang::Cap(L.Name("station", s, s))));
		}
		if (d->Requires.empty() && d->Physical.Empty() && d->Tier <= 0) ImGui::TextDisabled("%s", L.Text("nothing").c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
		ImGui::SameLine(0.0f, 12.0f);
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + half);
		section(L.Text("How it is learnt"));
		auto way = [&](const std::string& text, float v, float easy, float fair)
		{
			const std::string word = v <= 0.0f ? L.Text("no") : v >= easy ? L.Text("easy") : v >= fair ? L.Text("possible") : L.Text("hard");
			ImGui::TextUnformatted(text.c_str());
			ImGui::SameLine(half - ImGui::CalcTextSize(word.c_str()).x);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(v <= 0.0f ? T::BoneDim : v >= easy ? T::Good : v >= fair ? T::Bone : T::Warn), "%s", word.c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%.2f", static_cast<double>(v));
		};
		way(L.Text("Trying"), d->Discovery.Experiment, 0.2f, 0.1f);
		way(L.Text("Watching another clan"), d->Discovery.Observe, 0.3f, 0.2f);
		way(L.Text("Being taught"), d->Discovery.Teach, 0.3f, 0.2f);
		if (!e.From.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::News), "%s", L.Text("The {0} know it: learning it from another clan skips the tier", L.Name("clan", e.From, e.From)).c_str());
		else if (!have) ImGui::TextDisabled("%s", L.Text("Learning it from another clan skips the tier").c_str());
		section(L.Text("Who"));
		if (!e.Who.empty())
		{
			std::string who;
			for (const std::string& n : e.Who) who += (who.empty() ? "" : ", ") + n;
			ImGui::TextUnformatted(L.Text("on it: {0}", who).c_str());
		}
		else if (have) ImGui::TextDisabled("%s", L.Text("the clan knows it").c_str());
		else
		{
			std::string who;
			for (const auto& [name, n] : StudyPeople(t)) if (n > 0) who += (who.empty() ? "" : ", ") + name;
			ImGui::TextDisabled("%s", (who.empty() ? L.Text("nobody has Knowledge in Work") : L.Text("it would be studied by {0} (Knowledge in Work)", who)).c_str());
		}
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
	}
}
