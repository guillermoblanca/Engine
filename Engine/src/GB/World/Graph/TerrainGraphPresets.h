#pragma once
#include "GB/World/Graph/TerrainGraph.h"
#include <string>
#include <vector>

namespace GB
{
	// Terrain graphs that ship with the engine, to start from or to compare against (docs/GRAFO_TERRENO.md):
	//   "alpine_peaks"      a mountain range of sharp ridged crests, eroded into gullies, with rock on the faces and snow in the hollows
	//                       (modelled on a reference image of a snowy massif: no lake, river or trees)
	//   "temperate_valley"  the whole pipeline: hills, the lake and the river found and carved, erosion kept out of the water, moisture, biomes,
	//                       trees and rocks scattered by biome, grass and rock material layers
	//   "highland_valley"   an open landscape of about 3 km (terrain_graph preset highland_valley temperate <seed> - 3000 1025 3000): a grassy hill in
	//                       front, a valley with a lake and forest, a snow-capped range in the distance
	//   "temperate", "alpine", "desert"  the worlds of generator 4 with their default options (MakeWorldGraph)
	std::vector<std::string> TerrainGraphPresetNames();
	// The preset of that name; false when there is none.
	bool MakeTerrainGraphPreset(const std::string& name, TerrainGraph& out);

	// What a world of generator 4 asks of its graph (WorldGen.cpp fills it from the WorldDesc; GB/World/WorldGenVersion.h).
	struct WorldGraphOptions
	{
		enum class Ground { Hills, Mountains, Dunes };   // temperate, alpine, desert
		Ground Kind = Ground::Hills;
		bool Lake = true, River = true, Erosion = true;
		float LakeRadius = 22.0f, LakeDepth = 3.0f, LakeLevelAbove = 1.0f, LakeIrregularity = 0.12f;
		float RiverWidth = 5.0f;
		int TreeCount = 1260, RockCount = 210;   // spots tried (the world keeps its budget of them)
		float ForestMoisture = 0.525f;           // moister than this is forest
	};

	// The graph of a world of generator 4: the engine's hills or dunes, the lake in the broadest basin and the river down the drainage, dug in;
	// rain erosion kept out of the water; moisture and biomes; trees by biome (forest, lone meadow trees, a few on rock, sparse palms in the
	// desert) away from the river, rocks mostly on rock, shores and snow; on mountains, a rock material layer on the faces. Publishes height, water,
	// moisture, biome, trees, rocks (and splat_rock).
	TerrainGraph MakeWorldGraph(const WorldGraphOptions& options);
}
