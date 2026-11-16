// T248 + T255: the store buildings of a clan and the rule of what goes in which (see StoreLayout.h).
#include "GB/Game/StoreLayout.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/Tribe.h"
#include <algorithm>

namespace GB
{
	int StoreLayout::Place::Units() const
	{
		int n = 0;
		for (const auto& [id, count] : Held) n += count;
		return n;
	}

	const StoreLayout::Place* StoreLayout::Find(const std::string& building) const
	{
		for (const Place& p : m_Places) if (p.Name == building) return &p;
		return nullptr;
	}

	int StoreLayout::InBuildings() const
	{
		int n = 0;
		for (const Place& p : m_Places) n += p.Units();
		return n;
	}

	int StoreLayout::AtCampUnits() const
	{
		int n = 0;
		for (const auto& [id, count] : m_Camp) n += count;
		return n;
	}

	std::vector<StoreLayout::Place> StoreLayout::Buildings(const Tribe& tribe)
	{
		std::vector<Place> out;
		if (!tribe.Items() || !tribe.Buildings()) return out;
		const auto& kinds = tribe.Config.Econ.Logistics.Stores;
		for (const Tribe::Placed& p : tribe.Placements())
		{
			if (!p.State.Done || !p.Def) continue;
			Place place;
			place.Name = p.Name;
			place.Pos = p.Pos;
			place.Door = SiteFront(p.Pos, *p.Def);
			place.Reach = p.Def->Radius() + 3.0f;
			place.Keep = p.Def->Keep;
			place.FoodRoom = std::max(0, p.Def->FoodCap);
			place.Room = std::max(0, p.Def->StoreCap);
			if (!p.Def->Store.empty() && p.Def->Store != "main")
				if (const auto k = kinds.find(p.Def->Store); k != kinds.end()) place.Room += std::max(0, k->second.Cap) * std::max(0, k->second.Slots);
			if (place.FoodRoom > 0 || place.Room > 0) out.push_back(std::move(place));
		}
		return out;
	}

	int StoreLayout::Free(const Tribe& tribe, const Place& place, const std::string& item)
	{
		int food = 0, rest = 0;
		for (const auto& [id, n] : place.Held) (tribe.IsFood(id) ? food : rest) += n;
		const int roomUsed = rest + std::max(0, food - place.FoodRoom);
		const int roomFree = std::max(0, place.Room - roomUsed);
		if (!tribe.IsFood(item)) return roomFree;
		return std::max(0, place.FoodRoom - food) + roomFree;
	}

	StoreLayout StoreLayout::Of(const Tribe& tribe)
	{
		StoreLayout out;
		out.m_Places = Buildings(tribe);
		const ClanStores& stores = tribe.Stores();
		for (Place& p : out.m_Places)
			if (const auto it = stores.Ledger().find(p.Name); it != stores.Ledger().end())
				for (const auto& [id, n] : it->second) if (n > 0) p.Held[id] = n;
		for (const ItemSlot& s : tribe.Store().Stacks())
			if (s.Count > 0) out.m_Camp[s.Id] += s.Count;
		for (auto it = out.m_Camp.begin(); it != out.m_Camp.end();)
		{
			it->second = std::max(0, it->second - stores.InBuildings(it->first));
			it = it->second > 0 ? std::next(it) : out.m_Camp.erase(it);
		}
		return out;
	}
}
