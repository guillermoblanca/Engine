#pragma once
#include "GB/Gameplay/Items.h"
#include <string>
#include <vector>

namespace GB
{
	// Spoilage of goods (E6, H18): food that is not eaten in time goes off. Pure CPU and deterministic, like the rest of Items.
	//
	// The rule: an item with a ShelfLife (seconds of simulation, ItemDef) ages in whatever holds it (a bag, the store, a stockpile). Each stack
	// carries the mean age of its units (ItemSlot::Age: a fresh unit put on an old stack brings it down, an old one up), and when that age reaches
	// the shelf life the whole stack goes off at once: it turns into the item's SpoilsTo (as many units, fresh) or, without one, it is gone.
	// Preserving is not a special case: dried berries are another item with a longer life, made from berries by a recipe at a station (the
	// drying rack), and a container ages what it holds at its own rate (a cool store 0.5, a bag 1), which the caller passes as `rate`.
	// Items without a shelf life never age (their Age stays 0), so nothing changes for them and saves without ages load as fresh.

	struct SpoilEvent
	{
		std::string Item;       // what went off
		int Count = 0;          // how many units
		std::string Into;       // what they turned into ("" = gone)
	};

	// Ages the perishable stacks of `inv` by `seconds` x `rate` and lets the ones past their shelf life go off. Returns what went off, in stack
	// order (the same inventory and the same steps always give the same events).
	std::vector<SpoilEvent> AgeInventory(Inventory& inv, const ItemLibrary& lib, float seconds, float rate);

	// Seconds a stack has left before it goes off at rate 1 (a huge number for what never spoils).
	float LifeLeft(const ItemLibrary& lib, const ItemSlot& stack);
	// The units of `inv` that will go off within `seconds` at `rate` (for panels and the planner: food to eat or preserve first).
	int SpoilingWithin(const Inventory& inv, const ItemLibrary& lib, float seconds, float rate);
}
