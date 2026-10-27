// The editor's side of the Gameplay Ability System: the `garden` and `ability` commands and the Abilities panel.
#include "EditorApp.h"
#include "GB/Log.h"
#include <algorithm>
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

	CommandResult EditorApp::NavCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		NavGrid& nav = m_Actors.Navigation();
		if (sub == "debug")
		{
			if (a.size() < 2 || (a[1] != "on" && a[1] != "off")) return { false, "nav debug on|off" };
			if (a[1] == "on" && !nav.Ready())
			{
				std::string error;
				if (!m_Player.SyncNavigation(m_World, m_SceneEditor, m_Steps.get(), &error)) return { false, error };
			}
			m_NavDebug = a[1] == "on";
			nav.SetRecording(m_NavDebug);
			return { true, m_NavDebug ? "navigation debug on: red = wall, yellow = costly, blue = water, green lines = the paths found (the last 64)" : "navigation debug off" };
		}
		if (sub == "rebuild" || sub == "sync")
		{
			std::string error;
			if (sub == "rebuild") nav.Clear();
			if (!m_Player.SyncNavigation(m_World, m_SceneEditor, m_Steps.get(), &error)) return { false, error };
			const NavStats& s = nav.Stats();
			char text[200];
			std::snprintf(text, sizeof text, "navigation %s: %dx%d cells of %.2f m, %zu obstacles; revision %u, %ld builds (%.0f ms), %ld updates (%ld cells)", sub == "rebuild" ? "rebuilt" : "synced",
				nav.Width(), nav.Height(), static_cast<double>(nav.CellSize()), nav.Obstacles(), nav.Revision(), s.Builds, s.BuildSeconds * 1000.0, s.Updates, s.CellsUpdated);
			return { true, text };
		}
		if (!nav.Ready()) return { false, "no navigation yet: spawn an actor or `nav debug on` once the world is generated" };
		if (sub == "path")
		{
			if (a.size() < 5) return { false, "nav path <x1 z1 x2 z2> [load 0-1]" };
			NavProfile profile = NavProfile::Carrying(a.size() > 5 ? static_cast<float>(std::atof(a[5].c_str())) : 0.0f);
			const glm::vec2 from(static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())));
			const glm::vec2 to(static_cast<float>(std::atof(a[3].c_str())), static_cast<float>(std::atof(a[4].c_str())));
			NavPath path;
			const bool ok = nav.FindPath(profile, from, to, path);
			float length = 0.0f;
			glm::vec2 p = from;
			for (const glm::vec2& q : path.Points) { length += glm::distance(p, q); p = q; }
			char text[200];
			std::snprintf(text, sizeof text, "%s: %zu waypoints, %.1f m (straight %.1f m), cost %.1f, %d cells opened", ok ? "path found" : (path.Partial ? "goal unreachable; nearest" : "no path"), path.Points.size(),
				static_cast<double>(length), static_cast<double>(glm::distance(from, to)), static_cast<double>(path.Cost), path.Expanded);
			return { ok || path.Partial, text };
		}
		long blocked, costly, wet;
		nav.Count(blocked, costly, wet);
		const NavStats& s = nav.Stats();
		char text[360];
		std::snprintf(text, sizeof text, "navigation: %dx%d cells of %.2f m (%ld blocked, %ld costly, %ld wet), %zu obstacles, revision %u | %ld paths (%ld failed, %ld replans), %.2f ms each, %ld cells opened | %ld updates, %ld cells redone | debug %s",
			nav.Width(), nav.Height(), static_cast<double>(nav.CellSize()), blocked, costly, wet, nav.Obstacles(), nav.Revision(), s.Paths, s.Failed, s.Replans,
			s.Paths ? s.PathSeconds / static_cast<double>(s.Paths) * 1000.0 : 0.0, s.Expanded, s.Updates, s.CellsUpdated, m_NavDebug ? "on" : "off");
		return { true, text };
	}

	void EditorApp::DrawNavDebug(float ox, float oy, float w, float h)
	{
		const NavGrid& nav = m_Actors.Navigation();
		if (!nav.Ready()) return;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const glm::vec2 size(w, h);
		const glm::mat4 viewProj = m_Camera.GetProjection(w / h) * m_Camera.GetView();
		const glm::vec2 clipMin(ox, oy), clipMax(ox + w, oy + h);
		dl->PushClipRect(ImVec2(clipMin.x, clipMin.y), ImVec2(clipMax.x, clipMax.y), true);
		auto lift = [&](const glm::vec2& p) { return glm::vec3(p.x, (m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(p.x, p.y) : 0.0f) + 0.12f, p.y); };
		const NavProfile profile;
		// cells in a window around the focus (the player, or what the orbit camera looks at), one in two so that the cost stays small
		const glm::vec3 focus = m_Player.IsActive() ? m_Player.GetPosition() : m_Orbit.Target;
		const float cs = nav.CellSize();
		const int radius = static_cast<int>(36.0f / cs);
		int ci, cj;
		if (nav.CellAt({ focus.x, focus.z }, ci, cj))
			for (int j = std::max(0, cj - radius); j < std::min(nav.Height(), cj + radius); j += 2)
				for (int i = std::max(0, ci - radius); i < std::min(nav.Width(), ci + radius); i += 2)
				{
					const float cost = nav.CellCost(i, j, profile);
					const bool wet = nav.Depth(i, j) > 0.05f;
					if (cost >= 0.0f && cost < 1.5f && !wet) continue;
					const ImU32 color = cost < 0.0f ? IM_COL32(230, 60, 50, 110) : (wet ? IM_COL32(60, 120, 240, 110) : IM_COL32(240, 200, 50, 90));
					const glm::vec2 c = nav.CellCenter(i, j);
					glm::vec2 q[4];
					const glm::vec2 corner[4] = { { -cs, -cs }, { cs, -cs }, { cs, cs }, { -cs, cs } };
					bool ok = true;
					for (int k = 0; k < 4 && ok; ++k) ok = ProjectToScreen(viewProj, lift(c + corner[k]), size, q[k]);
					if (ok) dl->AddQuadFilled(ImVec2(ox + q[0].x, oy + q[0].y), ImVec2(ox + q[1].x, oy + q[1].y), ImVec2(ox + q[2].x, oy + q[2].y), ImVec2(ox + q[3].x, oy + q[3].y), color);
				}
		for (const std::vector<glm::vec2>& trace : nav.Traces())
			for (size_t k = 1; k < trace.size(); ++k)
			{
				glm::vec2 a, b;
				if (ProjectToScreen(viewProj, lift(trace[k - 1]), size, a) && ProjectToScreen(viewProj, lift(trace[k]), size, b))
					dl->AddLine(ImVec2(ox + a.x, oy + a.y), ImVec2(ox + b.x, oy + b.y), IM_COL32(90, 235, 110, 230), 2.0f);
			}
		dl->PopClipRect();
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
			{
				// a well near the camp (the members get thirsty); the fire pit and the gate of the interactives are not wanted here
				EntityWorld& world = m_SceneEditor.World();
				const glm::vec3 wellOrigin(camp.x + 4.0f, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(camp.x + 4.0f, camp.y - 6.0f) : 0.0f, camp.y - 6.0f);
				BuildInteractives(world, wellOrigin);
				for (const char* extra : { "Fire Pit", "Gate" }) if (const EntityId id = world.FindByName(extra); id != NullEntity) world.Destroy(id);
				SyncGameplay();
			}
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

	// ------------------------------------------------------------------ the brain of the members
	void EditorApp::RestoreTribe(const std::string& json)
	{
		m_Tribe.Clear();
		m_TribeBodies.clear();
		m_Tribe.Bind(m_SceneEditor.World(), m_Gameplay, m_Items, m_Recipes, m_NodeDefs);
		for (const auto& [name, role] : Tribe::SavedMembers(json))
		{
			Actor* actor = m_Actors.Find(name);
			if (!actor) { GB_CLIENT_WARN("the saved tribe has {} but there is no such actor", name); continue; }
			// A save taken in the middle of an ability keeps the tags that ability held (State.Busy...) but not the ability: without this the member could never act again.
			if (AbilitySystemComponent* asc = m_Gameplay.Find(name))
			{
				asc->Tags().RemoveAll("State.Busy");
				for (const std::string& granted : asc->Granted())
					if (const AbilityDef* def = m_Gameplay.Library.Ability(granted))
					{
						if (!def->Tag.empty()) asc->Tags().RemoveAll(def->Tag);
						for (const std::string& tag : def->ActiveTags) asc->Tags().RemoveAll(tag);
					}
			}
			m_TribeBodies.push_back(std::make_unique<ActorBody>(actor));
			m_Tribe.AddMember(name, role, m_TribeBodies.back().get());
		}
		if (!m_Tribe.FromJson(json, true)) GB_CLIENT_WARN("the saved tribe could not be read: it starts again");
		m_Panels.Tribe = true;
	}

	CommandResult EditorApp::AiCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "debug")
		{
			if (a.size() > 1) m_AiDebug = a[1] == "on" || a[1] == "1" || a[1] == "true";
			else m_AiDebug = !m_AiDebug;
			return { true, std::string("ai debug ") + (m_AiDebug ? "on: view cones, obstacles and the memory of the selected member are drawn" : "off") };
		}
		if (m_Tribe.Members().empty()) return { false, "no tribe (tribe build [model.fbx])" };
		const std::string who = a.size() > 1 ? a[1] : m_AbilityActor;
		const TribeMember* member = nullptr;
		for (const TribeMember& m : m_Tribe.Members()) if (m.Name == who) member = &m;
		if (sub == "status" && a.size() < 2)
		{
			std::string s = "season " + std::string(SeasonClock::Name(m_Tribe.CurrentSeason())) + ", hour " + std::to_string(static_cast<int>(m_Tribe.Hour())) + " | ";
			for (const TribeMember& m : m_Tribe.Members()) s += m.Name + ": " + m_Tribe.Because(m) + " | ";
			return { true, s };
		}
		if (!member) return { false, "no member called '" + who + "' (select one in the Tribe panel or give the name)" };
		SelectActor(member->Name, false);   // the Tribe panel then shows its mind
		if (sub == "scream")
		{
			const Actor* actor = m_Actors.Find(member->Name);
			if (!actor) return { false, "no actor " + who };
			m_Tribe.Shout(member->Name, { actor->GetPosition().x, actor->GetPosition().z });
			return { true, member->Name + " screams: whoever is within 30 m hears it and feels less safe" };
		}
		if (sub == "memory")
		{
			std::string s = member->Name + " knows " + std::to_string(member->Mind.Memory.Size()) + " things: ";
			for (const auto& [name, e] : member->Mind.Memory.All())
			{
				char b[160];
				std::snprintf(b, sizeof b, "%s (%s at %.0f,%.0f, %s, seen %.0f s ago%s) | ", name.c_str(), e.Kind.c_str(), static_cast<double>(e.Pos.x), static_cast<double>(e.Pos.y),
					e.Amount < 0 ? "?" : (std::to_string(e.Amount) + " left").c_str(), m_Tribe.Time() - e.Seen, e.Heard ? ", by ear" : "");
				s += b;
			}
			return { true, s };
		}
		if (sub == "status")
		{
			std::string s = member->Name + " (" + member->Role + "): " + m_Tribe.Because(*member) + " | needs:";
			for (const auto& [need, value] : m_Tribe.NeedLevels(*member)) s += " " + need + " " + std::to_string(static_cast<int>(value));
			s += " | options:";
			for (const UtilityOption& o : member->Mind.Options)
			{
				char b[96];
				std::snprintf(b, sizeof b, " %s%s%s=%.2f", o.Kind.c_str(), o.Target.empty() ? "" : " ", o.Target.c_str(), static_cast<double>(o.Score));
				s += b;
			}
			return { true, s };
		}
		return { false, "ai debug [on|off] | status [name] | memory <name> | scream <name>" };
	}

	namespace
	{
		ImVec4 NeedColor(float level)
		{
			return level < 25.0f ? ImVec4(0.9f, 0.3f, 0.25f, 1.0f) : level < 50.0f ? ImVec4(0.95f, 0.7f, 0.25f, 1.0f) : ImVec4(0.35f, 0.75f, 0.4f, 1.0f);
		}
	}

	// A member's mind: its needs, what it decided and why, the options it weighed with the factors of each score, what it senses and what it remembers.
	void EditorApp::DrawNpcInspector(const TribeMember& m)
	{
		if (m.Dead) ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.25f, 1.0f), "Dead of %s", m.Cause.c_str());
		ImGui::TextWrapped("Decision: %s", m_Tribe.Because(m).c_str());
		const std::map<std::string, float> needs = m_Tribe.NeedLevels(m);
		if (!needs.empty() && ImGui::CollapsingHeader("Needs", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const char* need : { NeedName::Hunger, NeedName::Thirst, NeedName::Rest, NeedName::Warmth, NeedName::Safety, NeedName::Company, NeedName::Health })
			{
				const auto it = needs.find(need);
				if (it == needs.end()) continue;
				char label[48];
				std::snprintf(label, sizeof label, "%s %.0f", need, static_cast<double>(it->second));
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, NeedColor(it->second));
				ImGui::ProgressBar(it->second / 100.0f, ImVec2(-1.0f, 0.0f), label);
				ImGui::PopStyleColor();
			}
			const auto vigor = needs.find(NeedName::Vigor);
			if (vigor != needs.end()) ImGui::TextDisabled("Vigor x%.2f (low needs slow it down)", static_cast<double>(vigor->second));
			ImGui::TextDisabled("%s, %02d:00, %s", SeasonClock::Name(m_Tribe.CurrentSeason()), static_cast<int>(m_Tribe.Hour()), IsNight(m_Tribe.Config.Needs, m_Tribe.Hour()) ? "night" : "day");
		}
		if (ImGui::CollapsingHeader("Why (options and scores)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (m.Mind.Options.empty()) ImGui::TextDisabled("it has not decided yet");
			else
			{
				std::vector<size_t> order(m.Mind.Options.size());
				for (size_t i = 0; i < order.size(); ++i) order[i] = i;
				std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return m.Mind.Options[x].Score > m.Mind.Options[y].Score; });
				for (const size_t i : order)
				{
					const UtilityOption& o = m.Mind.Options[i];
					const bool chosen = static_cast<int>(i) == m.Mind.Chosen;
					ImGui::TextColored(chosen ? ImVec4(0.45f, 0.9f, 0.5f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s%s%s%s  %.2f", chosen ? "> " : "  ", o.Kind.c_str(), o.Target.empty() ? "" : " ", o.Target.c_str(), static_cast<double>(o.Score));
					std::string why = o.Note;
					for (const ScoreFactor& f : o.Factors)
					{
						char b[64];
						std::snprintf(b, sizeof b, "%s%s %.2f", why.empty() ? "" : "; ", f.Name.c_str(), static_cast<double>(f.Value));
						why += b;
					}
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextDisabled("    %s", why.c_str());
					ImGui::PopTextWrapPos();
				}
			}
		}
		if (ImGui::CollapsingHeader("Senses and memory"))
		{
			std::string seen, heard;
			for (const std::string& v : m.Mind.Visible) seen += (seen.empty() ? "" : ", ") + v;
			for (const std::string& v : m.Mind.Heard) heard += (heard.empty() ? "" : ", ") + v;
			ImGui::TextWrapped("Sees: %s", seen.empty() ? "nothing" : seen.c_str());
			ImGui::TextWrapped("Hears: %s", heard.empty() ? "nothing" : heard.c_str());
			ImGui::Text("Knows %zu places (the clan %zu):", m.Mind.Memory.Size(), m_Tribe.ClanMemory().Size());
			ImGui::BeginChild("memory", ImVec2(0.0f, 110.0f), true);
			for (const auto& [name, e] : m.Mind.Memory.All())
				ImGui::Text("%s  %s  (%.0f, %.0f)  %s  seen %.0f s ago%s", name.c_str(), e.Kind.c_str(), static_cast<double>(e.Pos.x), static_cast<double>(e.Pos.y),
					e.Amount < 0 ? "?" : (std::to_string(e.Amount) + " left").c_str(), m_Tribe.Time() - e.Seen, e.Heard ? ", by ear" : "");
			ImGui::EndChild();
			ImGui::Checkbox("Draw view cones and memory in the viewport (ai debug)", &m_AiDebug);
		}
	}

	// The view cone of every member, the obstacles that hide things from them, and the memory of the selected one (a circle for each place it knows,
	// fainter the older the news; a dashed ring for what it only heard).
	void EditorApp::DrawAiDebug(ImDrawList* dl, const glm::mat4& viewProj, const glm::vec2& size, float ox, float oy)
	{
		if (m_Tribe.Members().empty()) return;
		const VisionParams& vision = m_Tribe.Config.Vision;
		auto ground = [&](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) + 0.15f : 0.15f; };
		auto point = [&](const glm::vec2& p, glm::vec2& out) { return ProjectToScreen(viewProj, glm::vec3(p.x, ground(p.x, p.y), p.y), size, out); };
		auto line = [&](const glm::vec2& a, const glm::vec2& b, ImU32 color, float width)
		{
			glm::vec2 sa, sb;
			if (point(a, sa) && point(b, sb)) dl->AddLine(ImVec2(ox + sa.x, oy + sa.y), ImVec2(ox + sb.x, oy + sb.y), color, width);
		};
		auto ring = [&](const glm::vec2& c, float radius, ImU32 color, int segments, bool dashed)
		{
			for (int i = 0; i < segments; ++i)
			{
				if (dashed && i % 2) continue;
				const float a0 = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853f, a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.2831853f;
				line(c + glm::vec2(std::cos(a0), std::sin(a0)) * radius, c + glm::vec2(std::cos(a1), std::sin(a1)) * radius, color, 1.5f);
			}
		};

		for (const Occluder& o : m_Tribe.Occluders()) ring(o.Center, o.Radius, IM_COL32(200, 80, 80, 150), 10, false);

		for (const TribeMember& m : m_Tribe.Members())
		{
			const Actor* actor = m_Actors.Find(m.Name);
			if (!actor || m.Dead) continue;
			const glm::vec2 eye(actor->GetPosition().x, actor->GetPosition().z);
			const bool selected = m.Name == m_AbilityActor;
			const ImU32 cone = selected ? IM_COL32(255, 230, 90, 230) : IM_COL32(120, 200, 255, 130);
			const float facing = std::atan2(m.Mind.Facing.y, m.Mind.Facing.x);
			const int arc = 14;
			glm::vec2 previous = eye + glm::vec2(std::cos(facing - vision.HalfFov), std::sin(facing - vision.HalfFov)) * vision.Range;
			line(eye, previous, cone, selected ? 2.0f : 1.0f);
			for (int i = 1; i <= arc; ++i)
			{
				const float t = facing - vision.HalfFov + 2.0f * vision.HalfFov * static_cast<float>(i) / static_cast<float>(arc);
				const glm::vec2 next = eye + glm::vec2(std::cos(t), std::sin(t)) * vision.Range;
				line(previous, next, cone, selected ? 2.0f : 1.0f);
				previous = next;
			}
			line(eye, previous, cone, selected ? 2.0f : 1.0f);
			ring(eye, vision.NearRange, cone, 16, false);
			if (!selected) continue;

			const double now = m_Tribe.Time();
			for (const auto& [name, e] : m.Mind.Memory.All())
			{
				const float conf = SpatialMemory::Confidence(e, now, m_Tribe.Config.Memory);
				const int alpha = 70 + static_cast<int>(conf * 185.0f);
				const ImU32 color = e.Kind == "berry_bush" ? IM_COL32(230, 90, 160, alpha) : e.Kind == "tree" ? IM_COL32(80, 220, 90, alpha) : e.Kind == "rock" ? IM_COL32(190, 190, 200, alpha)
					: e.Kind == "fiber_grass" ? IM_COL32(210, 220, 90, alpha) : e.Kind == "well" ? IM_COL32(90, 150, 255, alpha) : IM_COL32(255, 160, 80, alpha);
				ring(e.Pos, 0.8f, color, 10, e.Heard);
				glm::vec2 s;
				if (point(e.Pos, s)) dl->AddText(ImVec2(ox + s.x + 6.0f, oy + s.y - 6.0f), color, (name + (e.Amount >= 0 ? " " + std::to_string(e.Amount) : "")).c_str());
			}
			// what it is going to
			const auto& orders = m.Mind.Orders;
			if (!orders.empty()) { glm::vec2 s; if (point(eye, s)) dl->AddText(ImVec2(ox + s.x + 8.0f, oy + s.y - 22.0f), IM_COL32(255, 255, 255, 230), m_Tribe.Because(m).c_str()); }
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
						if (m.Dead) ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.25f, 1.0f), "dead (%s)", m.Cause.c_str());
						else if (m.Job.empty()) ImGui::TextDisabled("deciding");
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
					DrawNpcInspector(m);
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
