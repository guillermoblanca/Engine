#pragma once
#include <map>
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
	};

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

		int Slots() const { return m_Slots; }
		int UsedSlots() const { return static_cast<int>(m_Stacks.size()); }
		float MaxWeight() const { return m_MaxWeight; }
		const std::vector<ItemSlot>& Stacks() const { return m_Stacks; }
		// Replaces the contents as they were saved (no checks against a library: a save is trusted to hold what it held).
		void SetStacks(std::vector<ItemSlot> stacks) { m_Stacks = std::move(stacks); }

		// Compact JSON, e.g. {"slots":8,"maxWeight":20,"items":[["wood",3]]}. Load returns false (and changes nothing) on malformed text.
		std::string ToJson() const;
		bool FromJson(const std::string& json);

	private:
		int m_Slots = 0;
		float m_MaxWeight = 0.0f;
		std::vector<ItemSlot> m_Stacks;
	};

	// A gatherable thing in the world: a tree, a rock, a berry bush, a deposit. It holds a quantity, gives it out in pieces, and either
	// grows back (Regen units every RegenSeconds) or stays depleted.
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
	private:
		std::map<std::string, Recipe> m_Recipes;
	};

	// The era-1 content: wood, stone, fiber, berries, a stone axe and pickaxe, a campfire and the resource nodes and recipes that go with them.
	void RegisterEraOneItems(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes);
}
