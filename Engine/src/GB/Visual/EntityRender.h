#pragma once
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Scene/Entity.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/ModelGpu.h"
#include "GB/Renderer/Renderer.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace GB
{
	// Turns the entities of an EntityWorld into scene content: primitives, glTF models and point lights.
	// Owns the GPU meshes and models they use (needs a GL context). Keep it alive while the scene is rendered.
	class EntityRenderer
	{
	public:
		explicit EntityRenderer(std::string assetDir = "") : m_AssetDir(std::move(assetDir)) {}

		// Appends the objects and lights of every active entity to `scene`. Returns how many entities were drawn.
		// `highlight` gets a faint glow (editor selection).
		size_t Build(const EntityWorld& world, Scene& scene, EntityId highlight = NullEntity);
		// Advances the animation of the models in the world.
		void Update(float deltaTime);

		// Draws the entities that carry a PropComponent (the host knows its trees and rocks). Returns false when
		// it could not draw it. `world` is the entity's world matrix.
		using PropBuilder = std::function<bool(const PropComponent& prop, const glm::mat4& world, Scene& scene)>;
		void SetPropBuilder(PropBuilder builder) { m_PropBuilder = std::move(builder); }
		// Props (trees, rocks) farther than `distance` from `origin` are not built at all (> 0; 0 = all of them): a map of kilometres has tens
		// of thousands, which the renderer would cull by distance anyway after copying each into the scene. Call once a frame with the camera.
		void SetPropCull(const glm::vec3& origin, float distance) { m_CullOrigin = origin; m_CullDistance = distance; }
		// Draws an entity by its data look (resource nodes, buildings, creatures: see GB/Visual/SceneVisuals.h). Called for every active entity
		// first; when it returns true the entity's own mesh and every descendant of it are not drawn (the primitive parts stay in the scene data
		// as the fallback a build without the visuals shows).
		using EntityVisual = std::function<bool(const EntityWorld& world, EntityId id, const glm::mat4& matrix, Scene& scene)>;
		void SetEntityVisual(EntityVisual visual) { m_EntityVisual = std::move(visual); }

		// The playing model of an entity (null when it has none or it failed to load), for animation control.
		ModelInstance* FindInstance(EntityId id);
		// Local bounding sphere (center, radius) of the entity's model; false when not loaded.
		bool ModelBounds(EntityId id, glm::vec3& center, float& radius);
		// Forgets cached instances and the lists Build walks. The lists need no call (Build lists again when EntityWorld::Structure changes, a
		// Clear or a move too: T53, T66); the playing models are kept by id, so a new world whose entity takes an old id and the same model
		// would get the old one's animation. Call it when the world is replaced by another (the editor's new scene, scene load and game
		// load do, through SceneEditor::OnWorldReplaced); an undo keeps them (the same ids are the same entities).
		void Reset() { m_Instances.clear(); m_ListedStructure = 0; }

		// Where the last Build's CPU time went (the editor's `drawcalls`): the data looks (nodes, buildings, creatures) and the walk of the
		// meshes, lights and props; how many look entities were read and how many drew, and how many entities the walk read.
		struct BuildStats
		{
			float LooksMs = 0.0f, WalkMs = 0.0f;
			uint32_t Looks = 0, LooksDrawn = 0, Walked = 0;
		};
		const BuildStats& LastBuild() const { return m_Stats; }

	private:
		const Mesh* PrimitiveMesh(MeshShape shape, float size);
		ModelInstance* InstanceFor(EntityId id, const std::string& path);

		std::string m_AssetDir;
		PropBuilder m_PropBuilder;
		glm::vec3 m_CullOrigin{ 0.0f };
		float m_CullDistance = 0.0f;
		EntityVisual m_EntityVisual;
		BuildStats m_Stats;
		// What Build walks, listed again when the world's structure changes (an entity added or removed or moved to another parent:
		// EntityWorld::Structure) and every couple of seconds besides (a component added without a new entity): the entities that may have a
		// look of their own, those with a mesh or a light, and the props in cells of kPropCell metres (a world of kilometres has tens of
		// thousands; only the cells within the cull distance are read). Each keeps its Entity's address, valid while the structure stays the
		// same (a std::map's nodes do not move), so the walk does not look the tens of thousands of ids up in the world's map every frame (T59).
		static constexpr float kPropCell = 64.0f;
		struct Listed
		{
			EntityId Id = NullEntity;
			const Entity* E = nullptr;
			const Entity* Parent = nullptr;   // (its parent's, null at the root)
			EntityId Look = NullEntity;   // (the meshes and lights: the nearest ancestor that may have a look; NullEntity: none)
		};
		void RefreshLists(const EntityWorld& world);
		uint64_t m_ListedStructure = 0;
		int m_ListedAge = 0;
		std::vector<Listed> m_Looks, m_Draws, m_Props;
		std::map<std::pair<int, int>, std::vector<Listed>> m_PropCells;
		// Build's scratch, kept between frames: the look entities that drew (or were culled with all they hold) this Build, by id (the Build's
		// number in their slot: nothing to clear), the entries the walk reads and an entity's chain of ancestors.
		std::vector<uint32_t> m_ClaimedAt;
		uint32_t m_BuildNumber = 0;
		std::vector<const Listed*> m_Walk;
		std::vector<const Entity*> m_Chain;
		std::map<std::pair<int, float>, std::unique_ptr<Mesh>> m_Primitives;
		std::map<std::string, std::unique_ptr<Model>> m_Models;   // null entry: failed to load (not retried)
		std::map<EntityId, std::pair<std::string, std::unique_ptr<ModelInstance>>> m_Instances;
	};
}
