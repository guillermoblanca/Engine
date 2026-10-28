// Seed and parameter sweeps (like Houdini PDG "wedges"): generate many worlds and check invariants that must hold
// for every one of them, reporting the seeds that break them. CPU only (WorldGenerator::GenerateData), with the newest generator.
#include "GB/Log.h"
#include "GB/World/WorldGen.h"
#include "GB/Gameplay/WorldResources.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int failures = 0;

	// Returns the broken invariants of one world, empty when it is sound.
	std::vector<std::string> Audit(const GB::GeneratedWorld& w)
	{
		std::vector<std::string> bad;
		const GB::Terrain& t = *w.TerrainRef;
		const GB::Heightmap& hm = t.GetHeightmap();
		for (uint32_t j = 0; j < hm.GetResolution(); j += 3)
			for (uint32_t i = 0; i < hm.GetResolution(); i += 3)
				if (!std::isfinite(hm.At(i, j)) || std::fabs(hm.At(i, j)) > 500.0f) { bad.push_back("a height is not finite or absurd"); i = j = hm.GetResolution(); }
		if (static_cast<int>(w.Trees.size()) > w.Desc.TreeBudget) bad.push_back("more trees than the budget");
		if (static_cast<int>(w.Rocks.size()) > w.Desc.RockBudget) bad.push_back("more rocks than the budget");
		const glm::vec2 lo = hm.GetMin(), hi = hm.GetMax();
		auto inside = [&](const glm::vec3& p) { return p.x >= lo.x && p.x <= hi.x && p.z >= lo.y && p.z <= hi.y && std::isfinite(p.y); };
		int wet = 0, outside = 0, badVariant = 0, floating = 0;
		for (const auto& tr : w.Trees)
		{
			if (!inside(tr.Position)) ++outside;
			if (tr.Variant < 0 || tr.Variant >= static_cast<int>(w.TreeRecipes.size())) ++badVariant;
			const GB::Biome b = w.BiomeAt(tr.Position.x, tr.Position.z);
			if (b == GB::Biome::Water || b == GB::Biome::Shore) ++wet;
			const float above = tr.Position.y - t.GetHeight(tr.Position.x, tr.Position.z);
			if (above > 0.1f || above < -0.6f) ++floating;   // planted a little into the ground, never in the air
		}
		for (const auto& r : w.Rocks)
		{
			if (!inside(r.Position)) ++outside;
			if (r.Variant < 0 || r.Variant >= static_cast<int>(w.RockRecipes.size())) ++badVariant;
			if (w.BiomeAt(r.Position.x, r.Position.z) == GB::Biome::Water) ++wet;
		}
		if (outside) bad.push_back(std::to_string(outside) + " placements outside the map");
		if (badVariant) bad.push_back(std::to_string(badVariant) + " placements with an invalid recipe");
		if (wet) bad.push_back(std::to_string(wet) + " trees/rocks in the water or on the shore");
		if (floating) bad.push_back(std::to_string(floating) + " trees not on the ground");
		const auto shares = w.BiomeShares(32);
		float total = 0.0f;
		for (float s : shares) total += s;
		if (std::fabs(total - 1.0f) > 1e-3f) bad.push_back("biome shares do not add up to 1");
		if (const GB::RiverData* r = t.GetRiver())
		{
			bool monotonic = true;
			for (size_t i = 1; i < r->Points.size(); ++i) if (r->Points[i].y > r->Points[i - 1].y + 1e-4f) monotonic = false;
			if (!monotonic) bad.push_back("the river level goes uphill");
			for (const glm::vec3& p : r->Points) if (!inside(p)) { bad.push_back("the river leaves the map"); break; }
			if (t.HasLake())
			{
				const float drop = r->Points.back().y - t.GetWaterLevel();
				if (drop < -0.6f || drop > 3.5f) bad.push_back("the river ends " + std::to_string(drop) + " m from the lake level");
				if (glm::distance(glm::vec2(r->Points.back().x, r->Points.back().z), t.GetLakeCenter()) > t.GetLakeRadius() * 2.2f) bad.push_back("the river does not reach the lake");
			}
		}
		if (t.HasLake() && !(t.GetHeight(t.GetLakeCenter().x, t.GetLakeCenter().y) < t.GetWaterLevel() - 1.0f)) bad.push_back("the lake basin is not below the water level");
		if (w.BiomeAt(w.Spawn.x, w.Spawn.y) == GB::Biome::Water) bad.push_back("the spawn point is in the water");
		return bad;
	}

	// The resource nodes the era-1 data spreads over the same world (PlanWorldResources): on the ground, out of the water, clear of the world's trees and
	// rocks, and a green world always has wood, stone and food somewhere.
	std::vector<std::string> AuditResources(const GB::GeneratedWorld& w, const std::map<std::string, GB::ResourceNodeDef>& defs)
	{
		std::vector<std::string> bad;
		const GB::ResourceGround ground = GB::ResourceGroundOf(w);
		const std::vector<GB::ResourceSpot> spots = GB::PlanWorldResources(ground, defs, w.Desc.Seed);
		int wet = 0, floating = 0, blocked = 0, wrongBiome = 0;
		for (const GB::ResourceSpot& s : spots)
		{
			const GB::Biome b = w.BiomeAt(s.Position.x, s.Position.z);
			if (b == GB::Biome::Water) ++wet;
			if (std::fabs(s.Position.y - w.TerrainRef->GetHeight(s.Position.x, s.Position.z)) > 1e-3f) ++floating;
			for (const auto& k : ground.Blockers) if (glm::distance(k.At, glm::vec2(s.Position.x, s.Position.z)) < k.Radius) { ++blocked; break; }
			const auto& biomes = defs.at(s.Node).Spawn.Biomes;
			if (std::find(biomes.begin(), biomes.end(), ground.Biome(s.Position.x, s.Position.z)) == biomes.end()) ++wrongBiome;
		}
		if (wet) bad.push_back(std::to_string(wet) + " resource nodes in the water");
		if (floating) bad.push_back(std::to_string(floating) + " resource nodes off the ground");
		if (blocked) bad.push_back(std::to_string(blocked) + " resource nodes inside a tree or rock of the world");
		if (wrongBiome) bad.push_back(std::to_string(wrongBiome) + " resource nodes on a biome their type does not grow on");
		if (w.Desc.Type != GB::WorldType::Desert)
		{
			const auto counts = GB::CountSpots(spots);
			auto has = [&](std::initializer_list<const char*> ids) { for (const char* id : ids) if (counts.count(id)) return true; return false; };
			if (!has({ "tree", "old_tree" })) bad.push_back("no wood on the whole map");
			if (!has({ "rock", "flint_outcrop" })) bad.push_back("no stone on the whole map");
			if (!has({ "berry_bush" })) bad.push_back("no berries on the whole map");
		}
		return bad;
	}
}

