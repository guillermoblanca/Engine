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
	// patchWorkers 0 = the small default patch; more = the patch the node definitions describe, for that many workers (E1)
	explicit Field(int patchWorkers = 0)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		Layout = patchWorkers > 0 ? BuildResourcePatch(Entities, glm::vec3(0.0f), Defs, patchWorkers) : BuildResources(Entities, glm::vec3(0.0f), Defs);
		const EntityId w = Entities.Create("Worker");
		Entities.Find(w)->Gameplay.emplace();
		Entities.Find(w)->Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Collect" };
		Gameplay.Load(Entities);
		Rules.Bind(Gameplay, Defs, [this](const std::string& who, const std::string& item, int n) { if (who == "Worker") Bag.Add(Items, item, n); });
	}
	void Frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			ResourceSystem::SyncTools(*Gameplay.Find("Worker"), Bag, Items);
			Gameplay.Update(dt);
			Rules.Update(Gameplay, dt);
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
	// Frames an ability takes from activation to the end (-1 if it fails): the tool's speed shows here.
	int DoFrames(const char* ability, const std::string& target)
	{
		AbilitySystemComponent* a = Gameplay.Find("Worker");
		if (!a->Activate(ability, Gameplay.Find(target), nullptr)) return -1;
		int n = 0;
		for (; n < 60 * 20 && a->AnyActive(); ++n) Frames(1);
		return a->AnyActive() || !a->LastError().empty() ? -1 : n;
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
	step(f.Do("PullFiber", "fiber grass 1") && f.Do("PullFiber", "fiber grass 1") && f.Do("PullFiber", "fiber grass 1"), "pull fibre by hand");
	step(!f.Do("Gather", "fiber grass 2"), "berry picking does not work on a tuft of grass");
	step(f.Bag.Count("fiber") == 6, "6 fibre");
	step(f.Do("Gather", "berry bush 1") && f.Bag.Count("berry") == 2, "berries");

	f.Bag.Add(f.Items, "wood", 4);
	f.Bag.Add(f.Items, "stone", 5);
	step(f.Recipes.Craft(f.Items, f.Bag, "stone_axe", "workbench"), "axe");
	f.Frames(2);
	step(f.Gameplay.Find("Worker")->HasTag("Item.Tool.Axe"), "the axe is a tag of the worker");
	step(!f.Do("Mine", "rock 1"), "an axe does not mine");

	for (int i = 0; i < 5; ++i) { step(f.Do("Chop", "tree 1"), "chop"); f.Frames(60); }
	step(f.Amount("tree 1") == 0 && f.Bag.Count("wood") == 7, "five chops fell the tree (2 wood left after the axe + 5)");
	return f.Bag.ToJson() + "|" + std::to_string(f.Amount("tree 1")) + "|" + std::to_string(f.Amount("fiber grass 1"));
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
		for (int i = 0; i < 3; ++i) { Check(f.Do("Gather", "berry bush 2"), "gather a berry bush"); f.Frames(cooldown); }
		Check(f.Bag.Count("berry") == 6 && f.Amount("berry bush 2") == 0, "three gathers empty a bush");
		f.Frames(2);
		Check(f.Gameplay.Find("berry bush 2")->HasTag("State.Depleted"), "an empty bush is depleted");
		Check(!f.Do("Gather", "berry bush 2"), "nothing to gather from a depleted bush");
		const EntityId berries = f.Entities.FindByName("berry bush 2 Berries");
		Check(berries != NullEntity && !f.Entities.Find(berries)->Active, "its berries disappear");

		// It grows back: +2 every 20 s.
		f.Frames(60 * 21);
		Check(f.Amount("berry bush 2") >= 2, "the bush regrows");
		Check(!f.Gameplay.Find("berry bush 2")->HasTag("State.Depleted") && f.Entities.Find(berries)->Active, "and its berries come back");

		// A rock does not.
		f.Bag.Add(f.Items, "stone_pickaxe", 1);
		f.Frames(2);
		for (int i = 0; i < 4; ++i) { Check(f.Do("Mine", "rock 1"), "mine a rock"); f.Frames(60); }
		Check(f.Amount("rock 1") == 0 && f.Bag.Count("stone") == 4, "four minings empty a rock");
		f.Frames(60 * 600);
		Check(f.Amount("rock 1") == 0, "a rock never regrows");
	}

	// A forest that regrows (H3): every tree felled, then ten minutes of accelerated simulation; the amounts never leave their bounds.
	{
		Field f;
		f.Bag.Add(f.Items, "stone_axe", 1);
		f.Frames(2);
		for (const char* tree : { "tree 1", "tree 2", "tree 3" })
			for (int i = 0; i < 5; ++i) { Check(f.Do("Chop", tree), "fell a tree"); f.Frames(30); }
		Check(f.Bag.Count("wood") == 15 && f.Amount("tree 3") <= 1, "the three trees are felled (a regrown piece at most: 30 s have passed)");
		bool inBounds = true;
		for (int minute = 0; minute < 10; ++minute)
		{
			f.Frames(60 * 60);
			for (const char* tree : { "tree 1", "tree 2", "tree 3" }) inBounds = inBounds && f.Amount(tree) >= 0 && f.Amount(tree) <= 5;
		}
		Check(inBounds, "amounts stay between zero and capacity");
		Check(f.Amount("tree 1") == 5 && f.Amount("tree 2") == 5 && f.Amount("tree 3") == 5, "ten minutes regrow every tree (1 wood per 30 s)");
		Check(!f.Gameplay.Find("tree 2")->HasTag("State.Depleted"), "regrown trees are no longer depleted");
	}

	// Nothing regrows in deep winter (rate 0), and the timer is part of the node (an attribute), so it is saved with it.
	{
		Field f;
		f.Gameplay.Find("berry bush 1")->Attributes().SetBase("Amount", 0.0f);
		for (int i = 0; i < 60 * 60; ++i) f.Rules.Update(f.Gameplay, dt, 0.0f);
		Check(f.Amount("berry bush 1") == 0, "a bush does not regrow in winter");
		for (int i = 0; i < 60 * 10; ++i) f.Rules.Update(f.Gameplay, dt, 1.0f);
		Check(f.Gameplay.Find("berry bush 1")->Attributes().Current("Regrow") > 9.0f, "the regrowth timer advances as an attribute");
		for (int i = 0; i < 60 * 11; ++i) f.Rules.Update(f.Gameplay, dt, 1.0f);
		Check(f.Amount("berry bush 1") == 2, "and the bush gives its pieces back in spring");
	}

	// Tools with a level (E2): a flint axe is an axe (tag hierarchy) but gives more per swing and swings faster than a stone one.
	{
		Field stone, flint;
		stone.Bag.Add(stone.Items, "stone_axe", 1);
		flint.Bag.Add(flint.Items, "flint_axe", 1);
		stone.Frames(2); flint.Frames(2);
		Check(flint.Gameplay.Find("Worker")->HasTag("Item.Tool.Axe"), "a flint axe satisfies the generic axe tag");
		Check(flint.Gameplay.Find("Worker")->Attributes().Current("Power:Item.Tool.Axe") == 1.0f && stone.Gameplay.Find("Worker")->Attributes().Current("Power:Item.Tool.Axe") == 0.0f, "the best axe sets the power");
		const int slow = stone.DoFrames("Chop", "tree 1"), fast = flint.DoFrames("Chop", "tree 1");
		Check(slow > 0 && fast > 0 && fast < slow, "a flint swing is quicker than a stone one");
		Check(stone.Bag.Count("wood") == 1 && flint.Bag.Count("wood") == 2, "and brings one more wood");
		stone.Frames(60); flint.Frames(60);
		stone.DoFrames("Chop", "tree 1"); flint.DoFrames("Chop", "tree 1");
		stone.Frames(60); flint.Frames(60);
		flint.DoFrames("Chop", "tree 1");
		Check(flint.Amount("tree 1") == 0 && flint.Bag.Count("wood") == 5, "three flint swings fell a tree (the last takes what is left)");
		Check(stone.Amount("tree 1") == 3, "while the stone axe needs five");
		// both axes: the better one counts; without any, the power is gone
		flint.Bag.Add(flint.Items, "stone_axe", 1);
		flint.Frames(2);
		Check(flint.Gameplay.Find("Worker")->Attributes().Current("Power:Item.Tool.Axe") == 1.0f, "with two axes the flint one counts");
		flint.Bag.Remove("flint_axe", 1); flint.Bag.Remove("stone_axe", 1);
		flint.Frames(2);
		Check(flint.Gameplay.Find("Worker")->Attributes().Current("Power:Item.Tool.Axe") == 0.0f && flint.Gameplay.Find("Worker")->Attributes().Current("Speed:Item.Tool.Axe") == 1.0f, "no axe, no bonus");

		Field pick;
		pick.Bag.Add(pick.Items, "flint_pickaxe", 1);
		pick.Frames(2);
		Check(pick.Do("Mine", "rock 1") && pick.Bag.Count("stone") == 2, "a flint pickaxe mines two stones a swing");
	}

	// The patch by data (E1): what the node definitions say is what is placed, scaled by the number of workers.
	{
		Field probe;
		for (int workers : { 1, 2, 5 })
		{
			const ResourceCounts legacy = ScaledResources(workers);
			std::map<std::string, int> counts;
			for (const auto& [id, n] : PatchCounts(probe.Defs, workers)) counts[id] = n;
			Check(counts["tree"] == legacy.Trees && counts["rock"] == legacy.Rocks && counts["berry_bush"] == legacy.Bushes && counts["fiber_grass"] == legacy.Grass,
				"the four old node types keep the scale of ScaledResources");
			Check(counts["old_tree"] == 1 + workers && counts["young_tree"] == 1 + workers && counts["copper_vein"] == 1 && counts["clay_bank"] == 1, "the new types scale with their own numbers");
		}

		Field p1(1), p3(3), again(3);
		size_t expected1 = 0, expected3 = 0;
		for (const auto& [id, n] : PatchCounts(p1.Defs, 1)) expected1 += static_cast<size_t>(n);
		for (const auto& [id, n] : PatchCounts(p3.Defs, 3)) expected3 += static_cast<size_t>(n);
		Check(p1.Layout.Nodes.size() == expected1 && p3.Layout.Nodes.size() == expected3 && expected3 > expected1, "a bigger tribe gets a bigger patch, exactly as the data counts");
		for (const char* name : { "old tree 1", "young tree 1", "clay bank 1", "reed bed 1", "flint outcrop 1", "copper vein 1", "tree 1", "rock 1", "berry bush 1", "fiber grass 1" })
			Check(p3.Gameplay.Find(name) != nullptr, "every type of the patch is placed");
		Check(p3.Gameplay.Find("old tree 4") != nullptr && p3.Gameplay.Find("old tree 5") == nullptr, "1 + 3 old trees for three workers");
		bool same = p3.Layout.Nodes == again.Layout.Nodes;
		for (const std::string& n : p3.Layout.Nodes)
		{
			const glm::vec3 a = p3.Entities.Find(p3.Entities.FindByName(n))->Transform.Position, b = again.Entities.Find(again.Entities.FindByName(n))->Transform.Position;
			same = same && a == b;
		}
		Check(same, "the same data and workers give the same patch");

		// no two nodes of the patch stand on top of each other
		bool apart = true;
		for (size_t i = 0; i < p3.Layout.Nodes.size(); ++i)
			for (size_t j = i + 1; j < p3.Layout.Nodes.size(); ++j)
			{
				const glm::vec3 a = p3.Entities.Find(p3.Entities.FindByName(p3.Layout.Nodes[i]))->Transform.Position, b = p3.Entities.Find(p3.Entities.FindByName(p3.Layout.Nodes[j]))->Transform.Position;
				if (glm::distance(a, b) < 1.5f) apart = false;
			}
		Check(apart, "and none overlaps another");
		const glm::vec3 oldPos = p3.Entities.Find(p3.Entities.FindByName("old tree 1"))->Transform.Position, youngPos = p3.Entities.Find(p3.Entities.FindByName("young tree 1"))->Transform.Position;
		Check(glm::distance(oldPos, youngPos) < 4.0f, "a young tree stands beside an old one (sibling nodes)");

		// the kind of a node comes from its data
		Check(p3.Gameplay.Find("old tree 1")->HasTag("Resource.Wood") && p3.Gameplay.Find("clay bank 1")->HasTag("Resource.Loose.Clay") && p3.Gameplay.Find("clay bank 1")->HasTag("Resource.Loose")
			&& p3.Gameplay.Find("flint outcrop 1")->HasTag("Resource.Stone") && p3.Gameplay.Find("flint outcrop 1")->HasTag("Resource.Stone.Flint") && !p3.Gameplay.Find("flint outcrop 1")->HasTag("Resource.Loose")
			&& p3.Gameplay.Find("young tree 1")->HasTag("Resource.Plant") && !p3.Gameplay.Find("young tree 1")->HasTag("Resource.Wood"), "the kind tags come from the definition, not from the item");
		// a definition without a kind (an older content file) is still typed by its item
		{
			std::map<std::string, ResourceNodeDef> old = probe.Defs;
			for (auto& [id, d] : old) d.Kind.clear();
			EntityWorld w;
			BuildResources(w, glm::vec3(0.0f), old);
			GameplayWorld g;
			g.Load(w);
			Check(g.Find("tree 1")->HasTag("Resource.Wood") && g.Find("rock 1")->HasTag("Resource.Stone") && g.Find("berry bush 1")->HasTag("Resource.Plant"), "without a kind the old rule (by item) still applies");
		}

		// working them: each ability matches by the kind tag
		Field f(2);
		f.Bag.Add(f.Items, "stone_axe", 1);
		f.Bag.Add(f.Items, "stone_pickaxe", 1);
		f.Frames(2);
		Check(f.Do("Gather", "young tree 1") && f.Bag.Count("branch") == 1, "branches come off a young tree by hand");
		Check(!f.Do("Chop", "young tree 1") && !f.Do("Gather", "old tree 1"), "an axe is for the old tree, hands cannot gather it");
		f.Frames(60);
		Check(f.Do("Chop", "old tree 1") && f.Bag.Count("wood") == 1 && f.Amount("old tree 1") == 7, "the old tree gives wood to the axe");
		f.Frames(60);
		Check(f.Do("Collect", "clay bank 1") && f.Bag.Count("clay") == 2, "clay is collected by hand, two at a time");
		Check(!f.Do("Gather", "clay bank 1") && !f.Do("Mine", "clay bank 1"), "gathering and mining do not work on clay");
		f.Frames(60);
		Check(f.Do("Collect", "reed bed 1") && f.Bag.Count("reed") == 2, "reeds too");
		f.Frames(60);
		Check(f.Do("Mine", "flint outcrop 1") && f.Bag.Count("flint") == 1, "flint is mined with a pickaxe");
		f.Frames(60);
		for (int i = 0; i < 4; ++i) { Check(f.Do("Mine", "copper vein 1"), "mine copper"); f.Frames(60); }
		Check(f.Bag.Count("copper_ore") == 4 && f.Amount("copper vein 1") == 0, "four minings empty a copper vein");
		f.Frames(2);
		const EntityId ore = f.Entities.FindByName("copper vein 1 Ore 1"), base = f.Entities.FindByName("copper vein 1 Base");
		Check(ore != NullEntity && !f.Entities.Find(ore)->Active && base != NullEntity && f.Entities.Find(base)->Active, "an emptied vein loses its ore and keeps its rock");
		f.Frames(60 * 600);
		Check(f.Amount("copper vein 1") == 0, "copper never grows back");
		Check(!f.Do("Mine", "old tree 1") && !f.Do("Collect", "tree 1"), "a pickaxe does not fell a tree");
		// without the tool nothing is mined
		Field bare(2);
		Check(!bare.Do("Mine", "flint outcrop 1") && !bare.Do("Mine", "copper vein 1") && !bare.Do("Chop", "old tree 1"), "stone and big trees need their tools");

		// regrowth by season: clay comes back with the seasons' rate, not in winter
		f.Gameplay.Find("reed bed 1")->Attributes().SetBase("Amount", 0.0f);
		for (int i = 0; i < 60 * 120; ++i) f.Rules.Update(f.Gameplay, dt, 0.0f);
		Check(f.Amount("reed bed 1") == 0, "reeds do not regrow in winter");
		for (int i = 0; i < 60 * 41; ++i) f.Rules.Update(f.Gameplay, dt, 0.5f);
		Check(f.Amount("reed bed 1") == 2, "and come back at the speed of the season (half speed: 2 every 40 s)");
		f.Frames(2);
		Check(!f.Gameplay.Find("reed bed 1")->HasTag("State.Depleted"), "a regrown bed is no longer depleted");

		// each type looks different: size and colour tell them apart
		auto part = [&](const char* name) -> const Entity* { const EntityId id = f.Entities.FindByName(name); return id == NullEntity ? nullptr : f.Entities.Find(id); };
		const Entity* youngTrunk = part("young tree 1 Trunk"); const Entity* oldTrunk = part("old tree 1 Trunk");
		Check(youngTrunk && oldTrunk && oldTrunk->Transform.Scale.y > 2.0f * youngTrunk->Transform.Scale.y, "an old tree is much bigger than a young one");
		const Entity* clay = part("clay bank 1 Mound 1"); const Entity* reed = part("reed bed 1 Reeds 1"); const Entity* flint = part("flint outcrop 1 Flint 1"); const Entity* ore1 = part("copper vein 1 Ore 1");
		Check(clay && clay->Mesh->Albedo.r > clay->Mesh->Albedo.g && clay->Mesh->Albedo.g > clay->Mesh->Albedo.b, "clay is brown");
		Check(reed && reed->Mesh->Albedo.g > reed->Mesh->Albedo.r && reed->Transform.Scale.y > 1.5f && reed->Transform.Scale.x < 0.2f, "reeds are tall, thin and green");
		Check(flint && flint->Mesh->Albedo.r < 0.3f && flint->Mesh->Albedo.g < 0.3f && flint->Mesh->Albedo.b < 0.3f, "flint is dark grey");
		Check(ore1 && ore1->Mesh->Albedo.g > ore1->Mesh->Albedo.r, "copper ore is greenish");
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
