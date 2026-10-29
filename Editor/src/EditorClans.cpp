// The editor's side of H13: several clans in one world (GB/Game/Clans.h). Clan 0 is the first tribe (m_Tribe, `tribe build`, saved in the "ai"
// section as before); `clan new` makes the others, kept by the clan world, with actors of their own placed by the site chooser away from the other
// camps; `clan select` chooses the clan the Tribe panel and the `tribe` reports show. The clans besides the first are saved in the "clans" section.
#include "EditorApp.h"
#include "GB/Log.h"
#include "GB/Gameplay/WorldResources.h"
#include <algorithm>
#include <cstdlib>
#include <imgui.h>

namespace GB::Editor
{
	Tribe& EditorApp::ActiveTribe()
	{
		Clan* c = m_Clans.Find(m_ActiveClan);
		return c ? *c->Folk : m_Tribe;
	}

	void EditorApp::PrepareClanTribe(Tribe& t)
	{
		t.Bind(m_SceneEditor.World(), m_Gameplay, m_Items, m_Recipes, m_NodeDefs);
		t.SetInteractives(&m_Interactives.Defs);
		t.SetTechs(&m_Techs);
		t.SetBuildings(&m_Buildings.Defs);   // H8: every clan builds
		t.SetGround([this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; });
		if (!m_Regions.Empty()) t.SetRegions(&m_Regions, [this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; });
		if (!m_Regions.Empty() && m_World.IsBuilt()) t.SetSiteChooser(SettlementSiteChooser(t, ResourceGroundOf(m_World.World())));   // H8: where it moves to
		// its children get actors of the clan's model (that of the first tribe: the editor keeps one)
		t.SetSpawner([this](const std::string& name, const glm::vec2& at, float growth) -> ITribeBody*
		{
			if (m_Actors.Find(name)) return nullptr;
			std::string error;
			const glm::vec3 near(at.x + 0.8f, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(at.x + 0.8f, at.y) : 0.0f, at.y);
			if (!m_Player.Spawn(name, m_TribeModel, m_World, m_SceneEditor, m_AssetRoot, near, m_Steps.get(), error, true)) { GB_CLIENT_WARN("no actor for the newborn {}: {}", name, error); return nullptr; }
			if (const Entity* e = m_SceneEditor.World().Find(m_SceneEditor.World().FindByName(name)); e && e->Gameplay && !m_Gameplay.Find(name))
				m_Gameplay.Create(name)->FromComponent(*e->Gameplay);
			m_Actors.PrepareAbilities();
			m_TribeBodies.push_back(MakeTribeBody(name));
			m_TribeBodies.back()->SetGrowth(growth);
			return m_TribeBodies.back().get();
		});
	}

	void EditorApp::ClearKeptClans()
	{
		std::vector<ClanId> kept;
		for (const auto& c : m_Clans.All()) if (c->Kept()) kept.push_back(c->Id);
		for (const ClanId id : kept)
		{
			const Clan* c = m_Clans.Find(id);
			for (const TribeMember& m : c->Folk->Members())
				m_TribeBodies.erase(std::remove_if(m_TribeBodies.begin(), m_TribeBodies.end(), [&](const std::unique_ptr<ITribeBody>& b) { return b.get() == m.Body; }), m_TribeBodies.end());
			m_Clans.Remove(id);
		}
		if (!m_Clans.Find(m_ActiveClan)) m_ActiveClan = 0;
	}

	void EditorApp::RestoreClans(const std::string& json)
	{
		for (const ClanWorld::SavedClan& s : ClanWorld::SavedClans(json))
		{
			if (!s.Kept) continue;
			Clan& c = m_Clans.Add(s.Name, nullptr, s.Id, &m_Tribe.Config);
			PrepareClanTribe(*c.Folk);
			for (const auto& [name, role] : s.Members)
			{
				if (!m_Actors.Find(name)) { GB_CLIENT_WARN("the saved clan {} has {} but there is no such actor", s.Name, name); continue; }
				if (AbilitySystemComponent* asc = m_Gameplay.Find(name)) asc->Tags().RemoveAll("State.Busy");   // (see RestoreTribe)
				m_TribeBodies.push_back(MakeTribeBody(name));
				c.Folk->AddMember(name, role, m_TribeBodies.back().get());
			}
		}
		if (!m_Clans.FromJson(json, true)) GB_CLIENT_WARN("the clans of the save could not be read whole: some start again");
	}

