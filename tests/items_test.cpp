// Items, inventories, resource nodes and recipes (H2): stacking and weight, transfers, save/load of an inventory, regrowing nodes, and a whole
// gather-and-craft loop (chop, mine, pick fibre, make an axe) that gives the same result in two runs. CPU only.
#include "GB/Gameplay/Items.h"
#include <cstdio>
#include <map>
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

struct World
{
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	World() { RegisterEraOneItems(Items, Recipes, Defs); }
};

// Gathers a node dry into the inventory, as an actor with the right tool would.
static int GatherAll(const World& w, ResourceNode& node, Inventory& inv)
{
	int got = 0;
	while (!node.Depleted())
	{
		if (!node.Def().RequiresTag.empty() && inv.CountTagged(w.Items, node.Def().RequiresTag) == 0) break;
		const int n = node.Gather();
		got += inv.Add(w.Items, node.Def().Item, n);
	}
	return got;
}

// The whole loop; returns the final inventory as JSON so two runs can be compared.
static std::string Loop(const World& w, bool& ok)
{
	Inventory inv(12, 60.0f);
	ResourceNode tree(w.Defs.at("tree")), rock(w.Defs.at("rock")), grass(w.Defs.at("fiber_grass")), tree2(w.Defs.at("tree"));

	// No axe yet: the tree cannot be gathered, the grass can.
	ok = ok && GatherAll(w, tree, inv) == 0;
	ok = ok && GatherAll(w, grass, inv) == 8;
	// Hands are not enough for rock either; a workbench recipe needs wood and stone first, so the first axe is made from found pieces.
	inv.Add(w.Items, "wood", 4);
	inv.Add(w.Items, "stone", 5);
	ok = ok && !w.Recipes.Craft(w.Items, inv, "stone_axe", "");          // wrong place
	ok = ok && w.Recipes.Craft(w.Items, inv, "stone_axe", "workbench");
	ok = ok && inv.Count("stone_axe") == 1 && inv.Count("wood") == 2 && inv.Count("fiber") == 5;

	ok = ok && GatherAll(w, tree, inv) == 5;                              // the axe opens the tree
	ok = ok && w.Recipes.Craft(w.Items, inv, "stone_pickaxe", "workbench");
	ok = ok && GatherAll(w, rock, inv) == 4;
	ok = ok && w.Recipes.Craft(w.Items, inv, "campfire", "");
	ok = ok && inv.Count("campfire") == 1;

	// A tree regrows one wood per 30 s.
	for (int i = 0; i < 31; ++i) tree.Update(1.0f);
	ok = ok && tree.Amount() == 1;
	(void)tree2;
	return inv.ToJson();
}

