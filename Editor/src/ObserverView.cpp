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
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <optional>
#include <set>
#include <sstream>

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

		// TODO(U3): the winter's outlook will be WinterOutlook's (a pure function in GB/Game, MODO_DIOS_UX 3.4). Until it lands, the same definition
		// here, only to draw the provisional mark: the days until the spring (those to the winter plus the winter's own, or in the winter what is
		// left of it) against the days of food the clan has in hand today (Tribe::FoodDays: no births or deaths guessed). Short 0 = covered.
		struct WinterGuess { float ToWinter = 0.0f, UntilSpring = 0.0f, Short = 0.0f, SeasonDays = 14.0f; bool In = false; };
		WinterGuess GuessWinter(const Tribe& t)
		{
			const SeasonClock& s = t.Config.Seasons;
			const double now = t.WorldSeconds();
			const float daySeconds = std::max(1.0f, t.Config.Needs.DaySeconds);
			const float seasonDays = static_cast<float>(std::max(1, static_cast<int>(std::lround(s.SeasonSeconds / daySeconds))));   // (as the god's bar counts them)
			const Season season = s.At(now);
			const float left = (1.0f - std::clamp(s.Progress(now), 0.0f, 1.0f)) * seasonDays;
			WinterGuess g;
			g.SeasonDays = seasonDays;
			g.In = season == Season::Winter;
			g.ToWinter = g.In ? 0.0f : left + static_cast<float>(2 - static_cast<int>(season)) * seasonDays;   // (the whole seasons between this one and the winter)
			g.UntilSpring = g.In ? left : g.ToWinter + seasonDays;
			g.Short = std::max(0.0f, g.UntilSpring - t.FoodDays());
			return g;
		}
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
					{ "book", &m_Observer.BookBox }, { "follow", &m_Observer.FollowBox }, { "news", &m_Observer.NewsBox }, { "selection", &m_Observer.SelectionBox } };
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
		if (m_Clans.Count() == 0) return { false, "no clans (tribe build, clan new)" };
		// U2: what the god's list shows of each clan, and its SocietyLine (the measure the list draws in this same frame)
		if (a[0] == "clans") return { true, ObserverClansText() };
		if (a[0] == "tip")   // (a script has no mouse: holds the quality of life tooltip of a clan's row open, for a capture)
		{
			if (a.size() < 2) return { false, "observer tip <clan id or name>|off" };
			if (a[1] == "off") { m_Observer.Tip = -1; return { true, "tip off" }; }
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
			char x[32], z[32];
			std::snprintf(x, sizeof x, "%.2f", static_cast<double>(e->Where.x));
			std::snprintf(z, sizeof z, "%.2f", static_cast<double>(e->Where.y));
			const CommandResult r = m_GameView.Command({ "overview", x, z, "45" }, m_Actors, m_Player, m_World, m_Camera);
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
		else return { false, std::string("observer on|off|toggle|status|next|prev|sheet <") + kSheets + ">|screen on|off|pause [on|off]|resume|speed <1-20>|faster|slower|clans|tip <clan>|off|chronicle [all|<clan>] [kind...] [weight <n>]|go <event>|newest|person <name>|sample|off|follow [name]|off|<clan id or name>" };
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
		m_Observer.NewsBox = m_Observer.FollowBox = m_Observer.SelectionBox = {};
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
			DrawObserverNews(ox, w);
		}
		DrawHandBar(ox, oy, w, h);   // T201: the god's hand, at the bottom between the list and the book
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
					if (w.Short <= 0.0f) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", L.Text("covered").c_str());
					else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(WinterColour(w)), "%s", L.Text("{0} d short", L.Num(w.Short, 0)).c_str());
					if (m_Observer.Tip < 0 && hovered(wa, ImGui::GetItemRectMax()))
						ImGui::SetTooltip("%s", (w.In ? L.Text("Winter: {0} days until the spring", L.Num(w.UntilSpring, 0)) : L.Text("Winter in {0} days: {1} days until the spring", L.Num(w.ToWinter, 0), L.Num(w.UntilSpring, 0)))
							.append("\n").append(L.Text("It has {0} days of food in hand", L.Num(t.FoodDays(), 1))).append("\n")
							.append(w.Short <= 0.0f ? L.Text("The winter is covered") : L.Text("It is {0} days of food short", L.Num(w.Short, 0))).c_str());
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
		for (auto it = events.rbegin(); it != events.rend() && it->Id > m_Observer.NewsSeen; ++it)
			if (!SheetKind(it->Kind).empty() && it->Weight >= 4) fresh.push_back(it->Id);
		for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) m_Observer.NewsList.push_back({ *it, now });
		m_Observer.NewsSeen = newest;
		while (m_Observer.NewsList.size() > 40) m_Observer.NewsList.pop_front();
		struct Shown { std::string Text; const HistoryEvent* Event; float Alpha; int Count; };
		std::vector<Shown> shown;
		const double life = static_cast<double>(m_Controls.Hud.NotifySeconds);
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
		const float top = std::max(m_Observer.BarBottom, m_Observer.FollowBox.Y1) + T::Gap;
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
			// [Calling] (T239): the god sends it a dream of a trade; the menu says, trade by trade, whether it would take it
			if (clanId >= 0 && !m.Player)
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
		// needs: the HUD's, each a bar with its number and its colour (good, warning, bad) and the danger mark at 25
		ImGui::Separator();
		const std::map<std::string, float> levels = t.NeedLevels(m);
		for (const std::string& n : m_Controls.Hud.Needs)
		{
			const auto it = levels.find(n);
			if (it == levels.end()) continue;
			const float v = it->second, v01 = std::clamp(v / 100.0f, 0.0f, 1.0f);
			label(Lang::Cap(L.Name("need", n, n)));
			const ImVec2 b0 = ImGui::GetCursorScreenPos();
			SmallBar(v01, 110.0f, T::Level(v01));
			const float mark = b0.x + 110.0f * 0.25f, h = ImGui::GetTextLineHeight();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(mark, b0.y + (h - 10.0f) * 0.5f), ImVec2(mark, b0.y + (h + 10.0f) * 0.5f + 2.0f), T::Outline, 2.0f);
			ImGui::SameLine();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Level(v01)), "%s", L.Num(v, 0).c_str());
			if (v01 <= 0.25f) { ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Bad), "%s", L.Text("danger").c_str()); }
		}
		label(L.Text("Mood"));
		const float mood = t.Mood(m.Name), grief = t.Grief(m.Name);
		value(L.Num(mood, 2) + (grief > 0.01f ? "   " + L.Text("grief {0}", L.Num(grief, 2)) : std::string()), mood < 0.0f ? T::Warn : T::Bone);
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
		const char* usage = "director hand status|build|omen|focus|calling <person>|off|place <building>|turn [degrees]|tip <sign>";
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
			return { true, "hand: empty" };
		}
		if (m_Clans.Count() == 0) return { false, "no clans (shell new, or tribe build and clan new)" };
		const Clan* c = m_Clans.Find(m_ActiveClan);
		if (!c || !c->Folk) return { false, "no clan under the hand" };
		if (sub == "build" || sub == "omen" || sub == "focus")
		{
			m_Hand.Menu = sub;
			m_Hand.Building.clear();
			m_Hand.Pinned = false;
			m_Observer.On = true;
			return { true, "hand: " + sub + " menu for the clan " + std::to_string(c->Id) };
		}
		if (sub == "place")
		{
			if (a.size() < 3) return { false, usage };
			if (!c->Folk->Buildings() || !c->Folk->Buildings()->count(a[2])) return { false, "no building " + a[2] };
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
		std::string s = "hand: clan " + std::to_string(m_ActiveClan) + ", menu " + (m_Hand.Menu.empty() ? std::string("none") : m_Hand.Menu);
		if (m_Hand.Building.empty()) return s + ", placing nothing";
		s += ", placing " + m_Hand.Building + " yaw " + Fixed(m_Hand.Yaw) + (m_Hand.Pinned ? " (pinned)" : "");
		if (!m_Hand.Has) return s + ", off the ground";
		const DirectorSignPreview& p = m_Hand.Preview;
		s += " at " + Fixed(m_Hand.X) + " " + Fixed(m_Hand.Z) + ": ";
		if (!p.Ok) return s + "red: " + p.Why;
		return s + "green, cost " + Fixed(p.Cost) + ", vote " + std::to_string(p.Yes) + " of " + std::to_string(p.Voters) + " (" + p.Outcome + ")" + (p.Warning.empty() ? std::string() : "; " + p.Warning);
	}

	const DirectorSignPreview& EditorApp::HandPreview(int clan, float x, float z)
	{
		const DirectorSign* open = m_Director.OpenSign(clan);
		const int kx = static_cast<int>(std::floor(x)), kz = static_cast<int>(std::floor(z));
		const int ko = open ? open->Id : 0, ki = static_cast<int>(std::floor(m_Director.Influence()));
		if (!m_Hand.Cached || kx != m_Hand.KeyX || kz != m_Hand.KeyZ || clan != m_Hand.KeyClan || ko != m_Hand.KeyOpen || ki != m_Hand.KeyInfluence || m_Hand.Building != m_Hand.KeyBuilding)
		{
			m_Hand.Preview = m_Director.PreviewSign(clan, m_Hand.Building, x, z);   // (once per change of the key, never every frame: it touches the clan's caches)
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
			const Clan* c = m_Clans.Find(static_cast<ClanId>(s.Clan));
			const std::string clan = c ? L.Name("clan", c->Name, c->Name) : std::to_string(s.Clan);
			const std::string b = L.Name("building", s.Building, s.Building);
			std::string text;
			if (s.Status == "waiting") text = L.Text("{0}: the sign is up ({1}); someone of the clan must see it", clan, b);
			else if (s.Status == "seen") text = L.Text("{0}: {1} saw the sign ({2})", clan, s.Messenger, b);
			else if (s.Status == "accepted") text = L.Text("{0} agree to raise it: {1} ({2} of {3})", clan, b, s.Yes, s.Voters);
			else if (s.Status == "refused") text = L.Text("{0} refuse the sign: {1} ({2} of {3})", clan, b, s.Yes, s.Voters);
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
		const Clan* clan = m_Clans.Find(m_ActiveClan);
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
			if (p.Ok)
			{
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", (L.Text("A sign here: {0} influence", L.Num(p.Cost, 0)) + (p.SiteOnly ? " " + L.Text("(they wanted it: only the place)") : std::string())).c_str());
				if (p.Voters > 0) ImGui::TextUnformatted(L.Text("Vote foreseen: {0} of {1}, {2}", p.Yes, p.Voters, L.Text(OutcomeText(p.Outcome))).c_str());
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
			if (p.Ok)
			{
				m_UiCommands.push_back("director sign " + std::to_string(clan->Id) + " " + m_Hand.Building + " " + Fixed(x) + " " + Fixed(z) + " " + Fixed(m_Hand.Yaw));
				m_UiCommands.push_back("director hand off");
			}
			else ShellToastShown(Lang::Cap(L.Line(p.Why)));
		}
	}

	void EditorApp::DrawHandBar(float ox, float oy, float w, float h)
	{
		namespace T = GameTheme;
		m_Hand.Box = m_Hand.MenuBox = {};
		if (m_Clans.Count() == 0) return;
		const Clan* clan = m_Clans.Find(m_ActiveClan);
		if (!clan || !clan->Folk) return;
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
			// the clan under the hand: its faith and the pressure the god has put on it
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
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(room, std::max(h * 0.45f, 160.0f)));
		if (ImGui::Begin("HandMenu##observer", nullptr, fixed & ~ImGuiWindowFlags_NoScrollbar))
		{
			const float width = std::min(room, 620.0f) - 2.0f * T::Pad;
			auto wrapped = [&](float& x, float bw, bool first)   // a row of buttons that wraps instead of growing wide
			{
				if (!first && x + ImGui::GetStyle().ItemSpacing.x + bw <= width) { ImGui::SameLine(); x += ImGui::GetStyle().ItemSpacing.x + bw; }
				else x = bw;
			};
			if (m_Hand.Menu == "build")
			{
				ImGui::TextUnformatted(L.Text("What the {0} know how to raise", name).c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s", L.Text("(a sign: {0} influence)", L.Num(cost("sign"), 0)).c_str());
				ImGui::Separator();
				struct Entry { std::string Id, Label; const BuildingDef* Def; };
				std::vector<Entry> known, unknown;
				if (const auto* defs = clan->Folk->Buildings())
					for (const auto& [id, def] : *defs) (clan->Folk->CanBuild(id) ? known : unknown).push_back({ id, Lang::Cap(L.Name("building", id, def.Name)), &def });
				auto byName = [](const Entry& a, const Entry& b) { return a.Label < b.Label; };
				std::sort(known.begin(), known.end(), byName);
				std::sort(unknown.begin(), unknown.end(), byName);
				float x = 0.0f;
				for (size_t i = 0; i < known.size(); ++i)
				{
					const Entry& e = known[i];
					wrapped(x, ImGui::CalcTextSize(e.Label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f, i == 0);
					if (ImGui::Button((e.Label + "###b" + e.Id).c_str())) run("director hand place " + e.Id);
					std::string does;
					for (const std::string& line : BuildingDoes(*e.Def)) does += "\n" + line;
					tip(L.Text("Place its sign on the ground ({0} x {1} m)", L.Num(e.Def->Footprint.x, 0), L.Num(e.Def->Footprint.y, 0)) + does);
				}
				if (known.empty()) ImGui::TextDisabled("%s", L.Text("They know how to raise nothing yet").c_str());
				if (!unknown.empty() && ImGui::TreeNodeEx("unknown", ImGuiTreeNodeFlags_None, "%s", L.Text("They do not know how ({0})", static_cast<int>(unknown.size())).c_str()))
				{
					x = 0.0f;
					ImGui::BeginDisabled();
					for (size_t i = 0; i < unknown.size(); ++i)
					{
						const Entry& e = unknown[i];
						wrapped(x, ImGui::CalcTextSize(e.Label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f, i == 0);
						ImGui::Button((e.Label + "###u" + e.Id).c_str());
						tip(L.Text("The {0} do not know how to raise it yet: a vision first", name));
					}
					ImGui::EndDisabled();
					ImGui::TreePop();
				}
			}
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
}
