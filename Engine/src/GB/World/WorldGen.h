#pragma once
#include "GB/World/Biome.h"
#include "GB/World/Rocks.h"
#include "GB/World/Terrain.h"
#include "GB/World/Trees.h"
#include "GB/World/WorldGenVersion.h"
#include "GB/World/Graph/TerrainGraph.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace GB
{
	enum class WorldType { Temperate, Alpine, Desert };

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
		// Hydraulic erosion (GB::HydraulicErosion, GB/World/Erosion.h) carved into the hills after the lake and river, before anything reads the
		// terrain (biomes, spawn, trees, rocks all see the eroded ground). Deterministic for a seed; off for Desert (there is no water to carve with).
		bool Erosion = true;
		// Render-mesh refinement by curvature (Terrain::FlagDetailCells, HillParams/DuneParams::DetailThreshold): metres a flat quad may miss
		// the smooth surface before it is split. Smaller = more detail (and more vertices); <= 0 = off (water-shore refinement only).
		float MeshDetail = 0.08f;
		float ForestAmount = 0.55f;      // 0..1: share of the land covered by forest
		int TreeBudget = 420;            // most trees placed (per tile)
		int RockBudget = 70;             // (per tile)
		// H16, a bigger world: tiles per side (1..4). The world is then Tiles x Tiles maps of Size metres made by the same generator AS ONE (one
		// heightmap of (Resolution - 1) x Tiles + 1 samples, the budgets x Tiles^2), so it is seamless at the borders by construction; GPU meshes
		// and navigation can still be cut by tile (WorldTiles.h). 1, the default, is exactly the world of before. One lake and one river per world.
		int Tiles = 1;
		std::shared_ptr<const TerrainGraph> TerrainGraphDefinition;
		// The node-by-node memo of the graph's evaluation (GB/World/Graph/TerrainGraphCache.h): the editor keeps one across regenerations so a
		// parameter change only runs that node and what depends on it. Null = every node runs. It never changes the world: a reused output is
		// the output the node would give again.
		std::shared_ptr<TerrainGraphCache> GraphCache;
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
		// Cover of ground plants (0..1) at each sample: the density of the biome blurred over a few metres, so the grass fades from one zone to the next.
		// Empty on a map without it (the desert).
		std::vector<float> Grass;

		bool Empty() const { return N == 0; }
		size_t Nearest(float x, float z) const;          // index of the sample nearest to a point (clamped to the map)
		glm::vec2 SamplePosition(int i, int j) const { return Min + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * Cell; }
		float WaterDistanceAt(float x, float z) const;   // bilinear between the samples
		float GrassAt(float x, float z) const;           // bilinear between the samples (needs Grass)
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
		// What a terrain graph published as "biome" (GB::Biome ids) and "moisture" (0..1): empty when the world has no graph or it published
		// neither; then the biome is measured and the moisture is a noise.
		TgField BiomeField;
		TgField MoistureField;

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
		// has no mesh nor textures yet (PrepareRenderData). What the headless tests, the save baseline and a simulation without a window use.
		static GeneratedWorld GenerateData(const WorldDesc& desc);
		// The CPU half of the terrain's drawing (Terrain::PrepareRenderData: the refined mesh, the images of its textures), still with no OpenGL.
		// The upload is GB/Renderer/TerrainGpu.h's, made by whoever draws the world. Call it before moving TerrainPtr out.
		static void PrepareRenderData(GeneratedWorld& world);
		// GenerateData + PrepareRenderData.
		static GeneratedWorld Generate(const WorldDesc& desc);
	};
}
