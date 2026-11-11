#pragma once
#include "GB/World/Heightmap.h"
#include "GB/World/Water/WaterField.h"
#include "GB/World/Water/WaterPresets.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Every lake of a WaterField as one triangle mesh, for one draw (T105, TERRENO_V5 12.4). A lake whose shore is a contour line (lake_fill)
	// does not fit in a disc: a disc drawn at its level shows water over the lower ground past its outlet, over a neighbouring valley and over
	// a lower lake. This mesh lies on the samples of the terrain grid instead, so the shore is where the terrain's depth cuts the flat water.
	// Plain CPU data, no OpenGL, deterministic: the renderer uploads it (Renderer/WaterRenderer, the other session's) and a test measures it.
	//
	// The rule, per triangle of the terrain grid (the same split as Terrain.cpp's mesh, counter-clockwise seen from +Y):
	//   - the lake's cells are the samples it covers (LakeBody::Covers: its Mask, or the outline of its Radius for the main lake), the first lake
	//     of WaterField::Lakes() that covers a sample owning it, as WaterRaster does;
	//   - a triangle is drawn when every corner is a cell of the lake or a skirt sample: within SkirtCells (Chebyshev) of a cell, with the ground
	//     at or above the lake's level; and at least one corner is within SkirtCells - 1 of a cell (with 1: one corner is a cell).
	// So the skirt is under the terrain (the ground is linear on the triangle and at or above the water at every non-lake corner), and where
	// the ground past the shore falls below the level (an outlet, a lower valley, a lower lake) there is no triangle: no water over dry ground.
	//
	// The contract with the renderer:
	//   - LakeVertex: world position with y = the lake's level, the normal up, the lake's index (Bodies[Lake]), the water's depth and the
	//     distance to the shore (T139, for the shader's colour, clarity and shore foam); the wave UVs come from xz. Nine tightly packed
	//     32-bit values, stride 36:
	//       offset  0  vec3  Position
	//       offset 12  vec3  Normal         (0, 1, 0)
	//       offset 24  uint  Lake
	//       offset 28  float Depth          m: the lake's level minus the ground at that sample, signed and not clamped. Positive in the water;
	//                                       0 or negative on the skirt (its ground is at or above the level: the water is under the terrain
	//                                       there, so the value says how far under). A cell of a disc lake (the main one, Covers by its
	//                                       outline) can be on ground above its level too: negative. The shader clamps (e.g. saturate as alpha).
	//       offset 32  float ShoreDistance  m: the exact Euclidean distance (Felzenszwalb-Huttenlocher transform, in samples, times Spacing)
	//                                       from a cell to the nearest sample of the grid that is not a cell of this lake (dry ground, the
	//                                       skirt, or a cell owned by another lake). The samples past the grid's edge count as shore. So
	//                                       the first ring of cells is at 1 * Spacing (diagonal: sqrt(2) * Spacing), and the skirt is 0;
	//                                       interpolated on a shore triangle, the value crosses ~0.5 * Spacing where the shore is.
	//     No flow field: the outlet is a point (LakeBody::Outlet), and a straight pull toward it crosses the land of a lake that is not convex.
	//   - Bodies is parallel to WaterField::Lakes(): index 0 is the main lake when there is one, so a lake's index is not its MoreLakes() index;
	//   - one vertex and index buffer for every lake (one draw); Ranges says which part is which lake. The rivers stay their own meshes.

	struct LakeVertex
	{
		glm::vec3 Position{ 0.0f };            // world; y = the level of the lake
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		uint32_t Lake = 0;                     // index into LakeMesh::Bodies (and WaterField::Lakes())
		float Depth = 0.0f;                    // m, level - ground, signed (<= 0 on the skirt)
		float ShoreDistance = 0.0f;            // m to the nearest sample that is not a cell of this lake (0 on the skirt)
	};
	static_assert(sizeof(LakeVertex) == 36, "LakeVertex: 9 tightly packed 32-bit values (stride 36)");
	static_assert(offsetof(LakeVertex, Position) == 0 && offsetof(LakeVertex, Normal) == 12 && offsetof(LakeVertex, Lake) == 24 &&
		offsetof(LakeVertex, Depth) == 28 && offsetof(LakeVertex, ShoreDistance) == 32, "LakeVertex: Position 0, Normal 12, Lake 24, Depth 28, ShoreDistance 32");

	// What the renderer needs to shade one lake: its level and its look (WaterParams, the colours and the clarity are its absorption).
	struct WaterBodyParams
	{
		float Level = 0.0f;
		WaterParams Look;
		glm::vec2 Center{ 0.0f };   // the circle that holds the lake (LakeBody::Center, Radius), e.g. for culling
		float Radius = 0.0f;
	};

	struct LakeMeshOptions
	{
		int SkirtCells = 1;   // 1 or 2: how many samples past the shore the water reaches under the ground
	};

	struct LakeMesh
	{
		std::vector<LakeVertex> Vertices;
		std::vector<uint32_t> Indices;          // triangles, counter-clockwise seen from +Y
		std::vector<WaterBodyParams> Bodies;    // one per WaterField::Lakes(), in that order (also a lake with no triangle on this grid)
		struct Range { uint32_t FirstVertex = 0, VertexCount = 0, FirstIndex = 0, IndexCount = 0; };
		std::vector<Range> Ranges;              // parallel to Bodies: each lake's vertices and indices are contiguous
		// Measures of the build (not for drawing): the lake's cells on the grid, and the triangles with a corner in the lake that were left out
		// because another corner is dry ground below the level (the shore at an outlet ends one triangle short there).
		size_t LakeCells = 0, DroppedShoreTriangles = 0;

		bool Empty() const { return Indices.empty(); }
		size_t TriangleCount() const { return Indices.size() / 3; }
	};

	// The ground the lakes lie on: a square grid of N x N heights, row major, sample (i, j) at Min + (i, j) * Spacing (a TgField, a Heightmap).
	struct LakeMeshGround
	{
		const float* Heights = nullptr;
		uint32_t N = 0;
		glm::vec2 Min{ 0.0f };
		float Spacing = 1.0f;

		float At(uint32_t i, uint32_t j) const { return Heights[static_cast<size_t>(j) * N + i]; }
		glm::vec2 SamplePosition(uint32_t i, uint32_t j) const { return Min + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * Spacing; }
	};
	LakeMeshGround GroundOf(const Heightmap& heightmap);

	LakeMesh BuildLakeMesh(const WaterField& water, const LakeMeshGround& ground, const WaterLook& look = DefaultWaterLook(),
		const LakeMeshOptions& options = {});
	inline LakeMesh BuildLakeMesh(const WaterField& water, const Heightmap& ground, const WaterLook& look = DefaultWaterLook(),
		const LakeMeshOptions& options = {})
	{
		return BuildLakeMesh(water, GroundOf(ground), look, options);
	}
}
