#pragma once
#include "GB/Scene/MeshData.h"
#include "GB/Scene/Vertex.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	struct RockParams
	{
		uint32_t Seed = 1;
		float Size = 1.0f;                         // radius in meters before Stretch
		glm::vec3 Stretch{ 1.0f, 0.7f, 1.0f };     // per-axis scale: flat slabs, tall pillars
		float Roughness = 0.35f;                   // displacement amplitude (relative to the radius)
		float Frequency = 1.6f;                    // noise cycles over the rock: lumpy (1) to craggy (3)
		float Facets = 0.6f;                       // 0 = smooth pebble, 1 = flat-shaded cut faces
		float Flatten = 0.35f;                     // 0..1: how flat the base is (so it sits on the ground)
		float Strata = 0.5f;                       // bedding grooves (layers), 0..1
		float Cracks = 0.5f;                       // narrow cracks, 0..1
		float Erosion = 0.5f;                      // fine pitting and rounded edges, 0..1
		int Detail = 4;                            // icosphere subdivisions (4 = 5120 triangles)
		glm::vec3 Color{ 0.42f, 0.40f, 0.37f };
		// Surface layers (see Material): moss on top, sand at the base, wet band at the water line.
		float Moss = 0.0f;
		float Sand = 0.0f;
	};

	// A procedural rock on the CPU (no OpenGL, any thread): a noise-displaced icosphere with a flat base, and its measures. The procedural
	// grain/bump of its material is derived from the params by GB/Renderer/RocksGpu.h, which uploads it (docs/ARQUITECTURA_REVISION.md, H5).
	struct Rock
	{
		RockParams Params;
		MeshData Geometry;
		float Radius = 1.0f;                       // horizontal radius of the mesh around its axis
		float Height = 1.0f;                       // top of the mesh above its base
		size_t TriangleCount = 0;
	};

	class RockFactory
	{
	public:
		static std::unique_ptr<Rock> Create(const RockParams& params);
		// The geometry alone (vertices and indices).
		static void BuildGeometry(const RockParams& params, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
	};
}
