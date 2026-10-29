#pragma once
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Game/Entity.h"
#include "GB/Renderer/Mesh.h"
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
		// Draws an entity by its data look (resource nodes, buildings, creatures: see GB/Visual/SceneVisuals.h). Called for every active entity
		// first; when it returns true the entity's own mesh and every descendant of it are not drawn (the primitive parts stay in the scene data
		// as the fallback a build without the visuals shows).
		using EntityVisual = std::function<bool(const EntityWorld& world, EntityId id, const glm::mat4& matrix, Scene& scene)>;
		void SetEntityVisual(EntityVisual visual) { m_EntityVisual = std::move(visual); }

		// The playing model of an entity (null when it has none or it failed to load), for animation control.
		ModelInstance* FindInstance(EntityId id);
		// Local bounding sphere (center, radius) of the entity's model; false when not loaded.
		bool ModelBounds(EntityId id, glm::vec3& center, float& radius);
		// Forgets cached instances (ids were reused, e.g. after EntityWorld::Clear()).
		void Reset() { m_Instances.clear(); }

	private:
		const Mesh* PrimitiveMesh(MeshShape shape, float size);
		ModelInstance* InstanceFor(EntityId id, const std::string& path);

		std::string m_AssetDir;
		PropBuilder m_PropBuilder;
		EntityVisual m_EntityVisual;
		std::map<std::pair<int, float>, std::unique_ptr<Mesh>> m_Primitives;
		std::map<std::string, std::unique_ptr<Model>> m_Models;   // null entry: failed to load (not retried)
		std::map<EntityId, std::pair<std::string, std::unique_ptr<ModelInstance>>> m_Instances;
	};
}
