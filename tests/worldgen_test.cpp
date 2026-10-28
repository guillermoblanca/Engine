// The world generator on the CPU only (no window, no OpenGL): WorldGenerator::GenerateData makes whole worlds headless. Checks that a seed always
// gives the same world, that the generator versions behave (version 1 kept as it was, version 2 mixing the seed into the placement), that the baked
// biome map agrees with the biome measured at its samples, that the distance to the water is a real distance, and the usual invariants of a world.
#include "GB/Gameplay/WorldResources.h"
#include "GB/Log.h"
#include "GB/World/WorldGen.h"
#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	GB::WorldDesc Small(GB::WorldType type, uint32_t seed, uint32_t version)
	{
		GB::WorldDesc d;
		d.Type = type; d.Seed = seed; d.GeneratorVersion = version;
		d.Size = 192.0f; d.Resolution = 161; d.TreeBudget = 150; d.RockBudget = 40;
		return d;
	}

	bool SamePlacements(const GB::GeneratedWorld& a, const GB::GeneratedWorld& b)
	{
		if (a.Trees.size() != b.Trees.size() || a.Rocks.size() != b.Rocks.size()) return false;
		for (size_t i = 0; i < a.Trees.size(); ++i)
			if (a.Trees[i].Position != b.Trees[i].Position || a.Trees[i].Variant != b.Trees[i].Variant || a.Trees[i].Yaw != b.Trees[i].Yaw || a.Trees[i].Scale != b.Trees[i].Scale) return false;
		for (size_t i = 0; i < a.Rocks.size(); ++i)
			if (a.Rocks[i].Position != b.Rocks[i].Position || a.Rocks[i].Variant != b.Rocks[i].Variant) return false;
		return a.Spawn == b.Spawn;
	}

	bool SameHeights(const GB::GeneratedWorld& a, const GB::GeneratedWorld& b)
	{
		const GB::Heightmap& x = a.TerrainRef->GetHeightmap();
		const GB::Heightmap& y = b.TerrainRef->GetHeightmap();
		if (x.GetResolution() != y.GetResolution()) return false;
		for (uint32_t j = 0; j < x.GetResolution(); ++j)
			for (uint32_t i = 0; i < x.GetResolution(); ++i)
				if (x.At(i, j) != y.At(i, j)) return false;
		return true;
	}

	// The invariants every world keeps, whatever its type, seed and version.
	void Audit(const GB::GeneratedWorld& w, const char* label)
	{
		char what[256];
		auto check = [&](bool ok, const char* text) { std::snprintf(what, sizeof(what), "%s: %s", label, text); Check(ok, what); };
		const GB::BiomeMap& m = w.Maps;
		const GB::Heightmap& hm = w.TerrainRef->GetHeightmap();
		check(!w.TerrainRef->HasGpu(), "GenerateData builds no mesh");
		check(m.N > 2 && m.Biomes.size() == static_cast<size_t>(m.N) * static_cast<size_t>(m.N) && m.WaterDistance.size() == m.Biomes.size(), "the biome map is baked");
		check(m.Cell > 0.5f && m.Cell <= 2.0f && std::fabs(m.Min.x - hm.GetMin().x) < 1e-4f && std::fabs(m.SamplePosition(m.N - 1, m.N - 1).x - hm.GetMax().x) < 1e-2f, "on a 0.5-2 m grid over the whole map");
		// the map holds what is measured at its samples
		int mismatches = 0, checked = 0;
		for (int j = 0; j < m.N; j += 7)
			for (int i = 0; i < m.N; i += 5)
			{
				const glm::vec2 p = m.SamplePosition(i, j);
				++checked;
				if (static_cast<GB::Biome>(m.Biomes[static_cast<size_t>(j) * static_cast<size_t>(m.N) + static_cast<size_t>(i)]) != w.ComputeBiome(p.x, p.y)) ++mismatches;
			}
		check(checked > 500 && mismatches == 0, "every baked sample is the biome measured there");
		// the distance to the water: 0 in it, a sample away on the next dry one, and it never grows faster than the distance walked
		bool zeroInWater = true, lipschitz = true, positiveOnLand = true, anyWater = false;
		for (int j = 0; j < m.N; ++j)
			for (int i = 0; i < m.N; ++i)
			{
				const size_t o = static_cast<size_t>(j) * static_cast<size_t>(m.N) + static_cast<size_t>(i);
				const bool water = static_cast<GB::Biome>(m.Biomes[o]) == GB::Biome::Water;
				anyWater = anyWater || water;
				if (water && m.WaterDistance[o] != 0.0f) zeroInWater = false;
				if (!water && m.WaterDistance[o] < m.Cell * 0.999f) positiveOnLand = false;
				if (i + 1 < m.N && m.WaterDistance[o] < 1e8f && std::fabs(m.WaterDistance[o] - m.WaterDistance[o + 1]) > m.Cell * 1.001f) lipschitz = false;
				if (j + 1 < m.N && m.WaterDistance[o] < 1e8f && std::fabs(m.WaterDistance[o] - m.WaterDistance[o + static_cast<size_t>(m.N)]) > m.Cell * 1.001f) lipschitz = false;
			}
		check(zeroInWater && positiveOnLand && lipschitz, "the distance to the water is 0 in it, at least a sample on land and continuous");
		if (!anyWater) check(m.WaterDistance.front() >= 1e8f, "a map without water is far from it everywhere");
		// placements: inside the map, on dry land, valid recipes, within budget
		int bad = 0;
		for (const GB::TreePlacement& t : w.Trees)
		{
			const GB::Biome b = w.BiomeAt(t.Position.x, t.Position.z);
			if (b == GB::Biome::Water || b == GB::Biome::Shore || t.Variant < 0 || t.Variant >= static_cast<int>(w.TreeRecipes.size()) || !hm.Contains(t.Position.x, t.Position.z)) ++bad;
		}
		for (const GB::RockPlacement& r : w.Rocks)
			if (w.BiomeAt(r.Position.x, r.Position.z) == GB::Biome::Water || r.Variant < 0 || r.Variant >= static_cast<int>(w.RockRecipes.size())) ++bad;
		if (w.Desc.GeneratorVersion >= 2) check(bad == 0, "no tree in the water or on the shore, no rock in the water, valid recipes");   // (generator 1 placed with the measured biome)
		check(static_cast<int>(w.Trees.size()) <= w.Desc.TreeBudget && static_cast<int>(w.Rocks.size()) <= w.Desc.RockBudget, "within the budgets");
		check(w.BiomeAt(w.Spawn.x, w.Spawn.y) != GB::Biome::Water, "the spawn point is dry");
		float total = 0.0f;
		for (float s : w.BiomeShares(32)) total += s;
		check(std::fabs(total - 1.0f) < 1e-3f, "the biome shares add up to 1");
	}
}

