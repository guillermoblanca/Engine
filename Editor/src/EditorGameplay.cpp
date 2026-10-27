// The editor's side of the Gameplay Ability System: the `garden` and `ability` commands and the Abilities panel.
#include "EditorApp.h"
#include "GB/Log.h"
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		std::string Quoted(const std::string& s) { return s.find(' ') == std::string::npos ? s : "\"" + s + "\""; }

		std::string Join(const std::vector<std::string>& v, const char* sep = ", ")
		{
			std::string s;
			for (const std::string& x : v) s += (s.empty() ? "" : sep) + x;
			return s;
		}

		std::string Number(float v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.4g", static_cast<double>(v));
			return b;
		}
	}

	void EditorApp::SyncGameplay()
	{
		m_Gameplay.Load(m_SceneEditor.World());
		m_Garden.Bind(m_Gameplay, &m_SceneEditor.World());
		m_Garden.SyncVisuals();
		BindResources();
		m_Actors.PrepareAbilities();   // the new systems get the player's character at once (a script can activate in this same frame)
	}

	void EditorApp::BindResources()
	{
		m_Resources.Bind(m_Gameplay, m_NodeDefs, [this](const std::string& who, const std::string& item, int count)
		{
			if (Actor* actor = m_Actors.Find(who)) actor->Bag().Add(m_Items, item, count);
		});
		m_Resources.SyncVisuals(m_Gameplay, m_SceneEditor.World());
		m_Ecology.Bind(m_SceneEditor.World(), m_Gameplay, [this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; });
		m_Interactives.Bind(m_Gameplay,
			[this](const std::string& who, const std::string& item, int n) { Actor* a = m_Actors.Find(who); return a ? a->Bag().Remove(item, n) : 0; },
			[this](const std::string& who, const std::string& item, int n) { Actor* a = m_Actors.Find(who); return a ? a->Bag().Add(m_Items, item, n) : 0; });
	}

	void EditorApp::StepResources()
	{
		for (const std::string& name : m_Actors.Names())
			if (AbilitySystemComponent* asc = m_Gameplay.Find(name))
				ResourceSystem::SyncTools(*asc, m_Actors.Find(name)->Bag(), m_Items);
		m_Resources.SyncVisuals(m_Gameplay, m_SceneEditor.World());
	}

	namespace
	{
		// A real actor as a member of the tribe.
		class ActorBody : public ITribeBody
		{
		public:
			explicit ActorBody(Actor* actor) : m_Actor(actor) {}
			bool Busy() const override { return m_Actor->TasksRunning(); }
			bool Failed() const override { return m_Actor->TasksFailed(); }
			glm::vec2 Position() const override { const glm::vec3 p = m_Actor->GetPosition(); return { p.x, p.z }; }
			bool Do(const std::string& tasks, std::string& error) override { return m_Actor->RunTasks(tasks, error); }
			Inventory& Bag() override { return m_Actor->Bag(); }
		private:
			Actor* m_Actor;
		};
	}

	CommandResult EditorApp::TribeCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "build")
		{
			std::string model = "fox";
			std::vector<float> numbers;
			for (size_t i = 1; i < a.size(); ++i)
			{
				if (PlayerMode::IsFbx(a[i]) || a[i] == "fox" || a[i] == "cesium") model = a[i];
				else numbers.push_back(static_cast<float>(std::atof(a[i].c_str())));
			}
			glm::vec3 origin = m_Orbit.Target;
			int workers = 4;
			if (numbers.size() >= 2) { origin.x = numbers[0]; origin.z = numbers[1]; }
			if (numbers.size() >= 3) workers = std::max(2, std::min(6, static_cast<int>(numbers[2])));
			origin.y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(origin.x, origin.z) : 0.0f;

			m_Tribe.Clear();
			m_TribeBodies.clear();
			static const char* const names[] = { "Ana", "Biel", "Cleo", "Dan", "Eli", "Fran" };
			static const char* const roles[] = { "forager", "woodcutter", "miner", "forager", "woodcutter", "forager" };
			for (int i = 0; i < 6; ++i) if (m_Actors.Find(names[i])) m_Actors.Remove(names[i]);

			const CommandResult built = ResourcesCommand({ "build", std::to_string(origin.x), std::to_string(origin.z) });
			if (!built.Ok) return built;
			const glm::vec2 camp(origin.x, origin.z - 8.0f);
			for (int i = 0; i < workers; ++i)
			{
				const CommandResult r = Execute(std::string("actor spawn ") + names[i] + " \"" + model + "\" " + std::to_string(camp.x + (i - (workers - 1) * 0.5f) * 1.5f) + " " + std::to_string(camp.y) + " ai");
				if (!r.Ok) return r;
				m_TribeBodies.push_back(std::make_unique<ActorBody>(m_Actors.Find(names[i])));
			}
			m_SeasonVisuals = true;
			m_Tribe.Bind(m_SceneEditor.World(), m_Gameplay, m_Items, m_Recipes, m_NodeDefs);
			m_Panels.Tribe = true;
			m_FocusTribe = 20;
			m_Tribe.Config.Camp = camp;
			m_Tribe.Config.CampY = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(camp.x, camp.y) : 0.0f;
			m_Tribe.Store().Add(m_Items, "wood", 4);    // what they found lying around on the first day
			m_Tribe.Store().Add(m_Items, "stone", 5);
			for (int i = 0; i < workers; ++i) m_Tribe.AddMember(names[i], roles[i], m_TribeBodies[static_cast<size_t>(i)].get());
			return { true, std::to_string(workers) + " workers at the camp (" + std::to_string(static_cast<int>(camp.x)) + ", " + std::to_string(static_cast<int>(camp.y)) + ") with 9 resource nodes around; they act on their own: tribe status | tribe log" };
		}
		if (sub == "clear") { m_Tribe.Clear(); m_TribeBodies.clear(); return { true, "tribe cleared (the actors stay: actor remove <name>)" }; }
		if (m_Tribe.Members().empty()) return { false, "no tribe (tribe build [model.fbx])" };
		if (sub == "log")
		{
			const int n = a.size() > 1 ? std::max(1, std::atoi(a[1].c_str())) : 12;
			const auto& log = m_Tribe.Log();
			std::string s;
			for (size_t i = log.size() > static_cast<size_t>(n) ? log.size() - static_cast<size_t>(n) : 0; i < log.size(); ++i) s += log[i] + " | ";
			return { true, s.empty() ? "nothing has happened yet" : s };
		}
		std::string s = m_Tribe.Report();
		for (char& c : s) if (c == '\n') c = '|';   // one line: the console shows the first line of a message
		return { true, s };
	}

	// The look of the season: the weather it brings, snow on the ground, and the colour of the crowns and bushes of the resource nodes.
	// Only the properties that changed are written, and only when a scene with seasons in it asked for it (`ecology visuals`).
	void EditorApp::ApplySeasonLook(double clock)
	{
		const SeasonClock seasons;
		const Season season = seasons.At(clock);
		const float progress = seasons.Progress(clock);

		const std::string weather = SeasonClock::Weather(season, progress);
		if (weather != m_AutoWeather) { m_AutoWeather = weather; m_Props.Set("weather.preset", weather); }

		const float snow = SeasonClock::SnowCover(season, progress);
		if (std::fabs(snow - m_AutoSnow) > 0.01f) { m_AutoSnow = snow; m_Props.Set("ground.snow", std::to_string(snow)); }

		const glm::vec3 leaf = SeasonClock::Foliage(season, progress);
		if (glm::distance(leaf, m_AutoLeaf) > 0.01f)
		{
			m_AutoLeaf = leaf;
			EntityWorld& w = m_SceneEditor.World();
			for (const std::string& n : m_Gameplay.Names())
			{
				const AbilitySystemComponent* a = m_Gameplay.Find(n);
				if (!a || !a->HasTag("Resource.Node")) continue;
				for (const char* part : { " Crown", " Bush" })
					if (const EntityId id = w.FindByName(n + part); id != NullEntity)
						if (Entity* e = w.Find(id); e && e->Mesh) e->Mesh->Albedo = leaf;
			}
		}
	}

	void EditorApp::StepWorld(double clock)
	{
		const Season season = SeasonClock().At(clock);
		if (m_SeasonVisuals && m_SimTick % 30 == 0) ApplySeasonLook(clock);
		m_Garden.GrowthRate = SeasonClock::GrowthRate(season);
		m_Ecology.Update(clock, static_cast<float>(1.0 / 60.0));
		m_Resources.Update(m_Gameplay, static_cast<float>(1.0 / 60.0), SeasonClock::GrowthRate(season));   // plants and trees regrow with the season
		// rain waters the plots (every five seconds of simulation, so the effect does not depend on the frame rate)
		const std::string weather = m_Props.Get("weather.preset");
		if ((weather == "rain" || weather == "lightrain" || weather == "storm") && m_SimTick % 300 == 0)
			for (const std::string& n : m_Gameplay.Names())
				if (AbilitySystemComponent* plot = m_Gameplay.Find(n); plot && plot->HasTag("Garden.Plot")) plot->ApplyEffect("Soak");
	}

	CommandResult EditorApp::EcologyCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "spawn")
		{
			glm::vec3 at = m_Orbit.Target;
			int count = a.size() > 1 ? std::max(1, std::atoi(a[1].c_str())) : 4;
			if (a.size() > 3) { at.x = static_cast<float>(std::atof(a[2].c_str())); at.z = static_cast<float>(std::atof(a[3].c_str())); }
			m_SeasonVisuals = true;   // a world with animals has seasons you can see
			const int made = m_Ecology.Spawn(at, count);
			m_SceneEditor.Commit();
			return { made > 0, std::to_string(made) + " rabbits at (" + std::to_string(static_cast<int>(at.x)) + ", " + std::to_string(static_cast<int>(at.z)) + "); " + m_Ecology.Describe() };
		}
		if (sub == "visuals")
		{
			if (a.size() > 1) { m_SeasonVisuals = a[1] != "off" && a[1] != "0"; m_AutoWeather.clear(); m_AutoSnow = -1.0f; m_AutoLeaf = glm::vec3(-1.0f); }
			return { true, std::string("seasonal visuals ") + (m_SeasonVisuals ? "on: weather, snow and foliage follow the season" : "off") };
		}
		if (sub == "season")
		{
			m_SeasonVisuals = true;
			if (a.size() > 1)
			{
				int index = -1;
				for (int i = 0; i < 4; ++i) if (a[1] == SeasonClock::Name(static_cast<Season>(i))) index = i;
				if (index < 0) return { false, "usage: ecology season spring|summer|autumn|winter" };
				m_SimTick = static_cast<uint64_t>(static_cast<double>(index) * static_cast<double>(SeasonClock().SeasonSeconds) * 60.0);
			}
			const double clock = static_cast<double>(m_SimTick) / 60.0;
			const Season now = SeasonClock().At(clock);
			char rate[16];
			std::snprintf(rate, sizeof rate, "%.1f", static_cast<double>(SeasonClock::GrowthRate(now)));
			return { true, std::string(SeasonClock::Name(now)) + " (" + std::to_string(static_cast<int>(SeasonClock().Progress(clock) * 100.0f)) + "% through it, plants grow x" + rate + ")" };
		}
		const double clock = static_cast<double>(m_SimTick) / 60.0;
		return { true, std::string(SeasonClock::Name(SeasonClock().At(clock))) + "; " + m_Ecology.Describe() };
	}

	void EditorApp::DrawWorldStatePanel()
	{
		if (ImGui::Begin("World state", &m_Panels.WorldState))
		{
			const double clock = static_cast<double>(m_SimTick) / 60.0;
			const Season season = SeasonClock().At(clock);
			ImGui::Text("Season: %s", SeasonClock::Name(season));
			ImGui::ProgressBar(SeasonClock().Progress(clock), ImVec2(-1.0f, 0.0f));
			ImGui::Text("Plants grow x%.1f   weather: %s   time %.0f s", static_cast<double>(SeasonClock::GrowthRate(season)), m_Props.Get("weather.preset").c_str(), clock);
			if (ImGui::CollapsingHeader("Animals", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::TextWrapped("%s", m_Ecology.Describe().c_str());
				if (ImGui::SmallButton("Add 4 rabbits")) m_UiCommands.push_back("ecology spawn 4");
				for (const std::string& n : m_Gameplay.Names())
				{
					const AbilitySystemComponent* a = m_Gameplay.Find(n);
					if (!a || !a->HasTag("Animal.Rabbit")) continue;
					const float food = a->Attributes().Current("Food");
					char label[96];
					std::snprintf(label, sizeof label, "%s  food %.0f  age %.0f s%s", n.c_str(), static_cast<double>(food), static_cast<double>(a->Attributes().Current("Age")), food < 55.0f ? "  (hungry)" : "");
					ImGui::ProgressBar(food / 100.0f, ImVec2(-1.0f, 0.0f), label);
				}
			}
			if (ImGui::CollapsingHeader("Interactives", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool any = false;
				for (const std::string& n : m_Gameplay.Names())
				{
					const AbilitySystemComponent* a = m_Gameplay.Find(n);
					if (!a) continue;
					if (a->HasTag("Interactive.Campfire")) { any = true; ImGui::Text("%s: %s, fuel %.0f", n.c_str(), InteractiveSystem::CampfireState(*a).c_str(), static_cast<double>(a->Attributes().Current("Fuel"))); }
					else if (a->HasTag("Interactive.Well")) { any = true; ImGui::Text("%s: water %.0f / 6", n.c_str(), static_cast<double>(a->Attributes().Current("Water"))); }
					else if (a->HasTag("Interactive.Door")) { any = true; ImGui::Text("%s: %s", n.c_str(), InteractiveSystem::DoorState(*a).c_str()); }
				}
				if (!any) ImGui::TextDisabled("none (interactives build)");
			}
			if (ImGui::CollapsingHeader("Resource nodes", ImGuiTreeNodeFlags_DefaultOpen))
			{
				std::map<std::string, std::pair<int, int>> kinds;   // kind -> (units, nodes)
				for (const std::string& n : m_Gameplay.Names())
				{
					const AbilitySystemComponent* a = m_Gameplay.Find(n);
					if (!a || !a->HasTag("Resource.Node")) continue;
					const std::string kind = n.substr(0, n.rfind(' '));
					kinds[kind].first += ResourceSystem::Amount(*a);
					++kinds[kind].second;
				}
				for (const auto& [kind, v] : kinds) ImGui::Text("%s: %d units in %d nodes", kind.c_str(), v.first, v.second);
				if (kinds.empty()) ImGui::TextDisabled("none (resources build)");
			}
			if (ImGui::CollapsingHeader("Garden", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool any = false;
				for (const std::string& n : m_Gameplay.Names())
					if (const AbilitySystemComponent* a = m_Gameplay.Find(n); a && a->HasTag("Garden.Plot")) { any = true; ImGui::Text("%s: %s", n.c_str(), GardenSystem::SoilState(*a).c_str()); }
				if (!any) ImGui::TextDisabled("none (garden build)");
			}
		}
		ImGui::End();
	}

	CommandResult EditorApp::InteractivesCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "build")
		{
			glm::vec3 origin = m_Orbit.Target;
			if (a.size() > 2) { origin.x = static_cast<float>(std::atof(a[1].c_str())); origin.z = static_cast<float>(std::atof(a[2].c_str())); }
			origin.y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(origin.x, origin.z) : 0.0f;
			BuildInteractives(m_SceneEditor.World(), origin);
			m_SceneEditor.Select(NullEntity);
			SyncGameplay();
			m_SceneEditor.Commit();
			return { true, "a campfire (\"Fire Pit\"), a well and a gate built; try: actor bag <name> add wood 2, ability activate Stoke \"Fire Pit\" <name>, ability activate DrawWater Well <name>, ability activate OpenDoor Gate <name>" };
		}
		std::string s;
		for (const std::string& n : m_Gameplay.Names())
		{
			const AbilitySystemComponent* x = m_Gameplay.Find(n);
			if (!x) continue;
			if (x->HasTag("Interactive.Campfire")) s += n + ": " + InteractiveSystem::CampfireState(*x) + ", fuel " + std::to_string(static_cast<int>(x->Attributes().Current("Fuel"))) + "; ";
			else if (x->HasTag("Interactive.Well")) s += n + ": water " + std::to_string(static_cast<int>(x->Attributes().Current("Water"))) + "; ";
			else if (x->HasTag("Interactive.Door")) s += n + ": " + InteractiveSystem::DoorState(*x) + "; ";
		}
		return s.empty() ? CommandResult{ false, "no interactives in the scene (interactives build)" } : CommandResult{ true, s };
	}

	CommandResult EditorApp::ResourcesCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "build")
		{
			glm::vec3 origin = m_Orbit.Target;
			if (a.size() > 2) { origin.x = static_cast<float>(std::atof(a[1].c_str())); origin.z = static_cast<float>(std::atof(a[2].c_str())); }
			origin.y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(origin.x, origin.z) : 0.0f;
			const ResourceLayout layout = BuildResources(m_SceneEditor.World(), origin, m_NodeDefs);
			m_SceneEditor.Select(NullEntity);
			SyncGameplay();
			m_SceneEditor.Commit();
			return { true, std::to_string(layout.Nodes.size()) + " resource nodes built at (" + std::to_string(static_cast<int>(origin.x)) + ", " + std::to_string(static_cast<int>(origin.z))
				+ "). Gather with: ability activate Gather \"berry bush 1\" (Chop needs an axe, Mine a pickaxe: actor bag <name> craft stone_axe workbench)" };
		}
		std::string s;
		for (const std::string& n : m_Gameplay.Names())
			if (const AbilitySystemComponent* node = m_Gameplay.Find(n); node && node->HasTag("Resource.Node"))
				s += n + ": " + std::to_string(ResourceSystem::Amount(*node)) + (node->HasTag("State.Depleted") ? " (depleted)" : "") + "; ";
		if (s.empty()) return { false, "no resource nodes in the scene (resources build)" };
		return { true, s };
	}

	CommandResult EditorApp::GardenCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "build")
		{
			glm::vec3 origin = m_Orbit.Target;
			if (a.size() > 2) { origin.x = static_cast<float>(std::atof(a[1].c_str())); origin.z = static_cast<float>(std::atof(a[2].c_str())); }
			origin.y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(origin.x, origin.z) : 0.0f;
			const GardenLayout layout = BuildGarden(m_SceneEditor.World(), origin);
			m_SceneEditor.Select(NullEntity);
			SyncGameplay();
			m_SceneEditor.Commit();
			std::ostringstream message;   // (a fixed buffer with names of any length: gcc rejects it as a possible truncation)
			message << std::fixed << std::setprecision(1) << "garden built at (" << origin.x << ", " << origin.y << ", " << origin.z << "): " << layout.Plots.size()
				<< " plots, a tree spot, a fruit tree, a cow, " << layout.Boxes.size() << " boxes, a wheelbarrow, the " << layout.Player << " and the " << layout.Npc
				<< ". Start the player (player <character.fbx>) after it to walk around the props, then: ability activate Dig \"Plot 1\"";
			return { true, message.str() };
		}
		if (sub == "status")
		{
			std::string s;
			for (const std::string& n : m_Gameplay.Names())
			{
				const AbilitySystemComponent* e = m_Gameplay.Find(n);
				std::string st = GardenSystem::SoilState(*e);
				if (st.empty()) st = GardenSystem::TreeState(*e);
				if (st.empty()) continue;
				s += n + ": " + st + (e->HasTag("State.Wet") ? " (wet)" : "") + (e->Attributes().Has("Fruit") ? ", fruit " + Number(e->Attribute("Fruit")) : "") + "\n";
			}
			if (s.empty()) return { false, "no garden in the scene (garden build)" };
			return { true, s + m_Gameplay.Describe(m_AbilityActor) };
		}
		return { false, "usage: garden build [x z] | garden status" };
	}

	CommandResult EditorApp::AbilityCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "list" : a[0];
		auto find = [this](const std::string& name, std::string& error) -> AbilitySystemComponent*
		{
			AbilitySystemComponent* e = m_Gameplay.Find(name);
			if (!e) error = "no ability system called '" + name + "' (garden build, or give an entity with a gameplay component). Have: " + Join(m_Gameplay.Names());
			return e;
		};
		std::string error;
		if (sub == "sync")
		{
			SyncGameplay();
			return { true, std::to_string(m_Gameplay.Count()) + " ability systems from the scene" };
		}
		if (sub == "store")
		{
			const int n = m_Gameplay.Store(m_SceneEditor.World());
			m_SceneEditor.Commit();
			return { true, "state of " + std::to_string(n) + " ability systems written into the scene's entities" };
		}
		if (sub == "list" || sub == "status")
		{
			AbilitySystemComponent* e = find(a.size() > 1 ? a[1] : m_AbilityActor, error);
			if (!e) return { false, error };
			std::string s;
			if (sub == "list")
			{
				for (const std::string& n : e->Granted())
				{
					const AbilityDef* d = m_Gameplay.Library.Ability(n);
					std::string state = e->IsActive(n) ? "running" : e->CooldownRemaining(n) > 0.0f ? "cooldown " + Number(e->CooldownRemaining(n)) + " s" : "ready";
					s += n + " [" + state + "]" + (d && !d->Note.empty() ? " - " + d->Note : "") + "\n";
				}
				return { true, s.empty() ? e->Name() + " has no abilities" : e->Name() + ":\n" + s };
			}
			s = m_Gameplay.Describe(e->Name()) + "\ntags: " + Join(e->Tags().Names()) + "\neffects: ";
			for (const ActiveEffect& fx : e->ActiveEffects()) s += fx.Def.Name + (fx.Def.Policy == EffectPolicy::Duration ? "(" + Number(fx.Remaining) + " s) " : " ");
			s += "\nrunning: " + Join(e->ActiveAbilities());
			if (!e->LastError().empty()) s += "\nlast failure: " + e->LastError();
			return { true, s };
		}
		if (sub == "grant")
		{
			if (a.size() < 2) return { false, "usage: ability grant <ability> [entity]" };
			AbilitySystemComponent* e = find(a.size() > 2 ? a[2] : m_AbilityActor, error);
			if (!e) return { false, error };
			return e->Grant(a[1]) ? CommandResult{ true, e->Name() + " can now " + a[1] } : CommandResult{ false, "unknown ability '" + a[1] + "'" };
		}
		if (sub == "apply")
		{
			if (a.size() < 2) return { false, "usage: ability apply <effect> [entity]" };
			AbilitySystemComponent* e = find(a.size() > 2 ? a[2] : m_AbilityActor, error);
			if (!e) return { false, error };
			return e->ApplyEffect(a[1]) ? CommandResult{ true, "applied " + a[1] + " to " + e->Name() } : CommandResult{ false, "unknown effect '" + a[1] + "'" };
		}
		if (sub == "activate")
		{
			if (a.size() < 2) return { false, "usage: ability activate <ability> [target entity] [actor]" };
			const std::string target = a.size() > 2 ? a[2] : "";
			const std::string actor = a.size() > 3 ? a[3] : m_AbilityActor;
			AbilitySystemComponent* e = find(actor, error);
			if (!e) return { false, error };
			if (!target.empty() && !m_Gameplay.Find(target)) return { false, "no target called '" + target + "'" };
			std::string why;
			const bool ok = m_Actors.Find(actor) ? m_Actors.Find(actor)->UseAbility(a[1], target, why) : e->Activate(a[1], target.empty() ? nullptr : m_Gameplay.Find(target), &why, target);
			return ok ? CommandResult{ true, actor + ": " + a[1] + (target.empty() ? "" : " on " + target) + (e->Runtime() ? "" : " (no character: runs on timers)") } : CommandResult{ false, actor + " cannot " + a[1] + ": " + why };
		}
		if (sub == "cancel")
		{
			const std::string which = a.size() > 1 ? a[1] : "all";
			AbilitySystemComponent* e = find(a.size() > 2 ? a[2] : m_AbilityActor, error);
			if (!e) return { false, error };
			if (which == "all") { e->CancelAll(); return { true, "cancelled everything " + e->Name() + " was doing" }; }
			return e->Cancel(which) ? CommandResult{ true, "cancelled " + which } : CommandResult{ false, which + " is not running" };
		}
		return { false, "usage: ability list|status [entity] | grant <ability> [entity] | activate <ability> [target] [actor] | cancel [ability] [actor] | apply <effect> [entity] | sync | store" };
	}


	std::string EditorApp::PickActor(const Ray& ray) const
	{
		std::string best;
		float bestT = 1e30f;
		for (const std::string& name : m_Actors.Names())
		{
			const Actor* actor = m_Actors.Find(name);
			if (!actor) continue;
			// the body as a vertical segment (feet to head): the closest point of the ray to it
			const glm::vec3 a = actor->GetPosition(), b = a + glm::vec3(0.0f, 1.7f, 0.0f);
			const glm::vec3 d = glm::normalize(ray.Dir), e = b - a, w = ray.Origin - a;
			const float dd = glm::dot(d, d), de = glm::dot(d, e), ee = glm::dot(e, e), dw = glm::dot(d, w), ew = glm::dot(e, w);
			const float denom = dd * ee - de * de;
			float s = denom > 1e-6f ? glm::clamp((de * ew - ee * dw) / denom, 0.0f, 1e6f) : 0.0f;   // along the ray
			float t = glm::clamp((ew + s * de) / ee, 0.0f, 1.0f);                                   // along the segment
			s = glm::max(0.0f, glm::dot(a + e * t - ray.Origin, d));
			const float distance = glm::length(ray.Origin + d * s - (a + e * t));
			if (distance < glm::max(actor->Radius(), 0.35f) + 0.2f && s < bestT) { bestT = s; best = name; }
		}
		return best;
	}

	void EditorApp::SelectActor(const std::string& name, bool focus)
	{
		m_AbilityActor = name;
		if (const EntityId id = m_SceneEditor.World().FindByName(name); id != NullEntity) m_SceneEditor.Select(id);
		if (focus)
			if (const Actor* actor = m_Actors.Find(name)) m_Orbit.Target = actor->GetPosition();
	}

	void EditorApp::DrawBag(const std::string& actorName)
	{
		Actor* actor = m_Actors.Find(actorName);
		if (!actor) { ImGui::TextDisabled("(not a character: no inventory)"); return; }
		const Inventory& bag = actor->Bag();
		ImGui::Text("%d / %d slots, %.1f kg%s", bag.UsedSlots(), bag.Slots(), static_cast<double>(bag.Weight(m_Items)), bag.MaxWeight() > 0.0f ? "" : " (no weight limit)");
		if (bag.Stacks().empty()) { ImGui::TextDisabled("empty"); return; }
		if (ImGui::BeginTable("bag", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Item"); ImGui::TableSetupColumn("Count"); ImGui::TableSetupColumn("Kind");
			ImGui::TableHeadersRow();
			for (const ItemSlot& s : bag.Stacks())
			{
				const ItemDef* def = m_Items.Find(s.Id);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(def ? def->Name.c_str() : s.Id.c_str());
				ImGui::TableNextColumn(); ImGui::Text("%d", s.Count);
				ImGui::TableNextColumn(); ImGui::TextDisabled("%s", def ? def->Category.c_str() : "?");
			}
			ImGui::EndTable();
		}
	}

	void EditorApp::DrawTribePanel()
	{
		if (m_FocusTribe > 0) { ImGui::SetNextWindowFocus(); --m_FocusTribe; }
		if (ImGui::Begin("Tribe", &m_Panels.Tribe))
		{
			if (m_Tribe.Members().empty())
			{
				ImGui::TextWrapped("No tribe. Build one with: tribe build \"<model.fbx>\" 0 0 4");
				ImGui::End();
				return;
			}
			ImGui::Text("Time %.0f s   %s", m_Tribe.Time(), !m_Tribe.HasFire() ? "no campfire yet" : m_Tribe.FireBurning() ? "campfire lit" : "campfire out (no wood)");
			ImGui::SameLine();
			ImGui::TextDisabled("  speed x%d", m_TimeScale);

			if (ImGui::CollapsingHeader("Store", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const Inventory& store = m_Tribe.Store();
				for (const std::string& id : m_Items.Ids())
				{
					const int n = store.Count(id);
					if (n == 0) continue;
					const ItemDef* def = m_Items.Find(id);
					char label[64];
					std::snprintf(label, sizeof label, "%s %d / %d", def ? def->Name.c_str() : id.c_str(), n, m_Tribe.Config.StockCap);
					ImGui::ProgressBar(std::min(1.0f, static_cast<float>(n) / static_cast<float>(m_Tribe.Config.StockCap)), ImVec2(-1.0f, 0.0f), label);
				}
				if (store.Stacks().empty()) ImGui::TextDisabled("empty");
			}

			if (ImGui::CollapsingHeader("Members", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (ImGui::BeginTable("members", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Role"); ImGui::TableSetupColumn("Doing"); ImGui::TableSetupColumn("Carries");
					ImGui::TableHeadersRow();
					for (const TribeMember& m : m_Tribe.Members())
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						if (ImGui::Selectable(m.Name.c_str(), m.Name == m_AbilityActor, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
							SelectActor(m.Name, ImGui::IsMouseDoubleClicked(0));
						ImGui::TableNextColumn(); ImGui::TextUnformatted(m.Role.c_str());
						ImGui::TableNextColumn();
						if (m.Job.empty()) ImGui::TextDisabled("deciding");
						else if (m.Job == "rest") ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "resting");
						else ImGui::Text("%s %s", m.Job.c_str(), m.Detail.c_str());
						if (m.Hungry) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "(hungry)"); }
						ImGui::TableNextColumn();
						std::string carried;
						for (const ItemSlot& s : m.Body->Bag().Stacks()) carried += (carried.empty() ? "" : ", ") + s.Id + " x" + std::to_string(s.Count);
						ImGui::TextUnformatted(carried.empty() ? "-" : carried.c_str());
					}
					ImGui::EndTable();
				}
				ImGui::TextDisabled("Click a name to select it (double click: go to it). You can also click a character in the viewport.");
			}

			for (const TribeMember& m : m_Tribe.Members())
				if (m.Name == m_AbilityActor && ImGui::CollapsingHeader(("Selected: " + m.Name).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Text("%s, %d carried to the store, %d made, %d meals%s", m.Role.c_str(), m.Deposited, m.Made, m.Meals, m.Hungry ? ", hungry" : "");
					if (!m.Why.empty()) ImGui::TextWrapped("Resting because: %s", m.Why.c_str());
					if (Actor* actor = m_Actors.Find(m.Name)) ImGui::TextWrapped("Orders: %s", actor->TaskStatus().c_str());
					DrawBag(m.Name);
				}

			if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const auto& log = m_Tribe.Log();
				ImGui::BeginChild("tribelog", ImVec2(0.0f, 160.0f), true);
				for (size_t i = log.size() > 80 ? log.size() - 80 : 0; i < log.size(); ++i) ImGui::TextUnformatted(log[i].c_str());
				if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
				ImGui::EndChild();
			}
		}
		ImGui::End();
	}

	void EditorApp::DrawAbilitiesPanel()
	{
		if (ImGui::Begin("Abilities", &m_Panels.Abilities))
		{
			const std::vector<std::string> names = m_Gameplay.Names();
			if (names.empty())
			{
				ImGui::TextWrapped("No ability systems in the scene. Build the vegetable garden with: garden build");
				if (ImGui::Button("Build the garden")) m_UiCommands.push_back("garden build");
				ImGui::End();
				return;
			}
			if (ImGui::BeginCombo("Entity", m_AbilityActor.c_str()))
			{
				for (const std::string& n : names)
					if (ImGui::Selectable(n.c_str(), n == m_AbilityActor)) m_AbilityActor = n;
				ImGui::EndCombo();
			}
			AbilitySystemComponent* asc = m_Gameplay.Find(m_AbilityActor);
			if (!asc) { ImGui::TextUnformatted("(gone)"); ImGui::End(); return; }
			ImGui::TextDisabled("%s", asc->Runtime() ? "driving a character" : "no character: abilities run on timers");
			if (Actor* actor = m_Actors.Find(m_AbilityActor))
			{
				std::string role;
				for (const TribeMember& m : m_Tribe.Members()) if (m.Name == m_AbilityActor) role = "  [" + m.Role + ": " + (m.Job.empty() ? "deciding" : m.Job + " " + m.Detail) + "]";
				ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "Character: %s%s", m_AbilityActor.c_str(), role.c_str());
				if (ImGui::CollapsingHeader("Inventory", ImGuiTreeNodeFlags_DefaultOpen)) DrawBag(m_AbilityActor);
				(void)actor;
			}

			static char target[96] = "Plot 1";
			ImGui::InputText("Target", target, sizeof target);
			if (ImGui::CollapsingHeader("Abilities", ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const std::string& n : asc->Granted())
				{
					const AbilityDef* d = m_Gameplay.Library.Ability(n);
					const bool running = asc->IsActive(n);
					const float cooldown = asc->CooldownRemaining(n);
					std::string why;
					const bool can = asc->CanActivate(n, d && d->NeedsTarget ? m_Gameplay.Find(target) : nullptr, &why);
					ImGui::PushID(n.c_str());
					if (running)
					{
						if (ImGui::SmallButton("Cancel")) m_UiCommands.push_back("ability cancel " + n + " " + Quoted(asc->Name()));
					}
					else
					{
						ImGui::BeginDisabled(!can);
						if (ImGui::SmallButton("Use")) m_UiCommands.push_back("ability activate " + n + " " + (d && d->NeedsTarget ? Quoted(target) : std::string("\"\"")) + " " + Quoted(asc->Name()));
						ImGui::EndDisabled();
					}
					ImGui::SameLine();
					ImGui::Text("%s", n.c_str());
					ImGui::SameLine();
					if (running) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "running");
					else if (cooldown > 0.0f) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "cooldown %.1f s", static_cast<double>(cooldown));
					else if (can) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "ready");
					else ImGui::TextDisabled("%s", why.c_str());
					if (d && !d->Note.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d->Note.c_str());
					ImGui::PopID();
				}
			}
			if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_DefaultOpen))
				for (const AttributeSet::Attribute& at : asc->Attributes().All())
				{
					const float span = at.Max - at.Min;
					char label[64];
					std::snprintf(label, sizeof label, "%s %.4g / %.4g", at.Name.c_str(), static_cast<double>(at.Current), static_cast<double>(at.Max));
					ImGui::ProgressBar(span > 0.0f && span < 1e6f ? (at.Current - at.Min) / span : 0.0f, ImVec2(-1.0f, 0.0f), label);
				}
			if (ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen))
				for (const auto& [tag, count] : asc->Tags().All())
					ImGui::BulletText("%s%s", tag.c_str(), count > 1 ? (" x" + std::to_string(count)).c_str() : "");
			if (ImGui::CollapsingHeader("Effects and cooldowns", ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const ActiveEffect& fx : asc->ActiveEffects())
				{
					if (fx.Def.Policy == EffectPolicy::Duration) ImGui::BulletText("%s: %.1f s left", fx.Def.Name.c_str(), static_cast<double>(fx.Remaining));
					else ImGui::BulletText("%s%s", fx.Def.Name.c_str(), fx.Def.Period > 0.0f ? (" (every " + Number(fx.Def.Period) + " s)").c_str() : "");
				}
				if (asc->ActiveEffects().empty()) ImGui::TextDisabled("none");
			}
			if (!asc->LastError().empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "last failure: %s", asc->LastError().c_str());
			if (ImGui::CollapsingHeader("Garden"))
			{
				for (const std::string& n : names)
				{
					const AbilitySystemComponent* e = m_Gameplay.Find(n);
					std::string st = GardenSystem::SoilState(*e);
					if (st.empty()) st = GardenSystem::TreeState(*e);
					if (st.empty() && !e->HasTag("State.Cow")) continue;
					ImGui::Text("%s: %s%s", n.c_str(), st.empty() ? "cow" : st.c_str(), e->HasTag("State.Wet") ? " (wet)" : "");
				}
			}
		}
		ImGui::End();
	}
}
