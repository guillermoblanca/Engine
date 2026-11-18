// T277, T278 and T279: the three pieces the farming and food data stand on (documentation/01_diseno/AGRICULTURA.md §5, ALIMENTACION.md §10.2).
//   tool_recipe (T278, P-F10): BestToolRecipe asks for the tool the clan can make: with no stone and the inputs of another known recipe of a tool
//     of the same family at hand, that one; with stone, the usual (the most powerful); with nothing at hand, the usual too; a recipe to try only
//     when none is known.
//   node_extra_chance (T279, P-F12): a node's "extra" with "chance" 0.1 gives it in about 10 % of the gathers, the same ones with the same node
//     every time; without "chance", in every gather (as before) and with no state on the node; the chance reads, writes and is validated.
//   meal_log (T277, P-A10): a clan that eats counts its meals by food and by source (GB/Game/MealLog.h), with the nutrition the diet gives.
#include "GB/Game/MealLog.h"
#include "GB/Game/Production.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/Social.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include "GB/World/WorldGen.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
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

static int ToolRecipe()
{
	failures = 0;
	ItemLibrary items;
	RecipeBook recipes;
	auto item = [&](const char* id, const char* tag, int power)
	{
		ItemDef d;
		d.Id = id;
		d.Category = tag[0] ? "tool" : "resource";
		if (tag[0]) d.Tags = { tag };
		d.Power = power;
		items.Add(d);
	};
	item("stone", "", 0);
	item("branch", "", 0);
	item("bone", "", 0);
	item("copper_ingot", "", 0);
	item("stone_hoe", "Item.Tool.Hoe", 1);
	item("bone_hoe", "Item.Tool.Hoe.Bone", 0);        // (a sub-tag: it is still a hoe)
	item("copper_hoe", "Item.Tool.Hoe.Copper", 2);
	recipes.Add({ "stone_hoe", "workbench", "", { { "stone", 2 }, { "branch", 2 } }, "stone_hoe", 1, "Craft" });
	recipes.Add({ "bone_hoe", "", "", { { "bone", 2 }, { "branch", 1 } }, "bone_hoe", 1, "Craft" });
	recipes.Add({ "copper_hoe", "forge", "", { { "copper_ingot", 2 }, { "branch", 1 } }, "copper_hoe", 1, "Craft" });

	std::map<std::string, int> have;
	bool knowCopper = false;
	ToolRecipeQuery q;
	q.Known = [&](const Recipe& r) { return r.Id != "copper_hoe" || knowCopper; };
	q.Station = [](const std::string& s) { return s != "forge"; };   // (no forge yet)
	q.Have = [&](const std::string& id) { const auto it = have.find(id); return it == have.end() ? 0 : it->second; };
	auto pick = [&](bool& known) { const Recipe* r = BestToolRecipe(recipes, items, "Item.Tool.Hoe", -1, q, known); return r ? r->Id : std::string("(none)"); };

	bool known = false;
	have = { { "stone", 2 }, { "branch", 2 }, { "bone", 2 } };
	Check(pick(known) == "stone_hoe" && known, "with stone at hand: the stone hoe, as always (the most powerful known)");
	have = { { "stone", 0 }, { "branch", 2 }, { "bone", 2 } };
	Check(pick(known) == "bone_hoe" && known, "with no stone and bone and a branch at hand: the bone hoe, the one it can make (P-F10)");
	have = { { "stone", 1 }, { "branch", 2 }, { "bone", 1 } };
	Check(pick(known) == "stone_hoe", "with the inputs of neither: the most powerful, as before (" + pick(known) + ")");
	have = {};
	{
		ToolRecipeQuery old = q;
		old.Have = nullptr;
		have = { { "branch", 2 }, { "bone", 2 } };
		const Recipe* r = BestToolRecipe(recipes, items, "Item.Tool.Hoe", -1, old, known);
		Check(r && r->Id == "stone_hoe", "without a Have the inputs are not looked at (the old choice)");
	}
	// a better tool (the upgrade: power above the one carried) is only the stone hoe; the copper one waits for its forge
	have = { { "copper_ingot", 4 }, { "branch", 2 }, { "bone", 2 } };
	knowCopper = true;
	Check(BestToolRecipe(recipes, items, "Item.Tool.Hoe", 0, q, known) && BestToolRecipe(recipes, items, "Item.Tool.Hoe", 0, q, known)->Id == "stone_hoe",
		"no forge: the copper hoe is not asked for, whatever is at hand");
	q.Station = nullptr;   // a forge now
	have = { { "branch", 2 }, { "bone", 2 } };
	Check(pick(known) == "bone_hoe", "a known copper hoe with no copper at hand: the bone hoe it can make, not the copper one (" + pick(known) + ")");
	have = { { "copper_ingot", 2 }, { "branch", 2 }, { "bone", 2 } };
	Check(pick(known) == "copper_hoe", "with the copper at hand: the copper hoe (the best it can make)");
	// none known: a recipe to try (E5), the best, not the one with its inputs at hand
	q.Known = [](const Recipe&) { return false; };
	have = { { "bone", 2 }, { "branch", 2 } };
	Check(pick(known) == "copper_hoe" && !known, "nothing known: the best one to try, as before (" + pick(known) + ")");
	return failures ? 1 : 0;
}

