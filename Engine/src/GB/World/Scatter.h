#pragma once
#include "GB/World/Heightmap.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Where trees (and anything placed like them: the terrain graph's "scatter" node) go over a heightmap. Moved out of Trees.h/.cpp, which
	// build the trees' meshes, so the terrain graph (GB_WorldGen) does not depend on them; Trees.h includes it.
	struct TreePlacement
	{
		int Variant = 0;            // index into the list of trees given to Scatter
		glm::vec3 Position{ 0.0f };
		float Yaw = 0.0f;
		float Scale = 1.0f;
	};

	// Scatters `count` trees over a heightmap with a minimum spacing. Rejects steep ground, positions outside
	// [minHeight, maxHeight] and those where `allow(x, z)` (optional) is false. Deterministic by seed. Takes the
	// Heightmap directly (not a Terrain) because that is all it ever reads: a caller that only has one (a
	// terrain graph node, GB/World/Graph/Nodes/ScatterNode.h) does not need to build a whole Terrain for it.
	std::vector<TreePlacement> ScatterTrees(const Heightmap& heightmap, int count, int variants, float minSpacing, uint32_t seed,
		float minHeight, float maxHeight, float maxSlopeDegrees = 28.0f, const std::function<bool(float, float)>& allow = {});
}
