#pragma once
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Game/Entity.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
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

	private:
		const Mesh* PrimitiveMesh(MeshShape shape, float size);
		ModelInstance* InstanceFor(EntityId id, const std::string& path);

		std::string m_AssetDir;
		std::map<std::pair<int, float>, std::unique_ptr<Mesh>> m_Primitives;
		std::map<std::string, std::unique_ptr<Model>> m_Models;   // null entry: failed to load (not retried)
		std::map<EntityId, std::pair<std::string, std::unique_ptr<ModelInstance>>> m_Instances;
	};
}