static int NodeExtraChance()
{
	failures = 0;
	// the roll itself: deterministic, about the chance
	{
		int falls = 0, again = 0;
		const int n = 4000;
		for (int i = 0; i < n; ++i)
		{
			falls += ResourceSystem::ExtraFalls("berry bush 1", static_cast<uint64_t>(i), 0.1f) ? 1 : 0;
			again += ResourceSystem::ExtraFalls("berry bush 1", static_cast<uint64_t>(i), 0.1f) ? 1 : 0;
		}
		Check(falls == again, "the same node and rolls give the same extras");
		Check(falls > n * 8 / 100 && falls < n * 12 / 100, "chance 0.1: about 10 % (" + std::to_string(falls) + " of " + std::to_string(n) + ")");
		Check(ResourceSystem::ExtraFalls("x", 7, 1.0f) && !ResourceSystem::ExtraFalls("x", 7, 0.0f), "1 always, 0 never");
	}

	// the data: read, written, validated
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> nodes;
		const std::string base = R"({ "items": [ { "id": "berry", "category": "food" }, { "id": "pea_seed" } ], "nodes": [ )";
		Check(LoadContent(base + R"({ "id": "bush", "item": "berry", "extra": { "item": "pea_seed", "perGather": 1, "chance": 0.1 } } ] })", items, recipes, nodes).empty(), "a chance of 0.1 reads");
		Check(nodes.count("bush") && std::abs(nodes["bush"].ExtraChance - 0.1f) < 1e-6f, "and is 0.1");
		const std::string written = ContentToJson(items, recipes, nodes);
		Check(written.find("\"chance\"") != std::string::npos, "it is written");
		std::map<std::string, ResourceNodeDef> back;
		ItemLibrary items2;
		RecipeBook recipes2;
		Check(LoadContent(written, items2, recipes2, back).empty() && std::abs(back["bush"].ExtraChance - 0.1f) < 1e-6f, "and reads back the same");
		std::map<std::string, ResourceNodeDef> plain;
		Check(LoadContent(base + R"({ "id": "tree", "item": "berry", "extra": { "item": "pea_seed" } } ] })", items, recipes, plain).empty() && plain["tree"].ExtraChance == 1.0f, "no chance: 1");
		Check(ContentToJson(items, recipes, plain).find("\"chance\"") == std::string::npos, "and none is written (the old data reads back the same)");
		std::map<std::string, ResourceNodeDef> bad;
		Check(!LoadContent(base + R"({ "id": "b", "item": "berry", "extra": { "item": "pea_seed", "chance": 1.5 } } ] })", items, recipes, bad).empty(), "a chance above 1 is refused");
		Check(!LoadContent(base + R"({ "id": "b", "item": "berry", "extra": { "item": "pea_seed", "chance": -0.1 } } ] })", items, recipes, bad).empty(), "a negative chance is refused");
	}

	// gathering: a bush gathered N times gives its extra about 10 % of them; the same run twice gives the same; without chance, every time
	auto gather = [](float chance, int n, std::string& sequence, bool& counted)
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> defs;
		RegisterEraOneItems(items, recipes, defs);
		defs["berry_bush"].ExtraItem = "fiber";
		defs["berry_bush"].ExtraPerGather = 1;
		defs["berry_bush"].ExtraChance = chance;
		EntityWorld entities;
		GameplayWorld gameplay;
		RegisterResourceContent(gameplay.Library, defs);
		BuildResources(entities, glm::vec3(0.0f), defs);
		const EntityId w = entities.Create("Worker");
		entities.Find(w)->Gameplay.emplace();
		gameplay.Load(entities);
		int extras = 0;
		ResourceSystem rules;
		rules.Bind(gameplay, defs, [&](const std::string&, const std::string& item, int count) { if (item == "fiber") extras += count; });
		AbilitySystemComponent* node = gameplay.Find("berry bush 1");
		AbilitySystemComponent* worker = gameplay.Find("Worker");
		if (!node || !worker) return -1;
		for (int i = 0; i < n; ++i)
		{
			node->Attributes().SetBase("Amount", static_cast<float>(defs["berry_bush"].Capacity));
			const int before = extras;
			node->SendEvent({ "Event.Resource.Gather", worker, node });
			sequence += extras > before ? '1' : '0';
		}
		counted = node->Attributes().Has("ExtraRolls");
		return extras;
	};
	const int n = 2000;
	std::string a, b, c;
	bool countedA = false, countedB = false, countedC = false;
	const int some = gather(0.1f, n, a, countedA);
	const int same = gather(0.1f, n, b, countedB);
	const int every = gather(1.0f, n, c, countedC);
	Check(some > n * 8 / 100 && some < n * 12 / 100, "chance 0.1: the extra in about 10 % of " + std::to_string(n) + " gathers (" + std::to_string(some) + ")");
	Check(a == b, "the same node gathered the same times gives the extra in the same gathers");
	std::printf("node_extra_chance: chance 0.1 gave the extra in %d of %d gathers; chance 1 in %d\n", some, n, every);
	Check(countedA, "the node counts its rolls (attribute ExtraRolls, saved with it)");
	Check(every == n && !countedC, "without a chance: the extra every gather, as before, and nothing kept on the node (" + std::to_string(every) + ")");
	return failures ? 1 : 0;
}

