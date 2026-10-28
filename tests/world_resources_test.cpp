// Resource nodes spread over a world (PlanWorldResources): on a synthetic ground with a forest, a meadow, rocky ground and a lake, every node lands on
// one of its biomes, on ground it accepts, apart from the others and from the world's trees and rocks; the same seed gives the same world, the caps
// hold, the density scales, and the nodes built from the plan can be gathered like any other. CPU only.
#include "GB/Gameplay/WorldResources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	constexpr float dt = 1.0f / 60.0f;
}

using namespace GB;

// 240 x 240 m: forest to the west, meadow in the middle, rocky ground to the east (a cliff past x = 100), a lake along the north edge with its shore.
static ResourceGround Synthetic()
{
	ResourceGround g;
	g.Min = { -120.0f, -120.0f };
	g.Max = { 120.0f, 120.0f };
	g.Height = [](float x, float z) { return 0.04f * x + 0.5f * std::sin(z * 0.05f); };
	g.WaterDistance = [](float, float z) { return std::max(0.0f, 90.0f - z); };
	g.Biome = [](float x, float z) -> std::string
	{
		if (z >= 90.0f) return "Water";
		if (z > 83.0f) return "Shore";
		if (x < -40.0f) return "Forest";
		if (x > 45.0f) return "Rock";
		return "Meadow";
	};
	g.SlopeDegrees = [](float x, float) { return x > 100.0f ? 60.0f : x > 45.0f ? 30.0f : 4.0f; };
	for (int k = 0; k < 40; ++k)   // the world's own trees in the forest
		g.Blockers.push_back({ glm::vec2(-110.0f + 7.0f * static_cast<float>(k % 10), -100.0f + 20.0f * static_cast<float>(k / 10)), 1.5f });
	return g;
}

static bool Contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

