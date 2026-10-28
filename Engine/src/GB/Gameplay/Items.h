#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GB
{
	// Items, inventories, resource nodes and recipes: the data and rules of "things to pick up, carry, store, use and make". Pure logic with
	// no scene or renderer, deterministic (ordered containers, no randomness), so a test can run a whole gather-and-craft loop twice.

	// What an item is. Items are data: a library maps an id ("wood", "stone_axe") to its definition.
	struct ItemDef
	{
		std::string Id;
		std::string Name;
		std::string Category;               // "resource", "tool", "food", ...
		float Weight = 1.0f;                // per unit
		int MaxStack = 1;                   // units per slot (1 = does not stack)
		std::vector<std::string> Tags;      // "Item.Tool.Axe": what a requirement can ask for
		std::string Ability;                // ability the item enables or triggers ("" = none)
		std::string Socket;                 // hand/hip socket it is shown in when carried ("" = not shown)
		// Tools (E2): a variant ("Item.Tool.Axe.Flint") is still an axe through the tag hierarchy; these say how much better it is.
		int Power = 0;                      // extra units a node gives per swing, on top of its own PerGather
		float Speed = 1.0f;                 // multiplier of the swing's playback rate (1.25 = a quarter faster)
		// Containers (E2): carried in the bag, they make it bigger. Only the best one counts (Inventory::SyncCapacity).
		int BagSlots = 0;                   // extra slots while carried
		float BagWeight = 0.0f;             // extra weight limit while carried (only for a bag that has a limit)
		// Fuel (E1): units of heat a campfire gets when this item is put in (Stoke). 0 = the campfire's own default (InteractiveSystem::Config::FuelPerWood).
		// Only items tagged "Item.Resource.Wood" (or below) can be put in: wood gives 2, a branch 1, so it takes more branches for the same heat.
		int Fuel = 0;
	};

	// The tool family of a tool tag: the first three segments, "Item.Tool.Axe.Flint" -> "Item.Tool.Axe" (anything else is returned as is).
	// Power and speed of the best tool of a family are the attributes "Power:<family>" and "Speed:<family>" of whoever carries it.
	std::string ToolFamily(const std::string& tag);

	class ItemLibrary
	{
	public:
		void Add(const ItemDef& def) { m_Items[def.Id] = def; }
		const ItemDef* Find(const std::string& id) const;
		std::vector<std::string> Ids() const;
	private:
		std::map<std::string, ItemDef> m_Items;
	};

	struct ItemSlot
	{
		std::string Id;
		int Count = 0;
	};

	// A container (backpack, crate, store): a fixed number of slots and an optional weight limit (<= 0 = unlimited).
	class Inventory
	{
	public:
		Inventory() = default;
		Inventory(int slots, float maxWeight = 0.0f) : m_Slots(slots), m_MaxWeight(maxWeight) {}

		// Capacity = the bag's own plus the bonus of the best container it holds (SyncCapacity). Contents over the capacity (a container
		// that was dropped) stay where they are; nothing more goes in until there is room.
		int Slots() const { return m_Slots + m_BonusSlots; }
		float MaxWeight() const { return m_MaxWeight > 0.0f ? m_MaxWeight + m_BonusWeight : 0.0f; }
		int BaseSlots() const { return m_Slots; }
		float BaseMaxWeight() const { return m_MaxWeight; }
		int BonusSlots() const { return m_BonusSlots; }
		float BonusWeight() const { return m_BonusWeight; }
		void SetBonus(int slots, float weight) { m_BonusSlots = std::max(0, slots); m_BonusWeight = std::max(0.0f, weight); }
		// The best container held: the one tagged "Item.Container" (or below) with the most slots, then the most weight, then the lowest id. Null if none.
		const ItemDef* BestContainer(const ItemLibrary& lib) const;
		// Applies the bonus of BestContainer. Only one counts, so carrying two baskets does not stack. Call it whenever the contents change
		// and after a load: the bonus is derived from the contents, so a save does not store it.
		void SyncCapacity(const ItemLibrary& lib);
		// The strongest tool of a family held (most power, then speed, then lowest id): "Item.Tool.Axe" finds every kind of axe. Null if none.
		const ItemDef* BestTool(const ItemLibrary& lib, const std::string& tag) const;

		// Puts up to `count` units in, filling stacks first and then empty slots, within the weight limit. Returns how many fit.
		int Add(const ItemLibrary& lib, const std::string& id, int count);
		// Takes up to `count` units out (from the last stacks first). Returns how many were taken.
		int Remove(const std::string& id, int count);
		int Count(const std::string& id) const;
		// Units of items carrying `tag` (or a descendant of it), e.g. "Item.Tool.Axe".
		int CountTagged(const ItemLibrary& lib, const std::string& tag) const;
		float Weight(const ItemLibrary& lib) const;
		// Moves up to `count` units into another container; returns how many moved.
		int Transfer(const ItemLibrary& lib, Inventory& to, const std::string& id, int count);

		int UsedSlots() const { return static_cast<int>(m_Stacks.size()); }
		const std::vector<ItemSlot>& Stacks() const { return m_Stacks; }
		// Replaces the contents as they were saved (no checks against a library: a save is trusted to hold what it held).
		void SetStacks(std::vector<ItemSlot> stacks) { m_Stacks = std::move(stacks); }

		// Compact JSON, e.g. {"slots":8,"maxWeight":20,"items":[["wood",3]]} ("slots" and "maxWeight" are the bag's own; "bonusSlots" and
		// "bonusWeight" are written only when a container gives some, and an older text without them loads as before). Load returns false (and changes nothing) on malformed text.
		std::string ToJson() const;
		bool FromJson(const std::string& json);

	private:
		int m_Slots = 0;
		float m_MaxWeight = 0.0f;
		int m_BonusSlots = 0;
		float m_BonusWeight = 0.0f;
		std::vector<ItemSlot> m_Stacks;
	};

	// For the editor: "8 / 18 slots (12 + 6 from Wicker basket), 3.2 / 28 kg" and one line per tool family held, the best of each:
	// "Item.Tool.Axe: Flint axe (power +1, speed x1.15)".
	std::string CapacityText(const ItemLibrary& lib, const Inventory& bag);
	std::vector<std::string> ActiveTools(const ItemLibrary& lib, const Inventory& bag);

	// A gatherable thing in the world: a tree, a rock, a berry bush, a deposit. It holds a quantity, gives it out in pieces, and either
	// grows back (Regen units every RegenSeconds) or stays depleted.
	// Where and how many nodes of a type are put in a patch (E1). Count + PerWorker * workers nodes, so a tribe of any size gets a patch that feeds it.
	// Count 0 and PerWorker 0 = the type is not part of a patch (it can still be placed by hand).
	struct ResourcePatchDef
	{
		int Count = 0;                      // nodes for nobody (the base of the patch)
		int PerWorker = 0;                  // more nodes for each worker of the tribe
		float Spacing = 4.0f;               // metres between neighbours
		std::string Shape = "row";          // "row" (lines of eight), "ring" or "cluster" (a scatter around a centre)
		std::string Near;                   // another node type: each node of this one stands beside a node of that one (sibling nodes: a big tree's branches)
		int CountFor(int workers) const { return std::max(0, Count + PerWorker * std::max(1, workers)); }
	};

	// Where a type of node grows over a generated world (PlanWorldResources): on which biomes, how thick, how far apart, and whether it keeps to the
	// water or comes in groves and veins. PerHectare 0 or no biomes = the type is not spread over the world (it can still be in a patch).
	struct ResourceSpawnDef
	{
		std::vector<std::string> Biomes;    // "Meadow", "Forest", "Shore", "Rock", "Snow", "Dunes" (GB::Biome names)
		float PerHectare = 0.0f;            // nodes per 10 000 m² of matching ground, before the spacing and the clumping thin them out
		float Spacing = 6.0f;               // metres to any other node
		float MaxSlope = 35.0f;             // degrees
		float WaterWithin = 0.0f;           // > 0: only this close (metres) to the lake or the river
		float Clump = 0.0f;                 // 0..0.99: share of the ground a noise field leaves out, so the nodes gather in groves and veins
		int Max = 0;                        // most nodes over the whole map (0 = no limit)
		bool Placed() const { return PerHectare > 0.0f && !Biomes.empty(); }
	};

	struct ResourceNodeDef
	{
		std::string Id;                     // "tree", "rock", "berry_bush"
		std::string Item;                   // item it yields
		int Capacity = 5;
		int PerGather = 1;
		float RegenSeconds = 0.0f;          // 0 = never regrows
		int Regen = 1;
		std::string RequiresTag;            // tool tag needed to gather ("" = bare hands)
		std::string Ability;                // ability that gathers it (animation montage comes from the ability)
		// E1: the kind of the node, "Wood", "Stone", "Stone.Flint", "Plant", "Loose.Clay"...: the node carries the tag "Resource.<Kind>" and the abilities
		// match on it (Chop on Resource.Wood, Mine on Resource.Stone and below, Gather on Resource.Plant, Collect on Resource.Loose). Empty = worked out
		// from the item as before (wood -> Wood, stone -> Stone, anything else -> Plant), so an older content file still works.
		std::string Kind;
		std::string Model;                  // the primitives it is drawn with ("" = the node's id; an unknown model is a plain grey blob)
		ResourcePatchDef Patch;
		ResourceSpawnDef Spawn;
	};

	class ResourceNode
	{
	public:
		ResourceNode() = default;
		explicit ResourceNode(const ResourceNodeDef& def) : m_Def(def), m_Amount(def.Capacity) {}

		const ResourceNodeDef& Def() const { return m_Def; }
		int Amount() const { return m_Amount; }
		bool Depleted() const { return m_Amount <= 0; }
		// Takes one gather's worth out; returns the units obtained (0 when depleted).
		int Gather();
		void Update(float dt);
		void SetAmount(int amount);
	private:
		ResourceNodeDef m_Def;
		int m_Amount = 0;
		float m_Timer = 0.0f;
	};

	// Raw materials -> a product, optionally at a station and with a tool that is needed but not used up.
	struct Recipe
	{
		std::string Id;
		std::string Station;                // "" = anywhere ("workbench", "campfire")
		std::string ToolTag;                // tool tag required in the inventory, not consumed ("" = none)
		std::map<std::string, int> Inputs;
		std::string Output;
		int OutputCount = 1;
		std::string Ability;                // ability (and so montage) that performs it
	};

	class RecipeBook
	{
	public:
		void Add(const Recipe& r) { m_Recipes[r.Id] = r; }
		const Recipe* Find(const std::string& id) const;
		std::vector<std::string> Ids() const;
		// Why `id` cannot be made now from `inv` at `station` ("" = it can): missing input, tool, station or room.
		std::string WhyNot(const ItemLibrary& lib, const Inventory& inv, const std::string& id, const std::string& station) const;
		// Makes it: takes the inputs, adds the output. False (nothing changes) when WhyNot is not empty.
		bool Craft(const ItemLibrary& lib, Inventory& inv, const std::string& id, const std::string& station) const;
		// The same for a crafter that only knows some recipes (H11): `known` is the set of recipe ids it knows how to make (see ClanKnowledge::KnownRecipes);
		// a recipe outside it gives "does not know how to make '<id>'" before anything else. nullptr = it knows them all, as the overloads above.
		std::string WhyNot(const ItemLibrary& lib, const Inventory& inv, const std::string& id, const std::string& station, const std::set<std::string>* known) const;
		bool Craft(const ItemLibrary& lib, Inventory& inv, const std::string& id, const std::string& station, const std::set<std::string>* known) const;
		// The recipe ids a crafter knows: all of them with nullptr, else those of `known` that are recipes here.
		std::vector<std::string> Known(const std::set<std::string>* known) const;
	private:
		std::map<std::string, Recipe> m_Recipes;
	};

	// Content as data: items, recipes and resource nodes in one JSON document, so the game's things can be added and changed without a build.
	//   { "items":   [ { "id", "name", "category", "weight", "stack", "tags": [], "ability", "socket", "power", "speed", "bagSlots", "bagWeight", "fuel" } ],
	//     "recipes": [ { "id", "station", "tool", "inputs": { "wood": 2 }, "output", "count", "ability" } ],
	//     "nodes":   [ { "id", "item", "capacity", "perGather", "regenSeconds", "regen", "requires", "ability", "kind", "model",
	//                    "patch": { "count", "perWorker", "spacing", "shape", "near" } } ] }
	// Entries replace those of the same id; anything else already loaded stays. Returns "" or what is wrong (and changes nothing).
	std::string LoadContent(const std::string& json, ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes);
	std::string LoadContentFile(const std::string& path, ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes);
	// The same document for what is loaded now (pretty-printed): the starting point of a content file.
	std::string ContentToJson(const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes);

	// (The last five item fields are optional: power 0, speed 1, bagSlots 0, bagWeight 0 and fuel 0 when missing; "kind", "model" and "patch" of a node
	// too: an older file without them keeps working.)

	// The era-1 content: wood, stone, fiber, berries, stone and flint tools, a wicker basket, a campfire and the resource nodes and recipes that go with them.
	void RegisterEraOneItems(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes);
}
