#include "GB/Gameplay/Spoilage.h"
#include <algorithm>

namespace GB
{
	std::vector<SpoilEvent> AgeInventory(Inventory& inv, const ItemLibrary& lib, float seconds, float rate)
	{
		std::vector<SpoilEvent> out;
		const float step = std::max(0.0f, seconds) * std::max(0.0f, rate);
		if (step <= 0.0f) return out;
		std::vector<ItemSlot> stacks = inv.Stacks();
		bool changed = false;
		for (size_t i = 0; i < stacks.size();)
		{
			ItemSlot& s = stacks[i];
			const ItemDef* def = lib.Find(s.Id);
			if (!def || !def->Perishable()) { ++i; continue; }
			s.Age += step;
			changed = true;
			if (s.Age < def->ShelfLife) { ++i; continue; }
			// past its life: the whole stack goes off, into what it spoils into (in its place, fresh) or away
			out.push_back({ s.Id, s.Count, def->SpoilsTo });
			if (!def->SpoilsTo.empty() && lib.Find(def->SpoilsTo))
			{
				s.Id = def->SpoilsTo;
				s.Age = 0.0f;
				++i;
			}
			else stacks.erase(stacks.begin() + static_cast<std::ptrdiff_t>(i));
		}
		if (changed) inv.SetStacks(std::move(stacks));
		return out;
	}

	float LifeLeft(const ItemLibrary& lib, const ItemSlot& stack)
	{
		const ItemDef* def = lib.Find(stack.Id);
		if (!def || !def->Perishable()) return 1e30f;
		return std::max(0.0f, def->ShelfLife - stack.Age);
	}

	int SpoilingWithin(const Inventory& inv, const ItemLibrary& lib, float seconds, float rate)
	{
		int n = 0;
		for (const ItemSlot& s : inv.Stacks())
			if (LifeLeft(lib, s) <= seconds * std::max(rate, 0.0f)) n += s.Count;
		return n;
	}
}