int main()
{
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> defs;
	RegisterEraOneItems(items, recipes, defs);
	const ResourceGround ground = Synthetic();

	const std::vector<ResourceSpot> spots = PlanWorldResources(ground, defs, 7);
	const std::map<std::string, int> counts = CountSpots(spots);
	std::fprintf(stderr, "info: %zu spots:", spots.size());
	for (const auto& [id, n] : counts) std::fprintf(stderr, " %s %d", id.c_str(), n);
	std::fprintf(stderr, "\n");

	// every type with a spawn shows up on this ground, which has all its biomes
	for (const auto& [id, def] : defs)
		if (def.Spawn.Placed() && !counts.count(id)) { std::fprintf(stderr, "  (missing: %s)\n", id.c_str()); Check(false, "every type with a spawn is placed somewhere"); }

	// the rules of each spot
	int badBiome = 0, steep = 0, wet = 0, farFromWater = 0, outside = 0, offGround = 0, blocked = 0, crowded = 0;
	for (size_t a = 0; a < spots.size(); ++a)
	{
		const ResourceSpot& s = spots[a];
		const ResourceSpawnDef& sp = defs.at(s.Node).Spawn;
		const float x = s.Position.x, z = s.Position.z;
		if (!Contains(sp.Biomes, ground.Biome(x, z))) ++badBiome;
		if (ground.SlopeDegrees(x, z) > sp.MaxSlope) ++steep;
		if (ground.WaterDistance(x, z) < 1.0f) ++wet;
		if (sp.WaterWithin > 0.0f && ground.WaterDistance(x, z) > sp.WaterWithin) ++farFromWater;
		if (x < ground.Min.x + 2.0f || x > ground.Max.x - 2.0f || z < ground.Min.y + 2.0f || z > ground.Max.y - 2.0f) ++outside;
		if (std::fabs(s.Position.y - ground.Height(x, z)) > 1e-4f) ++offGround;
		for (const ResourceGround::Blocker& b : ground.Blockers)
			if (glm::distance(b.At, glm::vec2(x, z)) < b.Radius) ++blocked;
		for (size_t b = a + 1; b < spots.size(); ++b)
		{
			const float need = std::max(sp.Spacing, defs.at(spots[b].Node).Spawn.Spacing);
			if (glm::distance(glm::vec2(x, z), glm::vec2(spots[b].Position.x, spots[b].Position.z)) < need - 1e-3f) ++crowded;
		}
	}
	Check(badBiome == 0, "every node is on one of its biomes");
	Check(steep == 0, "no node on ground steeper than its type accepts");
	Check(wet == 0, "no node in the water");
	Check(farFromWater == 0, "clay and reeds keep to the water");
	Check(outside == 0, "no node at the edge of the map");
	Check(offGround == 0, "every node stands on the ground");
	Check(blocked == 0, "no node inside one of the world's trees or rocks");
	Check(crowded == 0, "no two nodes closer than their spacing");
	for (const auto& [id, def] : defs)
		if (def.Spawn.Max > 0 && counts.count(id) && counts.at(id) > def.Spawn.Max) Check(false, "no type over its cap");
	Check(counts.count("old_tree") && counts.count("reed_bed") && counts.count("copper_vein"), "big trees in the forest, reeds on the shore and copper on the rocks");

	// a type with a cap spreads over its ground instead of filling the first rows of the map: plenty of rocky ground (it runs the whole length in z),
	// a dense type capped at 8
	{
		std::map<std::string, ResourceNodeDef> capped = { { "flint_outcrop", defs.at("flint_outcrop") } };
		ResourceSpawnDef& sp = capped["flint_outcrop"].Spawn;
		sp.Biomes = { "Rock" }; sp.PerHectare = 60.0f; sp.Clump = 0.0f; sp.Max = 8;
		const std::vector<ResourceSpot> few = PlanWorldResources(ground, capped, 7);
		float lo = 1e9f, hi = -1e9f;
		for (const ResourceSpot& s : few) { lo = std::min(lo, s.Position.z); hi = std::max(hi, s.Position.z); }
		Check(few.size() == 8, "the cap is reached");
		Check(hi - lo > 100.0f, "and the capped nodes spread over the rocky ground, not along one edge");
	}

	// determinism and variety
	const std::vector<ResourceSpot> again = PlanWorldResources(ground, defs, 7);
	bool same = again.size() == spots.size();
	for (size_t i = 0; same && i < spots.size(); ++i) same = again[i].Node == spots[i].Node && again[i].Position == spots[i].Position && again[i].Yaw == spots[i].Yaw;
	Check(same, "the same ground, data and seed give the same nodes");
	const std::vector<ResourceSpot> other = PlanWorldResources(ground, defs, 8);
	bool differs = other.size() != spots.size();
	for (size_t i = 0; !differs && i < spots.size(); ++i) differs = other[i].Position != spots[i].Position;
	Check(differs, "another seed gives another layout");

	// density
	const std::map<std::string, int> rich = CountSpots(PlanWorldResources(ground, defs, 7, 2.0f));
	const std::map<std::string, int> poor = CountSpots(PlanWorldResources(ground, defs, 7, 0.5f));
	Check(rich.at("tree") > counts.at("tree") && poor.at("tree") < counts.at("tree"), "the density scales the number of nodes");
	Check(PlanWorldResources(ground, defs, 7, 0.0f).empty(), "density 0 places nothing");

	// a type without a spawn stays out; a type with a spawn on a biome the ground lacks gets nothing
	{
		std::map<std::string, ResourceNodeDef> only = { { "tree", defs.at("tree") }, { "rock", defs.at("rock") } };
		only["rock"].Spawn = {};
		only["tree"].Spawn.Biomes = { "Dunes" };
		Check(PlanWorldResources(ground, only, 7).empty(), "no spawn or no matching ground, no nodes");
	}

	// content: the spawn travels through the JSON and bad values are refused
	{
		ItemLibrary i2;
		RecipeBook r2;
		std::map<std::string, ResourceNodeDef> n2;
		const std::string error = LoadContent(ContentToJson(items, recipes, defs), i2, r2, n2);
		Check(error.empty() && n2.at("clay_bank").Spawn.WaterWithin == defs.at("clay_bank").Spawn.WaterWithin && n2.at("copper_vein").Spawn.Max == 5
			&& n2.at("tree").Spawn.Biomes == defs.at("tree").Spawn.Biomes, "the spawn of each node survives a round trip through the content JSON");
		const std::string bad = R"({"nodes":[{"id":"x","item":"wood","spawn":{"biomes":["Jungle"],"perHectare":1}}]})";
		Check(!LoadContent(bad, i2, r2, n2).empty(), "an unknown biome is refused");
		const std::string badClump = R"({"nodes":[{"id":"x","item":"wood","spawn":{"biomes":["Forest"],"perHectare":1,"clump":1.5}}]})";
		Check(!LoadContent(badClump, i2, r2, n2).empty(), "a clump out of 0..0.99 is refused");
	}

	// the nodes built from the plan are real nodes: tags, a name of their own, and a worker can gather from one
	{
		EntityWorld entities;
		const ResourceLayout layout = BuildWorldResources(entities, spots, defs);
		Check(layout.Nodes.size() == spots.size(), "one node per spot");
		Check(entities.FindByName("wild berry bush 1") != NullEntity && entities.FindByName("berry bush 1") == NullEntity, "named apart from the patch's nodes");
		const Entity* first = entities.Find(entities.FindByName(layout.Nodes.front()));
		Check(first && first->Transform.Position == spots.front().Position && first->Gameplay && first->Gameplay->Tags.size() == 3, "placed where planned, with the node tags");
		BuildResourcePatch(entities, glm::vec3(0.0f), defs, 1);
		Check(entities.FindByName("World resources") != NullEntity && entities.FindByName("Resources") != NullEntity, "a patch and the world's nodes live side by side");
		const ResourceLayout again2 = BuildWorldResources(entities, spots, defs);
		int roots = 0;
		for (const auto& [id, e] : entities.All()) if (e.Name == "World resources") ++roots;
		Check(roots == 1 && again2.Nodes.size() == spots.size(), "building again replaces the older set");

		GameplayWorld gameplay;
		RegisterResourceContent(gameplay.Library, defs);
		const EntityId w = entities.Create("Worker");
		entities.Find(w)->Gameplay.emplace();
		entities.Find(w)->Gameplay->Abilities = { "Gather" };
		gameplay.Load(entities);
		Inventory bag(12, 0.0f);
		ResourceSystem rules;
		rules.Bind(gameplay, defs, [&](const std::string& who, const std::string& item, int n) { if (who == "Worker") bag.Add(items, item, n); });
		AbilitySystemComponent* worker = gameplay.Find("Worker");
		Check(worker->Activate("Gather", gameplay.Find("wild berry bush 1"), nullptr), "a worker can start gathering a wild bush");
		for (int i = 0; i < 60 * 20 && worker->AnyActive(); ++i) { gameplay.Update(dt); rules.Update(gameplay, dt); }
		Check(bag.Count("berry") == defs.at("berry_bush").PerGather, "and gets its berries");
	}

	// the patch on a hillside: every node set on the ground under it, not left at the height of the origin
	{
		EntityWorld entities;
		auto hill = [](float x, float z) { return 0.3f * x + 0.1f * z; };
		const glm::vec3 origin(10.0f, hill(10.0f, 5.0f), 5.0f);
		const ResourceLayout layout = BuildResourcePatch(entities, origin, defs, 2);
		SnapToGround(entities, layout, hill);
		int off = 0;
		for (const std::string& n : layout.Nodes)
		{
			const Entity* e = entities.Find(entities.FindByName(n));
			const float x = origin.x + e->Transform.Position.x, z = origin.z + e->Transform.Position.z;
			const float base = origin.y + e->Transform.Position.y;
			if (base > hill(x, z) + 1e-3f || base < hill(x, z) - 0.3f) ++off;   // at most a little into the ground on the downhill side
		}
		Check(off == 0, "a patch on a hillside stands on the ground");
	}

	if (failures == 0) std::fprintf(stderr, "world resources: all checks passed\n");
	return failures;
}
