#pragma once
#include "GB/Game/Actor.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	struct ActorSpawn
	{
		std::string Path;                  // an FBX character (relative to the assets, or absolute) with its folder's animations; empty: `Shared`
		std::shared_ptr<Model> Shared;     // or an already built model (a generated humanoid in a test)
		bool Rigged = true;
		float Height = 1.75f;
		glm::vec2 Position{ 0.0f };        // xz, moved to free dry ground (clear of the other actors too)
		float Yaw = 0.0f;
		bool CreateEntity = false;         // make the scene entity (with a gameplay component and its ability system) when there is none of that name
	};

	// The actors of a world: it creates, updates, draws and removes them, shares each loaded model between the actors that use it, keeps
	// them out of each other's way, and links every one to the entity of the scene with its name and to its ability system. Updating goes
	// in name order, so a run is the same every time.
	class ActorWorld
	{
	public:
		ActorWorld() = default;
		ActorWorld(const ActorWorld&) = delete;
		ActorWorld& operator=(const ActorWorld&) = delete;

		// What new actors get from the world. Existing actors keep the one they were built with: clear and respawn when the terrain changes.
		void SetHost(ActorHost host) { m_Host = std::move(host); }
		const ActorHost& Host() const { return m_Host; }
		void SetAssetRoot(const std::string& root) { m_AssetRoot = root; }
		// The scene and the abilities the actors are linked to (both optional, both must outlive the actors or be unset with nullptr).
		void Bind(EntityWorld* scene, GameplayWorld* gameplay) { m_Scene = scene; m_Gameplay = gameplay; }
		// The manifest new actors are built with (null: the built-in one).
		void SetManifest(const AnimManifest* manifest) { m_Manifest = manifest ? *manifest : MixamoManifest(); }

		// A model loaded once however many actors use it.
		std::shared_ptr<Model> LoadModel(const std::string& path, std::string& error);
		size_t CachedModels() const { return m_Models.size(); }

		// Creates the actor `name` (a clash is refused). Null and `error` when it cannot be built.
		Actor* Spawn(const std::string& name, const ActorSpawn& spawn, std::string& error);
		bool Remove(const std::string& name);
		void Clear();
		Actor* Find(const std::string& name);
		const Actor* Find(const std::string& name) const;
		std::vector<std::string> Names() const;
		size_t Count() const { return m_Actors.size(); }
		std::string Describe() const;

		// The entity an actor stands for: the one of its name (made, with a gameplay component, when the scene had none).
		EntityId EntityOf(const std::string& name) const;
		// Writes the actors' positions and headings into their entities.
		int SyncEntities();

		// Frame, in this order: the abilities are lent the characters, then every actor steps (each against the others' positions of the start
		// of the frame, so the order does not matter).
		void PrepareAbilities();
		void Update(float dt);
		void AddToScene(Scene& scene) const;

		// Circles of every actor except `except`.
		std::vector<CircleObstacle> CirclesExcept(const std::string& except) const;

	private:
		ActorHost m_Host;
		std::string m_AssetRoot;
		AnimManifest m_Manifest = MixamoManifest();
		EntityWorld* m_Scene = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		std::map<std::string, std::shared_ptr<Model>> m_Models;
		std::map<std::string, std::unique_ptr<Actor>> m_Actors;
	};
}
