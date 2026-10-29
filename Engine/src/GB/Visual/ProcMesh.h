#pragma once
#include "GB/Renderer/Mesh.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Procedural geometry on the CPU (no GL): the shapes the visual descriptions of resource nodes, props, buildings and creatures are made of
	// (see VisualDefs.h). Everything is deterministic: the same arguments (and seed) give the same vertices bit for bit on every run, so a test can
	// hash them and the GPU cache can key them by their arguments. UVs are in metres (a texture repeats once per metre at UVScale 1), so a long
	// log and a short stake show the same grain size.
	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;

		size_t Triangles() const { return Indices.size() / 3; }
		bool Empty() const { return Indices.empty(); }
		// Adds `other` moved by `transform` (normals and tangents follow; a mirroring transform flips the winding back).
		void Append(const MeshData& other, const glm::mat4& transform = glm::mat4(1.0f));
		// Tangents from the UVs (needed by normal mapping and the grain bump); the shape builders call it.
		void ComputeTangents();
		// Axis-aligned bounds of the vertices (zero when empty).
		void Bounds(glm::vec3& min, glm::vec3& max) const;
		// A 64-bit FNV hash of the positions, normals, UVs and indices: the determinism checks compare it.
		uint64_t Hash() const;
	};

	namespace ProcMesh
	{
		// A box of `size` centred on the origin. Each face is mapped in metres.
		MeshData Box(const glm::vec3& size);
		// A (tapered) cylinder standing on the origin: radius `bottom` at y = 0 and `top` at y = `height` (0 = a cone), `sides` around (3..48),
		// with caps when `caps`. `bend` leans the top sideways (+X, metres) along a smooth curve: a trunk that is not a pole.
		MeshData Cylinder(float bottom, float top, float height, int sides, bool caps = true, float bend = 0.0f);
		// A lumpy ball of radius 1 centred on the origin, scaled by `scale`: a subdivided icosahedron (`detail` 0..4) pushed in and out by
		// smooth noise of amplitude `roughness` (0 = a sphere). Crowns of trees, bushes, rocks, clay mounds, berries, bodies of animals.
		// `flatBase` > -1 flattens everything below that height (before scaling) so a rock sits on the ground.
		MeshData Blob(const glm::vec3& scale, int detail, float roughness, uint32_t seed, float flatBase = -2.0f);
		// A thin double-sided blade (grass, reed, straw): a quad strip `width` wide at the base narrowing to a point, `height` tall, bent `lean`
		// metres along +X at the tip.
		MeshData Blade(float width, float height, float lean, int segments = 3);
	}

	// Integer hashing for the procedural content: the same inputs give the same numbers everywhere (no std::rand, no floating point state).
	uint32_t VisualHash(uint32_t a, uint32_t b = 0x9E3779B9u, uint32_t c = 0x85EBCA6Bu);
	uint32_t VisualHash(const char* text, uint32_t seed = 0);
	// A number in [0, 1) from a hash.
	inline float HashUnit(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }
}
