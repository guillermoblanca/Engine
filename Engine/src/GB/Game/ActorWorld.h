#pragma once
#include "GB/Game/Actor.h"
#include "GB/Navigation/NavGrid.h"
#include "GB/Save/SaveWorld.h"
#include <map>
#include <set>
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
		std::string Kind;                  // what the caller asked for ("fox", a file...): saved with the actor so a loaded game can make it again
		bool NoModel = false;              // no path and no model on purpose: a logic-only actor (it moves, works and is saved; it is not drawn). Without it a missing model is an error
		bool CreateEntity = false;         // make the scene entity (with a gameplay component and its ability system) when there is none of that name
	};

	// Level of detail of the animation (H16): how far from the viewer an actor stops getting its full pose. Presentation only (see
	// AnimInstance::SetDetail): the movement, the abilities, their montages and notifies are the same at every level, so the game does not change.
	// Farther still the tribe makes a member abstract (TribeConfig::Performance.DetailRadius) and the actor is suspended.
	struct AnimLodConfig
	{
		bool Enabled = true;
		float FullRadius = 25.0f;      // m: closer than this, the full pose every frame (clips, foot IK, look-at, hands on props)
		float ReducedRadius = 50.0f;   // m: up to here no IK, and the pose every ReducedEvery frames; beyond, every FarEvery frames
		int ReducedEvery = 2;
		int FarEvery = 4;
		bool operator==(const AnimLodConfig&) const = default;
	};
	// The distances of each quality preset ("minimum", "low", "medium", "high", "ultra"; unknown: medium). The editor's `quality` sets it.
	AnimLodConfig AnimLodForQuality(const std::string& preset);

	// The actors of a world: it creates, updates, draws and removes them, shares each loaded model between the actors that use it, keeps
	// them out of each other's way, and links every one to the entity of the scene with its name and to its ability system. Updating goes
	// in name order, so a run is the same every time.
	class ActorWorld : public Save::IActorStateSource
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
		// The animation bindings (AnimBindings.h) of every character: those that exist now are rebuilt when their manifest changes with them, and the ones
		// made later are born with them. `setup->Only` limits them to the models with that SkeletonKey; null takes them all off. Returns how many characters
		// changed.
		int SetBindings(std::shared_ptr<const BindingsSetup> setup);
		const BindingsSetup* Bindings() const { return m_Bindings.get(); }
		// Every character puts its manifest's ability montages (and the notifies the abilities need) into the shared library again.
		void SyncAbilities() { for (auto& [name, actor] : m_Actors) actor->SyncAbilities(); if (m_Gameplay) m_AbilityRevision = m_Gameplay->Library.Revision(); }

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
		// Level of detail: a suspended actor is not animated, not drawn, not lent its abilities' runtime and not an obstacle to the others; its owner
		// (the tribe's abstract agent) moves it by hand and puts it back (Teleport) when it is resumed.
		void SetSuspended(const std::string& name, bool suspended);
		bool Suspended(const std::string& name) const { const Actor* a = Find(name); return a && a->Paused(); }
		// Level of detail of the animation (H16): from the viewer (the camera) and AnimLodConfig. Without a viewer every actor is at full detail.
		void SetViewer(const glm::vec3& pos) { m_Viewer = pos; m_HasViewer = true; }
		void ClearViewer() { m_HasViewer = false; }
		void SetAnimLod(const AnimLodConfig& lod) { m_Lod = lod; }
		const AnimLodConfig& AnimLod() const { return m_Lod; }
		// The level an actor was updated at in the last frame: 0 full, 1 reduced (no IK), 2 far (no IK, no props, posed every few frames), 3 suspended
		// (abstract) or unknown. TribeVisuals reads it to skip the hands reaching for things beyond full detail.
		int AnimLevel(const std::string& name) const { const Actor* a = Find(name); return !a ? 0 : a->Paused() ? 3 : a->AnimLevel(); }
		// What the last Update did (the perf line `stats ai`): actors at each level, suspended ones, poses computed and the wall time (measured only).
		struct FrameCost { int Full = 0, Reduced = 0, Far = 0, Suspended = 0, Posed = 0; double Ms = 0.0; };
		const FrameCost& LastCost() const { return m_Cost; }

		// Saving (see GB/Save): where each actor is, which way it faces and which locomotion set it moves with. Loading puts back the actors that exist
		// now; one the world does not have is left out (its model is not in the save yet: spawn it again with `actor spawn`).
		std::vector<Save::ActorState> CaptureActors() const override;
		std::string RestoreActors(const std::vector<Save::ActorState>& actors, bool check) override;

		// Navigation (H5): the grid the actors plan their paths over, built from the host's world (its ground, water and static obstacles). The first call
		// builds it; later ones only redo the cells around obstacles that appeared or went (a tree felled) and build again when the ground is not the same.
		// Every actor, those that exist and those spawned later, plans with it. False (and `error`) when the world has no bounds to cover.
		bool SyncNavigation(const NavConfig& config = NavConfig(), std::string* error = nullptr);
		NavGrid& Navigation() { return m_Nav; }
		const NavGrid& Navigation() const { return m_Nav; }

		// Circles of every actor except `except`.
		std::vector<CircleObstacle> CirclesExcept(const std::string& except) const;

	private:
		ActorHost m_Host;
		std::string m_AssetRoot;
		AnimManifest m_Manifest = MixamoManifest();
		std::shared_ptr<const BindingsSetup> m_Bindings;
		unsigned m_AbilityRevision = 0;   // GameplayLibrary::Revision the actors' ability montages were put there for
		EntityWorld* m_Scene = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		std::map<std::string, std::shared_ptr<Model>> m_Models;
		std::map<std::string, std::unique_ptr<Actor>> m_Actors;
		uint32_t m_NextHandle = 0;   // GBObject::Handle of the next actor: never reused by this world
		NavGrid m_Nav;
		std::vector<CircleObstacle> m_PushedObstacles;   // the static obstacles the actors have now
		glm::vec3 m_Viewer{ 0.0f };
		bool m_HasViewer = false;
		AnimLodConfig m_Lod;
		long m_LodFrame = 0;
		FrameCost m_Cost;
	};
}
