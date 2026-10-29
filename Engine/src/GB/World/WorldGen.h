#pragma once
#include "GB/World/Rocks.h"
#include "GB/World/Terrain.h"
#include "GB/World/Trees.h"
#include "GB/World/WorldGenVersion.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace GB
{
	enum class WorldType { Temperate, Alpine, Desert };
	enum class Biome { Water, Shore, Meadow, Forest, Rock, Snow, Dunes };
	const char* BiomeName(Biome b);   // "Water", "Shore", "Meadow", "Forest", "Rock", "Snow", "Dunes" (the names the data uses)

	struct WorldDesc
	{
		WorldType Type = WorldType::Temperate;
		uint32_t Seed = 1;
		// Which generator makes the world (WorldGenVersion.h): a new world uses the newest; a saved one keeps the version it was made with, so the
		// same seed gives it the same trees and rocks forever.
		uint32_t GeneratorVersion = kWorldGeneratorLatest;
		float Size = 360.0f;
		uint32_t Resolution = 641;
		bool River = true;               // a river from the high ground down to the lake (or across the map)
		bool Lake = true;                // a lake in the lowest basin
		float ForestAmount = 0.55f;      // 0..1: share of the land covered by forest
		int TreeBudget = 420;            // most trees placed (per tile)
		int RockBudget = 70;             // (per tile)
		// H16, a bigger world: tiles per side (1..4). The world is then Tiles x Tiles maps of Size metres made by the same generator AS ONE (one
		// heightmap of (Resolution - 1) x Tiles + 1 samples, the budgets x Tiles^2), so it is seamless at the borders by construction; GPU meshes
		// and navigation can still be cut by tile (WorldTiles.h). 1, the default, is exactly the world of before. One lake and one river per world.
		int Tiles = 1;
		float Extent() const { return Size * static_cast<float>(Tiles < 1 ? 1 : Tiles); }   // metres per side of the whole world
	};

	struct RockPlacement
	{
		int Variant = 0;
		glm::vec3 Position{ 0.0f };
		float Yaw = 0.0f;
		float Scale = 1.0f;
	};

	// The biomes and the distance to the water of a world, baked on a regular grid when it is generated. The grass, the resource planner and the
	// AI ask for the biome thousands of times; reading a cell is much cheaper than measuring the river, the normal and the moisture noise each time.
	struct BiomeMap
	{
		glm::vec2 Min{ 0.0f };           // world x,z of sample (0, 0)
		float Cell = 1.0f;               // meters between samples
		int N = 0;                       // samples per side (0 = not baked)
		std::vector<uint8_t> Biomes;     // Biome of each sample, row by row (z), N * N
		// Meters from each sample to the nearest water sample (lake or river): 0 in the water, 1e9 on a map without water. A true Euclidean
		// distance transform of the water, so the banks of a winding river and of an irregular lake shore are measured as they are.
		std::vector<float> WaterDistance;

		bool Empty() const { return N == 0; }
		size_t Nearest(float x, float z) const;          // index of the sample nearest to a point (clamped to the map)
		glm::vec2 SamplePosition(int i, int j) const { return Min + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * Cell; }
		float WaterDistanceAt(float x, float z) const;   // bilinear between the samples
	};

	// A whole generated map: terrain with lake and river, biome classification, and the placement of trees and
	// rocks following the biomes. The recipes list the species/rock variants the placements refer to.
	struct GeneratedWorld
	{
		WorldDesc Desc;
		std::unique_ptr<Terrain> TerrainPtr;       // may be moved out; the queries keep working through TerrainRef while it lives
		const Terrain* TerrainRef = nullptr;
		HillParams Hills;
		std::vector<TreeParams> TreeRecipes;
		std::vector<TreePlacement> Trees;          // Variant indexes TreeRecipes
		std::vector<RockParams> RockRecipes;
		std::vector<RockPlacement> Rocks;          // Variant indexes RockRecipes
		float SnowLine = 1e9f;                     // world height above which it is snowy
		glm::vec2 Spawn{ 0.0f };                   // a flat, dry place near the middle
		BiomeMap Maps;                             // baked biomes and distance to the water (see BiomeMap)

		// The biome at a point: the baked map's nearest sample (ComputeBiome before the map is baked).
		Biome BiomeAt(float x, float z) const;
		// The biome measured at a point (river distance, slope, moisture): what the map bakes, and what generator 1 placed its trees and rocks with.
		Biome ComputeBiome(float x, float z) const;
		// Meters to the lake or the river (0 in the water; large when there is none), from the baked map.
		float WaterDistanceAt(float x, float z) const;
		// 0..1 coverage of ground plants (grass) per biome.
		float GrassDensityAt(float x, float z) const;
		float MoistureAt(float x, float z) const;  // 0..1 noise field (forest clumps)
		// Share of the map of each biome (sampled), indexed by Biome.
		std::vector<float> BiomeShares(int samples = 48) const;
	};

	class WorldGenerator
	{
	public:
		// Pure CPU (no OpenGL, no window): the heights, the lake and river, the baked biome map and the placement of trees and rocks. The terrain
		// has no mesh nor textures yet (BuildGpu adds them). What the headless tests, the save baseline and a simulation without a window use.
		static GeneratedWorld GenerateData(const WorldDesc& desc);
		// The mesh, material and textures of the terrain of a world from GenerateData (needs an OpenGL context). Call it before moving TerrainPtr out.
		static void BuildGpu(GeneratedWorld& world);
		// GenerateData + BuildGpu: needs an OpenGL context.
		static GeneratedWorld Generate(const WorldDesc& desc);
	};
}
