// Forests and the wild nodes (docs/BOSQUES.md): the nature config of assets/data/bosques.json loads, round-trips and refuses mistakes; the patches (forests,
// stony ground, flower beds) are planned the same for the same seed, within the budget, with room between the nodes and away from the water and the world's own
// trees; every node has a capacity and a procedural model of the world; the nodes without a tool are slower than the ones with; the grass of a generated world
// fades from one zone to the next. CPU only.
#include "GB/Gameplay/WorldResources.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <chrono>
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
}

using namespace GB;

// 240 x 240 m: forest to the west, meadow in the middle, rocky ground to the east, a lake along the north edge.
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
	for (int k = 0; k < 40; ++k)
		g.Blockers.push_back({ glm::vec2(-110.0f + 7.0f * static_cast<float>(k % 10), -100.0f + 20.0f * static_cast<float>(k / 10)), 1.5f });
	g.TreeVariants = 6;
	g.RockVariants = 4;
	return g;
}

static bool SameSpots(const std::vector<ResourceSpot>& a, const std::vector<ResourceSpot>& b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i].Node != b[i].Node || a[i].Position != b[i].Position || a[i].Yaw != b[i].Yaw || a[i].Scale != b[i].Scale || a[i].Variant != b[i].Variant || a[i].Stack != b[i].Stack) return false;
	return true;
}

