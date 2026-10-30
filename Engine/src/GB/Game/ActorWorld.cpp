#include <set>
#include "GB/Game/ActorWorld.h"
#include "GB/Assets/AssetPath.h"
#include "GB/Log.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace GB
{
	std::shared_ptr<Model> ActorWorld::LoadModel(const std::string& path, std::string& error)
	{
		namespace fs = std::filesystem;
		const std::string resolved = ResolveAssetPath(path, m_AssetRoot);
		const auto found = m_Models.find(resolved);
		if (found != m_Models.end()) return found->second;
		std::string ext = fs::path(resolved).extension().string();
		for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		std::shared_ptr<Model> model;
		if (ext == ".fbx")
			model = Actor::LoadCharacter(resolved, m_AssetRoot, error);
		else
		{
			model = Model::Load(resolved);
			if (!model) error = "could not load the model " + resolved;
		}
		if (model) m_Models[resolved] = model;
		return model;
	}

	std::vector<CircleObstacle> ActorWorld::CirclesExcept(const std::string& except) const
	{
		std::vector<CircleObstacle> out;
		out.reserve(m_Actors.size());
		for (const auto& [name, actor] : m_Actors)
			if (name != except)
			{
				const glm::vec3 p = actor->GetPosition();
				out.push_back({ { p.x, p.z }, actor->Radius() });
			}
		return out;
	}

	bool ActorWorld::SyncNavigation(const NavConfig& config, std::string* error)
	{
		const CharacterWorld& world = m_Host.World;
		// the actors collide with what the host says is there now (a tree felled, a door shut), not with what there was when they were made
		bool same = m_PushedObstacles.size() == world.Obstacles.size();
		for (size_t i = 0; same && i < world.Obstacles.size(); ++i)
			same = m_PushedObstacles[i].Center == world.Obstacles[i].Center && m_PushedObstacles[i].Radius == world.Obstacles[i].Radius;
		if (!same)
		{
			m_PushedObstacles = world.Obstacles;
			for (auto& [name, actor] : m_Actors) actor->SetStaticObstacles(world.Obstacles);
		}
		if (!m_Nav.Ready() || !m_Nav.Matches(world))
		{
			if (!m_Nav.Build(world, config, error))
			{
				for (auto& [name, actor] : m_Actors) actor->SetNavigation(nullptr);
				return false;
			}
		}
		else m_Nav.SyncObstacles(world.Obstacles);
		for (auto& [name, actor] : m_Actors) actor->SetNavigation(&m_Nav);
		return true;
	}

	Actor* ActorWorld::Spawn(const std::string& name, const ActorSpawn& spawn, std::string& error)
	{
		if (name.empty()) { error = "an actor needs a name"; return nullptr; }
		if (m_Actors.count(name)) { error = "there is already an actor called '" + name + "'"; return nullptr; }
		std::shared_ptr<Model> model = spawn.Shared;
		if (!model && !spawn.NoModel)
		{
			if (spawn.Path.empty()) { error = "no model for the actor"; return nullptr; }
			model = LoadModel(spawn.Path, error);
			if (!model) return nullptr;
		}
		ActorSpec spec;
		spec.Name = name;
		spec.Height = spawn.Height;
		spec.Rigged = spawn.Rigged;
		spec.Manifest = &m_Manifest;
		spec.Bindings = m_Bindings;
		spec.Skeleton = SkeletonKey(spawn.Kind.empty() ? spawn.Path : spawn.Kind);
		spec.Start = spawn.Position;
		spec.StartYaw = spawn.Yaw;
		spec.Others = CirclesExcept("");
		auto actor = std::make_unique<Actor>();
		ActorHost host = m_Host;
		if (m_Nav.Ready()) host.World.Nav = &m_Nav;
		if (!actor->Init(model, host, spec, error)) return nullptr;
		if (spawn.CreateEntity && m_Scene && m_Scene->FindByName(name) == NullEntity)
		{
			m_Scene->Find(m_Scene->Create(name))->Gameplay.emplace();
			if (m_Gameplay && !m_Gameplay->Find(name)) m_Gameplay->Create(name);
		}
		actor->SetAbilities(m_Gameplay, name);   // the ability system of that name, if the scene has one
		actor->SetKind(spawn.Kind);
		Actor* raw = actor.get();
		GBObjectContext context;
		context.World = this;
		raw->OnSpawnStart(context);
		raw->OnSpawnEnd();
		raw->SetHandle(++m_NextHandle);
		m_Actors[name] = std::move(actor);   // published
		raw->BeginPlay();
		raw->PrepareAbilities();
		if (m_Gameplay) m_AbilityRevision = m_Gameplay->Library.Revision();
		return raw;
	}

	int ActorWorld::SetBindings(std::shared_ptr<const BindingsSetup> setup)
	{
		m_Bindings = std::move(setup);
		int changed = 0;
		for (auto& [name, actor] : m_Actors) changed += actor->SetBindings(m_Bindings, actor->Skeleton()) ? 1 : 0;
		if (m_Gameplay) m_AbilityRevision = m_Gameplay->Library.Revision();
		return changed;
	}

	std::vector<Save::ActorState> ActorWorld::CaptureActors() const
	{
		std::vector<Save::ActorState> out;
		for (const auto& [name, actor] : m_Actors)
		{
			Save::ActorState s;
			actor->CaptureState(s);
			out.push_back(std::move(s));
		}
		return out;
	}

	std::string ActorWorld::RestoreActors(const std::vector<Save::ActorState>& actors, bool check)
	{
		if (check) return {};   // nothing here can make a load fail: an actor that does not exist is skipped
		// The saved game is the whole truth about who is there: an actor made after it was taken goes (a replay starts from a save, and its orders spawn that actor again).
		std::set<std::string> saved;
		for (const Save::ActorState& s : actors) saved.insert(s.Name);
		for (const std::string& name : Names())
			if (!saved.count(name)) Remove(name);
		for (const Save::ActorState& s : actors)
		{
			Actor* actor = Find(s.Name);
			if (!actor) continue;
			actor->RestoreState(s);
		}
		return {};
	}

	void ActorWorld::SetSuspended(const std::string& name, bool suspended)
	{
		Actor* actor = Find(name);
		if (!actor) return;
		if (suspended) actor->OnPause(); else actor->OnResume();
	}

	bool ActorWorld::Remove(const std::string& name)
	{
		const auto it = m_Actors.find(name);
		if (it == m_Actors.end()) return false;
		it->second->EndPlay();
		it->second->OnDestroy();
		m_Actors.erase(it);   // the actor lets go of its ability system and its ragdoll
		return true;
	}

	void ActorWorld::Clear()
	{
		for (auto& [name, actor] : m_Actors) { actor->EndPlay(); actor->OnDestroy(); }
		m_Actors.clear();
		m_Models.clear();
	}

	Actor* ActorWorld::Find(const std::string& name)
	{
		const auto it = m_Actors.find(name);
		return it == m_Actors.end() ? nullptr : it->second.get();
	}

	const Actor* ActorWorld::Find(const std::string& name) const
	{
		const auto it = m_Actors.find(name);
		return it == m_Actors.end() ? nullptr : it->second.get();
	}

	std::vector<std::string> ActorWorld::Names() const
	{
		std::vector<std::string> out;
		for (const auto& [name, actor] : m_Actors) out.push_back(name);
		return out;
	}

	std::string ActorWorld::Describe() const
	{
		std::ostringstream out;
		for (const auto& [name, actor] : m_Actors) out << actor->Describe() << "\n";
		return out.str();
	}

	EntityId ActorWorld::EntityOf(const std::string& name) const
	{
		return m_Scene ? m_Scene->FindByName(name) : NullEntity;
	}

	int ActorWorld::SyncEntities()
	{
		if (!m_Scene) return 0;
		int n = 0;
		for (const auto& [name, actor] : m_Actors)
			if (Entity* e = m_Scene->Find(m_Scene->FindByName(name)))
			{
				e->Transform.Position = actor->GetPosition();
				e->Transform.Rotation = { 0.0f, glm::degrees(actor->GetYaw()), 0.0f };
				++n;
			}
		return n;
	}

	void ActorWorld::PrepareAbilities()
	{
		// the rules registered the abilities again (a combat rules reload writes their montage lists anew): the characters put their own back
		if (m_Gameplay && m_Gameplay->Library.Revision() != m_AbilityRevision)
		{
			for (auto& [name, actor] : m_Actors) actor->SyncAbilities();
			m_AbilityRevision = m_Gameplay->Library.Revision();
		}
		for (auto& [name, actor] : m_Actors) if (!actor->Paused()) actor->PrepareAbilities();
	}

	void ActorWorld::Update(float dt)
	{
		m_Nav.NewFrame();
		struct Snapshot { std::string Name; CircleObstacle Circle; };
		std::vector<Snapshot> start;
		start.reserve(m_Actors.size());
		for (const auto& [name, actor] : m_Actors)
		{
			if (actor->Paused()) continue;
			const glm::vec3 p = actor->GetPosition();
			start.push_back({ name, { { p.x, p.z }, actor->Radius() } });
		}
		std::vector<CircleObstacle> others;
		// H16: the level of detail of the animation, from the distance to the viewer. The far ones are posed every few frames, staggered by their
		// place in the (name) order so the poses of a crowd spread over the frames instead of all landing on one.
		const auto t0 = std::chrono::steady_clock::now();
		m_Cost = FrameCost();
		++m_LodFrame;
		long slot = 0;
		for (auto& [name, actor] : m_Actors)
		{
			if (actor->Paused()) { ++m_Cost.Suspended; continue; }
			others.clear();
			for (const Snapshot& s : start)
				if (s.Name != name) others.push_back(s.Circle);
			actor->SetOthers(others);
			bool pose = true, ik = true;
			int level = 0;
			if (m_HasViewer && m_Lod.Enabled)
			{
				const glm::vec3 p = actor->GetPosition();
				const float d = glm::distance(glm::vec2(p.x, p.z), glm::vec2(m_Viewer.x, m_Viewer.z));
				if (d > m_Lod.FullRadius)
				{
					const bool far = d > m_Lod.ReducedRadius;
					const long every = std::max(1, far ? m_Lod.FarEvery : m_Lod.ReducedEvery);
					pose = (m_LodFrame + slot) % every == 0;
					ik = false;
					++(far ? m_Cost.Far : m_Cost.Reduced);
					level = far ? 2 : 1;
				}
				else ++m_Cost.Full;
			}
			else ++m_Cost.Full;
			++slot;
			m_Cost.Posed += pose ? 1 : 0;
			actor->SetAnimationDetail(pose, ik, level < 2);   // (the far ones without props)
			actor->SetAnimLevel(level);
			actor->Update(dt);
		}
		m_Cost.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}

	AnimLodConfig AnimLodForQuality(const std::string& preset)
	{
		AnimLodConfig c;   // medium
		if (preset == "minimum") { c.FullRadius = 12.0f; c.ReducedRadius = 30.0f; c.ReducedEvery = 2; c.FarEvery = 6; }
		else if (preset == "low") { c.FullRadius = 18.0f; c.ReducedRadius = 40.0f; c.ReducedEvery = 2; c.FarEvery = 4; }
		else if (preset == "high") { c.FullRadius = 35.0f; c.ReducedRadius = 70.0f; c.ReducedEvery = 1; c.FarEvery = 3; }
		else if (preset == "ultra") { c.FullRadius = 50.0f; c.ReducedRadius = 100.0f; c.ReducedEvery = 1; c.FarEvery = 2; }
		return c;
	}

	void ActorWorld::AddToScene(Scene& scene) const
	{
		for (const auto& [name, actor] : m_Actors) if (!actor->Paused()) actor->AddToScene(scene);
	}
}
