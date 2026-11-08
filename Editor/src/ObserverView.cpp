// The observer of the free mode (`observer on|off|<clan>|next`, the "Clans" button over the viewport, View > Observe the clans): the simulation seen
// clan by clan with the game's look (GameTheme) inside the editor's viewport. A card top left picks the clan (the same one the Tribe panel shows,
// m_ActiveClan) and sums it up; the clan book on the right draws the game's own sheets for that clan (GameSheets.cpp: the village, the board, the
// homes, what it knows, its relations) plus its members, its sites and its log, so the game and the observer never tell the same thing two ways.
// Nothing here decides or changes the simulation: it reads the clans, and its buttons run editor commands (camera, observer), so a script can show
// the same. Every word goes through Lang (Lang.h).
#include "EditorApp.h"
#include "GameTheme.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
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
		if (a.empty() || a[0] == "on" || a[0] == "toggle")
		{
			m_Observer.On = a.empty() || a[0] == "on" ? true : !m_Observer.On;
			return { true, m_Observer.On ? "observer on: the clan " + std::to_string(m_ActiveClan) + " (observer <clan> | next | off)" : "observer off" };
		}
		if (a[0] == "off") { m_Observer.On = false; return { true, "observer off" }; }
		if (a[0] == "tree") { m_Observer.Tree = !m_Observer.Tree; m_Observer.On = true; return { true, m_Observer.Tree ? "tree of knowledge open" : "tree of knowledge closed" }; }
		if (a[0] == "fold") { m_Observer.Folded = !m_Observer.Folded; m_Observer.On = true; return { true, m_Observer.Folded ? "book folded" : "book open" }; }
		if (a[0] == "status") return { true, std::string(m_Observer.On ? "observer on" : "observer off") + ", clan " + std::to_string(m_ActiveClan) + ", sheet " + m_Observer.Tab };
		if (a[0] == "sheet" && a.size() > 1) { m_Observer.Ask = a[1]; m_Observer.On = true; return { true, "sheet " + a[1] }; }
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
		else return { false, std::string("observer on|off|toggle|status|next|prev|sheet <") + kSheets + ">|<clan id or name>" };
		m_Observer.On = true;
		const Clan* c = m_Clans.Find(m_ActiveClan);
		return { true, "observing the clan " + std::to_string(m_ActiveClan) + (c ? " " + c->Name : std::string()) };
	}

	void EditorApp::DrawObserver(float ox, float oy, float w, float h)
	{
		m_Observer.BookWidth = 0.0f;
		m_Observer.CardBottom = 0.0f;
		if (!m_Observer.On || m_Clans.Count() == 0) return;
		if (!m_Clans.Find(m_ActiveClan)) m_ActiveClan = m_Clans.All().front()->Id;
		const Clan& clan = *m_Clans.Find(m_ActiveClan);
		if (!clan.Folk) return;
		const Tribe& t = *clan.Folk;
		namespace T = GameTheme;
		const T::Scope theme;
		if (m_Observer.Tree)   // the tree of knowledge alone (the card and the book would come over it)
		{
			DrawTechTree(t, false, ox, oy, w, h, m_Observer.Tree, m_Observer.TreePick);
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
		ImGui::SetNextWindowPos(ImVec2(ox + T::Margin, oy + 52.0f));
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
		}
		ImGui::End();

		// ---- right: the clan book of the observed clan, a tab per sheet (the game's own sheets for this clan, without the player's buttons)
		const float bw = std::clamp(w * 0.42f, 320.0f, 560.0f);
		ImGui::SetNextWindowPos(ImVec2(ox + w - T::Margin, oy + 8.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(bw, m_Observer.Folded ? 64.0f : std::max(h - 8.0f - T::Margin, 160.0f)), ImGuiCond_Always);
		const std::string title = L.Name("clan", clan.Name, clan.Name) + "###observerbook";
		if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)
			&& ImGui::BeginTabBar("sheets", ImGuiTabBarFlags_FittingPolicyScroll))   // (whole names, scrolled, in a narrow viewport)
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
}
