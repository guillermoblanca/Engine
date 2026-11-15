// The observer of the free mode (`observer on|off|<clan>|next`, the "Clans" button over the viewport, View > Observe the clans): the simulation seen
// clan by clan with the game's look (GameTheme) inside the editor's viewport. A card top left picks the clan (the same one the Tribe panel shows,
// m_ActiveClan) and sums it up; the clan book on the right draws the game's own sheets for that clan (GameSheets.cpp: the village, the board, the
// homes, what it knows, its relations) plus its members, its sites and its log, so the game and the observer never tell the same thing two ways.
// Nothing here decides or changes the simulation: it reads the clans, and its buttons run editor commands (camera, observer), so a script can show
// the same. Every word goes through Lang (Lang.h).
// U1 (`observer screen on`): the god's screen, the observer over the whole window with no editor panel (EditorApp::DrawUI draws it like the game's
// screen), with the god's bar on top (DrawGodBar: the clock and its own pause and speed, the season, the weather) and the card and the book under it.
#include "EditorApp.h"
#include "GameTheme.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <optional>
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
		static const char* kSheets = "clan|members|village|board|homes|sites|knowledge|relations|log";
		// the observer going off takes its screen and its pause with it (a frozen editor with nothing that says so)
		auto off = [&]() { m_Observer.On = m_Observer.Screen = m_Observer.Paused = false; return CommandResult{ true, "observer off" }; };
		if (a.empty() || a[0] == "on" || a[0] == "toggle")
		{
			if (!a.empty() && a[0] == "toggle" && m_Observer.On) return off();
			m_Observer.On = true;
			return { true, "observer on: the clan " + std::to_string(m_ActiveClan) + " (observer <clan> | next | off)" };
		}
		if (a[0] == "off") return off();
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
					{ "book", &m_Observer.BookBox } };
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
		if (a[0] == "next" || a[0] == "prev")
		{
			const auto& all = m_Clans.All();
			size_t i = 0;
			while (i < all.size() && all[i]->Id != m_ActiveClan) ++i;
			i = (i + (a[0] == "next" ? 1 : all.size() - 1)) % all.size();
			m_ActiveClan = all[i]->Id;
		}
		else if (const Clan* c = m_Clans.Find(a[0])) m_ActiveClan = c->Id;
		else return { false, std::string("observer on|off|toggle|status|next|prev|sheet <") + kSheets + ">|screen on|off|pause [on|off]|resume|speed <1-20>|faster|slower|clans|tip <clan>|off|<clan id or name>" };
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
		m_Observer.ViewBox = { ox, oy, ox + w, oy + h };
		if (!m_Observer.On) return;
		namespace T = GameTheme;
		const T::Scope theme;
		// U1: on the god's screen the god's bar goes over the whole width at the top and the rest under it; in the editor's viewport the card goes
		// under the camera buttons and the book from the top
		const bool god = m_Observer.Screen && !m_Game.Active;
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
		if (bookOpen && ImGui::BeginTabBar("sheets", ImGuiTabBarFlags_FittingPolicyScroll))   // (whole names, scrolled, in a narrow viewport)
		{
			static const char* kSheets[][2] = { { "clan", "Clan" }, { "members", "Members" }, { "village", "Village" }, { "board", "Job board" }, { "homes", "Homes" }, { "sites", "Sites" },
				{ "knowledge", "Knowledge" }, { "relations", "Clans" }, { "log", "Log" } };
			for (const auto& sheet : kSheets)
			{
				const std::string id = sheet[0];
				const ImGuiTabItemFlags flags = m_Observer.Ask == id ? ImGuiTabItemFlags_SetSelected : 0;
				if (!ImGui::BeginTabItem((L.Text(sheet[1]) + "###" + id).c_str(), nullptr, flags)) continue;
				m_Observer.Tab = id;
				if (m_Observer.Folded) { ImGui::EndTabItem(); continue; }   // (folded: the tabs only; a click on one unfolds it below)
				ImGui::BeginChild("sheet");
				if (id == "clan") DrawClanSheet(clan);
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
								else if (ImGui::Selectable((m.Name + "###" + m.Name).c_str(), m_FreeSelected == m.Name, ImGuiSelectableFlags_SpanAllColumns)) m_FreeSelected = m.Name;
								if (!m.Dead && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Select {0} (its card goes under the clan's)", m.Name).c_str());
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
			// the way back to the editor, at the right end (or right after the rest, in a narrow window)
			const std::string back = L.Text("Editor") + "###editor";
			const float bw = ImGui::CalcTextSize(L.Text("Editor").c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 14.0f;
			ImGui::SameLine(std::max(after, ImGui::GetWindowContentRegionMax().x - bw));
			if (ImGui::Button(back.c_str())) run("observer screen off");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s [%s]", L.Text("Back to the editor").c_str(), keyOf("quit").c_str());
			m_Observer.BarBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
			m_Observer.BarBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, m_Observer.BarBottom };
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}
}
