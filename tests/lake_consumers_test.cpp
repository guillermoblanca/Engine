// The consumers of a world's water see every lake of a terrain graph, not only the main one (T104, TERRENO_V5 §12.5). On the highland valley
// with the lakes of E1 (highland_valley_e1.json, 3 km at 257 x 257, seed 5), whose biome is still the frozen biome_classify that only knows the
// main lake:
//   - a walker (MakeTerrainCharacterWorld, and so the navigation grid) finds water at a new lake's level wherever the lake covers the ground;
//   - the rain (MakeGroundFunction(true)) lands on that surface;
//   - no grass grows under a new lake nor on its wet rim (GrassDensityAt), and no resource node stands in one (PlanWorldResources);
//   - the new lakes' shores are refined like the main lake's (more mesh than the same ground with only the main lake).
// CPU only, no window.
#include "GB/Game/CharacterController.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/World/Terrain.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}
}

using namespace GB;

int GBTest_lake_consumers(int, char**)
{
	std::ifstream file(std::string(GB_ASSETS_DIR) + "/data/terrain_graphs/highland_valley_e1.json", std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	WorldDesc desc;
	desc.Type = WorldType::Temperate;
	desc.Seed = 5;
	desc.Size = 3000.0f;
	desc.Resolution = 257;
	const std::string error = SetWorldGraphJson(desc, text.str());
	Check(error.empty(), "highland_valley_e1.json reads: " + error);
	if (!error.empty()) return 1;
	const GeneratedWorld world = WorldGenerator::GenerateData(desc);
	Check(world.TerrainRef != nullptr, "the valley is generated");
	if (!world.TerrainRef) return 1;
	const Terrain& t = *world.TerrainRef;
	const WaterField& water = t.Water();
	const std::vector<LakeBody>& more = water.MoreLakes();
	Check(more.size() >= 2, "the valley has several lakes of E1 (" + std::to_string(more.size()) + ")");

	// 1. under each new lake: the walker's water, the rain's ground and the grass
	const CharacterWorld walker = MakeTerrainCharacterWorld(t);
	const auto rain = t.MakeGroundFunction(true);
	size_t wet = 0, walkerDry = 0, rainOff = 0, grassIn = 0, rim = 0, grassRim = 0;
	glm::vec3 probe(0.0f);
	float probeDepth = 0.0f;
	for (const LakeBody& lake : more)
	{
		const LakeMask& m = lake.Mask;
		for (int j = 0; j < m.H; ++j)
			for (int i = 0; i < m.W; ++i)
			{
				const float x = m.Origin.x + static_cast<float>(i) * m.Spacing, z = m.Origin.y + static_cast<float>(j) * m.Spacing;
				const float ground = t.GetHeight(x, z);
				if (m.Cells[static_cast<size_t>(j) * static_cast<size_t>(m.W) + static_cast<size_t>(i)] != 0)
				{
					if (ground >= lake.Level - 0.05f) continue;   // (the shallowest samples of the rim: a walker there stands, as at the main lake)
					++wet;
					if (lake.Level - ground > probeDepth) { probeDepth = lake.Level - ground; probe = { x, lake.Level, z }; }
					if (walker.WaterLevel(x, z) != lake.Level) ++walkerDry;
					if (rain(x, z) != lake.Level) ++rainOff;
					if (world.GrassDensityAt(x, z) > 0.0f) ++grassIn;
				}
				else if (m.Contains(x + m.Spacing, z) || m.Contains(x - m.Spacing, z) || m.Contains(x, z + m.Spacing) || m.Contains(x, z - m.Spacing))
				{
					if (ground > lake.Level + 0.2f) continue;   // the wet rim: one sample out, hardly above the water
					++rim;
					if (world.GrassDensityAt(x, z) > 0.0f) ++grassRim;
				}
			}
	}
	std::printf("lake_consumers: %zu lakes of E1, %zu samples under them, %zu on their wet rim; deepest at (%.1f, %.1f), level %.2f m, %.1f m deep\n",
		more.size(), wet, rim, probe.x, probe.z, probe.y, probeDepth);
	std::printf("lake_consumers: walker sees no water at %zu, rain misses %zu, grass under %zu and on the rim %zu\n", walkerDry, rainOff, grassIn, grassRim);
	Check(wet > 100, "the new lakes cover ground (" + std::to_string(wet) + " samples)");
	Check(walkerDry == 0, "the walker finds water at the lake's level under every new lake (" + std::to_string(walkerDry) + " samples dry)");
	Check(probeDepth > 1.0f && walker.WaterLevel(probe.x, probe.z) == probe.y && walker.WaterLevel(probe.x, probe.z) > walker.GroundHeight(probe.x, probe.z) + 1.0f,
		"at the deepest point of a new lake the walker's water stands at its level, more than a metre over the ground");
	Check(rainOff == 0, "the rain lands on the new lakes' surface (" + std::to_string(rainOff) + " samples)");
	Check(grassIn == 0, "no grass under a new lake (" + std::to_string(grassIn) + " samples)");
	Check(grassRim == 0, "no grass on a new lake's wet rim (" + std::to_string(grassRim) + " samples)");

	// away from every lake and the river nothing changes: dry ground stays dry
	{
		size_t dryWet = 0, dry = 0;
		const Heightmap& hm = t.GetHeightmap();
		for (int j = 8; j < 257; j += 16)
			for (int i = 8; i < 257; i += 16)
			{
				const glm::vec2 p = hm.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
				if (water.LevelNear(p.x, p.y, 1.5f, 3.0f) != kNoWaterLevel || water.LakeLevelAt(p.x, p.y) != kNoWaterLevel) continue;
				bool nearLake = false;
				for (const LakeBody& lake : more) nearLake = nearLake || glm::distance(p, lake.Center) < lake.Radius * 1.2f;
				if (nearLake || (t.HasLake() && t.GetHeight(p.x, p.y) < t.GetWaterLevel())) continue;
				++dry;
				if (walker.WaterLevel(p.x, p.y) > -1e8f) ++dryWet;
			}
		Check(dry > 50 && dryWet == 0, "dry ground far from the water stays dry to the walker (" + std::to_string(dryWet) + " of " + std::to_string(dry) + ")");
	}

	// 2. resource nodes: none under a new lake
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> defs;
		RegisterEraOneItems(items, recipes, defs);
		const std::vector<ResourceSpot> spots = PlanWorldResources(ResourceGroundOf(world), defs, 5);
		size_t inLake = 0;
		for (const ResourceSpot& s : spots)
			for (const LakeBody& lake : more)
				if (lake.Covers({ s.Position.x, s.Position.z }) && t.GetHeight(s.Position.x, s.Position.z) < lake.Level) { ++inLake; break; }
		std::printf("lake_consumers: %zu resource nodes, %zu under a new lake\n", spots.size(), inLake);
		Check(!spots.empty(), "the valley has resource nodes");
		Check(inLake == 0, "no resource node under a new lake (" + std::to_string(inLake) + ")");
	}

	// 3. the shore: the same ground and water with and without the new lakes; their shores add refined cells
	{
		HillParams recipe;
		recipe.SurfaceMaps = false;   // (no curvature refinement either: only the shores flag cells)
		WaterField mainOnly;
		if (water.HasLake()) mainOnly.SetLake(water.Lake());
		if (const RiverData* r = water.River()) mainOnly.SetRiver(*r);
		auto all = Terrain::CreateFromHeightmap(t.GetHeightmap(), water, recipe);
		auto one = Terrain::CreateFromHeightmap(t.GetHeightmap(), mainOnly, recipe);
		all->PrepareRenderData();
		one->PrepareRenderData();
		const auto allData = all->TakeRenderData(), oneData = one->TakeRenderData();
		const size_t allV = all->GetVertices().size(), oneV = one->GetVertices().size();
		std::printf("lake_consumers: mesh %zu vertices with the new lakes' shores, %zu with the main lake's only\n", allV, oneV);
		Check(allV > oneV + 1000, "the new lakes' shores are refined");
	}

	// 4. the cost of the question every walker asks every tick (information only: no limit is checked, the machine's load varies)
	{
		const Heightmap& hm = t.GetHeightmap();
		constexpr int kSide = 1000;
		auto at = [&](int k) { return hm.GetMin() + (glm::vec2(static_cast<float>(k % kSide), static_cast<float>(k / kSide)) + 0.5f) * (hm.GetSize() / kSide); };
		double sum = 0.0;
		auto t0 = std::chrono::steady_clock::now();
		for (int k = 0; k < kSide * kSide; ++k) { const glm::vec2 p = at(k); sum += walker.WaterLevel(p.x, p.y) > -1e8f ? 1.0 : 0.0; }
		const double allNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (kSide * kSide);
		t0 = std::chrono::steady_clock::now();
		for (int k = 0; k < kSide * kSide; ++k)   // the question of before: the main lake and the river only
		{
			const glm::vec2 p = at(k);
			const float lake = t.HasLake() && t.GetHeight(p.x, p.y) < t.GetWaterLevel() ? t.GetWaterLevel() : -1e9f;
			sum += std::max(lake, t.GetRiverLevelAt(p.x, p.y)) > -1e8f ? 1.0 : 0.0;
		}
		const double mainNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (kSide * kSide);
		std::printf("lake_consumers: the walker's water costs %.1f ns a point with %zu extra lakes, %.1f ns asking the main lake and the river only (%.0f wet)\n",
			allNs, more.size(), mainNs, sum);
	}

	if (failures == 0) std::printf("lake_consumers: OK\n");
	return failures == 0 ? 0 : 1;
}
