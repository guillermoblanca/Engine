#pragma once
#include "GB/Scene/MeshData.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Procedural geometry on the CPU (no GL): the shapes the visual descriptions of resource nodes, props, buildings and creatures are made of
	// (see VisualDefs.h). Everything is deterministic: the same arguments (and seed) give the same vertices bit for bit on every run, so a test can
	// hash them and the GPU cache can key them by their arguments. UVs are in metres (a texture repeats once per metre at UVScale 1), so a long
	// log and a short stake show the same grain size. The mesh type itself, MeshData, is plain CPU data in GB/Scene/MeshData.h.
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
		// T103: a card for an alpha-masked texture (leaves, grass): a quad `width` wide and `height` tall standing on the origin in the XY
		// plane, facing +Z, its top bent `bend` metres toward +Z along a smooth curve. One-sided (draw it with a double-sided material).
		// Unlike the other shapes its UVs are not in metres: 0..1 over the card (u across, v up), so the whole texture is the card.
		MeshData Card(float width, float height, float bend = 0.0f, int segments = 3);
	}

	// Integer hashing for the procedural content: the same inputs give the same numbers everywhere (no std::rand, no floating point state).
	uint32_t VisualHash(uint32_t a, uint32_t b = 0x9E3779B9u, uint32_t c = 0x85EBCA6Bu);
	uint32_t VisualHash(const char* text, uint32_t seed = 0);
	// A number in [0, 1) from a hash.
	inline float HashUnit(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }
}
