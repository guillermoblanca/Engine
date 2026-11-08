// The editor's side of H13: several clans in one world (GB/Game/Clans.h). Clan 0 is the first tribe (m_Tribe, `tribe build`, saved in the "ai"
// section as before); `clan new` makes the others, kept by the clan world, with actors of their own placed by the site chooser away from the other
// camps; `clan select` chooses the clan the Tribe panel and the `tribe` reports show. The clans besides the first are saved in the "clans" section.
#include "EditorApp.h"
#include "GB/Log.h"
#include "GB/Gameplay/WorldResources.h"
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>

namespace GB::Editor
{
	Tribe& EditorApp::ActiveTribe()
	{
		Clan* c = m_Clans.Find(m_ActiveClan);
		return c ? *c->Folk : m_Tribe;
	}

	const Tribe& EditorApp::PlayTribe() const
	{
		if (!m_Game.Player.empty() && !m_Tribe.FindMember(m_Game.Player))
			if (const Clan* c = m_Clans.ClanOf(m_Game.Player); c && c->Folk) return *c->Folk;
		return m_Tribe;
	}

	Tribe& EditorApp::PlayTribe() { return const_cast<Tribe&>(static_cast<const EditorApp*>(this)->PlayTribe()); }

	void EditorApp::PrepareClanTribe(Tribe& t)
	{
		t.Bind(m_SceneEditor.World(), m_Gameplay, m_Items, m_Recipes, m_NodeDefs);
		t.SetInteractives(&m_Interactives.Defs);
		t.SetTechs(&m_Techs);
		t.SetBuildings(&m_Buildings.Defs);   // H8: every clan builds
		t.SetGround([this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; });
		t.SetWellDigger([this](const std::string& name, const glm::vec2& at) { PlaceWell(name, at); });   // a big village digs more wells
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

	// A well of the data (its ability system made at once): by a new camp, or where a dug well stands (Tribe::SetWellDigger)
	void EditorApp::PlaceWell(const std::string& name, const glm::vec2& at)
	{
		const auto def = m_Interactives.Defs.find("well");
		const InteractiveDef well = def != m_Interactives.Defs.end() ? def->second : DefaultInteractives().at("well");
		BuildInteractive(m_SceneEditor.World(), well, name, NullEntity, { at.x, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(at.x, at.y) : 0.0f, at.y });
		SyncNewGameplay();
	}

	// N7: a clan founded by a split: its camp and well, its tribe bound to the world; the members move in (ClanWorld::Split)
	ClanId EditorApp::FoundClan(ClanId parent, const glm::vec2& camp, const std::string& name)
	{
		(void)parent;
		if (m_Clans.Find(name)) return -1;
		Clan& c = m_Clans.Add(name, nullptr, -1, &m_Tribe.Config);
		PlaceWell("Well " + name, camp + glm::vec2(4.0f, -6.0f));
		Tribe& t = *c.Folk;
		PrepareClanTribe(t);
		t.Config.Economy.Camp = camp;
		t.Config.Economy.CampY = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(camp.x, camp.y) : 0.0f;
		return c.Id;
	}

	CommandResult EditorApp::ClanCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "list" : a[0];
		auto clanArg = [&](size_t i) -> Clan* { return i < a.size() ? m_Clans.Find(a[i]) : nullptr; };
		if (sub == "polities")   // V7: the rank of each settlement and the polities the alliances make
		{
			std::string s;
			for (const auto& c : m_Clans.All())
				s += (s.empty() ? "" : " | ") + c->Name + ": " + (m_Clans.RankOf(c->Id).empty() ? std::string("no rank") : m_Clans.RankOf(c->Id)) + " (" + std::to_string(c->Folk ? std::count_if(c->Folk->Members().begin(), c->Folk->Members().end(), [](const TribeMember& m) { return !m.Dead; }) : 0) + " people, " + std::to_string(c->Folk ? c->Folk->BuildingCount() : 0) + " buildings)";
			for (const std::vector<ClanId>& g : m_Clans.Polities())
			{
				s += " | polity: " + m_Clans.PolityName(g) + " (";
				for (size_t i = 0; i < g.size(); ++i) if (const Clan* c = m_Clans.Find(g[i])) s += (i ? ", " : "") + c->Name;
				s += ")";
			}
			return { true, s };
		}
		if (sub == "ally")   // V7: two clans agree an alliance now
		{
			Clan* x = clanArg(1);
			Clan* y = clanArg(2);
			if (!x || !y) return { false, "usage: clan ally <a> <b>" };
			if (const std::string error = m_Clans.Ally(x->Id, y->Id); !error.empty()) return { false, error };
			return { true, x->Name + " and " + y->Name + " are allies" };
		}
		if (sub == "leap")   // V6: every clan lives some years in aggregate (Tribe::Leap): births, deaths, what it learns, written to the history
		{
			const float years = a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : 0.0f;
			if (years <= 0.0f) return { false, "usage: clan leap <years>" };
			std::string s = std::to_string(static_cast<int>(years)) + " years pass";
			for (const std::string& l : m_Clans.Leap(years)) s += " | " + l;
			SyncNewGameplay();   // (the newborns' bodies)
			return { true, s };
		}
		auto lines = [](std::string s) { while (!s.empty() && s.back() == '\n') s.pop_back(); for (char& c : s) if (c == '\n') c = '|'; return s; };
		if (sub == "new")
		{
			if (a.size() < 2) return { false, "usage: clan new <name> [x z] [members] [model.fbx|fox]" };
			const std::string name = a[1];
			if (m_Clans.Find(name)) return { false, "there is a clan called '" + name + "' already" };
			FinishWorldBuild();
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
			const SpawnBatch crew(*this);   // (its actors without reloading the scene's systems each)
			Clan& c = m_Clans.Add(name, nullptr, -1, &m_Tribe.Config);
			Tribe& t = *c.Folk;
			// a well by the camp (its own name: the interactives are found by name)
			{
				const auto def = m_Interactives.Defs.find("well");
				const InteractiveDef well = def != m_Interactives.Defs.end() ? def->second : DefaultInteractives().at("well");
				const glm::vec2 at = camp + glm::vec2(4.0f, -6.0f);
				BuildInteractive(m_SceneEditor.World(), well, "Well " + name, NullEntity, { at.x, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(at.x, at.y) : 0.0f, at.y });
				SyncNewGameplay();
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
			t.Config.Economy.Camp = camp;
			t.Config.Economy.CampY = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(camp.x, camp.y) : 0.0f;
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
		if (sub == "property")   // N1b: whose the food is, clan by clan
		{
			Clan* x = clanArg(1);
			if (!x || !x->Folk || a.size() < 3 || (a[2] != "communal" && a[2] != "household" && a[2] != "mixed")) return { false, "usage: clan property <clan> communal|household|mixed" };
			x->Folk->Config.Economy.Property = a[2];
			return { true, x->Name + ": the food is " + (a[2] == "communal" ? std::string("the clan's") : a[2] == "household" ? std::string("each household's first") : std::string("half the household's")) };
		}
		if (sub == "split")   // N7: a part of a clan leaves to found a new one now
		{
			Clan* x = clanArg(1);
			if (!x) return { false, "usage: clan split <clan>" };
			const std::string why = m_Clans.Split(x->Id, true);
			return { why.empty(), why.empty() ? lines(m_Clans.Report()) : why };
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
		return { false, "usage: clan new <name> [x z] [members] [model] | list | select <id|name> | relations | trade <a> <b> [force] | visit <a> <b> | split <clan> | wed <a> <b> | property <clan> communal|household|mixed | move <member> <clan> | territory | meetings | log [n]" };
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

namespace GB::Editor
{
	// A world that looks lived in: clans founded with their villages already standing. Each settles where the site chooser says (by the water, near
	// what its trades want, on flat ground); the trees of a clearing around its camp are felled and its buildings stand in a ring around the fire,
	// their doors toward it: homes for every member (a longhouse and huts), a store and a raised granary, the work stations, a hearth and a totem.
	std::string EditorApp::RaiseVillage(Tribe& t, const std::string& label)
	{
		const glm::vec2 camp = t.Config.Economy.Camp;
		const Terrain& ground = m_World.GetTerrain();
		const GeneratedWorld& world = m_World.World();
		// the clearing: the trees and rocks of the scenery within its reach go (the resource nodes stay, the clan lives off them)
		constexpr float kClearing = 30.0f;
		std::vector<EntityId> felled;
		for (const auto& [id, e] : m_SceneEditor.World().All())
			if (e.Prop && !e.Gameplay && e.Children.empty() && (e.Name.rfind("Tree ", 0) == 0 || e.Name.rfind("Rock ", 0) == 0)
				&& glm::distance(glm::vec2(e.Transform.Position.x, e.Transform.Position.z), camp) < kClearing) felled.push_back(id);
		for (const EntityId id : felled) m_SceneEditor.World().Destroy(id);

		// what the village has: beds for everyone and some to spare (a longhouse for a big clan, huts, at least two), then the rest of a settled camp
		const int members = static_cast<int>(t.Members().size());
		std::vector<std::string> plan;
		int beds = 0;
		if (members >= 10) { plan.push_back("longhouse"); beds += 8; }
		for (int huts = 0; beds < members + 2 || huts < 2; ++huts) { plan.push_back("hut"); beds += 4; }
		for (const char* id : { "storage", "granary", "workbench", "drying_rack", "smoking_rack", "hearth", "totem", "healer_hut", "hide_frame" }) plan.push_back(id);

		struct Standing { glm::vec2 At; float Radius; };
		std::vector<Standing> standing = { { camp, 4.0f }, { camp + glm::vec2(4.0f, -6.0f), 1.5f } };   // the fire and the well
		for (const auto& [id, e] : m_SceneEditor.World().All())   // and the resource nodes and interactives near (a building never stands on a bush)
			if (e.Gameplay && glm::distance(glm::vec2(e.Transform.Position.x, e.Transform.Position.z), camp) < kClearing + 6.0f && !m_Actors.Find(e.Name))
				standing.push_back({ { e.Transform.Position.x, e.Transform.Position.z }, 0.8f });
		auto fits = [&](const glm::vec2& at, const BuildingDef& def)
		{
			const float r = def.Radius();
			for (const Standing& s : standing) if (glm::distance(s.At, at) < s.Radius + r + 1.8f) return false;
			const float h = ground.GetHeight(at.x, at.y);
			for (const glm::vec2 corner : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(-1, 1), glm::vec2(1, 1) })
			{
				const glm::vec2 c = at + corner * (0.5f * def.Footprint);
				if (std::fabs(ground.GetHeight(c.x, c.y) - h) > 0.9f) return false;   // flat enough to stand on
				const Biome b = world.BiomeAt(c.x, c.y);
				if (b == Biome::Water || b == Biome::Shore) return false;
			}
			return true;
		};
		std::string raised, missed;
		int placed = 0;
		for (const std::string& id : plan)
		{
			const auto def = m_Buildings.Defs.find(id);
			if (def == m_Buildings.Defs.end()) continue;
			bool done = false;
			for (float radius = 8.0f + def->second.Radius(); radius < kClearing - 2.0f && !done; radius += 3.0f)
				for (int k = 0; k < 24 && !done; ++k)
				{
					// round the ring, starting each building a little further on so they spread instead of piling up on one side
					const float angle = (static_cast<float>(placed) * 2.4f + static_cast<float>(k) * 0.2618f);
					const glm::vec2 at = camp + radius * glm::vec2(std::cos(angle), std::sin(angle));
					if (!fits(at, def->second)) continue;
					const glm::vec2 toFire = camp - at;
					std::string name;
					if (const std::string error = t.RaiseBuilding(id, at, glm::degrees(std::atan2(toFire.x, toFire.y)), &name); !error.empty()) { missed += " " + id + " (" + error + ")"; done = true; break; }
					standing.push_back({ at, def->second.Radius() });
					raised += (raised.empty() ? "" : ", ") + name;
					++placed;
					done = true;
				}
			if (!done) missed += " " + id;
		}
		// the ground they tread: the yard around the fire, a ring before each door and a path from it to the fire (the grass grows short and thin)
		m_World.AddClearing(camp, 9.0f, kClearing - 6.0f, 0.75f);
		for (size_t i = 2; i < standing.size(); ++i)
			if (glm::distance(standing[i].At, camp) < kClearing && standing[i].Radius > 0.9f)
			{
				m_World.AddClearing(standing[i].At, standing[i].Radius + 0.5f, standing[i].Radius + 2.5f, 1.0f);
				m_World.AddPath(standing[i].At, camp, 1.4f, 0.9f);
			}
		m_World.RegrowGrass();
		SyncNewGameplay();   // (RaiseBuilding made each building's system: what is running elsewhere is not reloaded)
		return label + " at " + std::to_string(static_cast<int>(camp.x)) + ", " + std::to_string(static_cast<int>(camp.y)) + ": " + raised + " (" + std::to_string(felled.size()) + " trees and rocks cleared"
			+ (missed.empty() ? "" : "; no room for" + missed) + ")";
	}

	// Where the villages of a big world go, read from the terrain alone (the site chooser scores every place against every resource node, which a
	// map of kilometres with its wild nodes everywhere makes far too slow): flat meadow or forest edge, low, some way from the water but near it,
	// away from the border and far from each other.
	std::vector<glm::vec2> EditorApp::PickVillageSites(int count, std::vector<glm::vec2> sites) const
	{
		const GeneratedWorld& world = m_World.World();
		const Terrain& ground = *world.TerrainRef;
		const Heightmap& hm = ground.GetHeightmap();
		const float lowest = hm.GetMinHeight(), span = std::max(hm.GetMaxHeight() - hm.GetMinHeight(), 1.0f);
		const float extent = world.Desc.Extent(), margin = std::min(150.0f, extent * 0.1f), apart = std::max(250.0f, extent / 8.0f);
		struct Candidate { glm::vec2 At; float Score; };
		std::vector<Candidate> candidates;
		for (float z = hm.GetMin().y + margin; z <= hm.GetMax().y - margin; z += 25.0f)
			for (float x = hm.GetMin().x + margin; x <= hm.GetMax().x - margin; x += 25.0f)
			{
				const Biome b = world.BiomeAt(x, z);
				if (b != Biome::Meadow && b != Biome::Forest) continue;
				const float h = ground.GetHeight(x, z);
				float rough = 0.0f;
				for (int k = 0; k < 8; ++k)
				{
					const float angle = static_cast<float>(k) * 0.785398f;
					rough = std::max(rough, std::fabs(ground.GetHeight(x + 22.0f * std::cos(angle), z + 22.0f * std::sin(angle)) - h));
				}
				if (rough > 2.5f) continue;
				const float water = world.WaterDistanceAt(x, z);
				if (water < 35.0f) continue;
				const float score = (1.0f - rough / 2.5f) + (1.0f - glm::clamp((water - 60.0f) / 400.0f, 0.0f, 1.0f)) - 0.8f * (h - lowest) / span + (b == Biome::Meadow ? 0.2f : 0.0f);
				candidates.push_back({ { x, z }, score });
			}
		std::sort(candidates.begin(), candidates.end(), [](const Candidate& p, const Candidate& q) { return p.Score > q.Score; });
		for (const Candidate& c : candidates)
		{
			if (static_cast<int>(sites.size()) >= count) break;
			if (std::all_of(sites.begin(), sites.end(), [&](const glm::vec2& s) { return glm::distance(s, c.At) >= apart; })) sites.push_back(c.At);
		}
		return sites;
	}

	CommandResult EditorApp::VillageCommand(const std::vector<std::string>& args)
	{
		if (!args.empty() && args[0] == "report")
		{
			// `village report every <seconds>`: the same lines in the log every so many seconds of game (StepVillageReport); `off` stops them
			if (args.size() > 1 && args[1] == "off") { m_VillageReportEvery = 0.0; return { true, "village report: off" }; }
			if (args.size() > 2 && args[1] == "every") { m_VillageReportEvery = std::max(1.0, std::atof(args[2].c_str())); m_VillageReportNext = 0.0; }
			return { true, VillageReport() };
		}
		FinishWorldBuild();
		// `bare`: the clans are founded with their patch and their well, but nothing built: they have to raise their village themselves
		std::vector<std::string> a;
		bool bare = false;
		for (const std::string& s : args) { if (s == "bare") bare = true; else a.push_back(s); }
		if (!m_World.IsBuilt()) return { false, "no world (generate ... or terrain_graph preset ...)" };
		const SpawnBatch crew(*this);
		const int clans = a.size() > 0 ? std::clamp(std::atoi(a[0].c_str()), 1, 6) : 3;
		const int members = a.size() > 1 ? std::clamp(std::atoi(a[1].c_str()), 2, 40) : 8;
		// people: X Bot, the character of the packs with every animation (packs/full); CesiumMan when the packs are not there
		const std::string model = a.size() > 2 && a[2] != "-" ? a[2] : std::filesystem::exists("packs/full/X Bot.fbx") ? std::string("packs/full/X Bot.fbx") : std::string("cesium");
		const std::string quoted = "\"" + model + "\"";
		// A small world: the wild resource nodes over all of it and the site chooser's places. A big one (over a kilometre): the places of
		// PickVillageSites, each with a patch of resources of its own around it.
		const bool big = m_World.World().Desc.Extent() > 1000.0f;
		// (the seconds of each step go in the answer: founding the villages of a big world is a long wait)
		using Clock = std::chrono::steady_clock;
		std::string took;
		auto lap = [&took, mark = Clock::now()](const char* what) mutable { const auto now = Clock::now(); char t[64]; std::snprintf(t, sizeof t, "%s%s %.1f s", took.empty() ? "" : ", ", what, std::chrono::duration<double>(now - mark).count()); took += t; mark = now; };
		std::vector<glm::vec2> sites;
		if (a.size() >= 5) sites.push_back({ static_cast<float>(std::atof(a[3].c_str())), static_cast<float>(std::atof(a[4].c_str())) });   // the first village where it is asked for
		if (big)
		{
			sites = PickVillageSites(clans, sites);
			if (sites.empty()) return { false, "no flat dry ground for a village on this world" };
			lap("sites");
		}
		else if (SceneResources(m_SceneEditor.World(), m_Gameplay, m_NodeDefs).empty())
			if (const CommandResult r = Execute("resources world"); !r.Ok) return r;
		auto at = [&](size_t i) { return i < sites.size() ? " " + std::to_string(sites[i].x) + " " + std::to_string(sites[i].y) : std::string(); };
		// each village keeps its patch: `resources build` replaces the group called "Resources", so the one just made takes the village's number
		auto patch = [&](size_t i)
		{
			if (i >= sites.size()) return;
			Execute("resources build " + std::to_string(sites[i].x + 34.0f) + " " + std::to_string(sites[i].y + 20.0f) + " " + std::to_string(members));
			EntityWorld& w = m_SceneEditor.World();
			if (const EntityId root = w.FindByName("Resources"); root != NullEntity) w.Rename(root, "Village resources " + std::to_string(i + 1));
			lap("resources");
		};
		std::vector<std::pair<Tribe*, std::string>> villages;
		if (m_Tribe.Members().empty())
		{
			patch(0);
			if (const CommandResult r = Execute("tribe build " + quoted + at(0) + " " + std::to_string(members)); !r.Ok) return r;
			villages.push_back({ &m_Tribe, "the first tribe" });
			lap("tribe");
		}
		static const char* const kNames[] = { "Ash", "Reed", "Elk", "Fern", "Otter", "Flint" };
		for (const char* name : kNames)
		{
			if (static_cast<int>(villages.size()) >= clans || (big && villages.size() >= sites.size())) break;
			if (m_Clans.Find(name)) continue;
			patch(villages.size());
			const CommandResult r = Execute("clan new " + std::string(name) + at(villages.size()) + " " + std::to_string(members) + " " + quoted);
			if (!r.Ok) { GB_CLIENT_WARN("village: no clan {}: {}", name, r.Message); break; }
			if (Clan* c = m_Clans.Find(name)) villages.push_back({ &*c->Folk, std::string("clan ") + name });
			lap(name);
		}
		if (villages.empty()) return { false, "no clan to found (the first tribe exists: give more clans)" };
		std::string report;
		for (auto& [tribe, label] : villages)
		{
			if (big) tribe->Config.Performance.DetailRadius = std::max(tribe->Config.Performance.DetailRadius, 150.0f);   // a village seen across a meadow is full of people
			if (bare)
			{
				const glm::vec2 camp = tribe->Config.Economy.Camp;
				report += (report.empty() ? "" : " | ") + label + " at " + std::to_string(static_cast<int>(camp.x)) + ", " + std::to_string(static_cast<int>(camp.y)) + ": nothing built yet";
				continue;
			}
			report += (report.empty() ? "" : " | ") + RaiseVillage(*tribe, label);
			lap("buildings");
		}
		Execute("nav rebuild");   // the felled trees were walls
		lap("nav");
		return { true, report + " | took: " + took };
	}

	// One line per clan, to weigh how each one develops: its people, what it has built and is building, its store, its food and where it is
	// on the pyramid of needs, and what its updates cost (mean and slowest since it was founded). Above them, the game time and the frame.
	std::string EditorApp::VillageReport() const
	{
		char head[200];
		const double seconds = static_cast<double>(m_SimTick) / 60.0;   // (fixed ticks of 1/60 s)
		const ClanWorld::FrameCost& all = m_Clans.LastCost();
		std::snprintf(head, sizeof head, "village report at %.0f s of game (day %d): frame %.1f ms, the clans %.2f ms a tick (peak %.2f)", seconds,
			static_cast<int>(seconds / static_cast<double>(m_Tribe.Config.Needs.DaySeconds)) + 1, static_cast<double>(m_FrameMsSmooth), all.MeanMs, all.PeakMs);
		std::string s = head;
		for (const auto& c : m_Clans.All())
		{
			const Tribe& t = *c->Folk;
			if (t.Members().empty()) continue;
			int alive = 0;
			for (const TribeMember& m : t.Members()) if (!m.Dead) ++alive;
			int done = 0;
			std::string building;
			for (const Tribe::SettlementSite& site : t.SettlementSites())
			{
				if (site.State.Done) { ++done; continue; }
				char b[96];
				std::snprintf(b, sizeof b, "%s%s %.0f%%", building.empty() ? "" : ", ", site.Building.c_str(), static_cast<double>(site.State.Progress * 100.0f));
				building += b;
			}
			int stored = 0;
			std::string goods;
			for (const ItemSlot& slot : t.Store().Stacks()) stored += slot.Count;
			for (const char* item : { "wood", "stone", "berry", "fiber" }) if (const int n = t.Store().Count(item); n > 0) goods += std::string(goods.empty() ? "" : " ") + item + " " + std::to_string(n);
			const std::vector<BuildingNeed> wants = t.BuildingNeeds();
			char line[600];
			std::snprintf(line, sizeof line, "\n%s: %d/%zu alive | built %d%s%s%s | wants %s | store %d (%s) | food %.1f days, needs: %s, fire %s | cpu %.2f ms mean, %.2f peak over %lld ticks",
				c->Name.empty() ? "clan 0" : c->Name.c_str(), alive, t.Members().size(), done, building.empty() ? "" : ", building ", building.c_str(), "",
				wants.empty() ? "nothing" : wants.front().Building.c_str(), stored, goods.empty() ? "-" : goods.c_str(), static_cast<double>(t.FoodDays()), t.MaslowTier().c_str(),
				t.FireBurning() ? "burning" : t.HasFire() ? "out" : "none", t.MeanUpdateMs(), t.PeakUpdateMs(), t.UpdateCount());
			s += line;
			// P4: how far it climbed on its own: what it knows, its fields and harvests, the step it is after now
			int harvested = 0;
			for (const auto& [item, n] : t.Economy().Harvested) harvested += n;
			const std::vector<std::string>& goals = t.Knowledge().Goals();
			int housed = 0;
			for (const Tribe::Household& h : t.Households()) if (!h.Home.empty()) ++housed;
			std::snprintf(line, sizeof line, "\n    %s | knows %zu technologies | %zu fields, %d harvested | %zu households, %d with a house | next step: %s", t.EraName().empty() ? "no eras" : t.EraName().c_str(), t.Knowledge().Clan().size(), t.Economy().Fields.size(), harvested,
				t.Households().size(), housed, goals.empty() ? "-" : goals.front().c_str());
			s += line;
		}
		return s;
	}

	void EditorApp::StepVillageReport()
	{
		if (m_VillageReportEvery <= 0.0) return;
		const double seconds = static_cast<double>(m_SimTick) / 60.0;   // (fixed ticks of 1/60 s)
		if (seconds < m_VillageReportNext) return;
		m_VillageReportNext = seconds + m_VillageReportEvery;
		GB_CLIENT_INFO("{}", VillageReport());
	}
}