	CommandResult EditorApp::ClanCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "list" : a[0];
		auto clanArg = [&](size_t i) -> Clan* { return i < a.size() ? m_Clans.Find(a[i]) : nullptr; };
		auto lines = [](std::string s) { while (!s.empty() && s.back() == '\n') s.pop_back(); for (char& c : s) if (c == '\n') c = '|'; return s; };
		if (sub == "new")
		{
			if (a.size() < 2) return { false, "usage: clan new <name> [x z] [members] [model.fbx|fox]" };
			const std::string name = a[1];
			if (m_Clans.Find(name)) return { false, "there is a clan called '" + name + "' already" };
			std::string model = m_TribeModel;
			std::vector<float> numbers;
			for (size_t i = 2; i < a.size(); ++i)
			{
				if (PlayerMode::IsFbx(a[i]) || a[i] == "fox" || a[i] == "cesium") model = a[i];
				else numbers.push_back(static_cast<float>(std::atof(a[i].c_str())));
			}
			int members = 4;
			if (numbers.size() == 1 || numbers.size() == 3) members = std::max(1, std::min(200, static_cast<int>(numbers.back())));
			glm::vec2 camp(0.0f);
			std::string where;
			if (numbers.size() >= 2) { camp = { numbers[0], numbers[1] }; where = "at the place given"; }
			else
			{
				// the site chooser (H8's when it gives one; otherwise the best dry anchor of a region away from every other camp)
				SyncRegions();
				m_Clans.SetRegions(&m_Regions);
				CampSite site;
				if (!m_Clans.ChooseHome(90.0f, nullptr, site)) return { false, "no place for a new clan: give x z (a generated world lets the site chooser pick a region)" };
				camp = site.At;
				where = site.Why;
			}
			Clan& c = m_Clans.Add(name, nullptr, -1, &m_Tribe.Config);
			Tribe& t = *c.Folk;
			// a well by the camp (its own name: the interactives are found by name)
			{
				const auto def = m_Interactives.Defs.find("well");
				const InteractiveDef well = def != m_Interactives.Defs.end() ? def->second : DefaultInteractives().at("well");
				const glm::vec2 at = camp + glm::vec2(4.0f, -6.0f);
				BuildInteractive(m_SceneEditor.World(), well, "Well " + name, NullEntity, { at.x, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(at.x, at.y) : 0.0f, at.y });
				SyncGameplay();
			}
			std::vector<std::string> names;
			for (size_t k = 0; static_cast<int>(names.size()) < members; ++k)   // the names of the data, skipping those any clan or actor uses
				if (const std::string n = MemberName(t.Config.Work, k); !m_Clans.NameTaken(n) && !m_Actors.Find(n)) names.push_back(n);
			std::vector<ITribeBody*> bodies;
			for (int i = 0; i < members; ++i)
			{
				const int col = i % 8, row = i / 8, inRow = std::min(8, members - row * 8);
				const CommandResult r = Execute("actor spawn " + names[static_cast<size_t>(i)] + " \"" + model + "\" " + std::to_string(camp.x + (static_cast<float>(col) - static_cast<float>(inRow - 1) * 0.5f) * 1.5f)
					+ " " + std::to_string(camp.y + 1.5f * static_cast<float>(row)) + " ai");
				if (!r.Ok) { m_Clans.Remove(c.Id); return r; }
				m_TribeBodies.push_back(MakeTribeBody(names[static_cast<size_t>(i)]));
				bodies.push_back(m_TribeBodies.back().get());
			}
			PrepareClanTribe(t);
			t.Config.Camp = camp;
			t.Config.CampY = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(camp.x, camp.y) : 0.0f;
			t.Store().Add(m_Items, "wood", 2 + 2 * members);    // what they found lying around on the first day (as `tribe build`)
			t.Store().Add(m_Items, "stone", 3 * members);
			const std::vector<std::string> roles = t.PlanRoles(members);
			for (int i = 0; i < members; ++i)
				t.AddMember(names[static_cast<size_t>(i)], static_cast<size_t>(i) < roles.size() ? roles[static_cast<size_t>(i)] : std::string("forager"), bodies[static_cast<size_t>(i)]);
			m_SeasonVisuals = true;
			m_Panels.Tribe = true;
			return { true, "clan " + std::to_string(c.Id) + " " + name + ": " + std::to_string(members) + " members at (" + std::to_string(static_cast<int>(camp.x)) + ", "
				+ std::to_string(static_cast<int>(camp.y)) + ") " + where + "; clan list | clan select " + name + " | clan relations" };
		}
		if (sub == "list") return { true, lines(m_Clans.Report()) };
		if (sub == "select")
		{
			Clan* c = clanArg(1);
			if (!c) return { false, "usage: clan select <id|name>" };
			m_ActiveClan = c->Id;
			m_Panels.Tribe = true;
			if (!c->Folk->Members().empty()) SelectActor(c->Folk->Members().front().Name, false);
			return { true, "the Tribe panel and the tribe reports show the " + c->Name };
		}
		if (sub == "relations") return { true, lines(m_Clans.RelationsReport()) };
		if (sub == "territory") return { true, lines(m_Clans.TerritoryReport()) };
		if (sub == "meetings") return { true, lines(m_Clans.MeetingsReport()) };
		if (sub == "log")
		{
			const size_t n = a.size() > 1 ? static_cast<size_t>(std::max(1, std::atoi(a[1].c_str()))) : 12;
			const auto& log = m_Clans.Log();
			std::string s;
			for (size_t i = log.size() > n ? log.size() - n : 0; i < log.size(); ++i) s += log[i] + " | ";
			return { true, s.empty() ? "nothing has happened between the clans yet" : s };
		}
		if (sub == "trade" || sub == "visit")
		{
			Clan* x = clanArg(1);
			Clan* y = clanArg(2);
			if (!x || !y) return { false, "usage: clan " + sub + " <a> <b>" + (sub == "trade" ? " [force]" : "") };
			const bool force = a.size() > 3 && a[3] == "force";
			const std::string why = sub == "trade" ? m_Clans.ProposeTrade(x->Id, y->Id, force) : m_Clans.ProposeVisit(x->Id, y->Id, force);
			if (!why.empty()) return { false, why };
			return { true, lines(m_Clans.MeetingsReport()) };
		}
		if (sub == "wed")
		{
			if (a.size() < 3) return { false, "usage: clan wed <member> <member>" };
			const std::string why = m_Clans.Wed(a[1], a[2]);
			return { why.empty(), why.empty() ? a[1] + " and " + a[2] + " are wed" : why };
		}
		if (sub == "move")
		{
			Clan* to = clanArg(2);
			if (a.size() < 3 || !to) return { false, "usage: clan move <member> <clan>" };
			const std::string why = m_Clans.MoveMember(a[1], to->Id, "moved by hand");
			return { why.empty(), why.empty() ? a[1] + " is of the " + to->Name + " now" : why };
		}
		return { false, "usage: clan new <name> [x z] [members] [model] | list | select <id|name> | relations | trade <a> <b> [force] | visit <a> <b> | wed <a> <b> | move <member> <clan> | territory | meetings | log [n]" };
	}

	void EditorApp::DrawClansSection()
	{
		if (m_Clans.Count() < 2 || !ImGui::CollapsingHeader("Clans", ImGuiTreeNodeFlags_DefaultOpen)) return;
		for (const auto& c : m_Clans.All())
		{
			int alive = 0;
			for (const TribeMember& m : c->Folk->Members()) alive += m.Dead ? 0 : 1;
			const std::string label = std::to_string(c->Id) + " " + c->Name + " (" + std::to_string(alive) + " alive)";
			if (ImGui::Selectable(label.c_str(), c->Id == m_ActiveClan)) m_ActiveClan = c->Id;
		}
		if (ImGui::BeginTable("relations", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("pair");
			ImGui::TableSetupColumn("stance");
			ImGui::TableSetupColumn("attitude");
			ImGui::TableSetupColumn("trust");
			ImGui::TableHeadersRow();
			const auto& all = m_Clans.All();
			for (size_t i = 0; i < all.size(); ++i)
				for (size_t j = i + 1; j < all.size(); ++j)
				{
					const ClanId x = all[i]->Id, y = all[j]->Id;
					const std::string stance = m_Clans.Stance(x, y);
					const ImVec4 colour = stance == "hostile" ? ImVec4(0.95f, 0.4f, 0.35f, 1.0f) : stance == "allied" || stance == "friendly" ? ImVec4(0.45f, 0.9f, 0.5f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::Text("%s - %s", all[i]->Name.c_str(), all[j]->Name.c_str());
					ImGui::TableNextColumn(); ImGui::TextColored(colour, "%s", stance.c_str());
					ImGui::TableNextColumn(); ImGui::Text("%.2f", static_cast<double>(m_Clans.Attitude(x, y)));
					ImGui::TableNextColumn(); ImGui::Text("%.2f", static_cast<double>(m_Clans.Trust(x, y)));
				}
			ImGui::EndTable();
		}
		if (ImGui::TreeNode("Meetings and territory"))
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(m_Clans.MeetingsReport().c_str());
			ImGui::TextUnformatted(m_Clans.TerritoryReport().c_str());
			ImGui::PopTextWrapPos();
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Between the clans (log)"))
		{
			const auto& log = m_Clans.Log();
			for (size_t i = log.size() > 30 ? log.size() - 30 : 0; i < log.size(); ++i) ImGui::TextWrapped("%s", log[i].c_str());
			ImGui::TreePop();
		}
		ImGui::Separator();
	}
}
