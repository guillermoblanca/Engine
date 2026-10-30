#pragma once
#include "GB/Game/Components/ActorStateSaver.h"
#include "GB/Game/GBObject.h"
#include "GB/Gameplay/Items.h"

namespace GB
{
	// What an actor carries (H2): a backpack of 12 slots. Saved with the actor.
	class InventoryComponent : public GBObjectComponent, public IStateSaver
	{
	public:
		Inventory& Bag() { return m_Bag; }
		const Inventory& Bag() const { return m_Bag; }

		void CaptureState(Save::ActorState& state) const override
		{
			for (const ItemSlot& slot : m_Bag.Stacks()) state.Inventory.push_back({ slot.Id, slot.Count, slot.Age });   // (E6: the age of what spoils; 0 is not written)
		}
		void RestoreState(const Save::ActorState& state) override
		{
			std::vector<ItemSlot> bag;
			for (const Save::ItemStack& it : state.Inventory) bag.push_back({ it.Item, it.Count, it.Age });
			m_Bag.SetStacks(std::move(bag));
		}

	private:
		Inventory m_Bag{ 12, 0.0f };
	};
}
