#pragma once
#include "GB/World/Rocks.h"
#include "GB/World/Terrain.h"
#include "GB/World/Trees.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace GB
{
	enum class WorldType { Temperate, Alpine, Desert };
	enum class Biome { Water, Shore, Meadow, Forest, Rock, Snow, Dunes };

	struct WorldDesc
	{
		WorldType Type = WorldType::Temperate;
		uint32_t Seed = 1;
		float Size = 360.0f;
		uint32_t Resolution = 641;
		bool River = true;               // a river from the high ground down to the lake (or across the map)
		bool Lake = true;                // a lake in the lowest basin
		float ForestAmount = 0.55f;      // 0..1: share of the land covered by forest
		int TreeBudget = 420;            // most trees placed
		int RockBudget = 70;
	};

	struct RockPlacement
	{
		int Variant = 0;
		glm::vec3 Position{ 0.0f };
		float Yaw = 0.0f;
		float Scale = 1.0f;
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

		Biome BiomeAt(float x, float z) const;
		// 0..1 coverage of ground plants (grass) per biome.
		float GrassDensityAt(float x, float z) const;
		float MoistureAt(float x, float z) const;  // 0..1 noise field (forest clumps)
		// Share of the map of each biome (sampled), indexed by Biome.
		std::vector<float> BiomeShares(int samples = 48) const;
	};

	class WorldGenerator
	{
	public:
		// Needs an OpenGL context (the terrain builds its mesh).
		static GeneratedWorld Generate(const WorldDesc& desc);
	};
}
