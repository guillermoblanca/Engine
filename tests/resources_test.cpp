// Resource nodes in the world (H2): a worker with the Chop, Mine and Gather abilities gathers from tree, rock, bush and grass entities, needs the right
// tool for wood and stone, empties nodes, sees them regrow, and the whole loop (gather, craft an axe and a pickaxe, chop, mine) ends the same twice.
#include "GB/Gameplay/Resources.h"
#include <cstdio>

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

struct Field
{
	Field()
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		Layout = BuildResources(Entities, glm::vec3(0.0f), Defs);
		const EntityId w = Entities.Create("Worker");
		Entities.Find(w)->Gameplay.emplace();
		Entities.Find(w)->Gameplay->Abilities = { "Chop", "Mine", "Gather" };
		Gameplay.Load(Entities);
		Rules.Bind(Gameplay, Defs, [this](const std::string& who, const std::string& item, int n) { if (who == "Worker") Bag.Add(Items, item, n); });
	}
	void Frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			ResourceSystem::SyncTools(*Gameplay.Find("Worker"), Bag, Items);
			Gameplay.Update(dt);
			Rules.SyncVisuals(Gameplay, Entities);
		}
	}
	bool Do(const char* ability, const std::string& target)
	{
		AbilitySystemComponent* a = Gameplay.Find("Worker");
		std::string why;
		if (!a->Activate(ability, Gameplay.Find(target), &why)) return false;
		for (int i = 0; i < 60 * 20 && a->AnyActive(); ++i) Frames(1);
		return !a->AnyActive() && a->LastError().empty();
	}
	int Amount(const std::string& node) { return ResourceSystem::Amount(*Gameplay.Find(node)); }

	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Rules;
	ResourceLayout Layout;
	Inventory Bag{ 12, 0.0f };
};

static std::string Loop(bool& ok)
{
	Field f;
	auto step = [&](bool good, const char* what) { if (!good) std::fprintf(stderr, "  (loop step failed: %s)\n", what); ok = ok && good; };

	step(!f.Do("Chop", "tree 1"), "no axe, no chopping");
	step(f.Do("Gather", "fiber_grass 1") && f.Do("Gather", "fiber_grass 1") && f.Do("Gather", "fiber_grass 1"), "gather fibre by hand");
	step(f.Bag.Count("fiber") == 6, "6 fibre");
	step(f.Do("Gather", "berry_bush 1") && f.Bag.Count("berry") == 2, "berries");

	f.Bag.Add(f.Items, "wood", 4);
	f.Bag.Add(f.Items, "stone", 5);
	step(f.Recipes.Craft(f.Items, f.Bag, "stone_axe", "workbench"), "axe");
	f.Frames(2);
	step(f.Gameplay.Find("Worker")->HasTag("Item.Tool.Axe"), "the axe is a tag of the worker");
	step(!f.Do("Mine", "rock 1"), "an axe does not mine");

	for (int i = 0; i < 5; ++i) { step(f.Do("Chop", "tree 1"), "chop"); f.Frames(60); }
	step(f.Amount("tree 1") == 0 && f.Bag.Count("wood") == 7, "five chops fell the tree (2 wood left after the axe + 5)");
	return f.Bag.ToJson() + "|" + std::to_string(f.Amount("tree 1")) + "|" + std::to_string(f.Amount("fiber_grass 1"));
}

int main()
{
	// One field, step by step.
	{
		Field f;
		Check(f.Layout.Nodes.size() == 9, "3 trees, 2 rocks, 2 bushes, 2 tufts");
		Check(f.Amount("tree 1") == 5 && f.Amount("rock 2") == 4, "full nodes");
		Check(!f.Gameplay.Find("tree 1")->HasTag("State.Depleted"), "not depleted");

		// Empty a bush by hand: 6 berries, 2 per gather.
		const int cooldown = 60;
		for (int i = 0; i < 3; ++i) { Check(f.Do("Gather", "berry_bush 2"), "gather a berry bush"); f.Frames(cooldown); }
		Check(f.Bag.Count("berry") == 6 && f.Amount("berry_bush 2") == 0, "three gathers empty a bush");
		f.Frames(2);
		Check(f.Gameplay.Find("berry_bush 2")->HasTag("State.Depleted"), "an empty bush is depleted");
		Check(!f.Do("Gather", "berry_bush 2"), "nothing to gather from a depleted bush");
		const EntityId berries = f.Entities.FindByName("berry_bush 2 Berries");
		Check(berries != NullEntity && !f.Entities.Find(berries)->Active, "its berries disappear");

		// It grows back: +2 every 20 s.
		f.Frames(60 * 21);
		Check(f.Amount("berry_bush 2") >= 2, "the bush regrows");
		Check(!f.Gameplay.Find("berry_bush 2")->HasTag("State.Depleted") && f.Entities.Find(berries)->Active, "and its berries come back");

		// A rock does not.
		f.Bag.Add(f.Items, "stone_pickaxe", 1);
		f.Frames(2);
		for (int i = 0; i < 4; ++i) { Check(f.Do("Mine", "rock 1"), "mine a rock"); f.Frames(60); }
		Check(f.Amount("rock 1") == 0 && f.Bag.Count("stone") == 4, "four minings empty a rock");
		f.Frames(60 * 600);
		Check(f.Amount("rock 1") == 0, "a rock never regrows");
	}

	// The whole loop, twice.
	{
		bool ok1 = true, ok2 = true;
		const std::string a = Loop(ok1), b = Loop(ok2);
		Check(ok1 && ok2, "the loop reaches every step");
		Check(a == b, "two runs end the same");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("resources: ok");
	return 0;
}