static int MealLogTest()
{
	failures = 0;
	// the pure parts
	{
		Check(MealLog::Source(true, true, "x") == "pantry" && MealLog::Source(false, true, "") == "bag" && MealLog::Source(false, false, "Shed 1") == "store@Shed 1"
			&& MealLog::Source(false, false, "") == "store", "the sources");
		DietRules diet;
		diet.Foods["stew"].Nutrition = 45.0f;
		Check(MealLog::Nutrition(diet, true, 25, "stew") == 45.0f && MealLog::Nutrition(diet, true, 25, "berry") == 25.0f && MealLog::Nutrition(diet, false, 25, "stew") == 25.0f,
			"the nutrition: the diet's, else the need's (and the need's with the social life off, as Tribe::Ate)");
		MealLog log;
		log.Record("stew", "store@Shed 1", 45.0f);
		log.Record("berry", "bag", 25.0f);
		log.Record("berry", "store", 25.0f);
		Check(log.Total().Units == 3 && log.Total().Nutrition == 95.0 && log.ByItem().at("berry").Units == 2 && log.ByItem().at("berry").Nutrition == 50.0, "by food");
		Check(log.BySource().at("store@").Units == 1 && log.BySource().at("bag").Units == 1 && log.BySource().at("store").Units == 1, "by source (the store buildings together)");
	}
	// a clan that eats
	{
		WorldDesc d;
		d.Type = WorldType::Temperate;
		d.Seed = 3;
		d.Resolution = 129;
		const GeneratedWorld world = WorldGenerator::GenerateData(d);
		SimSetup setup;
		setup.World = &world;
		setup.Members = 4;
		SimRun run(setup);
		Check(run.Ok(), "a clan of four" + (run.Ok() ? std::string() : ": " + run.Error()));
		if (!run.Ok()) return 1;
		run.Run(2.0);
		Tribe& t = run.Brain();
		Check(t.Items() && t.Store().Add(*t.Items(), "berry", 40) > 0, "berries for the store");
		for (int i = 0; i < 300 && t.Meals().Total().Units < 4; ++i) run.Run(10.0);   // (until four meals, at most most of a day)
		const MealLog& log = t.Meals();
		for (const auto& [food, n] : log.ByItem()) std::printf("meal_log: %s %ld meals, %.0f nutrition\n", food.c_str(), n.Units, n.Nutrition);
		for (const auto& [source, n] : log.BySource()) std::printf("meal_log: from %s %ld\n", source.c_str(), n.Units);
		long meals = 0;
		for (const TribeMember& m : t.Members()) meals += m.Meals;
		Check(log.Total().Units >= 1, "the hungry clan ate (" + std::to_string(log.Total().Units) + " meals)");
		Check(log.Total().Units == meals, "the log counts every meal the members count (" + std::to_string(log.Total().Units) + " / " + std::to_string(meals) + ")");
		Check(!log.ByItem().empty() && t.IsFood(log.ByItem().begin()->first), "by food: " + (log.ByItem().empty() ? std::string("-") : log.ByItem().begin()->first));
		Check(!log.BySource().empty() && log.Total().Nutrition > 0.0, "with a source and a nutrition");
	}
	return failures ? 1 : 0;
}

// One pool entry, three ctest tests: `GBSimTests food_base <tool_recipe|node_extra_chance|meal_log>` (none: all three)
int GBTest_food_base(int argc, char** argv)
{
	const char* which = argc > 1 ? argv[1] : "";
	int bad = 0;
	if (!*which || !std::strcmp(which, "tool_recipe")) bad |= ToolRecipe();
	if (!*which || !std::strcmp(which, "node_extra_chance")) bad |= NodeExtraChance();
	if (!*which || !std::strcmp(which, "meal_log")) bad |= MealLogTest();
	return bad;
}
