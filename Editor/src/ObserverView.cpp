// The observer of the free mode (`observer on|off|<clan>|next`, the "Clans" button over the viewport, View > Observe the clans): the simulation seen
// clan by clan with the game's look (GameTheme) inside the editor's viewport. A card top left picks the clan (the same one the Tribe panel shows,
// m_ActiveClan) and sums it up; the clan book on the right draws the game's own sheets for that clan (GameSheets.cpp: the village, the board, the
// homes, what it knows, its relations) plus its members, its sites and its log, so the game and the observer never tell the same thing two ways.
// Nothing here decides or changes the simulation: it reads the clans, and its buttons run editor commands (camera, observer), so a script can show
// the same. Every word goes through Lang (Lang.h).
// U1 (`observer screen on`): the god's screen, the observer over the whole window with no editor panel (EditorApp::DrawUI draws it like the game's
// screen), with the god's bar on top (DrawGodBar: the clock and its own pause and speed, the season, the weather) and the card and the book under it.
// U5/U6: the book's tabs in four groups (Clan, Person, Chronicle, More), the world's chronicle of every clan with its filters and [go], the news
// of the god's screen, a person's sheet and the chip of whom the camera follows.
#include "EditorApp.h"
#include "GameTheme.h"
#include "StoryKinds.h"
#include "GB/Game/WinterOutlook.h"
#include "GB/Game/WorldAlerts.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

