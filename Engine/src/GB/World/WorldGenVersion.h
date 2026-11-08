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
	// 3: the water is found by a terrain graph on the bare ground (GB/Water/WaterPlanner.h): the lake in the broadest basin with a lobed shore, the river
	//    along the real drainage network down to it, widening, deepening and quickening downstream (GB/Water/RiverHydraulics.h). The terrain, the biomes
	//    and the placement are the same code as 2; only the lake and the river change, so a world of generator 3 differs from the same seed of 2 there.
	// 4: the whole world is a terrain graph (docs/GRAFO_TERRENO.md, section 12): a preset of GB/World/Graph/TerrainGraphPresets.h made from the
	//    WorldDesc (MakeWorldGraph) builds the ground, digs the lake and the river, erodes around them, and publishes the moisture, the biomes, the
	//    trees and rocks and the material layers. Generators 1-3 keep their own code (WorldGenLegacy.cpp), frozen.
	constexpr uint32_t kWorldGeneratorV1 = 1;
	constexpr uint32_t kWorldGeneratorV3 = 3;
	constexpr uint32_t kWorldGeneratorV4 = 4;
	constexpr uint32_t kWorldGeneratorLatest = 4;
}
