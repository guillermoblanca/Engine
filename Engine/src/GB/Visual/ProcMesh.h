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
		// T106: a curved card, the same mapping and facing as Card (UVs 0..1, u across, v up, front +Z, the base row on the origin) but a
		// real 3D sheet that catches the light differently across it: a grid of `segmentsAcross` (made even, 2..8, so a column of vertices lies
		// on the midrib at u = 0.5) by `segmentsAlong` (1..16) quads.
		//   bend    metres the top moves toward +Z (negative: away, a droop when the card leans back), along t^2 as Card
		//   fold    metres both side edges rise toward +Z over the midrib (a soft V along the central vein; negative folds the other way)
		//   twist   radians the cross-section turns around the spine at the top (0 at the base, growing linearly)
		//   jitter  metres of per-vertex noise along the sheet's normal (0 on the base row, so the card stays anchored), from `seed`
		// Normals are the area-weighted sum of the faces' (so they follow the fold, bend and twist), tangents from the UVs.
		MeshData CurvedCard(float width, float height, int segmentsAcross = 4, int segmentsAlong = 4, float bend = 0.0f, float fold = 0.0f,
		                    float twist = 0.0f, float jitter = 0.0f, uint32_t seed = 0);
		// T106: plants assembled from curved cards (one alpha-masked material over the whole mesh, drawn double-sided), standing on the origin.
		// A bush: three upright crossed cards in the core and `cards` more around it, leaning out and up at varied angles and heights, their
		// tips drooping, every one folded and bent differently (by `seed`). `cardWidth` x `cardHeight` is the size of a card (the leaf_small
		// or leaf_broad texture).
		MeshData CardShrub(float cardWidth, float cardHeight, int cards, uint32_t seed);
		// A young conifer `height` tall and `radius` wide at the bottom, of leaf_needle cards: `tiers` whorls stepping up the axis, six
		// folded sprays a whorl leaning out about 50 degrees (shorter toward the top, drooping at the tips), and a leader of two crossed upright cards.
		MeshData CardConifer(float height, float radius, int tiers, uint32_t seed);
		// A tuft of grass: `cards` crossed cards (the grass_* textures) `width` x `height`, each arched outward with a crease down the middle.
		MeshData CardTuft(float width, float height, int cards, uint32_t seed);
	}

	// Integer hashing for the procedural content: the same inputs give the same numbers everywhere (no std::rand, no floating point state).
	uint32_t VisualHash(uint32_t a, uint32_t b = 0x9E3779B9u, uint32_t c = 0x85EBCA6Bu);
	uint32_t VisualHash(const char* text, uint32_t seed = 0);
	// A number in [0, 1) from a hash.
	inline float HashUnit(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }
}