namespace GB::Editor
{
	namespace
	{
		ImU32 ClanColour(const ClanWorld& clans, ClanId id)
		{
			const glm::vec3 c = glm::clamp(clans.ColorOf(id), glm::vec3(0.0f), glm::vec3(1.0f));
			return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f), 255);
		}

		// The clan's colour as a small square before its name.
		void Swatch(ImU32 colour)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float s = ImGui::GetTextLineHeight();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + 1.0f), ImVec2(at.x + s, at.y + s + 1.0f), colour, 3.0f);
			ImGui::GetWindowDrawList()->AddRect(ImVec2(at.x, at.y + 1.0f), ImVec2(at.x + s, at.y + s + 1.0f), GameTheme::Outline, 3.0f);
			ImGui::Dummy(ImVec2(s, s));
			ImGui::SameLine();
		}

		// The lowest need the clan has not covered (Tribe::MaslowTier), in the player's words: long on the card and the sheet, short in the list.
		const char* TierText(const std::string& tier)
		{
			return tier == "survival" ? "food (survival)" : tier == "warmth" ? "warmth (fire and roof)" : tier == "safety" ? "safety" : tier == "growth" ? "all covered: growing" : tier.c_str();
		}
		const char* TierShort(const std::string& tier)
		{
			return tier == "survival" ? "lacks food" : tier == "warmth" ? "lacks warmth" : tier == "safety" ? "lacks safety" : tier == "growth" ? "growing" : tier.c_str();
		}
		ImU32 TierColour(const std::string& tier) { return tier == "survival" ? GameTheme::Bad : tier == "growth" ? GameTheme::Good : GameTheme::Warn; }

		// T219 (U3): the winter's outlook is GB's (GB/Game/WinterOutlook.h, the one definition the list, the sheet, the alerts and GBSim read): the
		// days until the spring against the days of food the clan has in hand today. Short 0 = covered.
		bool IsConditionKind(const std::string& kind) { return kind == "hunger" || kind == "winter" || kind == "extinct"; }   // (T220: the alerts that are a state, not an event)
		using WinterGuess = WinterOutlook;
		WinterGuess GuessWinter(const Tribe& t) { return ForecastWinter(t); }
		// covered: good; short in the winter or 3 days before it: bad; short with the winter the next season: warn; short with the winter further off
		// (in the spring every clan is short of a whole summer and autumn): only a note
		ImU32 WinterColour(const WinterGuess& g)
		{
			if (g.Short <= 0.0f) return GameTheme::Good;
			return (g.In || g.ToWinter <= 3.0f) ? GameTheme::Bad : g.ToWinter <= g.SeasonDays ? GameTheme::Warn : GameTheme::BoneDim;
		}

		// Small drawn marks (the font has no arrows nor snowflakes): a snowflake of three strokes, and the trend's triangle (up, down, or a dash when
		// it barely moved). Each goes next to a number or a word, so the colour is never the only sign.
		void Flake(ImU32 colour)
		{
			const float s = ImGui::GetTextLineHeight();
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(s, s));
			const ImVec2 c(at.x + s * 0.5f, at.y + s * 0.5f + 1.0f);
			const float r = s * 0.45f;
			ImDrawList* dl = ImGui::GetWindowDrawList();
			for (int i = 0; i < 3; ++i)
			{
				const float a = 1.5707963f + static_cast<float>(i) * 1.0471976f;
				dl->AddLine(ImVec2(c.x - std::cos(a) * r, c.y - std::sin(a) * r), ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), colour, 1.6f);
			}
		}
		void Trend(float delta)
		{
			const float s = ImGui::GetTextLineHeight();
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(s * 0.8f, s));
			const float x0 = at.x + 1.0f, x1 = at.x + s * 0.8f - 1.0f, xm = (x0 + x1) * 0.5f, y0 = at.y + 3.0f, y1 = at.y + s - 1.0f;
			ImDrawList* dl = ImGui::GetWindowDrawList();
			if (delta >= 0.005f) dl->AddTriangleFilled(ImVec2(xm, y0), ImVec2(x1, y1), ImVec2(x0, y1), GameTheme::Good);
			else if (delta <= -0.005f) dl->AddTriangleFilled(ImVec2(x0, y0), ImVec2(x1, y0), ImVec2(xm, y1), GameTheme::Bad);
			else dl->AddRectFilled(ImVec2(x0, (y0 + y1) * 0.5f - 1.0f), ImVec2(x1, (y0 + y1) * 0.5f + 1.0f), GameTheme::BoneDim);
		}

		// A small bar (the list's and the sheet's quality of life), the fill in `colour`.
		void SmallBar(float v01, float width, ImU32 colour)
		{
			namespace T = GameTheme;
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float h = ImGui::GetTextLineHeight();
			ImGui::Dummy(ImVec2(width, h));
			const float y0 = at.y + (h - 10.0f) * 0.5f + 1.0f;
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + width, y0 + 10.0f), T::HideRaised, 3.0f);
			if (v01 > 0.0f) dl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + width * std::clamp(v01, 0.0f, 1.0f), y0 + 10.0f), colour, 3.0f);
			dl->AddRect(ImVec2(at.x, y0), ImVec2(at.x + width, y0 + 10.0f), T::HideLine, 3.0f);
		}

		// The words of the parts of the quality of life (SocietyMetrics.h, QolParts()).
		const char* PartText(const std::string& part)
		{
			return part == "needs" ? "Needs met" : part == "shelter" ? "Shelter" : part == "food" ? "Food" : part == "tools" ? "Tools" : part == "health" ? "Health"
				: part == "longevity" ? "Longevity" : part == "techs" ? "Techniques" : part.c_str();
		}
	}

	// ------------------------------------------------------------------ U2: the quality of life of the clans, as the editor measures it
	const std::vector<SocietyMeasure>& EditorApp::Societies(bool force)
	{
		// (once per frame at most, so that a command and the drawing of the same frame read the same measure; the clock moving a few seconds, the
		// clans changing or `force` measure again)
		const double now = m_Tribe.WorldSeconds();
		const int frame = ImGui::GetFrameCount();
		const bool stale = m_SocietiesAt < 0.0 || now < m_SocietiesAt || now - m_SocietiesAt >= 5.0 || m_SocietiesClans != m_Clans.Count();
		if (force || (stale && frame != m_SocietiesFrame))
		{
			m_Societies = MeasureSocieties(m_Clans);
			m_SocietiesAt = now;
			m_SocietiesClans = m_Clans.Count();
			m_SocietiesFrame = frame;
		}
		return m_Societies;
	}

	const SocietyMeasure* EditorApp::MeasureOf(int clan)
	{
		for (const SocietyMeasure& m : Societies()) if (m.Clan == clan) return &m;
		return nullptr;
	}

	void EditorApp::TrackQol()
	{
		if (m_Clans.Count() == 0) return;
		const int day = GameDay();
		if (day < m_QolDay) m_QolDays.clear();   // (a world made anew or loaded: its clock went back)
		m_QolDay = day;
		// a sample a day of each clan with people: the first measure of the day once it has them (an empty clan, as a new world's first tribe
		// before its village, would read 0 and fake a rise; a clan founded today gets its first sample today). Nothing to measure once every
		// living clan has today's.
		bool missing = false;
		for (const auto& c : m_Clans.All())
		{
			if (!c->Folk || std::none_of(c->Folk->Members().begin(), c->Folk->Members().end(), [](const TribeMember& m) { return !m.Dead; })) continue;
			const auto it = m_QolDays.find(c->Id);
			if (it == m_QolDays.end() || !it->second.count(day)) { missing = true; break; }
		}
		if (!missing) return;
		for (const SocietyMeasure& m : Societies())
		{
			if (m.Alive <= 0) continue;
			std::map<int, float>& days = m_QolDays[m.Clan];
			days.emplace(day, m.Qol);
			while (days.size() > 8) days.erase(days.begin());
		}
	}

	EditorApp::QolTrend EditorApp::TrendOf(int clan, float qol) const
	{
		QolTrend r;
		const auto it = m_QolDays.find(clan);
		if (it == m_QolDays.end()) return r;
		const std::map<int, float>& days = it->second;
		auto at = [&](int d) { const auto f = days.find(d); return f == days.end() ? -1.0f : f->second; };
		r.Ago1 = at(m_QolDay - 1);
		r.Ago3 = at(m_QolDay - 3);
		int base = r.Ago3 >= 0.0f ? m_QolDay - 3 : -1;   // against 3 days ago, else the oldest earlier day kept
		if (base < 0)
			for (const auto& [d, q] : days) if (d < m_QolDay) { base = d; break; }
		if (base < 0) return r;
		r.Known = true;
		r.Days = m_QolDay - base;
		r.Delta = qol - days.at(base);
		return r;
	}

	std::string EditorApp::ObserverClansText()
	{
		std::string out;
		char line[256];
		for (const SocietyMeasure& m : Societies(true))
		{
			const Clan* c = m_Clans.Find(m.Clan);
			if (!c || !c->Folk) continue;
			const Tribe& t = *c->Folk;
			const QolTrend trend = TrendOf(m.Clan, m.Qol);
			const WinterGuess w = GuessWinter(t);
			char trendText[48] = "none yet", winter[64];
			if (trend.Known) std::snprintf(trendText, sizeof trendText, "%+.2f over %d day(s)", static_cast<double>(trend.Delta), trend.Days);
			if (w.Short <= 0.0f) std::snprintf(winter, sizeof winter, "covered");
			else std::snprintf(winter, sizeof winter, "short %.0f days", static_cast<double>(w.Short));
			std::snprintf(line, sizeof line, "clan %d %s: qol %.2f, trend %s, food %.1f days, state %s, winter %s (in %.0f days, until spring %.0f)", m.Clan, m.Name.c_str(),
				static_cast<double>(m.Qol), trendText, static_cast<double>(t.FoodDays()), t.MaslowTier().c_str(), winter, static_cast<double>(w.ToWinter), static_cast<double>(w.UntilSpring));
			out += (out.empty() ? "" : "\n") + std::string(line) + "\n" + SocietyLine(m);
		}
		return out.empty() ? std::string("no clans measured") : out;
	}

	// [go] of the alerts and the chronicle: the god's own camera (the editor orbit, free or top) slides its look point to the place and keeps
	// its heading, tilt and zoom, so Q/E and the mouse still turn it afterwards. Only when a game camera is already on (the player's view)
	// does the game overview take the place, as before.
	CommandResult EditorApp::ObserverLookAt(const glm::vec2& where)
	{
		if (m_GameView.Active())
		{
			char x[32], z[32];
			std::snprintf(x, sizeof x, "%.2f", static_cast<double>(where.x));
			std::snprintf(z, sizeof z, "%.2f", static_cast<double>(where.y));
			return m_GameView.Command({ "overview", x, z, "45" }, m_Actors, m_Player, m_World, m_Camera);
		}
		if (m_CamMode == CamMode::Follow) SetCamMode(CamMode::Free);
		const float y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(where.x, where.y) : m_Orbit.Target.y;
		m_Orbit.Target = { where.x, y, where.y };
		m_Orbit.Distance = std::clamp(m_Orbit.Distance, 20.0f, 60.0f);
		char buf[96];
		std::snprintf(buf, sizeof buf, "camera over (%.1f, %.1f)", static_cast<double>(where.x), static_cast<double>(where.y));
		return { true, buf };
	}

	CommandResult EditorApp::ObserverCommand(const std::vector<std::string>& a)
	{
		static const char* kSheets = "clan|person|members|chronicle|village|board|homes|sites|knowledge|relations|log";
		// the observer going off takes its screen and its pause with it (a frozen editor with nothing that says so)
		auto off = [&]() { m_Observer.On = m_Observer.Screen = m_Observer.Paused = false; return CommandResult{ true, "observer off" }; };
		if (a.empty() || a[0] == "on" || a[0] == "toggle")
		{
			if (!a.empty() && a[0] == "toggle" && m_Observer.On) return off();
			m_Observer.On = true;
			return { true, "observer on: the clan " + std::to_string(m_ActiveClan) + " (observer <clan> | next | off)" };
		}
		if (a[0] == "off") return off();
		if (a[0] == "tree" && a.size() >= 2) { m_Observer.Tree = m_Observer.On = true; m_Observer.TreePick = a[1]; return { true, "tree of knowledge open on " + a[1] }; }   // T242: open on a technology
		if (a[0] == "tree") { m_Observer.Tree = !m_Observer.Tree; m_Observer.On = true; return { true, m_Observer.Tree ? "tree of knowledge open" : "tree of knowledge closed" }; }
		if (a[0] == "fold") { m_Observer.Folded = !m_Observer.Folded; m_Observer.On = true; return { true, m_Observer.Folded ? "book folded" : "book open" }; }
		if (a[0] == "status")   // (the first line as it was; the clock under it and, on the god's screen, what its panels cover of the world)
		{
			std::string s = std::string(m_Observer.On ? "observer on" : "observer off") + ", clan " + std::to_string(m_ActiveClan) + ", sheet " + m_Observer.Tab
				+ (m_Observer.Screen ? ", screen on" : "") + "\n" + ObserverTimeLine();
			const ObserverState::Box& v = m_Observer.ViewBox;
			if (m_Observer.Screen && v.X1 > v.X0 && v.Y1 > v.Y0)
			{
				const std::pair<const char*, const ObserverState::Box*> boxes[] = { { "bar", &m_Observer.BarBox }, { "card", &m_Observer.CardBox }, { "clans", &m_Observer.ClansBox },
					{ "book", &m_Observer.BookBox }, { "follow", &m_Observer.FollowBox }, { "news", &m_Observer.NewsBox }, { "selection", &m_Observer.SelectionBox }, { "alerts", &m_Observer.AlertsBox } };
				auto area = [](const ObserverState::Box& b) { return std::max(0.0f, b.X1 - b.X0) * std::max(0.0f, b.Y1 - b.Y0); };
				float covered = 0.0f;
				std::string parts, overlaps;
				char text[96];
				for (const auto& [name, b] : boxes)
				{
					if (area(*b) <= 0.0f) continue;
					covered += area(*b);
					std::snprintf(text, sizeof text, "%s%s %.0f,%.0f-%.0f,%.0f", parts.empty() ? "" : ", ", name, static_cast<double>(b->X0 - v.X0), static_cast<double>(b->Y0 - v.Y0),
						static_cast<double>(b->X1 - v.X0), static_cast<double>(b->Y1 - v.Y0));
					parts += text;
				}
				for (size_t i = 0; i < std::size(boxes); ++i)
					for (size_t j = i + 1; j < std::size(boxes); ++j)
					{
						const ObserverState::Box& p = *boxes[i].second;
						const ObserverState::Box& q = *boxes[j].second;
						const ObserverState::Box both{ std::max(p.X0, q.X0), std::max(p.Y0, q.Y0), std::min(p.X1, q.X1), std::min(p.Y1, q.Y1) };
						if (area(p) > 0.0f && area(q) > 0.0f && area(both) > 0.0f) overlaps += (overlaps.empty() ? "" : ", ") + std::string(boxes[i].first) + "/" + boxes[j].first;
					}
				std::snprintf(text, sizeof text, "layout: %.0fx%.0f, world %.0f %% uncovered; ", static_cast<double>(v.X1 - v.X0), static_cast<double>(v.Y1 - v.Y0),
					100.0 * (1.0 - static_cast<double>(covered / area(v))));
				s += std::string("\n") + text + parts + "; overlaps " + (overlaps.empty() ? "none" : overlaps);
			}
			char line[200];
			if (m_Observer.TabsRoom > 0.0f)   // (U5/U6: the group tabs against the book's width: they never scroll)
			{
				std::snprintf(line, sizeof line, "\ntabs: 4 groups want %.0f of %.0f px (%s); more: 7 sheets in %d row(s)", static_cast<double>(m_Observer.TabsNeed), static_cast<double>(m_Observer.TabsRoom),
					m_Observer.TabsNeed <= m_Observer.TabsRoom ? "fit" : "narrowed", m_Observer.MoreRows);
				s += line;
			}
			const glm::vec3 focus = m_GameView.Focus();
			if (const std::string who = m_GameView.Following(); !who.empty())   // (U6: where the followed member is in the simulation and how far the camera looks from it)
			{
				const Tribe* clan = ClanOfMember(who);
				const TribeMember* m = clan ? clan->FindMember(who) : nullptr;
				if (m && !m->Dead)   // (the simulation's own place of the member, Tribe::Position: not the camera's subject, which may be its actor's feet)
				{
					const glm::vec2 at = clan->Position(*m);
					std::snprintf(line, sizeof line, "\nfollow: %s at (%.1f, %.1f), camera focus (%.1f, %.1f), %.1f m away", who.c_str(), static_cast<double>(at.x), static_cast<double>(at.y),
						static_cast<double>(focus.x), static_cast<double>(focus.z), static_cast<double>(glm::distance(at, glm::vec2(focus.x, focus.z))));
					s += line;
				}
				else s += "\nfollow: " + who + " (not a member)";
			}
			if (const HistoryEvent* e = m_Observer.GoEvent > 0 ? m_Clans.Past().Find(m_Observer.GoEvent) : nullptr; e && e->HasWhere && m_GameView.Active() && m_GameView.Mode() == GameCameraMode::Overview)   // (U5: the last [go], while its view lasts)
			{
				std::snprintf(line, sizeof line, "\ngo: event #%d at (%.1f, %.1f), camera focus (%.1f, %.1f), %.1f m away", e->Id, static_cast<double>(e->Where.x), static_cast<double>(e->Where.y),
					static_cast<double>(focus.x), static_cast<double>(focus.z), static_cast<double>(glm::distance(e->Where, glm::vec2(focus.x, focus.z))));
				s += line;
			}
			return { true, s };
		}
		if (a[0] == "sheet" && a.size() > 1) { m_Observer.Ask = a[1]; m_Observer.On = true; return { true, "sheet " + a[1] }; }
		// U1: the god's screen, its pause and its speed (the free mode's clock: a game keeps `game pause|speed`)
		if (a[0] == "screen")
		{
			const std::string to = a.size() > 1 ? a[1] : "toggle";
			if (to != "on" && to != "off" && to != "toggle") return { false, "observer screen on|off|toggle" };
			const bool on = to == "on" || (to == "toggle" && !m_Observer.Screen);
			if (on && m_Game.Active) return { false, "not while playing: the game has its own screen (play off first)" };
			if (on && !m_Observer.Screen) m_Observer.Folded = true;   // (the book starts folded: the world first)
			m_Observer.Screen = on;
			if (on) m_Observer.On = true;
			return { true, on ? "observer screen on (Esc or observer screen off: back to the editor)" : "observer screen off" };
		}
		if (a[0] == "pause" || a[0] == "resume")
		{
			if (m_Game.Active) return { false, "playing: the game's clock is `game pause`" };
			m_Observer.Paused = a[0] == "pause" ? (a.size() > 1 ? a[1] == "on" : !m_Observer.Paused) : false;
			m_Observer.On = true;
			return { true, m_Observer.Paused ? "observer paused" : "observer running" };
		}
		if (a[0] == "speed" || a[0] == "faster" || a[0] == "slower")
		{
			if (m_Game.Active) return { false, "playing: the game's clock is `game speed`" };
			static const int steps[] = { 1, 4, 8, 20 };   // (the god's bar's buttons)
			int i = 0;
			while (i < 3 && steps[i] < m_TimeScale) ++i;
			if (a[0] == "speed")
			{
				if (a.size() < 2 || std::atoi(a[1].c_str()) < 1) return { false, "observer speed <1-20>" };
				m_TimeScale = std::clamp(std::atoi(a[1].c_str()), 1, 20);
			}
			else m_TimeScale = steps[a[0] == "faster" ? std::min(3, i + (steps[i] == m_TimeScale ? 1 : 0)) : std::max(0, i - 1)];
			return { true, "speed x" + std::to_string(m_TimeScale) };
		}
		// T220 (U4): the god's alerts as text (what the stack draws, in its order), dismissing one or going to it (by its number in that list or its
		// key, as the stack's buttons do), and whether a new critical one may pause the god's game (a script's switch over player.json's alertPause)
		if (a[0] == "alerts" && a.size() > 1 && a[1] == "fold")
		{
			m_Observer.AlertsFolded = a.size() > 2 ? a[2] == "on" : !m_Observer.AlertsFolded;
			return { true, m_Observer.AlertsFolded ? "alerts folded (the counter of the god's bar shows them again)" : "alerts shown" };
		}
		if (a[0] == "alerts") return { true, ObserverAlertsText() };
		if (a[0] == "alert")
		{
			const std::string usage = "observer alert dismiss <n|key> | go <n|key> | pause on|off";
			if (a.size() > 2 && a[1] == "pause")
			{
				if (a[2] != "on" && a[2] != "off") return { false, usage };
				m_AlertPauseOff = a[2] == "off";
				return { true, m_AlertPauseOff ? "alerts never pause" : "the critical alerts of controls.hud.alertPause pause the god's game" };
			}
			if (a.size() < 3 || (a[1] != "dismiss" && a[1] != "go")) return { false, usage };
			const std::vector<const Alert*> shown = VisibleAlerts();
			const Alert* pick = nullptr;
			int n = 0;
			if (!a[2].empty() && std::all_of(a[2].begin(), a[2].end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }))
			{
				n = std::atoi(a[2].c_str());
				if (n >= 1 && n <= static_cast<int>(shown.size())) pick = shown[static_cast<size_t>(n - 1)];
			}
			else
				for (size_t i = 0; i < shown.size(); ++i)
					if (shown[i]->Key == a[2]) { pick = shown[i]; n = static_cast<int>(i) + 1; }
			if (!pick) return { false, "no alert " + a[2] + " (" + std::to_string(shown.size()) + " shown: observer alerts)" };
			const std::string text = pick->Text, key = pick->Key;
			if (a[1] == "dismiss")
			{
				m_Alerts.DismissKey(key);
				return { true, "alert " + std::to_string(n) + " dismissed: " + text };
			}
			if (!pick->HasWhere) return { false, "the alert " + std::to_string(n) + " has no place" };
			const CommandResult r = ObserverLookAt(pick->Where);
			return { r.Ok, "alert " + std::to_string(n) + " (" + text + "): " + r.Message };
		}
		if (m_Clans.Count() == 0) return { false, "no clans (tribe build, clan new)" };
		// U2: what the god's list shows of each clan, and its SocietyLine (the measure the list draws in this same frame)
		if (a[0] == "clans") return { true, ObserverClansText() };
		if (a[0] == "tip")   // (a script has no mouse: holds the quality of life tooltip of a clan's row open, for a capture)
		{
			if (a.size() < 2) return { false, "observer tip <clan id or name>|stage|winter <clan>|off" };
			if (a[1] == "off") { m_Observer.Tip = -1; m_Observer.StageTip = false; m_Observer.WinterTip = -1; return { true, "tip off" }; }
			if (a[1] == "stage") { m_Observer.StageTip = true; return { true, "tip on the stage" }; }   // D48: the stage's goals of the god's bar
			if (a[1] == "winter" && a.size() > 2)   // the winter's column of a clan's row: its food against the winter
			{
				const Clan* c = m_Clans.Find(a[2]);
				if (!c) return { false, "no clan " + a[2] };
				m_Observer.WinterTip = c->Id;
				return { true, "tip on the winter of the clan " + std::to_string(c->Id) };
			}
			const Clan* c = m_Clans.Find(a[1]);
			if (!c) return { false, "no clan " + a[1] };
			m_Observer.Tip = c->Id;
			return { true, "tip on the clan " + std::to_string(c->Id) };
		}
		// U5: the world's chronicle: sets the sheet's filters (the clan, the groups, the least weight) and lists what it shows
		if (a[0] == "chronicle")
		{
			size_t i = 1;
			if (i < a.size() && a[i] != "weight" && !FindStoryKind(a[i]))
			{
				if (a[i] == "all") m_Observer.ChronicleClan = -1;
				else if (const Clan* c = m_Clans.Find(a[i])) m_Observer.ChronicleClan = c->Id;
				else return { false, "observer chronicle [all|<clan>] [birth|death|build|know|clans|fight...] [weight <1-5>]: no clan " + a[i] };
				++i;
			}
			std::set<std::string> shown;
			for (; i < a.size(); ++i)
			{
				if (a[i] == "weight" && i + 1 < a.size()) { m_Observer.ChronicleWeight = std::clamp(std::atoi(a[++i].c_str()), 1, 5); continue; }
				if (!FindStoryKind(a[i])) return { false, "observer chronicle: no kind " + a[i] + " (birth|death|build|know|clans|fight)" };
				shown.insert(a[i]);
			}
			if (!shown.empty())
			{
				m_Observer.ChronicleHidden.clear();
				for (int k = 0; k < StoryKindCount; ++k) if (!shown.count(StoryKinds()[k].Id)) m_Observer.ChronicleHidden.insert(StoryKinds()[k].Id);
			}
			m_Observer.On = true;
			m_Observer.Folded = false;
			m_Observer.Ask = "chronicle";
			return { true, ObserverChronicleText() };
		}
		// U5: the camera to where an event happened (the chronicle's and the news' [go]): the clan's view from above over its place
		if (a[0] == "go")
		{
			const HistoryEvent* e = nullptr;
			if (a.size() > 1 && a[1] == "newest")   // (for a script: the newest event the chronicle's filters show that has a place)
			{
				const std::vector<HistoryEvent>& all = m_Clans.Past().Events();
				for (auto it = all.rbegin(); it != all.rend() && !e; ++it) if (it->HasWhere && ChronicleShows(*it)) e = &*it;
			}
			else if (a.size() > 1) e = m_Clans.Past().Find(std::atoi(a[1].c_str()));
			if (!e) return { false, "observer go <event id>|newest (observer chronicle lists them)" };
			if (!e->HasWhere) return { false, "the event #" + std::to_string(e->Id) + " has no place" };
			m_Observer.GoEvent = e->Id;
			const CommandResult r = ObserverLookAt(e->Where);
			return { r.Ok, "event #" + std::to_string(e->Id) + ": " + r.Message };
		}
		// U6: the game camera follows a member (the person's sheet's [Follow]: the one on the sheet by default), or lets it go
		if (a[0] == "follow")
		{
			const std::string name = a.size() > 1 ? a[1] : m_Observer.Person;
			if (name == "off") { m_GameView.Command({ "off" }, m_Actors, m_Player, m_World, m_Camera); return { true, "nobody followed" }; }
			if (name.empty()) return { false, "observer follow <name>|off (or open a person's sheet first)" };
			const CommandResult r = m_GameView.Command({ "follow", name }, m_Actors, m_Player, m_World, m_Camera);
			return { r.Ok, r.Message };
		}
		// U6: a person's sheet in the book (any clan's member: its clan becomes the observed one), and its fields as text
		if (a[0] == "person")
		{
			if (a.size() < 2) return { false, "observer person <name>|off" };
			if (a[1] == "off") { m_Observer.Person.clear(); m_Observer.Ask = "members"; return { true, "person sheet closed" }; }
			std::string name = a[1];
			if (name == "sample")   // (for a script: a living member of the observed clan that fills the sheet: 3 memories, then known parents, then more memories)
			{
				const Clan* c = m_Clans.Find(m_ActiveClan);
				if (!c || !c->Folk) return { false, "no clan observed" };
				int best = -1;
				for (const TribeMember& m : c->Folk->Members())
				{
					if (m.Dead) continue;
					const Person* p = c->Folk->People().Find(m.Name);
					const int memories = static_cast<int>(m_Clans.Past().Recall(m.Name, 100).size());
					const int score = (memories >= 3 ? 2000 : 0) + (p && !p->Mother.empty() ? 1000 : 0) + memories;
					if (score > best) { best = score; name = m.Name; }
				}
				if (best < 0) return { false, "nobody alive in the clan " + std::to_string(m_ActiveClan) };
			}
			if (name == "skeptic")   // (for a script: the first skeptic of the world, T241)
			{
				if (m_Director.Skeptics().empty()) return { false, "nobody laughs at the god's dreams" };
				name = m_Director.Skeptics().front().Person;
			}
			const Tribe* t = ClanOfMember(name);
			if (!t) return { false, "nobody called " + name + " in any clan" };
			for (const auto& c : m_Clans.All()) if (c->Folk == t) m_ActiveClan = c->Id;
			m_Observer.Person = name;
			m_Observer.On = true;
			m_Observer.Folded = false;
			m_Observer.Ask = "person";
			return { true, ObserverPersonText(name) };
		}
		if (a[0] == "next" || a[0] == "prev")
		{
			const auto& all = m_Clans.All();
			size_t i = 0;
			while (i < all.size() && all[i]->Id != m_ActiveClan) ++i;
			i = (i + (a[0] == "next" ? 1 : all.size() - 1)) % all.size();
			m_ActiveClan = all[i]->Id;
		}
		else if (const Clan* c = m_Clans.Find(a[0])) m_ActiveClan = c->Id;
		else return { false, std::string("observer on|off|toggle|status|next|prev|sheet <") + kSheets + ">|screen on|off|pause [on|off]|resume|speed <1-20>|faster|slower|clans|tip <clan>|off|chronicle [all|<clan>] [kind...] [weight <n>]|go <event>|newest|person <name>|sample|off|follow [name]|off|alerts|alert dismiss|go <n>|alert pause on|off|<clan id or name>" };
		m_Observer.On = true;
		const Clan* c = m_Clans.Find(m_ActiveClan);
		return { true, "observing the clan " + std::to_string(m_ActiveClan) + (c ? " " + c->Name : std::string()) };
	}

	void EditorApp::DrawObserver(float ox, float oy, float w, float h)
	{
		m_Observer.BookWidth = 0.0f;
		m_Observer.CardBottom = 0.0f;
		m_Observer.BarBottom = 0.0f;
		m_Observer.BarBox = m_Observer.CardBox = m_Observer.BookBox = m_Observer.ClansBox = {};
		m_Observer.NewsBox = m_Observer.FollowBox = m_Observer.SelectionBox = m_Observer.AlertsBox = {};
		m_Observer.ViewBox = { ox, oy, ox + w, oy + h };
		if (!m_Observer.On) return;
		namespace T = GameTheme;
		const T::Scope theme;
		// U1: on the god's screen the god's bar goes over the whole width at the top and the rest under it; in the editor's viewport the card goes
		// under the camera buttons and the book from the top
		const bool god = m_Observer.Screen && !m_Game.Active;
		// U6: on the god's screen a character picked in the world opens its sheet in the book (its card is not drawn there)
		if (m_FreeSelected != m_Observer.SelectionSeen)
		{
			m_Observer.SelectionSeen = m_FreeSelected;
			if (god && !m_FreeSelected.empty() && m_FreeSelected != m_Observer.Person && AnyMember(m_FreeSelected)) m_UiCommands.push_back("observer person " + m_FreeSelected);
		}
		if (god) DrawGodBar(ox, oy, w);
		const float cardTop = god ? m_Observer.BarBottom + T::Gap : oy + 52.0f, bookTop = god ? m_Observer.BarBottom + T::Gap : oy + 8.0f;
		if (m_Clans.Count() == 0) return;
		if (!m_Clans.Find(m_ActiveClan)) m_ActiveClan = m_Clans.All().front()->Id;
		const Clan& clan = *m_Clans.Find(m_ActiveClan);
		if (!clan.Folk) return;
		const Tribe& t = *clan.Folk;
		if (m_Observer.Tree)   // the tree of knowledge alone (the card and the book would come over it)
		{
			const float treeTop = god ? m_Observer.BarBottom : oy;
			DrawTechTree(t, false, ox, treeTop, w, std::max(oy + h - treeTop, 160.0f), m_Observer.Tree, m_Observer.TreePick);
			return;
		}
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
		auto swatch = [&](ImU32 colour)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float s = ImGui::GetTextLineHeight();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + 1.0f), ImVec2(at.x + s, at.y + s + 1.0f), colour, 3.0f);
			ImGui::GetWindowDrawList()->AddRect(ImVec2(at.x, at.y + 1.0f), ImVec2(at.x + s, at.y + s + 1.0f), T::Outline, 3.0f);
			ImGui::Dummy(ImVec2(s, s));
			ImGui::SameLine();
		};
		auto meter = [&](const std::string& label, float v01, ImU32 colour, const std::string& value, const std::string& tip)
		{
			ImGui::PushID(label.c_str());
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", label.c_str());
			ImGui::SameLine(80.0f);
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float bw = 110.0f, y0 = at.y + 2.0f;
			ImGui::InvisibleButton("bar", ImVec2(bw, 18.0f));
			const bool hovered = ImGui::IsItemHovered();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + bw, y0 + T::BarH), T::HideRaised, 3.0f);
			dl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + bw * std::clamp(v01, 0.0f, 1.0f), y0 + T::BarH), colour, 3.0f);
			dl->AddRect(ImVec2(at.x, y0), ImVec2(at.x + bw, y0 + T::BarH), T::HideLine, 3.0f);
			ImGui::SameLine();
			ImGui::TextUnformatted(value.c_str());
			if (hovered || ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
			ImGui::PopID();
		};

		// ---- top left: on the god's screen the list of every clan (U2: its quality of life, food, state and winter); in the editor's viewport, under
		// the camera buttons, the clans to pick from and the picked one in a few lines
		if (god) DrawClanList(ox, cardTop);
		else ImGui::SetNextWindowPos(ImVec2(ox + T::Margin, cardTop));
		if (!god && ImGui::Begin("Clan##observer", nullptr, fixed))
		{
			for (const auto& c : m_Clans.All())
			{
				int alive = 0;
				for (const TribeMember& m : c->Folk->Members()) alive += m.Dead ? 0 : 1;
				ImGui::PushID(c->Id);
				swatch(ClanColour(m_Clans, c->Id));
				const bool on = c->Id == m_ActiveClan;
				if (on) ImGui::PushStyleColor(ImGuiCol_Button, T::HideActive);
				if (ImGui::Button((L.Name("clan", c->Name, c->Name) + " " + std::to_string(alive) + "###pick").c_str())) run("observer " + std::to_string(c->Id));
				if (on) ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Observe the {0}: {1} alive", L.Name("clan", c->Name, c->Name), alive).c_str());
				ImGui::PopID();
				ImGui::SameLine();
			}
			ImGui::NewLine();
			ImGui::Separator();
			int alive = 0, dead = 0;
			for (const TribeMember& m : t.Members()) (m.Dead ? dead : alive) += 1;
			swatch(ClanColour(m_Clans, clan.Id));
			ImGui::TextUnformatted(L.Name("clan", clan.Name, clan.Name).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", (dead ? L.Text("{0} alive, {1} dead", alive, dead) : L.Text("{0} alive", alive)).c_str());
			const float food = 1.0f - std::clamp(t.FoodShortage(), 0.0f, 1.0f);
			meter(L.Text("Food"), food, T::Level(food), L.Text("{0} days", L.Num(t.FoodDays(), 1)), L.Text("Days of food the clan has in hand (store and bags); the bar is how far it is from the days it wants"));
			const std::string tier = t.MaslowTier();
			ImGui::TextDisabled("%s", L.Text("State").c_str());
			ImGui::SameLine(80.0f);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(TierColour(tier)), "%s", L.Text(TierText(tier)).c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The lowest need the clan has not covered: food, then warmth (fire and roof), then safety; when all are covered it grows").c_str());
			ImGui::TextDisabled("%s", L.Text("Fire").c_str());
			ImGui::SameLine(80.0f);
			if (!t.HasFire()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("no campfire yet").c_str());
			else if (t.FireBurning()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", L.Text("lit").c_str());
			else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", L.Text("out (no wood)").c_str());
			if (m_Observer.Paused && !god)   // (the god's screen says it in its bar)
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("Time stopped (observer pause)").c_str());
			const glm::vec2 camp = t.Config.Economy.Camp;
			if (ImGui::Button((L.Text("Go to the camp") + "###camp").c_str())) run("camera 0 45 45 " + std::to_string(camp.x) + " ground " + std::to_string(camp.y));
			ImGui::SameLine();
			if (ImGui::Button("<###prev")) run("observer prev");
			ImGui::SameLine();
			if (ImGui::Button(">###next")) run("observer next");
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Tree of knowledge") + "###tree").c_str())) run("observer tree");
			if (ImGui::Button((L.Text(m_Observer.Folded ? "Unfold the book" : "Fold the book") + "###fold").c_str())) run("observer fold");
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Close") + "###close").c_str())) run("observer off");
			m_Observer.CardBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
			m_Observer.CardBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, m_Observer.CardBottom };
		}
		if (!god) ImGui::End();

		// ---- right: the clan book of the observed clan, a tab per sheet (the game's own sheets for this clan, without the player's buttons)
		// (on the god's screen at most a third of the width, so that the world keeps most of it: MODO_DIOS_UX 3.0)
		const float bw = god ? std::clamp(w * 0.32f, 300.0f, 520.0f) : std::clamp(w * 0.42f, 320.0f, 560.0f);
		ImGui::SetNextWindowPos(ImVec2(ox + w - T::Margin, bookTop), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(bw, m_Observer.Folded ? 64.0f : std::max(oy + h - bookTop - T::Margin, 160.0f)), ImGuiCond_Always);
		const std::string title = L.Name("clan", clan.Name, clan.Name) + "###observerbook";
		const bool bookOpen = ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
		m_Observer.BookBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		// U5/U6: four group tabs, so that none scrolls even in the 1280 wide window (H15): the clan, a person (the members until one is picked),
		// the world's chronicle and More, whose sheets are a row of buttons that wraps. A sheet asked for by a command selects its group's tab.
		static const char* kGroups[][2] = { { "clan", "Clan" }, { "person", "Person" }, { "chronicle", "Chronicle" }, { "more", "More" } };
		static const char* kMoreSheets[][2] = { { "village", "Village" }, { "board", "Job board" }, { "homes", "Homes" }, { "sites", "Sites" }, { "knowledge", "Knowledge" },
			{ "relations", "Clans" }, { "log", "Log" } };
		auto groupOf = [](const std::string& sheet) -> std::string
		{
			if (sheet == "clan" || sheet == "chronicle") return sheet;
			if (sheet == "members" || sheet == "person") return "person";
			return "more";
		};
		if (!m_Observer.Ask.empty())
		{
			if (m_Observer.Ask == "members") m_Observer.Person.clear();
			if (groupOf(m_Observer.Ask) == "more") m_Observer.More = m_Observer.Ask;
		}
		if (bookOpen)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			float need = 0.0f;
			for (const auto& g : kGroups) need += ImGui::CalcTextSize(L.Text(g[1]).c_str()).x + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x;
			m_Observer.TabsNeed = need;
			m_Observer.TabsRoom = ImGui::GetContentRegionAvail().x;
		}
		if (bookOpen && ImGui::BeginTabBar("sheets", ImGuiTabBarFlags_FittingPolicyResizeDown))   // (never scrolled: narrower tabs before that)
		{
			for (const auto& group : kGroups)
			{
				const std::string gid = group[0];
				const ImGuiTabItemFlags flags = !m_Observer.Ask.empty() && groupOf(m_Observer.Ask) == gid ? ImGuiTabItemFlags_SetSelected : 0;
				if (!ImGui::BeginTabItem((L.Text(group[1]) + "###" + gid).c_str(), nullptr, flags)) continue;
				const TribeMember* person = nullptr;
				if (gid == "person" && !m_Observer.Person.empty()) person = t.FindMember(m_Observer.Person);
				std::string id = gid == "person" ? (person ? "person" : "members") : gid == "more" ? m_Observer.More : gid;
				m_Observer.Tab = id;
				if (m_Observer.Folded) { ImGui::EndTabItem(); continue; }   // (folded: the tabs only; a click on one unfolds it below)
				if (gid == "more")   // More's sheets: a button each, the one shown lit, wrapping to a new row rather than scrolling
				{
					float x = 0.0f;
					const float room = ImGui::GetContentRegionAvail().x;
					m_Observer.MoreRows = 1;
					for (size_t i = 0; i < std::size(kMoreSheets); ++i)
					{
						const std::string label = L.Text(kMoreSheets[i][1]);
						const float bw2 = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
						if (i > 0 && x + ImGui::GetStyle().ItemSpacing.x + bw2 <= room) { ImGui::SameLine(); x += ImGui::GetStyle().ItemSpacing.x + bw2; }
						else { if (i > 0) ++m_Observer.MoreRows; x = bw2; }
						std::optional<T::PrimaryScope> lit;
						if (m_Observer.More == kMoreSheets[i][0]) lit.emplace();
						if (ImGui::Button((label + "###more" + kMoreSheets[i][0]).c_str())) run(std::string("observer sheet ") + kMoreSheets[i][0]);
					}
					ImGui::Separator();
				}
				ImGui::BeginChild("sheet");
				if (id == "clan") DrawClanSheet(clan);
				else if (id == "chronicle") DrawChronicleSheet();
				else if (id == "person") DrawPersonSheet(t, *person);
				else if (id == "members")
				{
					if (ImGui::BeginTable("members", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
					{
						ImGui::TableSetupColumn(L.Text("Who").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.2f);
						ImGui::TableSetupColumn(L.Text("Doing").c_str(), ImGuiTableColumnFlags_WidthStretch, 2.4f);
						ImGui::TableSetupColumn(L.Text("Lowest need").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.2f);
						ImGui::TableHeadersRow();
						for (const bool deadPass : { false, true })
							for (const TribeMember& m : t.Members())
							{
								if (m.Dead != deadPass) continue;
								ImGui::TableNextRow();
								ImGui::TableNextColumn();
								if (m.Dead) ImGui::TextDisabled("%s", m.Name.c_str());
								else if (ImGui::Selectable((m.Name + "###" + m.Name).c_str(), m_FreeSelected == m.Name, ImGuiSelectableFlags_SpanAllColumns))
								{
									m_FreeSelected = m.Name;   // (its bubble over it in the world)
									run("observer person " + m.Name);
								}
								if (!m.Dead && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Open the sheet of {0}", m.Name).c_str());
								ImGui::TextDisabled("%s", L.Name("role", m.Role).c_str());
								ImGui::TableNextColumn();
								if (m.Dead) ImGui::TextDisabled("%s", (m.Cause.empty() ? L.Text("dead") : L.Text("dead of {0}", L.Name("need", m.Cause))).c_str());
								else ImGui::TextWrapped("%s", L.Doing(m.Job, m.Detail).c_str());
								ImGui::TableNextColumn();
								if (m.Dead) continue;
								// the lowest of the needs the HUD shows (hud.needs, 0..100: the others are on other scales)
								std::string low;
								float value = 101.0f;
								const std::map<std::string, float> levels = t.NeedLevels(m);
								for (const std::string& need : m_Controls.Hud.Needs)
									if (const auto it = levels.find(need); it != levels.end() && it->second < value) { value = it->second; low = need; }
								if (!low.empty())
									ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Level(value / 100.0f)), "%s %.0f", Lang::Cap(L.Name("need", low, low)).c_str(), static_cast<double>(value));
							}
						ImGui::EndTable();
					}
				}
				else if (id == "village") DrawVillageSheet(t);
				else if (id == "board") DrawBoardSheet(t, false);
				else if (id == "homes") DrawHomesSheet(t, false);
				else if (id == "sites")
				{
					const auto sites = t.SettlementSites();
					if (sites.empty()) ImGui::TextDisabled("%s", L.Text("no sites yet").c_str());
					for (const Tribe::SettlementSite& s : sites)
					{
						ImGui::PushID(s.Name.c_str());
						ImGui::TextUnformatted(L.Entity(s.Name).c_str());
						ImGui::SameLine();
						if (s.State.Done)
						{
							std::string gives;
							if (s.Beds > 0) gives = L.Text("{0} beds", s.Beds);
							if (!s.Station.empty()) gives += (gives.empty() ? "" : ", ") + L.Name("station", s.Station);
							ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", (L.Text("stands") + (gives.empty() ? std::string() : " (" + gives + ")")).c_str());
							if (const auto def = m_Buildings.Defs.find(s.Building); def != m_Buildings.Defs.end() && ImGui::IsItemHovered())
							{
								std::string tip;
								for (const std::string& line : BuildingDoes(def->second)) tip += (tip.empty() ? "" : "\n") + line;
								ImGui::SetTooltip("%s", tip.c_str());
							}
						}
						else
						{
							ImGui::TextDisabled("%s", L.Text("{0} ({1} of {2})", L.Name("phase", s.State.PhaseName), s.State.Phase + 1, s.State.Phases).c_str());
							ImGui::ProgressBar(std::clamp(s.State.Progress, 0.0f, 1.0f), ImVec2(-FLT_MIN, 6.0f), "");
							std::string missing;
							for (const auto& [item, n] : s.State.Missing) if (n > 0) missing += (missing.empty() ? "" : ", ") + std::to_string(n) + " " + L.Name("item", item);
							if (!missing.empty()) ImGui::TextDisabled("%s", L.Text("lacks {0}", missing).c_str());
						}
						ImGui::PopID();
					}
				}
				else if (id == "knowledge") DrawTechSheet(t);
				else if (id == "relations") DrawClansSheet(t);
				else
				{
					const std::vector<std::string>& log = t.Log();
					int n = 0;
					for (auto it = log.rbegin(); it != log.rend() && n < 60; ++it, ++n)
					{
						const size_t cut = it->find_first_not_of(" 0123456789.");
						ImGui::TextWrapped("%s", Lang::Cap(L.Line(cut == std::string::npos ? *it : it->substr(cut))).c_str());
					}
					if (log.empty()) ImGui::TextDisabled("%s", L.Text("nothing yet").c_str());
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
		m_Observer.Ask.clear();
		m_Observer.BookWidth = bw + T::Margin;
		// U5/U6: on the god's screen, between the list and the book under the bar: whom the camera follows, and under it the news
		if (god)
		{
			DrawFollowChip(ox, w);
			DrawObserverAlerts(ox, w);   // T220: the alerts first, the news under them
			DrawObserverNews(ox, w);
		}
		DrawHandBar(ox, oy, w, h);   // T201: the god's hand, at the bottom between the list and the book
		if (god) DrawColonyTabs(ox, oy, w, h);   // T308: the colony's Work or Stock over that bar
		if (god && m_Director.Defeated() && !m_Director.DefeatSeen()) DrawDefeat(ox, oy, w, h);   // §6.2 A (never in the player's game)
		else if (god && m_Director.StageWon() && !m_Director.Defeated()) DrawStageWon(ox, oy, w, h);   // D48 (the defeat goes first)
	}

	// U2: the inside of the quality of life tooltip: the index, then each of its 7 parts with its bar, its value and its weight ("-" when it cannot
	// be told yet: its weight goes to the others), then what the recent young dead take off.
	void EditorApp::DrawQolTip(const SocietyMeasure& m)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		ImGui::TextUnformatted(L.Text("Quality of life {0}", L.Num(m.Qol, 2)).c_str());
		ImGui::TextDisabled("%s", L.Text("The weighted mean of its parts, less what the recent young dead take off").c_str());
		ImGui::Separator();
		if (ImGui::BeginTable("qolparts", 3, ImGuiTableFlags_SizingFixedFit))
		{
			for (const std::string& part : QolParts())
			{
				const auto it = m.Parts.find(part);
				const float v = it == m.Parts.end() ? -1.0f : it->second;
				const auto w = m_Clans.Metrics.Weights.find(part);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(L.Text(PartText(part)).c_str());
				ImGui::TableNextColumn();
				SmallBar(v, 70.0f, T::Level(v));
				ImGui::SameLine();
				if (v < 0.0f) ImGui::TextDisabled("%s", L.Text("- (not yet)").c_str());
				else ImGui::TextUnformatted(L.Num(v, 2).c_str());
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", L.Text("weight {0} %", L.Num((w == m_Clans.Metrics.Weights.end() ? 0.0f : w->second) * 100.0f, 0)).c_str());
			}
			ImGui::EndTable();
		}
		ImGui::Separator();
		if (m.Losses > 0.0f) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", L.Text("Losses (young dead lately): -{0} %", L.Num(m.Losses * 100.0f, 0)).c_str());
		else ImGui::TextDisabled("%s", L.Text("Losses (young dead lately): none").c_str());
	}

	// U2: the god's list of clans, in the card's place (MODO_DIOS_UX 3.4): a row per clan with its quality of life and its trend, its days of food,
	// its state and the provisional winter mark; a click on a row observes that clan (`observer <id>`). Under it, the book's and the tree's buttons
	// (the card had them). Every number comes from the same measure `observer clans` prints (Societies()).
	void EditorApp::DrawClanList(float ox, float top)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto hovered = [](const ImVec2& a, const ImVec2& b) { return ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b); };
		const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
		ImGui::SetNextWindowPos(ImVec2(ox + T::Margin, top));
		if (ImGui::Begin("Clans##observer", nullptr, fixed))
		{
			if (ImGui::BeginTable("clans", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
			{
				for (const char* head : { "Clan", "Quality of life", "Food", "State", "Winter" }) ImGui::TableSetupColumn(L.Text(head).c_str());
				ImGui::TableHeadersRow();
				for (const auto& c : m_Clans.All())
				{
					if (!c->Folk) continue;
					const Tribe& t = *c->Folk;
					const SocietyMeasure* m = MeasureOf(c->Id);
					const int alive = m ? m->Alive : 0;
					const std::string name = L.Name("clan", c->Name, c->Name);
					ImGui::PushID(c->Id);
					ImGui::TableNextRow();
					// the clan: its colour, its name and its living; the row is the button
					ImGui::TableNextColumn();
					Swatch(ClanColour(m_Clans, c->Id));
					if (ImGui::Selectable((name + " " + std::to_string(alive) + "###row").c_str(), c->Id == m_ActiveClan, ImGuiSelectableFlags_SpanAllColumns))
						run("observer " + std::to_string(c->Id));
					// (the row's button spans every column, so the cells' tooltips test their own rectangles and the row's shows only over the name)
					const bool rowHovered = ImGui::IsItemHovered() && m_Observer.Tip < 0;
					const std::string rowTip = L.Text("Observe the {0}: {1} alive", name, alive);
					if (!m || alive == 0)   // (an extinct clan stays in the list, dimmed)
					{
						if (rowHovered) ImGui::SetTooltip("%s", rowTip.c_str());
						ImGui::TableNextColumn();
						ImGui::TextDisabled("%s", L.Text("extinct").c_str());
						ImGui::PopID();
						continue;
					}
					// the quality of life: a bar, the number with 2 decimals and the trend (the tooltip: its 7 parts)
					ImGui::TableNextColumn();
					const ImVec2 qa = ImGui::GetCursorScreenPos();
					if (rowHovered && ImGui::GetMousePos().x < qa.x) ImGui::SetTooltip("%s", rowTip.c_str());
					SmallBar(m->Qol, 46.0f, T::Level(m->Qol));
					ImGui::SameLine();
					ImGui::TextUnformatted(L.Num(m->Qol, 2).c_str());
					const QolTrend trend = TrendOf(c->Id, m->Qol);
					ImGui::SameLine();
					if (trend.Known)
					{
						Trend(trend.Delta);
						ImGui::SameLine(0.0f, 2.0f);
						ImGui::TextDisabled("%s%s", trend.Delta >= 0.0f ? "+" : "", L.Num(trend.Delta, 2).c_str());
					}
					else ImGui::TextDisabled("  ");
					const ImVec2 qb = ImGui::GetItemRectMax();
					std::string trendTip = !trend.Known ? L.Text("No trend yet: it is kept once a day") : L.Text("{0} in {1} day(s)", (trend.Delta >= 0.0f ? "+" : "") + L.Num(trend.Delta, 2), trend.Days);
					if (trend.Ago1 >= 0.0f) trendTip += "\n" + L.Text("1 day ago: {0}", L.Num(trend.Ago1, 2));
					if (trend.Ago3 >= 0.0f) trendTip += "\n" + L.Text("3 days ago: {0}", L.Num(trend.Ago3, 2));
					const bool forced = m_Observer.Tip == c->Id;
					if (forced || hovered(qa, ImVec2(qb.x, qa.y + ImGui::GetTextLineHeightWithSpacing())))
					{
						if (forced) ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + T::Gap, qa.y));
						if (ImGui::BeginTooltip())
						{
							DrawQolTip(*m);
							ImGui::Separator();
							ImGui::TextDisabled("%s", trendTip.c_str());
							ImGui::EndTooltip();
						}
					}
					// the days of food
					ImGui::TableNextColumn();
					const float food = 1.0f - std::clamp(t.FoodShortage(), 0.0f, 1.0f);
					const ImVec2 fa = ImGui::GetCursorScreenPos();
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Level(food)), "%s", L.Text("{0} d", L.Num(t.FoodDays(), 1)).c_str());
					if (m_Observer.Tip < 0 && hovered(fa, ImGui::GetItemRectMax())) ImGui::SetTooltip("%s", L.Text("Days of food the clan has in hand (store and bags); the bar is how far it is from the days it wants").c_str());
					// the state: the lowest need not covered
					ImGui::TableNextColumn();
					const std::string tier = t.MaslowTier();
					const ImVec2 sa = ImGui::GetCursorScreenPos();
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(TierColour(tier)), "%s", L.Text(TierShort(tier)).c_str());
					if (m_Observer.Tip < 0 && hovered(sa, ImGui::GetItemRectMax())) ImGui::SetTooltip("%s", L.Text("The lowest need the clan has not covered: food, then warmth (fire and roof), then safety; when all are covered it grows").c_str());
					// the winter (provisional, until the winter's outlook: U3)
					ImGui::TableNextColumn();
					const WinterGuess w = GuessWinter(t);
					const ImVec2 wa = ImGui::GetCursorScreenPos();
					Flake(WinterColour(w));
					ImGui::SameLine(0.0f, 3.0f);
					// (what the clan's food lacks to reach the spring, not the days to the winter: "-20 d food", "food ok")
					if (w.Short <= 0.0f) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", L.Text("food ok").c_str());
					else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(WinterColour(w) == T::BoneDim ? T::Warn : WinterColour(w)), "%s", L.Text("-{0} d food", L.Num(w.Short, 0)).c_str());
					if (m_Observer.WinterTip == c->Id || (m_Observer.Tip < 0 && hovered(wa, ImGui::GetItemRectMax())))
						ImGui::SetTooltip("%s", (w.In ? L.Text("The winter is here: {0} days until the spring", L.Num(w.UntilSpring, 0)) : L.Text("The winter comes in {0} days and lasts {1}", L.Num(w.ToWinter, 0), L.Num(w.SeasonDays, 0)))
							.append("\n").append(w.Short <= 0.0f ? L.Text("Food stored for {0} days: enough to reach the spring", L.Num(t.FoodDays(), 1))
								: L.Text("Food stored for {0} days; it lacks {1} days to reach the spring", L.Num(t.FoodDays(), 1), L.Num(w.Short, 0))).c_str());
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::Separator();
			if (ImGui::Button((L.Text(m_Observer.Folded ? "Unfold the book" : "Fold the book") + "###fold").c_str())) run("observer fold");
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Tree of knowledge") + "###tree").c_str())) run("observer tree");
			m_Observer.CardBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;   // (the selection's card goes under it)
			m_Observer.ClansBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, m_Observer.CardBottom };
		}
		ImGui::End();
	}

	// U2: the clan's sheet of the book (MODO_DIOS_UX 3.4), the first tab: who the clan is, its quality of life with its parts, its food and the
	// provisional winter, its fire and state, what it believes, what it knows and its people; its buttons go to the camp, the tree and the other
	// sheets (`camera`, `observer ...` commands).
	void EditorApp::DrawClanSheet(const Clan& clan)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const Tribe& t = *clan.Folk;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto label = [&](const std::string& text) { ImGui::TextDisabled("%s", text.c_str()); ImGui::SameLine(124.0f); };
		// (the values wrap: the book is a third of a 1280 wide window)
		auto value = [](const std::string& text, ImU32 colour = T::Bone) { ImGui::PushStyleColor(ImGuiCol_Text, colour); ImGui::TextWrapped("%s", text.c_str()); ImGui::PopStyleColor(); };
		const SocietyMeasure* m = MeasureOf(clan.Id);
		int alive = 0, dead = 0;
		for (const TribeMember& p : t.Members()) (p.Dead ? dead : alive) += 1;
		Swatch(ClanColour(m_Clans, clan.Id));
		ImGui::TextUnformatted(L.Name("clan", clan.Name, clan.Name).c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("%s", (dead ? L.Text("{0} alive, {1} dead", alive, dead) : L.Text("{0} alive", alive)).c_str());
		const glm::vec2 camp = t.Config.Economy.Camp;
		if (ImGui::Button((L.Text("Go to the camp") + "###camp").c_str())) run("camera 0 45 45 " + std::to_string(camp.x) + " ground " + std::to_string(camp.y));
		ImGui::SameLine();
		if (ImGui::Button((L.Text("Tree of knowledge") + "###tree").c_str())) run("observer tree");
		ImGui::Separator();
		if (!m || alive == 0)
		{
			ImGui::TextDisabled("%s", L.Text("extinct").c_str());
			return;
		}
		// the quality of life, its trend and its parts
		label(L.Text("Quality of life"));
		SmallBar(m->Qol, 90.0f, T::Level(m->Qol));
		ImGui::SameLine();
		ImGui::TextUnformatted(L.Num(m->Qol, 2).c_str());
		const QolTrend trend = TrendOf(clan.Id, m->Qol);
		if (trend.Known)
		{
			ImGui::SameLine();
			Trend(trend.Delta);
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::TextDisabled("%s", L.Text("{0} in {1} day(s)", (trend.Delta >= 0.0f ? "+" : "") + L.Num(trend.Delta, 2), trend.Days).c_str());
		}
		const int perRow = ImGui::GetContentRegionAvail().x >= 420.0f ? 2 : 1;   // (one part a row in a narrow book: the editor's viewport)
		if (ImGui::BeginTable("parts", 2 * perRow, ImGuiTableFlags_SizingFixedFit))
		{
			int i = 0;
			for (const std::string& part : QolParts())
			{
				if (i++ % perRow == 0) ImGui::TableNextRow();
				const auto it = m->Parts.find(part);
				const float v = it == m->Parts.end() ? -1.0f : it->second;
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", L.Text(PartText(part)).c_str());
				ImGui::TableNextColumn();
				SmallBar(v, 60.0f, T::Level(v));
				ImGui::SameLine();
				ImGui::TextUnformatted(v < 0.0f ? "-" : L.Num(v, 2).c_str());
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::Dummy(ImVec2(12.0f, 1.0f));   // (room before the next part)
			}
			ImGui::EndTable();
		}
		if (m->Losses > 0.0f) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", L.Text("Losses (young dead lately): -{0} %", L.Num(m->Losses * 100.0f, 0)).c_str());
		ImGui::Separator();
		// food, store and the winter
		const float food = 1.0f - std::clamp(t.FoodShortage(), 0.0f, 1.0f);
		label(L.Text("Food"));
		SmallBar(food, 90.0f, T::Level(food));
		ImGui::SameLine();
		ImGui::TextUnformatted(L.Text("{0} days", L.Num(t.FoodDays(), 1)).c_str());
		{
			const int used = t.Store().UsedSlots(), slots = std::max(1, t.Store().Slots());
			label(L.Text("Store"));
			value(L.Text("{0} of {1} places, up to {2} of each", used, slots, t.StoreCap()));
		}
		const WinterGuess w = GuessWinter(t);
		label(L.Text("Winter"));
		Flake(WinterColour(w));
		ImGui::SameLine(0.0f, 3.0f);
		value(w.In ? L.Text("Winter: {0} days until the spring", L.Num(w.UntilSpring, 0)) : L.Text("Winter in {0} days: {1} days until the spring", L.Num(w.ToWinter, 0), L.Num(w.UntilSpring, 0)));
		ImGui::SetCursorPosX(124.0f + ImGui::GetTextLineHeight() + 3.0f);   // (under the line above, past the snowflake)
		value(w.Short <= 0.0f ? L.Text("The winter is covered") : L.Text("It is {0} days of food short", L.Num(w.Short, 0)),
			w.Short <= 0.0f ? T::Good : WinterColour(w) == T::BoneDim ? T::Bone : WinterColour(w));
		// fire and state
		label(L.Text("Fire"));
		if (!t.HasFire()) value(L.Text("no campfire yet"), T::Warn);
		else if (t.FireBurning()) value(L.Text("lit"), T::Good);
		else value(L.Text("out (no wood)"), T::Bad);
		const std::string tier = t.MaslowTier();
		label(L.Text("State"));
		value(L.Text(TierText(tier)), TierColour(tier));
		// faith, knowledge, people
		const SocialState& social = t.Social();
		if (const std::string dominant = social.Dominant(); !dominant.empty())
		{
			const auto def = t.Config.Social.Beliefs.Systems.find(dominant);
			std::string name = L.Name("belief", dominant, def != t.Config.Social.Beliefs.Systems.end() ? def->second.Name : std::string());
			if (const size_t cut = name.find(" ("); cut != std::string::npos) name = name.substr(0, cut);
			const auto v = social.Beliefs.find(dominant);
			label(L.Text("Faith"));
			value(L.Text("{0}, {1} %", Lang::Cap(name), L.Num((v == social.Beliefs.end() ? 0.0f : v->second) * 100.0f, 0)));
		}
		label(L.Text("Knowledge"));
		value(L.Text("{0} technologies, {1} that better life", m->Techs, m->LifeTechs));
		label(L.Text("People"));
		// (SocietyMeasure::Generations is the highest generation alive, the founders 0: one more is how many generations the clan has had)
		value(L.Text("{0} generations, {1} lineages, {2} inventors", m->Generations + 1, m->Lineages, m->Inventors));
		ImGui::Separator();
		static const char* kMore[][2] = { { "village", "Village" }, { "board", "Job board" }, { "homes", "Homes" }, { "knowledge", "Knowledge" }, { "relations", "Clans" } };
		for (size_t i = 0; i < std::size(kMore); ++i)
		{
			if (i) ImGui::SameLine();
			if (ImGui::SmallButton((L.Text(kMore[i][1]) + "###more" + kMore[i][0]).c_str())) run(std::string("observer sheet ") + kMore[i][0]);
		}
	}

	// U1: the god's bar, over the whole width at the top of the god's screen (MODO_DIOS_UX 3.1): the pause and the speeds, the day and the hour, the
	// season with what is left of it, the weather of the day and how wet the season runs, and the way back to the editor. Its buttons run `observer
	// ...` commands; the clock is the world's (the first tribe's, which every clan shares) and the weather the one the sky shows (ApplySeasonLook).
	// Drawn inside the observer's GameTheme::Scope.
	void EditorApp::DrawGodBar(float ox, float oy, float w)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const Tribe& t = m_Tribe;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto keyOf = [&](const char* action) { const auto it = m_Controls.Keys.find(action); return it == m_Controls.Keys.end() ? std::string("-") : L.Key(it->second); };
		auto divider = [&]() { ImGui::SameLine(0.0f, 14.0f); ImGui::TextDisabled("|"); ImGui::SameLine(0.0f, 14.0f); };
		ImGui::SetNextWindowPos(ImVec2(ox, oy), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);   // (0: as tall as its one line)
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (ImGui::Begin("God##observer", nullptr, flags))
		{
			// the clock's control: pause (ochre while paused) and the speeds, the one running in ochre
			{
				std::optional<T::PrimaryScope> lit;
				if (m_Observer.Paused) lit.emplace();
				if (ImGui::Button(m_Observer.Paused ? " > ###pause" : " || ###pause")) run("observer pause");
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s [%s]", L.Text(m_Observer.Paused ? "Resume" : "Pause").c_str(), keyOf("pause").c_str());
			for (const int speed : { 1, 4, 8, 20 })
			{
				ImGui::SameLine();
				const std::string label = "x" + std::to_string(speed) + "###speed" + std::to_string(speed);
				if (speed == m_TimeScale) { const T::PrimaryScope lit; if (ImGui::Button(label.c_str())) run("observer speed " + std::to_string(speed)); }
				else if (ImGui::Button(label.c_str())) run("observer speed " + std::to_string(speed));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Time runs {0} times as fast [{1} {2}]", speed, keyOf("slower"), keyOf("faster")).c_str());
			}
			if (m_Observer.Paused)
			{
				ImGui::SameLine(0.0f, 10.0f);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("PAUSED").c_str());
			}
			// the day and the hour
			const double now = t.WorldSeconds();
			const float daySeconds = std::max(1.0f, t.Config.Needs.DaySeconds);
			const SeasonClock& seasons = t.Config.Seasons;
			const int seasonDays = std::max(1, static_cast<int>(std::lround(seasons.SeasonSeconds / daySeconds))), yearDays = 4 * seasonDays;
			const int day = GameDay();
			const float hour = t.Hour();
			char clock[16];
			std::snprintf(clock, sizeof clock, "%02d:%02d", static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f));
			divider();
			ImGui::TextUnformatted((L.Text("Day {0}", day) + "  " + clock).c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Year {0}, day {1} of {2}", (day - 1) / yearDays + 1, (day - 1) % yearDays + 1, yearDays).c_str());
			// the season, a bar with what is left of it and the day of it
			const Season season = seasons.At(now);
			const float progress = std::clamp(seasons.Progress(now), 0.0f, 1.0f);
			const int seasonDay = std::min(seasonDays, static_cast<int>(progress * static_cast<float>(seasonDays)) + 1), left = seasonDays - seasonDay + 1;
			const Season next = static_cast<Season>((static_cast<int>(season) + 1) % 4);
			divider();
			ImGui::BeginGroup();
			ImGui::TextUnformatted(Lang::Cap(L.Name("season", SeasonClock::Name(season))).c_str());
			ImGui::SameLine();
			{
				const ImVec2 at = ImGui::GetCursorScreenPos();
				const float bw = 90.0f, y0 = at.y + (ImGui::GetTextLineHeight() - T::BarH) * 0.5f + 1.0f;
				ImGui::Dummy(ImVec2(bw, ImGui::GetTextLineHeight()));
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + bw, y0 + T::BarH), T::HideRaised, 3.0f);
				// (ochre always: the warning before a winter without reserves waits for the winter's outlook, U3)
				dl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + bw * (1.0f - progress), y0 + T::BarH), T::Ochre, 3.0f);
				dl->AddRect(ImVec2(at.x, y0), ImVec2(at.x + bw, y0 + T::BarH), T::HideLine, 3.0f);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(L.Text("{0} of {1}", seasonDay, seasonDays).c_str());
			ImGui::EndGroup();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", (left == 1 ? L.Text("The {0} begins tomorrow", L.Name("season", SeasonClock::Name(next))) : L.Text("{0} days left until the {1}", left, L.Name("season", SeasonClock::Name(next)))).c_str());
			// the weather of the day and, with a seeded climate, whether the season runs dry or wet against the usual (a fifth either way)
			const std::string weather = seasons.WeatherAt(now);
			divider();
			ImGui::BeginGroup();
			ImGui::TextUnformatted(Lang::Cap(L.Name("weather", weather, weather)).c_str());
			const float wet = seasons.WetnessAt(now), usual = seasons.Climate.Seasons[static_cast<size_t>(season)].Wet;
			const bool seeded = seasons.Climate.Seed != 0u && usual > 0.0f;
			std::string wetTip = L.Text("The weather of the day.");
			if (seeded)
			{
				wetTip += "\n" + L.Text("This season {0} % of the days are wet; usually {1} %.", L.Num(wet * 100.0f, 0), L.Num(usual * 100.0f, 0));
				if (wet < usual * 0.8f || wet > usual * 1.2f)
				{
					ImGui::SameLine();
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(wet < usual ? T::Warn : T::News), "%s", L.Text(wet < usual ? "dry season" : "wet season").c_str());
				}
			}
			ImGui::EndGroup();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", wetTip.c_str());
			// T220: the alerts' counter (red with a critical one, ochre with only important ones; none: nothing drawn); a click folds the stack
			{
				const std::vector<const Alert*> alerts = VisibleAlerts();
				if (!alerts.empty())
				{
					const bool critical = std::any_of(alerts.begin(), alerts.end(), [](const Alert* x) { return x->Level == AlertLevel::Critical; });
					divider();
					ImGui::PushStyleColor(ImGuiCol_Text, critical ? T::Bad : T::Ochre);
					ImGui::PushStyleColor(ImGuiCol_Border, critical ? T::Bad : T::Ochre);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
					if (ImGui::Button(("(!) " + std::to_string(alerts.size()) + "###alerts").c_str())) run("observer alerts fold");
					ImGui::PopStyleVar();
					ImGui::PopStyleColor(2);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", (L.Text("{0} alerts need your attention", static_cast<int>(alerts.size())) + "\n" + L.Text(m_Observer.AlertsFolded ? "Click to show them" : "Click to hide them")).c_str());
				}
			}
			// the way back to the editor, at the right end (or right after the rest, in a narrow window); in GB.exe (G1) there is no editor: its menu
			const char* backLabel = m_Shell.On ? "Menu" : "Editor";
			const std::string back = L.Text(backLabel) + "###editor";
			const float bw = ImGui::CalcTextSize(L.Text(backLabel).c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 14.0f;
			ImGui::SameLine(std::max(after, ImGui::GetWindowContentRegionMax().x - bw));
			if (ImGui::Button(back.c_str())) run(m_Shell.On ? "shell pause on" : "observer screen off");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s [%s]", L.Text(m_Shell.On ? "Pause menu: save, load, settings, main menu" : "Back to the editor").c_str(), keyOf("quit").c_str());
			m_Observer.BarBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
			m_Observer.BarBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, m_Observer.BarBottom };
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}

	// ------------------------------------------------------------------ U5: the world's chronicle and its news
	int EditorApp::EventDay(double at) const
	{
		const Tribe& t = m_Tribe;   // (the world's clock and its day, as DayOf counts them for the god's bar)
		return static_cast<int>(at / std::max(1.0, static_cast<double>(t.Config.Needs.DaySeconds)) + static_cast<double>(t.Config.Needs.StartHour) / 24.0) + 1;
	}

	bool EditorApp::ChronicleShows(const HistoryEvent& e) const
	{
		const std::string kind = SheetKind(e.Kind);
		return !kind.empty() && !m_Observer.ChronicleHidden.count(kind) && (m_Observer.ChronicleClan < 0 || e.Clan == m_Observer.ChronicleClan) && e.Weight >= m_Observer.ChronicleWeight;
	}

	std::string EditorApp::ObserverChronicleText()
	{
		std::string kinds;
		for (int k = 0; k < StoryKindCount; ++k)
			if (!m_Observer.ChronicleHidden.count(StoryKinds()[k].Id)) kinds += (kinds.empty() ? "" : " ") + std::string(StoryKinds()[k].Id);
		std::vector<const HistoryEvent*> shown;
		for (const HistoryEvent& e : m_Clans.Past().Events()) if (ChronicleShows(e)) shown.push_back(&e);
		const std::string clan = m_Observer.ChronicleClan < 0 ? std::string("all") : std::to_string(m_Observer.ChronicleClan);
		char line[200];
		std::snprintf(line, sizeof line, "chronicle: clan %s, kinds %s, weight >= %d: %zu events", clan.c_str(), kinds.empty() ? "none" : kinds.c_str(), m_Observer.ChronicleWeight, shown.size());
		std::string out = line;
		const size_t from = shown.size() > 40 ? shown.size() - 40 : 0;   // (the newest 40, oldest first, like the log)
		if (from) out += " (the newest 40)";
		for (size_t i = from; i < shown.size(); ++i)
		{
			const HistoryEvent& e = *shown[i];
			char where[48] = "-";
			if (e.HasWhere) std::snprintf(where, sizeof where, "%.1f,%.1f", static_cast<double>(e.Where.x), static_cast<double>(e.Where.y));
			std::snprintf(line, sizeof line, "\n#%d day %d clan %d kind %s (%s) w%d at %s: ", e.Id, EventDay(e.At), e.Clan, SheetKind(e.Kind).c_str(), e.Kind.c_str(), e.Weight, where);
			out += line + e.Text;
		}
		return out;
	}

	// U5: the chronicle of every clan (MODO_DIOS_UX 3.6): its filters on top (a box per group with its count, the clan, the least weight), then the
	// events newest first under their day, each with its group's colour and icon, its clan and a [go] that sends the camera there. The filters are
	// the view's (ObserverState) and its boxes only queue `observer chronicle ...`, so a script sees the same sheet.
	void EditorApp::DrawChronicleSheet()
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const int clanFilter = m_Observer.ChronicleClan, weight = m_Observer.ChronicleWeight;
		auto filterCommand = [&](int clan, const std::set<std::string>& hidden, int least)
		{
			std::string c = "observer chronicle " + (clan < 0 ? std::string("all") : std::to_string(clan));
			for (int k = 0; k < StoryKindCount; ++k) if (!hidden.count(StoryKinds()[k].Id)) c += std::string(" ") + StoryKinds()[k].Id;
			return c + " weight " + std::to_string(least);
		};
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
		// the groups (one at least stays shown), wrapping to a new row when the next would not fit the book
		const float room = ImGui::GetContentRegionAvail().x;
		float x = 0.0f;
		for (int k = 0; k < StoryKindCount; ++k)
		{
			const StoryKind& kind = StoryKinds()[k];
			const std::string text = L.Text(kind.Label) + " (" + std::to_string(counts[kind.Id]) + ")";
			const float cw = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(text.c_str()).x;
			if (k > 0 && x + ImGui::GetStyle().ItemSpacing.x + cw <= room) { ImGui::SameLine(); x += ImGui::GetStyle().ItemSpacing.x + cw; }
			else x = cw;
			bool on = !m_Observer.ChronicleHidden.count(kind.Id);
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::ColorConvertU32ToFloat4(kind.Colour));
			if (ImGui::Checkbox((text + "###" + kind.Id).c_str(), &on))
			{
				std::set<std::string> hidden = m_Observer.ChronicleHidden;
				if (on) hidden.erase(kind.Id); else hidden.insert(kind.Id);
				if (static_cast<int>(hidden.size()) < StoryKindCount) run(filterCommand(clanFilter, hidden, weight));
			}
			ImGui::PopStyleColor();
		}
		// the clan and the least weight
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", L.Text("Clan").c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(130.0f);
		if (ImGui::BeginCombo("###chronicleclan", clanFilter < 0 ? L.Text("all").c_str() : clanName(clanFilter).c_str()))
		{
			if (ImGui::Selectable((L.Text("all") + "###all").c_str(), clanFilter < 0)) run(filterCommand(-1, m_Observer.ChronicleHidden, weight));
			for (const auto& c : m_Clans.All())
				if (ImGui::Selectable((L.Name("clan", c->Name, c->Name) + "###clan" + std::to_string(c->Id)).c_str(), clanFilter == c->Id))
					run(filterCommand(c->Id, m_Observer.ChronicleHidden, weight));
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", L.Text("Weight").c_str());
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("How much the clans remember it: 1 a small thing, 5 something never forgotten").c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		auto weightText = [&](int n) { return n <= 1 ? L.Text("any") : L.Text("{0} or more", n); };
		if (ImGui::BeginCombo("###chronicleweight", weightText(weight).c_str()))
		{
			for (int n = 1; n <= 5; ++n)
				if (ImGui::Selectable((weightText(n) + "###w" + std::to_string(n)).c_str(), weight == n)) run(filterCommand(clanFilter, m_Observer.ChronicleHidden, n));
			ImGui::EndCombo();
		}
		ImGui::Separator();
		if (shown.empty())
		{
			ImGui::TextDisabled("%s", (counts.empty() ? L.Text("nothing has happened yet") : L.Text("nothing passes these filters")).c_str());
			return;
		}
		// newest first, under the day it happened (the newest 300: a long game would make the sheet slow)
		int day = -1, drawn = 0;
		for (auto it = shown.rbegin(); it != shown.rend() && drawn < 300; ++it, ++drawn)
		{
			const HistoryEvent& e = **it;
			if (const int d = EventDay(e.At); d != day)
			{
				day = d;
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(T::Ochre));
				ImGui::TextUnformatted(L.Text("Day {0}", day).c_str());
				ImGui::PopStyleColor();
			}
			ImGui::PushID(e.Id);
			const StoryKind* kind = FindStoryKind(SheetKind(e.Kind));
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + 3.0f, at.y + ImGui::GetTextLineHeight() + 2.0f), kind ? kind->Colour : T::BoneDim);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
			// [go]: the camera to its place (disabled, with why, when it has none)
			if (!e.HasWhere) ImGui::BeginDisabled();
			if (ImGui::SmallButton((L.Text("go") + "###go").c_str())) run("observer go " + std::to_string(e.Id));
			if (!e.HasWhere) ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", (e.HasWhere ? L.Text("The camera to where it happened") : L.Text("It has no place to go to")).c_str());
			ImGui::SameLine();
			if (kind && Icon(kind->Icon, ImGui::GetTextLineHeight())) ImGui::SameLine();
			if (e.Clan >= 0)
			{
				Swatch(ClanColour(m_Clans, static_cast<ClanId>(e.Clan)));
				ImGui::TextDisabled("%s", clanName(e.Clan).c_str());
				ImGui::SameLine();
			}
			ImGui::TextWrapped("%s", Lang::Cap(L.Line(e.Text)).c_str());
			ImGui::PopID();
		}
		if (shown.size() > 300) ImGui::TextDisabled("%s", L.Text("(and {0} older)", static_cast<int>(shown.size()) - 300).c_str());
	}

	// U5: the news of the god's screen (MODO_DIOS_UX 3.6): the weighty events of every clan (a death, a birth, a couple, a discovery, a raid, a new
	// clan...) as cards under the bar and left of the book, newest on top; they fade out as the player's news do (controls.hud: how many, how long),
	// a line said again shows its count, danger has a red edge and a discovery an ochre one. A click on a card sends the camera there.
	void EditorApp::DrawObserverNews(float ox, float w)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const std::vector<HistoryEvent>& events = m_Clans.Past().Events();
		const int newest = events.empty() ? 0 : events.back().Id;
		const double now = ImGui::GetTime();
		if (m_Observer.NewsSeen < 0 || newest < m_Observer.NewsSeen) { m_Observer.NewsSeen = newest; m_Observer.NewsList.clear(); }   // (the screen just opened, or a new world)
		std::vector<int> fresh;
		// T220: a discovery or a split the alerts' stack tells (the player sees those alerts and the stack is not folded) is not told again as news
		const std::vector<std::string>& alerted = m_Controls.Hud.Alerts;
		auto told = [&](const std::string& kind)
		{
			if (m_Observer.AlertsFolded) return false;
			const char* alert = kind == "discovery" ? "inventor" : kind == "found" ? "split" : nullptr;
			return alert && std::find(alerted.begin(), alerted.end(), alert) != alerted.end();
		};
		for (auto it = events.rbegin(); it != events.rend() && it->Id > m_Observer.NewsSeen; ++it)
			if (!SheetKind(it->Kind).empty() && it->Weight >= 4 && !told(it->Kind)) fresh.push_back(it->Id);
		for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) m_Observer.NewsList.push_back({ *it, now });
		m_Observer.NewsSeen = newest;
		while (m_Observer.NewsList.size() > 40) m_Observer.NewsList.pop_front();
		struct Shown { std::string Text; const HistoryEvent* Event; float Alpha; int Count; };
		std::vector<Shown> shown;
		const double life = static_cast<double>(m_Controls.Hud.NotifySeconds);
		// T271: an order of the colony that was cut ("Biel dejó la orden: mucha hambre"), newest first, before the chronicle's news
		if (ColonyScreen())
			for (auto it = m_Colony.Notes.rbegin(); it != m_Colony.Notes.rend() && static_cast<int>(shown.size()) < m_Controls.Hud.Notifications; ++it)
				if (const double age = now - it->At; age <= life) shown.push_back({ Lang::Cap(L.Line(it->Text)), nullptr, std::clamp(static_cast<float>(life - age) / 1.5f, 0.0f, 1.0f), 1 });
		for (auto it = m_Observer.NewsList.rbegin(); it != m_Observer.NewsList.rend() && static_cast<int>(shown.size()) < m_Controls.Hud.Notifications; ++it)
		{
			const double age = now - it->At;
			if (age > life) break;
			const HistoryEvent* e = m_Clans.Past().Find(it->Event);
			if (!e) continue;
			const Clan* c = e->Clan >= 0 ? m_Clans.Find(static_cast<ClanId>(e->Clan)) : nullptr;
			const std::string text = (c ? L.Name("clan", c->Name, c->Name) + ": " : std::string()) + Lang::Cap(L.Line(e->Text));
			bool same = false;
			for (Shown& s : shown) if (s.Text == text) { ++s.Count; same = true; }
			if (!same) shown.push_back({ text, e, std::clamp(static_cast<float>(life - age) / 1.5f, 0.0f, 1.0f), 1 });
		}
		if (shown.empty()) return;
		// between the clans' list and the book, under the bar and the follow chip
		const float left = m_Observer.ClansBox.X1 > m_Observer.ClansBox.X0 ? m_Observer.ClansBox.X1 + T::Gap : ox + T::Margin;
		const float right = m_Observer.BookBox.X1 > m_Observer.BookBox.X0 ? m_Observer.BookBox.X0 - T::Gap : ox + w - T::Margin;
		const float top = std::max({ m_Observer.BarBottom, m_Observer.FollowBox.Y1, m_Observer.AlertsBox.Y1 }) + T::Gap;   // (T220: under the alerts)
		const float card = std::min(380.0f, right - left), wrap = card - 2.0f * T::Pad - 4.0f;
		if (wrap < 80.0f) return;   // (no room: the window is too narrow for news)
		ImGui::SetNextWindowPos(ImVec2(right, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground;
		if (ImGui::Begin("News##observer", nullptr, flags))
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			for (const Shown& s : shown)
			{
				if (!s.Event)   // T271: a cut order (no place to go to: the person moves on)
				{
					const ImVec2 size = ImGui::CalcTextSize(s.Text.c_str(), nullptr, false, wrap);
					const ImVec2 min = ImGui::GetCursorScreenPos();
					ImGui::Dummy(ImVec2(card, size.y + 12.0f));
					T::Card(dl, min, ImVec2(min.x + card, min.y + size.y + 12.0f), T::Warn, s.Alpha);
					dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + T::Pad + 4.0f, min.y + 6.0f), T::Faded(T::News, s.Alpha), s.Text.c_str(), nullptr, wrap);
					continue;
				}
				const StoryKind* kind = FindStoryKind(SheetKind(s.Event->Kind));
				const ImU32 accent = kind && kind->Colour == T::Bad ? T::Bad : kind && std::string(kind->Id) == "know" ? T::Ochre : 0u;
				const std::string text = s.Count > 1 ? s.Text + "  x" + std::to_string(s.Count) : s.Text;
				const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
				const ImVec2 min = ImGui::GetCursorScreenPos();
				ImGui::PushID(s.Event->Id);
				if (ImGui::InvisibleButton("news", ImVec2(card, size.y + 12.0f)) && s.Event->HasWhere) m_UiCommands.push_back("observer go " + std::to_string(s.Event->Id));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", (s.Event->HasWhere ? L.Text("The camera to where it happened") : L.Text("It has no place to go to")).c_str());
				ImGui::PopID();
				const ImVec2 max(min.x + card, min.y + size.y + 12.0f);
				T::Card(dl, min, max, accent, s.Alpha);
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + T::Pad + 4.0f, min.y + 6.0f), T::Faded(T::News, s.Alpha), text.c_str(), nullptr, wrap);
			}
			m_Observer.NewsBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	// ------------------------------------------------------------------ T220 (U4): the god's alerts
	void EditorApp::TrackAlerts()
	{
		if (m_Clans.Count() == 0)
		{
			if (!m_Alerts.All().empty()) { m_Alerts.Clear(); m_AlertShown.clear(); }
			m_AlertStarted = false;
			return;
		}
		const std::vector<HistoryEvent>& events = m_Clans.Past().Events();
		const int newest = events.empty() ? 0 : events.back().Id;
		if (!m_AlertStarted || newest < m_AlertNewest || m_SimTick < m_AlertTick)   // a world made anew or loaded: what its history brings is not news
		{
			m_Alerts.Clear();
			m_Alerts.SkipHistory(m_Clans);
			m_AlertShown.clear();
			m_AlertStarted = true;
		}
		else if (m_SimTick < m_AlertTick + 60) { m_AlertNewest = newest; return; }   // (a look a second of game)
		m_AlertNewest = newest;
		m_AlertTick = m_SimTick;
		const std::vector<std::string> raised = m_Alerts.Look(m_Clans, m_AlertRules);
		const double now = ImGui::GetTime();
		for (const Alert& a : m_Alerts.All())   // (when it first appeared, or last changed by a new event: an important one goes after AlertSeconds)
		{
			auto [it, fresh] = m_AlertShown.emplace(a.Key, std::make_pair(now, a.Last));
			if (!fresh && !IsConditionKind(a.Kind) && it->second.second != a.Last) it->second = { now, a.Last };
		}
		for (auto it = m_AlertShown.begin(); it != m_AlertShown.end();)
		{
			const bool live = std::any_of(m_Alerts.All().begin(), m_Alerts.All().end(), [&](const Alert& a) { return a.Key == it->first; });
			it = live ? std::next(it) : m_AlertShown.erase(it);
		}
		// a critical alert the player sees and chose to pause on pauses the god's game when it is first raised (never again until it clears and
		// comes back; the player resumes as always)
		if (m_AlertPauseOff || m_Game.Active || !m_Observer.Screen || m_Observer.Paused) return;
		const HudRules& hud = m_Controls.Hud;
		for (const std::string& key : raised)
			for (const Alert& a : m_Alerts.All())
				if (a.Key == key && a.Level == AlertLevel::Critical && std::find(hud.Alerts.begin(), hud.Alerts.end(), a.Kind) != hud.Alerts.end()
					&& std::find(hud.AlertPause.begin(), hud.AlertPause.end(), a.Kind) != hud.AlertPause.end())
				{
					m_Observer.Paused = true;
					m_Observer.AlertsFolded = false;
					GB_CLIENT_INFO("alert: {}: the god's game pauses (observer pause to go on; controls.hud.alertPause says which pause)", a.Text);
					return;
				}
	}

	std::vector<const Alert*> EditorApp::VisibleAlerts() const
	{
		std::vector<const Alert*> out;
		const double now = ImGui::GetTime();
		const std::vector<std::string>& kinds = m_Controls.Hud.Alerts;
		for (const Alert* a : m_Alerts.Shown())
		{
			if (std::find(kinds.begin(), kinds.end(), a->Kind) == kinds.end()) continue;
			if (a->Level == AlertLevel::Important)   // (an important one goes by itself after AlertSeconds; a critical one stays until it is dismissed)
				if (const auto it = m_AlertShown.find(a->Key); it != m_AlertShown.end() && now - it->second.first > AlertSeconds) continue;
			out.push_back(a);
		}
		return out;
	}

	std::string EditorApp::AlertText(const Alert& a) const
	{
		const Lang& L = m_Lang;
		const std::string clan = L.Name("clan", a.ClanName, a.ClanName), other = L.Name("clan", a.OtherName, a.OtherName);
		if (a.Kind == "hunger") return L.Text("{0}: {1} days of food ({2} people)", clan, L.Num(a.Value, 1), static_cast<int>(a.Value2));
		if (a.Kind == "winter")
		{
			const std::string spring = L.Num(a.Value2 + a.Value, 0);
			return a.Value3 <= 0.0f ? L.Text("{0}: in the winter; has {1} d of food of {2}", clan, L.Num(a.Value2, 0), spring)
				: L.Text("{0}: winter in {1} d; has {2} d of food of {3}", clan, L.Num(a.Value3, 0), L.Num(a.Value2, 0), spring);
		}
		if (a.Kind == "war") return a.Value > 1.0f ? L.Text("{0} attacks {1} ({2} clashes)", clan, other, static_cast<int>(a.Value)) : L.Text("{0} attacks {1}", clan, other);
		if (a.Kind == "extinct") return L.Text("{0} has died out", clan);
		if (a.Kind == "inventor")
			return a.Value > 1.0f ? L.Text("Inventor ({0}): {1} ({2} discoveries)", clan, Lang::Cap(L.Line(a.What)), static_cast<int>(a.Value)) : L.Text("Inventor ({0}): {1}", clan, Lang::Cap(L.Line(a.What)));
		if (a.Kind == "split") return L.Text("{0} splits: {1} is founded", clan, other);
		return a.Text;
	}

	std::string EditorApp::ObserverAlertsText() const
	{
		const std::vector<const Alert*> shown = VisibleAlerts();
		std::string s = shown.empty() ? std::string("no alerts") : std::to_string(shown.size()) + " alert(s):";
		int n = 0;
		char where[64];
		for (const Alert* a : shown)
		{
			std::snprintf(where, sizeof where, " at (%.1f, %.1f)", static_cast<double>(a->Where.x), static_cast<double>(a->Where.y));
			s += "\n" + std::to_string(++n) + ". " + AlertLevelName(a->Level) + " " + a->Kind + " [" + a->Key + "]: " + a->Text + (a->HasWhere ? where : "");
		}
		return s + "\nraised so far: " + m_Alerts.RaisedLine() + (m_AlertPauseOff ? "; they never pause" : "");
	}

	void EditorApp::DrawObserverAlerts(float ox, float w)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const std::vector<const Alert*> shown = VisibleAlerts();
		if (shown.empty() || m_Observer.AlertsFolded) return;   // (nothing to say: nothing drawn)
		// top right, left of the book, under the bar and the follow chip (the news go under it)
		const float left = m_Observer.ClansBox.X1 > m_Observer.ClansBox.X0 ? m_Observer.ClansBox.X1 + T::Gap : ox + T::Margin;
		const float right = m_Observer.BookBox.X1 > m_Observer.BookBox.X0 ? m_Observer.BookBox.X0 - T::Gap : ox + w - T::Margin;
		const float top = std::max(m_Observer.BarBottom, m_Observer.FollowBox.Y1) + T::Gap;
		const float card = std::min(400.0f, right - left);
		const ImGuiStyle& style = ImGui::GetStyle();
		const std::string goLabel = L.Text("go"), xLabel = "x";
		const float goW = ImGui::CalcTextSize(goLabel.c_str()).x + style.FramePadding.x * 2.0f, xW = ImGui::CalcTextSize(xLabel.c_str()).x + style.FramePadding.x * 2.0f;
		const float wrap = card - 2.0f * T::Pad - 8.0f - goW - xW - 2.0f * style.ItemSpacing.x;
		if (wrap < 80.0f) return;   // (no room: the window is too narrow)
		ImGui::SetNextWindowPos(ImVec2(right, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground;
		if (ImGui::Begin("Alerts##observer", nullptr, flags))
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const size_t most = 6;   // (more than six: the rest are counted, the bar's counter says how many)
			for (size_t i = 0; i < shown.size() && i < most; ++i)
			{
				const Alert& a = *shown[i];
				const bool critical = a.Level == AlertLevel::Critical;
				const ImU32 accent = critical ? T::Bad : T::Ochre;
				const std::string text = std::string(a.Kind == "inventor" || a.Kind == "split" ? "* " : "! ") + AlertText(a);
				const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
				const float h = std::max(size.y, ImGui::GetFrameHeight()) + 12.0f;
				const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + card, min.y + h);
				T::Card(dl, min, max, accent);
				dl->AddRect(min, max, accent, T::Radius, 0, critical ? 2.0f : 1.5f);   // (the border says the level: bad for critical, ochre for important)
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + T::Pad + 4.0f, min.y + 6.0f), T::Bone, text.c_str(), nullptr, wrap);
				ImGui::PushID(a.Key.c_str());
				const float by = min.y + (h - ImGui::GetFrameHeight()) * 0.5f;
				ImGui::SetCursorScreenPos(ImVec2(max.x - T::Pad - xW - style.ItemSpacing.x - goW, by));
				if (a.HasWhere)
				{
					if (ImGui::Button((goLabel + "###go").c_str())) m_UiCommands.push_back("observer alert go " + a.Key);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The camera to where it happens").c_str());
				}
				else ImGui::Dummy(ImVec2(goW, ImGui::GetFrameHeight()));
				ImGui::SameLine();
				if (ImGui::Button((xLabel + "###dismiss").c_str())) m_UiCommands.push_back("observer alert dismiss " + a.Key);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text(critical ? "Dismiss it (it stays until you do)" : "Dismiss it (it goes by itself in a few seconds)").c_str());
				ImGui::PopID();
				ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 4.0f));
				ImGui::Dummy(ImVec2(card, 0.0f));
			}
			if (shown.size() > most) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::BoneDim), "%s", L.Text("and {0} more", static_cast<int>(shown.size() - most)).c_str());
			m_Observer.AlertsBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	// ------------------------------------------------------------------ U6: a person
	const Tribe* EditorApp::ClanOfMember(const std::string& name) const
	{
		if (name.empty()) return nullptr;
		if (m_Tribe.FindMember(name)) return &m_Tribe;
		for (const auto& c : m_Clans.All()) if (c->Folk && c->Folk->FindMember(name)) return c->Folk;
		return nullptr;
	}

	bool EditorApp::MemberWhere(const std::string& name, glm::vec3& at) const
	{
		const Tribe* t = ClanOfMember(name);
		const TribeMember* m = t ? t->FindMember(name) : nullptr;
		if (!m || m->Dead) return false;
		const glm::vec2 p = t->Position(*m);
		// its body where the simulation has it: its feet; else (simulated without it, its actor left behind) the ground under that place
		if (const Actor* a = m_Actors.Find(name); a && glm::distance(glm::vec2(a->GetPosition().x, a->GetPosition().z), p) < 2.0f) { at = a->GetPosition(); return true; }
		float y = 0.0f;
		if (m_World.IsBuilt())
			if (const std::function<float(float, float)> ground = m_World.GroundFunction()) y = ground(p.x, p.y);
		at = glm::vec3(p.x, y, p.y);
		return true;
	}

	namespace
	{
		// a parent or a partner: whether any clan knows it, whether it lives and its clan
		struct Kin { bool Found = false, Dead = false; int Clan = -1; std::string Cause; };
		Kin FindKin(const ClanWorld& clans, const std::string& name)
		{
			Kin k;
			for (const auto& c : clans.All())
				if (c->Folk)
					if (const Person* p = c->Folk->People().Find(name))
					{
						k.Found = true;
						k.Dead = p->Dead;
						k.Clan = c->Id;
						k.Cause = p->Cause;
						if (!p->Dead) break;   // (one who moved clan is kept dead in the old one: the living entry wins)
					}
			return k;
		}
	}

	std::string EditorApp::ObserverPersonText(const std::string& name)
	{
		const Tribe* t = ClanOfMember(name);
		const TribeMember* m = t ? t->FindMember(name) : nullptr;
		if (!m) return "nobody called " + name;
		int clanId = -1;
		std::string clanName;
		for (const auto& c : m_Clans.All()) if (c->Folk == t) { clanId = c->Id; clanName = c->Name; }
		const Person* p = t->People().Find(name);
		char line[256];
		std::snprintf(line, sizeof line, "person %s, clan %d %s: %s, %.1f years, %s, %s", name.c_str(), clanId, clanName.c_str(), p && p->Female ? "female" : "male", static_cast<double>(p ? p->Age : 0.0f),
			m->Role.c_str(), m->Dead ? ("dead of " + (m->Cause.empty() ? std::string("?") : m->Cause)).c_str() : "alive");
		std::string out = line;
		auto kin = [&](const char* tie, const std::string& who)
		{
			if (who.empty()) return std::string(tie) + " unknown";
			const Kin k = FindKin(m_Clans, who);
			return std::string(tie) + " " + who + " (" + (!k.Found ? std::string("not known") : (k.Dead ? "dead" : "alive") + std::string(", clan ") + std::to_string(k.Clan)) + ")";
		};
		out += "\nparents: " + kin("mother", p ? p->Mother : std::string()) + ", " + kin("father", p ? p->Father : std::string()) + (p && !p->Partner.empty() ? "; " + kin("partner", p->Partner) : std::string());
		out += "\ndoing: " + (m->Job.empty() ? std::string("idle") : m->Job) + (m->Detail.empty() ? std::string() : " " + m->Detail);
		std::string needs = "needs:";
		const std::map<std::string, float> levels = t->NeedLevels(*m);
		for (const std::string& n : m_Controls.Hud.Needs)
			if (const auto it = levels.find(n); it != levels.end()) { std::snprintf(line, sizeof line, " %s %.0f", n.c_str(), static_cast<double>(it->second)); needs += line; }
		out += "\n" + needs;
		std::snprintf(line, sizeof line, "\nmood %.2f, grief %.2f", static_cast<double>(t->Mood(name)), static_cast<double>(t->Grief(name)));
		out += line;
		std::string learning;
		const AgentKnowledge* k = t->Knowledge().Of(name);
		if (k)
			for (const auto& [tech, v] : k->Progress) { std::snprintf(line, sizeof line, "%s%s %.0f %%", learning.empty() ? "" : ", ", tech.c_str(), static_cast<double>(v * 100.0f)); learning += line; }
		out += "\nlearning: " + (learning.empty() ? std::string("nothing now") : learning);
		out += "\nknows: " + std::to_string(k ? k->Known.size() : 0) + " technologies";
		std::string memories;
		for (const HistoryEvent* e : m_Clans.Past().Recall(name, 3)) memories += (memories.empty() ? "" : "; ") + std::string("day ") + std::to_string(EventDay(e->At)) + " [" + e->Kind + "] " + e->Text;
		out += "\nremembers: " + (memories.empty() ? std::string("nothing yet") : memories);
		std::string genes;
		if (p)
			for (const auto& [g, v] : p->Genes) { std::snprintf(line, sizeof line, "%s%s %.2f", genes.empty() ? "" : ", ", g.c_str(), static_cast<double>(v)); genes += line; }
		out += "\ngenes: " + (genes.empty() ? std::string("none") : genes);
		glm::vec3 at(0.0f);
		if (MemberWhere(name, at)) { std::snprintf(line, sizeof line, "\nat (%.1f, %.1f)", static_cast<double>(at.x), static_cast<double>(at.z)); out += line; }
		return out;
	}

	// U6: a person's sheet (MODO_DIOS_UX 3.5), the Person tab: who it is (age, trade, clan), its parents and partner, what it does, its needs with
	// their number and colour, its mood, what it is learning with how far, what it knows, the three things it remembers most and its genes. Its
	// buttons follow it with the game camera, send the camera to it, or go back to the members (`camera game ...`, `observer ...`).
	void EditorApp::DrawPersonSheet(const Tribe& t, const TribeMember& m)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto label = [&](const std::string& text) { ImGui::TextDisabled("%s", text.c_str()); ImGui::SameLine(110.0f); };
		auto value = [](const std::string& text, ImU32 colour = T::Bone) { ImGui::PushStyleColor(ImGuiCol_Text, colour); ImGui::TextWrapped("%s", text.c_str()); ImGui::PopStyleColor(); };
		auto clanName = [&](int id) { const Clan* c = m_Clans.Find(static_cast<ClanId>(id)); return c ? L.Name("clan", c->Name, c->Name) : std::string(); };
		int clanId = -1;
		for (const auto& c : m_Clans.All()) if (c->Folk == &t) clanId = c->Id;
		const Person* p = t.People().Find(m.Name);
		// who: the name, the age, the trade and the clan
		if (ImGui::SmallButton((L.Text("< Members") + "###members").c_str())) run("observer person off");
		ImGui::SameLine();
		if (clanId >= 0) Swatch(ClanColour(m_Clans, static_cast<ClanId>(clanId)));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(m.Dead ? T::BoneDim : T::Ochre));
		ImGui::TextUnformatted(m.Name.c_str());
		ImGui::PopStyleColor();
		ImGui::SameLine();
		std::string who = p ? (std::lround(p->Age) == 1 ? L.Text("1 year") : L.Text("{0} years", L.Num(p->Age, 0))) + ", " : std::string();
		who += L.Name("role", m.Role) + (clanId >= 0 ? " (" + clanName(clanId) + ")" : std::string());
		ImGui::TextDisabled("%s", who.c_str());
		// T271: several people of the colony selected: how many and who (a right click gives them all the order)
		if (ColonyScreen() && m_Colony.Selection.size() > 1 && std::find(m_Colony.Selection.begin(), m_Colony.Selection.end(), m.Name) != m_Colony.Selection.end())
		{
			std::string names;
			for (const std::string& n : m_Colony.Selection) names += (names.empty() ? "" : ", ") + n;
			value(L.Text("{0} selected: {1}", m_Colony.Selection.size(), names), T::Ochre);
			ImGui::TextDisabled("%s", L.Text("Right click on something: the order goes to all of them").c_str());
		}
		// follow it, or the camera to it once
		if (m.Dead) value(m.Cause.empty() ? L.Text("dead") : L.Text("dead of {0}", L.Name("need", m.Cause)), T::Bad);
		else
		{
			const bool following = m_GameView.Following() == m.Name;
			{
				std::optional<T::PrimaryScope> lit;
				if (following) lit.emplace();
				if (ImGui::Button((L.Text(following ? "Following" : "Follow") + "###follow").c_str())) run(following ? std::string("observer follow off") : "observer follow " + m.Name);
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text(following ? "The camera follows {0}: click to let go" : "The camera follows {0} wherever it goes", m.Name).c_str());
			ImGui::SameLine();
			glm::vec3 at(0.0f);
			if (ImGui::Button((L.Text("Go to") + "###goto").c_str()) && MemberWhere(m.Name, at))
			{
				char x[32], z[32];
				std::snprintf(x, sizeof x, "%.2f", static_cast<double>(at.x));
				std::snprintf(z, sizeof z, "%.2f", static_cast<double>(at.z));
				run(std::string("camera game overview ") + x + " " + z + " 30");
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The camera to where {0} is now", m.Name).c_str());
			// T269 (MODO_COLONIA §6): in the colony, [Change trade]: the player picks it, it takes it up now and keeps it (`director role`)
			if (m_Director.Colony() && clanId >= 0 && clanId == m_Director.ColonyClan() && !m.Player)
			{
				ImGui::SameLine();
				const bool open = m_Hand.Calling == m.Name;
				{
					std::optional<T::PrimaryScope> lit;
					if (open) lit.emplace();
					if (ImGui::Button((L.Text("Change trade") + "###trade").c_str())) run(open ? "director hand calling off" : "director hand calling " + m.Name);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Pick the trade of {0}: it takes it up now and keeps it until you set it free", m.Name).c_str());
				const bool fixed = m_Director.TradeFixed(m.Name);
				if (fixed) value(L.Text("Trade fixed by you: {0}", L.Name("role", m.Role)), T::Ochre);
				if (open)
				{
					ImGui::Indent(12.0f);
					for (const auto& [role, def] : t.Config.Roles)
					{
						if (def.Fixed || def.Skill.empty()) continue;
						std::optional<T::PrimaryScope> lit;
						if (role == m.Role) lit.emplace();
						if (ImGui::SmallButton((Lang::Cap(L.Name("role", role)) + "###trade" + role).c_str()))
						{
							run("director role " + m.Name + " " + role);
							run("director hand calling off");
						}
						ImGui::SameLine();
					}
					ImGui::NewLine();
					if (fixed && ImGui::SmallButton((L.Text("Let the clan choose") + "###tradeauto").c_str()))
					{
						run("director role " + m.Name + " auto");
						run("director hand calling off");
					}
					ImGui::Unindent(12.0f);
				}
			}
			// [Calling] (T239): the god sends it a dream of a trade; the menu says, trade by trade, whether it would take it
			else if (clanId >= 0 && !m.Player && !m_Director.Colony())
			{
				ImGui::SameLine();
				const bool open = m_Hand.Calling == m.Name;
				const DirectorCheck hand = m_Director.CanSpend("calling", clanId, true);
				ImGui::BeginDisabled(!hand.Ok && !open);
				{
					std::optional<T::PrimaryScope> lit;
					if (open) lit.emplace();
					if (ImGui::Button((L.Text("Calling") + "###calling").c_str())) run(open ? "director hand calling off" : "director hand calling " + m.Name);
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", hand.Ok ? L.Text("{0} dreams of a trade: if it believes, it takes it up at the next review ({1} influence)", m.Name, L.Num(hand.Cost, 0)).c_str() : Lang::Cap(L.Line(hand.Why)).c_str());
				for (const DirectorCalling& c : m_Director.Callings())
					if (c.Person == m.Name) value(L.Text("Calling: {0}", L.Name("role", c.Role)), T::Ochre);
				if (m_Director.IsSkeptic(clanId, m.Name))   // T241: the mark of a skeptic (in the director's book)
				{
					value(L.Text("Skeptic: laughs at the god's dreams"), T::Bad);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Its vote against the god's ideas weighs double, and the rites no longer raise its devotion").c_str());
				}
				if (open)
				{
					ImGui::Indent(12.0f);
					ImGui::TextDisabled("%s", L.Text("Which trade does {0} dream of?", m.Name).c_str());
					for (const auto& [role, def] : t.Config.Roles)
					{
						if (def.Fixed || def.Skill.empty()) continue;
						const DirectorCallingPreview cp = m_Director.PreviewCalling(m.Name, role);   // (changes nothing)
						ImGui::BeginDisabled(!cp.Ok);
						if (ImGui::SmallButton((Lang::Cap(L.Name("role", role)) + "###call" + role).c_str()))
						{
							run("director calling " + m.Name + " " + role);
							run("director hand calling off");
						}
						ImGui::EndDisabled();
						ImGui::SameLine(130.0f);
						if (!cp.Ok) ImGui::TextDisabled("%s", Lang::Cap(L.Line(cp.Why)).c_str());
						else if (cp.Accepts) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", L.Text("would accept (devotion {0}, skill {1} against {2})", L.Num(m_Director.DevotionOf(clanId, m.Name), 2), L.Num(cp.Skill, 0), L.Num(cp.Own, 0)).c_str());
						else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", L.Text("would not: it does not believe enough (devotion {0})", L.Num(m_Director.DevotionOf(clanId, m.Name), 2)).c_str());
					}
					ImGui::Unindent(12.0f);
				}
			}
		}
		ImGui::Separator();
		// kin
		auto kinText = [&](const std::string& name) -> std::pair<std::string, ImU32>
		{
			if (name.empty()) return { L.Text("unknown"), T::BoneDim };
			const Kin k = FindKin(m_Clans, name);
			std::string s = name;
			if (k.Found && k.Clan != clanId) s += " (" + clanName(k.Clan) + ")";
			if (k.Dead) s += " (" + L.Text("dead") + ")";
			return { s, k.Dead ? T::BoneDim : T::Bone };
		};
		if (p)
		{
			label(L.Text("Mother"));
			const auto mother = kinText(p->Mother);
			value(mother.first, mother.second);
			label(L.Text("Father"));
			const auto father = kinText(p->Father);
			value(father.first, father.second);
			if (!p->Partner.empty())
			{
				label(L.Text("Partner"));
				const auto partner = kinText(p->Partner);
				value(partner.first, partner.second);
			}
		}
		if (m.Dead) return;
		label(L.Text("Doing"));
		value(L.Doing(m.Job, m.Detail));
		// T308: and why (its order, the reason of its decision); in the colony, whether it follows the player's table and its row of priorities
		if (const std::string why = PersonWhy(t, m); !why.empty())
		{
			label(L.Text("Why"));
			value(why, T::BoneDim);
		}
		const bool colonyOwn = ColonyScreen() && clanId >= 0 && clanId == m_Director.ColonyClan() && !m.Player && !t.Config.Work.Categories.Empty();
		if (colonyOwn) DrawPersonWork(t, m);
		// needs: the HUD's, each with its icon, a bar with its number and its colour (good, warning, bad) and the danger mark at 25
		ImGui::Separator();
		const std::map<std::string, float> levels = t.NeedLevels(m);
		auto needLabel = [&](const char* icon, const std::string& text)
		{
			const ImVec2 at = ImGui::GetCursorPos();
			if (Icon(icon, ImGui::GetTextLineHeight())) ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextDisabled("%s", text.c_str());
			ImGui::SameLine(at.x + 110.0f);
		};
		static const std::pair<const char*, const char*> kNeedIcons[] = { { "Hunger", "state.hungry" }, { "Thirst", "state.thirsty" }, { "Rest", "state.tired" },
			{ "Warmth", "state.cold" }, { "Health", "state.wounded" } };
		for (const std::string& n : m_Controls.Hud.Needs)
		{
			const auto it = levels.find(n);
			if (it == levels.end()) continue;
			const float v = it->second, v01 = std::clamp(v / 100.0f, 0.0f, 1.0f);
			const char* icon = "";
			for (const auto& [need, id] : kNeedIcons) if (n == need) icon = id;
			needLabel(icon, Lang::Cap(L.Name("need", n, n)));
			const ImVec2 b0 = ImGui::GetCursorScreenPos();
			SmallBar(v01, 110.0f, T::Level(v01));
			const float mark = b0.x + 110.0f * 0.25f, h = ImGui::GetTextLineHeight();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(mark, b0.y + (h - 10.0f) * 0.5f), ImVec2(mark, b0.y + (h + 10.0f) * 0.5f + 2.0f), T::Outline, 2.0f);
			ImGui::SameLine();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Level(v01)), "%s", L.Num(v, 0).c_str());
			if (v01 <= 0.25f) { ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", L.Text("danger").c_str()); }
		}
		// the mood (0..1) as a bar too, its low line (the log's) marked
		needLabel("state.happy", L.Text("Mood"));
		const float mood = t.Mood(m.Name), grief = t.Grief(m.Name), low = t.Config.Social.Mood.Low;
		const ImU32 moodColour = mood < low ? T::Bad : mood < 0.5f ? T::Warn : T::Good;
		{
			const ImVec2 b0 = ImGui::GetCursorScreenPos();
			SmallBar(std::clamp(mood, 0.0f, 1.0f), 110.0f, moodColour);
			const float mark = b0.x + 110.0f * std::clamp(low, 0.0f, 1.0f), lh = ImGui::GetTextLineHeight();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(mark, b0.y + (lh - 10.0f) * 0.5f), ImVec2(mark, b0.y + (lh + 10.0f) * 0.5f + 2.0f), T::Outline, 2.0f);
		}
		ImGui::SameLine();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(moodColour), "%s", (L.Num(mood * 100.0f, 0) + (grief > 0.01f ? "   " + L.Text("grief {0}", L.Num(grief, 2)) : std::string())).c_str());
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Mood, 0 to 100; under {0} it is low, and low spirits make it drop an order", L.Num(low * 100.0f, 0)).c_str());
		// what it is learning (and how far), and what it knows
		ImGui::Separator();
		const AgentKnowledge* k = t.Knowledge().Of(m.Name);
		std::vector<std::pair<float, std::string>> learning;
		if (k) for (const auto& [tech, v] : k->Progress) learning.push_back({ v, tech });
		std::sort(learning.rbegin(), learning.rend());
		label(L.Text("Learning"));
		if (learning.empty()) value(L.Text("nothing now"), T::BoneDim);
		else
		{
			ImGui::NewLine();
			for (const auto& [v, tech] : learning)
			{
				ImGui::Indent(12.0f);
				SmallBar(v, 80.0f, T::News);
				ImGui::SameLine();
				ImGui::TextUnformatted((L.Num(v * 100.0f, 0) + " %  " + Lang::Cap(L.Name("tech", tech, t.Knowledge().Name(tech)))).c_str());
				ImGui::Unindent(12.0f);
			}
		}
		label(L.Text("Knows"));
		value(L.Text("{0} technologies", k ? static_cast<int>(k->Known.size()) : 0));
		// what it remembers most (the weightiest, then the newest)
		ImGui::Separator();
		label(L.Text("Remembers"));
		const std::vector<const HistoryEvent*> memories = m_Clans.Past().Recall(m.Name, 3);
		if (memories.empty()) value(L.Text("nothing yet"), T::BoneDim);
		else
		{
			ImGui::NewLine();
			for (const HistoryEvent* e : memories)
			{
				ImGui::Indent(12.0f);
				ImGui::TextDisabled("%s", L.Text("Day {0}", EventDay(e->At)).c_str());
				ImGui::SameLine();
				ImGui::TextWrapped("%s", Lang::Cap(L.Line(e->Text)).c_str());
				ImGui::Unindent(12.0f);
			}
		}
		// genes (0..1, the mean is a half), two to a row
		ImGui::Separator();
		ImGui::TextDisabled("%s", L.Text("Genes").c_str());
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("What it was born with, 0 to 1 (a half is the usual); its children inherit them").c_str());
		if (p && ImGui::BeginTable("genes", 4, ImGuiTableFlags_SizingFixedFit))
		{
			int i = 0;
			for (const auto& [g, v] : p->Genes)
			{
				if (i++ % 2 == 0) ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", Lang::Cap(L.Name("gene", g, g)).c_str());
				ImGui::TableNextColumn();
				SmallBar(v, 50.0f, T::Ochre);
				ImGui::SameLine();
				ImGui::TextUnformatted(L.Num(v, 2).c_str());
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::Dummy(ImVec2(10.0f, 1.0f));
			}
			ImGui::EndTable();
		}
	}

	// U6: whom the camera follows, under the god's bar between the list and the book: its name and clan, its sheet and the way to let it go
	void EditorApp::DrawFollowChip(float ox, float w)
	{
		namespace T = GameTheme;
		const std::string who = m_GameView.Following();
		if (who.empty()) return;
		const Lang& L = m_Lang;
		const Tribe* t = ClanOfMember(who);
		std::string clan;
		for (const auto& c : m_Clans.All()) if (t && c->Folk == t) clan = L.Name("clan", c->Name, c->Name);
		const float left = m_Observer.ClansBox.X1 > m_Observer.ClansBox.X0 ? m_Observer.ClansBox.X1 + T::Gap : ox + T::Margin;
		const float right = m_Observer.BookBox.X1 > m_Observer.BookBox.X0 ? m_Observer.BookBox.X0 - T::Gap : ox + w - T::Margin;
		ImGui::SetNextWindowPos(ImVec2((left + right) * 0.5f, m_Observer.BarBottom + T::Gap), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(std::max(right - left, 60.0f), FLT_MAX));   // (centred and never wider than the gap: it cannot reach the list nor the book)
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
		if (ImGui::Begin("Follow##observer", nullptr, flags))
		{
			// an eye: a ring and its pupil (the font has no icons), then the name in ochre
			const float s = ImGui::GetTextLineHeight();
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(s, s));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddEllipse(ImVec2(at.x + s * 0.5f, at.y + s * 0.5f + 1.0f), ImVec2(s * 0.5f, s * 0.3f), T::Ochre, 0.0f, 0, 1.6f);
			dl->AddCircleFilled(ImVec2(at.x + s * 0.5f, at.y + s * 0.5f + 1.0f), s * 0.17f, T::Ochre);
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", (L.Text("Following {0}", who) + (clan.empty() ? std::string() : " (" + clan + ")")).c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton((L.Text("Sheet") + "###sheet").c_str())) m_UiCommands.push_back("observer person " + who);
			ImGui::SameLine();
			if (ImGui::SmallButton((L.Text("Let go") + "###letgo").c_str())) m_UiCommands.push_back("observer follow off");
			m_Observer.FollowBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		}
		ImGui::End();
	}

	// ------------------------------------------------------------------ T201: the god's hand (MODO_DIOS_DECISIONES 3.a, MODO_DIOS_UX 5)
	// The bar at the bottom (the influence as a number and a bar, the faith and the pressure of the clan under the hand, [Build], [Omen] and the
	// hand's next decisions greyed with "soon"), the palette of what the clan knows how to raise, and the building chosen as a ghost footprint over
	// the ground that follows the mouse, Black & White's way: green where the sign can go, with its cost and the vote foreseen, red with why not.
	// A click queues `director sign <clan> <building> <x> <z> <yaw>` (the replay keeps it); the right click or Esc lets it go. Each sign has a ring
	// and a stake on its place with its state, and a toast when it changes (GB.exe's shell draws the toasts).
	namespace
	{
		std::string Fixed(float v)   // (a number inside an order: a point, never the Spanish comma of Lang::Num)
		{
			char s[32];
			std::snprintf(s, sizeof s, "%.2f", static_cast<double>(v));
			return s;
		}

		bool Number(const std::string& w, float& out)
		{
			char* end = nullptr;
			out = std::strtof(w.c_str(), &end);
			return end != w.c_str() && *end == '\0' && std::isfinite(out);
		}

		const char* SignStateText(const std::string& status)
		{
			if (status == "waiting") return "Waiting for someone of the clan to see it";
			if (status == "seen") return "Seen: the clan talks it over at the fire";
			if (status == "accepted") return "Accepted: the site is open";
			if (status == "built") return "It stands";
			if (status == "refused") return "Refused by the clan";
			if (status == "unseen") return "Nobody saw it";
			return "It came to nothing";
		}

		ImU32 SignColour(const std::string& status)
		{
			namespace T = GameTheme;
			if (status == "waiting") return T::Ochre;
			if (status == "seen") return T::Warn;
			if (status == "accepted" || status == "built") return T::Good;
			return T::Bad;
		}

		const char* FocusGroupText(const std::string& group)   // T238 (the text of the group, through Lang)
		{
			if (group == "food") return "food";
			if (group == "build") return "works";
			if (group == "lore") return "lore";
			if (group == "defense") return "defense";
			if (group == "faith") return "faith";
			return group.c_str();
		}

		const char* OutcomeText(const std::string& outcome)
		{
			if (outcome == "obey") return "they would agree";
			if (outcome == "doubt") return "they would doubt";
			return "they would refuse";
		}

		// T254: the grid's categories, worked out from what a building does (the data has no category of its own): the first rule that holds wins
		const char* const kBuildTabs[] = { "all", "homes", "food", "crafts", "fire_water", "worship", "defense", "community" };

		std::string BuildingTab(const BuildingDef& d)
		{
			if (d.Well || d.Station == "hearth") return "fire_water";
			if (d.Shelter > 0) return "homes";
			if (d.Defense >= 0.3f || d.Healing > 0.0f) return "defense";
			if (!d.Station.empty()) return d.Zone == "food" ? "food" : "crafts";
			if (!d.Store.empty() || d.StoreCap > 0 || d.FoodCap > 0) return "food";
			if (!d.Rank.empty()) return "community";
			if (d.Culture > 0.0f || d.Provides.count("faith")) return "worship";
			return "community";
		}

		const char* BuildTabText(const std::string& tab)
		{
			if (tab == "homes") return "Homes";
			if (tab == "food") return "Food and stores";
			if (tab == "crafts") return "Crafts";
			if (tab == "fire_water") return "Fire and water";
			if (tab == "worship") return "Worship";
			if (tab == "defense") return "Defence and healing";
			if (tab == "community") return "Community";
			return "All";
		}

		// T254: why a clan cannot raise a building yet (Tribe::ClanKnowsBuilding's rules, read back): the rank it lacks, the value of its culture,
		// the technologies that would open it (any one of them)
		struct BuildLock
		{
			std::string Rank, Value;
			bool TooMuch = false;
			std::vector<std::string> Techs;
		};

		BuildLock LockOf(const Tribe& t, const BuildingDef& d)
		{
			BuildLock k;
			if (!d.Rank.empty() && !t.Ranks().count(d.Rank)) k.Rank = d.Rank;
			for (const auto& [value, threshold] : d.Values)
			{
				const auto it = t.Culture().find(value);
				const int have = it != t.Culture().end() ? it->second : 0;
				if (it == t.Culture().end() || (threshold > 0 ? have < threshold : have > threshold)) { k.Value = value; k.TooMuch = threshold < 0; }
			}
			if (!d.Tech.empty()) k.Techs.push_back(d.Tech);
			if (const TechTree* tree = t.Knowledge().Techs())
				for (const auto& [id, tech] : tree->Techs)
					if ((std::find(tech.Buildings.begin(), tech.Buildings.end(), d.Id) != tech.Buildings.end() || std::find(tech.Buildings.begin(), tech.Buildings.end(), d.Name) != tech.Buildings.end())
						&& std::find(k.Techs.begin(), k.Techs.end(), id) == k.Techs.end())
						k.Techs.push_back(id);
			return k;
		}

		std::string LockText(const BuildLock& k)   // (the order's answer, in English like every answer)
		{
			std::string s;
			if (!k.Rank.empty()) s += "the clan must be a " + k.Rank + " first";
			if (!k.Value.empty()) s += (s.empty() ? "" : "; ") + std::string(k.TooMuch ? "its culture values " : "its culture does not value ") + k.Value + (k.TooMuch ? " too much" : " enough");
			if (!k.Techs.empty())
			{
				std::string techs;
				for (const std::string& id : k.Techs) techs += (techs.empty() ? "" : " or ") + id;
				s += (s.empty() ? "" : "; ") + std::string("it needs ") + techs;
			}
			return s.empty() ? std::string("unknown") : s;
		}

		ImTextureID ThumbId(const Texture2D& t) { return static_cast<ImTextureID>(static_cast<intptr_t>(t.GetID())); }
	}

	CommandResult EditorApp::HandCommand(const std::vector<std::string>& a)
	{
		if (a[0] == "ghost")   // director ghost <clan> <building> <x> <z>|camp <dx> <dz> [yaw] | ghost off: the ghost as if the cursor were there
		{
			const char* usage = "director ghost <clan> <building> <x> <z>|camp <dx> <dz> [yaw] | ghost off";
			if (a.size() > 1 && a[1] == "off") { m_Hand.Pinned = false; m_Hand.Building.clear(); return { true, "ghost off" }; }
			if (a.size() < 5) return { false, usage };
			const Clan* c = m_Clans.Find(a[1]);
			if (!c || !c->Folk) return { false, "no clan " + a[1] };
			if (!c->Folk->Buildings() || !c->Folk->Buildings()->count(a[2])) return { false, "no building " + a[2] };
			const bool camp = a[3] == "camp";
			const size_t n = camp ? 4 : 3;
			float x = 0.0f, z = 0.0f, yaw = m_Hand.Yaw;
			if (a.size() < n + 2 || !Number(a[n], x) || !Number(a[n + 1], z) || (a.size() > n + 2 && !Number(a[n + 2], yaw))) return { false, usage };
			if (camp) m_Director.CampOffset(c->Id, x, z, x, z);
			m_ActiveClan = c->Id;
			m_Observer.On = true;
			m_Hand.Menu.clear();
			m_Hand.Building = a[2];
			m_Hand.Pinned = true;
			m_Hand.PinX = x;
			m_Hand.PinZ = z;
			m_Hand.Yaw = yaw;
			return { true, "ghost " + a[2] + " at " + Fixed(x) + " " + Fixed(z) + " yaw " + Fixed(yaw) + " for the clan " + std::to_string(c->Id) };
		}
		const std::string sub = a.size() > 1 ? a[1] : "status";
		const char* usage = "director hand status|build [all|homes|food|crafts|fire_water|worship|defense|community]|omen|focus|calling <person>|off|place <building>|turn [degrees]|tip <sign>|peek <building>";
		if (sub == "status") return { true, HandStatus() };
		if (sub == "calling")   // T239: the menu of callings on a person's sheet (`calling off` closes it)
		{
			if (a.size() < 3) return { false, usage };
			if (a[2] == "off") { m_Hand.Calling.clear(); return { true, "hand: calling menu closed" }; }
			m_Hand.Calling = a[2];
			return { true, "hand: calling menu for " + a[2] };
		}
		if (sub == "off")
		{
			m_Hand.Calling.clear();
			m_Hand.Menu.clear();
			m_Hand.Building.clear();
			m_Hand.Pinned = false;
			m_Hand.Tip = 0;
			m_Hand.Peek.clear();
			return { true, "hand: empty" };
		}
		if (sub == "peek")   // T254: the card of a grid cell held open, as if the mouse were over it (`peek off` lets it go)
		{
			if (a.size() < 3) return { false, usage };
			m_Hand.Peek = a[2] == "off" ? std::string() : a[2];
			return { true, m_Hand.Peek.empty() ? std::string("hand: peek off") : "hand: the card of " + a[2] + " held open" };
		}
		if (m_Clans.Count() == 0) return { false, "no clans (shell new, or tribe build and clan new)" };
		const Clan* c = m_Clans.Find(HandClan());   // (T269: the player's own in the colony)
		if (!c || !c->Folk) return { false, "no clan under the hand" };
		if (sub == "build" || sub == "omen" || sub == "focus")
		{
			if (sub != "build" && m_Director.Colony()) return { false, "hand: no " + sub + " in the colony game" };   // T269
			if (sub == "build" && a.size() > 2)   // T254: the grid's category
			{
				if (std::find_if(std::begin(kBuildTabs), std::end(kBuildTabs), [&](const char* t) { return a[2] == t; }) == std::end(kBuildTabs)) return { false, usage };
				m_Hand.Tab = a[2];
			}
			m_Hand.Menu = sub;
			m_Hand.Building.clear();
			m_Hand.Pinned = false;
			m_Observer.On = true;
			return { true, "hand: " + sub + " menu for the clan " + std::to_string(c->Id) + (sub == "build" ? ", category " + m_Hand.Tab : std::string()) };
		}
		if (sub == "place")
		{
			if (a.size() < 3) return { false, usage };
			if (!c->Folk->Buildings() || !c->Folk->Buildings()->count(a[2])) return { false, "no building " + a[2] };
			if (!c->Folk->CanBuild(a[2])) return { false, "the clan " + std::to_string(c->Id) + " cannot raise " + a[2] + " yet: " + LockText(LockOf(*c->Folk, c->Folk->Buildings()->at(a[2]))) };
			m_Hand.Peek.clear();
			m_Hand.Menu.clear();
			m_Hand.Building = a[2];
			m_Hand.Pinned = false;
			return { true, "hand: placing " + a[2] + " for the clan " + std::to_string(c->Id) + " (click on the ground)" };
		}
		if (sub == "turn")
		{
			float step = 45.0f;
			if (a.size() > 2 && !Number(a[2], step)) return { false, usage };
			m_Hand.Yaw = std::fmod(m_Hand.Yaw + step + 3600.0f, 360.0f);
			return { true, "hand: yaw " + Fixed(m_Hand.Yaw) };
		}
		if (sub == "tip")
		{
			if (a.size() < 3) return { false, usage };
			m_Hand.Tip = a[2] == "off" ? 0 : std::atoi(a[2].c_str());
			return { true, m_Hand.Tip ? "hand: the card of the sign " + std::to_string(m_Hand.Tip) + " held open" : std::string("hand: tip off") };
		}
		return { false, usage };
	}

	std::string EditorApp::HandStatus() const
	{
		std::string s = "hand: clan " + std::to_string(HandClan()) + ", menu " + (m_Hand.Menu.empty() ? std::string("none") : m_Hand.Menu);
		if (m_Hand.Building.empty()) return s + ", placing nothing";
		s += ", placing " + m_Hand.Building + " yaw " + Fixed(m_Hand.Yaw) + (m_Hand.Pinned ? " (pinned)" : "");
		if (!m_Hand.Has) return s + ", off the ground";
		const DirectorSignPreview& p = m_Hand.Preview;
		s += " at " + Fixed(m_Hand.X) + " " + Fixed(m_Hand.Z) + ": ";
		if (!p.Ok) return s + "red: " + p.Why;
		if (m_Director.Colony()) return s + "green" + (p.Warning.empty() ? std::string() : "; " + p.Warning);   // T269: no cost, no vote
		return s + "green, cost " + Fixed(p.Cost) + ", vote " + std::to_string(p.Yes) + " of " + std::to_string(p.Voters) + " (" + p.Outcome + ")" + (p.Warning.empty() ? std::string() : "; " + p.Warning);
	}

	const DirectorSignPreview& EditorApp::HandPreview(int clan, float x, float z)
	{
		const DirectorSign* open = m_Director.OpenSign(clan);
		const int kx = static_cast<int>(std::floor(x)), kz = static_cast<int>(std::floor(z));
		const int ko = m_Director.Colony() ? (m_Director.Signs().empty() ? 0 : m_Director.Signs().back().Id) : open ? open->Id : 0;   // (T269: a plan placed takes the spot)
		const int ki = static_cast<int>(std::floor(m_Director.Influence()));
		const int ky = static_cast<int>(std::lround(m_Hand.Yaw * 10.0f));   // (T269: the turn changes the footprint)
		if (!m_Hand.Cached || kx != m_Hand.KeyX || kz != m_Hand.KeyZ || clan != m_Hand.KeyClan || ko != m_Hand.KeyOpen || ki != m_Hand.KeyInfluence || ky != m_Hand.KeyYaw || m_Hand.Building != m_Hand.KeyBuilding)
		{
			// (once per change of the key, never every frame: it touches the clan's caches). T269: the colony's [Build] has no cost and no vote
			m_Hand.Preview = m_Director.Colony() ? m_Director.PreviewBuild(clan, m_Hand.Building, x, z, m_Hand.Yaw) : m_Director.PreviewSign(clan, m_Hand.Building, x, z);
			m_Hand.KeyYaw = ky;
			m_Hand.Cached = true;
			m_Hand.KeyX = kx;
			m_Hand.KeyZ = kz;
			m_Hand.KeyClan = clan;
			m_Hand.KeyOpen = ko;
			m_Hand.KeyInfluence = ki;
			m_Hand.KeyBuilding = m_Hand.Building;
		}
		return m_Hand.Preview;
	}

	void EditorApp::HandToasts()
	{
		const Lang& L = m_Lang;
		const double now = m_Director.Targets().Now ? m_Director.Targets().Now() : 0.0;
		std::set<int> alive;
		for (const DirectorSign& s : m_Director.Signs())
		{
			alive.insert(s.Id);
			const auto it = m_Hand.SignSeen.find(s.Id);
			const bool fresh = it == m_Hand.SignSeen.end();
			if (!fresh && it->second == s.Status) continue;
			m_Hand.SignSeen[s.Id] = s.Status;
			if (fresh && !(s.Status == "waiting" && now - s.Posted < 5.0)) continue;   // (a loaded world's signs: no toast for what was already so)
			if (m_Director.Colony())   // T269: the player's plans: only when one stands (no messenger, no vote to tell)
			{
				if (s.Status == "built")
				{
					const Clan* c = m_Clans.Find(static_cast<ClanId>(s.Clan));
					ShellToastShown(L.Text("{0}: the {1} is built", c ? L.Name("clan", c->Name, c->Name) : std::to_string(s.Clan), L.Name("building", s.Building, s.Building)));
				}
				continue;
			}
			const Clan* c = m_Clans.Find(static_cast<ClanId>(s.Clan));
			const std::string clan = c ? L.Name("clan", c->Name, c->Name) : std::to_string(s.Clan);
			const std::string b = L.Name("building", s.Building, s.Building);
			std::string text;
			if (s.Status == "waiting") text = L.Text("{0}: the sign is up ({1}); someone of the clan must see it", clan, b);
			else if (s.Status == "seen") text = L.Text("{0}: {1} saw the sign ({2})", clan, s.Messenger, b);
			else if (s.Status == "accepted") text = L.Text("{0} agree to raise it: {1} ({2} of {3})", clan, b, s.Yes, s.Voters);
			else if (s.Status == "refused") text = L.Text("{0} refuse the sign: {1} ({2} of {3})", clan, b, s.Yes, s.Voters) + (s.Against.empty() ? std::string() : ": " + L.Text(s.Against));
			else if (s.Status == "built") text = L.Text("{0}: the sign stands ({1})", clan, b);
			else if (s.Status == "unseen") text = L.Text("{0}: nobody saw the sign ({1})", clan, b);
			else text = L.Text("{0}: the sign came to nothing ({1})", clan, b);
			ShellToastShown(text);
		}
		for (auto it = m_Hand.SignSeen.begin(); it != m_Hand.SignSeen.end();) it = alive.count(it->first) ? std::next(it) : m_Hand.SignSeen.erase(it);
	}

	void EditorApp::DrawHandWorld(float ox, float oy, float w, float h)
	{
		namespace T = GameTheme;
		HandToasts();
		m_Hand.Has = false;
		if (!m_World.IsBuilt() || m_Clans.Count() == 0) return;
		const Lang& L = m_Lang;
		const ImGuiIO& io = ImGui::GetIO();
		ImDrawList* dl = ImGui::GetWindowDrawList();   // (the viewport's: the panels drawn after it come over the ghost)
		const T::Scope theme;
		const Terrain& terrain = m_World.GetTerrain();
		const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
		auto screen = [&](float x, float z, float lift, ImVec2& out)
		{
			glm::vec2 s;
			if (!ProjectToScreen(viewProj, glm::vec3(x, terrain.GetHeight(x, z) + lift, z), glm::vec2(w, h), s)) return false;
			out = ImVec2(ox + s.x, oy + s.y);
			return true;
		};
		const ImGuiWindowFlags card = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
		const ImVec2 mouse = io.MousePos;

		// ---- the signs on their places: a ring on the ground and a stake, in the colour of their state (a refused one for a day after the vote)
		const double now = m_Director.Targets().Now ? m_Director.Targets().Now() : 0.0;
		const DirectorSign* tip = nullptr;
		ImVec2 tipAt;
		for (const DirectorSign& s : m_Director.Signs())
		{
			if (m_Director.Colony()) break;   // T269: no signs in the colony (a plan is its site, drawn as one)
			const bool recent = s.Status == "refused" && now - s.VotedAt < static_cast<double>(m_Director.Rules.DaySeconds);
			if (!s.Open() && s.Status != "accepted" && !recent) continue;
			const ImU32 col = SignColour(s.Status);
			ImVec2 ring[24], foot, top;
			bool ok = screen(s.X, s.Z, 0.0f, foot) && screen(s.X, s.Z, 2.4f, top);
			for (int i = 0; i < 24 && ok; ++i)
			{
				const float t = static_cast<float>(i) * 6.2831853f / 24.0f;
				ok = screen(s.X + 1.8f * std::cos(t), s.Z + 1.8f * std::sin(t), 0.15f, ring[i]);
			}
			if (!ok) continue;
			dl->AddPolyline(ring, 24, T::Outline, ImDrawFlags_Closed, 4.0f);
			dl->AddPolyline(ring, 24, col, ImDrawFlags_Closed, 2.0f);
			dl->AddLine(foot, top, T::Outline, 5.0f);
			dl->AddLine(foot, top, col, 2.5f);
			dl->AddCircleFilled(top, 5.0f, col);
			dl->AddCircle(top, 5.0f, T::Outline, 0, 1.5f);
			const float dx = mouse.x - top.x, dy = mouse.y - (top.y + foot.y) * 0.5f;
			if (m_Hand.Tip == s.Id || (m_ViewportHovered && dx * dx + dy * dy < 400.0f)) { tip = &s; tipAt = top; }
		}
		if (tip)
		{
			const Clan* c = m_Clans.Find(static_cast<ClanId>(tip->Clan));
			ImGui::SetNextWindowPos(ImVec2(tipAt.x + 14.0f, tipAt.y - 8.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
			if (ImGui::Begin("Sign##hand", nullptr, card))
			{
				ImGui::TextUnformatted(L.Text("Sign of the god: {0}", L.Name("building", tip->Building, tip->Building)).c_str());
				if (c) ImGui::TextDisabled("%s", L.Name("clan", c->Name, c->Name).c_str());
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(SignColour(tip->Status)), "%s", L.Text(SignStateText(tip->Status)).c_str());
				if (!tip->Messenger.empty())
					ImGui::TextUnformatted((tip->Status == "waiting" ? L.Text("Who will likely see it: {0}", tip->Messenger) : L.Text("Messenger: {0}", tip->Messenger)).c_str());
				if (tip->Voters > 0) ImGui::TextUnformatted(L.Text("Vote at the fire: {0} of {1}", tip->Yes, tip->Voters).c_str());
				if (tip->Status == "refused" && !tip->Against.empty())   // D43: why they said no
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", L.Text("Why not: {0}", L.Text(tip->Against)).c_str());
			}
			ImGui::End();
		}

		// ---- the ghost of the building being placed
		if (m_Hand.Building.empty()) { m_Hand.DownL = m_Hand.DownR = false; return; }
		auto press = [&](int button, bool& down, glm::vec2& at)   // a click that did not drag the camera
		{
			if (m_ViewportHovered && ImGui::IsMouseClicked(button)) { down = true; at = { mouse.x, mouse.y }; }
			if (!down || !ImGui::IsMouseReleased(button)) return false;
			down = false;
			return m_ViewportHovered && glm::distance(at, glm::vec2(mouse.x, mouse.y)) < 5.0f;
		};
		if (press(1, m_Hand.DownR, m_Hand.PressR)) { m_UiCommands.push_back("director hand off"); return; }
		if (m_ViewportHovered && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_R, false)) m_UiCommands.push_back(io.KeyShift ? "director hand turn -45" : "director hand turn 45");
		const Clan* clan = m_Clans.Find(HandClan());   // (T269: the player's own in the colony)
		if (!clan || !clan->Folk || !clan->Folk->Buildings()) return;
		const auto def = clan->Folk->Buildings()->find(m_Hand.Building);
		if (def == clan->Folk->Buildings()->end()) return;
		glm::vec3 ground(m_Hand.PinX, 0.0f, m_Hand.PinZ);
		if (!m_Hand.Pinned && !GroundUnderCursor(ground)) return;
		// on the metre's grid: what is shown green is where the sign goes
		const float x = m_Hand.Pinned ? m_Hand.PinX : std::floor(ground.x) + 0.5f, z = m_Hand.Pinned ? m_Hand.PinZ : std::floor(ground.z) + 0.5f;
		m_Hand.Has = true;
		m_Hand.X = x;
		m_Hand.Z = z;
		const DirectorSignPreview& p = HandPreview(clan->Id, x, z);
		const ImU32 col = p.Ok ? T::Good : T::Bad;
		const float yaw = glm::radians(m_Hand.Yaw), cy = std::cos(yaw), sy = std::sin(yaw);
		const glm::vec2 half = def->second.Footprint * 0.5f;
		auto turned = [&](float lx, float lz) { return glm::vec2(x + lx * cy + lz * sy, z - lx * sy + lz * cy); };
		const glm::vec2 corner[4] = { { -half.x, -half.y }, { half.x, -half.y }, { half.x, half.y }, { -half.x, half.y } };
		ImVec2 outline[24], roof[4];
		bool ok = true;
		for (int k = 0; k < 4 && ok; ++k)
		{
			for (int i = 0; i < 6 && ok; ++i)   // (6 points an edge: the footprint lies on the slope)
			{
				const glm::vec2 l = glm::mix(corner[k], corner[(k + 1) % 4], static_cast<float>(i) / 6.0f);
				const glm::vec2 g = turned(l.x, l.y);
				ok = screen(g.x, g.y, 0.15f, outline[k * 6 + i]);
			}
			const glm::vec2 g = turned(corner[k].x, corner[k].y);
			ok = ok && screen(g.x, g.y, 2.4f, roof[k]);   // (a ghost of its walls: the size reads at any distance)
		}
		ImVec2 centre, front;
		const glm::vec2 f = turned(0.0f, half.y + 0.8f);
		if (ok && screen(x, z, 0.15f, centre) && screen(f.x, f.y, 0.15f, front))
		{
			for (int k = 0; k < 4; ++k)
			{
				const ImVec2 wall[4] = { outline[k * 6], outline[((k + 1) % 4) * 6], roof[(k + 1) % 4], roof[k] };
				dl->AddConvexPolyFilled(wall, 4, T::Faded(col, 0.12f));
				dl->AddLine(outline[k * 6], roof[k], T::Faded(col, 0.8f), 1.5f);
			}
			dl->AddPolyline(roof, 4, T::Faded(col, 0.8f), ImDrawFlags_Closed, 1.5f);
			dl->AddConvexPolyFilled(outline, 24, T::Faded(col, 0.30f));
			dl->AddPolyline(outline, 24, T::Outline, ImDrawFlags_Closed, 4.5f);
			dl->AddPolyline(outline, 24, col, ImDrawFlags_Closed, 2.5f);
			dl->AddLine(centre, front, col, 2.5f);   // (its front: where the door would face)
			dl->AddCircleFilled(front, 4.0f, col);
		}
		else centre = mouse;
		// beside the cursor (or the pinned ghost): what it costs and the vote foreseen, or why not
		const ImVec2 at = m_Hand.Pinned ? centre : mouse;
		const bool flipX = at.x > ox + w * 0.62f, flipY = at.y > oy + h * 0.7f;   // (the card turns to the side with room: it never leaves the window)
		ImGui::SetNextWindowPos(ImVec2(at.x + (flipX ? -22.0f : 22.0f), at.y + (flipY ? -18.0f : 18.0f)), ImGuiCond_Always, ImVec2(flipX ? 1.0f : 0.0f, flipY ? 1.0f : 0.0f));
		if (ImGui::Begin("Ghost##hand", nullptr, card))
		{
			ImGui::TextUnformatted(Lang::Cap(L.Name("building", def->first, def->second.Name)).c_str());
			if (p.Ok && m_Director.Colony())   // T269: a plan, at once: no cost, no vote
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", L.Text("The clan raises it here with its own material").c_str());
				if (!p.Warning.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", Lang::Cap(L.Line(p.Warning)).c_str());
				ImGui::TextDisabled("%s", L.Text("[Click] place it  [R] turn  [Right click] stop").c_str());
			}
			else if (p.Ok)
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", (L.Text("A sign here: {0} influence", L.Num(p.Cost, 0)) + (p.SiteOnly ? " " + L.Text("(they wanted it: only the place)") : std::string())).c_str());
				if (p.Voters > 0) ImGui::TextUnformatted(L.Text("Vote foreseen: {0} of {1}, {2}", p.Yes, p.Voters, L.Text(OutcomeText(p.Outcome))).c_str());
				if (p.Voters > 0 && p.Outcome != "obey" && !p.Against.empty())   // D43: why they would not
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("Why not: {0}", L.Text(p.Against)).c_str());
				if (!p.Messenger.empty()) ImGui::TextDisabled("%s", L.Text("Who will likely see it: {0}", p.Messenger).c_str());
				if (!p.Warning.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", Lang::Cap(L.Line(p.Warning)).c_str());
				ImGui::TextDisabled("%s", L.Text("[Click] place the sign  [R] turn  [Right click] cancel").c_str());
			}
			else
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", Lang::Cap(L.Line(p.Why)).c_str());
				ImGui::TextDisabled("%s", L.Text("[Right click] or [Esc] cancel").c_str());
			}
		}
		ImGui::End();
		if (m_Hand.Pinned) return;   // (a pinned ghost is a script's: its sign is `director sign`)
		if (press(0, m_Hand.DownL, m_Hand.PressL))
		{
			if (p.Ok && m_Director.Colony())   // T269: the site at once, turned; T276: the tool stays for the next one (right click or Esc lets it go)
			{
				m_UiCommands.push_back("director build " + std::to_string(clan->Id) + " " + m_Hand.Building + " " + Fixed(x) + " " + Fixed(z) + " " + Fixed(m_Hand.Yaw));
			}
			else if (p.Ok)
			{
				m_UiCommands.push_back("director sign " + std::to_string(clan->Id) + " " + m_Hand.Building + " " + Fixed(x) + " " + Fixed(z) + " " + Fixed(m_Hand.Yaw));
				m_UiCommands.push_back("director hand off");
			}
			else ShellToastShown(Lang::Cap(L.Line(p.Why)));
		}
	}

	namespace
	{
		// a line of the chronicle of a screen of the end (a stage won, the defeat): when, the clan's English name, the English line
		struct WeightyLine { double At = 0.0; std::string Clan, Text; };

		// D48: only what matters for the screens of the end, never a gather nor a theft of a node: the history's heavy kinds (weight 4 or more:
		// discoveries, births, deaths, marriages, alliances, ranks, polities, new clans, raids...), the buildings finished, the skeptics and the tired
		// clans, and the god's own book (a sign or a vision fulfilled). At most `most`, the heaviest first, two at most of a kind (the births would fill it),
		// then in the order they happened.
		std::vector<WeightyLine> WeightyChronicle(const ClanWorld& clans, const WorldDirector& god, double from, double to, size_t most)
		{
			struct Pick { int Weight; WeightyLine Line; std::string Kind; };
			std::vector<Pick> all;
			for (const HistoryEvent& e : clans.Past().Events())
			{
				if (e.At < from || e.At > to) continue;
				const bool kept = e.Kind == "built" || e.Kind == "skeptic" || e.Kind == "tired";   // (weight 3, but what the player wants told)
				if (SheetKind(e.Kind).empty() && e.Kind != "rank" && e.Kind != "polity" && e.Kind != "leap") continue;   // (the story's kinds only)
				if (e.Weight < 4 && !kept) continue;
				const Clan* c = clans.Find(static_cast<ClanId>(e.Clan));
				all.push_back({ kept ? 3 : e.Weight, { e.At, c ? c->Name : std::string(), e.Text }, e.Kind });
			}
			static const std::map<std::string, int> kBook = { { "fulfilled", 4 } };   // (a heresy is told by the history: the new clan it founds)
			for (const DirectorEntry& e : god.Book())
			{
				const auto w = kBook.find(e.Action);
				if (w == kBook.end() || e.At < from || e.At > to) continue;
				all.push_back({ w->second, { e.At, e.Target == "world" ? std::string() : e.Target, e.Text }, "god " + e.Action });
			}
			std::stable_sort(all.begin(), all.end(), [](const Pick& a, const Pick& b) { return a.Weight != b.Weight ? a.Weight > b.Weight : a.Line.At > b.Line.At; });
			std::vector<WeightyLine> out;
			std::map<std::string, int> perKind;
			for (const Pick& p : all)
			{
				if (out.size() >= most) break;
				if (++perKind[p.Kind] > 2) continue;
				out.push_back(p.Line);
			}
			std::sort(out.begin(), out.end(), [](const WeightyLine& a, const WeightyLine& b) { return a.At < b.At; });
			return out;
		}
	}

	// §6.2 A: the end of the god's game, over the world: nobody believes any more. The days played and the last lines of the world's chronicle;
	// [Watch the world] goes on looking without the hand (`director defeat watch`), [Main menu] leaves (`shell menu`).
	void EditorApp::DrawDefeat(float ox, float oy, float w, float h)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + w, oy + h), IM_COL32(0, 0, 0, 120));
		const float width = std::min(560.0f, w - 40.0f);
		ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, oy + h * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowFocus();
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
		if (ImGui::Begin("Defeat##observer", nullptr, flags))
		{
			const std::string title = L.Text(m_Director.Colony() ? "Your clan is gone" : "Nobody believes in you any more");   // (T269: the colony's end)
			ImGui::SetCursorPosX(std::max(0.0f, (width - ImGui::CalcTextSize(title.c_str()).x) * 0.5f));
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", title.c_str());
			const double day = m_Director.DefeatAt() / static_cast<double>(m_Director.Rules.DaySeconds);
			const std::string days = L.Text("Days played: {0}", L.Num(static_cast<float>(day), 1));
			ImGui::SetCursorPosX(std::max(0.0f, (width - ImGui::CalcTextSize(days.c_str()).x) * 0.5f));
			ImGui::TextUnformatted(days.c_str());
			ImGui::Separator();
			ImGui::TextDisabled("%s", L.Text("The last of the chronicle").c_str());
			const double daySeconds = static_cast<double>(m_Director.Rules.DaySeconds);
			int shown = 0;
			for (const WeightyLine& l : WeightyChronicle(m_Clans, m_Director, std::max(0.0, m_Director.DefeatAt() - 5.0 * daySeconds), m_Director.DefeatAt() + 1.0, 6))   // (the last days' weighty ones)
			{
				const std::string when = L.Text("day {0}", L.Num(static_cast<float>(l.At / daySeconds), 1));
				ImGui::TextDisabled("%s", (when + (l.Clan.empty() ? std::string() : "  " + L.Name("clan", l.Clan, l.Clan))).c_str());
				ImGui::SameLine(150.0f);
				ImGui::PushTextWrapPos(width - 12.0f);
				ImGui::TextWrapped("%s", Lang::Cap(L.Line(l.Text)).c_str());
				ImGui::PopTextWrapPos();
				++shown;
			}
			if (shown == 0) ImGui::TextDisabled("%s", L.Text("nothing yet").c_str());
			ImGui::Separator();
			const float bw = (width - 3.0f * T::Gap) * 0.5f;
			{
				T::PrimaryScope lit;
				if (ImGui::Button((L.Text("Watch the world") + "###defeat_watch").c_str(), ImVec2(bw, 28.0f))) run("director defeat watch");
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text(m_Director.Colony() ? "The world goes on without your clan: you watch it" : "The world goes on: you watch it, without the hand").c_str());
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Main menu") + "###defeat_menu").c_str(), ImVec2(bw, 28.0f))) run("shell menu");
		}
		ImGui::End();
	}

	// D48: the goals of a stage, one per row: a mark (a filled circle with a tick when met, a ring when not: the font has no such glyphs), what it
	// asks and how far it is ("14/20", the clan nearest to it). The tooltip of the god's bar and the screen of a stage won draw it.
	void EditorApp::DrawStageGoals(const DirectorStageDef& stage, float width)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const double day = static_cast<double>(m_Director.Rules.DaySeconds);
		const int need = m_Director.GoalsNeeded();
		const bool colony = m_Director.Colony();   // T269 (C-4): the goals are the player's clan's
		ImGui::TextUnformatted((L.Text("Stage {0}", m_Director.StageNumber()) + ": " + L.Text(stage.Name)).c_str());
		ImGui::TextDisabled("%s", L.Text("Fulfil {0} of these {1} goals", need, static_cast<int>(stage.Goals.size())).c_str());
		ImGui::Separator();
		auto clanName = [&](int id) -> std::string
		{
			const Clan* c = id >= 0 ? m_Clans.Find(static_cast<ClanId>(id)) : nullptr;
			return c ? L.Name("clan", c->Name, c->Name) : std::string();
		};
		for (const DirectorGoalDef& g : stage.Goals)
		{
			const DirectorGoalState s = m_Director.GoalOf(g.Id);
			// the mark
			const float size = ImGui::GetTextLineHeight();
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(size, size));
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 centre(at.x + size * 0.5f, at.y + size * 0.5f);
			const float r = size * 0.42f;
			if (s.Done)
			{
				draw->AddCircleFilled(centre, r, T::Good, 16);
				const ImVec2 tick[3] = { ImVec2(centre.x - r * 0.5f, centre.y), ImVec2(centre.x - r * 0.12f, centre.y + r * 0.42f), ImVec2(centre.x + r * 0.55f, centre.y - r * 0.4f) };
				draw->AddPolyline(tick, 3, IM_COL32(30, 26, 20, 255), ImDrawFlags_None, 2.0f);
			}
			else draw->AddCircle(centre, r, T::BoneDim, 16, 1.5f);
			ImGui::SameLine(0.0f, 8.0f);
			// what it asks
			std::string what, progress;
			const std::string count = std::to_string(s.Value) + "/" + std::to_string(g.Type == "tech" ? g.Clans : g.Count);
			const std::string leader = clanName(s.Leader);
			if (g.Type == "population")
			{
				what = colony ? L.Text("Your clan reaches {0} people", g.Count) : g.Clans == 1 ? L.Text("A believing clan of {0} people", g.Count) : L.Text("{0} believing clans of {1} people", g.Clans, g.Count);
				progress = count + (leader.empty() ? std::string() : "  (" + leader + ")");
			}
			else if (g.Type == "tech")
			{
				const TechTree* techs = nullptr;
				for (const auto& c : m_Clans.All()) if (c->Folk && (techs = c->Folk->Knowledge().Techs())) break;
				const TechDef* t = techs ? techs->Find(g.Tech) : nullptr;
				const std::string tech = L.Name("tech", g.Tech, t ? t->Name : g.Tech);
				what = colony ? L.Text("Your clan learns {0}", tech) : g.Clans == 1 ? L.Text("A believing clan learns {0}", tech) : L.Text("{0} believing clans learn {1}", g.Clans, tech);
				progress = g.Clans > 1 ? count : s.Done ? leader : L.Text("not yet");
			}
			else if (g.Type == "techs")
			{
				what = colony ? L.Text("Your clan knows {0} technologies", g.Count) : g.Clans == 1 ? L.Text("A believing clan knows {0} technologies", g.Count) : L.Text("{0} believing clans know {1} technologies", g.Clans, g.Count);
				progress = count + (leader.empty() ? std::string() : "  (" + leader + ")");
			}
			else if (g.Type == "building")
			{
				// the building's name with its article where the language has one ("building_a": "una casa larga"), else its plain name
				const BuildingDef* def = nullptr;
				for (const auto& c : m_Clans.All())
					if (c->Folk && c->Folk->Buildings())
						if (const auto it = c->Folk->Buildings()->find(g.Building); it != c->Folk->Buildings()->end()) { def = &it->second; break; }
				const std::string plain = L.Name("building", g.Building, def ? def->Name : g.Building);
				const std::string one = L.Name("building_a", g.Building, L.On() ? plain : "a " + plain);
				if (colony) what = g.Count == 1 ? L.Text("Your clan raises {0}", one) : L.Text("Your clan raises {0} of: {1}", g.Count, plain);
				else if (g.Count == 1) what = g.Clans == 1 ? L.Text("A believing clan raises {0}", one) : L.Text("{0} believing clans raise {1}", g.Clans, one);
				else what = g.Clans == 1 ? L.Text("A believing clan raises {0} of: {1}", g.Count, plain) : L.Text("{0} believing clans raise {1} of: {2}", g.Clans, g.Count, plain);
				progress = count + (leader.empty() ? std::string() : "  (" + leader + ")");
			}
			else if (g.Type == "believers") { what = colony ? L.Text("Your clan lives on") : L.Text("{0} clans believe in you", g.Count); progress = count; }
			else if (g.Type == "signs_built") { what = colony ? L.Text("{0} buildings of your plans standing", g.Count) : L.Text("{0} signs of the god standing", g.Count); progress = count; }
			else if (g.Type == "peace") { what = L.Text("{0} days of peace among the clans", g.Count); progress = count; }
			else if (g.Type == "no_hunger_winter")
			{
				if (colony) what = g.Count == 1 ? L.Text("Your clan goes through a whole winter without hunger") : L.Text("Your clan goes through {0} whole winters without hunger", g.Count);
				else what = g.Count == 1 ? L.Text("No believing clan goes hungry for a whole winter") : L.Text("No believing clan goes hungry for {0} whole winters", g.Count);
				if (s.Done) progress = L.Text("done");
				else if (s.Year >= 0) progress = s.Hungry ? L.Text("this winter: hunger ({0})", leader) : s.Whole ? L.Text("this winter: nobody hungry yet") : L.Text("this winter came before the stage");
				else if (!m_Clans.All().empty() && m_Clans.All().front()->Folk)
				{
					const int days = static_cast<int>(std::lround(ForecastWinter(*m_Clans.All().front()->Folk).ToWinter));
					progress = days <= 0 ? L.Text("the winter comes within a day") : L.Text(days == 1 ? "the winter comes in {0} day" : "the winter comes in {0} days", days);
				}
				if (g.Count > 1) progress = count + "  " + progress;
			}
			else what = g.Id;
			if (s.Done && s.DoneAt >= 0.0) progress += (progress.empty() ? "" : "  ") + L.Text("day {0}", L.Num(static_cast<float>(s.DoneAt / day), 1));
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(s.Done ? T::Bone : T::BoneDim), "%s", what.c_str());
			if (!progress.empty())
			{
				const float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 16.0f;   // (never over what it asks)
				const float room = width - ImGui::CalcTextSize(progress.c_str()).x - 8.0f;
				if (after <= room) ImGui::SameLine(room);
				else ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), room));   // (no room on the line: under it, to the right)
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(s.Done ? T::Good : T::Bone), "%s", progress.c_str());
			}
		}
		if (stage.Draft) ImGui::TextDisabled("%s", L.Text("(a draft: its numbers may change)").c_str());
	}

	// D48: a stage fulfilled, over the world, in the style of the defeat: the days played, the goals, the chronicle of the stage (its weighty events)
	// and [Go on] (`director stage next`: the next stage, or the world kept with no more) or [Main menu] (`shell menu`).
	void EditorApp::DrawStageWon(float ox, float oy, float w, float h)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const DirectorStageDef* stage = m_Director.Stage();
		if (!stage) return;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const double day = static_cast<double>(m_Director.Rules.DaySeconds);
		ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + w, oy + h), IM_COL32(0, 0, 0, 120));
		const float width = std::min(600.0f, w - 40.0f);
		ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, oy + h * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowFocus();
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
		if (ImGui::Begin("StageWon##observer", nullptr, flags))
		{
			auto centred = [&](const std::string& text, ImU32 colour)
			{
				ImGui::SetCursorPosX(std::max(0.0f, (width - ImGui::CalcTextSize(text.c_str()).x) * 0.5f));
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour), "%s", text.c_str());
			};
			centred(L.Text("Stage fulfilled"), T::Good);
			centred(L.Text("Stage {0}", m_Director.StageNumber()) + ": " + L.Text(stage->Name), T::Bone);
			centred(L.Text("Days played: {0}", L.Num(static_cast<float>(m_Director.StageWonAt() / day), 1)), T::BoneDim);
			ImGui::Separator();
			DrawStageGoals(*stage, width - 16.0f);
			ImGui::Separator();
			ImGui::TextDisabled("%s", L.Text("The chronicle of the stage").c_str());
			int shown = 0;
			for (const WeightyLine& l : WeightyChronicle(m_Clans, m_Director, m_Director.StageSince(), m_Director.StageWonAt() + 1.0, 8))   // (the weighty ones of the stage)
			{
				const std::string when = L.Text("day {0}", L.Num(static_cast<float>(l.At / day), 1));
				ImGui::TextDisabled("%s", (when + (l.Clan.empty() ? std::string() : "  " + L.Name("clan", l.Clan, l.Clan))).c_str());
				ImGui::SameLine(150.0f);
				ImGui::PushTextWrapPos(width - 12.0f);
				ImGui::TextWrapped("%s", Lang::Cap(L.Line(l.Text)).c_str());
				ImGui::PopTextWrapPos();
				++shown;
			}
			if (shown == 0) ImGui::TextDisabled("%s", L.Text("nothing yet").c_str());
			ImGui::Separator();
			const float bw = (width - 3.0f * T::Gap) * 0.5f;
			const std::vector<DirectorStageDef>& all = m_Director.Rules.Stages.List;
			const int next = m_Director.StageNumber();   // (the next one's index)
			{
				T::PrimaryScope lit;
				if (ImGui::Button((L.Text("Go on") + "###stage_next").c_str(), ImVec2(bw, 28.0f))) run("director stage next");
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", next < static_cast<int>(all.size()) ? L.Text("Opens the stage {0}: {1}", next + 1, L.Text(all[static_cast<size_t>(next)].Name)).c_str()
					: L.Text("There are no more stages: the world goes on and the god keeps it").c_str());
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Main menu") + "###stage_menu").c_str(), ImVec2(bw, 28.0f))) run("shell menu");
		}
		ImGui::End();
	}

	void EditorApp::DrawHandBar(float ox, float oy, float w, float h)
	{
		namespace T = GameTheme;
		m_Hand.Box = m_Hand.MenuBox = {};
		if (m_Clans.Count() == 0) return;
		const Clan* clan = m_Clans.Find(HandClan());   // (T269: the player's own in the colony)
		if (!clan || !clan->Folk) return;
		const bool colony = m_Director.Colony();   // T269 (MODO_COLONIA §4.1): no influence, no faith, no omen, no focus; [Build] and [Research]
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto divider = [&]() { ImGui::SameLine(0.0f, 12.0f); ImGui::TextDisabled("|"); ImGui::SameLine(0.0f, 12.0f); };
		auto tip = [&](const std::string& text) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text.c_str()); };
		const std::string name = L.Name("clan", clan->Name, clan->Name);
		const DirectorRules& R = m_Director.Rules;
		// at the bottom, between the clans' list and the book when they reach down that far (a folded book or a short list leave the width free)
		const float bottomZone = oy + h - 90.0f;
		const ObserverState::Box& list = m_Observer.ClansBox.X1 > m_Observer.ClansBox.X0 ? m_Observer.ClansBox : m_Observer.CardBox;
		const float left = list.X1 > list.X0 && list.Y1 > bottomZone ? list.X1 + T::Gap : ox + T::Margin;
		const float right = m_Observer.BookBox.X1 > m_Observer.BookBox.X0 && m_Observer.BookBox.Y1 > bottomZone ? m_Observer.BookBox.X0 - T::Gap : ox + w - T::Margin;
		const float room = std::max(right - left, 120.0f);
		const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
		auto cost = [&](const char* action) { const auto it = R.Costs.find(action); return it == R.Costs.end() ? 0.0f : it->second; };
		const DirectorCheck omen = m_Director.CanSpend("omen", clan->Id, false);   // (an omen is a phenomenon of the world: no faith needed)
		ImGui::SetNextWindowPos(ImVec2((left + right) * 0.5f, oy + h - T::Margin), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(room, FLT_MAX));
		if (ImGui::Begin("Hand##observer", nullptr, fixed))
		{
			if (!colony)   // (T269: the god's game only: the influence and the believers)
			{
			// the influence: what the god can spend, as a number and a bar
			const float influence = m_Director.Influence(), most = std::max(R.Max, 1.0f);
			ImGui::BeginGroup();
			ImGui::TextDisabled("%s", L.Text("Influence").c_str());
			ImGui::SameLine();
			SmallBar(influence / most, 90.0f, T::Ochre);
			ImGui::SameLine();
			ImGui::TextUnformatted(L.Text("{0} of {1}", L.Num(influence, 0), L.Num(most, 0)).c_str());
			ImGui::EndGroup();
			tip(L.Text("What the god can spend. It comes from the faith of the clans: their rites, the omens they read and their devotion every day.") + "\n"
				+ L.Text("A sign costs {0}, an omen {1}.", L.Num(cost("sign"), 0), L.Num(cost("omen"), 0)) + (m_Director.Sandbox() ? "\n" + L.Text("Sandbox: it refills on a fixed clock") : std::string()));
			divider();
			// §6.2 A: the believers (the clans whose faith reaches the hand, and their people): at none, the god's game is lost
			{
				const DirectorBelievers b = m_Director.Believers();
				ImGui::BeginGroup();
				ImGui::TextDisabled("%s", L.Text("Believers").c_str());
				ImGui::SameLine();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(b.Clans == 0 ? T::Bad : b.Clans == 1 ? T::Warn : T::Bone), "%s",
					(L.Text(b.Clans == 1 ? "{0} clan" : "{0} clans", b.Clans) + " \xC2\xB7 " + L.Text(b.People == 1 ? "{0} person" : "{0} people", b.People)).c_str());
				ImGui::EndGroup();
				tip(m_Director.Defeated() ? L.Text("Nobody believes in you any more: you only watch the world")
					: L.Text("The clans whose faith reaches {0} % and their people. If none is left, the game is lost.", L.Num(R.MinFaith * 100.0f, 0)));
				divider();
			}
			}
			// D48: the stage of the legacy ("Stage 1 · 2 of 4"); over it, its goals and what each one lacks (only in the god's game: the stakes)
			if (const DirectorStageDef* stage = m_Director.Stage(); stage && m_Director.Stakes())
			{
				const int done = m_Director.GoalsDone(), need = m_Director.GoalsNeeded();
				ImGui::BeginGroup();
				ImGui::TextDisabled("%s", L.Text("Stage {0}", m_Director.StageNumber()).c_str());
				ImGui::SameLine();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(done >= need ? T::Good : T::Bone), "%s", ("\xC2\xB7 " + L.Text("{0} of {1}", done, static_cast<int>(stage->Goals.size()))).c_str());
				ImGui::EndGroup();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || m_Observer.StageTip)
				{
					// (wide enough from its first frame for the progress put on the right: else the end of "40 días" was cut)
					ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f + 2.0f * ImGui::GetStyle().WindowPadding.x, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
					ImGui::BeginTooltip();
					DrawStageGoals(*stage, 420.0f);
					ImGui::EndTooltip();
				}
				divider();
			}
			// the clan under the hand: its faith and the pressure the god has put on it
			if (colony)   // T269: the player's clan and its people; then [Build] and [Research]
			{
				int alive = 0;
				for (const TribeMember& m : clan->Folk->Members()) if (!m.Dead) ++alive;
				ImGui::BeginGroup();
				Swatch(ClanColour(m_Clans, clan->Id));
				ImGui::TextUnformatted(name.c_str());
				ImGui::SameLine(0.0f, 10.0f);
				ImGui::TextDisabled("%s", L.Text(alive == 1 ? "{0} person" : "{0} people", alive).c_str());
				ImGui::EndGroup();
				tip(L.Text("Your clan. If nobody of it is left alive, the game is lost."));
				divider();
				const bool building = m_Hand.Menu == "build" || !m_Hand.Building.empty();
				{
					std::optional<T::PrimaryScope> lit;
					if (building) lit.emplace();
					if (ImGui::Button((L.Text("Build") + "###handbuild").c_str())) run(building ? "director hand off" : "director hand build");
				}
				tip(L.Text("Choose a building and where it goes: the {0} raise it with their own material", name));
				ImGui::SameLine();
				{
					std::optional<T::PrimaryScope> lit;
					if (m_Observer.Tree) lit.emplace();
					if (ImGui::Button((L.Text("Research") + "###handResearch").c_str())) run("observer tree");
				}
				{
					const std::string& aim = clan->Folk->ResearchAim();
					const TechTree* techs = clan->Folk->Knowledge().Techs();
					const TechDef* def = !aim.empty() && techs ? techs->Find(aim) : nullptr;
					tip(aim.empty() ? L.Text("Choose what the {0} seek in the tree of knowledge (they also find things out by themselves)", name)
						: L.Text("The {0} seek {1}", name, Lang::Cap(L.Name("tech", aim, def ? def->Name : aim))));
				}
				// T308 (MODO_COLONIA §4.3): [Work] the table of priorities, [Stock] the goals of what the clan keeps (ColonyTabs.cpp)
				for (const auto& [tab, label, help] : { std::tuple<const char*, const char*, const char*>{ "work", "Work", "Who does what first: the priorities of each person" },
					std::tuple<const char*, const char*, const char*>{ "stock", "Stock", "What the clan keeps in store: N of a thing, made or gathered when it lacks" } })
				{
					ImGui::SameLine();
					const bool open = m_Colony.Tab == tab;
					{
						std::optional<T::PrimaryScope> lit;
						if (open) lit.emplace();
						if (ImGui::Button((L.Text(label) + "###hand" + tab).c_str())) run(open ? std::string("colony tab off") : std::string("colony tab ") + tab);
					}
					tip(L.Text(help));
				}
			}
			else
			{
			ImGui::BeginGroup();
			Swatch(ClanColour(m_Clans, clan->Id));
			ImGui::TextUnformatted(name.c_str());
			ImGui::SameLine(0.0f, 10.0f);
			const float faith = m_Director.FaithOf(clan->Id);
			const bool low = faith < R.MinFaith;
			ImGui::TextDisabled("%s", L.Text("Faith").c_str());
			ImGui::SameLine();
			SmallBar(faith, 70.0f, low ? T::Bad : T::Ochre);
			ImGui::SameLine();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(low ? T::Bad : T::Bone), "%s", (L.Num(faith * 100.0f, 0) + " %").c_str());
			ImGui::EndGroup();
			tip(L.Text("How much the {0} believe in the god. Under {1} % the hand does not reach them (an omen still does). Pick another clan in the list.", name, L.Num(R.MinFaith * 100.0f, 0)));
			ImGui::SameLine(0.0f, 10.0f);
			const std::string level = m_Director.PressureLevel(clan->Id);
			const ImU32 levelColour = level == "heresy" ? T::Bad : level == "tired" ? T::Warn : T::Good;
			ImGui::BeginGroup();
			ImGui::TextDisabled("%s", L.Text("Pressure").c_str());
			ImGui::SameLine();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(levelColour), "%s", L.Text(level == "heresy" ? "heresy" : level == "tired" ? "tired" : "low").c_str());
			ImGui::EndGroup();
			tip(L.Text("How hard the god has pressed on the {0} ({1}): it fades a little every day. Tired at {2}, heresy at {3}.", name, L.Num(m_Director.PressureOf(clan->Id), 1), L.Num(R.PressureTired, 0), L.Num(R.PressureHeresy, 0)));
			if (room >= 900.0f) divider();   // (one line when there is room; else the buttons under the numbers)
			// [Build]: why not, when not
			const DirectorCheck sign = m_Director.CanSpend("sign", clan->Id, true);
			const DirectorSign* open = m_Director.OpenSign(clan->Id);
			const std::string buildWhy = open ? L.Text("A sign already waits on the {0}: see what they make of it first", name) : sign.Ok ? std::string() : Lang::Cap(L.Line(sign.Why));
			const bool building = m_Hand.Menu == "build" || !m_Hand.Building.empty();
			ImGui::BeginDisabled(!buildWhy.empty() && !building);
			{
				std::optional<T::PrimaryScope> lit;
				if (building) lit.emplace();
				if (ImGui::Button((L.Text("Build") + "###handbuild").c_str())) run(building ? "director hand off" : "director hand build");
			}
			ImGui::EndDisabled();
			tip(!buildWhy.empty() ? buildWhy : L.Text("Show the {0} where to raise a building: someone of the clan may see the sign, and the clan votes it at the fire", name));
			ImGui::SameLine();
			// [Omen]
			const std::string omenWhy = omen.Ok ? std::string() : Lang::Cap(L.Line(omen.Why));
			const bool omening = m_Hand.Menu == "omen";
			ImGui::BeginDisabled(!omenWhy.empty() && !omening);
			{
				std::optional<T::PrimaryScope> lit;
				if (omening) lit.emplace();
				if (ImGui::Button((L.Text("Omen") + "###handomen").c_str())) run(omening ? "director hand off" : "director hand omen");
			}
			ImGui::EndDisabled();
			tip(!omenWhy.empty() ? omenWhy : L.Text("Send the {0} a sign of the sky or the earth: they read it as a natural one", name));
			// [Focus] (T238): the menu of the groups; the hand still reaches only a believing clan
			ImGui::SameLine();
			const DirectorCheck focusHand = m_Director.CanSpend("focus", clan->Id, true);
			const std::string focusWhy = focusHand.Ok ? std::string() : Lang::Cap(L.Line(focusHand.Why));
			const bool focusing = m_Hand.Menu == "focus";
			ImGui::BeginDisabled(!focusWhy.empty() && !focusing);
			{
				std::optional<T::PrimaryScope> lit;
				if (focusing) lit.emplace();
				if (ImGui::Button((L.Text("Focus") + "###handFocus").c_str())) run(focusing ? "director hand off" : "director hand focus");
			}
			ImGui::EndDisabled();
			tip(!focusWhy.empty() ? focusWhy : L.Text("The {0} put their effort on one thing for a few days: the believers follow it more than the rest", name));
			// [Vision] (T242): opens the tree of knowledge of the clan under the hand, where each technology has its [Vision]
			ImGui::SameLine();
			const DirectorCheck visionHand = m_Director.CanSpend("vision", clan->Id, true);
			const std::string visionWhy = visionHand.Ok ? std::string() : Lang::Cap(L.Line(visionHand.Why));
			ImGui::BeginDisabled(!visionWhy.empty() && !m_Observer.Tree);
			{
				std::optional<T::PrimaryScope> lit;
				if (m_Observer.Tree) lit.emplace();
				if (ImGui::Button((L.Text("Vision") + "###handVision").c_str())) run("observer tree");
			}
			ImGui::EndDisabled();
			{
				const DirectorVision* held = m_Director.VisionOf(clan->Id);
				const TechTree* techs = clan->Folk->Knowledge().Techs();
				const TechDef* dreamt = held && techs ? techs->Find(held->Tech) : nullptr;
				tip(!visionWhy.empty() ? visionWhy : held ? L.Text("The {0} seek {1}, as {2} dreamt", name, Lang::Cap(L.Name("tech", held->Tech, dreamt ? dreamt->Name : held->Tech)), held->Dreamer)
					: L.Text("Someone of the {0} dreams of a technology of the tree: the clan seeks it ({1} influence). Pick it in the tree of knowledge", name, L.Num(cost("vision"), 0)));
			}
			// the focus the clan follows now: its group, the days left and [Remove]
			if (const DirectorFocus* f = m_Director.FocusOf(clan->Id))
			{
				const double now = m_Director.Targets().Now ? m_Director.Targets().Now() : 0.0;
				const float daysLeft = static_cast<float>(std::max(0.0, f->Until - now) / static_cast<double>(R.DaySeconds));
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(f->Paused ? T::Warn : T::Ochre), "%s", (L.Text("Focus: {0} ({1} days left)", L.Text(FocusGroupText(f->Group)), L.Num(daysLeft, 1))
					+ (f->Paused ? "  " + L.Text("paused: they are hungry") : std::string())).c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton((L.Text("Remove") + "###focusoff").c_str())) run("director focus " + std::to_string(clan->Id) + " off");
				tip(L.Text("The {0} go back to their own way (what was paid is not given back)", name));
			}
			}   // (T269: the god's game's hand)
			if (!m_Hand.Building.empty())
			{
				std::string what = m_Hand.Building;
				if (const auto* defs = clan->Folk->Buildings())
					if (const auto def = defs->find(m_Hand.Building); def != defs->end()) what = Lang::Cap(L.Name("building", def->first, def->second.Name));
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("Placing: {0}", what).c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s", L.Text("[Click] on the ground  [R] turn ({0} degrees)  [Right click] or [Esc] cancel", L.Num(m_Hand.Yaw, 0)).c_str());
			}
			m_Hand.Box = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		}
		ImGui::End();
		if (m_Hand.Menu.empty() || m_Hand.Box.Y1 <= m_Hand.Box.Y0) return;

		// the menu over the bar: the palette of buildings or the omens
		ImGui::SetNextWindowPos(ImVec2((left + right) * 0.5f, m_Hand.Box.Y0 - T::Gap), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		const float menuMaxH = std::max(h * (m_Hand.Menu == "build" ? 0.40f : 0.45f), 160.0f);   // (T254: the grid never covers more than 40 % of the view)
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(room, menuMaxH));
		if (ImGui::Begin("HandMenu##observer", nullptr, fixed & ~ImGuiWindowFlags_NoScrollbar))
		{
			const float width = std::min(room, 620.0f) - 2.0f * T::Pad;
			auto wrapped = [&](float& x, float bw, bool first)   // a row of buttons that wraps instead of growing wide
			{
				if (!first && x + ImGui::GetStyle().ItemSpacing.x + bw <= width) { ImGui::SameLine(); x += ImGui::GetStyle().ItemSpacing.x + bw; }
				else x = bw;
			};
			if (m_Hand.Menu == "build")   // T254: every building of the content in a grid, Black & White's way
				DrawHandGrid(*clan, room, menuMaxH);
			else if (m_Hand.Menu == "focus")   // T238: the groups, soft and strong, with their cost or why not
			{
				ImGui::TextUnformatted(L.Text("Where the {0} put their effort", name).c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s", L.Text("(3 days; soft x{0}: {1} + {2} a day; strong x{3}: {4} + {5} a day)", L.Num(R.Focus.Soft, 2), L.Num(cost("focus"), 0), L.Num(R.Focus.UpkeepSoft, 0),
					L.Num(R.Focus.Strong, 2), L.Num(cost("focus_strong"), 0), L.Num(R.Focus.UpkeepStrong, 0)).c_str());
				if (clan->Folk->MaslowTier() == "survival")   // (why the other groups are greyed, without a hover)
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("Now the {0} are hungry (survival): only the focus on food", name).c_str());
				else if (const DirectorCheck hand = m_Director.CanSpend("focus", clan->Id, true); !hand.Ok)
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", Lang::Cap(L.Line(hand.Why)).c_str());
				ImGui::Separator();
				const DirectorFocus* active = m_Director.FocusOf(clan->Id);
				int alive = 0;
				for (const TribeMember& m : clan->Folk->Members()) if (!m.Dead) ++alive;
				for (const auto& [group, def] : R.Focus.Groups)
				{
					ImGui::PushID(group.c_str());
					const bool on = active && active->Group == group;
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(on ? T::Ochre : T::Bone), "%s", Lang::Cap(L.Text(FocusGroupText(group))).c_str());
					ImGui::SameLine(130.0f);
					for (const bool strong : { false, true })
					{
						const DirectorCheck can = m_Director.CanFocus(clan->Id, group, strong);
						int follow = 0;
						for (const TribeMember& m : clan->Folk->Members())
							if (!m.Dead && m_Director.FocusMultiplier(clan->Id, m.Name, strong, group) > 1.1f) ++follow;
						ImGui::BeginDisabled(!can.Ok);
						if (ImGui::Button((L.Text(strong ? "Strong" : "Soft") + (strong ? "###strong" : "###soft")).c_str()))
						{
							run("director focus " + std::to_string(clan->Id) + " " + group + (strong ? " strong" : " soft") + " 3");
							run("director hand off");
						}
						ImGui::EndDisabled();
						tip(can.Ok ? L.Text("{0} influence; {1} of {2} would follow it", L.Num(can.Cost, 0), follow, alive) : Lang::Cap(L.Line(can.Why)));
						ImGui::SameLine();
					}
					std::string kinds;
					for (const std::string& k : def.Kinds) kinds += (kinds.empty() ? "" : ", ") + L.Text(k);
					if (def.FoodGather) kinds = L.Text("gathering food") + (kinds.empty() ? "" : ", ") + kinds;
					if (def.OtherGather) kinds += (kinds.empty() ? "" : ", ") + L.Text("gathering wood, stone and fibre");
					ImGui::TextDisabled("%s", kinds.c_str());
					ImGui::PopID();
				}
			}
			else
			{
				const SocialRules& S = clan->Folk->Config.Social;
				ImGui::TextUnformatted(L.Text("Omens for the {0}", name).c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s", L.Text("({0} influence)", L.Num(cost("omen"), 0)).c_str());
				ImGui::Separator();
				if (!S.On || S.Beliefs.Omens.empty()) ImGui::TextDisabled("%s", L.Text("The {0} read no omens", name).c_str());
				float x = 0.0f;
				bool first = true;
				for (const auto& [id, def] : S.Beliefs.Omens)
				{
					if (!S.On) break;
					const std::string label = Lang::Cap(L.Name("omen", def.Name, def.Name));
					wrapped(x, ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f, first);
					first = false;
					ImGui::BeginDisabled(!omen.Ok);
					if (ImGui::Button((label + "###o" + id).c_str()))
					{
						run("director omen " + id + " " + std::to_string(clan->Id));
						run("director hand off");
					}
					ImGui::EndDisabled();
					tip(omen.Ok ? L.Text("The world sends it to the {0}: their mood and beliefs move, maybe a rite", name) : Lang::Cap(L.Line(omen.Why)));
				}
			}
			m_Hand.MenuBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		}
		ImGui::End();
	}

	// T254: the grid of [Build]. Every building of the content, by category: its picture (assets/ui/thumbs, `thumbs build`), its name and the cost of
	// the sign; what the clan cannot raise yet is greyed with a lock and its card says why (the rank, the culture, the technologies that open it).
	// A click or the keys 1-9 choose a cell (`director hand place`), Tab moves through the categories (`director hand build <category>`), Esc closes.
	// The vote is not foreseen here: PreviewSign needs a place, and the ghost shows it as soon as the building follows the mouse.
	void EditorApp::DrawHandGrid(const Clan& clan, float room, float maxH)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const Tribe& folk = *clan.Folk;
		const std::string name = L.Name("clan", clan.Name, clan.Name);
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const auto signCost = m_Director.Rules.Costs.find("sign");
		const float cost = signCost == m_Director.Rules.Costs.end() ? 0.0f : signCost->second;
		const TechTree* tree = folk.Knowledge().Techs();
		auto techName = [&](const std::string& id) { const TechDef* d = tree ? tree->Find(id) : nullptr; return L.Name("tech", id, d ? d->Name : id); };

		struct Cell { std::string Id, Label, Tab; const BuildingDef* Def = nullptr; bool Known = false; int Have = 0; BuildLock Lock; };
		std::vector<Cell> cells;
		std::map<std::string, int> have, count;
		int known = 0;
		for (const Tribe::SettlementSite& s : folk.SettlementSites())
			if (s.State.Done) ++have[s.Building];
		if (const auto* defs = folk.Buildings())
			for (const auto& [id, def] : *defs)
			{
				Cell c;
				c.Id = id;
				c.Label = Lang::Cap(L.Name("building", id, def.Name));
				c.Tab = BuildingTab(def);
				c.Def = &def;
				c.Known = folk.CanBuild(id);
				c.Have = have.count(id) ? have[id] : 0;
				if (!c.Known) c.Lock = LockOf(folk, def);
				else ++known;
				++count[c.Tab];
				++count["all"];
				if (m_Hand.Tab == "all" || m_Hand.Tab == c.Tab) cells.push_back(std::move(c));
			}
		std::stable_sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return a.Known != b.Known ? a.Known : a.Label < b.Label; });

		// the header: whose hand it is, what a sign costs and how much of the content the clan knows
		ImGui::TextUnformatted(L.Text("Build for the {0}", name).c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("%s", ((m_Director.Colony() ? std::string() : L.Text("(a sign: {0} influence)", L.Num(cost, 0)) + "  ") + L.Text("they know {0} of {1}", known, count["all"])).c_str());   // (T269: no cost in the colony)
		// the categories: buttons of their own (the lit one is the open one), only those with something in them
		std::vector<std::string> tabs;
		for (const char* t : kBuildTabs)
			if (std::string(t) == "all" || count[t] > 0 || m_Hand.Tab == t) tabs.push_back(t);
		const float width = std::min(room, 1000.0f) - 2.0f * T::Pad;
		float x = 0.0f;
		for (size_t i = 0; i < tabs.size(); ++i)
		{
			const std::string label = L.Text(BuildTabText(tabs[i])) + " " + std::to_string(count[tabs[i]]);
			const float bw = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
			if (i > 0 && x + ImGui::GetStyle().ItemSpacing.x + bw <= width) { ImGui::SameLine(); x += ImGui::GetStyle().ItemSpacing.x + bw; }
			else x = bw;
			std::optional<T::PrimaryScope> lit;
			if (tabs[i] == m_Hand.Tab) lit.emplace();
			if (ImGui::Button((label + "###tab" + tabs[i]).c_str())) run("director hand build " + tabs[i]);
		}
		ImGui::Separator();

		// the cells: a picture on parchment, the name (two lines at most) and the cost, or the lock and what is missing
		const float img = 68.0f, cellW = 124.0f, gap = T::Gap, line = ImGui::GetTextLineHeight();
		const float cellH = 5.0f + img + 3.0f + 2.0f * line + 2.0f + line + 5.0f;
		const int cols = std::max(1, static_cast<int>((width - ImGui::GetStyle().ScrollbarSize + gap) / (cellW + gap)));
		const int rows = (static_cast<int>(cells.size()) + cols - 1) / cols;
		const float hint = line + ImGui::GetStyle().ItemSpacing.y * 2.0f + T::Pad;
		const float spare = maxH - (ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y) - hint - T::Pad;
		const float fullH = static_cast<float>(std::max(rows, 1)) * (cellH + gap);
		// (whole rows: a row cut in half reads as a broken grid; the rest scrolls)
		const float gridH = fullH <= spare ? fullH : std::max(std::floor((spare + gap) / (cellH + gap)), 1.0f) * (cellH + gap);
		const float gridW = static_cast<float>(cols) * (cellW + gap) + ImGui::GetStyle().ScrollbarSize;
		auto shortWhy = [&](const Cell& c)
		{
			if (!c.Lock.Rank.empty()) return Lang::Cap(L.Name("rank", c.Lock.Rank, c.Lock.Rank));
			if (!c.Lock.Value.empty()) return L.Text("Other ways");
			if (!c.Lock.Techs.empty()) return Lang::Cap(techName(c.Lock.Techs.front()));
			return L.Text("Not known yet");
		};
		auto choose = [&](const Cell& c)
		{
			if (c.Known) run("director hand place " + c.Id);
			else ShellToastShown(L.Text("The {0} cannot raise it yet: {1}", name, c.Label) + " (" + shortWhy(c) + ")");
		};
		// the card of a cell: what it does, what it asks, and why not when it cannot be raised
		auto card = [&](const Cell& c)
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", c.Label.c_str());
			if (c.Known) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", (m_Director.Colony() ? L.Text("Place it on the ground ({0} x {1} m)", L.Num(c.Def->Footprint.x, 0), L.Num(c.Def->Footprint.y, 0)) : L.Text("Place its sign on the ground ({0} x {1} m)", L.Num(c.Def->Footprint.x, 0), L.Num(c.Def->Footprint.y, 0))).c_str());
			else
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", L.Text("The {0} do not know how to raise it yet", name).c_str());
				if (!c.Lock.Rank.empty()) ImGui::TextUnformatted(L.Text("The clan must be a {0} first", L.Name("rank", c.Lock.Rank, c.Lock.Rank)).c_str());
				if (!c.Lock.Value.empty())
					ImGui::TextUnformatted(L.Text(c.Lock.TooMuch ? "Their culture values {0} too much" : "Their culture does not value {0} enough", L.Name("value", c.Lock.Value)).c_str());
				if (!c.Lock.Techs.empty())
				{
					std::string techs;
					for (const std::string& t : c.Lock.Techs) techs += (techs.empty() ? "" : ", ") + techName(t);
					ImGui::TextUnformatted((c.Lock.Techs.size() > 1 ? L.Text("It needs one of these: {0}", techs) : L.Text("It needs {0}", techs)).c_str());
					ImGui::TextDisabled("%s", L.Text(m_Director.Colony() ? "Research it in the tree of knowledge" : "A vision of the tree of knowledge can lead them to it").c_str());
				}
				ImGui::TextDisabled("%s", L.Text("Size: {0} x {1} m", L.Num(c.Def->Footprint.x, 0), L.Num(c.Def->Footprint.y, 0)).c_str());
			}
			for (const std::string& does : BuildingDoes(*c.Def)) ImGui::TextUnformatted(("- " + does).c_str());
			std::string needs;
			for (const auto& [item, n] : c.Def->TotalNeeds()) needs += (needs.empty() ? "" : ", ") + std::to_string(n) + " " + L.Name("item", item);
			if (!needs.empty()) ImGui::TextDisabled("%s", L.Text("Asks for {0}", needs).c_str());
			if (c.Have > 0) ImGui::TextDisabled("%s", L.Text("The {0} have {1} standing", name, c.Have).c_str());
			if (c.Known && !m_Director.Colony()) ImGui::TextDisabled("%s", L.Text("The vote is foreseen when you place it").c_str());   // (T269: no vote in the colony)
			ImGui::PopTextWrapPos();
		};
		glm::vec2 lockUv0(0.0f), lockUv1(0.0f);
		const bool fallback = IconUv("building.shelter", lockUv0, lockUv1);
		const Cell* peek = nullptr;
		ImVec2 peekAt;
		ImGui::BeginChild("grid", ImVec2(gridW, gridH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			if (cells.empty()) ImGui::TextDisabled("%s", L.Text("Nothing in this category").c_str());
			for (size_t i = 0; i < cells.size(); ++i)
			{
				const Cell& c = cells[i];
				if (i % static_cast<size_t>(cols) != 0) ImGui::SameLine(0.0f, gap);
				ImGui::PushID(c.Id.c_str());
				const ImVec2 at = ImGui::GetCursorScreenPos();
				if (ImGui::InvisibleButton("cell", ImVec2(cellW, cellH))) choose(c);
				const bool hovered = ImGui::IsItemHovered();
				if (m_Hand.Peek == c.Id && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);   // (a held card's cell comes into view)
				const ImVec2 to(at.x + cellW, at.y + cellH);
				dl->AddRectFilled(at, to, c.Known ? (hovered ? T::HideHover : T::HideRaised) : T::Hide, T::Radius);
				dl->AddRect(at, to, hovered && c.Known ? T::Ochre : T::HideLine, T::Radius, 0, hovered ? 2.0f : 1.0f);
				// the picture on its parchment (grey with a lock when they cannot raise it)
				const ImVec2 p0(at.x + (cellW - img) * 0.5f, at.y + 6.0f), p1(p0.x + img, p0.y + img);
				dl->AddRectFilled(p0, p1, c.Known ? T::Parchment : T::Faded(T::BoneDim, 0.55f), 4.0f);
				if (const BuildingThumb* th = BuildingThumbOf(c.Id))
					dl->AddImage(ThumbId(c.Known ? *th->Colour : *th->Grey), ImVec2(p0.x + 2.0f, p0.y + 2.0f), ImVec2(p1.x - 2.0f, p1.y - 2.0f));
				else if (fallback)
					dl->AddImage(ThumbId(*m_IconTexture), ImVec2(p0.x + 10.0f, p0.y + 10.0f), ImVec2(p1.x - 10.0f, p1.y - 10.0f), ImVec2(lockUv0.x, lockUv0.y), ImVec2(lockUv1.x, lockUv1.y),
						c.Known ? IM_COL32_WHITE : T::Faded(T::BoneDim, 0.6f));
				if (!c.Known)
				{
					const ImVec2 k(p1.x - 13.0f, p1.y - 13.0f);
					dl->AddCircleFilled(k, 11.0f, T::Outline);
					dl->PathArcTo(ImVec2(k.x, k.y - 1.5f), 3.6f, 3.14159265f, 6.28318531f);
					dl->PathStroke(T::Bone, ImDrawFlags_None, 2.0f);
					dl->AddLine(ImVec2(k.x - 3.6f, k.y - 1.5f), ImVec2(k.x - 3.6f, k.y + 0.5f), T::Bone, 2.0f);
					dl->AddLine(ImVec2(k.x + 3.6f, k.y - 1.5f), ImVec2(k.x + 3.6f, k.y + 0.5f), T::Bone, 2.0f);
					dl->AddRectFilled(ImVec2(k.x - 5.5f, k.y), ImVec2(k.x + 5.5f, k.y + 6.5f), T::Bone, 1.5f);
				}
				if (i < 9) T::KeyCap(dl, ImVec2(at.x + 4.0f, at.y + 4.0f), std::to_string(i + 1).c_str(), 17.0f);
				if (c.Have > 0)
				{
					const std::string n = "x" + std::to_string(c.Have);
					const ImVec2 s = ImGui::CalcTextSize(n.c_str());
					const ImVec2 b0(to.x - s.x - 12.0f, at.y + 4.0f);
					dl->AddRectFilled(b0, ImVec2(to.x - 4.0f, b0.y + s.y + 4.0f), T::Outline, 4.0f);
					dl->AddText(ImVec2(b0.x + 4.0f, b0.y + 2.0f), T::Ochre, n.c_str());
				}
				// the name, centred, in two lines at most (cut at a word)
				dl->PushClipRect(ImVec2(at.x + 3.0f, at.y), ImVec2(to.x - 3.0f, to.y), true);
				{
					std::string first = c.Label, second;
					const float room1 = cellW - 8.0f;
					if (ImGui::CalcTextSize(first.c_str()).x > room1)
					{
						size_t cut = first.size();
						while (cut != std::string::npos && cut > 0)
						{
							cut = first.rfind(' ', cut - 1);
							if (cut != std::string::npos && ImGui::CalcTextSize(first.substr(0, cut).c_str()).x <= room1) break;
						}
						if (cut != std::string::npos && cut > 0) { second = first.substr(cut + 1); first = first.substr(0, cut); }
					}
					float y = p1.y + 4.0f;
					for (const std::string* s : { &first, &second })
					{
						if (s->empty()) continue;
						const float tw = ImGui::CalcTextSize(s->c_str()).x;
						dl->AddText(ImVec2(at.x + std::max((cellW - tw) * 0.5f, 4.0f), y), c.Known ? T::Bone : T::BoneDim, s->c_str());
						y += line;
					}
					const std::string under = c.Known ? (m_Director.Colony() ? L.Text("Size: {0} x {1} m", L.Num(c.Def->Footprint.x, 0), L.Num(c.Def->Footprint.y, 0)) : L.Text("Sign: {0}", L.Num(cost, 0))) : shortWhy(c);
					const float uw = ImGui::CalcTextSize(under.c_str()).x;
					dl->AddText(ImVec2(at.x + std::max((cellW - uw) * 0.5f, 4.0f), p1.y + 4.0f + 2.0f * line + 2.0f), c.Known ? T::Ochre : T::BoneDim, under.c_str());
				}
				dl->PopClipRect();
				if (hovered)
				{
					ImGui::PushStyleColor(ImGuiCol_PopupBg, T::HidePanel);   // (solid: the cells under it must not show through the text)
					ImGui::BeginTooltip();
					card(c);
					ImGui::EndTooltip();
					ImGui::PopStyleColor();
				}
				else if (m_Hand.Peek == c.Id) { peek = &c; peekAt = ImVec2(to.x + T::Gap, at.y); }
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::TextDisabled("%s", L.Text("[Click] or [1-9] choose  [Tab] category  [Esc] close").c_str());
		if (peek)   // `director hand peek`: the card held open beside its cell, as the mouse would show it
		{
			const ImVec2 screen = ImGui::GetIO().DisplaySize;   // (beside its cell, or on its left near the right edge)
			const bool flip = peekAt.x > screen.x * 0.66f;
			ImGui::SetNextWindowPos(ImVec2(flip ? peekAt.x - cellW - 2.0f * T::Gap : peekAt.x, std::min(peekAt.y, screen.y * 0.55f)), ImGuiCond_Always, ImVec2(flip ? 1.0f : 0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_PopupBg, T::HidePanel);
			ImGui::BeginTooltip();
			card(*peek);
			ImGui::EndTooltip();
			ImGui::PopStyleColor();
		}
		// the keys: 1-9 the cells of the open category, Tab (Shift+Tab) the next (previous) category
		const ImGuiIO& io = ImGui::GetIO();
		if (io.WantTextInput) return;
		for (int k = 0; k < 9 && k < static_cast<int>(cells.size()); ++k)
			if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + k), false)) choose(cells[static_cast<size_t>(k)]);
		if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !tabs.empty())
		{
			const auto it = std::find(tabs.begin(), tabs.end(), m_Hand.Tab);
			const int at = it == tabs.end() ? 0 : static_cast<int>(it - tabs.begin()), n = static_cast<int>(tabs.size());
			run("director hand build " + tabs[static_cast<size_t>((at + (io.KeyShift ? n - 1 : 1)) % n)]);
		}
	}
}
