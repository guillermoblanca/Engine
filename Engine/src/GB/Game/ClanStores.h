#pragma once
// T255: the clan's store buildings as the place its things are kept.
//
// The clan's things stay one total (Tribe::Store): every count, cap, recipe and query reads it, so the clan's food, its winter stock, the bag
// panel and "game status" sum everything wherever it lies. This class is the ledger of WHERE: so many units of each item in each finished store
// building (storage, cache pit, granary, jar store, cellar...); what the ledger does not place lies by the fire (the camp's pile).
//  - Leaving: once a clan has a store building, a deposit goes to a building (Destination: perishable food to the best keeper with room, the rest
//    to the nearest with room); the member walks to its door and leaves it there. What fits in no building goes to the fire, as before any.
//  - Taking: who eats, delivers to a site, makes at a station or takes a tool walks to the building that holds it (Source) and takes it there.
//  - The fire keeps a small reserve at hand (economy.logistics.campReserve): deposits fill it first; the rest of its pile is hauled to the
//    buildings (a "haul" job from the camp), so the first store built empties the old pile by the fire.
//  - Food keeps where it lies: the store spoils at the mean rate of where its food is (Keep).
// Invariant: for each item the buildings hold no more than the total (Reconcile trims them after any take that did not say where: the fire's
// share goes first). Saved with the tribe ("stores", version 1); an older save has none: everything lies by the fire and is hauled in.
#include "GB/Game/StoreLayout.h"
#include "GB/Gameplay/Items.h"
#include <glm/vec2.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;

	class ClanStores
	{
	public:
		using Items = std::map<std::string, int>;

		// at least one finished store building: the deposits go to buildings
		static bool Active(const Tribe& tribe);
		// What lies in a building; and in all of them, of an item.
		int Held(const std::string& building, const std::string& item) const;
		int InBuildings(const std::string& item) const;
		int InBuildings() const;
		const std::map<std::string, Items>& Ledger() const { return m_Held; }
		// What of an item lies by the fire (the total less what the buildings hold).
		int AtCamp(const Tribe& tribe, const std::string& item) const;
		// The camp's reserve of an item (economy.logistics.campReserve; "food" is any food, counted together) and how short of it the fire is.
		int ReserveShort(const Tribe& tribe, const std::string& item) const;
		// What of an item lies by the fire past its reserve (what a hauler carries to the buildings).
		int Surplus(const Tribe& tribe, const std::string& item) const;

		// Where a deposit of `item` goes: the name of a building with room for it, "" when none has (it goes to the fire).
		std::string Destination(const Tribe& tribe, const std::string& item, const glm::vec2& from, StoreLayout::Place* place = nullptr) const;
		// Where `item` is taken from: "" the fire, a building's name, "-" nowhere. The nearest that holds some (the fire when it holds enough).
		std::string Source(const Tribe& tribe, const std::string& item, int units, const glm::vec2& from, StoreLayout::Place* place = nullptr) const;
		// Where food is taken from (any food): "" the fire, a building's name, "-" nowhere; the nearest that holds some.
		std::string FoodSource(const Tribe& tribe, const glm::vec2& from, StoreLayout::Place* place = nullptr) const;
		// What lies at a place ("" the fire, a building), as an inventory of the store's stacks (the whole store while the clan has no building).
		Inventory At(const Tribe& tribe, const std::string& building) const;
		// The building named, if it is a finished store building.
		static bool Find(const Tribe& tribe, const std::string& building, StoreLayout::Place& out);

		// The ledger: units left in a building (the total grew by them), or taken from it (the total shrank, or they went to the fire's share).
		void Put(const std::string& building, const std::string& item, int units);
		int Take(const std::string& building, const std::string& item, int units);

		// The ledger follows the total and the buildings: a building that is gone lets go of what it held (it lies by the fire), a building holds
		// no more than the total (the fire's share is used first, then the buildings in name order), nor more than its room. A store building of
		// an older save kept its things in a stockpile of its name: they move into the total and its ledger.
		void Reconcile(Tribe& tribe);
		// How the store's food keeps: the mean of where its food lies (the fire at 1, each building at its Keep), for the spoilage.
		float Keep(const Tribe& tribe) const;

		// T256: the food the store lost lately: what went off, fading over economy.logistics.spoilMemory (the planner answers it with a better
		// food store, SettlementFacts::SpoiledLately).
		void NoteSpoiled(int units, double now, float memory);
		int SpoiledLately(double now, float memory) const;

		// T256: the fire's reserve filled again from the buildings. A "haul" job of the fire's reserve (kRestock) while the fire is short of it and
		// a building holds some: the hauler walks to the building that holds it, takes what the fire lacks and leaves it by the fire (a "hauled"
		// deposit, which the fire takes up to its reserve). The haul of the camp's pile only carries what is past the reserve: the two never cross.
		static const char* const kRestock;
		void PostRestock(Tribe& tribe) const;
		bool StartRestock(Tribe& tribe, TribeMember& m) const;    // ExecuteEconomy of the job: false when there is nothing to fetch
		bool FinishRestock(Tribe& tribe, TribeMember& m);         // at the building's door: what it takes, and the walk to the fire

		// T256: a food store rebuilt into a better one the clan knows (the data's "supersedes": the granary over the cache pit), when food goes off
		// or lies by the fire with no room: the better one is raised beside it as an ordinary site; when it stands, what the old one held moves into
		// it (up to its room, the rest lies by the fire) and the old one is pulled down. One at a time. Called with the settlement's plan.
		void PlanUpgrade(Tribe& tribe);
		int Upgraded() const { return m_Upgraded; }
		const std::map<std::string, std::string>& Upgrades() const { return m_Upgrades; }   // new site -> the store it replaces

		bool Empty() const { return m_Held.empty() && m_Upgrades.empty() && m_Spoiled <= 0.0 && m_Upgraded == 0; }
		std::string ToJson() const;
		bool FromJson(const std::string& json);
		void Clear() { m_Held.clear(); m_Upgrades.clear(); m_Spoiled = 0.0; m_SpoiledAt = 0.0; m_Upgraded = 0; }

	private:
		std::map<std::string, Items> m_Held;   // building -> item -> units
		std::map<std::string, std::string> m_Upgrades;   // T256: new site -> the store it replaces
		double m_Spoiled = 0.0, m_SpoiledAt = 0.0;       // T256: food lost lately, as of this time
		int m_Upgraded = 0;                              // T256: stores rebuilt into better ones
	};
}
