// T255: the clan's store buildings as the place its things are kept (see ClanStores.h).
#include "GB/Game/ClanStores.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		// the store buildings with what the ledger says each holds
		std::vector<StoreLayout::Place> Filled(const Tribe& tribe, const std::map<std::string, ClanStores::Items>& held)
		{
			std::vector<StoreLayout::Place> places = StoreLayout::Buildings(tribe);
			for (StoreLayout::Place& p : places)
				if (const auto it = held.find(p.Name); it != held.end()) p.Held = it->second;
			return places;
		}

		bool Perishes(const Tribe& tribe, const std::string& item)
		{
			const ItemDef* d = tribe.Items() ? tribe.Items()->Find(item) : nullptr;
			return d && d->Perishable() && tribe.IsFood(item);
		}
	}

	bool ClanStores::Active(const Tribe& tribe) { return !StoreLayout::Buildings(tribe).empty(); }

	int ClanStores::Held(const std::string& building, const std::string& item) const
	{
		const auto b = m_Held.find(building);
		if (b == m_Held.end()) return 0;
		const auto i = b->second.find(item);
		return i == b->second.end() ? 0 : i->second;
	}

	int ClanStores::InBuildings(const std::string& item) const
	{
		int n = 0;
		for (const auto& [b, items] : m_Held)
			if (const auto i = items.find(item); i != items.end()) n += i->second;
		return n;
	}

	int ClanStores::InBuildings() const
	{
		int n = 0;
		for (const auto& [b, items] : m_Held) for (const auto& [id, k] : items) n += k;
		return n;
	}

	int ClanStores::AtCamp(const Tribe& tribe, const std::string& item) const { return std::max(0, tribe.Store().Count(item) - InBuildings(item)); }

	int ClanStores::ReserveShort(const Tribe& tribe, const std::string& item) const
	{
		const auto& reserve = tribe.Config.Econ.Logistics.CampReserve;
		if (tribe.IsFood(item))
			if (const auto f = reserve.find("food"); f != reserve.end())
			{
				int food = 0;
				for (const ItemSlot& s : tribe.Store().Stacks()) if (tribe.IsFood(s.Id)) food += s.Count;
				for (const auto& [b, items] : m_Held) for (const auto& [id, k] : items) if (tribe.IsFood(id)) food -= k;
				return std::max(0, f->second - std::max(0, food));
			}
		const auto r = reserve.find(item);
		return r == reserve.end() ? 0 : std::max(0, r->second - AtCamp(tribe, item));
	}

	int ClanStores::Surplus(const Tribe& tribe, const std::string& item) const
	{
		const int camp = AtCamp(tribe, item);
		const auto& reserve = tribe.Config.Econ.Logistics.CampReserve;
		if (tribe.IsFood(item))
			if (const auto f = reserve.find("food"); f != reserve.end())
			{
				int food = 0;   // the fire's food together, past the reserve
				for (const ItemSlot& s : tribe.Store().Stacks()) if (tribe.IsFood(s.Id)) food += s.Count;
				for (const auto& [b, items] : m_Held) for (const auto& [id, k] : items) if (tribe.IsFood(id)) food -= k;
				return std::clamp(food - f->second, 0, camp);
			}
		const auto r = reserve.find(item);
		return std::max(0, camp - (r == reserve.end() ? 0 : r->second));
	}

	bool ClanStores::Find(const Tribe& tribe, const std::string& building, StoreLayout::Place& out)
	{
		for (StoreLayout::Place& p : StoreLayout::Buildings(tribe))
			if (p.Name == building) { out = std::move(p); return true; }
		return false;
	}

	std::string ClanStores::Destination(const Tribe& tribe, const std::string& item, const glm::vec2& from, StoreLayout::Place* place) const
	{
		const std::vector<StoreLayout::Place> places = Filled(tribe, m_Held);
		// food that goes off soon: the best keeper first; food that keeps long: the worst keeper with room (the good ones stay for what goes off
		// soon); the rest: the nearest. Ties: the nearest, then the first in the settlement's order
		const ItemDef* def = tribe.Items() ? tribe.Items()->Find(item) : nullptr;
		const int order = !Perishes(tribe, item) ? 0 : def && def->ShelfLife < tribe.Config.Econ.Logistics.KeepBestBelow ? 1 : -1;   // (+1: the lowest Keep wins; -1: the highest)
		auto foodRoom = [&](const StoreLayout::Place& p)   // (food that keeps long goes to a granary's food room before a storage's room for anything)
		{
			int food = 0;
			for (const auto& [id, n] : p.Held) if (tribe.IsFood(id)) food += n;
			return order < 0 && p.FoodRoom > food;
		};
		const StoreLayout::Place* best = nullptr;
		float bestDistance = 0.0f;
		bool bestRoom = false;
		for (const StoreLayout::Place& p : places)
		{
			if (StoreLayout::Free(tribe, p, item) <= 0) continue;
			const float d = glm::distance(from, p.Door);
			const bool room = foodRoom(p);
			const float keep = static_cast<float>(order) * p.Keep, bestKeep = best ? static_cast<float>(order) * best->Keep : 0.0f;   // (lower is better)
			const bool better = !best || (room && !bestRoom) || (room == bestRoom && ((order != 0 && keep < bestKeep - 1e-4f) || ((order == 0 || std::abs(keep - bestKeep) <= 1e-4f) && d < bestDistance)));
			if (better) { best = &p; bestDistance = d; bestRoom = room; }
		}
		if (!best) return {};
		if (place) *place = *best;
		return best->Name;
	}

	std::string ClanStores::Source(const Tribe& tribe, const std::string& item, int units, const glm::vec2& from, StoreLayout::Place* place) const
	{
		const int camp = AtCamp(tribe, item);
		const std::vector<StoreLayout::Place> places = Filled(tribe, m_Held);
		std::string best = "-";
		float bestDistance = 0.0f;
		bool bestEnough = false;
		auto consider = [&](const std::string& name, const glm::vec2& at, int holds, const StoreLayout::Place* p)
		{
			if (holds <= 0) return;
			const bool enough = holds >= units;
			const float d = glm::distance(from, at);
			if (best == "-" || (enough && !bestEnough) || (enough == bestEnough && d < bestDistance))
			{
				best = name;
				bestDistance = d;
				bestEnough = enough;
				if (place && p) *place = *p;
			}
		};
		consider(std::string(), tribe.Config.Economy.Camp, camp, nullptr);
		for (const StoreLayout::Place& p : places)
		{
			const auto it = p.Held.find(item);
			consider(p.Name, p.Door, it == p.Held.end() ? 0 : it->second, &p);
		}
		return best;
	}

	std::string ClanStores::FoodSource(const Tribe& tribe, const glm::vec2& from, StoreLayout::Place* place) const
	{
		int camp = 0;
		for (const ItemSlot& s : tribe.Store().Stacks()) if (tribe.IsFood(s.Id)) camp += s.Count;
		camp -= [&] { int n = 0; for (const auto& [b, items] : m_Held) for (const auto& [id, k] : items) if (tribe.IsFood(id)) n += k; return n; }();
		std::string best = camp > 0 ? std::string() : std::string("-");
		float bestDistance = camp > 0 ? glm::distance(from, tribe.Config.Economy.Camp) : 0.0f;
		for (const StoreLayout::Place& p : Filled(tribe, m_Held))
		{
			int food = 0;
			for (const auto& [id, n] : p.Held) if (tribe.IsFood(id)) food += n;
			const float d = glm::distance(from, p.Door);
			if (food <= 0 || (best != "-" && d >= bestDistance)) continue;
			best = p.Name;
			bestDistance = d;
			if (place) *place = p;
		}
		return best;
	}

	Inventory ClanStores::At(const Tribe& tribe, const std::string& building) const
	{
		if (!tribe.Items() || !Active(tribe)) return tribe.Store();
		Inventory here(tribe.Store().Slots(), 0.0f);
		for (const ItemSlot& s : tribe.Store().Stacks())
			if (const int n = building.empty() ? AtCamp(tribe, s.Id) : Held(building, s.Id); n > 0 && here.Count(s.Id) == 0) here.Add(*tribe.Items(), s.Id, n, s.Age);
		return here;
	}

	void ClanStores::Put(const std::string& building, const std::string& item, int units)
	{
		if (building.empty() || units <= 0) return;
		m_Held[building][item] += units;
	}

	int ClanStores::Take(const std::string& building, const std::string& item, int units)
	{
		const auto b = m_Held.find(building);
		if (b == m_Held.end() || units <= 0) return 0;
		const auto i = b->second.find(item);
		if (i == b->second.end()) return 0;
		const int n = std::min(units, i->second);
		i->second -= n;
		if (i->second <= 0) b->second.erase(i);
		if (b->second.empty()) m_Held.erase(b);
		return n;
	}

	void ClanStores::Reconcile(Tribe& tribe)
	{
		const std::vector<StoreLayout::Place> places = StoreLayout::Buildings(tribe);
		// an older save: a store building's things were a stockpile of its name (they move into the total, in that building)
		if (tribe.m_Items)
			for (auto pile = tribe.m_Econ.Stockpiles.begin(); pile != tribe.m_Econ.Stockpiles.end();)
			{
				const bool building = std::any_of(places.begin(), places.end(), [&](const StoreLayout::Place& p) { return p.Name == pile->Name; });
				if (!building) { ++pile; continue; }
				for (const ItemSlot& s : std::vector<ItemSlot>(pile->Store.Stacks()))
					Put(pile->Name, s.Id, pile->Store.Transfer(*tribe.m_Items, tribe.m_Store, s.Id, s.Count));
				if (pile->Store.Stacks().empty()) pile = tribe.m_Econ.Stockpiles.erase(pile);
				else ++pile;
			}
		// a building that is gone (or no longer keeps things) lets go of what it held: it lies by the fire
		for (auto b = m_Held.begin(); b != m_Held.end();)
		{
			const bool there = std::any_of(places.begin(), places.end(), [&](const StoreLayout::Place& p) { return p.Name == b->first; });
			b = there ? std::next(b) : m_Held.erase(b);
		}
		// the buildings hold no more than the total: what was taken without saying where came from the fire first, then from the buildings
		std::map<std::string, int> over;
		for (const auto& [b, items] : m_Held) for (const auto& [id, k] : items) over[id] += k;
		for (auto& [id, n] : over) n -= tribe.m_Store.Count(id);
		for (auto& [b, items] : m_Held)
			for (auto i = items.begin(); i != items.end();)
			{
				int& o = over[i->first];
				if (o > 0) { const int k = std::min(o, i->second); i->second -= k; o -= k; }
				i = i->second > 0 ? std::next(i) : items.erase(i);
			}
		for (auto b = m_Held.begin(); b != m_Held.end();) b = b->second.empty() ? m_Held.erase(b) : std::next(b);
	}

	float ClanStores::Keep(const Tribe& tribe) const
	{
		const std::vector<StoreLayout::Place> places = Filled(tribe, m_Held);
		double units = 0.0, kept = 0.0;
		for (const ItemSlot& s : tribe.Store().Stacks())
			if (Perishes(tribe, s.Id)) units += s.Count;
		double inBuildings = 0.0;
		for (const StoreLayout::Place& p : places)
			for (const auto& [id, n] : p.Held)
				if (Perishes(tribe, id)) { kept += static_cast<double>(n) * p.Keep; inBuildings += n; }
		if (units <= 0.0) return 1.0f;
		kept += std::max(0.0, units - inBuildings);   // the fire's share keeps at the base rate
		return static_cast<float>(std::clamp(kept / units, 0.0, 1.0));
	}

	std::string ClanStores::ToJson() const
	{
		Json held = Json::object();
		for (const auto& [b, items] : m_Held)
		{
			Json j = Json::object();
			for (const auto& [id, n] : items) if (n > 0) j[id] = n;
			if (!j.empty()) held[b] = j;
		}
		return Json{ { "version", 1 }, { "held", held } }.dump();
	}

	bool ClanStores::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || j.value("version", 0) < 1 || !j.contains("held") || !j["held"].is_object()) return false;
		std::map<std::string, Items> held;
		for (const auto& [b, items] : j["held"].items())
		{
			if (!items.is_object()) return false;
			for (const auto& [id, n] : items.items())
			{
				if (!n.is_number_integer()) return false;
				if (n.get<int>() > 0) held[b][id] = n.get<int>();
			}
		}
		m_Held = std::move(held);
		return true;
	}
}