int main()
{
	GB::Log::Init();
	using GB::WorldType;

	// ---- a whole world, twice, without a window
	const auto t0 = std::chrono::steady_clock::now();
	const GB::GeneratedWorld a = GB::WorldGenerator::GenerateData(Small(WorldType::Temperate, 5, 2));
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("info: temperate 5 (192 m, generator 2) generated on the CPU in %.0f ms: %zu trees, %zu rocks, biome map %dx%d\n", ms, a.Trees.size(), a.Rocks.size(), a.Maps.N, a.Maps.N);
	Check(a.TerrainRef && a.TerrainRef->HasLake() && a.TerrainRef->GetRiver(), "a temperate world has a lake and a river");
	Check(a.Trees.size() > 40 && !a.Rocks.empty(), "and trees and rocks");
	Audit(a, "temperate 5 v2");
	const GB::GeneratedWorld again = GB::WorldGenerator::GenerateData(Small(WorldType::Temperate, 5, 2));
	Check(SameHeights(a, again) && SamePlacements(a, again) && a.Maps.Biomes == again.Maps.Biomes && a.Maps.WaterDistance == again.Maps.WaterDistance, "the same seed gives the same world, bit for bit");
	const GB::GeneratedWorld other = GB::WorldGenerator::GenerateData(Small(WorldType::Temperate, 6, 2));
	Check(!SameHeights(a, other) && !SamePlacements(a, other), "another seed, another world");

	// ---- versions: the terrain is the same, the placement is not; version 1 is deterministic too
	const GB::GeneratedWorld v1 = GB::WorldGenerator::GenerateData(Small(WorldType::Temperate, 5, 1));
	const GB::GeneratedWorld v1again = GB::WorldGenerator::GenerateData(Small(WorldType::Temperate, 5, 1));
	Check(SameHeights(a, v1), "both generator versions make the same terrain");
	Check(!SamePlacements(a, v1), "but place the trees and rocks differently");
	Check(SamePlacements(v1, v1again) && v1.Maps.Biomes == v1again.Maps.Biomes, "generator 1 is deterministic");
	Audit(v1, "temperate 5 v1");
	Check(GB::WorldDesc().GeneratorVersion == GB::kWorldGeneratorLatest, "a new world uses the newest generator");

	// ---- generator 2 mixes the seed into the placement: the share of meadow points that grow a lone tree is independent between seeds
	// (with the fixed salts of generator 1, the same points were chosen in every seed, so worlds with similar meadows had their lone trees at the same spots)
	{
		const GB::GeneratedWorld s7 = GB::WorldGenerator::GenerateData(Small(WorldType::Temperate, 7, 2));
		int shared = 0;
		for (const GB::TreePlacement& t : s7.Trees)
			for (const GB::TreePlacement& u : a.Trees)
				if (glm::distance(glm::vec2(t.Position.x, t.Position.z), glm::vec2(u.Position.x, u.Position.z)) < 0.01f) ++shared;
		Check(shared == 0, "two seeds do not share tree spots");
	}

	// ---- other types and water layouts
	const GB::GeneratedWorld alpine = GB::WorldGenerator::GenerateData(Small(WorldType::Alpine, 2, 2));
	Audit(alpine, "alpine 2 v2");
	const auto shares = alpine.BiomeShares();
	Check(alpine.SnowLine < 1e8f && shares[static_cast<size_t>(GB::Biome::Snow)] > 0.02f && shares[static_cast<size_t>(GB::Biome::Rock)] > 0.01f, "an alpine world has snow and bare rock");
	const GB::GeneratedWorld desert = GB::WorldGenerator::GenerateData(Small(WorldType::Desert, 3, 2));
	Audit(desert, "desert 3 v2");
	Check(desert.BiomeShares()[static_cast<size_t>(GB::Biome::Dunes)] > 0.99f && desert.WaterDistanceAt(0.0f, 0.0f) >= 1e8f, "a desert is dunes, far from any water");
	{
		GB::WorldDesc dry = Small(WorldType::Temperate, 50, 2);
		dry.River = false; dry.Lake = false;
		const GB::GeneratedWorld w = GB::WorldGenerator::GenerateData(dry);
		Audit(w, "no water v2");
		Check(!w.TerrainRef->HasLake() && !w.TerrainRef->GetRiver(), "a world without water has no lake nor river");
	}
	{
		GB::WorldDesc noLake = Small(WorldType::Temperate, 40, 2);
		noLake.Lake = false;
		Audit(GB::WorldGenerator::GenerateData(noLake), "no lake v2");
	}

	// ---- the distance to the water, against the lake: a point on the lake's axis, well outside it, is about as far as the shore
	{
		const GB::Terrain& t = *a.TerrainRef;
		const glm::vec2 c = t.GetLakeCenter();
		Check(a.WaterDistanceAt(c.x, c.y) == 0.0f && a.BiomeAt(c.x, c.y) == GB::Biome::Water, "the middle of the lake is water");
		const glm::vec2 out = c + glm::vec2(t.GetLakeRadius() * 2.0f, 0.0f);
		const float d = a.WaterDistanceAt(out.x, out.y);
		Check(d > 2.0f && d < t.GetLakeRadius() * 2.0f, "a point beyond the shore is closer to the water than to the lake's centre");
	}

	// ---- the resource ground reads the baked data
	{
		const GB::ResourceGround g = GB::ResourceGroundOf(a);
		int same = 0, n = 0;
		for (int k = 0; k < 400; ++k)
		{
			const float x = -90.0f + static_cast<float>(k % 20) * 9.0f, z = -90.0f + static_cast<float>(k / 20) * 9.0f;
			++n;
			if (g.WaterDistance(x, z) == a.WaterDistanceAt(x, z) && g.Biome(x, z) == std::string(GB::BiomeName(a.BiomeAt(x, z)))) ++same;
		}
		Check(same == n, "ResourceGroundOf gives the baked biome and distance to the water");
	}

	if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
	std::puts("worldgen tests passed");
	return 0;
}
