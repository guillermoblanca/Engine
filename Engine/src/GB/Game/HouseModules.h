#pragma once
// PROTOTYPE of documentation/01_diseno/TALLERES.md section 10 (D89: a module is a building in a slot of a house's yard), 2026-11-20, to try in the
// colony. A module is a BuildingDef with a "module" block, started as any site (Tribe::StartSite) in a free slot of the yard (GB/Game/Yard.h) of a
// standing house of the clan; the clan carries its materials and builds it like any other site, and once it stands everything that reads the
// buildings (its station, storeCap, keep...) reads it as before. What this class keeps is which site is a module of which house and in which slot
// (saved under "modules" only when there is one), and it draws the fence of a yard that has a module: posts and rails made by code from
// yards.json, no model (entities "<house> Yard ...", rebuilt by name, so a load that brought them back does not double them).
//
//   game module <house>                       the yard: its modules and its free slots
//   game module add <house> <module> [now]    the module's site in the first free slot of its kind ("now": it stands at once, for tests and captures)
//   game module remove <house> <module|site|slot>   pulls it down: what it cost (or was brought) goes back to the store
//
// The tribe owns one (Tribe::Modules()) and calls Step from one line of Update; with no module nothing here runs and nothing is saved, so the
// clans of the AI and GBSim behave as before (the planner never raises a module that is not "standalone": SettlementPlanner.cpp). CPU only.
#include "GB/Game/Yard.h"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;

	class HouseModules
	{
	public:
		struct Reply
		{
			bool Ok = false;
			std::string Text;   // English (the commands' answers)
		};
		struct Entry
		{
			std::string Host, Slot;
		};
		// A slot of a house's yard as the UI shows it
		struct SlotView
		{
			YardSlot Slot;
			std::string Site;       // the module standing (or being built) there ("" = free)
			std::string Module;     // its building id
			bool Done = false;
		};
		// A module the house could take, as the UI shows it
		struct Offer
		{
			std::string Id;
			bool Known = false;              // the clan knows how to raise it
			std::vector<std::string> Techs;  // what would open it (any of them) when it does not
			bool Room = false;               // there is a free slot of its kinds
		};

		// The yard of a standing house of the clan (Valid false when `host` is none, not standing or has no yard block).
		static YardShape YardOf(const Tribe& tribe, const std::string& host);
		std::vector<SlotView> Slots(const Tribe& tribe, const std::string& host) const;
		std::vector<Offer> Offers(const Tribe& tribe, const std::string& host) const;
		int Count(const std::string& host) const;
		int Max(const Tribe& tribe, const std::string& host) const;
		const Entry* Of(const std::string& site) const;
		// Where the worker of a module stands (its slot's work point); false when `site` is no module.
		bool UseSpot(const Tribe& tribe, const std::string& site, glm::vec2& at) const;

		Reply Add(Tribe& tribe, const std::string& host, const std::string& module, bool now = false);
		Reply Remove(Tribe& tribe, const std::string& host, const std::string& what);
		std::string Status(const Tribe& tribe, const std::string& host) const;

		// ---- the tribe's hook: forgets the modules whose site is gone and keeps the fences in step (once a second; nothing with no module)
		void Step(Tribe& tribe);

		bool Empty() const { return m_Sites.empty(); }
		void Clear() { m_Sites.clear(); m_Fenced.clear(); }
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		void BuildFence(Tribe& tribe, const std::string& host) const;
		static void TakeDownFence(Tribe& tribe, const std::string& host);
		std::map<std::string, Entry> m_Sites;   // module site -> its house and slot (saved)
		std::map<std::string, size_t> m_Fenced; // house -> the posts of the fence it has now (not saved: rebuilt by name)
		double m_Next = 0.0;
	};
}
