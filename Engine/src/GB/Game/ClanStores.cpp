// T255: the clan's store buildings as the place its things are kept (see ClanStores.h).
#include "GB/Game/ClanStores.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
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
		Json j{ { "version", 2 }, { "held", held } };
		if (!m_Upgrades.empty()) j["upgrades"] = m_Upgrades;   // T256
		if (m_Spoiled > 0.0) { j["spoiled"] = m_Spoiled; j["spoiledAt"] = m_SpoiledAt; }
		if (m_Upgraded > 0) j["upgraded"] = m_Upgraded;
		return j.dump();
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
		// T256 (version 2): the stores being rebuilt, the food lost lately and how many were rebuilt (a version 1 save has none of them)
		std::map<std::string, std::string> upgrades;
		if (j.contains("upgrades"))
		{
			if (!j["upgrades"].is_object()) return false;
			for (const auto& [site, old] : j["upgrades"].items())
			{
				if (!old.is_string()) return false;
				upgrades[site] = old.get<std::string>();
			}
		}
		const double spoiled = j.value("spoiled", 0.0), spoiledAt = j.value("spoiledAt", 0.0);
		const int upgraded = j.value("upgraded", 0);
		m_Held = std::move(held);
		m_Upgrades = std::move(upgrades);
		m_Spoiled = std::max(0.0, spoiled);
		m_SpoiledAt = spoiledAt;
		m_Upgraded = std::max(0, upgraded);
		return true;
	}

	// ------------------------------------------------------------------ T256: food lost lately
	void ClanStores::NoteSpoiled(int units, double now, float memory)
	{
		if (units <= 0) return;
		m_Spoiled = static_cast<double>(SpoiledLately(now, memory)) + static_cast<double>(units);
		m_SpoiledAt = now;
	}

	int ClanStores::SpoiledLately(double now, float memory) const
	{
		if (m_Spoiled <= 0.0) return 0;
		const double age = std::max(0.0, now - m_SpoiledAt);
		return static_cast<int>(std::lround(m_Spoiled * std::exp(-age / std::max(1.0, static_cast<double>(memory)))));
	}

	// ------------------------------------------------------------------ T256: the fire's reserve filled from the buildings
	const char* const ClanStores::kRestock = "the fire's reserve";

	namespace
	{
		std::string GoTo(const glm::vec2& at, const std::string& tail)
		{
			char b[64];
			std::snprintf(b, sizeof b, "goto %.2f %.2f | ", static_cast<double>(at.x), static_cast<double>(at.y));
			return b + tail;
		}

		// a member's own spot round a door (the golden angle, as the economy's), on ground it can stand on
		glm::vec2 Beside(const glm::vec2& at, size_t index, const ITribeBody* body)
		{
			for (size_t k = 0; k < 8; ++k)
			{
				const float a = static_cast<float>(index + k) * 2.3999632f;
				const glm::vec2 p = at + glm::vec2(std::cos(a), std::sin(a)) * 0.9f;
				if (!body || body->CanStandAt(p)) return p;
			}
			return at;
		}

		// what the fire lacks of each item of its reserve that the buildings (or one) hold ("food": the foods they hold, the lack shared out)
		std::map<std::string, int> Lacking(const Tribe& tribe, const ClanStores& stores, const std::string& building = std::string())
		{
			std::map<std::string, int> out;
			for (const auto& [key, units] : tribe.Config.Econ.Logistics.CampReserve)
			{
				if (key == "food")
				{
					int lack = -1;
					for (const auto& [b, items] : stores.Ledger())
						if (building.empty() || b == building)
							for (const auto& [id, n] : items)
							{
								if (!tribe.IsFood(id) || n <= 0) continue;
								if (lack < 0) lack = stores.ReserveShort(tribe, id);
								if (const int k = std::min(lack, n); k > 0) { out[id] += k; lack -= k; }
							}
					continue;
				}
				const int lack = stores.ReserveShort(tribe, key);
				int held = 0;
				for (const auto& [b, items] : stores.Ledger())
					if (building.empty() || b == building)
						if (const auto i = items.find(key); i != items.end()) held += i->second;
				if (const int k = std::min(lack, held); k > 0) out[key] = k;
			}
			return out;
		}
	}

	void ClanStores::PostRestock(Tribe& tribe) const
	{
		if (!tribe.m_Items || !Active(tribe) || m_Held.empty()) return;
		int units = 0;
		bool low = false;   // (a fire short of half of some reserve: a trip worth making)
		const std::map<std::string, int> lack = Lacking(tribe, *this);
		for (const auto& [id, n] : lack) units += n;
		for (const auto& [key, want] : tribe.Config.Econ.Logistics.CampReserve)
		{
			int shortBy = 0;
			if (key == "food") { for (const auto& [id, n] : lack) if (tribe.IsFood(id)) shortBy += n; }
			else if (const auto it = lack.find(key); it != lack.end()) shortBy = it->second;
			low = low || (want > 0 && 2 * shortBy >= want);
		}
		if (units <= 0 || !low) return;
		tribe.m_Board.Post("haul", kRestock, "", static_cast<float>(units), tribe.Config.Econ.Logistics.Priority * 1.2f, 1,
			"the fire is short of " + std::to_string(units) + " things of its reserve: they are in the store buildings", tribe.m_Time, tribe.Config.Work.Jobs);
	}

	bool ClanStores::StartRestock(Tribe& tribe, TribeMember& m) const
	{
		if (!m.Body) return false;
		// the building that holds the most of what the fire lacks (ties: the nearest)
		const glm::vec2 from = tribe.Where(m);
		const std::vector<StoreLayout::Place> places = StoreLayout::Buildings(tribe);
		const StoreLayout::Place* best = nullptr;
		int bestUnits = 0;
		float bestDistance = 0.0f;
		for (const StoreLayout::Place& p : places)
		{
			int units = 0;
			for (const auto& [id, n] : Lacking(tribe, *this, p.Name)) units += n;
			const float d = glm::distance(from, p.Door);
			if (units > 0 && (!best || units > bestUnits || (units == bestUnits && d < bestDistance))) { best = &p; bestUnits = units; bestDistance = d; }
		}
		if (!best) return false;
		if (!tribe.Order(m, GoTo(Beside(best->Door, tribe.IndexOf(m), m.Body), "wait 1"))) return false;
		m.Job = "haul";
		m.Detail = std::string(kRestock) + "@" + best->Name;
		tribe.Say(m, "goes to " + best->Name + " for what the fire is short of");
		return true;
	}

	bool ClanStores::FinishRestock(Tribe& tribe, TribeMember& m)
	{
		const size_t at = m.Detail.find('@');
		const std::string building = at == std::string::npos ? std::string() : m.Detail.substr(at + 1);
		StoreLayout::Place place;
		if (building.empty() || !m.Body || !tribe.m_Items || !Find(tribe, building, place)) return true;
		if (glm::distance(tribe.Where(m), place.Door) > place.Reach) { tribe.Say(m, "could not get to " + building); return true; }
		Inventory& bag = m.Body->Bag();
		int room = tribe.CarryLimit(m), moved = 0;
		for (const auto& [id, n] : Lacking(tribe, *this, building))
		{
			if (room <= 0) break;
			const int k = tribe.m_Store.Transfer(*tribe.m_Items, bag, id, std::min(room, n));
			Take(building, id, k);
			room -= k;
			moved += k;
		}
		if (moved == 0) return true;
		tribe.m_Econ.Hauled += moved;
		if (const Job* held = tribe.m_Board.ClaimOf(m.Name); held && held->Kind == "haul") tribe.m_Board.Progress(m.Name, static_cast<float>(moved), tribe.m_Time);
		tribe.Say(m, "takes " + std::to_string(moved) + " things from " + building + " to the fire");
		if (tribe.Order(m, GoTo(tribe.CampSpot(m), "wait 1"))) { m.Job = "deposit"; m.Detail = "hauled"; }
		return true;
	}

	// ------------------------------------------------------------------ T256: a food store rebuilt into a better one
	void ClanStores::PlanUpgrade(Tribe& tribe)
	{
		if (!tribe.m_Buildings || !tribe.m_Entities || !tribe.m_Gameplay) return;
		// a better store that stands takes what the old one held; the old one comes down
		for (auto it = m_Upgrades.begin(); it != m_Upgrades.end();)
		{
			bool standing = false, gone = true, oldThere = false;
			for (const Tribe::Placed& p : tribe.Placements())
			{
				if (p.Name == it->first) { gone = false; standing = p.State.Done; }
				if (p.Name == it->second) oldThere = true;
			}
			if (gone || !oldThere) { it = m_Upgrades.erase(it); continue; }
			if (!standing) { ++it; continue; }
			const std::string site = it->first, old = it->second;
			StoreLayout::Place to;
			int moved = 0;
			if (Find(tribe, site, to))
				if (const auto held = m_Held.find(old); held != m_Held.end())
					for (const auto& [id, n] : Items(held->second))
					{
						const auto now = m_Held.find(site);
						to.Held = now != m_Held.end() ? now->second : Items();
						const int k = std::min(n, StoreLayout::Free(tribe, to, id));
						if (k <= 0) continue;
						Put(site, id, Take(old, id, k));
						moved += k;
					}
			m_Held.erase(old);   // (what did not fit lies by the fire)
			if (const EntityId e = tribe.m_Entities->FindByName(old); e != NullEntity) tribe.m_Entities->Destroy(e);
			tribe.m_Gameplay->Remove(old);
			tribe.ForgetPlacements();
			++m_Upgraded;
			char t[48];
			std::snprintf(t, sizeof t, "%6.1f ", tribe.m_Time);
			tribe.m_Log.push_back(std::string(t) + "the clan moves " + std::to_string(moved) + " things from " + old + " into " + site + " and pulls " + old + " down: a better store");
			it = m_Upgrades.erase(it);
		}
		// one at a time, while food goes off or lies by the fire with no room, and a site is free
		const LogisticsRules& rules = tribe.Config.Econ.Logistics;
		if (rules.FoodStoreAt <= 0 || !m_Upgrades.empty() || !tribe.HasFire()) return;
		int open = 0, alive = 0;
		bool foodSite = false;   // (a food store being raised already answers the food: one at a time, as the planner)
		for (const Tribe::Placed& p : tribe.Placements()) if (!p.State.Done) { ++open; foodSite = foodSite || (p.Def && p.Def->FoodCap > 0); }
		for (const TribeMember& m : tribe.m_Members) if (!m.Dead) ++alive;
		if (alive == 0 || foodSite || open >= tribe.Config.Econ.Construction.SitesFor(alive)) return;
		int campFood = 0;
		for (const ItemSlot& s : tribe.m_Store.Stacks())
			if (tribe.IsFood(s.Id))
				if (const int n = Surplus(tribe, s.Id); n > 0 && Destination(tribe, s.Id, tribe.Config.Economy.Camp).empty()) campFood += n;
		if (SpoiledLately(tribe.m_Time, rules.SpoilMemory) + campFood < rules.FoodStoreAt) return;
		auto payable = [&](const BuildingDef& def)
		{
			for (const auto& [item, n] : def.TotalNeeds())
			{
				bool from = tribe.m_Store.Count(item) > 0;
				for (const auto& [kind, node] : tribe.m_Defs) from = from || node.Item == item;
				if (!from) return false;
			}
			return true;
		};
		// the worst-keeping food store that a better one the clan knows and can pay supersedes
		const Tribe::Placed* old = nullptr;
		const BuildingDef* better = nullptr;
		const auto placed = tribe.Placements();   // (kept while `old` points into it)
		for (const Tribe::Placed& p : placed)
		{
			if (!p.State.Done || !p.Def || p.Def->FoodCap <= 0) continue;
			if (old && p.Def->Keep <= old->Def->Keep) continue;
			const BuildingDef* best = nullptr;
			for (const auto& [id, def] : *tribe.m_Buildings)
			{
				if (def.FoodCap <= p.Def->FoodCap || def.Keep >= p.Def->Keep) continue;
				if (std::find(def.Supersedes.begin(), def.Supersedes.end(), p.Def->Id) == def.Supersedes.end()) continue;
				if (!tribe.ClanKnowsBuilding(def) || !payable(def)) continue;
				if (!best || def.Keep < best->Keep) best = &def;
			}
			if (best) { old = &p; better = best; }
		}
		if (!old) return;
		SitePlacement where = tribe.SitePlacementFor(*better);
		const glm::vec2 near = old->Pos;
		where.Rings = std::max(5, static_cast<int>((tribe.VillageReach() - where.SiteRadius) / 3.0f));
		where.Prefer = [near](const glm::vec2& p) { return -glm::distance(p, near); };   // as near the old store as it fits
		const std::optional<glm::vec2> spot = PlaceSite(where);
		if (!spot) return;
		const std::string oldName = old->Name;
		std::string site;
		if (!tribe.StartSite(better->Id, *spot, &site).empty() || site.empty()) return;
		m_Upgrades[site] = oldName;
		tribe.m_Log.back() += ": a better store for the clan's food than " + oldName;
	}
}