int main()
{
	World w;

	// Stacking, slots and weight.
	{
		Inventory inv(2, 0.0f);
		Check(inv.Add(w.Items, "wood", 25) == 20, "two slots of ten hold twenty wood");
		Check(inv.UsedSlots() == 2 && inv.Count("wood") == 20, "wood fills the stacks");
		Check(inv.Add(w.Items, "stone", 1) == 0, "no empty slot left");
		Check(inv.Remove("wood", 13) == 13 && inv.Count("wood") == 7 && inv.UsedSlots() == 1, "removing frees emptied stacks");
		Check(inv.Remove("wood", 99) == 7 && inv.UsedSlots() == 0, "removing more than held takes what there is");
		Check(inv.Add(w.Items, "nonsense", 1) == 0, "unknown items are refused");

		Inventory heavy(10, 10.0f);   // stone weighs 3
		Check(heavy.Add(w.Items, "stone", 9) == 3, "the weight limit caps the units");
		Check(heavy.Weight(w.Items) > 8.9f && heavy.Weight(w.Items) < 9.1f, "weight adds up");
	}

	// Tags and transfers.
	{
		Inventory a(4), b(1);
		a.Add(w.Items, "stone_axe", 1);
		a.Add(w.Items, "stone", 5);
		Check(a.CountTagged(w.Items, "Item.Tool") == 1, "a tool tag matches its descendants");
		Check(a.CountTagged(w.Items, "Item.Tool.Pickaxe") == 0, "other tools do not match");
		Check(a.Transfer(w.Items, b, "stone", 5) == 5 && b.Count("stone") == 5 && a.Count("stone") == 0, "transfer moves units");
		Check(a.Transfer(w.Items, b, "stone_axe", 1) == 0 && a.Count("stone_axe") == 1, "a full target takes nothing and the source keeps it");
	}

	// Save and load.
	{
		Inventory a(6, 40.0f);
		a.Add(w.Items, "wood", 14);
		a.Add(w.Items, "stone_axe", 1);
		Inventory b;
		Check(b.FromJson(a.ToJson()), "an inventory reads back");
		Check(b.ToJson() == a.ToJson() && b.Count("wood") == 14 && b.Slots() == 6, "and is identical");
		Inventory c(3);
		Check(!c.FromJson("{nope") && c.Slots() == 3, "malformed text is refused without changes");
		Check(!c.FromJson("{\"items\":[[\"wood\"]]}"), "a bad entry is refused");
	}

	// Nodes: depletion, regrowth, no regrowth.
	{
		ResourceNode bush(w.Defs.at("berry_bush"));
		Check(bush.Gather() == 2 && bush.Amount() == 4, "a gather takes PerGather units");
		bush.Update(19.0f);
		Check(bush.Amount() == 4, "not regrown yet");
		bush.Update(1.0f);
		Check(bush.Amount() == 6, "regrows after its time");
		ResourceNode rock(w.Defs.at("rock"));
		while (!rock.Depleted()) rock.Gather();
		rock.Update(1000.0f);
		Check(rock.Depleted() && rock.Gather() == 0, "a rock never regrows");
	}

	// Recipes explain themselves.
	{
		Inventory inv(8);
		Check(w.Recipes.WhyNot(w.Items, inv, "campfire", "").find("tool") != std::string::npos, "the missing tool is named");
		inv.Add(w.Items, "stone_axe", 1);
		Check(w.Recipes.WhyNot(w.Items, inv, "campfire", "").find("missing 3 stone") != std::string::npos, "the missing input is counted");
		Check(w.Recipes.WhyNot(w.Items, inv, "ghost", "").find("unknown") != std::string::npos, "unknown recipes are reported");
		Inventory full(1);
		full.Add(w.Items, "wood", 4);
		full.Add(w.Items, "stone", 1);   // does not fit: one slot only
		Check(full.Count("stone") == 0, "a one-slot bag is full");
	}

	// Content as data: export what the code defines, read it back, and the same things come out; mistakes are named and change nothing.
	{
		const std::string text = ContentToJson(w.Items, w.Recipes, w.Defs);
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> nodes;
		Check(LoadContent(text, items, recipes, nodes).empty(), "the exported content loads");
		Check(ContentToJson(items, recipes, nodes) == text, "and exports the same again");
		Check(items.Ids().size() == w.Items.Ids().size() && recipes.Ids().size() == w.Recipes.Ids().size() && nodes.size() == w.Defs.size(), "with all its items, recipes and nodes");

		// a new thing is data only: a flint knife made from stone and fibre, and a flint outcrop that gives stone
		const std::string extra = R"({ "items": [ { "id": "flint", "name": "Flint", "category": "resource", "weight": 0.5, "stack": 20, "tags": ["Item.Resource.Stone"] },
			{ "id": "knife", "name": "Flint knife", "category": "tool", "stack": 1, "tags": ["Item.Tool.Knife"] } ],
			"recipes": [ { "id": "knife", "station": "workbench", "inputs": { "flint": 2, "fiber": 1 }, "output": "knife" } ],
			"nodes": [ { "id": "flint_outcrop", "item": "flint", "capacity": 3, "perGather": 1 } ] })";
		Check(LoadContent(extra, items, recipes, nodes).empty(), "new items, recipes and nodes load on top of the others");
		Inventory bag(6);
		bag.Add(items, "flint", 2);
		bag.Add(items, "fiber", 1);
		Check(recipes.Craft(items, bag, "knife", "workbench") && bag.Count("knife") == 1, "and the new recipe works");
		Check(items.Find("wood") != nullptr && nodes.count("flint_outcrop") == 1, "what was there stays");

		const std::string before = ContentToJson(items, recipes, nodes);
		Check(LoadContent("{nope", items, recipes, nodes).find("JSON") != std::string::npos, "text that is not JSON is refused");
		Check(LoadContent(R"({ "items": [ { "id": "x", "stack": 0 } ] })", items, recipes, nodes).find("stack") != std::string::npos, "a stack of zero is refused");
		Check(LoadContent(R"({ "recipes": [ { "id": "r", "inputs": { "ghost": 1 }, "output": "wood" } ] })", items, recipes, nodes).find("ghost") != std::string::npos, "a recipe that needs an unknown item is refused");
		Check(LoadContent(R"({ "nodes": [ { "id": "n", "item": "ghost" } ] })", items, recipes, nodes).find("ghost") != std::string::npos, "a node that gives an unknown item is refused");
		Check(LoadContent(R"({ "items": [ { "id": "ok1" }, { "id": "bad", "stack": -1 } ] })", items, recipes, nodes).size() > 0 && items.Find("ok1") == nullptr, "a document with a mistake changes nothing at all");
		Check(ContentToJson(items, recipes, nodes) == before, "nothing changed after the refusals");

		// hostile or sloppy documents: an error message, never an exception, and nothing changes
		const char* bad[] = {
			R"({ "items": 5 })", R"({ "items": [ 5 ] })", R"({ "items": [ { "id": 7 } ] })", R"({ "items": [ { "id": "w", "weight": "heavy" } ] })",
			R"({ "items": [ { "id": "w", "tags": [ 1 ] } ] })", R"({ "items": [ { "id": "w", "tags": "Item.X" } ] })", R"({ "items": [ { "id": "w", "weight": -2 } ] })",
			R"({ "recipes": [ { "id": "r", "output": "wood", "inputs": { "wood": "two" } } ] })", R"({ "recipes": [ { "id": "r", "output": "wood", "inputs": { "wood": 0 } } ] })",
			R"({ "recipes": [ { "id": "r", "output": "wood", "count": 0 } ] })", R"({ "recipes": [ { "id": "r", "inputs": { "wood": 1 } } ] })",
			R"({ "nodes": [ { "id": "n", "item": "wood", "capacity": "big" } ] })", R"({ "nodes": [ { "id": "n", "item": "wood", "capacity": 0 } ] })",
			R"({ "nodes": [ { "id": "n", "item": "wood", "regenSeconds": -1 } ] })", R"({ "nodes": [ { "item": "wood" } ] })", R"([])", R"(null)", ""
		};
		for (const char* text : bad)
		{
			std::string answer;
			bool threw = false;
			try { answer = LoadContent(text, items, recipes, nodes); }
			catch (...) { threw = true; }
			if (threw || answer.empty()) std::fprintf(stderr, "  (accepted or crashed on: %s)\n", text);
			Check(!threw && !answer.empty(), "a malformed content document is refused with a message");
		}
		Check(ContentToJson(items, recipes, nodes) == before, "and none of them changed anything");

		// an inventory file that is damaged is refused whole (and never throws)
		Inventory keep(5, 0.0f);
		keep.Add(w.Items, "wood", 3);
		const std::string bagBefore = keep.ToJson();
		const char* badBags[] = {
			R"({ "items": [ [ "wood", 0 ] ] })", R"({ "items": [ [ "wood", -4 ] ] })", R"({ "items": [ [ "wood", 1.5 ] ] })", R"({ "items": [ [ "wood" ] ] })",
			R"({ "items": [ [ 3, 3 ] ] })", R"({ "items": [], "slots": "many" })", R"({ "items": [], "maxWeight": "heavy" })", R"({ "items": [], "slots": -1 })",
			R"({ "items": [], "maxWeight": -5 })", R"({ "items": {} })", "[]", "{", ""
		};
		for (const char* text : badBags)
		{
			bool threw = false, accepted = false;
			try { accepted = keep.FromJson(text); }
			catch (...) { threw = true; }
			if (threw || accepted) std::fprintf(stderr, "  (accepted or crashed on: %s)\n", text);
			Check(!threw && !accepted, "a damaged inventory is refused");
		}
		Check(keep.ToJson() == bagBefore, "and the inventory it was loaded into is unchanged");

		// the content file of the game says the same as the code
		ItemLibrary fileItems;
		RecipeBook fileRecipes;
		std::map<std::string, ResourceNodeDef> fileNodes;
		const std::string error = LoadContentFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", fileItems, fileRecipes, fileNodes);
		Check(error.empty(), "assets/data/era1.json loads");
		Check(ContentToJson(fileItems, fileRecipes, fileNodes) == text, "and defines what RegisterEraOneItems defines");
	}

	// The extended era-1 proposal (not loaded by default) loads cleanly on top of the base content and leaves it untouched.
	{
		ItemLibrary ei;
		RecipeBook er;
		std::map<std::string, ResourceNodeDef> en;
		RegisterEraOneItems(ei, er, en);
		const size_t baseItems = ei.Ids().size(), baseNodes = en.size();
		const std::string error = LoadContentFile(std::string(GB_ASSETS_DIR) + "/data/era1_ampliado.json", ei, er, en);
		Check(error.empty(), "assets/data/era1_ampliado.json loads on top of the base content");
		Check(ei.Ids().size() > baseItems && en.size() > baseNodes, "the extended content adds items and nodes");
		Check(ei.Find("stone_axe") && ei.Find("stone_axe")->Tags.size() == 1 && en["tree"].Capacity == 5, "and does not change the base definitions");
		// a tool variant still satisfies the generic tool requirement (tags are hierarchical)
		Inventory bag(8);
		bag.Add(ei, "flint_axe", 1);
		Check(bag.CountTagged(ei, "Item.Tool.Axe") == 1, "a flint axe counts as an axe");
		// the tool chain is reachable from nodes: branch + flint + fibre -> cordage -> flint axe at the workbench
		Inventory mats(12);
		mats.Add(ei, "fiber", 3); mats.Add(ei, "flint", 2); mats.Add(ei, "branch", 2);
		Check(er.Craft(ei, mats, "cordage", "") && er.Craft(ei, mats, "flint_axe", "workbench") && mats.Count("flint_axe") == 1, "cordage and a flint axe can be crafted");
	}

	// Tool levels and containers (E2).
	{
		const ItemDef* flint = w.Items.Find("flint_axe");
		const ItemDef* stone = w.Items.Find("stone_axe");
		Check(flint && stone && flint->Power > stone->Power && flint->Speed > stone->Speed, "a flint axe is stronger and faster than a stone one");
		Inventory tools(8);
		tools.Add(w.Items, "stone_axe", 1);
		tools.Add(w.Items, "flint_axe", 1);
		Check(tools.BestTool(w.Items, "Item.Tool.Axe") == flint && tools.BestTool(w.Items, "Item.Tool.Pickaxe") == nullptr, "the best tool of a family is found through the tag hierarchy");
		Check(ToolFamily("Item.Tool.Axe.Flint") == "Item.Tool.Axe" && ToolFamily("Item.Tool.Axe") == "Item.Tool.Axe", "a tool family is three tag segments");
		Inventory flintOnly(4);
		flintOnly.Add(w.Items, "flint_axe", 1);
		flintOnly.Add(w.Items, "wood", 4);
		flintOnly.Add(w.Items, "stone", 3);
		Check(w.Recipes.Craft(w.Items, flintOnly, "campfire", "") && flintOnly.Count("campfire") == 1, "a recipe that asks for an axe accepts a flint axe");

		// The basket: bigger bag; only the best container counts; the bonus goes when the basket does.
		Inventory bag(4, 10.0f);
		Check(bag.Slots() == 4 && bag.MaxWeight() == 10.0f, "a plain bag");
		bag.Add(w.Items, "basket_wicker", 1);
		bag.SyncCapacity(w.Items);
		Check(bag.Slots() == 10 && bag.MaxWeight() == 18.0f && bag.BestContainer(w.Items)->Id == "basket_wicker", "a basket adds six slots and 8 kg");
		Check(bag.Add(w.Items, "stone", 5) == 5 && bag.UsedSlots() == 2, "and the room can be used");
		ItemDef big = *w.Items.Find("basket_wicker");
		big.Id = "big_basket"; big.BagSlots = 2; big.BagWeight = 1.0f;
		ItemLibrary lib2 = w.Items;
		lib2.Add(big);
		bag.Add(lib2, "big_basket", 1);
		bag.SyncCapacity(lib2);
		Check(bag.Slots() == 10, "two containers do not add up: the better one counts");
		// capacity survives a save: stacks are what is stored, the bonus is derived again after loading
		Inventory loaded(4, 10.0f);
		loaded.SetStacks(bag.Stacks());
		Check(loaded.Slots() == 4, "before syncing, a loaded bag has only its own capacity");
		loaded.SyncCapacity(lib2);
		Check(loaded.Slots() == bag.Slots() && loaded.MaxWeight() == bag.MaxWeight(), "after a load the basket gives its room back");
		// the JSON keeps the bonus too, and an older text without it still loads
		Inventory fromJson(4, 10.0f);
		Check(fromJson.FromJson(bag.ToJson()) && fromJson.Slots() == bag.Slots() && fromJson.MaxWeight() == bag.MaxWeight(), "a bag with a basket round-trips as JSON");
		Check(fromJson.FromJson(R"({"slots":5,"maxWeight":0,"items":[["wood",1]]})") && fromJson.Slots() == 5 && fromJson.BonusSlots() == 0, "an older inventory text loads without a bonus");
		Check(!fromJson.FromJson(R"({"items":[],"bonusSlots":-1})"), "a negative bonus is refused");
		// dropping the basket: contents stay, nothing new goes in
		bag.Remove("basket_wicker", 1);
		bag.Remove("big_basket", 1);
		bag.SyncCapacity(w.Items);
		Check(bag.Slots() == 4 && bag.Count("stone") == 5, "without the basket the bag shrinks and keeps what it held");
		// an unlimited bag stays unlimited
		Inventory unlimited(4, 0.0f);
		unlimited.Add(w.Items, "basket_wicker", 1);
		unlimited.SyncCapacity(w.Items);
		Check(unlimited.Slots() == 10 && unlimited.MaxWeight() == 0.0f, "a bag without a weight limit keeps having none");
		Check(CapacityText(w.Items, unlimited).find("4 + 6 from Wicker basket") != std::string::npos && ActiveTools(w.Items, tools).size() == 1, "the editor texts describe the bag and the tool");

		// the data file: loading rejects nonsense in the new fields
		ItemLibrary li; RecipeBook lr; std::map<std::string, ResourceNodeDef> ln;
		Check(!LoadContent(R"({"items":[{"id":"x","power":-1}]})", li, lr, ln).empty() && !LoadContent(R"({"items":[{"id":"x","speed":0}]})", li, lr, ln).empty()
			&& !LoadContent(R"({"items":[{"id":"x","bagSlots":-2}]})", li, lr, ln).empty(), "negative power, zero speed and negative bag slots are refused");
	}

	// The whole loop, twice.
	{
		bool ok1 = true, ok2 = true;
		const std::string r1 = Loop(w, ok1), r2 = Loop(w, ok2);
		Check(ok1, "the gather-and-craft loop reaches every step");
		Check(ok2 && r1 == r2, "two runs end with the same inventory");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("items: ok");
	return 0;
}
