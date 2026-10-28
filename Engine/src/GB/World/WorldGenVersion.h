#pragma once
#include <cstdint>

// Versions of the world generator (WorldDesc::GeneratorVersion, saved as WorldParams::GeneratorVersion in a .gbsave). A seed must give the same
// world forever: the differential save rebuilds its baseline from the seed and a replay starts from it. So every change to what a seed produces
// (heights, river, biomes, the placement of trees and rocks) goes behind a new version, and the old ones keep their code path bit for bit.
// Kept apart from WorldGen.h so GB/Save can name it without pulling in the renderer.
namespace GB
{
	// 1: the first generator (until October 2026). The placement hashes of trees and rocks used fixed salts, the same in every seed.
	// 2: the placement hashes mix the seed in; the biomes and the distance to the water are baked on a grid before placing anything.
	constexpr uint32_t kWorldGeneratorV1 = 1;
	constexpr uint32_t kWorldGeneratorLatest = 2;
}
