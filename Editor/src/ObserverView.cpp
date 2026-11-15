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
	}

	CommandResult EditorApp::ObserverCommand(const std::vector<std::string>& a)
	{
		static const char* kSheets = "members|village|board|homes|sites|knowledge|relations|log";
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
				const std::pair<const char*, const ObserverState::Box*> boxes[] = { { "bar", &m_Observer.BarBox }, { "card", &m_Observer.CardBox }, { "book", &m_Observer.BookBox } };
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
		if (a[0] == "next" || a[0] == "prev")
		{
			const auto& all = m_Clans.All();
			size_t i = 0;
			while (i < all.size() && all[i]->Id != m_ActiveClan) ++i;
			i = (i + (a[0] == "next" ? 1 : all.size() - 1)) % all.size();
			m_ActiveClan = all[i]->Id;
		}
		else if (const Clan* c = m_Clans.Find(a[0])) m_ActiveClan = c->Id;
		else return { false, std::string("observer on|off|toggle|status|next|prev|sheet <") + kSheets + ">|screen on|off|pause [on|off]|resume|speed <1-20>|faster|slower|<clan id or name>" };
		m_Observer.On = true;
		const Clan* c = m_Clans.Find(m_ActiveClan);
		return { true, "observing the clan " + std::to_string(m_ActiveClan) + (c ? " " + c->Name : std::string()) };
	}

	void EditorApp::DrawObserver(float ox, float oy, float w, float h)
	{
		m_Observer.BookWidth = 0.0f;
		m_Observer.CardBottom = 0.0f;
		m_Observer.BarBottom = 0.0f;
		m_Observer.BarBox = m_Observer.CardBox = m_Observer.BookBox = {};
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

		// ---- top left, under the camera buttons: the clans to pick from and the picked one in a few lines
		ImGui::SetNextWindowPos(ImVec2(ox + T::Margin, cardTop));
		if (ImGui::Begin("Clan##observer", nullptr, fixed))
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
			const char* tierText = tier == "survival" ? "food (survival)" : tier == "warmth" ? "warmth (fire and roof)" : tier == "safety" ? "safety" : tier == "growth" ? "all covered: growing" : tier.c_str();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tier == "survival" ? T::Bad : tier == "growth" ? T::Good : T::Warn), "%s", L.Text(tierText).c_str());
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
		ImGui::End();

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
			static const char* kSheets[][2] = { { "members", "Members" }, { "village", "Village" }, { "board", "Job board" }, { "homes", "Homes" }, { "sites", "Sites" },
				{ "knowledge", "Knowledge" }, { "relations", "Clans" }, { "log", "Log" } };
			for (const auto& sheet : kSheets)
			{
				const std::string id = sheet[0];
				const ImGuiTabItemFlags flags = m_Observer.Ask == id ? ImGuiTabItemFlags_SetSelected : 0;
				if (!ImGui::BeginTabItem((L.Text(sheet[1]) + "###" + id).c_str(), nullptr, flags)) continue;
				m_Observer.Tab = id;
				if (m_Observer.Folded) { ImGui::EndTabItem(); continue; }   // (folded: the tabs only; a click on one unfolds it below)
				ImGui::BeginChild("sheet");
				if (id == "members")
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
