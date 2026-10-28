#include <set>
#include "GB/Game/ActorWorld.h"
#include "GB/Log.h"
#include <cctype>
#include <filesystem>
#include <sstream>

namespace GB
{
	std::shared_ptr<Model> ActorWorld::LoadModel(const std::string& path, std::string& error)
	{
		namespace fs = std::filesystem;
		std::string resolved = path;
		if (!fs::exists(resolved) && fs::exists(m_AssetRoot + "/" + path)) resolved = m_AssetRoot + "/" + path;
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
		if (!model)
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
		Actor* raw = actor.get();
		m_Actors[name] = std::move(actor);
		m_Kinds[name] = spawn.Kind;
		raw->PrepareAbilities();
		return raw;
	}

	std::vector<Save::ActorState> ActorWorld::CaptureActors() const
	{
		std::vector<Save::ActorState> out;
		for (const auto& [name, actor] : m_Actors)
		{
			Save::ActorState s;
			s.Name = name;
			if (const auto kind = m_Kinds.find(name); kind != m_Kinds.end()) s.Kind = kind->second;
			s.Position = actor->GetPosition();
			s.Yaw = actor->GetYaw();
			if (const AnimInstance* anim = actor->Anim()) s.Set = anim->Overlay();
			s.Props = actor->PropNames();
			s.Ragdoll = actor->Ragdolled();
			for (const ItemSlot& slot : actor->Bag().Stacks()) s.Inventory.push_back({ slot.Id, slot.Count });
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
			actor->Teleport(s.Position.x, s.Position.z, s.Yaw);
			std::vector<ItemSlot> bag;
			for (const Save::ItemStack& it : s.Inventory) bag.push_back({ it.Item, it.Count });
			actor->Bag().SetStacks(std::move(bag));
			if (!s.Set.empty() && actor->Anim() && actor->Anim()->HasSet(s.Set)) actor->SetOverlay(s.Set, 0.0f);
		}
		return {};
	}

	void ActorWorld::SetSuspended(const std::string& name, bool suspended)
	{
		if (suspended) m_Suspended.insert(name);
		else m_Suspended.erase(name);
	}

	bool ActorWorld::Remove(const std::string& name)
	{
		m_Suspended.erase(name);
		const auto it = m_Actors.find(name);
		if (it == m_Actors.end()) return false;
		m_Actors.erase(it);
		m_Kinds.erase(name);   // the actor lets go of its ability system and its ragdoll
		return true;
	}

	void ActorWorld::Clear()
	{
		m_Suspended.clear();
		m_Actors.clear();
		m_Kinds.clear();
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
		for (auto& [name, actor] : m_Actors) if (!m_Suspended.count(name)) actor->PrepareAbilities();
	}

	void ActorWorld::Update(float dt)
	{
		m_Nav.NewFrame();
		struct Snapshot { std::string Name; CircleObstacle Circle; };
		std::vector<Snapshot> start;
		start.reserve(m_Actors.size());
		for (const auto& [name, actor] : m_Actors)
		{
			if (m_Suspended.count(name)) continue;
			const glm::vec3 p = actor->GetPosition();
			start.push_back({ name, { { p.x, p.z }, actor->Radius() } });
		}
		std::vector<CircleObstacle> others;
		for (auto& [name, actor] : m_Actors)
		{
			if (m_Suspended.count(name)) continue;
			others.clear();
			for (const Snapshot& s : start)
				if (s.Name != name) others.push_back(s.Circle);
			actor->SetOthers(others);
			actor->Update(dt);
		}
	}

	void ActorWorld::AddToScene(Scene& scene) const
	{
		for (const auto& [name, actor] : m_Actors) if (!m_Suspended.count(name)) actor->AddToScene(scene);
	}
}