int main()
{
	const std::string data = std::string(GB_ASSETS_DIR) + "/data";

	// ---- the content: era 1 and the forest pack
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> defs;
	RegisterEraOneItems(items, recipes, defs);
	Check(LoadContentFile(data + "/era1.json", items, recipes, defs).empty(), "era1.json loads");
	const std::string contentError = LoadContentFile(data + "/era1_bosque.json", items, recipes, defs);
	if (!contentError.empty()) std::fprintf(stderr, "%s\n", contentError.c_str());
	Check(contentError.empty(), "era1_bosque.json loads as content");
	for (const char* id : { "tree", "old_tree", "young_tree", "rock", "rock_small", "boulder", "rock_pile", "loose_stones", "fallen_branch", "mushroom_patch", "wild_roots",
		"hazelnut_bush", "bird_nest", "flowers_red", "flowers_white" })
		Check(defs.count(id) == 1, id);
	Check(defs["tree"].Spawn.Prop == "tree" && defs["rock"].Spawn.Prop == "rock", "trees and rocks are drawn with the procedural models");
	Check(defs["boulder"].Spawn.ScaleMin > defs["rock"].Spawn.ScaleMax && defs["rock_small"].Spawn.ScaleMax < defs["rock"].Spawn.ScaleMin, "rocks come in several sizes");
	Check(defs["rock_pile"].Spawn.Stack >= 2, "rocks pile up");
	Check(defs["flowers_red"].Decor && !defs["tree"].Decor, "flowers are scenery");
	// content round trip: the new fields survive
	{
		ItemLibrary i2; RecipeBook r2; std::map<std::string, ResourceNodeDef> d2;
		Check(LoadContent(ContentToJson(items, recipes, defs), i2, r2, d2).empty(), "the content with the new fields loads again");
		Check(d2["rock_pile"].Spawn.Stack == defs["rock_pile"].Spawn.Stack && d2["flowers_red"].Decor && d2["tree"].Spawn.Prop == "tree" && d2["tree"].Spawn.ScaleMax == defs["tree"].Spawn.ScaleMax,
			"and keeps the prop, the scale, the stack and the decor");
	}
	// without a tool: slower and poorer than with one (the yield of a hand is the base; a tool adds its power)
	for (const char* id : { "loose_stones", "fallen_branch", "mushroom_patch", "wild_roots", "hazelnut_bush", "bird_nest" })
		Check(defs[id].RequiresTag.empty() && defs[id].RegenSeconds >= 200.0f && defs[id].Capacity <= 6, id);
	Check(defs["loose_stones"].PerGather <= defs["rock"].PerGather && defs["loose_stones"].Capacity < defs["rock"].Capacity && defs["loose_stones"].Item == "stone",
		"loose stones give stone by hand, fewer and slower than a rock with a pickaxe");
	Check(items.Find("mushroom") && items.Find("root") && items.Find("nut") && items.Find("egg"), "the wild foods exist");

	// ---- the config
	NatureConfig cfg;
	const std::string cfgError = LoadNatureConfigFile(data + "/bosques.json", cfg);
	if (!cfgError.empty()) std::fprintf(stderr, "%s\n", cfgError.c_str());
	Check(cfgError.empty(), "bosques.json loads");
	Check(cfg.Abundance > 1.0f && cfg.Abundance <= 10.0f, "the world starts with abundance (above the 1.0 of the node data)");
	Check(cfg.Patches.size() >= 4 && cfg.MaxNodes > 0, "the config has patches and a budget");
	for (const NaturePatchDef& p : cfg.Patches)
		for (const NaturePatchMix& m : p.Mix)
			Check(defs.count(m.Node) == 1, "every node of a patch mix exists");
	{
		NatureConfig again;
		Check(ParseNatureConfig(NatureConfigToJson(cfg), again).empty() && again == cfg, "the config round-trips through its JSON");
		NatureConfig keep = cfg;
		Check(!ParseNatureConfig("{ \"abundance\": 0 }", keep).empty() && keep == cfg, "abundance 0 is refused and nothing changes");
		Check(!ParseNatureConfig("{ \"patches\": [ { \"id\": \"x\", \"biomes\": { \"Moon\": 1 }, \"mix\": [ { \"node\": \"tree\" } ] } ] }", keep).empty(), "an unknown biome is refused");
		Check(!ParseNatureConfig("{ \"patches\": [ { \"id\": \"x\", \"biomes\": { \"Forest\": 1 }, \"mix\": [] } ] }", keep).empty(), "an empty mix is refused");
		Check(!ParseNatureConfig("{ \"patches\": [ { \"id\": \"x\", \"biomes\": { \"Forest\": 1 }, \"spacing\": 0.1, \"mix\": [ { \"node\": \"tree\" } ] } ] }", keep).empty(), "a spacing out of range is refused");
		Check(!ParseNatureConfig("not json", keep).empty() && keep == cfg, "text that is not JSON is refused");
	}

	// ---- the plan
	const ResourceGround ground = Synthetic();
	const auto t0 = std::chrono::steady_clock::now();
	const std::vector<ResourceSpot> spots = PlanWorldNature(ground, defs, cfg, 7);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	const std::map<std::string, int> counts = CountSpots(spots);
	std::fprintf(stderr, "info: %zu spots in %.1f ms:", spots.size(), ms);
	for (const auto& [id, n] : counts) std::fprintf(stderr, " %s %d", id.c_str(), n);
	std::fprintf(stderr, "\n");

	Check(SameSpots(spots, PlanWorldNature(ground, defs, cfg, 7)), "the same seed plans the same nodes, in the same order");
	Check(!SameSpots(spots, PlanWorldNature(ground, defs, cfg, 8)), "another seed plans other nodes");
	int wood = 0, flowers = 0, small = 0, big = 0, piles = 0;
	for (const auto& [id, n] : counts)
	{
		if (id == "tree" || id == "old_tree" || id == "young_tree") wood += n;
		if (id.rfind("flowers_", 0) == 0) flowers += n;
	}
	small = counts.count("rock_small") ? counts.at("rock_small") : 0;
	big = counts.count("boulder") ? counts.at("boulder") : 0;
	piles = counts.count("rock_pile") ? counts.at("rock_pile") : 0;
	// the forest is 80 x 240 m = 1.9 ha: at abundance 2 it must be thick (tens of trees per hectare at least) and still inside the budget
	Check(wood >= 150, "the forest has a lot of trees (a patch is thick)");
	Check(static_cast<int>(spots.size()) <= cfg.MaxNodes + 2000, "the number of nodes stays within reach of the budget");
	{
		int patchOnly = 0;
		for (const ResourceSpot& s : spots)
			if (s.Node.rfind("flowers_", 0) == 0 || s.Node == "rock_small" || s.Node == "boulder" || s.Node == "rock_pile" || s.Node == "loose_stones") ++patchOnly;
		Check(patchOnly <= cfg.MaxNodes, "the nodes the patches add never pass maxNodes");
	}
	Check(flowers > 0 && small > 0 && big > 0 && piles > 0, "flowers, small rocks, boulders and piles of rocks are there");
	{
		NatureConfig poor = cfg; poor.Abundance = 0.5f;
		NatureConfig rich = cfg; rich.Abundance = 4.0f;
		const size_t few = PlanWorldNature(ground, defs, poor, 7).size(), many = PlanWorldNature(ground, defs, rich, 7).size();
		Check(few < spots.size() && spots.size() < many, "the abundance scales the number of nodes");
		NatureConfig none = cfg; none.Patches.clear();
		Check(SameSpots(PlanWorldNature(ground, defs, none, 7), PlanWorldResources(ground, defs, 7, cfg.Abundance)), "without patches it is the old plan at that density");
		NatureConfig capped = cfg; capped.MaxNodes = 100;
		int added = 0;
		const auto base = PlanWorldResources(ground, defs, 7, cfg.Abundance);
		added = static_cast<int>(PlanWorldNature(ground, defs, capped, 7).size()) - static_cast<int>(base.size());
		Check(added >= 0 && added <= 100, "maxNodes caps what the patches add");
	}

	// every node: inside the map, on the ground, dry, sized by its data, with a model of the world, and with room
	bool inside = true, onGround = true, dry = true, scaled = true, variants = true, stacks = true, hasRadius = true;
	for (const ResourceSpot& s : spots)
	{
		const ResourceNodeDef& def = defs.at(s.Node);
		inside = inside && s.Position.x >= ground.Min.x && s.Position.x <= ground.Max.x && s.Position.z >= ground.Min.y && s.Position.z <= ground.Max.y;
		onGround = onGround && std::fabs(s.Position.y - ground.Height(s.Position.x, s.Position.z)) < 1e-3f;
		dry = dry && ground.Biome(s.Position.x, s.Position.z) != "Water" && ground.WaterDistance(s.Position.x, s.Position.z) >= 1.0f;
		if (!def.Spawn.Prop.empty())
		{
			scaled = scaled && s.Scale >= def.Spawn.ScaleMin - 1e-4f && s.Scale <= def.Spawn.ScaleMax + 1e-4f;
			const int n = def.Spawn.Prop == "tree" ? ground.TreeVariants : ground.RockVariants;
			variants = variants && s.Variant >= 0 && s.Variant < n;
			stacks = stacks && s.Stack >= 0 && s.Stack <= def.Spawn.Stack;
		}
		hasRadius = hasRadius && s.Radius >= 1.0f;
	}
	Check(inside, "every node is inside the map");
	Check(onGround, "every node is on the ground");
	Check(dry, "no node is in the water");
	Check(scaled, "every model is sized within its data");
	Check(variants, "every model is one of the world's trees or rocks");
	Check(stacks, "no pile is taller than its data allows");
	Check(hasRadius, "every node asked for room");
	{
		bool room = true;
		for (size_t i = 0; i < spots.size() && room; ++i)
			for (size_t j = i + 1; j < spots.size(); ++j)
			{
				const float dx = spots[i].Position.x - spots[j].Position.x, dz = spots[i].Position.z - spots[j].Position.z;
				const float need = std::max(spots[i].Radius, spots[j].Radius) - 1e-3f;
				if (dx * dx + dz * dz < need * need) { room = false; break; }
			}
		Check(room, "no two nodes are closer than the room the larger of them asked for");
		bool clear = true;
		for (const ResourceSpot& s : spots)
			for (const ResourceGround::Blocker& b : ground.Blockers)
				if (glm::distance(glm::vec2(s.Position.x, s.Position.z), b.At) < b.Radius - 1e-3f) clear = false;
		Check(clear, "no node is inside a tree or a rock of the world");
	}
	// forests are groups: the trees are clustered (far more of them close to each other than a uniform scatter would give) and there are glades
	{
		int near = 0, total = 0;
		for (const ResourceSpot& s : spots)
		{
			if (s.Node != "tree") continue;
			++total;
			int others = 0;
			for (const ResourceSpot& o : spots)
				if (&o != &s && (o.Node == "tree" || o.Node == "old_tree" || o.Node == "young_tree") && glm::distance(s.Position, o.Position) < 8.0f) ++others;
			if (others >= 3) ++near;
		}
		std::fprintf(stderr, "info: %d of %d trees have 3 or more neighbours within 8 m\n", near, total);
		Check(total > 0 && near * 5 >= total, "trees stand in groups (about twice as many with 3 neighbours as a uniform scatter of the same density)");
	}

	// ---- the nodes in a scene: capacity, models, scenery
	{
		EntityWorld world;
		const ResourceLayout layout = BuildWorldResources(world, spots, defs);
		Check(layout.Nodes.size() == spots.size(), "every spot became a node");
		int gather = 0, props = 0, decor = 0, badCapacity = 0, treeWithStump = 0, trees = 0;
		for (const auto& [id, e] : world.All())
		{
			if (e.Name.rfind("wild ", 0) != 0 || e.Parent == NullEntity || world.Find(e.Parent)->Name != layout.Root) continue;
			if (e.Gameplay)
			{
				++gather;
				bool amount = false;
				for (const auto& a : e.Gameplay->Attributes) if (a.Name == "Amount" && a.Value > 0.0f && a.Value == a.Max) amount = true;
				if (!amount) ++badCapacity;
			}
			else ++decor;
			if (e.Name.rfind("wild tree ", 0) == 0 || e.Name.rfind("wild old tree ", 0) == 0 || e.Name.rfind("wild young tree ", 0) == 0)
			{
				++trees;
				bool crown = false, trunk = false;
				for (const EntityId c : e.Children)
				{
					const Entity* part = world.Find(c);
					if (part->Prop && part->Prop->Kind == PropKind::Tree && part->Name.find("Crown") != std::string::npos) crown = true;
					if (part->Mesh && part->Name.find("Trunk") != std::string::npos) trunk = true;
				}
				if (crown && trunk) ++treeWithStump;
			}
			for (const EntityId c : e.Children) if (world.Find(c)->Prop) ++props;
		}
		Check(gather > 0 && badCapacity == 0, "every node you can work has its capacity");
		Check(decor == flowers && decor > 0, "the flowers have no gameplay");
		Check(trees > 0 && treeWithStump == trees, "every tree is a procedural model with a stump that stays when it is felled");
		Check(props >= wood, "the models are props");
		std::fprintf(stderr, "info: %zu entities for %zu nodes (%d props)\n", world.Count(), spots.size(), props);
	}

	// ---- a generated world
	{
		WorldDesc desc;
		desc.Type = WorldType::Temperate;
		desc.Seed = 5;
		const auto g0 = std::chrono::steady_clock::now();
		GeneratedWorld w = WorldGenerator::GenerateData(desc);
		const double genMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g0).count();
		const ResourceGround real = ResourceGroundOf(w);
		Check(real.TreeVariants > 0 && real.RockVariants > 0, "the world gives its tree and rock recipes to the planner");
		const auto p0 = std::chrono::steady_clock::now();
		const std::vector<ResourceSpot> a = PlanWorldNature(real, defs, cfg, desc.Seed);
		const double planMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p0).count();
		const std::map<std::string, int> c = CountSpots(a);
		std::fprintf(stderr, "info: temperate 5: generate %.0f ms, plan %.0f ms, %zu nodes (+%zu trees of the world):", genMs, planMs, a.size(), w.Trees.size());
		for (const auto& [id, n] : c) std::fprintf(stderr, " %s %d", id.c_str(), n);
		std::fprintf(stderr, "\n");
		Check(SameSpots(a, PlanWorldNature(real, defs, cfg, desc.Seed)), "the plan of a generated world is the same twice");
		const size_t before = PlanWorldResources(real, defs, desc.Seed, 1.0f).size();
		Check(a.size() > 2 * before, "a world with forests starts with far more nodes than one with the old plan");
		bool wet = false;
		for (const ResourceSpot& s : a) if (w.BiomeAt(s.Position.x, s.Position.z) == Biome::Water) wet = true;
		Check(!wet, "no node is in the water of a generated world");

		// the grass fades between zones: neighbours 1 m apart on dry ground never differ much (a hard border between a meadow and a rock would jump by 0.88)
		float worst = 0.0f, lo = 1.0f, hi = 0.0f;
		int middle = 0;
		const Heightmap& hm = w.TerrainRef->GetHeightmap();
		for (float z = hm.GetMin().y + 2.0f; z < hm.GetMin().y + hm.GetSize() - 2.0f; z += 1.0f)
			for (float x = hm.GetMin().x + 2.0f; x < hm.GetMin().x + hm.GetSize() - 2.0f; x += 1.0f)
			{
				if (w.BiomeAt(x, z) == Biome::Water || w.BiomeAt(x + 1.0f, z) == Biome::Water) continue;
				const float d0 = w.GrassDensityAt(x, z), d1 = w.GrassDensityAt(x + 1.0f, z);
				worst = std::max(worst, std::fabs(d0 - d1));
				lo = std::min(lo, d0); hi = std::max(hi, d0);
				if (d0 > 0.3f && d0 < 0.9f && w.BiomeAt(x, z) == Biome::Meadow) ++middle;
			}
		std::fprintf(stderr, "info: grass: steepest change per metre %.3f, range %.2f..%.2f, %d meadow samples in between\n", worst, lo, hi, middle);
		Check(worst < 0.2f, "the grass fades from one zone to the next without a hard edge");
		Check(hi > 0.9f && lo < 0.5f, "and still covers the meadows and thins out elsewhere");
	}

	std::fprintf(stderr, failures ? "forest: %d FAILED\n" : "forest: all passed\n", failures);
	return failures ? 1 : 0;
}
