#pragma once
// documentation/01_diseno/TALLERES.md section 10 (D89: yards, modules, closures and sizes). Prototype of 2026-11-20, made playable by T367.
//
// A module is a BuildingDef with a "module" block, started as any site (Tribe::StartSite) in a free slot of the yard (GB/Game/Yard.h) of a
// standing house of the clan; the clan carries its materials and builds it like any other site, and once it stands everything that reads the
// buildings (its station, storeCap, keep...) reads it as before. This class keeps which site is a module of which house and in which slot (saved
// under "modules" only when there is one) and, per house (saved under "yards" only when there is one):
//   - its size (TALLERES 10.4, Y-1): S is the building as its data has it; M is the same building with the values of yards.json "sizes" (a
//     derived BuildingDef that Tribe::Placements hands out, so beds, warmth and storeCap read it with no other change); L is another building of
//     the data (the hut's L is the longhouse: the house turns into one in its place). A longhouse of an old save is a hut of size L as it is.
//   - its closure (T367f): fence, wicker, drystone (yards.json "closures"); it draws the fence and adds its defense (Tribe::Defense, capped).
//   - the work in course on it (TALLERES 10.2 point 4): growing or closing is a work on the standing house, which stays in use with its old
//     values (its beds count) until the work is done. The clan brings its materials and works it with the options "deliver" and "build" whose
//     target is the house (WorkOptions, ExecuteWork, FinishWork: one line each in Settlement.cpp).
// From the first module or a closure the yard is ground taken (decision Y-2: TakenGround reads Ground); two yards may touch.
//
//   game module <house>                              the yard: its modules and its free slots
//   game module add <house> <module> [now]           the module's site in the first free slot of its kind ("now": it stands at once)
//   game module remove <house> <module|site|slot>    pulls it down: what it cost (or was brought) goes back to the store
//   game yard <house>                                size, closure, modules and the work in course
//   game yard <house> closure <fence|wicker|drystone> [now]   the work of that closure (its cost by the metres of fence)
//   game yard <house> cancel                         calls the work in course off (what was brought goes back to the store)
//   game grow <house> M|L [now]                      the work of growing it in its place (the cost is the difference)
//
// The tribe owns one (Tribe::Modules()) and calls Step from one line of Update; with no module and no house state nothing here runs and nothing
// is saved. CPU only, deterministic.
#include "GB/AI/Utility.h"
#include "GB/Game/JobBoard.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/Yard.h"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;

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
		// A work on a standing house (T367f/g): growing or closing its yard. What it needs is fixed when it starts (saved with it).
		struct Work
		{
			std::string Kind;                 // "grow" or "closure" ("" = none)
			std::string To;                   // the size ("M") or the closure ("wicker")
			std::map<std::string, int> Needs;
			std::map<std::string, std::vector<std::string>> Substitutes;
			std::map<std::string, int> Got;   // brought (a substitute counts as the item it stands for)
			float Done = 0.0f, Needed = 0.0f; // seconds of work
			bool Any() const { return !Kind.empty(); }
			std::map<std::string, int> Missing() const;
			bool Ready() const { return Missing().empty(); }
			float Progress() const;           // 0..1: half the materials, half the work
		};
		// What the UI shows of a house's yard, size and closure (T367 step 6). Empty `Line` = the house has no sizes; `Size` is still "S".
		struct Info
		{
			bool Valid = false;               // a standing house of the clan
			std::string Line;                 // the host id of its line of sizes ("hut"), "" = none
			std::string Size = "S", NextSize; // its size and the next one ("" = none)
			int Beds = 0, NextBeds = 0, MaxModules = 0, NextMaxModules = 0, Modules = 0;
			std::map<std::string, int> NextSizeCost;   // what growing to NextSize would cost now (the difference, the closure's metres included)
			float NextSizeWork = 0.0f;
			bool NextSizeKnown = false;       // the clan knows how
			std::string NextSizeTech;         // what opens it when it does not
			std::string GrowBlocked;          // why it cannot grow now ("" = it can: known, no work in course, room on the ground)
			bool HasYard = false, Reserved = false;   // a yard block; its ground taken (a module or a closure)
			float FenceLength = 0.0f;         // metres of fence of its yard now (the gate left out)
			int Closure = 0;                  // its level (0 = none)
			std::string ClosureId, NextClosure;   // "" = none
			std::map<std::string, int> NextClosureCost;
			float NextClosureWork = 0.0f;
			bool NextClosureKnown = false;
			std::string NextClosureTech;
			std::string ClosureBlocked;       // why the next closure cannot start now ("" = it can)
			Work Current;                     // the work in course (Kind "" = none)
			float Progress = 0.0f;            // of the work in course, 0..1
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

		// ---- T367: sizes, closures and works on a house (HouseWorks.cpp)
		Info InfoOf(const Tribe& tribe, const std::string& house) const;
		std::string SizeOf(const Tribe& tribe, const std::string& house) const;   // "S" when it has no sizes
		int ClosureOf(const std::string& house) const;
		const Work* WorkOn(const std::string& house) const;
		bool Reserved(const std::string& house) const;   // its yard is ground taken (a module, a closure or a closure being made)
		Reply Grow(Tribe& tribe, const std::string& house, const std::string& size, bool now = false);
		Reply SetClosure(Tribe& tribe, const std::string& house, const std::string& closure, bool now = false);
		Reply CancelWork(Tribe& tribe, const std::string& house);
		std::string YardStatus(const Tribe& tribe, const std::string& house) const;

		// ---- the tribe's hooks (one line each)
		void Step(Tribe& tribe);   // forgets the modules and houses that are gone and keeps the fences and the sizes' look in step (once a second)
		const BuildingDef* Variant(const std::string& name, const BuildingDef* def) const;   // Tribe::Placements: the def of the house's size
		void Ground(const Tribe& tribe, std::vector<GroundRect>& out) const;   // TakenGround: the reserved yards, the bigger sizes, the ground of a growth
		float ClosureDefense() const;                                           // Tribe::Defense: the closures', at most YardRules::DefenseCap
		void WorkOptions(const Tribe& tribe, TribeMember& m, std::vector<UtilityOption>& opts) const;   // SettlementOptions
		int ExecuteWork(Tribe& tribe, TribeMember& m, const UtilityOption& o);    // ExecuteSettlement: -1 not a work on a house
		bool FinishWork(Tribe& tribe, TribeMember& m, const std::string& job);   // FinishSettlement: false not a work on a house
		void WorkDemand(const Tribe& tribe, std::map<std::string, std::map<std::string, int>>& now) const;   // PlanSettlement: what the works lack
		void PostJobs(Tribe& tribe) const;   // PostSettlementJobs: "deliver <house>" and "build <house>" on the job board, as a site's
		float JobScore(const Tribe& tribe, const TribeMember& m, const Job& job) const;   // SettlementJobScore: -1 not a work on a house
		void OpenWorks(std::vector<std::string>& out) const;   // ColonyDesignations: the houses with a work in course (open sites of the queue)
		bool WorkOpenFor(const Tribe& tribe, const std::string& house, const std::string& kind, const TribeMember* asking) const;   // ... that work can take `kind` now
		void Facts(const Tribe& tribe, SettlementFacts& facts) const;            // BuildingNeeds: modules with room, the ids a size absorbs
		int PlanNeed(Tribe& tribe, const BuildingNeed& need);   // PlanSettlement: 1 answered (a module in a slot, a house grown), -1 skip it, 0 as before
		bool PlanGrow(Tribe& tribe, bool any = false);   // UpgradeHouse: a household with more members than beds grows its house first (any: or any house)
		static bool Absorbs(const std::string& id);      // a building a size of a house stands for (the longhouse): grown into, never raised on its own
		bool PlanClosure(Tribe& tribe);   // PlanSettlement with nothing else to build: a yard closed (low priority)

		bool Empty() const { return m_Sites.empty(); }
		bool NoHouses() const { return m_Houses.empty(); }
		void Clear() { m_Sites.clear(); m_Fenced.clear(); m_Houses.clear(); }
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake
		std::string HousesToJson() const;          // the "yards" section (version 1)
		bool HousesFromJson(const std::string& json);

	private:
		struct House
		{
			std::string Size;   // "" = the building's own (S, or L for a longhouse)
			int Closure = 0;
			Work Doing;
			bool Empty() const { return Size.empty() && Closure == 0 && !Doing.Any(); }
		};
		void BuildFence(Tribe& tribe, const std::string& host) const;
		static void TakeDownFence(Tribe& tribe, const std::string& host);
		void Complete(Tribe& tribe, const std::string& house);
		int Bring(const Tribe& tribe, const std::string& house, const Work& w, std::string* what) const;   // units the store can send the work now (nobody bringing them)
		int Builders(const Tribe& tribe, const std::string& house, const TribeMember* asking) const;
		std::string FreeHost(const Tribe& tribe, const BuildingDef& module) const;   // the house whose yard takes the module now ("" = none)
		// why `house` as `to` at `at` would not fit ("" = it fits): its yard (`yard`) or its walls against the rest of the clan's ground, the nodes, water
		std::string GrowRoom(const Tribe& tribe, const std::string& house, const BuildingDef& to, const glm::vec2& at, float yaw, bool yard) const;
		std::map<std::string, Entry> m_Sites;   // module site -> its house and slot (saved)
		std::map<std::string, House> m_Houses;  // house -> its size, closure and work (saved under "yards")
		std::map<std::string, size_t> m_Fenced; // house -> the posts of the fence it has now (not saved: rebuilt by name)
		mutable std::string m_Placing;          // the house a module is being placed in: its own yard is not in the way
		bool m_Planning = false;                // a module the planner places (not the player's: not in the queue of the player's sites)
		double m_Next = 0.0;
	};
}
