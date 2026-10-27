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
