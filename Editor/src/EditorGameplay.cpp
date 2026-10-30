// The editor's side of the Gameplay Ability System: the `garden` and `ability` commands and the Abilities panel.
#include "EditorApp.h"
#include "GB/Log.h"
#include "GB/Gameplay/SiteChoice.h"
#include "GB/Gameplay/WorldResources.h"
#include <algorithm>
#include <fstream>
#include <cmath>
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
			if (Actor* actor = m_Actors.Find(who)) m_SimQA.NoteYield(item, actor->Bag().Add(m_Items, item, count));   // (the QA panel's "gathered")
		});
		m_Resources.SyncVisuals(m_Gameplay, m_SceneEditor.World());
		m_Ecology.Bind(m_SceneEditor.World(), m_Gameplay, [this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; });
		m_Combat.Bind(m_SceneEditor.World(), m_Gameplay);   // H14: the combat abilities and effects, and the fighters' new ability systems
		m_Interactives.Items = &m_Items;   // Stoke burns anything tagged wood, each piece for the heat its item says (branch 1, wood 2)
		m_Interactives.Bind(m_Gameplay,
			[this](const std::string& who, const std::string& item, int n) { Actor* a = m_Actors.Find(who); return a ? a->Bag().Remove(item, n) : 0; },
			[this](const std::string& who, const std::string& item, int n) { Actor* a = m_Actors.Find(who); return a ? a->Bag().Add(m_Items, item, n) : 0; });
		m_Buildings.Bind(m_Gameplay);   // H8: Build on the sites of the scene (a loaded game brings them back as entities)
	}

	void EditorApp::StepResources()
	{
		for (const std::string& name : m_Actors.Names())
		{
			Actor* actor = m_Actors.Find(name);
			actor->Bag().SyncCapacity(m_Items);   // a carried basket makes the bag bigger (also after a load: the bonus is not saved)
			if (AbilitySystemComponent* asc = m_Gameplay.Find(name))
				ResourceSystem::SyncTools(*asc, actor->Bag(), m_Items);
		}
		m_Resources.SyncVisuals(m_Gameplay, m_SceneEditor.World());
	}

	namespace
	{
		// A real actor as a member of the tribe. It looks the actor up by name on every call: `actor remove` (or a rebuilt scene) may delete the
		// actor while the tribe still lists it, and a pointer kept from `tribe build` would dangle. A member whose actor is gone is busy for good.
		class ActorBody : public ITribeBody
		{
		public:
			ActorBody(ActorWorld& actors, std::string name) : m_Actors(actors), m_Name(std::move(name)), m_Gone(1, 0.0f) {}
			bool Busy() const override { const Actor* a = m_Actors.Find(m_Name); return !a || a->TasksRunning(); }
			bool Failed() const override { const Actor* a = m_Actors.Find(m_Name); return a && a->TasksFailed(); }
			glm::vec2 Position() const override { const Actor* a = m_Actors.Find(m_Name); const glm::vec3 p = a ? a->GetPosition() : glm::vec3(0.0f); return { p.x, p.z }; }
			bool Do(const std::string& tasks, std::string& error) override
			{
				Actor* a = m_Actors.Find(m_Name);
				if (!a) { error = "the actor " + m_Name + " was removed"; return false; }
				return a->RunTasks(tasks, error);
			}
			bool CanStandAt(const glm::vec2& p) const override { const NavGrid& nav = m_Actors.Navigation(); return !nav.Ready() || nav.Walkable(p, m_Profile); }
			bool CanReach(const glm::vec2& p) const override
			{
				const Actor* a = m_Actors.Find(m_Name);
				const NavGrid& nav = m_Actors.Navigation();
				if (!a || !nav.Ready()) return true;
				const glm::vec3 from = a->GetPosition();
				NavPath path;
				return nav.Walkable(p, m_Profile) && nav.FindPath(m_Profile, { from.x, from.z }, p, path);
			}
			// The tribe hands over the role's profile with the load of the bag: the actor's own paths (Actor::Navigation) and the queries above use it.
			void SetNavigation(const NavProfile& profile) override { m_Profile = profile; if (Actor* a = m_Actors.Find(m_Name)) a->Navigation = profile; }
			// What the abstract agent walks while the actor is far away: the same path the actor would plan, or no way at all. A goal inside a wall
			// (a tree's trunk) is fine when the path ends within reach of it.
			bool Route(const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points) const override
			{
				const NavGrid& nav = m_Actors.Navigation();
				if (!nav.Ready()) { points.assign(1, to); return true; }
				NavPath path;
				const bool found = nav.FindPath(m_Profile, from, to, path);
				if (path.Points.empty()) return false;
				if (!found && glm::distance(path.Points.back(), to) > reach + nav.CellSize()) return false;
				points = std::move(path.Points);
				return true;
			}
			// H16: the abstract agent's searches share the grid's plan budget with the actors (its own place in the fair queue)
			bool MayPlan() override { return m_Actors.Navigation().AllowPlan(&m_Ticket); }
			void Teleport(const glm::vec2& p) override { if (Actor* a = m_Actors.Find(m_Name)) a->Teleport(p.x, p.y, 0.0f); }
			void SetSuspended(bool on) override { m_Actors.SetSuspended(m_Name, on); }
			std::string Error() const override { const Actor* a = m_Actors.Find(m_Name); return a ? a->TaskStatus() : std::string(); }
			Inventory& Bag() override { Actor* a = m_Actors.Find(m_Name); return a ? a->Bag() : m_Gone; }
			// H9: a child grows; the actor was made at an adult's height, so that height divided by how grown it was is what it grows to
			void SetGrowth(float g) override
			{
				Actor* a = m_Actors.Find(m_Name);
				if (!a || g <= 0.0f || std::fabs(g - m_Growth) < 1e-3f) return;
				a->SetHeight(a->Height() / m_Growth * g);
				m_Growth = g;
			}
		private:
			ActorWorld& m_Actors;
			std::string m_Name;
			Inventory m_Gone;   // what a removed actor "carries": nothing
			NavProfile m_Profile;
			float m_Growth = 1.0f;
			PlanTicket m_Ticket;
		};
	}

	// H16: what the AI costs now, in one line: the clans' thinking turns against their shared budget, the searches of the grid against the plan budget
	// (with the queue and the path cache), the milliseconds of each system in the last tick, and the animation LOD of the actors.
	std::string EditorApp::AiStatsLine() const
	{
		const ClanWorld::FrameCost& clans = m_Clans.LastCost();
		const ActorWorld::FrameCost& actors = m_Actors.LastCost();
		const NavGrid& nav = m_Actors.Navigation();
		const NavFrameStats& plans = nav.FrameStats();
		const NavStats& s = nav.Stats();
		int members = 0, abstract = 0;
		for (const auto& c : m_Clans.All()) { members += static_cast<int>(c->Folk->Members().size()); abstract += c->Folk->AbstractCount(); }
		char text[640];
		std::snprintf(text, sizeof text,
			"ai: %d members (%d abstract) in %zu clans; thinks %d of %d (budget %d shared, %d waiting); tribes %.2f ms, diplomacy %.2f ms | "
			"nav: planned %d, refused %d, queue %d (peak %d, longest wait %ld frames), %ld searches %.2f ms mean, cache %ld hits / %ld misses (%zu kept) | "
			"actors: %.2f ms, %d full, %d reduced, %d far, %d suspended, %d posed",
			members, abstract, m_Clans.Count(), clans.Thinks, clans.Budget, m_Clans.ThinkPerFrame, clans.Due, clans.TribesMs, clans.DiplomacyMs,
			plans.Planned, plans.Refused, plans.Queue, s.QueuePeak, s.WaitPeak, s.Paths, s.Paths ? s.PathSeconds * 1000.0 / static_cast<double>(s.Paths) : 0.0, s.CacheHits, s.CacheMisses, nav.CachedPaths(),
			actors.Ms, actors.Full, actors.Reduced, actors.Far, actors.Suspended, actors.Posed);
		return text;
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
		// Every cell of the window, each one a quad on the terrain's own heights at its four corners (so the mesh hugs the hills instead of leaving
		// holes where the cells were cheap): green = free, yellow = costly (slope), blue = wet, red = wall.
		const int radius = static_cast<int>(24.0f / cs);
		const float half = cs * 0.5f;
		int ci, cj;
		if (nav.CellAt({ focus.x, focus.z }, ci, cj))
			for (int j = std::max(0, cj - radius); j < std::min(nav.Height(), cj + radius); ++j)
				for (int i = std::max(0, ci - radius); i < std::min(nav.Width(), ci + radius); ++i)
				{
					const float cost = nav.CellCost(i, j, profile);
					const bool wet = nav.Depth(i, j) > 0.05f;
					const ImU32 color = cost < 0.0f ? IM_COL32(230, 60, 50, 120) : wet ? IM_COL32(60, 120, 240, 110) : cost >= 1.5f ? IM_COL32(240, 200, 50, 100) : IM_COL32(70, 220, 110, 55);
					const glm::vec2 c = nav.CellCenter(i, j);
					glm::vec2 q[4];
					const glm::vec2 corner[4] = { { -half, -half }, { half, -half }, { half, half }, { -half, half } };
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

	// H9: a newborn of the tribe gets an actor of the tribe's model beside its mother, and an ability system from its entity (only its own: the
	// others keep theirs running, which a whole SyncGameplay in the middle of the tribe's update would not).
	std::unique_ptr<ITribeBody> EditorApp::MakeTribeBody(const std::string& actor) { return std::make_unique<ActorBody>(m_Actors, actor); }

	bool EditorApp::IsClanZeroBody(const ITribeBody* body) const
	{
		return std::any_of(m_Tribe.Members().begin(), m_Tribe.Members().end(), [&](const TribeMember& m) { return m.Body == body; });
	}

	void EditorApp::SetTribeSpawner()
	{
		m_Tribe.SetSpawner([this](const std::string& name, const glm::vec2& at, float growth) -> ITribeBody*
		{
			if (m_Actors.Find(name)) return nullptr;
			std::string error;
			const glm::vec3 near(at.x + 0.8f, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(at.x + 0.8f, at.y) : 0.0f, at.y);
			Actor* actor = m_Player.Spawn(name, m_TribeModel, m_World, m_SceneEditor, m_AssetRoot, near, m_Steps.get(), error, true);
			if (!actor) { GB_CLIENT_WARN("no actor for the newborn {}: {}", name, error); return nullptr; }
			if (const Entity* e = m_SceneEditor.World().Find(m_SceneEditor.World().FindByName(name)); e && e->Gameplay && !m_Gameplay.Find(name))
				m_Gameplay.Create(name)->FromComponent(*e->Gameplay);
			m_Actors.PrepareAbilities();
			m_TribeBodies.push_back(std::make_unique<ActorBody>(m_Actors, name));
			m_TribeBodies.back()->SetGrowth(growth);
			return m_TribeBodies.back().get();
		});
	}

	namespace
	{
		constexpr int kWildEnough = 8;   // wild resource nodes near the camp that make the patch of `tribe build` unnecessary
	}

	// H8: where a clan of four (the trades the data shares out) would settle on the generated world, with the wild nodes of the scene.
	CommandResult EditorApp::SiteCommand(const std::vector<std::string>& a)
	{
		if (!m_World.IsBuilt()) return { false, "no generated world (generate temperate 5; resources world)" };
		const std::string sub = a.empty() ? "best" : a[0];
		Tribe probe;
		probe.Config.Roles = m_Tribe.Config.Roles;
		probe.Config.Work = m_Tribe.Config.Work;
		probe.Config.Economy = m_Tribe.Config.Economy;
		probe.Bind(m_SceneEditor.World(), m_Gameplay, m_Items, m_Recipes, m_NodeDefs);
		const ResourceGround ground = ResourceGroundOf(m_World.World());
		const std::vector<SiteResource> resources = SceneResources(m_SceneEditor.World(), m_Gameplay, m_NodeDefs);
		const SiteWeights weights = ClanSiteWeights(probe, probe.PlanRoles(4));
		if (sub == "score")
		{
			glm::vec2 at(m_Orbit.Target.x, m_Orbit.Target.z);
			if (a.size() >= 3) at = { static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())) };
			return { true, ScoreSite(ground, resources, weights, at).Text() };
		}
		if (sub == "best") return { true, BestSite(ground, resources, weights).Text() + " (" + std::to_string(resources.size()) + " resource nodes in the scene)" };
		if (sub == "list")
		{
			const int n = a.size() > 1 ? std::max(1, std::atoi(a[1].c_str())) : 5;
			std::string s;
			for (const SiteRating& site : RankSites(ground, resources, weights, n)) s += site.Text() + " | ";
			return { true, s.empty() ? "no place fits a camp" : s };
		}
		return { false, "site best | score [x z] | list [n]" };
	}

	// H8: the buildings of the tribe; `start` puts a site down by hand (the clan carries the materials and builds it like one it planned).
	CommandResult EditorApp::SettlementCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "buildings")
		{
			std::string s;
			for (const auto& [id, def] : m_Buildings.Defs)
			{
				s += id + " (" + std::to_string(def.Phases.size()) + " phases:";
				for (const auto& [item, n] : def.TotalNeeds()) s += " " + item + " x" + std::to_string(n);
				s += ")" + (def.Station.empty() ? std::string() : " station " + def.Station) + (def.Shelter ? " beds " + std::to_string(def.Shelter) : std::string())
					+ (def.Store.empty() ? std::string() : " store " + def.Store) + " | ";
			}
			return { true, s };
		}
		if (ActiveTribe().Members().empty()) return { false, "no tribe (tribe build [model.fbx])" };
		if (sub == "start" && a.size() > 1)
		{
			glm::vec2 at = ActiveTribe().Config.Economy.Camp + glm::vec2(ActiveTribe().Config.SiteRadius, 0.0f);
			if (a.size() >= 4) at = { static_cast<float>(std::atof(a[2].c_str())), static_cast<float>(std::atof(a[3].c_str())) };
			std::string name;
			if (const std::string error = ActiveTribe().StartSite(a[1], at, &name); !error.empty()) return { false, error };
			return { true, name + " started at " + std::to_string(static_cast<int>(at.x)) + ", " + std::to_string(static_cast<int>(at.y)) };
		}
		if (sub != "status") return { false, "settlement status | start <building> [x z] | buildings" };
		std::string s = ActiveTribe().SettlementReport();
		for (char& c : s) if (c == '\n') c = '|';
		return { true, s };
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
			if (numbers.size() >= 3) workers = std::max(1, std::min(200, static_cast<int>(numbers[2])));   // (no fixed crew: the names and trades come from the data)
			else if (numbers.size() == 1) workers = std::max(1, std::min(200, static_cast<int>(numbers[0])));   // `tribe build <model> <n>`: n workers where the site choice says (H15: `play`)
			// H8: with no place given, on a generated world the clan settles where the site choice says (by the water, near what its trades want, on flat
			// ground); the camp is 8 m south of the origin, as always. Without a world (a test scene) it is the orbit camera's target, as before.
			std::string chosen;
			if (numbers.size() < 2 && m_World.IsBuilt())
			{
				m_Tribe.Bind(m_SceneEditor.World(), m_Gameplay, m_Items, m_Recipes, m_NodeDefs);
				const SiteRating best = BestSite(ResourceGroundOf(m_World.World()), SceneResources(m_SceneEditor.World(), m_Gameplay, m_NodeDefs), ClanSiteWeights(m_Tribe, m_Tribe.PlanRoles(workers)));
				if (best.Ok)
				{
					origin.x = best.At.x;
					origin.z = best.At.y + 8.0f;
					chosen = "; the site: " + best.Text();
				}
			}
			origin.y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(origin.x, origin.z) : 0.0f;

			// The names come from the data ("work" section: WorkRules::Names, then again with a number) and the trades from the clan's needs (Tribe::PlanRoles):
			// the stock it wants of what each trade gathers best, and the campfire. The actors of the last tribe and of these names go first.
			for (const TribeMember& m : m_Tribe.Members()) if (m_Actors.Find(m.Name)) m_Actors.Remove(m.Name);
			m_TribeBodies.erase(std::remove_if(m_TribeBodies.begin(), m_TribeBodies.end(), [&](const std::unique_ptr<ITribeBody>& b) { return IsClanZeroBody(b.get()); }), m_TribeBodies.end());
			m_Tribe.Clear();   // (the other clans, H13, keep their members and bodies)
			m_SimQA.Reset();   // a new tribe: the live checks start again
			m_QaWarned = false;
			std::vector<std::string> names;
			for (size_t k = 0; static_cast<int>(names.size()) < workers; ++k)   // (a name another clan uses is skipped: the actors' names are unique)
				if (const std::string n = MemberName(m_Tribe.Config.Work, k); !m_Clans.NameTaken(n)) names.push_back(n);
			for (const std::string& n : names) if (m_Actors.Find(n)) m_Actors.Remove(n);

			const glm::vec2 camp(origin.x, origin.z - 8.0f);
			// The patch of resource nodes is for empty and flat test worlds: where the world already has wild nodes near the camp, the clan lives off them.
			int wild = 0;
			for (const SiteResource& r : SceneResources(m_SceneEditor.World(), m_Gameplay, m_NodeDefs)) if (glm::distance(r.At, camp) < 40.0f) ++wild;
			CommandResult built{ true, std::to_string(wild) + " wild resource nodes within 40 m: no patch" };
			if (wild < kWildEnough)
			{
				built = ResourcesCommand({ "build", std::to_string(origin.x), std::to_string(origin.z), std::to_string(workers) });
				if (!built.Ok) return built;
			}
			{
				// a well near the camp (the members get thirsty); the fire pit and the gate of the interactives are not wanted here
				EntityWorld& world = m_SceneEditor.World();
				const glm::vec3 wellOrigin(camp.x + 4.0f, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(camp.x + 4.0f, camp.y - 6.0f) : 0.0f, camp.y - 6.0f);
				BuildInteractives(world, wellOrigin, m_Interactives.Defs);
				for (const char* extra : { "Fire Pit", "Gate" }) if (const EntityId id = world.FindByName(extra); id != NullEntity) world.Destroy(id);
				SyncGameplay();
			}
			std::vector<ITribeBody*> bodies;
			for (int i = 0; i < workers; ++i)
			{
				// a row of up to 8, rows behind each other: a large clan does not stand in one line across the map
				const int col = i % 8, row = i / 8, inRow = std::min(8, workers - row * 8);
				const CommandResult r = Execute("actor spawn " + names[static_cast<size_t>(i)] + " \"" + model + "\" " + std::to_string(camp.x + (static_cast<float>(col) - static_cast<float>(inRow - 1) * 0.5f) * 1.5f) + " " + std::to_string(camp.y + 1.5f * static_cast<float>(row)) + " ai");
				if (!r.Ok) return r;
				m_TribeBodies.push_back(std::make_unique<ActorBody>(m_Actors, names[static_cast<size_t>(i)]));
				bodies.push_back(m_TribeBodies.back().get());
			}
			m_SeasonVisuals = true;
			m_Tribe.Bind(m_SceneEditor.World(), m_Gameplay, m_Items, m_Recipes, m_NodeDefs);
			m_TribeModel = model;
			SetTribeSpawner();   // H9: its children get actors of the same model
			m_Panels.Tribe = true;
			m_FocusTribe = 20;
			SelectActor(names.front(), false);   // the panel shows its mind at once
			m_Tribe.Config.Economy.Camp = camp;
			m_Tribe.Config.Economy.CampY = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(camp.x, camp.y) : 0.0f;
			m_Tribe.Store().Add(m_Items, "wood", 2 + 2 * workers);    // what they found lying around on the first day
			m_Tribe.Store().Add(m_Items, "stone", 3 * workers);
			const std::vector<std::string> roles = m_Tribe.PlanRoles(workers);
			std::map<std::string, int> shares;
			for (int i = 0; i < workers; ++i)
			{
				const std::string role = static_cast<size_t>(i) < roles.size() ? roles[static_cast<size_t>(i)] : std::string("forager");
				m_Tribe.AddMember(names[static_cast<size_t>(i)], role, bodies[static_cast<size_t>(i)]);
				++shares[role];
			}
			const std::string game = m_Tribe.Config.Social.On ? "; " + SpawnWildlife(m_Tribe) : std::string();   // social life: deer, boar, grouse, rabbits around the camp
			std::string trades;
			for (const auto& [role, n] : shares) trades += (trades.empty() ? "" : ", ") + std::to_string(n) + " " + role;
			return { true, std::to_string(workers) + " workers at the camp (" + std::to_string(static_cast<int>(camp.x)) + ", " + std::to_string(static_cast<int>(camp.y)) + "): " + trades + "; " + built.Message + chosen + game + "; they act on their own: tribe status | tribe jobs | tribe log | settlement status" };
		}
		if (sub == "remove" && a.size() > 1)
		{
			const ITribeBody* body = nullptr;
			for (const TribeMember& m : m_Tribe.Members()) if (m.Name == a[1]) body = m.Body;
			if (!m_Tribe.RemoveMember(a[1])) return { false, "no member called '" + a[1] + "'" };
			m_TribeBodies.erase(std::remove_if(m_TribeBodies.begin(), m_TribeBodies.end(), [&](const std::unique_ptr<ITribeBody>& b) { return b.get() == body; }), m_TribeBodies.end());
			return { true, a[1] + " left the tribe (the actor stays: actor remove " + a[1] + ")" };
		}
		if (sub == "clear")
		{
			m_TribeBodies.erase(std::remove_if(m_TribeBodies.begin(), m_TribeBodies.end(), [&](const std::unique_ptr<ITribeBody>& b) { return IsClanZeroBody(b.get()); }), m_TribeBodies.end());
			m_Tribe.Clear();
			return { true, "tribe cleared (the actors stay: actor remove <name>)" };
		}
		// the chronicle of the clan (Chronicle.h): what it went through, by itself while it lives; the report is Markdown, the series CSV
		if (sub == "chronicle")
		{
			Chronicle& c = ActiveTribe().Record();
			const std::string what = a.size() > 1 ? a[1] : "summary";
			if (what == "summary") return { true, c.Summary() };
			if (what == "clear") { c.Clear(); return { true, "chronicle cleared" }; }
			if (what == "every")
			{
				if (a.size() < 3) return { true, "one row every " + std::to_string(static_cast<int>(c.Every)) + " simulation seconds" };
				c.Every = std::max(1.0, std::atof(a[2].c_str()));
				return { true, "one row every " + std::to_string(static_cast<int>(c.Every)) + " simulation seconds" };
			}
			if (what == "report" || what == "csv")
			{
				const std::string text = what == "csv" ? c.Csv() : c.Markdown();
				if (a.size() < 3) return { true, c.Summary() + " (give a file: tribe chronicle " + what + " <path>)" };
				std::ofstream out(a[2], std::ios::binary);
				if (!out) return { false, "cannot write " + a[2] };
				out << text;
				return { true, what + " written to " + a[2] + " (" + std::to_string(text.size()) + " bytes)" };
			}
			return { false, "usage: tribe chronicle [summary | report <file.md> | csv <file.csv> | every <seconds> | clear]" };
		}
		const bool society = sub == "family" || sub == "relations" || sub == "culture" || sub == "relate" || sub == "day";
		if (sub != "log" && sub != "status" && sub != "jobs" && !society)
			return { false, "usage: tribe build [model.fbx|fox] [x z] [workers] | status | jobs | log [n] | chronicle [report|csv <file>] | remove <name> | clear | family <name> | relations <name> | culture | relate <a> <b> <event> | day" };
		if (ActiveTribe().Members().empty()) return { false, "no tribe (tribe build [model.fbx])" };
		// H9 + H10: the family and character of a member, its relations, the culture of the clan; an event between two members; a day of the society now
		if (society)
		{
			auto member = [&](const std::string& n) { return std::any_of(ActiveTribe().Members().begin(), ActiveTribe().Members().end(), [&](const TribeMember& m) { return m.Name == n; }); };
			if (sub == "culture") return { true, ActiveTribe().CultureReport() };
			if (sub == "day") { ActiveTribe().SocietyDay(); return { true, "a day of the society passed: " + ActiveTribe().CultureReport() }; }
			if (sub == "relate")
			{
				if (a.size() < 4) return { false, "usage: tribe relate <a> <b> <event> (work, food, camp, conflict, teach, help, theft, rescue... see the society section)" };
				if (!member(a[1]) || !member(a[2])) return { false, "both must be members of the tribe" };
				if (!ActiveTribe().Config.Society.Relations.Events.count(a[3])) return { false, "unknown event '" + a[3] + "'" };
				ActiveTribe().Relate(a[1], a[2], a[3]);
				return { true, ActiveTribe().RelationsReport(a[1]) };
			}
			if (a.size() < 2 || !member(a[1])) return { false, "usage: tribe " + sub + " <member>" };
			std::string s = sub == "family" ? ActiveTribe().FamilyReport(a[1]) : ActiveTribe().RelationsReport(a[1]);
			for (char& c : s) if (c == '\n') c = '|';
			return { true, s };
		}
		if (sub == "log")
		{
			const int n = a.size() > 1 ? std::max(1, std::atoi(a[1].c_str())) : 12;
			const auto& log = ActiveTribe().Log();
			std::string s;
			for (size_t i = log.size() > static_cast<size_t>(n) ? log.size() - static_cast<size_t>(n) : 0; i < log.size(); ++i) s += log[i] + " | ";
			return { true, s.empty() ? "nothing has happened yet" : s };
		}
		std::string s = sub == "jobs" ? ActiveTribe().JobsReport() : ActiveTribe().Report();   // jobs: the clan's board (H7) and who holds what
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
			m_Visuals.Cache().Foliage = leaf;   // the procedural crowns and bushes take the season's colour too
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
			std::vector<std::string> args(a.begin() + 1, a.end());
			std::string species = "rabbit";   // a word among the numbers is the species
			for (auto it = args.begin(); it != args.end(); ++it)
				if (m_Ecology.Species.count(*it)) { species = *it; args.erase(it); break; }
			int count = !args.empty() ? std::max(1, std::atoi(args[0].c_str())) : 4;
			if (args.size() > 2) { at.x = static_cast<float>(std::atof(args[1].c_str())); at.z = static_cast<float>(std::atof(args[2].c_str())); }
			m_SeasonVisuals = true;   // a world with animals has seasons you can see
			const int made = m_Ecology.Spawn(at, count, species);
			m_SceneEditor.Commit();
			const auto def = m_Ecology.Species.find(species);
			return { made > 0, std::to_string(made) + " " + (def != m_Ecology.Species.end() ? def->second.Plural : species) + " at (" + std::to_string(static_cast<int>(at.x)) + ", " + std::to_string(static_cast<int>(at.z)) + "); " + m_Ecology.Describe() };
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
		if (sub != "status") return { false, "usage: ecology spawn [n] [x z] [species] | status | season [spring|summer|autumn|winter]" };
		const double clock = static_cast<double>(m_SimTick) / 60.0;
		return { true, std::string(SeasonClock::Name(SeasonClock().At(clock))) + "; " + m_Ecology.Describe() };
	}

	void EditorApp::DrawWorldStatePanel()
	{
		if (m_FocusWorldState > 0) { ImGui::SetNextWindowFocus(); --m_FocusWorldState; }
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
				if (m_Ecology.Species.count("fox")) { ImGui::SameLine(); if (ImGui::SmallButton("Add a fox")) m_UiCommands.push_back("ecology spawn 1 fox"); }
				for (const std::string& n : m_Gameplay.Names())
				{
					const AbilitySystemComponent* a = m_Gameplay.Find(n);
					if (!a || !a->HasTag("Animal")) continue;
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
					else if (a->HasTag("Interactive.Well")) { any = true; ImGui::Text("%s: water %.0f / %.0f", n.c_str(), static_cast<double>(a->Attributes().Current("Water")), static_cast<double>(a->Attributes().Max("Water"))); }
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
			m_Gameplay.Store(m_SceneEditor.World());   // (what the systems hold now goes into their components first: SyncGameplay rebuilds every system from them, and the nodes, rabbits and plots already running would start over)
			BuildInteractives(m_SceneEditor.World(), origin, m_Interactives.Defs);
			m_SceneEditor.Select(NullEntity);
			SyncGameplay();
			m_SceneEditor.Commit();
			return { true, "a campfire (\"Fire Pit\"), a well and a gate built; try: actor bag <name> add wood 2, ability activate Stoke \"Fire Pit\" <name>, ability activate DrawWater Well <name>, ability activate OpenDoor Gate <name>" };
		}
		if (sub != "status") return { false, "usage: interactives build [x z] | status" };
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
			m_Gameplay.Store(m_SceneEditor.World());   // (see interactives build)
			// `resources build x z <workers>`: the patch the node definitions describe (count, spacing and shape per type) for a tribe of that size
			const int workers = a.size() > 3 ? std::max(1, std::atoi(a[3].c_str())) : 1;
			const ResourceLayout layout = BuildResourcePatch(m_SceneEditor.World(), origin, m_NodeDefs, workers);
			if (m_World.IsBuilt()) SnapToGround(m_SceneEditor.World(), layout, [this](float x, float z) { return m_World.GetTerrain().GetHeight(x, z); });
			m_SceneEditor.Select(NullEntity);
			SyncGameplay();
			m_SceneEditor.Commit();
			std::string kinds;
			for (const auto& [id, n] : PatchCounts(m_NodeDefs, workers)) kinds += (kinds.empty() ? "" : ", ") + std::to_string(n) + " " + id;
			return { true, std::to_string(layout.Nodes.size()) + " resource nodes built at (" + std::to_string(static_cast<int>(origin.x)) + ", " + std::to_string(static_cast<int>(origin.z))
				+ ") for " + std::to_string(workers) + " worker(s): " + kinds + ". Gather with: ability activate Gather \"berry bush 1\", Collect \"clay bank 1\" (Chop needs an axe, Mine a pickaxe: actor bag <name> craft stone_axe workbench)" };
		}
		if (sub == "world")
		{
			// `resources world [density]`: the nodes the data spreads over the whole generated map, by biome (ResourceNodeDef::Spawn)
			if (!m_World.IsBuilt()) return { false, "resources world needs a generated world (generate temperate|alpine|desert <seed>)" };
			const float density = a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : 1.0f;
			if (!(density > 0.0f && density <= 10.0f)) return { false, "the density must be above 0 and at most 10" };
			m_Gameplay.Store(m_SceneEditor.World());   // (see interactives build)
			const std::vector<ResourceSpot> spots = PlanWorldResources(ResourceGroundOf(m_World.World()), m_NodeDefs, m_World.World().Desc.Seed, density);
			const ResourceLayout layout = BuildWorldResources(m_SceneEditor.World(), spots, m_NodeDefs);
			m_WildResources = density;   // the seed makes them again: a save keeps only what changed of them (EditorSaveHost::BuildBaseline)
			m_SceneEditor.Select(NullEntity);
			SyncGameplay();
			m_SceneEditor.Commit();
			std::string kinds;
			for (const auto& [id, n] : CountSpots(spots)) kinds += (kinds.empty() ? "" : ", ") + std::to_string(n) + " " + id;
			return { true, std::to_string(layout.Nodes.size()) + " resource nodes spread over the world (density " + std::to_string(density).substr(0, 4) + "): " + (kinds.empty() ? "none" : kinds) };
		}
		if (sub != "status") return { false, "usage: resources build [x z [workers]] | world [density] | status" };
			// Grouped by type: "tree: 3 nodes, 11 units, 1 depleted [tree 1: 5, tree 2: 0 (depleted), ...]"
		struct Group { int Nodes = 0, Units = 0, Depleted = 0; std::string Each; };
		std::map<std::string, Group> groups;
		for (const std::string& n : m_Gameplay.Names())
			if (const AbilitySystemComponent* node = m_Gameplay.Find(n); node && node->HasTag("Resource.Node"))
			{
				Group& g = groups[n.substr(0, n.rfind(' '))];
				const bool empty = node->HasTag("State.Depleted");
				++g.Nodes;
				g.Units += ResourceSystem::Amount(*node);
				g.Depleted += empty ? 1 : 0;
				g.Each += (g.Each.empty() ? "" : ", ") + n.substr(n.rfind(' ') + 1) + ": " + std::to_string(ResourceSystem::Amount(*node)) + (empty ? " (depleted)" : "");
			}
		if (groups.empty()) return { false, "no resource nodes in the scene (resources build)" };
		std::string s;
		for (const auto& [kind, g] : groups)
			s += kind + ": " + std::to_string(g.Nodes) + " nodes, " + std::to_string(g.Units) + " units" + (g.Depleted ? ", " + std::to_string(g.Depleted) + " depleted" : "") + " [" + g.Each + "]; ";
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
			m_Gameplay.Store(m_SceneEditor.World());   // (see interactives build)
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


	std::string EditorApp::PickNode(const Ray& ray) const
	{
		std::string best;
		float bestT = 1e30f;
		const glm::vec3 d = glm::normalize(ray.Dir);
		for (const auto& [id, e] : m_SceneEditor.World().All())
		{
			const AbilitySystemComponent* asc = m_Gameplay.Find(e.Name);
			if (!asc) continue;
			bool node = false;
			for (const auto& [kind, def] : m_NodeDefs) node = node || asc->HasTag("Resource.Node." + kind);
			if (!node) continue;
			// the node as a vertical segment of a metre and a half: the closest point of the ray to its middle
			const glm::vec3 centre = e.Transform.Position + glm::vec3(0.0f, 0.75f, 0.0f);
			const float s = glm::max(0.0f, glm::dot(centre - ray.Origin, d));
			const float distance = glm::length(ray.Origin + d * s - centre);
			if (distance < 1.1f && s < bestT) { bestT = s; best = e.Name; }
		}
		return best;
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
			m_TribeBodies.push_back(std::make_unique<ActorBody>(m_Actors, name));
			m_Tribe.AddMember(name, role, m_TribeBodies.back().get());
		}
		if (!m_Tribe.FromJson(json, true)) GB_CLIENT_WARN("the saved tribe could not be read: it starts again");
		SetTribeSpawner();   // H9: children born after the load get actors too (of m_TribeModel)
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

	// H11: what the members and the clan know (GB/Game/Knowledge.h); `grant` teaches a technology at once, for trying things out.
	CommandResult EditorApp::KnowledgeCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		ClanKnowledge& k = m_Tribe.Knowledge();
		if (sub == "techs")
		{
			std::string s = std::to_string(m_Techs.Techs.size()) + " technologies:";
			for (const auto& [id, t] : m_Techs.Techs)
			{
				s += " | " + id + " (" + t.Name + ", era " + std::to_string(t.Era) + (t.Known ? ", known from the start" : "") + ")";
				if (!t.Requires.empty()) { s += " needs"; for (const std::string& q : t.Requires) s += " " + q; }
				if (!t.Recipes.empty()) { s += " unlocks"; for (const std::string& r : t.Recipes) s += " " + r; }
			}
			return { true, s };
		}
		if (m_Tribe.Members().empty()) return { false, "no tribe (tribe build [model.fbx])" };
		if (sub == "status")
		{
			if (a.size() > 1 && a[1] == "clan") return { true, k.ClanStatus() };
			if (a.size() > 1) return { k.Of(a[1]) != nullptr || !k.Gated(), k.Status(a[1]) };
			std::string s = k.ClanStatus();
			for (const TribeMember& m : m_Tribe.Members()) s += " | " + k.Status(m.Name);
			return { true, s };
		}
		if (sub == "grant" && a.size() > 2)
		{
			const std::string error = a[1] == "clan" ? k.GrantClan(a[2]) : k.Grant(a[1], a[2]);
			if (!error.empty()) return { false, error };
			return { true, (a[1] == "clan" ? std::string("the whole clan") : a[1]) + " now knows " + k.Name(a[2]) };
		}
		return { false, "knowledge status [name|clan] | grant <name|clan> <tech> | techs" };
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
		ImGui::TextWrapped("Decision: %s", ActiveTribe().Because(m).c_str());
		const std::map<std::string, float> needs = ActiveTribe().NeedLevels(m);
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
			ImGui::TextDisabled("%s, %02d:00, %s", SeasonClock::Name(ActiveTribe().CurrentSeason()), static_cast<int>(ActiveTribe().Hour()), IsNight(ActiveTribe().Config.Needs, ActiveTribe().Hour()) ? "night" : "day");
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
			ImGui::Text("Knows %zu places (the clan %zu):", m.Mind.Memory.Size(), ActiveTribe().ClanMemory().Size());
			ImGui::BeginChild("memory", ImVec2(0.0f, 110.0f), true);
			for (const auto& [name, e] : m.Mind.Memory.All())
				ImGui::Text("%s  %s  (%.0f, %.0f)  %s  seen %.0f s ago%s", name.c_str(), e.Kind.c_str(), static_cast<double>(e.Pos.x), static_cast<double>(e.Pos.y),
					e.Amount < 0 ? "?" : (std::to_string(e.Amount) + " left").c_str(), ActiveTribe().Time() - e.Seen, e.Heard ? ", by ear" : "");
			ImGui::EndChild();
			ImGui::Checkbox("Draw view cones and memory in the viewport (ai debug)", &m_AiDebug);
		}
		// H9 + H10: its life, its family, its genes and personality, and whom it likes
		if (const Person* p = ActiveTribe().People().Find(m.Name); p && ImGui::CollapsingHeader("Life and family"))
		{
			const SocietyRules& rules = ActiveTribe().Config.Society;
			ImGui::Text("%s, %.1f years (%s), generation %d", p->Female ? "woman" : "man", static_cast<double>(p->Age), Society::StageName(ActiveTribe().People().StageOf(rules, m.Name)), p->Generation);
			ImGui::Text("Mother %s   father %s   partner %s   children %d", p->Mother.empty() ? "-" : p->Mother.c_str(), p->Father.empty() ? "-" : p->Father.c_str(),
				p->Partner.empty() ? "-" : p->Partner.c_str(), p->Children);
			ImGui::TextDisabled("Genes");
			for (const auto& [id, v] : p->Genes)
				if (rules.Genes.count(id)) ImGui::ProgressBar(v, ImVec2(-1.0f, 0.0f), (id + " " + std::to_string(static_cast<int>(v * 100.0f))).c_str());
			ImGui::TextDisabled("Personality (gene and upbringing)");
			for (const auto& [id, v] : p->Traits) ImGui::ProgressBar(v, ImVec2(-1.0f, 0.0f), (id + " " + std::to_string(static_cast<int>(v * 100.0f))).c_str());
		}
		if (ActiveTribe().People().Find(m.Name) && ImGui::CollapsingHeader("Relations"))
		{
			const SocietyRules& rules = ActiveTribe().Config.Society;
			const auto bonds = ActiveTribe().People().BondsOf(m.Name);
			if (bonds.empty()) ImGui::TextDisabled("no bonds yet (they grow from shared work, food, the camp and lessons)");
			for (const auto& [other, bond] : bonds)
			{
				const std::string kin = ActiveTribe().People().Kin(m.Name, other);
				const float affinity = ActiveTribe().People().Affinity(rules, m.Name, other);
				ImGui::TextColored(affinity >= 0.0f ? ImVec4(0.55f, 0.9f, 0.55f, 1.0f) : ImVec4(0.95f, 0.5f, 0.45f, 1.0f), "%s  %+.2f%s", other.c_str(), static_cast<double>(affinity), kin.empty() ? "" : ("  (" + kin + ")").c_str());
			}
		}
		if (ActiveTribe().Knowledge().Gated() && ImGui::CollapsingHeader("Knowledge"))
		{
			if (const AgentKnowledge* k = ActiveTribe().Knowledge().Of(m.Name))
			{
				std::string known;
				for (const std::string& t : k->Known) known += (known.empty() ? "" : ", ") + ActiveTribe().Knowledge().Name(t);
				ImGui::TextWrapped("Knows: %s", known.empty() ? "nothing" : known.c_str());
				for (const auto& [t, p] : k->Progress)
				{
					const std::string label = ActiveTribe().Knowledge().Name(t) + " " + std::to_string(static_cast<int>(p * 100.0f)) + "%";
					ImGui::ProgressBar(p, ImVec2(-1.0f, 0.0f), label.c_str());
				}
				for (const auto& [t, n] : k->Tries) ImGui::TextDisabled("tried %s %d time%s", ActiveTribe().Knowledge().Name(t).c_str(), n, n == 1 ? "" : "s");
			}
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

	// What the clan has in the world, so it can be followed without the console: every field plot painted by state (the inner square grows with the
	// crop, a blue dot is moist soil), the stockpiles, and over each member its name, trade and what it is doing.
	void EditorApp::DrawClanOverlay(ImDrawList* dl, const glm::mat4& viewProj, const glm::vec2& size, float ox, float oy)
	{
		auto ground = [&](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; };
		auto screen = [&](const glm::vec3& p, ImVec2& out)
		{
			glm::vec2 s;
			if (!ProjectToScreen(viewProj, p, size, s)) return false;
			out = ImVec2(ox + s.x, oy + s.y);
			return true;
		};
		auto quad = [&](const glm::vec2& c, float half, ImU32 fill, ImU32 edge)
		{
			ImVec2 q[4];
			const glm::vec2 corner[4] = { { -half, -half }, { half, -half }, { half, half }, { -half, half } };
			for (int i = 0; i < 4; ++i)
			{
				const glm::vec2 p = c + corner[i];
				if (!screen(glm::vec3(p.x, ground(p.x, p.y) + 0.06f, p.y), q[i])) return;
			}
			dl->AddConvexPolyFilled(q, 4, fill);
			if (edge) dl->AddPolyline(q, 4, edge, ImDrawFlags_Closed, 1.0f);
		};
		auto label = [&](const ImVec2& at, const std::string& text, ImU32 color)
		{
			const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
			const ImVec2 a(at.x - ts.x * 0.5f, at.y - ts.y);
			dl->AddRectFilled(ImVec2(a.x - 3.0f, a.y - 1.0f), ImVec2(a.x + ts.x + 3.0f, a.y + ts.y + 1.0f), IM_COL32(10, 12, 16, 170), 3.0f);
			dl->AddText(a, color, text.c_str());
		};

		// the dead: a cross where each one fell, with its name and what killed it
		for (const auto& [name, g] : m_Graves)
		{
			ImVec2 s;
			if (glm::distance(m_Camera.Position, glm::vec3(g.Pos.x, ground(g.Pos.x, g.Pos.y), g.Pos.y)) > 90.0f || !screen(glm::vec3(g.Pos.x, ground(g.Pos.x, g.Pos.y) + 1.4f, g.Pos.y), s)) continue;
			dl->AddLine(ImVec2(s.x, s.y - 14.0f), ImVec2(s.x, s.y + 6.0f), IM_COL32(235, 235, 225, 235), 3.0f);
			dl->AddLine(ImVec2(s.x - 7.0f, s.y - 8.0f), ImVec2(s.x + 7.0f, s.y - 8.0f), IM_COL32(235, 235, 225, 235), 3.0f);
			label(ImVec2(s.x, s.y + 22.0f), name + " (" + g.Cause + ")", IM_COL32(210, 205, 195, 235));
		}
		std::vector<std::pair<const Tribe*, std::string>> tribes{ { &m_Tribe, std::string() } };
		for (const auto& c : m_Clans.All())
			if (c->Folk && c->Folk != &m_Tribe) tribes.emplace_back(c->Folk, c->Name);
		for (const auto& [tribe, clanName] : tribes)
		{
		const Tribe& t = *tribe;
		const bool mine = tribe == &m_Tribe;
		const ImU32 tint = mine ? IM_COL32(255, 220, 120, 240) : IM_COL32(150, 200, 255, 240);

		// the camp: where the clan lives, with what it has in store and whether the fire is lit
		{
			const glm::vec2 camp = t.Config.Economy.Camp;
			ImVec2 s;
			if (glm::distance(m_Camera.Position, glm::vec3(camp.x, ground(camp.x, camp.y), camp.y)) < 140.0f && screen(glm::vec3(camp.x, ground(camp.x, camp.y) + 3.2f, camp.y), s))
			{
				std::string head = (clanName.empty() ? std::string("Your clan") : "Clan " + clanName) + " camp - " + std::to_string(t.Members().size()) + " people";
				if (t.HasFire()) head += t.FireBurning() ? ", fire lit" : ", fire OUT";
				if (const int beds = t.Beds(); beds > 0) head += ", " + std::to_string(beds) + " beds";
				label(ImVec2(s.x, s.y - 2.0f * ImGui::GetTextLineHeight() - 6.0f), head, tint);
				std::string store;
				int shown = 0;
				for (const std::string& id : m_Items.Ids())
				{
					const int n = t.Store().Count(id);
					if (n <= 0 || shown >= 5) continue;
					const ItemDef* def = m_Items.Find(id);
					store += (store.empty() ? "" : ", ") + (def ? def->Name : id) + " " + std::to_string(n);
					++shown;
				}
				label(ImVec2(s.x, s.y - ImGui::GetTextLineHeight() - 3.0f), "Store: " + (store.empty() ? std::string("empty") : store), IM_COL32(230, 200, 150, 235));
				label(s, "(the store is the camp's: leave what you gather here)", IM_COL32(180, 180, 180, 200));
			}
		}
		// the buildings: a finished one says what it is for, a site how far it is and what it still lacks
		for (const Tribe::SettlementSite& b : t.SettlementSites())
		{
			ImVec2 s;
			if (glm::distance(m_Camera.Position, glm::vec3(b.Pos.x, ground(b.Pos.x, b.Pos.y), b.Pos.y)) > 120.0f) continue;
			const bool done = b.State.Done;
			quad(b.Pos, 1.4f, done ? IM_COL32(200, 160, 90, 70) : IM_COL32(120, 160, 230, 70), done ? IM_COL32(220, 180, 100, 220) : IM_COL32(130, 170, 240, 220));
			if (!screen(glm::vec3(b.Pos.x, ground(b.Pos.x, b.Pos.y) + 2.6f, b.Pos.y), s)) continue;
			std::string text = b.Building;
			if (done)
			{
				if (b.Beds > 0) text += " - " + std::to_string(b.Beds) + " beds";
				if (!b.Station.empty()) text += " - " + b.Station;
				if (!b.Store.empty()) text += " - store";
				label(s, text, IM_COL32(235, 205, 140, 240));
			}
			else
			{
				text += " - building " + std::to_string(static_cast<int>(b.State.Progress * 100.0f)) + "% (" + b.State.PhaseName + ")";
				label(ImVec2(s.x, s.y - ImGui::GetTextLineHeight() - 3.0f), text, IM_COL32(150, 190, 250, 240));
				std::string lacks;
				for (const auto& [item, n] : b.State.Missing) lacks += (lacks.empty() ? "" : ", ") + item + " x" + std::to_string(n);
				label(s, b.State.Ready ? std::string("needs work (press E beside it)") : lacks.empty() ? std::string("waiting") : "needs " + lacks, IM_COL32(235, 235, 235, 230));
			}
		}

		for (const Field& f : t.Economy().Fields)
		{
			for (size_t i = 0; i < f.Plots.size(); ++i)
			{
				const FieldPlot& p = f.Plots[i];
				const glm::vec2 c = f.PlotPos(i);
				const float half = f.Spacing * 0.45f;
				ImU32 fill = IM_COL32(70, 130, 60, 90), inner = 0;
				if (p.State == "dug") fill = IM_COL32(120, 84, 50, 190);
				else if (p.State == "sown") { fill = IM_COL32(120, 84, 50, 200); inner = IM_COL32(200, 170, 90, 230); }
				else if (p.State == "sprout") { fill = IM_COL32(100, 80, 50, 200); inner = IM_COL32(110, 200, 80, 235); }
				else if (p.State == "grown") { fill = IM_COL32(150, 120, 50, 215); inner = IM_COL32(240, 205, 70, 245); }
				quad(c, half, fill, IM_COL32(20, 20, 20, 170));
				if (inner) quad(c, half * (0.3f + 0.7f * glm::clamp(p.State == "grown" ? 1.0f : p.Growth, 0.0f, 1.0f)), inner, 0);
				ImVec2 s;
				if (p.Moisture > 0.0f && screen(glm::vec3(c.x, ground(c.x, c.y) + 0.12f, c.y), s)) dl->AddCircleFilled(s, 3.0f, IM_COL32(90, 170, 255, 230));
			}
			ImVec2 s;
			const glm::vec2 mid = f.Centre();
			if (screen(glm::vec3(mid.x, ground(mid.x, mid.y) + 1.4f, mid.y), s)) label(s, f.Name + " (" + std::to_string(f.Count("grown")) + " ripe)", IM_COL32(240, 220, 120, 235));
		}
		for (const Stockpile& st : t.Economy().Stockpiles)
		{
			quad(st.Pos, 0.6f, IM_COL32(160, 120, 70, 220), IM_COL32(20, 20, 20, 200));
			ImVec2 s;
			if (screen(glm::vec3(st.Pos.x, ground(st.Pos.x, st.Pos.y) + 1.1f, st.Pos.y), s)) label(s, st.Name, IM_COL32(230, 200, 150, 235));
		}

		for (const TribeMember& m : t.Members())
		{
			glm::vec2 at;
			if (const Actor* actor = m_Actors.Find(m.Name)) at = glm::vec2(actor->GetPosition().x, actor->GetPosition().z);
			else if (m.Body) at = m.Body->Position();
			else continue;
			if (glm::distance(m_Camera.Position, glm::vec3(at.x, ground(at.x, at.y), at.y)) > 90.0f) continue;
			ImVec2 s;
			if (!screen(glm::vec3(at.x, ground(at.x, at.y) + 2.5f, at.y), s)) continue;
			const bool you = m_Game.Active && m.Name == m_Game.Player;
			if (you) dl->AddCircle(s, 14.0f, IM_COL32(255, 235, 90, 255), 20, 2.5f);
			const ImU32 color = you ? IM_COL32(255, 235, 90, 255) : m.Dead ? IM_COL32(150, 150, 150, 220) : m.Role == "forager" ? IM_COL32(150, 235, 120, 255) : m.Role == "miner" ? IM_COL32(150, 200, 255, 255)
				: m.Role == "woodcutter" ? IM_COL32(235, 180, 110, 255) : IM_COL32(255, 255, 255, 255);
			std::string task = m.Dead ? "dead" + (m.Cause.empty() ? std::string() : " of " + m.Cause) : m.Job.empty() ? std::string("idle") : m.Job;
			if (!m.Dead && !m.Detail.empty()) task += " " + m.Detail;
			label(ImVec2(s.x, s.y - ImGui::GetTextLineHeight() - 3.0f), (you ? "YOU - " : "") + m.Name + " - " + m.Role + (clanName.empty() ? std::string() : " (" + clanName + ")"), color);
			label(s, task, IM_COL32(235, 235, 235, 235));
		}
		}
	}

	void EditorApp::DrawTribePanel()
	{
		if (m_FocusTribe > 0) { ImGui::SetNextWindowFocus(); --m_FocusTribe; }
		if (ImGui::Begin("Tribe", &m_Panels.Tribe))
		{
			DrawClansSection();   // H13: the clans of the world, which one this panel shows, and what lies between them (EditorClans.cpp)
			if (ActiveTribe().Members().empty())
			{
				ImGui::TextWrapped("No tribe. Build one with: tribe build \"<model.fbx>\" 0 0 4");
				ImGui::End();
				return;
			}
			ImGui::Text("Time %.0f s   %s", ActiveTribe().Time(), !ActiveTribe().HasFire() ? "no campfire yet" : ActiveTribe().FireBurning() ? "campfire lit" : "campfire out (no wood)");
			ImGui::SameLine();
			ImGui::TextDisabled("  speed x%d", m_TimeScale);
			{
				int resting = 0, dead = 0;
				for (const TribeMember& m : ActiveTribe().Members()) { if (m.Dead) ++dead; else if (m.Job == "rest") ++resting; }
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("%zu members: %d resting, %d dead, %d abstract (far from the camera)", ActiveTribe().Members().size(), resting, dead, ActiveTribe().AbstractCount());
				ImGui::PopTextWrapPos();
			}
			ImGui::Checkbox("clan overlay", &m_ClanOverlay);
			ImGui::SameLine();
			ImGui::Checkbox("ai debug", &m_AiDebug);
			ImGui::SameLine();
			ImGui::Checkbox("nav debug", &m_NavDebug);

			if (ImGui::CollapsingHeader("Store", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const Inventory& store = ActiveTribe().Store();
				for (const std::string& id : m_Items.Ids())
				{
					const int n = store.Count(id);
					if (n == 0) continue;
					const ItemDef* def = m_Items.Find(id);
					char label[64];
					std::snprintf(label, sizeof label, "%s %d / %d", def ? def->Name.c_str() : id.c_str(), n, ActiveTribe().StoreCap());
					ImGui::ProgressBar(std::min(1.0f, static_cast<float>(n) / static_cast<float>(ActiveTribe().StoreCap())), ImVec2(-1.0f, 0.0f), label);
				}
				if (store.Stacks().empty()) ImGui::TextDisabled("empty");
			}

			if (ImGui::CollapsingHeader("Buildings (settlement)", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// H8: each site with its phase and what it lacks, each finished building with what it gives, and what the clan wants next
				std::stringstream lines(ActiveTribe().SettlementReport());
				for (std::string line; std::getline(lines, line);) ImGui::TextWrapped("%s", line.c_str());
			}

			if (ImGui::CollapsingHeader("Jobs (the clan's board)", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// H7: what the clan needs, from the most pressing, and who holds each job (a claim is a bonus on the member's options, not an order)
				const std::vector<const Job*> jobs = ActiveTribe().Board().Ordered();
				if (jobs.empty()) ImGui::TextDisabled("the clan needs nothing now");
				else if (ImGui::BeginTable("jobs", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthFixed, 120.0f); ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 52.0f); ImGui::TableSetupColumn("Held by and why");
					ImGui::TableHeadersRow();
					for (const Job* job : jobs)
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::Text("%s x%.0f", job->Id.c_str(), static_cast<double>(job->Amount));
						ImGui::TableNextColumn(); ImGui::Text("%.2f", static_cast<double>(job->Priority));
						ImGui::TableNextColumn();
						std::string who;
						for (const JobClaim& c : job->Claims) who += (who.empty() ? "" : ", ") + c.Member;
						if (who.empty()) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "nobody (0/%d)", job->Slots);
						else ImGui::TextWrapped("%s (%zu/%d)", who.c_str(), job->Claims.size(), job->Slots);
						if (!job->Why.empty()) ImGui::TextDisabled("%s", job->Why.c_str());
					}
					ImGui::EndTable();
				}
			}

			// H11: what the clan knows of the technologies, and what is not known yet (and who is half-way to it)
			if (ActiveTribe().Knowledge().Gated() && ImGui::CollapsingHeader("Knowledge", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const ClanKnowledge& k = ActiveTribe().Knowledge();
				for (const auto& [id, t] : m_Techs.Techs)
				{
					int knowers = 0;
					float best = 0.0f;
					for (const auto& [name, a] : k.Agents())
					{
						if (a.Known.count(id)) ++knowers;
						else if (const auto p = a.Progress.find(id); p != a.Progress.end()) best = std::max(best, p->second);
					}
					if (k.Clan().count(id)) ImGui::Text("%s  (era %d): known by the clan, %d member%s", t.Name.c_str(), t.Era, knowers, knowers == 1 ? "" : "s");
					else if (knowers > 0) ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1.0f), "%s: %d know%s it, not shown at the camp yet", t.Name.c_str(), knowers, knowers == 1 ? "s" : "");
					else if (best > 0.0f)
					{
						char label[96];
						std::snprintf(label, sizeof label, "%s: being learnt %d%%", t.Name.c_str(), static_cast<int>(best * 100.0f));
						ImGui::ProgressBar(best, ImVec2(-1.0f, 0.0f), label);
					}
					else ImGui::TextDisabled("%s: unknown", t.Name.c_str());
				}
			}

			// H9 + H10: how many live, are born and die, the norms of the clan and its traditions
			if (ImGui::CollapsingHeader("Society"))
			{
				const Society& people = ActiveTribe().People();
				const SocietyRules& rules = ActiveTribe().Config.Society;
				int children = 0, elders = 0;
				for (const auto& [name, p] : people.People())
					if (!p.Dead) { const Society::Stage s = people.StageOf(rules, name); children += s == Society::Stage::Child; elders += s == Society::Stage::Elder; }
				ImGui::Text("Day %d: %d alive (%d children, %d elders), %d born, %d died, %zu bonds", people.Days(), people.Alive(), children, elders, people.Births(), people.Deaths(), people.BondCount());
				for (const auto& [id, n] : rules.Culture.Norms)
				{
					const float v = people.Norm(rules, id);
					ImGui::ProgressBar(v, ImVec2(-1.0f, 0.0f), (id + " " + std::to_string(static_cast<int>(v * 100.0f))).c_str());
				}
				if (people.Traditions().empty()) ImGui::TextDisabled("no traditions yet");
				for (const std::string& id : people.Traditions())
					if (const auto t = rules.Culture.Traditions.find(id); t != rules.Culture.Traditions.end()) ImGui::Text("Tradition: %s (from %s)", t->second.Name.c_str(), t->second.Event.c_str());
			}

			// H18: the economy (production chains, spoilage, stores, hauling) and the fields, one line each (the same text as `economy status`, `field status`)
			if (ImGui::CollapsingHeader("Economy and fields"))
			{
				auto lines = [](const std::string& text)
				{
					size_t from = 0;
					while (from < text.size())
					{
						const size_t to = text.find('\n', from);
						const std::string line = text.substr(from, to == std::string::npos ? std::string::npos : to - from);
						if (!line.empty()) ImGui::TextWrapped("%s", line.c_str());
						if (to == std::string::npos) break;
						from = to + 1;
					}
				};
				lines(ActiveTribe().EconomyReport());
				ImGui::Separator();
				lines(ActiveTribe().FieldReport());
			}

			DrawSocialSection();   // social life: moods, talks, rites, beliefs, hunting (EditorSocial.cpp)

			// H14: threats, fights, the wounded, hunts and the raid (the same text as `combat status` for the clan)
			if (ImGui::CollapsingHeader("Conflict"))
			{
				const std::string text = ActiveTribe().ConflictReport();
				size_t from = 0;
				while (from < text.size())
				{
					const size_t to = text.find('\n', from);
					const std::string line = text.substr(from, to == std::string::npos ? std::string::npos : to - from);
					if (!line.empty()) ImGui::TextWrapped("%s", line.c_str());
					if (to == std::string::npos) break;
					from = to + 1;
				}
			}


			if (ImGui::CollapsingHeader("Members", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (ImGui::BeginTable("members", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 44.0f); ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 74.0f); ImGui::TableSetupColumn("Doing and why");   // (long texts wrap in the stretch columns instead of running over the next one)
					ImGui::TableHeadersRow();
					for (const TribeMember& m : ActiveTribe().Members())
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						if (ImGui::Selectable(m.Name.c_str(), m.Name == m_AbilityActor, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
							SelectActor(m.Name, ImGui::IsMouseDoubleClicked(0));
						ImGui::TableNextColumn(); ImGui::TextUnformatted(m.Role.c_str());
						if (const Job* held = ActiveTribe().Board().ClaimOf(m.Name)) ImGui::TextDisabled("%s", held->Id.c_str());
						ImGui::TableNextColumn();
						if (m.Dead) ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.25f, 1.0f), "dead (%s)", m.Cause.c_str());
						else if (m.Job.empty()) ImGui::TextDisabled("deciding");
						else if (m.Job == "rest") ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "resting");
						else ImGui::TextWrapped("%s %s", m.Job.c_str(), m.Detail.c_str());
						if (m.Abstract) { ImGui::SameLine(); ImGui::TextDisabled("(abstract)"); }
						if (!m.Dead)
						{
							const std::string why = m.Job == "rest" && !m.Why.empty() ? m.Why : ActiveTribe().Because(m);
							ImGui::TextDisabled("%s", why.c_str());
							const std::map<std::string, float> needs = ActiveTribe().NeedLevels(m);
							const char* lowest = nullptr;
							float lowestValue = 100.0f;
							for (const char* need : { NeedName::Hunger, NeedName::Thirst, NeedName::Rest, NeedName::Warmth, NeedName::Safety, NeedName::Company })
							{
								const auto it = needs.find(need);
								if (it != needs.end() && it->second < lowestValue) { lowestValue = it->second; lowest = need; }
							}
							if (lowest && lowestValue < 60.0f) ImGui::TextColored(NeedColor(lowestValue), "%s %.0f", lowest, static_cast<double>(lowestValue));
						}
					}
					ImGui::EndTable();
				}
				ImGui::TextDisabled("Click a name to select it (double click: go to it). You can also click a character in the viewport.");
			}

			for (const TribeMember& m : ActiveTribe().Members())
				if (m.Name == m_AbilityActor && ImGui::CollapsingHeader(("Selected: " + m.Name).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Text("%s, %d carried to the store, %d made, %d meals%s", m.Role.c_str(), m.Deposited, m.Made, m.Meals, m.Hungry ? ", hungry" : "");
					if (!m.Why.empty()) ImGui::TextWrapped("Resting because: %s", m.Why.c_str());
					if (const Job* held = ActiveTribe().Board().ClaimOf(m.Name)) ImGui::TextWrapped("Job: %s (%s)", held->Id.c_str(), held->Why.c_str());
					if (m.Skills.empty()) ImGui::TextDisabled("No skills yet: they grow with practice");
					for (const auto& [family, level] : m.Skills)   // H7: its aptitudes (0 to the max of the data), the one of its trade marked
					{
						char label[64];
						const auto role = ActiveTribe().Config.Roles.find(m.Role);
						const bool trade = role != ActiveTribe().Config.Roles.end() && role->second.Skill == family;
						std::snprintf(label, sizeof label, "%s %.0f%s", family.c_str(), static_cast<double>(level), trade ? " (its trade)" : "");
						ImGui::ProgressBar(std::clamp(level / std::max(ActiveTribe().Config.Work.Skills.Max, 1.0f), 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), label);
					}
					if (Actor* actor = m_Actors.Find(m.Name)) ImGui::TextWrapped("Orders: %s", actor->TaskStatus().c_str());
					DrawNpcInspector(m);
					DrawBag(m.Name);
				}

			if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const auto& log = ActiveTribe().Log();
				ImGui::BeginChild("tribelog", ImVec2(0.0f, 160.0f), true);
				for (size_t i = log.size() > 80 ? log.size() - 80 : 0; i < log.size(); ++i) ImGui::TextWrapped("%s", log[i].c_str());
				if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
				ImGui::EndChild();
			}
		}
		ImGui::End();
	}

	void EditorApp::DrawAbilitiesPanel()
	{
		if (m_FocusAbilities > 0) { ImGui::SetNextWindowFocus(); --m_FocusAbilities; }
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
	CommandResult EditorApp::TelemetryCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		auto status = [&]
		{
			if (!m_Telemetry.Active()) return std::string("the telemetry is off (telemetry start <file.db>)");
			const TelemetryStats s = m_Telemetry.Stats();
			return "the telemetry writes to " + m_Telemetry.Path() + " (run " + std::to_string(m_Telemetry.RunId()) + "): " + std::to_string(s.SamplesWritten) + " samples, "
				+ std::to_string(s.EventsWritten) + " events, " + std::to_string(s.SamplesDropped) + " samples dropped, " + std::to_string(s.WriteErrors) + " write errors";
		};
		if (sub == "status") return { true, status() };
		if (sub == "stop")
		{
			if (!m_Telemetry.Active()) return { true, status() };
			m_Clans.SetTelemetry(nullptr);
			const std::string path = m_Telemetry.Path();
			m_Telemetry.Stop(m_Clans.Time());
			return { true, "telemetry stopped: " + path };
		}
		if (sub == "start")
		{
			if (a.size() < 2) return { false, "telemetry start <file.db> [every seconds]" };
			double every = 60.0;
			if (a.size() > 2) try { every = std::stod(a[2]); } catch (...) { return { false, "every wants a number of seconds" }; }
			TelemetryRunInfo info;
			info.Seed = static_cast<int64_t>(m_WorldDesc.Seed);
			info.Note = "editor";
			const std::string why = m_Telemetry.Start(a[1], info);
			if (!why.empty()) return { false, why };
			m_Clans.SetTelemetry(&m_Telemetry, every);
			return { true, "telemetry started: " + a[1] + ", a row per member and clan every " + std::to_string(static_cast<int>(every)) + " simulation seconds" };
		}
		return { false, "telemetry start <file.db> [every seconds] | stop | status" };
	}
}
