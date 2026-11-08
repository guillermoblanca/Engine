#pragma once
#include "GB/Renderer/Material.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Scene.h"
#include "GB/World/Rocks.h"
#include <cstddef>
#include <glm/glm.hpp>
#include <memory>

namespace GB
{
	// One rock variant on the GPU: the mesh of a GB::Rock (GB/World/Rocks.h, pure CPU) and its material (procedural grain/bump, moss and sand
	// layers), ready to be placed in a Scene any number of times. The rocks do not know it exists: the render reads the world, never the other
	// way round (docs/ARQUITECTURA_REVISION.md, H5). Needs an OpenGL context; owned by whoever draws the rocks (the editor's WorldInstance, the
	// Sandbox, the tests).
	class RockGpu
	{
	public:
		std::unique_ptr<Mesh> MeshPtr;
		Material Mat;
		float Radius = 1.0f;                       // the measures of the Rock it was made from
		float Height = 1.0f;
		size_t TriangleCount = 0;
		RockParams Params;

		// Places an instance with its base on `position` (sunk by `sink` meters). `waterLine` (world y) wets the
		// lower part when the rock stands in water.
		void AddToScene(Scene& scene, const glm::vec3& position, float yaw, float scale = 1.0f, float sink = 0.1f, float waterLine = -1e9f) const;
	};

	class RocksGpu
	{
	public:
		// Uploads a rock's mesh and gives it its material (derived from rock.Params).
		static std::unique_ptr<RockGpu> Upload(const Rock& rock);
		// RockFactory::Create and Upload in one go.
		static std::unique_ptr<RockGpu> Create(const RockParams& params) { return Upload(*RockFactory::Create(params)); }
	};
}