int main()
{
	GB::Log::Init();
	int worlds = 0;
	GB::ItemLibrary items;
	GB::RecipeBook recipes;
	std::map<std::string, GB::ResourceNodeDef> defs;
	GB::RegisterEraOneItems(items, recipes, defs);
	auto sweep = [&](const char* label, GB::WorldDesc desc, uint32_t firstSeed, uint32_t count)
	{
		for (uint32_t s = firstSeed; s < firstSeed + count; ++s)
		{
			desc.Seed = s;
			const GB::GeneratedWorld w = GB::WorldGenerator::GenerateData(desc);   // the audit needs no mesh: CPU only
			++worlds;
			std::vector<std::string> problems = Audit(w);
			for (const std::string& r : AuditResources(w, defs)) problems.push_back(r);
			for (const std::string& problem : problems)
			{
				std::fprintf(stderr, "FAIL: %s seed %u: %s\n", label, s, problem.c_str());
				++failures;
			}
		}
	};
	GB::WorldDesc base;
	base.Size = 192.0f; base.Resolution = 129; base.TreeBudget = 120; base.RockBudget = 30;
	GB::WorldDesc temperate = base;
	sweep("temperate", temperate, 1, 8);
	GB::WorldDesc alpine = base; alpine.Type = GB::WorldType::Alpine;
	sweep("alpine", alpine, 1, 5);
	GB::WorldDesc desert = base; desert.Type = GB::WorldType::Desert;
	sweep("desert", desert, 1, 3);
	// parameter wedges: forest amount and which water features exist
	for (float forest : { 0.0f, 0.5f, 1.0f })
	{
		GB::WorldDesc d = base; d.ForestAmount = forest;
		sweep(("forest " + std::to_string(forest)).c_str(), d, 20, 2);
	}
	GB::WorldDesc noRiver = base; noRiver.River = false; sweep("no river", noRiver, 30, 2);
	GB::WorldDesc noLake = base; noLake.Lake = false; sweep("no lake", noLake, 40, 2);
	GB::WorldDesc dry = base; dry.River = false; dry.Lake = false; sweep("no water", dry, 50, 2);
	std::fprintf(stderr, "info: %d worlds audited\n", worlds);
	return failures;
}
