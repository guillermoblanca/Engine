// T336 (L7-L9, documentation/02_tecnico/gameplay/AUDITORIA_LATERAL_2026-11-19.md §4, option A approved 2026-11-19): the colony's screen.
//   top left:  "what is urgent", small: the clan, its food, its fire and its winter (a click opens the clan's sheet).
//   right:     the sheet of what is selected, and nothing when nothing is: a person (who, what it does and why, its needs in two columns, its bag,
//              then its work, what it learns, its family, memories and genes folded by depth), a node, a building or a site (SelectionView.cpp).
//   bottom:    the bar of icons: Build, Knowledge, Work, Stock, Clan, Chronicle (ObserverView.cpp, DrawHandBar).
//   centre:    the clan's sheet, opened from the bar: a column of icon tabs (Clan, Chronicle, Board, Sites, Homes, Knowledge, Clans) and the sheet.
// The god's book (ObserverView.cpp) stays as it was for the editor's observer and the god's game. The UI decides nothing: every button queues a
// `colony ...`, `game ...`, `director ...` or `observer ...` command.
#include "EditorApp.h"
#include "GameTheme.h"
#include "SceneEditor.h"
#include "StoryKinds.h"
#include "GB/Game/StoreLayout.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Transplant.h"
#include "GB/Game/WinterOutlook.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <functional>
#include <imgui.h>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace GB::Editor
{
	namespace
	{
		namespace T = GameTheme;

		ImU32 ClanColourOf(const ClanWorld& clans, ClanId id)
		{
			const glm::vec3 c = glm::clamp(clans.ColorOf(id), glm::vec3(0.0f), glm::vec3(1.0f));
			return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f), 255);
		}

		// the sheets of the clan's big sheet: their tab's id, label and icon ("" = drawn: no icon in the atlas yet, ART-1)
		struct SheetTab { const char* Id; const char* Label; const char* Icon; };
		const SheetTab kSheets[] = { { "clan", "Clan", "" }, { "chronicle", "Chronicle", "" }, { "board", "Job board", "" }, { "sites", "Sites", "build" },
			{ "homes", "Homes", "building.shelter" }, { "research", "Knowledge", "experiment" }, { "relations", "Clans", "trade" } };

		// the marks drawn while the atlas has no icon for them (ART-1): a banner (the clan), a scroll (the chronicle), a list (the board, the work),
		// an eye (follow), a swap (change trade), a pennant (an order of yours), a warning triangle
		void DrawMark(ImDrawList* dl, const std::string& mark, ImVec2 min, float s, ImU32 c)
		{
			const float x0 = min.x, y0 = min.y, x1 = min.x + s, y1 = min.y + s;
			if (mark == "clan")
			{
				dl->AddLine(ImVec2(x0 + s * 0.25f, y0 + s * 0.1f), ImVec2(x0 + s * 0.25f, y1 - s * 0.05f), c, 2.0f);
				dl->AddRectFilled(ImVec2(x0 + s * 0.28f, y0 + s * 0.12f), ImVec2(x1 - s * 0.15f, y0 + s * 0.55f), c, 1.0f);
			}
			else if (mark == "chronicle")
			{
				dl->AddRect(ImVec2(x0 + s * 0.2f, y0 + s * 0.12f), ImVec2(x1 - s * 0.2f, y1 - s * 0.12f), c, 2.0f, 0, 1.6f);
				for (int i = 0; i < 3; ++i) dl->AddLine(ImVec2(x0 + s * 0.32f, y0 + s * (0.32f + 0.18f * static_cast<float>(i))), ImVec2(x1 - s * 0.32f, y0 + s * (0.32f + 0.18f * static_cast<float>(i))), c, 1.4f);
			}
			else if (mark == "list")
			{
				for (int i = 0; i < 3; ++i)
				{
					const float y = y0 + s * (0.25f + 0.25f * static_cast<float>(i));
					dl->AddCircleFilled(ImVec2(x0 + s * 0.2f, y), s * 0.07f, c, 6);
					dl->AddLine(ImVec2(x0 + s * 0.35f, y), ImVec2(x1 - s * 0.1f, y), c, 1.8f);
				}
			}
			else if (mark == "eye")
			{
				dl->AddEllipse(ImVec2(x0 + s * 0.5f, y0 + s * 0.5f), ImVec2(s * 0.42f, s * 0.26f), c, 0.0f, 0, 1.6f);
				dl->AddCircleFilled(ImVec2(x0 + s * 0.5f, y0 + s * 0.5f), s * 0.13f, c);
			}
			else if (mark == "swap")
			{
				dl->AddLine(ImVec2(x0 + s * 0.15f, y0 + s * 0.35f), ImVec2(x1 - s * 0.15f, y0 + s * 0.35f), c, 1.8f);
				dl->AddTriangleFilled(ImVec2(x1 - s * 0.1f, y0 + s * 0.35f), ImVec2(x1 - s * 0.3f, y0 + s * 0.2f), ImVec2(x1 - s * 0.3f, y0 + s * 0.5f), c);
				dl->AddLine(ImVec2(x0 + s * 0.15f, y0 + s * 0.68f), ImVec2(x1 - s * 0.15f, y0 + s * 0.68f), c, 1.8f);
				dl->AddTriangleFilled(ImVec2(x0 + s * 0.1f, y0 + s * 0.68f), ImVec2(x0 + s * 0.3f, y0 + s * 0.53f), ImVec2(x0 + s * 0.3f, y0 + s * 0.83f), c);
			}
			else if (mark == "order")
			{
				dl->AddLine(ImVec2(x0 + s * 0.25f, y0 + s * 0.1f), ImVec2(x0 + s * 0.25f, y1 - s * 0.05f), c, 1.6f);
				dl->AddTriangleFilled(ImVec2(x0 + s * 0.28f, y0 + s * 0.1f), ImVec2(x1 - s * 0.1f, y0 + s * 0.3f), ImVec2(x0 + s * 0.28f, y0 + s * 0.5f), c);
			}
			else if (mark == "warn")
			{
				dl->AddTriangle(ImVec2(x0 + s * 0.5f, y0 + s * 0.1f), ImVec2(x1 - s * 0.05f, y1 - s * 0.1f), ImVec2(x0 + s * 0.05f, y1 - s * 0.1f), c, 1.5f);
				dl->AddLine(ImVec2(x0 + s * 0.5f, y0 + s * 0.38f), ImVec2(x0 + s * 0.5f, y0 + s * 0.65f), c, 1.5f);
				dl->AddCircleFilled(ImVec2(x0 + s * 0.5f, y0 + s * 0.77f), 1.2f, c);
			}
			else if (mark == "flake")
			{
				const ImVec2 m(x0 + s * 0.5f, y0 + s * 0.5f);
				for (int i = 0; i < 3; ++i)
				{
					const float a = 1.5707963f + static_cast<float>(i) * 1.0471976f, r = s * 0.42f;
					dl->AddLine(ImVec2(m.x - std::cos(a) * r, m.y - std::sin(a) * r), ImVec2(m.x + std::cos(a) * r, m.y + std::sin(a) * r), c, 1.6f);
				}
			}
			else dl->AddCircle(ImVec2(x0 + s * 0.5f, y0 + s * 0.5f), s * 0.3f, c, 12, 1.5f);   // (anything else: a ring)
		}

		// a bar of `n` segments, `filled` of them lit (the levels of the clan's sheet and of the person's work)
		void Segments(ImDrawList* dl, ImVec2 min, float w, float h, int n, int filled, ImU32 on)
		{
			const float gap = 2.0f, sw = (w - static_cast<float>(n - 1) * gap) / static_cast<float>(n);
			for (int i = 0; i < n; ++i)
			{
				const ImVec2 a(min.x + static_cast<float>(i) * (sw + gap), min.y), b(a.x + sw, a.y + h);
				if (i < filled) dl->AddRectFilled(a, b, on, 1.5f);
				else dl->AddRect(a, b, T::HideLine, 1.5f);
			}
		}

		// `s` cut to `width` with ".." (whole UTF-8 characters: the font has no ellipsis)
		std::string FitText(std::string s, float width)
		{
			if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
			while (!s.empty() && ImGui::CalcTextSize((s + "..").c_str()).x > width)
			{
				s.pop_back();
				while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
				if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0xC0) s.pop_back();
			}
			while (!s.empty() && s.back() == ' ') s.pop_back();
			return s + "..";
		}

		// `s` in lines of `width`, at most `most` (the last one cut with ".." when the rest does not fit)
		std::vector<std::string> Lines(const std::string& s, float width, int most)
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
			if (static_cast<int>(lines.size()) > most)
			{
				std::string rest;
				for (size_t i = static_cast<size_t>(most - 1); i < lines.size(); ++i) rest += (rest.empty() ? "" : " ") + lines[i];
				lines.resize(static_cast<size_t>(most - 1));
				lines.push_back(rest);
			}
			for (std::string& l : lines) l = FitText(l, width);
			return lines;
		}

		// a part of the quality of life (0..1; < 0: not yet) as a word, its colour and its 5 segments: never a bare 0,46 (the number is in the tooltip).
		// The techniques are low by nature at the start: "starting", never red (audit C1)
		struct Level { const char* Word; ImU32 Colour; int Filled; };
		Level LevelOf(float v, bool techs = false)
		{
			if (v < 0.0f) return { "no data yet", T::BoneDim, 0 };
			const int filled = std::clamp(static_cast<int>(std::ceil(v * 5.0f - 0.001f)), v > 0.0f ? 1 : 0, 5);
			if (techs && v < 0.6f) return { v < 0.3f ? "starting" : "growing", T::Ochre, filled };
			if (v >= 0.6f) return { "good", T::Good, filled };
			if (v >= 0.3f) return { "fair", T::Warn, filled };
			return { "poor", T::Bad, filled };
		}
		const char* PartWord(const std::string& part)
		{
			return part == "needs" ? "Needs met" : part == "shelter" ? "Shelter" : part == "food" ? "Food" : part == "tools" ? "Tools" : part == "health" ? "Health"
				: part == "longevity" ? "Longevity" : part == "techs" ? "Techniques" : part.c_str();
		}
		const char* PartIcon(const std::string& part)
		{
			return part == "needs" ? "state.hungry" : part == "shelter" ? "building.shelter" : part == "food" ? "berry" : part == "tools" ? "stone_axe" : part == "health" ? "care"
				: part == "techs" ? "experiment" : "";
		}
	}

	bool EditorApp::IsColonySheet(const std::string& tab)
	{
		for (const SheetTab& s : kSheets) if (tab == s.Id) return true;
		return false;
	}

	// ------------------------------------------------------------------ the screen
	void EditorApp::DrawColonyScreen(float ox, float oy, float w, float h, const Clan& clan)
	{
		const Tribe& t = *clan.Folk;
		m_Observer.TabsRoom = 0.0f;   // (no book: no tabs line in `observer status`)
		// a sheet of the god's book asked for (`observer sheet|person ...`, the scripts of the audit) goes to its place in the colony
		if (!m_Observer.Ask.empty())
		{
			const std::string ask = m_Observer.Ask;
			m_Observer.Ask.clear();
			if (ask == "person")
			{
				if (const TribeMember* m = t.FindMember(m_Observer.Person); m && !m->Dead) {
					// (a Shift+click that added it to the selection keeps the others)
					m_FreeSelected = m->Name;
					if (!m->Player && std::find(m_Colony.Selection.begin(), m_Colony.Selection.end(), m->Name) == m_Colony.Selection.end()) m_Colony.Selection = { m->Name };
				}
			}
			else if (ask != "members" && ask != "log")
			{
				const std::string tab = ask == "village" ? "clan" : ask == "knowledge" ? "research" : ask;
				if (IsColonySheet(tab)) m_UiCommands.push_back("colony tab " + tab);
			}
		}
		m_Observer.Tab = m_Colony.Tab.empty() ? std::string("selection") : m_Colony.Tab;   // (`observer status`'s "sheet")
		const float top = m_Observer.BarBottom + T::Gap;
		DrawColonyUrgent(ox, top, clan);
		// right: the sheet of the selection, nothing without one
		if (!m_FreeSelected.empty())
		{
			const float pw = std::clamp(w * 0.27f, 340.0f, 430.0f), maxH = std::max(oy + h - top - T::Margin, 160.0f);
			const TribeMember* m = AnyMember(m_FreeSelected);
			// T342: a node, a building or a site has its sheet as the person's (p1): the ring on the ground round it, the sheet on the right
			if (!m && !m_Gameplay.Find(m_FreeSelected)) m_FreeSelected.clear();
			else
			{
				if (!m) { DrawArea(ox, oy, w, h, m_FreeSelected); DrawYardWorld(ox, oy, w, h, m_FreeSelected); }   // (prototype TALLERES 10: a house's slots)
				ImGui::SetNextWindowPos(ImVec2(ox + w - T::Margin, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
				ImGui::SetNextWindowSizeConstraints(ImVec2(pw, 0.0f), ImVec2(pw, maxH));
				const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
					| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
				if (ImGui::Begin("Selected##colony", nullptr, flags))
				{
					if (m) { if (const Tribe* own = ClanOfMember(m->Name)) DrawColonyPerson(*own, *m, pw - 2.0f * T::Pad); }
					else DrawColonyThing(m_FreeSelected, pw - 2.0f * T::Pad);
					m_Observer.BookBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
				}
				ImGui::End();
			}
			if (m_Observer.BookBox.X1 > m_Observer.BookBox.X0) m_Observer.BookWidth = ox + w - m_Observer.BookBox.X0;
		}
	}

	// the top left: what is urgent, small (MODO_COLONIA §4.1). The head opens the clan's sheet.
	void EditorApp::DrawColonyUrgent(float ox, float top, const Clan& clan)
	{
		const Lang& L = m_Lang;
		const Tribe& t = *clan.Folk;
		const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
		ImGui::SetNextWindowPos(ImVec2(ox + T::Margin, top));
		if (ImGui::Begin("Urgent##colony", nullptr, fixed))
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float lh = ImGui::GetTextLineHeight(), labelX = lh + 6.0f, valueX = 150.0f;
			int alive = 0;
			for (const TribeMember& m : t.Members()) alive += m.Dead ? 0 : 1;
			const std::string name = L.Name("clan", clan.Name, clan.Name);
			{
				const ImVec2 at = ImGui::GetCursorScreenPos();
				const std::string people = L.Text(alive == 1 ? "{0} person" : "{0} people", alive);
				const float wHead = lh + 6.0f + ImGui::CalcTextSize((name + "  " + people).c_str()).x;
				if (ImGui::InvisibleButton("head", ImVec2(std::max(wHead, valueX + 90.0f), lh + 2.0f))) m_UiCommands.push_back(m_Colony.Tab == "clan" ? "colony tab off" : "colony tab clan");
				const bool hovered = ImGui::IsItemHovered();
				if (hovered) ImGui::SetTooltip("%s", L.Text("Open the sheet of the {0}", name).c_str());
				dl->AddRectFilled(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + lh - 2.0f, at.y + lh), ClanColourOf(m_Clans, clan.Id), 2.0f);
				dl->AddText(ImVec2(at.x + lh + 4.0f, at.y), hovered ? T::OchreHover : T::Bone, name.c_str());
				dl->AddText(ImVec2(at.x + lh + 4.0f + ImGui::CalcTextSize((name + "  ").c_str()).x, at.y), T::BoneDim, people.c_str());
			}
			auto row = [&](const char* icon, const char* mark, const std::string& label, const std::string& value, ImU32 colour, const std::string& tip, bool warn)
			{
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(valueX + (warn ? lh + 2.0f : 0.0f) + std::max(ImGui::CalcTextSize(value.c_str()).x, 90.0f), lh + 2.0f));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
				if (!IconAt(dl, icon, at.x, at.y, lh)) DrawMark(dl, mark, at, lh, T::BoneDim);
				dl->AddText(ImVec2(at.x + labelX, at.y), T::Bone, label.c_str());
				float x = at.x + valueX;
				if (warn) { DrawMark(dl, "warn", ImVec2(x, at.y), lh, T::Warn); x += lh + 2.0f; }
				dl->AddText(ImVec2(x, at.y), colour, value.c_str());
			};
			const float food = 1.0f - std::clamp(t.FoodShortage(), 0.0f, 1.0f);
			row("smoked_meat", "", L.Text("Food"), L.Text("for {0} days", L.Num(t.FoodDays(), 0)), food < 0.25f ? T::Warn : T::Bone,
				L.Text("Days of food the clan has in hand (store and bags); the bar is how far it is from the days it wants"), food < 0.25f);
			const bool lit = t.HasFire() && t.FireBurning();
			row("campfire", "", L.Text("Fire"), !t.HasFire() ? L.Text("no campfire yet") : lit ? L.Text("lit") : L.Text("out (no wood)"), lit ? T::Bone : T::Warn,
				L.Text("The campfire warms the clan and cooks its food"), !lit);
			const WinterOutlook wo = ForecastWinter(t);
			const bool shortNear = !wo.Covered() && (wo.In || wo.ToWinter <= wo.SeasonDays);
			row("", "flake", wo.In ? L.Text("Winter") : L.Text("Winter {0} d", L.Num(wo.ToWinter, 0)), wo.Covered() ? L.Text("covered") : L.Text("lacks food"), wo.Covered() ? T::Good : T::Bone,
				(wo.In ? L.Text("The winter is here: {0} days until the spring", L.Num(wo.UntilSpring, 0)) : L.Text("The winter comes in {0} days and lasts {1}", L.Num(wo.ToWinter, 0), L.Num(wo.SeasonDays, 0)))
				+ "\n" + (wo.Covered() ? L.Text("Food stored for {0} days: enough to reach the spring", L.Num(t.FoodDays(), 1))
					: L.Text("Food stored for {0} days; it lacks {1} days to reach the spring", L.Num(t.FoodDays(), 1), L.Num(wo.Short, 0))), !wo.Covered() && shortNear);
			m_Observer.CardBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
			m_Observer.ClansBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, m_Observer.CardBottom };
		}
		ImGui::End();
	}

	// ------------------------------------------------------------------ the person (L7, propuesta p1)
	void EditorApp::DrawColonyPerson(const Tribe& t, const TribeMember& m, float width)
	{
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float lh = ImGui::GetTextLineHeight();
		const Person* p = t.People().Find(m.Name);
		const bool own = ColonyTribe() == &t && !m.Player;
		// ---- who: what it is doing as a big icon in a ring, the name, the age and the trade; follow, go, change trade, close
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float ring = 44.0f;
			ImGui::Dummy(ImVec2(ring, ring));
			dl->AddCircleFilled(ImVec2(at.x + ring * 0.5f, at.y + ring * 0.5f), ring * 0.5f, T::HideRaised, 24);
			dl->AddCircle(ImVec2(at.x + ring * 0.5f, at.y + ring * 0.5f), ring * 0.5f, m.Dead ? T::HideLine : T::Ochre, 24, 2.0f);
			if (!IconAt(dl, MemberTaskIcon(m), at.x + 8.0f, at.y + 8.0f, ring - 16.0f)) DrawMark(dl, "", ImVec2(at.x + 8.0f, at.y + 8.0f), ring - 16.0f, T::BoneDim);
			ImGui::SameLine(0.0f, 10.0f);
			ImGui::BeginGroup();
			ImGui::SetWindowFontScale(1.3f);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(m.Dead ? T::BoneDim : T::Bone), "%s", m.Name.c_str());
			ImGui::SetWindowFontScale(1.0f);
			std::string who = p ? (std::lround(p->Age) == 1 ? L.Text("1 year") : L.Text("{0} years", L.Num(p->Age, 0))) + " \xC2\xB7 " : std::string();
			who += RoleName(t, m);
			if (m_Director.TradeFixed(m.Name)) who += " (" + L.Text("fixed by you") + ")";
			ImGui::TextDisabled("%s", who.c_str());
			ImGui::EndGroup();
			// the buttons on the right of the head: icons with their words in the tooltip
			const float b = ImGui::GetFrameHeight() + 4.0f;
			const int buttons = m.Dead ? 1 : own ? 4 : 3;
			ImGui::SetCursorScreenPos(ImVec2(at.x + width - static_cast<float>(buttons) * (b + style.ItemSpacing.x) + style.ItemSpacing.x, at.y + 2.0f));
			auto iconButton = [&](const char* id, const char* icon, const char* mark, bool lit, const std::string& tip) -> bool
			{
				std::optional<T::PrimaryScope> on;
				if (lit) on.emplace();
				const ImVec2 bt = ImGui::GetCursorScreenPos();
				const bool pressed = ImGui::Button(id, ImVec2(b, b));
				if (!IconAt(dl, icon, bt.x + 4.0f, bt.y + 4.0f, b - 8.0f)) DrawMark(dl, mark, ImVec2(bt.x + 4.0f, bt.y + 4.0f), b - 8.0f, lit ? T::Ink : T::Bone);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
				ImGui::SameLine();
				return pressed;
			};
			if (!m.Dead)
			{
				const bool following = m_GameView.Following() == m.Name;
				if (iconButton("###follow", "", "eye", following, L.Text(following ? "The camera follows {0}: click to let go" : "The camera follows {0} wherever it goes", m.Name)))
					run(following ? std::string("observer follow off") : "observer follow " + m.Name);
				if (iconButton("###goto", "goto", "", false, L.Text("The camera to where {0} is now", m.Name)))
				{
					glm::vec3 where(0.0f);
					if (MemberWhere(m.Name, where))
					{
						char x[32], z[32];
						std::snprintf(x, sizeof x, "%.2f", static_cast<double>(where.x));
						std::snprintf(z, sizeof z, "%.2f", static_cast<double>(where.z));
						run(std::string("camera game overview ") + x + " " + z + " 30");
					}
				}
				if (own && iconButton("###trade", "", "swap", m_Hand.Calling == m.Name, L.Text("Pick the trade of {0}: it takes it up now and keeps it until you set it free", m.Name)))
					run(m_Hand.Calling == m.Name ? "director hand calling off" : "director hand calling " + m.Name);
			}
			if (ImGui::Button("\xC3\x97###close", ImVec2(b, b))) { run("colony select clear"); run("colony inspect off"); }
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Close").c_str());
			ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + ring + style.ItemSpacing.y));
		}
		// the trades to pick (open by [change trade])
		if (own && m_Hand.Calling == m.Name)
		{
			for (const auto& [role, def] : t.Config.Roles)
			{
				if (def.Fixed || def.Skill.empty()) continue;
				std::optional<T::PrimaryScope> lit;
				if (role == m.Role) lit.emplace();
				if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(L.Name("role", role).c_str()).x + 16.0f) ImGui::NewLine();
				if (ImGui::SmallButton((Lang::Cap(L.Name("role", role)) + "###trade" + role).c_str())) { run("director role " + m.Name + " " + role); run("director hand calling off"); }
				ImGui::SameLine();
			}
			ImGui::NewLine();
			if (m_Director.TradeFixed(m.Name) && ImGui::SmallButton((L.Text("Let the clan choose") + "###tradeauto").c_str())) { run("director role " + m.Name + " auto"); run("director hand calling off"); }
		}
		// several people selected: how many and who (a right click gives them all the order)
		if (m_Colony.Selection.size() > 1 && std::find(m_Colony.Selection.begin(), m_Colony.Selection.end(), m.Name) != m_Colony.Selection.end())
		{
			std::string names;
			for (const std::string& n : m_Colony.Selection) names += (names.empty() ? "" : ", ") + n;
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(T::Ochre));
			ImGui::TextWrapped("%s", L.Text("{0} selected: {1}", m_Colony.Selection.size(), names).c_str());
			ImGui::PopStyleColor();
			ImGui::TextDisabled("%s", L.Text("Right click on something: the order goes to all of them").c_str());
		}
		if (m.Dead)
		{
			ImGui::TextUnformatted((m.Cause.empty() ? L.Text("dead") : L.Text("dead of {0}", L.Name("need", m.Cause))).c_str());
			return;
		}
		// ---- what it does and why: a card with an ochre edge (an order of yours goes first, with its pennant)
		{
			const ColonyOrders::Mandate* order = t.Colony().Of(m.Name);
			const std::string doing = Lang::Cap(L.Doing(m.Job, m.Detail));
			std::string why = PersonWhy(t, m);
			const ImVec2 at = ImGui::GetCursorScreenPos();
			dl->ChannelsSplit(2);
			dl->ChannelsSetCurrent(1);
			ImGui::Indent(10.0f);
			ImGui::Dummy(ImVec2(1.0f, 2.0f));
			{
				const ImVec2 i0 = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(lh + 2.0f, lh + 2.0f));
				if (!IconAt(dl, MemberTaskIcon(m), i0.x, i0.y, lh + 2.0f)) DrawMark(dl, "", i0, lh + 2.0f, T::BoneDim);
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::PushTextWrapPos(at.x + width - 8.0f - ImGui::GetWindowPos().x);
				ImGui::TextUnformatted(doing.c_str());
				ImGui::PopTextWrapPos();
			}
			if (order)
			{
				const ImVec2 i0 = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(lh, lh));
				DrawMark(dl, "order", i0, lh, T::Ochre);
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::PushTextWrapPos(at.x + width - 8.0f - ImGui::GetWindowPos().x);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", why.c_str());
				ImGui::PopTextWrapPos();
			}
			else if (!why.empty())
			{
				if (!why.empty() && static_cast<unsigned char>(why[0]) < 128) why[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(why[0])));
				ImGui::PushTextWrapPos(at.x + width - 8.0f - ImGui::GetWindowPos().x);
				ImGui::TextDisabled("%s", L.Text("Because {0}", why).c_str());
				ImGui::PopTextWrapPos();
			}
			ImGui::Dummy(ImVec2(1.0f, 2.0f));
			ImGui::Unindent(10.0f);
			const ImVec2 end(at.x + width, ImGui::GetCursorScreenPos().y);
			dl->ChannelsSetCurrent(0);
			dl->AddRectFilled(at, end, T::HideRaised, T::Radius);
			dl->AddRectFilled(at, ImVec2(at.x + 3.0f, end.y), order ? T::Ochre : T::Faded(T::Ochre, 0.7f), T::Radius, ImDrawFlags_RoundCornersLeft);
			dl->ChannelsMerge();
		}
		ImGui::Separator();
		// ---- the needs, two columns: icon, word, bar, number (a glance: no scroll)
		{
			const std::map<std::string, float> levels = t.NeedLevels(m);
			static const std::pair<const char*, const char*> kNeedIcons[] = { { "Hunger", "state.hungry" }, { "Thirst", "state.thirsty" }, { "Rest", "state.tired" },
				{ "Warmth", "state.cold" }, { "Health", "state.wounded" } };
			struct Need { std::string Label; const char* Icon; float V; ImU32 Colour; std::string Tip; };
			std::vector<Need> needs;
			for (const std::string& n : m_Controls.Hud.Needs)
			{
				const auto it = levels.find(n);
				if (it == levels.end()) continue;
				const char* icon = "";
				for (const auto& [need, id] : kNeedIcons) if (n == need) icon = id;
				const float v01 = std::clamp(it->second / 100.0f, 0.0f, 1.0f);
				needs.push_back({ Lang::Cap(L.Name("need", n, n)), icon, it->second, T::Level(v01), v01 <= 0.25f ? L.Text("In danger: under 25") : L.Text("Below the mark (25) it is a danger.") });
			}
			const float mood = t.Mood(m.Name), low = t.Config.Social.Mood.Low;
			needs.push_back({ L.Text("Mood"), "state.happy", mood * 100.0f, mood < low ? T::Bad : mood < 0.5f ? T::Warn : T::Good,
				L.Text("Mood, 0 to 100; under {0} it is low, and low spirits make it drop an order", L.Num(low * 100.0f, 0)) });
			const float colW = (width - style.ItemSpacing.x) * 0.5f, labelW = 74.0f, numW = 26.0f, barW = std::max(colW - lh - 4.0f - labelW - numW - 8.0f, 30.0f);
			for (size_t i = 0; i < needs.size(); ++i)
			{
				const Need& n = needs[i];
				if (i % 2 == 1) ImGui::SameLine(0.0f, style.ItemSpacing.x);
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(colW, lh + 4.0f));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", n.Tip.c_str());
				const float y = at.y + 2.0f;
				IconAt(dl, n.Icon, at.x, y, lh);
				dl->AddText(ImVec2(at.x + lh + 4.0f, y), T::Bone, n.Label.c_str());
				const float bx = at.x + lh + 4.0f + labelW, v01 = std::clamp(n.V / 100.0f, 0.0f, 1.0f);
				dl->AddRectFilled(ImVec2(bx, y + 3.0f), ImVec2(bx + barW, y + lh - 3.0f), T::HideRaised, 2.0f);
				if (v01 > 0.0f) dl->AddRectFilled(ImVec2(bx, y + 3.0f), ImVec2(bx + barW * v01, y + lh - 3.0f), n.Colour, 2.0f);
				dl->AddRect(ImVec2(bx, y + 3.0f), ImVec2(bx + barW, y + lh - 3.0f), T::HideLine, 2.0f);
				dl->AddLine(ImVec2(bx + barW * 0.25f, y + 1.0f), ImVec2(bx + barW * 0.25f, y + lh - 1.0f), T::Outline, 2.0f);   // (the danger mark)
				const std::string num = L.Num(n.V, 0);
				const ImVec2 ns = ImGui::CalcTextSize(num.c_str());
				dl->AddText(ImVec2(at.x + colW - ns.x, y), T::Bone, num.c_str());
			}
		}
		// ---- the bag: a slot each, what it carries with its number
		if (const Actor* actor = m_Actors.Find(m.Name))
		{
			ImGui::Separator();
			const Inventory& bag = actor->Bag();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", L.Text("Bag").c_str());
			ImGui::SameLine(60.0f);
			const float slot = 30.0f;
			const int slots = std::max(bag.Slots(), static_cast<int>(bag.Stacks().size()));
			const int shown = std::min(slots, std::max(1, static_cast<int>((width - 60.0f - 50.0f) / (slot + 4.0f))));
			for (int i = 0; i < shown; ++i)
			{
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(slot, slot));
				dl->AddRectFilled(at, ImVec2(at.x + slot, at.y + slot), T::HideRaised, 3.0f);
				dl->AddRect(at, ImVec2(at.x + slot, at.y + slot), T::HideLine, 3.0f);
				if (static_cast<size_t>(i) < bag.Stacks().size())
				{
					const ItemSlot& s = bag.Stacks()[static_cast<size_t>(i)];
					const ItemDef* d = m_Items.Find(s.Id);
					if (!IconAt(dl, s.Id, at.x + 3.0f, at.y + 3.0f, slot - 6.0f)) DrawMark(dl, "", ImVec2(at.x + 3.0f, at.y + 3.0f), slot - 6.0f, T::BoneDim);
					const std::string count = std::to_string(s.Count);
					const ImVec2 cs = ImGui::CalcTextSize(count.c_str());
					dl->AddText(ImVec2(at.x + slot - cs.x - 2.0f, at.y + slot - cs.y), T::Bone, count.c_str());
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s x%d", Lang::Cap(L.Name("item", s.Id, d && !d->Name.empty() ? d->Name : std::string())).c_str(), s.Count);
				}
				ImGui::SameLine(0.0f, 4.0f);
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", L.Text("{0} of {1}", static_cast<int>(bag.Stacks().size()), bag.Slots()).c_str());
		}
		ImGui::Separator();
		// ---- on request: its work (a strip of the categories; a click opens the Work table) and what it learns
		if (own && !t.Config.Work.Categories.Empty())
		{
			const WorkPriorities::Table* mine = t.WorkTable().Of(m.Name);
			const bool open = ImGui::TreeNodeEx((L.Text("Work") + "###work").c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
			{
				const std::string state = mine ? L.Text("your table") : L.Text("follows the AI");
				const std::string adjust = L.Text("Adjust");
				const float aw = ImGui::CalcTextSize(adjust.c_str()).x + style.FramePadding.x * 2.0f;
				const float sw = ImGui::CalcTextSize(state.c_str()).x;
				ImGui::SameLine(width - aw - sw - 18.0f);
				const ImVec2 d = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(10.0f, lh));
				if (mine) dl->AddCircleFilled(ImVec2(d.x + 5.0f, d.y + lh * 0.5f), 4.5f, T::Ochre, 12);
				else dl->AddCircle(ImVec2(d.x + 5.0f, d.y + lh * 0.5f), 4.5f, T::BoneDim, 12, 1.5f);
				ImGui::SameLine(0.0f, 4.0f);
				ImGui::TextDisabled("%s", state.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", mine ? L.Text("{0} takes the work of its lowest number first", m.Name).c_str()
						: L.Text("{0} decides as the AI. The numbers are what its trade ({1}) suggests: the first one you change starts its table from them", m.Name, RoleName(t, m)).c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton((adjust + "###worktab").c_str())) run("colony tab work");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Open the Work table").c_str());
			}
			if (open)
			{
				const WorkPriorities::Table tab = mine ? *mine : WorkPriorities::Suggested(t, m);
				const auto& list = t.Config.Work.Categories.List;
				const float cw = std::min(54.0f, (width - static_cast<float>(list.size() - 1) * 4.0f) / static_cast<float>(list.size())), ch = 52.0f;
				for (size_t i = 0; i < list.size(); ++i)
				{
					const WorkCategoryDef& c = list[i];
					const auto it = tab.Categories.find(c.Id);
					const int n = WorkLevel(it != tab.Categories.end() ? it->second : c.Default);
					if (i) ImGui::SameLine(0.0f, 4.0f);
					const ImVec2 at = ImGui::GetCursorScreenPos();
					ImGui::PushID(c.Id.c_str());
					if (ImGui::InvisibleButton("cat", ImVec2(cw, ch))) run("colony tab work");
					const bool hovered = ImGui::IsItemHovered();
					if (hovered) ImGui::SetTooltip("%s", (WorkLabel(c.Id) + " " + WorkPriorities::Text(n) + "\n" + L.Text("Open the Work table")).c_str());
					ImGui::PopID();
					dl->AddRectFilled(at, ImVec2(at.x + cw, at.y + ch), hovered ? T::HideHover : T::HideRaised, 3.0f);
					dl->AddRect(at, ImVec2(at.x + cw, at.y + ch), n == 1 ? T::Ochre : T::HideLine, 3.0f);
					IconAt(dl, WorkIconOf(c.Id), at.x + (cw - 16.0f) * 0.5f, at.y + 3.0f, 16.0f);
					const std::string num = WorkPriorities::Text(n);
					const ImVec2 ns = ImGui::CalcTextSize(num.c_str());
					dl->AddText(ImVec2(at.x + (cw - ns.x) * 0.5f, at.y + 19.0f), !mine ? T::BoneDim : n == 1 ? T::Ochre : T::Bone, num.c_str());
					if (n != WorkPriorities::kOff) Segments(dl, ImVec2(at.x + 5.0f, at.y + 33.0f), cw - 10.0f, 4.0f, 4, 5 - n, mine ? T::Ochre : T::Faded(T::Ochre, 0.55f));
					const std::string label = WorkShort(c.Id);
					const ImVec2 ls = ImGui::CalcTextSize(label.c_str());
					dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.9f, ImVec2(at.x + (cw - ls.x * 0.9f) * 0.5f, at.y + ch - lh), T::BoneDim, label.c_str());
				}
			}
		}
		{
			const AgentKnowledge* k = t.Knowledge().Of(m.Name);
			std::vector<std::pair<float, std::string>> learning;
			if (k) for (const auto& [tech, v] : k->Progress) learning.push_back({ v, tech });
			std::sort(learning.rbegin(), learning.rend());
			if (ImGui::TreeNodeEx((L.Text("Learning") + "###learn").c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
			{
				if (learning.empty()) ImGui::TextDisabled("%s", L.Text("nothing now").c_str());
				const float colW = width;   // (one a line: the names of the techniques are long)
				for (size_t i = 0; i < learning.size(); ++i)
				{
					const auto& [v, tech] = learning[i];
					const ImVec2 at = ImGui::GetCursorScreenPos();
					ImGui::Dummy(ImVec2(colW, lh + 2.0f));
					const std::string name = Lang::Cap(L.Name("tech", tech, t.Knowledge().Name(tech)));
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("{0}: {1} % worked out", name, L.Num(v * 100.0f, 0)).c_str());
					const float s = lh;
					const float tx = IconAt(dl, "tech." + tech, at.x, at.y, s) ? at.x + s + 4.0f : at.x;
					const std::string pct = L.Num(v * 100.0f, 0) + " %";
					const float pw = ImGui::CalcTextSize("100 %").x;
					const float nameW = std::min(ImGui::CalcTextSize(name.c_str()).x, colW * 0.5f);
					dl->PushClipRect(ImVec2(tx, at.y), ImVec2(tx + nameW, at.y + lh + 2.0f), true);
					dl->AddText(ImVec2(tx, at.y), T::Bone, name.c_str());
					dl->PopClipRect();
					const float bx = tx + nameW + 6.0f, bw = std::max(at.x + colW - pw - 6.0f - bx, 20.0f);
					dl->AddRectFilled(ImVec2(bx, at.y + 3.0f), ImVec2(bx + bw, at.y + lh - 3.0f), T::HideRaised, 2.0f);
					dl->AddRectFilled(ImVec2(bx, at.y + 3.0f), ImVec2(bx + bw * std::clamp(v, 0.0f, 1.0f), at.y + lh - 3.0f), T::Ochre, 2.0f);
					dl->AddRect(ImVec2(bx, at.y + 3.0f), ImVec2(bx + bw, at.y + lh - 3.0f), T::HideLine, 2.0f);
					dl->AddText(ImVec2(at.x + colW - ImGui::CalcTextSize(pct.c_str()).x, at.y), T::Bone, pct.c_str());
				}
				const int known = k ? static_cast<int>(k->Known.size()) : 0;
				if (ImGui::SmallButton((L.Text("knows {0} techniques", known) + " >###knows").c_str())) run("colony tab research");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("What the clan knows and studies").c_str());
			}
		}
		// ---- in depth, folded: the family, the memories, the genes (each with its summary on the right)
		auto folded = [&](const char* id, const std::string& label, const std::string& summary) -> bool
		{
			const bool open = ImGui::TreeNodeEx((label + "###" + id).c_str(), ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
			const float sw = ImGui::CalcTextSize(summary.c_str()).x;
			ImGui::SameLine(std::max(width - sw, ImGui::GetCursorPosX() + 10.0f));
			ImGui::TextDisabled("%s", summary.c_str());
			return open;
		};
		{
			auto kin = [&](const std::string& name) { return name.empty() ? L.Text("unknown") : name; };
			const bool parents = p && (!p->Mother.empty() || !p->Father.empty());
			const std::string summary = !p ? std::string() : !parents ? L.Text("parents unknown") : kin(p->Mother) + ", " + kin(p->Father);
			if (folded("family", L.Text("Family"), summary) && p)
			{
				ImGui::Indent(14.0f);
				ImGui::TextDisabled("%s", L.Text("Mother").c_str()); ImGui::SameLine(90.0f); ImGui::TextUnformatted(kin(p->Mother).c_str());
				ImGui::TextDisabled("%s", L.Text("Father").c_str()); ImGui::SameLine(90.0f); ImGui::TextUnformatted(kin(p->Father).c_str());
				if (!p->Partner.empty()) { ImGui::TextDisabled("%s", L.Text("Partner").c_str()); ImGui::SameLine(90.0f); ImGui::TextUnformatted(p->Partner.c_str()); }
				ImGui::Unindent(14.0f);
			}
		}
		{
			const std::vector<const HistoryEvent*> memories = m_Clans.Past().Recall(m.Name, 3);
			if (folded("memories", L.Text("Memories"), std::to_string(memories.size())))
			{
				ImGui::Indent(14.0f);
				if (memories.empty()) ImGui::TextDisabled("%s", L.Text("nothing yet").c_str());
				for (const HistoryEvent* e : memories)
				{
					ImGui::TextDisabled("%s", L.Text("Day {0}", EventDay(e->At)).c_str());
					ImGui::SameLine();
					ImGui::TextWrapped("%s", Lang::Cap(L.Line(e->Text)).c_str());
				}
				ImGui::Unindent(14.0f);
			}
		}
		if (p)
		{
			// the two genes furthest above the usual, as words ("bold, curious"); the bars and their numbers inside
			std::vector<std::pair<float, std::string>> top;
			for (const auto& [g, v] : p->Genes) top.push_back({ v, g });
			std::sort(top.rbegin(), top.rend());
			std::string summary;
			for (size_t i = 0; i < top.size() && i < 2; ++i) if (top[i].first > 0.55f) summary += (summary.empty() ? "" : ", ") + L.Name("genetrait", top[i].second, L.Name("gene", top[i].second));
			if (folded("genes", L.Text("Genes"), summary))
			{
				ImGui::Indent(14.0f);
				for (const auto& [g, v] : p->Genes)
				{
					ImGui::TextDisabled("%s", Lang::Cap(L.Name("gene", g, g)).c_str());
					ImGui::SameLine(110.0f);
					const ImVec2 at = ImGui::GetCursorScreenPos();
					ImGui::Dummy(ImVec2(80.0f, lh));
					Segments(dl, ImVec2(at.x, at.y + lh * 0.5f - 3.0f), 80.0f, 6.0f, 5, std::clamp(static_cast<int>(std::lround(v * 5.0f)), 0, 5), T::Ochre);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("{0} (a half is the usual)", L.Num(v, 2)).c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("%s", L.Text(v >= 0.6f ? "high" : v <= 0.4f ? "low" : "usual").c_str());
				}
				ImGui::Unindent(14.0f);
			}
		}
	}

	// ------------------------------------------------------------------ the clan's big sheet (L8 + L9, propuesta p4)
	void EditorApp::DrawColonySheet(const Tribe& t, float width, float height)
	{
		const Lang& L = m_Lang;
		const Clan* clan = nullptr;
		for (const auto& c : m_Clans.All()) if (c->Folk == &t) clan = c.get();
		if (!clan) return;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		// the column of tabs: an icon over its word, the open one lit with an ochre edge
		const float tabW = 72.0f, tabH = 58.0f;
		ImGui::BeginChild("tabs", ImVec2(tabW, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
		{
			ImDrawList* tl = ImGui::GetWindowDrawList();
			for (const SheetTab& s : kSheets)
			{
				const bool on = m_Colony.Tab == s.Id;
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::PushID(s.Id);
				if (ImGui::InvisibleButton("tab", ImVec2(tabW - 4.0f, tabH))) m_UiCommands.push_back(std::string("colony tab ") + s.Id);
				const bool hovered = ImGui::IsItemHovered();
				ImGui::PopID();
				if (on || hovered) tl->AddRectFilled(at, ImVec2(at.x + tabW - 4.0f, at.y + tabH), on ? T::HideActive : T::HideHover, 4.0f);
				if (on) tl->AddRectFilled(at, ImVec2(at.x + 3.0f, at.y + tabH), T::Ochre, 2.0f);
				const float s0 = 26.0f;
				const ImVec2 ip(at.x + (tabW - 4.0f - s0) * 0.5f, at.y + 6.0f);
				const std::string mark = std::string(s.Id) == "board" ? "list" : s.Id;
				if (!IconAt(tl, s.Icon, ip.x, ip.y, s0)) DrawMark(tl, mark, ip, s0, on ? T::Ochre : T::Bone);
				const std::string label = L.Text(s.Label);
				const ImVec2 ls = ImGui::CalcTextSize(label.c_str());
				tl->AddText(ImVec2(at.x + (tabW - 4.0f - ls.x) * 0.5f, at.y + tabH - ls.y - 4.0f), on ? T::Ochre : T::BoneDim, label.c_str());
			}
			// the close, at the foot of the column (Esc closes it too)
			const float box = ImGui::GetFrameHeight() + 6.0f;
			ImGui::SetCursorPos(ImVec2((tabW - 4.0f - box) * 0.5f, std::max(ImGui::GetCursorPosY(), height - box - 4.0f)));
			if (ImGui::Button("\xC3\x97###sheetclose", ImVec2(box, box))) m_UiCommands.push_back("colony tab off");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Close the panel (Esc)").c_str());
		}
		ImGui::EndChild();
		ImGui::SameLine(0.0f, 0.0f);
		{
			const ImVec2 a = ImGui::GetCursorScreenPos();
			dl->AddLine(ImVec2(a.x, a.y), ImVec2(a.x, a.y + height), T::HideLine, 1.0f);
		}
		ImGui::SameLine(0.0f, 8.0f);
		ImGui::BeginChild("sheet", ImVec2(width - tabW - 8.0f, height), ImGuiChildFlags_None);
		{
			const std::string& tab = m_Colony.Tab;
			const float room = ImGui::GetContentRegionAvail().x;
			if (tab == "clan") DrawColonyClanTab(*clan, ImGui::GetContentRegionAvail().x);
			else if (tab == "chronicle") DrawColonyChronicle(*clan, room);   // T342 (L10): the colony's own (the god's book keeps DrawChronicleSheet)
			else if (tab == "board") { ImGui::PushItemWidth(room); DrawBoardSheet(t, false); ImGui::PopItemWidth(); }
			else if (tab == "sites") DrawSitesSheet(t);
			else if (tab == "homes") DrawColonyHomes(t, room);
			else if (tab == "relations") DrawColonyClans(t, room);
			else if (tab == "research")
			{
				m_Colony.BareTitle = true;   // (the sheet has its close: the queue's title without its own)
				DrawResearchTab(t, room, height);
				m_Colony.BareTitle = false;
				// (T340, a3: "being discovered now" and the tree's button are in the sheet: what they learn on their own, and its head)
			}
		}
		ImGui::EndChild();
	}

	// the clan: a single sheet of its state (Clan and Village merged): how it lives as a word with its trend, what is urgent in six cards, the
	// knowledge, the faith and the people folded; on the right, how it lives in detail (its 7 parts as words, the number in the tooltip)
	void EditorApp::DrawColonyClanTab(const Clan& clan, float width)
	{
		const Lang& L = m_Lang;
		const Tribe& t = *clan.Folk;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float lh = ImGui::GetTextLineHeight();
		const SocietyMeasure* measure = MeasureOf(clan.Id);
		int alive = 0;
		for (const TribeMember& p : t.Members()) alive += p.Dead ? 0 : 1;
		const bool two = width >= 700.0f;
		const float leftW = two ? std::clamp(width - 360.0f, 440.0f, 540.0f) : width - 30.0f;
		ImGui::BeginGroup();
		// the head: the clan, its people and the way to its camp
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(lh * 1.3f, lh * 1.3f));
			dl->AddRectFilled(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + lh * 1.2f, at.y + lh * 1.3f), ClanColourOf(m_Clans, clan.Id), 3.0f);
			ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetWindowFontScale(1.3f);
			ImGui::TextUnformatted(L.Name("clan", clan.Name, clan.Name).c_str());
			ImGui::SetWindowFontScale(1.0f);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", L.Text(alive == 1 ? "{0} person" : "{0} people", alive).c_str());
			const std::string camp = L.Text("Go to the camp");
			const float cw = ImGui::CalcTextSize(camp.c_str()).x + style.FramePadding.x * 2.0f;
			ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, at.x - ImGui::GetWindowPos().x + leftW - cw));
			const glm::vec2 c = t.Config.Economy.Camp;
			if (ImGui::Button((camp + "###camp").c_str())) run("camera 0 45 45 " + std::to_string(c.x) + " ground " + std::to_string(c.y));
		}
		if (!measure || alive == 0) { ImGui::TextDisabled("%s", L.Text("extinct").c_str()); ImGui::EndGroup(); return; }
		// how it lives: a word, 5 segments and its trend
		{
			const Level lv = LevelOf(measure->Qol);
			const QolTrend trend = TrendOf(clan.Id, measure->Qol);
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float hgt = lh * 2.0f + 14.0f;
			ImGui::Dummy(ImVec2(leftW, hgt));
			if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) { DrawQolTip(*measure); ImGui::EndTooltip(); }
			dl->AddRectFilled(at, ImVec2(at.x + leftW, at.y + hgt), T::HideRaised, T::Radius);
			IconAt(dl, "state.happy", at.x + 8.0f, at.y + (hgt - 24.0f) * 0.5f, 24.0f);
			dl->AddText(ImVec2(at.x + 40.0f, at.y + 5.0f), T::BoneDim, L.Text("How it lives").c_str());
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.3f, ImVec2(at.x + 40.0f, at.y + 7.0f + lh), lv.Colour == T::Bad ? T::Bone : lv.Colour, Lang::Cap(L.Text(lv.Word)).c_str());   // (red only on the segments: contrast)
			Segments(dl, ImVec2(at.x + 150.0f, at.y + hgt * 0.5f - 5.0f), 110.0f, 10.0f, 5, lv.Filled, lv.Colour);
			if (trend.Known)
			{
				const float tx = at.x + leftW - 90.0f, ty = at.y + hgt * 0.5f;
				const bool up = trend.Delta >= 0.005f, down = trend.Delta <= -0.005f;
				if (up) dl->AddTriangleFilled(ImVec2(tx + 5.0f, ty - 5.0f), ImVec2(tx + 10.0f, ty + 4.0f), ImVec2(tx, ty + 4.0f), T::Good);
				else if (down) dl->AddTriangleFilled(ImVec2(tx, ty - 4.0f), ImVec2(tx + 10.0f, ty - 4.0f), ImVec2(tx + 5.0f, ty + 5.0f), T::Bad);
				else dl->AddRectFilled(ImVec2(tx, ty - 1.0f), ImVec2(tx + 10.0f, ty + 1.0f), T::BoneDim);
				dl->AddText(ImVec2(tx + 14.0f, ty - lh * 0.5f), T::Bone, L.Text(up ? "improving" : down ? "worsening" : "steady").c_str());
			}
		}
		// what is urgent: six cards
		ImGui::Spacing();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("What is urgent").c_str());
		{
			struct Card { const char* Icon; const char* Mark; std::string Title, Value, Sub; ImU32 Edge; bool Warn; float Bar; std::string Tip; };
			std::vector<Card> cards;
			const float food = 1.0f - std::clamp(t.FoodShortage(), 0.0f, 1.0f);
			cards.push_back({ "smoked_meat", "", L.Text("Food"), L.Text("for {0} days", L.Num(t.FoodDays(), 0)), std::string(), 0, food < 0.25f, food,
				L.Text("Days of food the clan has in hand (store and bags); the bar is how far it is from the days it wants") });
			const WinterOutlook wo = ForecastWinter(t);
			const bool shortNear = !wo.Covered() && (wo.In || wo.ToWinter <= wo.SeasonDays);
			cards.push_back({ "", "flake", L.Text("Winter"), wo.In ? L.Text("it is here") : L.Text("in {0} days", L.Num(wo.ToWinter, 0)),
				wo.Covered() ? L.Text("food enough") : L.Text("lacks {0} days of food", L.Num(wo.Short, 0)), wo.Covered() ? 0u : shortNear ? T::Warn : T::Ochre, !wo.Covered(), -1.0f,
				wo.In ? L.Text("The winter is here: {0} days until the spring", L.Num(wo.UntilSpring, 0)) : L.Text("The winter comes in {0} days and lasts {1}", L.Num(wo.ToWinter, 0), L.Num(wo.SeasonDays, 0)) });
			const bool lit = t.HasFire() && t.FireBurning();
			cards.push_back({ "campfire", "", L.Text("Fire"), !t.HasFire() ? L.Text("no campfire yet") : lit ? L.Text("lit") : L.Text("out (no wood)"),
				t.HasFire() ? L.Text("fuel: {0}", L.Num(t.FireFuel(), 0)) : std::string(), lit ? 0u : T::Warn, !lit, -1.0f, L.Text("The campfire warms the clan and cooks its food") });
			int beds = 0, wells = 0;
			for (const Tribe::SettlementSite& s : t.SettlementSites())
				if (s.State.Done)
				{
					beds += s.Beds;
					if (s.Building.find("well") != std::string::npos) ++wells;
				}
			cards.push_back({ "building.shelter", "", L.Text("Beds"), L.Text("{0} for {1}", beds, alive), beds >= alive ? L.Text("{0} to spare", beds - alive) : L.Text("{0} lack one", alive - beds),
				beds >= alive ? 0u : T::Warn, beds < alive, -1.0f, L.Text("The beds of the houses that stand, against the people of the clan") });
			cards.push_back({ "water", "", L.Text("Water"), wells == 1 ? L.Text("1 well") : L.Text("{0} wells", wells), wells > 0 ? L.Text("enough") : L.Text("they drink where they find it"),
				0u, false, -1.0f, L.Text("The wells that stand: without one the clan goes to the river or the lake") });
			float mood = 0.0f;
			int sad = 0;
			for (const TribeMember& p : t.Members())
				if (!p.Dead) { const float v = t.Mood(p.Name); mood += v; sad += v < t.Config.Social.Mood.Low ? 1 : 0; }
			mood /= static_cast<float>(std::max(alive, 1));
			cards.push_back({ "state.happy", "", L.Text("Mood"), L.Text(mood >= 0.5f ? "content" : mood >= t.Config.Social.Mood.Low ? "so-so" : "sad"),
				sad == 0 ? L.Text("nobody sad") : sad == 1 ? L.Text("1 sad") : L.Text("{0} sad", sad), sad > 0 ? T::Warn : 0u, sad > 0, -1.0f, L.Text("The mean mood of the clan, and who is low") });
			const int perRow = 3;
			const float cw = (leftW - static_cast<float>(perRow - 1) * 8.0f) / static_cast<float>(perRow);
			// T342: the small line under the value goes on two lines when it needs them (the winter's "lacks 51 days of food" was cut at 1600):
			// every card of the row as tall as the tallest
			std::vector<std::vector<std::string>> subs(cards.size());
			int subLines = 1;
			for (size_t i = 0; i < cards.size(); ++i)
			{
				if (cards[i].Bar >= 0.0f) continue;
				subs[i] = Lines(cards[i].Sub, cw - 12.0f - (cards[i].Warn ? lh + 3.0f : 0.0f), 2);
				subLines = std::max(subLines, static_cast<int>(subs[i].size()));
			}
			const float ch = lh * static_cast<float>(2 + subLines) + 26.0f;
			for (size_t i = 0; i < cards.size(); ++i)
			{
				const Card& c = cards[i];
				if (i % perRow) ImGui::SameLine(0.0f, 8.0f);
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(cw, ch));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.Tip.c_str());
				dl->AddRectFilled(at, ImVec2(at.x + cw, at.y + ch), T::HideRaised, T::Radius);
				dl->AddRect(at, ImVec2(at.x + cw, at.y + ch), c.Edge ? c.Edge : T::HideLine, T::Radius, 0, c.Edge ? 2.0f : 1.0f);
				if (!IconAt(dl, c.Icon, at.x + 8.0f, at.y + 6.0f, lh + 4.0f)) DrawMark(dl, c.Mark, ImVec2(at.x + 8.0f, at.y + 6.0f), lh + 4.0f, T::Bone);
				dl->AddText(ImVec2(at.x + 14.0f + lh + 4.0f, at.y + 8.0f), T::BoneDim, c.Title.c_str());
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.2f, ImVec2(at.x + 8.0f, at.y + 12.0f + lh + 2.0f), T::Bone, c.Value.c_str());
				if (c.Bar >= 0.0f)
				{
					const float sy = at.y + ch - lh - 6.0f;
					dl->AddRectFilled(ImVec2(at.x + 8.0f, sy + 3.0f), ImVec2(at.x + cw - 8.0f, sy + lh - 3.0f), T::Hide, 2.0f);
					dl->AddRectFilled(ImVec2(at.x + 8.0f, sy + 3.0f), ImVec2(at.x + 8.0f + (cw - 16.0f) * std::clamp(c.Bar, 0.0f, 1.0f), sy + lh - 3.0f), T::Level(c.Bar), 2.0f);
				}
				else
				{
					float sx = at.x + 8.0f, sy = at.y + 14.0f + lh * 2.2f + 4.0f;
					if (c.Warn) { DrawMark(dl, "warn", ImVec2(sx, sy), lh, T::Warn); sx += lh + 3.0f; }
					for (const std::string& line : subs[i]) { dl->AddText(ImVec2(sx, sy), c.Warn ? T::Warn : T::BoneDim, line.c_str()); sy += lh; }
				}
			}
		}
		// knowledge, faith and people: folded, each with its summary
		ImGui::Spacing();
		auto folded = [&](const char* id, const char* icon, const std::string& label, const std::string& summary) -> bool
		{
			const ImVec2 r0 = ImGui::GetCursorScreenPos();
			const bool open = ImGui::TreeNodeEx((std::string("      ") + label + "###" + id).c_str(), ImGuiTreeNodeFlags_NoTreePushOnOpen);
			IconAt(dl, icon, r0.x + ImGui::GetTreeNodeToLabelSpacing(), r0.y + 1.0f, lh);
			const float sx = r0.x + 200.0f, right = r0.x + leftW;
			dl->PushClipRect(ImVec2(sx, r0.y), ImVec2(right, r0.y + lh + 4.0f), true);
			dl->AddText(ImVec2(sx, r0.y + ImGui::GetStyle().FramePadding.y), T::BoneDim, summary.c_str());
			dl->PopClipRect();
			if (ImGui::CalcTextSize(summary.c_str()).x > right - sx && ImGui::IsMouseHoveringRect(ImVec2(sx, r0.y), ImVec2(right, r0.y + lh + 4.0f)))
				ImGui::SetTooltip("%s", summary.c_str());
			return open;
		};
		{
			std::string discovering;
			int shownLines = 0, lines = 0;
			for (const ClanKnowledge::ResearchLine& l : t.Knowledge().Research())
				if (++lines <= 2) { discovering += (discovering.empty() ? "" : ", ") + Lang::Cap(L.Name("tech", l.Tech, t.Knowledge().Name(l.Tech))); ++shownLines; }
			if (lines > shownLines) discovering += " " + L.Text("and {0} more", lines - shownLines);
			const std::string summary = L.Text("{0} techniques", measure->Techs) + (discovering.empty() ? std::string() : " \xC2\xB7 " + L.Text("discovering {0}", discovering));
			if (folded("knowledge", "experiment", L.Text("Knowledge"), summary))
			{
				ImGui::Indent(14.0f);
				ImGui::TextUnformatted(L.Text("{0} technologies, {1} that better life", measure->Techs, measure->LifeTechs).c_str());
				if (ImGui::Button((L.Text("Tree of knowledge") + "###tree").c_str())) run("observer tree");
				ImGui::SameLine();
				if (ImGui::Button((L.Text("What it studies") + "###study").c_str())) run("colony tab research");
				ImGui::Unindent(14.0f);
			}
		}
		{
			const SocialState& social = t.Social();
			std::string faith;
			if (const std::string dominant = social.Dominant(); !dominant.empty())
			{
				const auto def = t.Config.Social.Beliefs.Systems.find(dominant);
				std::string name = L.Name("belief", dominant, def != t.Config.Social.Beliefs.Systems.end() ? def->second.Name : std::string());
				if (const size_t cut = name.find(" ("); cut != std::string::npos) name = name.substr(0, cut);
				const auto v = social.Beliefs.find(dominant);
				const float share = v == social.Beliefs.end() ? 0.0f : v->second;
				faith = Lang::Cap(name) + " \xC2\xB7 " + L.Text(share >= 0.6f ? "firm" : share >= 0.3f ? "shared" : "weak");
				if (folded("faith", "rite", L.Text("Faith and rites"), faith))
				{
					ImGui::Indent(14.0f);
					ImGui::TextUnformatted(L.Text("{0}, {1} %", Lang::Cap(name), L.Num(share * 100.0f, 0)).c_str());
					ImGui::Unindent(14.0f);
				}
			}
		}
		{
			const int gens = measure->Generations + 1;
			const std::string summary = L.Text(gens == 1 ? "1 generation" : "{0} generations", gens) + " \xC2\xB7 " + L.Text(measure->Inventors == 1 ? "1 inventor" : "{0} inventors", measure->Inventors);
			if (folded("people", "talk", L.Text("People and lineages"), summary))
			{
				ImGui::Indent(14.0f);
				ImGui::TextUnformatted(L.Text(measure->Lineages == 1 ? "1 lineage" : "{0} lineages", measure->Lineages).c_str());
				ImGui::Unindent(14.0f);
			}
		}
		ImGui::EndGroup();
		// how it lives, in detail: the 7 parts as words
		if (two) ImGui::SameLine(0.0f, 24.0f);
		else ImGui::Spacing();
		ImGui::BeginGroup();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("How it lives, in detail").c_str());
		for (const std::string& part : QolParts())
		{
			const auto it = measure->Parts.find(part);
			const float v = it == measure->Parts.end() ? -1.0f : it->second;
			const Level lv = LevelOf(v, part == "techs");
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(300.0f, lh + 10.0f));
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", (Lang::Cap(L.Text(PartWord(part))) + ": " + L.Text(lv.Word) + (v < 0.0f ? std::string() : " (" + L.Num(v, 2) + ")")
					+ (part == "techs" ? "\n" + L.Text("Low at the start, as it should: it grows with every technique the clan knows") : std::string())).c_str());
			const float y = at.y + 5.0f;
			if (!IconAt(dl, PartIcon(part), at.x, y - 1.0f, lh + 2.0f)) DrawMark(dl, "", ImVec2(at.x, y - 1.0f), lh + 2.0f, T::BoneDim);
			dl->AddText(ImVec2(at.x + lh + 8.0f, y), T::Bone, L.Text(PartWord(part)).c_str());
			Segments(dl, ImVec2(at.x + 140.0f, y + lh * 0.5f - 4.0f), 80.0f, 8.0f, 5, lv.Filled, lv.Colour);
			dl->AddText(ImVec2(at.x + 232.0f, y), T::BoneDim, L.Text(lv.Word).c_str());
		}
		if (measure->Losses > 0.0f)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(lh, lh));
			DrawMark(dl, "warn", at, lh, T::Bad);
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextUnformatted(L.Text("Losses (young dead lately): -{0} %", L.Num(measure->Losses * 100.0f, 0)).c_str());
		}
		ImGui::EndGroup();
	}

	// the sites of the clan: a card for each work under way (its phase as a bar with its %, what it lacks as icons with their number), and the
	// buildings that stand in a line each (shared with the god's book)
	void EditorApp::DrawSitesSheet(const Tribe& t)
	{
		const Lang& L = m_Lang;
		const auto sites = t.SettlementSites();
		if (sites.empty()) { ImGui::TextDisabled("%s", L.Text("no sites yet").c_str()); return; }
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float lh = ImGui::GetTextLineHeight();
		for (const bool done : { false, true })
		{
			bool first = true;
			for (const Tribe::SettlementSite& s : sites)
			{
				if (s.State.Done != done) continue;
				if (first) { ImGui::SeparatorText(L.Text(done ? "Built" : "Under way").c_str()); first = false; }
				ImGui::PushID(s.Name.c_str());
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(lh + 4.0f, lh + 4.0f));
				IconAt(dl, done ? "building.shelter" : "build", at.x, at.y, lh + 4.0f);
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(L.Entity(s.Name).c_str());
				if (const auto def = m_Buildings.Defs.find(s.Building); def != m_Buildings.Defs.end() && ImGui::IsItemHovered())
				{
					std::string tip;
					for (const std::string& line : BuildingDoes(def->second)) tip += (tip.empty() ? "" : "\n") + line;
					ImGui::SetTooltip("%s", tip.c_str());
				}
				ImGui::SameLine();
				if (done)
				{
					std::string gives;
					if (s.Beds > 0) gives = L.Text("{0} beds", s.Beds);
					if (!s.Station.empty()) gives += (gives.empty() ? "" : ", ") + L.Name("station", s.Station);
					ImGui::TextDisabled("%s", gives.c_str());
				}
				else
				{
					ImGui::TextDisabled("%s", L.Text("{0} ({1} of {2})", L.Name("phase", s.State.PhaseName), s.State.Phase + 1, s.State.Phases).c_str());
					ImGui::Indent(lh + 10.0f);
					const float pct = std::clamp(s.State.Progress, 0.0f, 1.0f);
					ImGui::ProgressBar(pct, ImVec2(160.0f, lh), (L.Num(pct * 100.0f, 0) + " %").c_str());
					bool lacks = false;
					for (const auto& [item, n] : s.State.Missing)
					{
						if (n <= 0) continue;
						ImGui::SameLine(0.0f, lacks ? 6.0f : 14.0f);
						if (!lacks) { ImGui::TextDisabled("%s", L.Text("lacks").c_str()); ImGui::SameLine(0.0f, 6.0f); lacks = true; }
						const ImVec2 i0 = ImGui::GetCursorScreenPos();
						ImGui::Dummy(ImVec2(lh + 2.0f, lh));
						if (!IconAt(dl, item, i0.x, i0.y - 1.0f, lh + 2.0f)) DrawMark(dl, "", i0, lh, T::BoneDim);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Name("item", item).c_str());
						ImGui::SameLine(0.0f, 2.0f);
						ImGui::TextUnformatted(std::to_string(n).c_str());
					}
					std::string under = s.Name;
					for (char& ch : under) if (ch == ' ') ch = '_';
					ImGui::SameLine(0.0f, 14.0f);
					if (ImGui::SmallButton((L.Text("See") + "###see").c_str())) m_UiCommands.push_back("colony inspect " + under);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Its sheet: what it lacks, who works on it, its place in the queue").c_str());
					ImGui::Unindent(lh + 10.0f);
				}
				ImGui::PopID();
			}
		}
	}
	// ------------------------------------------------------------------ T342 (L10): the chronicle of the colony
	// The clan's own events first (audit CR2; "all the clans" shows the neighbours' too), the groups as chips of an icon and its count (a click shows
	// or hides one; a group with nothing in it is not drawn: no zeros), and each event a row whose click sends the camera there (it was an [go] on
	// every row, CR1). The filters are the god's book's (`observer chronicle ...`, `observer go ...`): a script sees the same.
	void EditorApp::DrawColonyChronicle(const Clan& own, float width)
	{
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float lh = ImGui::GetTextLineHeight();
		const int clanFilter = m_Observer.ChronicleClan, weight = m_Observer.ChronicleWeight;
		auto filterCommand = [&](int clan, const std::set<std::string>& hidden, int least)
		{
			std::string c = "observer chronicle " + (clan < 0 ? std::string("all") : std::to_string(clan));
			for (int k = 0; k < StoryKindCount; ++k) if (!hidden.count(StoryKinds()[k].Id)) c += std::string(" ") + StoryKinds()[k].Id;
			return c + " weight " + std::to_string(least);
		};
		// the first time the colony opens it: the player's own clan
		if (!m_Colony.ChronicleOwn)
		{
			m_Colony.ChronicleOwn = true;
			if (clanFilter < 0) run(filterCommand(own.Id, m_Observer.ChronicleHidden, weight));
		}
		auto clanName = [&](int id) { const Clan* c = m_Clans.Find(static_cast<ClanId>(id)); return c ? L.Name("clan", c->Name, c->Name) : std::string(); };
		std::map<std::string, int> counts;
		std::vector<const HistoryEvent*> shown;
		for (const HistoryEvent& e : m_Clans.Past().Events())
		{
			const std::string kind = SheetKind(e.Kind);
			if (kind.empty() || (clanFilter >= 0 && e.Clan != clanFilter) || e.Weight < weight) continue;
			++counts[kind];
			if (!m_Observer.ChronicleHidden.count(kind)) shown.push_back(&e);
		}
		// ---- the groups: an icon and its count in a chip, lit when shown (a group with nothing is left out, unless it is hidden)
		{
			const float chipH = lh + 10.0f, icon = lh + 4.0f;
			float x = 0.0f;
			bool first = true;
			for (int k = 0; k < StoryKindCount; ++k)
			{
				const StoryKind& kind = StoryKinds()[k];
				const int n = counts[kind.Id];
				const bool on = !m_Observer.ChronicleHidden.count(kind.Id);
				if (n == 0 && on) continue;
				const std::string count = std::to_string(n);
				const float cw = 8.0f + icon + 5.0f + ImGui::CalcTextSize(count.c_str()).x + 9.0f;
				if (!first && x + 6.0f + cw <= width) { ImGui::SameLine(0.0f, 6.0f); x += 6.0f + cw; }
				else x = cw;
				first = false;
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::PushID(kind.Id);
				const bool pressed = ImGui::InvisibleButton("chip", ImVec2(cw, chipH));
				const bool hovered = ImGui::IsItemHovered();
				ImGui::PopID();
				if (hovered) ImGui::SetTooltip("%s", (Lang::Cap(L.Text(kind.Label)) + ": " + count + "\n" + L.Text(on ? "Click: hide them" : "Click: show them")).c_str());
				if (pressed)
				{
					std::set<std::string> hidden = m_Observer.ChronicleHidden;
					if (on) hidden.insert(kind.Id); else hidden.erase(kind.Id);
					if (static_cast<int>(hidden.size()) < StoryKindCount) run(filterCommand(clanFilter, hidden, weight));
				}
				dl->AddRectFilled(at, ImVec2(at.x + cw, at.y + chipH), on ? (hovered ? T::HideHover : T::HideActive) : T::Hide, chipH * 0.5f);
				dl->AddRect(at, ImVec2(at.x + cw, at.y + chipH), on ? kind.Colour : T::HideLine, chipH * 0.5f, 0, on ? 1.5f : 1.0f);
				IconAt(dl, kind.Icon, at.x + 8.0f, at.y + (chipH - icon) * 0.5f, icon, on ? 0xFFFFFFFFu : IM_COL32(255, 255, 255, 90));
				dl->AddText(ImVec2(at.x + 8.0f + icon + 5.0f, at.y + (chipH - lh) * 0.5f), on ? T::Bone : T::BoneDim, count.c_str());
			}
			if (first) ImGui::TextDisabled("%s", L.Text("nothing has happened yet").c_str());
		}
		// ---- whose: the clan's own, or every clan's; and how weighty
		if (m_Clans.Count() > 1)
		{
			const std::string mineWord = L.Text("Only the {0}", clanName(own.Id)), allWord = L.Text("Every clan");
			{
				std::optional<T::PrimaryScope> lit;
				if (clanFilter >= 0) lit.emplace();
				if (ImGui::SmallButton((mineWord + "###chronown").c_str()) && clanFilter < 0) run(filterCommand(own.Id, m_Observer.ChronicleHidden, weight));
			}
			ImGui::SameLine(0.0f, 4.0f);
			{
				std::optional<T::PrimaryScope> lit;
				if (clanFilter < 0) lit.emplace();
				if (ImGui::SmallButton((allWord + "###chronall").c_str()) && clanFilter >= 0) run(filterCommand(-1, m_Observer.ChronicleHidden, weight));
			}
			ImGui::SameLine(0.0f, 16.0f);
		}
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", L.Text("Weight").c_str());
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("How much the clans remember it: 1 a small thing, 5 something never forgotten").c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		auto weightText = [&](int n) { return n <= 1 ? L.Text("any") : L.Text("{0} or more", n); };
		if (ImGui::BeginCombo("###chronweight", weightText(weight).c_str()))
		{
			for (int n = 1; n <= 5; ++n)
				if (ImGui::Selectable((weightText(n) + "###w" + std::to_string(n)).c_str(), weight == n)) run(filterCommand(clanFilter, m_Observer.ChronicleHidden, n));
			ImGui::EndCombo();
		}
		ImGui::Separator();
		if (shown.empty())
		{
			if (!counts.empty()) ImGui::TextDisabled("%s", L.Text("nothing passes these filters").c_str());
			return;
		}
		// ---- the events, newest first under their day (the newest 300): a click on one sends the camera there
		int day = -1, drawn = 0;
		for (auto it = shown.rbegin(); it != shown.rend() && drawn < 300; ++it, ++drawn)
		{
			const HistoryEvent& e = **it;
			if (const int d = EventDay(e.At); d != day)
			{
				day = d;
				ImGui::Spacing();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("Day {0}", day).c_str());
			}
			const StoryKind* kind = FindStoryKind(SheetKind(e.Kind));
			const std::string text = Lang::Cap(L.Line(e.Text));
			const bool stranger = e.Clan >= 0 && e.Clan != own.Id;   // (another clan's: its colour and its name before the line)
			const std::string tag = stranger ? clanName(e.Clan) : std::string();
			const float textX = 12.0f + lh + 6.0f + (stranger ? lh + ImGui::CalcTextSize(tag.c_str()).x + 10.0f : 0.0f);
			const float wrap = std::max(80.0f, width - textX - 6.0f);
			const ImVec2 ts = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
			const float rowH = std::max(lh, ts.y) + 4.0f;
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::PushID(e.Id);
			const bool pressed = ImGui::InvisibleButton("row", ImVec2(width, rowH));
			const bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			if (pressed && e.HasWhere) run("observer go " + std::to_string(e.Id));
			if (hovered)
			{
				if (e.HasWhere) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				ImGui::SetTooltip("%s", (e.HasWhere ? L.Text("Click: the camera to where it happened") : L.Text("It has no place to go to")).c_str());
				dl->AddRectFilled(at, ImVec2(at.x + width, at.y + rowH), T::HideHover, 3.0f);
			}
			dl->AddRectFilled(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + 3.0f, at.y + rowH - 2.0f), kind ? kind->Colour : T::BoneDim);
			if (kind) IconAt(dl, kind->Icon, at.x + 10.0f, at.y + 2.0f, lh);
			float x = at.x + 12.0f + lh + 6.0f;
			if (stranger)
			{
				dl->AddRectFilled(ImVec2(x, at.y + 4.0f), ImVec2(x + lh - 4.0f, at.y + lh), ClanColourOf(m_Clans, static_cast<ClanId>(e.Clan)), 2.0f);
				dl->AddText(ImVec2(x + lh, at.y + 2.0f), T::BoneDim, tag.c_str());
			}
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + textX, at.y + 2.0f), T::Bone, text.c_str(), nullptr, wrap);
		}
		if (shown.size() > 300) ImGui::TextDisabled("%s", L.Text("(and {0} older)", static_cast<int>(shown.size()) - 300).c_str());
	}

	// ------------------------------------------------------------------ T342 (L10): the households of the colony
	// A row per family: its house (or "no house"), its name and its people with their trade in their gender ("Ana, recolectora"); a family of more
	// than one gets its card, with a line per member and its pantry. A click on the house opens its sheet (`colony inspect`).
	void EditorApp::DrawColonyHomes(const Tribe& t, float width)
	{
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float lh = ImGui::GetTextLineHeight();
		const auto& households = t.Households();
		if (households.empty()) { ImGui::TextDisabled("%s", L.Text("no households yet: they form as couples pair and children are born").c_str()); return; }
		int housed = 0;
		for (const Tribe::Household& h : households) housed += h.Home.empty() ? 0 : 1;
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(lh + 4.0f, lh + 4.0f));
			IconAt(dl, "building.shelter", at.x, at.y, lh + 4.0f);
			ImGui::SameLine(0.0f, 8.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(L.Text(households.size() == 1 ? "1 family" : "{0} families", static_cast<int>(households.size())).c_str());
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextDisabled("%s", (" \xC2\xB7 " + L.Text(housed == 1 ? "1 with a house of its own" : "{0} with a house of their own", housed)).c_str());
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextDisabled("%s", (" \xC2\xB7 " + L.Text("property: {0}", L.Name("property", t.Config.Economy.Property))).c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Whose the food of the houses is: the clan's, the family's or each one's").c_str());
		}
		ImGui::Separator();
		auto person = [&](const std::string& name)
		{
			const TribeMember* m = t.FindMember(name);
			if (!m || !t.People().Find(name)) return name;
			const Society::Stage stage = t.People().StageOf(t.Config.Society, name);
			return name + ", " + (stage == Society::Stage::Child ? L.Text("child") : stage == Society::Stage::Elder ? L.Text("elder") + " " + RoleName(t, *m) : RoleName(t, *m));
		};
		const float nameX = lh + 12.0f, homeX = std::max(nameX + 200.0f, width * 0.40f), peopleX = std::max(homeX + 170.0f, width * 0.66f);
		for (const Tribe::Household& h : households)
		{
			ImGui::PushID(h.Id.c_str());
			const bool card = h.Members.size() > 1;
			const ImVec2 top = ImGui::GetCursorScreenPos();
			if (card) { ImGui::Dummy(ImVec2(1.0f, 3.0f)); }
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float rowH = lh + 8.0f;
			ImGui::Dummy(ImVec2(width, rowH));
			// the house: its icon (faded without one), the family's name, the house's name (a click: its sheet) or "no house"
			IconAt(dl, "building.shelter", at.x + 4.0f, at.y + 3.0f, lh + 2.0f, h.Home.empty() ? IM_COL32(255, 255, 255, 70) : 0xFFFFFFFFu);
			dl->AddText(ImVec2(at.x + nameX, at.y + 4.0f), T::Bone, L.Text("The {0} family", h.Id).c_str());
			if (h.Home.empty())
			{
				DrawMark(dl, "warn", ImVec2(at.x + homeX, at.y + 4.0f), lh, T::Warn);
				dl->AddText(ImVec2(at.x + homeX + lh + 4.0f, at.y + 4.0f), T::Warn, L.Text("no house").c_str());
			}
			else
			{
				const std::string home = L.Entity(h.Home);
				const ImVec2 hs = ImGui::CalcTextSize(home.c_str());
				ImGui::SetCursorScreenPos(ImVec2(at.x + homeX, at.y + 2.0f));
				if (ImGui::InvisibleButton("home", ImVec2(hs.x + 4.0f, lh + 4.0f)))
				{
					std::string under = h.Home;
					for (char& c : under) if (c == ' ') c = '_';
					run("colony inspect " + under);
				}
				const bool hovered = ImGui::IsItemHovered();
				if (hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", L.Text("Its sheet").c_str()); }
				dl->AddText(ImVec2(at.x + homeX, at.y + 4.0f), hovered ? T::OchreHover : T::Good, home.c_str());
				dl->AddLine(ImVec2(at.x + homeX, at.y + 5.0f + lh), ImVec2(at.x + homeX + hs.x, at.y + 5.0f + lh), T::Faded(T::Good, hovered ? 1.0f : 0.4f), 1.0f);
				ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH));
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
			if (!card)
			{
				// one person: the person on the same row
				if (!h.Members.empty())
				{
					dl->PushClipRect(ImVec2(at.x + peopleX, at.y), ImVec2(at.x + width, at.y + rowH), true);
					dl->AddText(ImVec2(at.x + peopleX, at.y + 4.0f), T::BoneDim, person(h.Members.front()).c_str());
					dl->PopClipRect();
				}
			}
			else
			{
				// a family: a line per member, then its pantry when the food is the family's
				ImGui::Indent(nameX);
				for (const std::string& name : h.Members) ImGui::TextDisabled("%s", person(name).c_str());
				if (const Inventory* pantry = t.Pantry(h.Id); t.Config.Economy.Property != "communal" || (pantry && pantry->UsedSlots() > 0))
				{
					int food = 0;
					if (pantry) for (const ItemSlot& s : pantry->Stacks()) if (s.Count > 0 && t.IsFood(s.Id)) food += s.Count;
					const int room = std::max(1, t.Config.Economy.PantryPerMember * static_cast<int>(h.Members.size()));
					ImGui::TextUnformatted(L.Text("Pantry").c_str());
					ImGui::SameLine();
					const ImVec2 b = ImGui::GetCursorScreenPos();
					ImGui::Dummy(ImVec2(120.0f, lh));
					const float v = std::clamp(static_cast<float>(food) / static_cast<float>(room), 0.0f, 1.0f);
					dl->AddRectFilled(ImVec2(b.x, b.y + 3.0f), ImVec2(b.x + 120.0f, b.y + lh - 3.0f), T::HideRaised, 2.0f);
					if (v > 0.0f) dl->AddRectFilled(ImVec2(b.x, b.y + 3.0f), ImVec2(b.x + 120.0f * v, b.y + lh - 3.0f), T::Level(v), 2.0f);
					ImGui::SameLine();
					ImGui::TextDisabled("%s", L.Text("{0} of {1} food", food, room).c_str());
				}
				ImGui::Unindent(nameX);
				const ImVec2 bottom = ImGui::GetItemRectMax();
				dl->AddRect(ImVec2(top.x, top.y), ImVec2(top.x + width, bottom.y + 4.0f), T::HideLine, T::Radius);
				ImGui::Dummy(ImVec2(1.0f, 4.0f));
			}
			ImGui::PopID();
		}
	}

	// ------------------------------------------------------------------ T342 (L10): the other clans, seen from the colony
	// Only the clans met (one not met yet has no name: it is only counted); how they stand as a word with its colour, the feeling as a bar from
	// enmity to friendship with a word (the numbers in the tooltip), and what passed between them as icons with their count, only those that are not 0
	// (audit CL1: no lists of zeros; the whole list is in the tooltip).
	void EditorApp::DrawColonyClans(const Tribe& t, float width)
	{
		const Lang& L = m_Lang;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float lh = ImGui::GetTextLineHeight();
		ClanId mine = -1;
		for (const auto& c : m_Clans.All()) if (c->Folk == &t) mine = c->Id;
		if (m_Clans.Count() < 2 || mine < 0) { ImGui::TextDisabled("%s", L.Text("the clan is alone in the world").c_str()); return; }
		int unmet = 0, met = 0;
		for (const auto& c : m_Clans.All())
		{
			if (!c->Folk || c->Id == mine) continue;
			if (!m_Clans.Met(mine, c->Id)) { ++unmet; continue; }
			++met;
			ImGui::PushID(static_cast<int>(c->Id));
			int alive = 0;
			for (const TribeMember& m : c->Folk->Members()) alive += m.Dead ? 0 : 1;
			const ImVec2 top = ImGui::GetCursorScreenPos();
			ImGui::Indent(T::Pad);
			ImGui::Dummy(ImVec2(1.0f, 2.0f));
			// who: its colour, its name, its people; how they stand, on the right
			{
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(lh, lh));
				dl->AddRectFilled(ImVec2(at.x, at.y + 1.0f), ImVec2(at.x + lh - 2.0f, at.y + lh - 1.0f), ClanColourOf(m_Clans, c->Id), 2.0f);
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::TextUnformatted(L.Name("clan", c->Name, c->Name).c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s", L.Text(alive == 1 ? "{0} person" : "{0} people", alive).c_str());
				const std::string stance = m_Clans.Stance(mine, c->Id);
				std::string words = Lang::Cap(L.Name("stance", stance));
				if (const ClanRelation* r = m_Clans.Relation(mine, c->Id)) for (const Pact& p : r->Pacts) words += " \xC2\xB7 " + L.Name("pact", p.Kind);
				const float sw = ImGui::CalcTextSize(words.c_str()).x;
				ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f, width - sw - T::Pad));
				const ImU32 sc = stance == "allied" || stance == "friendly" ? T::Good : stance == "hostile" ? T::Bad : stance == "wary" || stance == "truce" ? T::Warn : T::BoneDim;
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(sc), "%s", words.c_str());
			}
			// the feeling: a bar from enmity (left) to friendship (right), a word; the numbers in the tooltip
			{
				const float attitude = std::clamp(m_Clans.Attitude(mine, c->Id), -1.0f, 1.0f), trust = m_Clans.Trust(mine, c->Id);
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("%s", L.Text("Feeling").c_str());
				ImGui::SameLine(110.0f);
				const ImVec2 b = ImGui::GetCursorScreenPos();
				const float bw = 160.0f;
				ImGui::Dummy(ImVec2(bw, lh + 2.0f));
				const bool hovered = ImGui::IsItemHovered();
				const float mid = b.x + bw * 0.5f, end = mid + bw * 0.5f * attitude, y0 = b.y + 4.0f, y1 = b.y + lh - 2.0f;
				const ImU32 fc = attitude > 0.15f ? T::Good : attitude < -0.15f ? T::Bad : T::Warn;
				dl->AddRectFilled(ImVec2(b.x, y0), ImVec2(b.x + bw, y1), T::HideRaised, 2.0f);
				dl->AddRectFilled(ImVec2(std::min(mid, end), y0), ImVec2(std::max(mid, end) + (std::abs(end - mid) < 2.0f ? 2.0f : 0.0f), y1), fc, 2.0f);
				dl->AddRect(ImVec2(b.x, y0), ImVec2(b.x + bw, y1), T::HideLine, 2.0f);
				dl->AddLine(ImVec2(mid, y0 - 2.0f), ImVec2(mid, y1 + 2.0f), T::BoneDim, 1.0f);
				ImGui::SameLine(0.0f, 10.0f);
				const char* feel = attitude > 0.5f ? "close friends" : attitude > 0.15f ? "friendly" : attitude < -0.5f ? "enemies" : attitude < -0.15f ? "unfriendly" : "neither friends nor foes";
				const char* trustWord = trust >= 0.6f ? "much trust" : trust >= 0.25f ? "some trust" : "little trust yet";
				ImGui::TextUnformatted((L.Text(feel) + " \xC2\xB7 " + L.Text(trustWord)).c_str());
				if (hovered || ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", (L.Text("To the left enmity, to the right friendship") + "\n" + L.Text("{0}, trust {1}", (attitude >= 0.0f ? "+" : "") + L.Num(attitude, 2), L.Num(trust * 100.0f, 0) + " %")).c_str());
			}
			// what passed between them: only what happened (an icon and its count); the whole list in the tooltip
			if (const ClanRelation* r = m_Clans.Relation(mine, c->Id))
			{
				struct Count { const char* Icon; const char* One; const char* Many; int N; ImU32 Ink; };
				const Count counts[] = { { "talk", "met once", "met {0} times", r->Encounters, T::Bone }, { "trade", "1 trade", "{0} trades", r->Trades, T::Bone },
					{ "deposit", "1 gift", "{0} gifts", r->Gifts, T::Bone }, { "experiment", "1 lesson", "{0} lessons", r->Lessons, T::Bone },
					{ "state.love", "1 marriage", "{0} marriages", r->Marriages, T::Bone }, { "state.angry", "1 theft", "{0} thefts", r->Thefts, T::Bad },
					{ "fight", "1 raid", "{0} raids", r->Raids, T::Bad } };
				std::string all;
				bool any = false;
				for (size_t i = 0; i < std::size(counts); ++i)
				{
					const Count& k = counts[i];
					const std::string word = L.Text(k.N == 1 ? k.One : k.Many, k.N);
					all += (all.empty() ? "" : "\n") + word;
					if (k.N <= 0) continue;
					if (any) ImGui::SameLine(0.0f, 12.0f);
					else { ImGui::Dummy(ImVec2(0.0f, 0.0f)); ImGui::SameLine(110.0f); }
					any = true;
					const ImVec2 at = ImGui::GetCursorScreenPos();
					ImGui::Dummy(ImVec2(lh + 2.0f, lh + 2.0f));
					if (!IconAt(dl, k.Icon, at.x, at.y, lh + 2.0f)) DrawMark(dl, "", at, lh + 2.0f, T::BoneDim);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", word.c_str());
					ImGui::SameLine(0.0f, 3.0f);
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(k.Ink), "%s", (i == 0 ? word : std::to_string(k.N)).c_str());
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", all.c_str());
				}
				if (!r->Recent.empty())
				{
					// a line of Recent is "<the clans' clock in seconds> <event>: <what happened>" (ClanWorld::Record): how long ago, and what
					const std::string& last = r->Recent.back();
					char* end = nullptr;
					const double at = std::strtod(last.c_str(), &end);
					const bool stamped = end != nullptr && end != last.c_str();
					std::string what = stamped ? std::string(end) : last;
					what.erase(0, std::min(what.size(), what.find_first_not_of(' ')));
					if (const size_t colon = what.find(": "); colon != std::string::npos && what.find(' ') > colon) what = what.substr(colon + 2);
					what = L.Line(what);
					const int ago = stamped ? static_cast<int>(std::max(0.0, m_Clans.Time() - at) / std::max(1.0, static_cast<double>(t.Config.Needs.DaySeconds))) : -1;
					ImGui::PushTextWrapPos(top.x + width - T::Pad - ImGui::GetWindowPos().x);
					ImGui::TextDisabled("%s", (ago < 0 ? L.Text("lately: {0}", what) : ago == 0 ? L.Text("lately (today): {0}", what) : ago == 1 ? L.Text("lately (yesterday): {0}", what)
						: L.Text("lately ({0} days ago): {1}", ago, what)).c_str());
					ImGui::PopTextWrapPos();
				}
			}
			ImGui::Dummy(ImVec2(1.0f, 2.0f));
			ImGui::Unindent(T::Pad);
			const ImVec2 bottom = ImGui::GetItemRectMax();
			dl->AddRect(ImVec2(top.x, top.y - 2.0f), ImVec2(top.x + width, bottom.y + 3.0f), T::HideLine, T::Radius);
			ImGui::Dummy(ImVec2(1.0f, 6.0f));
			ImGui::PopID();
		}
		// the clans not met yet: how many, never who (what the clan does not know does not show)
		if (unmet > 0) ImGui::TextDisabled("%s", (met == 0 ? L.Text(unmet == 1 ? "There is another clan in the world: you have not met it yet" : "There are {0} other clans in the world: you have not met them yet", unmet)
			: L.Text(unmet == 1 ? "And 1 clan you have not met yet" : "And {0} clans you have not met yet", unmet)).c_str());
	}

	// ------------------------------------------------------------------ T342 (L10, p1): the sheet of a node, a building or a site
	// As the person's: a head (an icon in a ring, its name, what it is; go and close), a card with an ochre edge with what matters most (what is
	// left of a node, what a building does, the phase of a site), then rows of an icon, a word and a value, then what the player can do with it as
	// buttons of an icon and a word. Each button queues the command the old card queued (`game designate`, `game site`), and T333's Transplant
	// (`colony transplant <node>`: then a click on the ground) only when the node can be moved and the clan knows its verb (T327: else no button).
	void EditorApp::DrawColonyThing(const std::string& name, float width)
	{
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float lh = ImGui::GetTextLineHeight();
		const AbilitySystemComponent* asc = m_Gameplay.Find(name);
		if (!asc) return;
		const Tribe* own = ColonyTribe();
		std::string under = name;
		for (char& c : under) if (c == ' ') c = '_';
		auto itemName = [&](const std::string& id) { const ItemDef* d = m_Items.Find(id); return L.Name("item", id, d && !d->Name.empty() ? d->Name : std::string()); };
		// ---- the head
		auto head = [&](const std::string& icon, const std::string& sub)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float ring = 44.0f;
			ImGui::Dummy(ImVec2(ring, ring));
			dl->AddCircleFilled(ImVec2(at.x + ring * 0.5f, at.y + ring * 0.5f), ring * 0.5f, T::HideRaised, 24);
			dl->AddCircle(ImVec2(at.x + ring * 0.5f, at.y + ring * 0.5f), ring * 0.5f, T::Ochre, 24, 2.0f);
			if (!IconAt(dl, icon, at.x + 8.0f, at.y + 8.0f, ring - 16.0f)) DrawMark(dl, "", ImVec2(at.x + 8.0f, at.y + 8.0f), ring - 16.0f, T::BoneDim);
			ImGui::SameLine(0.0f, 10.0f);
			const float b = ImGui::GetFrameHeight() + 4.0f;
			ImGui::BeginGroup();
			ImGui::PushTextWrapPos(at.x + width - 2.0f * (b + style.ItemSpacing.x) - ImGui::GetWindowPos().x);
			ImGui::SetWindowFontScale(1.3f);
			ImGui::TextUnformatted(Lang::Cap(L.Entity(name)).c_str());
			ImGui::SetWindowFontScale(1.0f);
			ImGui::TextDisabled("%s", sub.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndGroup();
			const float below = ImGui::GetItemRectMax().y;
			// go and close, on the right of the head
			ImGui::SetCursorScreenPos(ImVec2(at.x + width - 2.0f * b - style.ItemSpacing.x, at.y + 2.0f));
			{
				const ImVec2 bt = ImGui::GetCursorScreenPos();
				if (ImGui::Button("###goto", ImVec2(b, b)))
					if (const EntityId id = m_SceneEditor.World().FindByName(name); id != NullEntity)
					{
						const glm::vec3 p(m_SceneEditor.World().WorldMatrix(id)[3]);
						char x[32], z[32];
						std::snprintf(x, sizeof x, "%.2f", static_cast<double>(p.x));
						std::snprintf(z, sizeof z, "%.2f", static_cast<double>(p.z));
						run(std::string("camera game overview ") + x + " " + z + " 30");
					}
				IconAt(dl, "goto", bt.x + 4.0f, bt.y + 4.0f, b - 8.0f);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The camera to it").c_str());
			}
			ImGui::SameLine();
			if (ImGui::Button("\xC3\x97###close", ImVec2(b, b))) run("colony inspect off");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Close").c_str());
			ImGui::SetCursorScreenPos(ImVec2(at.x, std::max(at.y + ring, below)));
			ImGui::Dummy(ImVec2(0.0f, 0.0f));
		};
		// ---- a card with an ochre edge round what `body` draws
		auto edged = [&](const std::function<void()>& body)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			dl->ChannelsSplit(2);
			dl->ChannelsSetCurrent(1);
			ImGui::Indent(10.0f);
			ImGui::Dummy(ImVec2(1.0f, 2.0f));
			ImGui::PushTextWrapPos(at.x + width - 8.0f - ImGui::GetWindowPos().x);
			body();
			ImGui::PopTextWrapPos();
			ImGui::Dummy(ImVec2(1.0f, 2.0f));
			ImGui::Unindent(10.0f);
			const ImVec2 end(at.x + width, ImGui::GetCursorScreenPos().y);
			dl->ChannelsSetCurrent(0);
			dl->AddRectFilled(at, end, T::HideRaised, T::Radius);
			dl->AddRectFilled(at, ImVec2(at.x + 3.0f, end.y), T::Faded(T::Ochre, 0.7f), T::Radius, ImDrawFlags_RoundCornersLeft);
			dl->ChannelsMerge();
		};
		// ---- a row: an icon, a word, its value (wrapped)
		const float valueX = lh + 96.0f;
		auto row = [&](const std::string& icon, const std::string& label, const std::string& value, ImU32 ink, const std::string& tip)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(lh + 2.0f, lh));
			if (!IconAt(dl, icon, at.x, at.y - 1.0f, lh + 2.0f)) DrawMark(dl, "", at, lh, T::BoneDim);
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::TextDisabled("%s", label.c_str());
			// (a word too long for its column puts the value on the next line, at the same column)
			if (ImGui::GetItemRectMax().x + 6.0f > at.x + valueX) ImGui::SetCursorScreenPos(ImVec2(at.x + valueX, ImGui::GetCursorScreenPos().y));
			else ImGui::SameLine(at.x - ImGui::GetWindowPos().x + valueX);
			ImGui::PushTextWrapPos(at.x + width - ImGui::GetWindowPos().x);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ink), "%s", value.c_str());
			ImGui::PopTextWrapPos();
			if (!tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
		};
		// ---- things as icons with their number (what a site lacks, what was brought), the name in the tooltip
		auto things = [&](const std::map<std::string, int>& list, const SiteState* site)
		{
			bool first = true;
			for (const auto& [item, n] : list)
			{
				if (n <= 0) continue;
				const float side = lh + 10.0f, cw = side + 4.0f + ImGui::CalcTextSize(std::to_string(n).c_str()).x + 6.0f;
				if (!first) ImGui::SameLine(0.0f, 6.0f);
				if (!first && ImGui::GetCursorScreenPos().x + cw > ImGui::GetWindowPos().x + style.WindowPadding.x + width) ImGui::NewLine();
				first = false;
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(cw, side));
				dl->AddRectFilled(at, ImVec2(at.x + cw, at.y + side), T::HideRaised, 3.0f);
				dl->AddRect(at, ImVec2(at.x + cw, at.y + side), T::HideLine, 3.0f);
				if (!IconAt(dl, item, at.x + 2.0f, at.y + 2.0f, side - 4.0f)) DrawMark(dl, "", ImVec2(at.x + 2.0f, at.y + 2.0f), side - 4.0f, T::BoneDim);
				dl->AddText(ImVec2(at.x + side + 4.0f, at.y + (side - lh) * 0.5f), T::Bone, std::to_string(n).c_str());
				if (ImGui::IsItemHovered())
				{
					std::string tip = Lang::Cap(itemName(item)) + " x" + std::to_string(n);
					if (site)
						if (const auto sub = site->Substitutes.find(item); sub != site->Substitutes.end())
							for (const std::string& other : sub->second) tip += "\n" + L.Text("{0} can stand in for {1}", Lang::Cap(itemName(other)), itemName(item));
					ImGui::SetTooltip("%s", tip.c_str());
				}
			}
		};

		// ================= a building or a site
		if (const BuildingDef* def = BuildingOf(*asc, m_Buildings.Defs))
		{
			const SiteState s = ReadSite(*asc, *def);
			if (s.Done)
			{
				std::string sub = L.Text("stands");
				if (def->Shelter > 0) sub += " \xC2\xB7 " + L.Text("{0} beds", def->Shelter);
				if (!def->Station.empty()) sub += " \xC2\xB7 " + L.Name("station", def->Station);
				head(def->Shelter > 0 ? "building.shelter" : "build", sub);
			}
			else head("build", L.Text("a work under way") + " \xC2\xB7 " + L.Text("phase {0} of {1}", s.Phase + 1, s.Phases));
			// what it does (or will do)
			edged([&]()
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", (s.Done ? L.Text("What it does") : L.Text("When it stands")).c_str());
				for (const std::string& line : BuildingDoes(*def)) { ImGui::Bullet(); ImGui::SameLine(); ImGui::TextUnformatted(line.c_str()); }
			});
			if (s.Done)
			{
				// whose house it is and who lives in it
				if (def->Shelter > 0)
				{
					const Tribe::Household* home = nullptr;
					const Tribe* owner = nullptr;
					auto find = [&](const Tribe& tt) { if (!home) if (const Tribe::Household* hh = tt.HouseholdAt(name)) { home = hh; owner = &tt; } };
					find(m_Tribe);
					for (const auto& c : m_Clans.All()) if (c->Folk) find(*c->Folk);
					if (!home) row("building.shelter", L.Text("Home of"), L.Text("nobody's yet: anyone may sleep in it"), T::BoneDim, std::string());
					else
					{
						row("building.shelter", L.Text("Home of"), L.Text("The {0} family", home->Id), T::Bone, std::string());
						const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.World().FindByName(name));
						for (const std::string& dweller : home->Members)
						{
							const TribeMember* m = owner->FindMember(dweller);
							const Actor* actor = m_Actors.Find(dweller);
							const bool inside = actor && e && glm::distance(glm::vec2(actor->GetPosition().x, actor->GetPosition().z), glm::vec2(e->Transform.Position.x, e->Transform.Position.z)) < def->Radius() + 0.8f;
							ImGui::SetCursorPosX(ImGui::GetCursorPosX() + valueX);
							std::string line = dweller + (m ? ", " + RoleName(*owner, *m) : std::string());
							if (inside) line += " \xC2\xB7 " + L.Text("inside");
							else if (m) line += " \xC2\xB7 " + L.Doing(m->Job, m->Detail);
							ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - valueX);
							ImGui::TextDisabled("%s", line.c_str());
							ImGui::PopTextWrapPos();
						}
					}
				}
				// who works at its station now
				if (!def->Station.empty())
				{
					std::string workers;
					auto scan = [&](const Tribe& tt)
					{
						for (const TribeMember& m : tt.Members())
							if (!m.Dead && m.Job == "make")
								if (const Recipe* r = m_Recipes.Find(m.Detail); r && r->Station == def->Station) workers += (workers.empty() ? "" : ", ") + m.Name + " (" + itemName(r->Output) + ")";
					};
					for (const auto& c : m_Clans.All()) if (c->Folk) scan(*c->Folk);
					row("make", L.Text("Working here"), workers.empty() ? L.Text("nobody now") : workers, workers.empty() ? T::BoneDim : T::Bone, std::string());
				}
				// what its store holds (its clan's stockpile, and its share of the clan's main store)
				if (!def->Store.empty() || def->StoreCap > 0 || def->FoodCap > 0)
				{
					const Stockpile* pile = nullptr;
					const Tribe* clan = nullptr;
					auto find = [&](const Tribe& tt)
					{
						for (const Stockpile& p : tt.Economy().Stockpiles) if (p.Name == name) pile = &p;
						for (const auto& site : tt.SettlementSites()) if (!clan && site.Name == name) clan = &tt;
					};
					for (const auto& c : m_Clans.All()) if (!clan && c->Folk) find(*c->Folk);
					std::map<std::string, int> held;
					if (pile) for (const ItemSlot& it : pile->Store.Stacks()) held[it.Id] += it.Count;
					if (clan)
						if (const StoreLayout layout = StoreLayout::Of(*clan); const StoreLayout::Place* place = layout.Find(name))
							for (const auto& [id, count] : place->Held) held[id] += count;
					int units = 0;
					for (const auto& [id, n] : held) units += n;
					row("deposit", L.Text("Store"), units == 0 ? L.Text("empty") : L.Text("{0} things", units), units == 0 ? T::BoneDim : T::Bone, std::string());
					if (units > 0) things(held, nullptr);
				}
				DrawYardSheet(name, *def, width);   // prototype TALLERES 10 / TL4: the yard of a house and the recipes of a station (ColonyYard.cpp)
				// T342: what the radial offers on it is still to come (repair, improve, move, pull down): not drawn here until it exists
			}
			else
			{
				// the site: its phase as a bar with its %, what it lacks and what was brought as icons, who works on it, its place in the queue
				edged([&]()
				{
					ImGui::TextUnformatted(Lang::Cap(L.Name("phase", s.PhaseName)).c_str());
					const ImVec2 b = ImGui::GetCursorScreenPos();
					const float bw = width - 70.0f;
					ImGui::Dummy(ImVec2(bw, lh));
					const float v = std::clamp(s.Progress, 0.0f, 1.0f);
					dl->AddRectFilled(ImVec2(b.x, b.y + 3.0f), ImVec2(b.x + bw, b.y + lh - 3.0f), T::Hide, 2.0f);
					if (v > 0.0f) dl->AddRectFilled(ImVec2(b.x, b.y + 3.0f), ImVec2(b.x + bw * v, b.y + lh - 3.0f), T::Ochre, 2.0f);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("{0} of {1} s of work in this phase", static_cast<int>(s.Work), static_cast<int>(s.WorkNeeded)).c_str());
					ImGui::SameLine();
					ImGui::TextUnformatted((L.Num(v * 100.0f, 0) + " %").c_str());
				});
				if (s.Missing.empty()) row("build", L.Text("Lacks"), L.Text("nothing: it can be built"), T::Good, std::string());
				else { row("build", L.Text("Lacks"), std::string(), T::Bone, std::string()); things(s.Missing, &s); }
				if (SiteInfo info; SiteInfoOf(name, info))
				{
					if (!info.Brought.empty()) { row("deliver", L.Text("Brought"), std::string(), T::Bone, std::string()); things(info.Brought, nullptr); }
					std::string workers;
					for (const std::string& wk : info.Workers) workers += (workers.empty() ? "" : ", ") + wk.substr(0, wk.find(" ("));
					row("make", L.Text("Working on it"), workers.empty() ? L.Text("nobody now") : workers, workers.empty() ? T::BoneDim : T::Bone, std::string());
					row("build", L.Text("Queue"), L.Text("Plan {0} of {1} in the queue", info.Queue, info.Queued), T::Bone, L.Text("The clan works on the first plans of the queue first"));
					ImGui::Separator();
					ImGui::BeginDisabled(info.Queue <= 1);
					if (ImGui::Button((L.Text("Move up") + "###siteup").c_str())) run("game site " + under + " up");
					ImGui::EndDisabled();
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", (info.Queue <= 1 ? L.Text("It is already the first") : L.Text("The clan works on it before the others")).c_str());
					ImGui::SameLine();
					ImGui::BeginDisabled(info.Queue >= info.Queued);
					if (ImGui::Button((L.Text("Move down") + "###sitedown").c_str())) run("game site " + under + " down");
					ImGui::EndDisabled();
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", (info.Queue >= info.Queued ? L.Text("It is already the last") : L.Text("The clan works on it after the others")).c_str());
					ImGui::SameLine();
					if (IconLabelButton("sitecancel", "state.cannot", L.Text("Cancel the plan"), false)) run("game site cancel " + under);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("It comes down; what was brought for it goes back to the store").c_str());
				}
			}
			return;
		}

		// ================= an animal
		for (const auto& [sid, sp] : m_Ecology.Species)
			if (asc->HasTag(sp.Tag))
			{
				head("hunt", Lang::Cap(L.Name("animal", sp.Name, sp.Name)) + " \xC2\xB7 " + L.Text(sp.Hunts() ? "a hunter" : "grazes"));
				for (const auto& at : asc->Attributes().All())
					if (at.Max > 0.0f && (at.Name == "Food" || at.Name == "Health"))
					{
						const float v = std::clamp(at.Current / at.Max, 0.0f, 1.0f);
						row(at.Name == "Food" ? "state.hungry" : "state.wounded", L.Text(at.Name == "Food" ? "Fed" : "Health"), L.Num(v * 100.0f, 0) + " %", T::Level(v), std::string());
					}
				std::string eats;
				for (const std::string& e : sp.Eats) eats += (eats.empty() ? "" : ", ") + L.Name("item", e);
				row("eat", L.Text("Eats"), eats, T::Bone, std::string());
				return;
			}

		// ================= a resource node
		std::string kind;
		for (const auto& [id, d] : m_NodeDefs) if (asc->HasTag("Resource.Node." + id)) { kind = id; break; }
		if (kind.empty()) { head("", std::string()); return; }
		const ResourceNodeDef& d = m_NodeDefs.at(kind);
		std::string needs = L.Text("bare hands");
		if (!d.RequiresTag.empty())
		{
			const std::string tool = d.RequiresTag.substr(d.RequiresTag.find_last_of('.') + 1);
			needs = L.Text(tool == "Axe" ? "an axe" : tool == "Pickaxe" ? "a pickaxe" : tool == "Hoe" ? "a hoe" : tool == "Sickle" ? "a sickle" : tool == "Spear" ? "a spear" : "a tool");
		}
		head(d.Item, Lang::Cap(itemName(d.Item)) + " \xC2\xB7 " + needs);
		const int left = ResourceSystem::Amount(*asc);
		edged([&]()
		{
			ImGui::TextUnformatted(L.Text("left: {0} of {1}", left, d.Capacity).c_str());
			const ImVec2 b = ImGui::GetCursorScreenPos();
			const float bw = width - 30.0f, v = d.Capacity > 0 ? std::clamp(static_cast<float>(left) / static_cast<float>(d.Capacity), 0.0f, 1.0f) : 0.0f;
			ImGui::Dummy(ImVec2(bw, lh * 0.6f));
			dl->AddRectFilled(ImVec2(b.x, b.y + 1.0f), ImVec2(b.x + bw, b.y + lh * 0.6f - 1.0f), T::Hide, 2.0f);
			if (v > 0.0f) dl->AddRectFilled(ImVec2(b.x, b.y + 1.0f), ImVec2(b.x + bw * v, b.y + lh * 0.6f - 1.0f), T::Ochre, 2.0f);
			ImGui::TextDisabled("%s", (d.RegenSeconds > 0.0f ? L.Text("regrows {0} every {1} s", d.Regen, static_cast<int>(d.RegenSeconds)) : L.Text("does not regrow")).c_str());
		});
		{
			std::string gives = L.Text("{0} x{1} each time", itemName(d.Item), d.PerGather);
			if (!d.ExtraItem.empty()) gives += ", " + L.Text("and {0} x{1}", itemName(d.ExtraItem), d.ExtraPerGather);
			row(d.Item, L.Text("Gives"), gives, T::Bone, std::string());
		}
		std::string workIcon;
		{
			const std::string& a = d.Ability;
			workIcon = a == "Chop" ? "chop" : a == "Mine" || a == "Dig" ? "mine" : a == "Collect" ? "collect" : "gather";
		}
		row(workIcon, L.Text("Needs"), needs, T::Bone, std::string());
		// what the player can do with it: mark it for the clan (T270), and move it (T333), each only when the clan knows how (T327)
		if (own)
		{
			const ColonyDesignations::Mark* mark = own->Designations().MarkOf(name);
			if (mark)
			{
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(lh, lh));
				DrawMark(dl, "order", at, lh, T::Ochre);
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("Marked to gather (priority {0})", mark->Priority).c_str());
			}
			ImGui::Separator();
			bool any = false;
			// (the next button on the same line only when it fits in the sheet)
			const float right = ImGui::GetCursorScreenPos().x + width - 10.0f;
			auto besideIfFits = [&](const std::string& label)
			{
				const float need = ImGui::CalcTextSize(label.c_str()).x + 2.0f * style.FramePadding.x + ImGui::GetFontSize() + 8.0f;
				if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + need <= right) ImGui::SameLine();
			};
			if (left > 0 && TechVerbs::Allows(*own, "designate", kind))
			{
				any = true;
				if (mark) { if (IconLabelButton("unmark", "state.cannot", L.Text("Remove the mark"), false)) run("game designate " + under + " off"); }
				else
				{
					if (IconLabelButton("designate", workIcon, L.Text("Gather with priority"), false)) run("game designate " + under);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Whoever of the clan can do it goes first to this node").c_str());
					besideIfFits(L.Text("Mark the whole deposit"));
					if (IconLabelButton("designateall", "explore", L.Text("Mark the whole deposit"), false)) run("game designate " + under + " all");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Every one of its kind around it, for the clan").c_str());
				}
			}
			if (const ResourceNodeDef* movable = Transplant::DefOf(*own, name); movable && TechVerbs::Knows(*own, movable->Transplant.Verb))
			{
				const bool placing = m_Colony.Transplant == name;
				if (any) besideIfFits(L.Text("Transplant..."));
				if (IconLabelButton("transplant", "sow", L.Text("Transplant..."), placing)) run(placing ? std::string("colony transplant off") : "colony transplant " + under);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", (placing ? L.Text("Click on the ground where it goes; Esc: leave it") : L.Text("Dig it up and plant it somewhere else: then a click on the ground")).c_str());
			}
		}
	}
}
