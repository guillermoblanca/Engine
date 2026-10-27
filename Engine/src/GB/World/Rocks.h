#pragma once
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
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

	// A procedural rock: noise-displaced icosphere, flat base, procedural grain/bump in the material.
	class Rock
	{
	public:
		std::unique_ptr<Mesh> MeshPtr;
		Material Mat;
		float Radius = 1.0f;                       // bounding radius of the mesh
		float Height = 1.0f;                       // top of the mesh above its base
		size_t TriangleCount = 0;
		RockParams Params;

		// Places an instance with its base on `position` (sunk by `sink` meters). `waterLine` (world y) wets the
		// lower part when the rock stands in water.
		void AddToScene(Scene& scene, const glm::vec3& position, float yaw, float scale = 1.0f, float sink = 0.1f, float waterLine = -1e9f) const;
	};

	class RockFactory
	{
	public:
		static std::unique_ptr<Rock> Create(const RockParams& params);
		// The CPU part alone (vertices and indices), usable without an OpenGL context.
		static void BuildGeometry(const RockParams& params, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
	};
}
