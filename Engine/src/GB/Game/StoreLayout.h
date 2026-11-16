#pragma once
// T248 + T255: the store buildings of a clan and the rule of what goes in which.
//
// A clan's things are one total (Tribe::Store: every count, cap and query reads it); where they lie is ClanStores' ledger (GB/Game/ClanStores.h):
// so much of each item in each finished store building, the rest by the fire. This class holds no state: it lists the store buildings (Buildings),
// says how much room one still has for an item given what it holds (Free), and draws the real layout for the panel, the piles and GBSim (Of).
// The rule of the share-out (ClanStores::Destination): food that goes off goes to the building that keeps it best (the lowest Keep) with room for
// it, the nearest of those; anything else to the nearest building with room for it (a storage: its store kind's cap x slots; any building's
// StoreCap). A building's FoodCap takes only food; its Room takes anything.
#include <glm/vec2.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;

	class StoreLayout
	{
	public:
		struct Place
		{
			std::string Name;                   // the building's name (as in Tribe::SettlementSites)
			std::string Kind;                   // its BuildingDef id (T256: where its things are drawn inside it, visuals.json storeSlots)
			glm::vec2 Pos{ 0.0f };              // its centre
			glm::vec2 Door{ 0.0f };             // where people stand to leave and take things (its front)
			float Reach = 4.0f;                 // a deposit or a take counts within this of its centre
			float Keep = 1.0f;                  // its BuildingDef::Keep (how fast food goes off in it)
			int FoodRoom = 0;                   // units of food it holds (FoodCap)
			int Room = 0;                       // units of anything (StoreCap, and its store kind's cap x slots)
			std::map<std::string, int> Held;    // item -> units of the clan's store that lie in it
			int Units() const;
		};

		// The clan's finished store buildings (Held left empty), in the order of the settlement (deterministic).
		static std::vector<Place> Buildings(const Tribe& tribe);
		// Room left in a place for more of an item, given what it holds: food fills FoodRoom first, then Room; the rest only Room.
		static int Free(const Tribe& tribe, const Place& place, const std::string& item);

		// The real layout: each building with what the ledger says it holds, and what lies by the fire.
		static StoreLayout Of(const Tribe& tribe);

		const std::vector<Place>& Places() const { return m_Places; }
		const Place* Find(const std::string& building) const;
		const std::map<std::string, int>& AtCamp() const { return m_Camp; }   // what lies by the fire
		int InBuildings() const;
		int AtCampUnits() const;

	private:
		std::vector<Place> m_Places;
		std::map<std::string, int> m_Camp;
	};
}
