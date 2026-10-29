#pragma once
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Game/Entity.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Items.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/Texture.h"
#include "GB/Visual/VisualDefs.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace GB
{
	// The GPU side of the visual descriptions: meshes built from a model in a state (cached by model, variant and which copies show) and the
	// procedural textures (made once at load, at a quarter of the size with `lowRes`). Needs a GL context; keep it alive while the scene is drawn.
	class VisualCache
	{
	public:
		explicit VisualCache(const VisualLibrary& library) : m_Library(library) {}

		// Forgets every mesh and texture (after the library was reloaded, or the quality changed).
		void Clear() { m_Meshes.clear(); m_Textures.clear(); }
		void SetLowRes(bool lowRes) { if (lowRes != m_LowRes) { m_LowRes = lowRes; m_Textures.clear(); } }
		bool LowRes() const { return m_LowRes; }
		// The colour of the season's leaves: seasonal materials (crowns, bushes) multiply their colour by it.
		glm::vec3 Foliage{ 0.22f, 0.45f, 0.16f };

		// Adds the model to the scene at `world`, with the copies `visible` shows (VisibleCopies / BuildingModel). False for an unknown model.
		bool Add(const std::string& key, const VisualModel& model, const std::vector<bool>& visible, int variant, const glm::mat4& world, Scene& scene, float maxDistance = 0.0f);
		// A model of the library in a state (fraction, see VisualPart) or a building in a phase.
		bool AddModel(const std::string& model, float fraction, int variant, const glm::mat4& world, Scene& scene, float maxDistance = 0.0f);
		bool AddBuilding(const std::string& building, int phase, float progress, const glm::mat4& world, Scene& scene);
		Material MaterialFor(const std::string& id) const;
		const Texture2D* TextureFor(const std::string& kind) const;
		size_t CachedMeshes() const { return m_Meshes.size(); }
		size_t CachedTextures() const { return m_Textures.size(); }

	private:
		struct Entry
		{
			std::vector<std::pair<std::unique_ptr<Mesh>, std::string>> Pieces;   // mesh, material
			glm::vec3 Center{ 0.0f };
			float Radius = 0.0f;
		};
		const VisualLibrary& m_Library;
		bool m_LowRes = false;
		std::map<std::string, Entry> m_Meshes;
		mutable std::map<std::string, std::unique_ptr<Texture2D>> m_Textures;
	};

	// Draws the entities whose look is data: resource nodes (by the node's model, in the state of its Amount), buildings (H8's construction sites and
	// buildings: tag "Building.Kind.<id>" with the attributes Phase and Work, or the standalone "Building.<id>" with Phase and Progress of
	// BuildingVisuals.h) and creatures (gameplay tag "Animal.<x>": the fox as the animated Fox.glb, the
	// rabbit as a procedural body). It is the EntityRenderer's visual hook: an entity it draws is not drawn by its own primitives, nor are its
	// children, so the scene data (and the save) keep the primitive parts as they were and a build without the visuals still shows them.
	class SceneVisuals
	{
	public:
		explicit SceneVisuals(std::string assetDir = "") : m_AssetDir(std::move(assetDir)), m_Cache(Library) {}
		void SetAssetDir(std::string dir) { m_AssetDir = std::move(dir); }

		VisualLibrary Library;   // the live "visuals" section (the content packs load it; call Cache().Clear() after a reload)
		VisualCache& Cache() { return m_Cache; }
		bool Enabled = true;
		// H16: resource nodes farther than this from the camera are not drawn (0: no limit). A procedural node is up to ~2900 triangles; a forest of
		// them far away is the cost, not the look. The editor sets it from the quality preset.
		float NodeDrawDistance = 0.0f;

		// The node definitions (model and capacity of each kind); the attribute reader gives the live value of an entity's attribute (the gameplay
		// world's), without it the entity's own GameplayComponent is read.
		void SetNodes(const std::map<std::string, ResourceNodeDef>* nodes) { m_Nodes = nodes; }
		// H8's building definitions (phases and the work each takes): a site's progress is its phase plus the work done over the work needed, and
		// it is drawn with the visual phases of the same id at the same share of the whole build (H8's three shelter phases over the four visual ones).
		void SetBuildings(const std::map<std::string, BuildingDef>* buildings) { m_Buildings = buildings; }
		using AttributeReader = std::function<bool(const std::string& entity, const std::string& attribute, float& value)>;
		void SetAttributes(AttributeReader reader) { m_Read = std::move(reader); }

		// What an entity is drawn as (CPU, no GL): kind "node", "building" or "creature", the model and its state.
		struct Look
		{
			std::string Kind, Model;
			float Fraction = 1.0f;
			int Variant = 0;
			int Phase = 0;
			float Progress = 1.0f;
		};
		bool Describe(const Entity& entity, Look& look) const;

		// The EntityRenderer hook. True when it drew the entity.
		bool Draw(const EntityWorld& world, EntityId id, const glm::mat4& matrix, Scene& scene);
		// Advances the creatures' animations (and measures how fast each moved, for its clip).
		void Update(float dt);
		// Forgets the creatures' instances (the world was rebuilt).
		void Reset() { m_Creatures.clear(); }

	private:
		bool ReadAttribute(const Entity& e, const std::string& name, float& value) const;
		bool DrawCreature(const Entity& e, EntityId id, const CreatureVisual& c, const glm::mat4& matrix, Scene& scene);

		std::string m_AssetDir;
		VisualCache m_Cache;
		const std::map<std::string, ResourceNodeDef>* m_Nodes = nullptr;
		const std::map<std::string, BuildingDef>* m_Buildings = nullptr;
		AttributeReader m_Read;
		std::map<std::string, std::unique_ptr<Model>> m_Models;   // creature models by path (null: failed, not retried)
		struct Creature
		{
			std::unique_ptr<ModelInstance> Instance;
			glm::vec3 Last{ 0.0f };
			float Speed = 0.0f;
			std::string Clip;
			bool Seen = false;
		};
		std::map<EntityId, Creature> m_Creatures;
		float m_Dt = 0.0f;
	};
}
