#pragma once
// T280 (A1 of documentation/01_diseno/AGRICULTURA.md, P-F1 and P-F2 for the player; MODO_COLONIA.md §4.4): the farm zones the player paints for
// the clan in the colony.
//
// A zone is a field (GB/Gameplay/Farming.h: plots 2 m apart) the player marks out on the ground with `game zone farm x0 z0 x1 z1 [crop]` (a
// rectangle) or `game zone farm poly x0 z0 x1 z1 x2 z2 ... [crop]` (a perimeter of points: the plots are the cells whose middle falls inside,
// Field::Cells), with a crop and a plan of its own. The perimeter is kept (Zone::Outline) for the fences and gates it will carry (MURALLAS.md). It is kept with the clan's other fields (EconomyState::Fields: it grows, is drawn, saved and keeps
// its ground like them) and this class keeps what is the player's on top: which fields are painted zones, their crop and their plan.
//   crop  the crop it is sown with ("" = any the clan can sow now: the first by id with seed in the store)
//   auto  the clan chooses the crop each time it sows (AutoCrop: the one whose produce the store has least of; with farming.foodDays, food
//         first), not its size nor its place. A crop with a technology of its own (crops[].tech) the clan does not know is never sown (T344)
//   plan  "sow": the whole cycle (till, sow, water, harvest); "harvest": what grows is watered and harvested, nothing new is tilled or sown;
//         "fallow": the ground rests (only what is ripe is harvested)
// The work of every field goes on the clan's job board as before: one "farm" job per action (till, sow, tend, harvest) for all its fields, so
// the table of priorities of T306 (category "field") and its marks rule it as any other work. A bigger zone has more plots: more work.
//
// Who paints (decision A-4). In the colony the player's clan (SetPlayer, from WorldDirector::Update as the table of T306) only gets the fields
// the player paints: it marks out no field of its own (the ones it marked before the colony started are still worked). The clans of the AI and
// GBSim mark out their own zones (Post): with farming.plotsPerMember in the data, that many plots per living member in zones of
// farming.zonePlots [min, max] plots (a new one while what they have falls short by half a smallest zone or more); without it, the old rule
// (FarmRules::FieldsFor fields of `plots` plots), so a run is the same as before. Their fields are in "auto" (AutoCrop). With no zone of the
// player nothing here is saved.
//
// The tribe owns one (Tribe::Zones()) and calls it from one line in each place: PostEconomyJobs (Post), EconomyJobScore and EconomyOptions
// (WorkPlace), ExecuteEconomy (Pick). Saved under "farmzones" when there is a zone. CPU only, deterministic.
#include "GB/Game/Scenery.h"
#include "GB/Gameplay/Ecology.h"
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	class Field;
	class AbilitySystemComponent;
	struct ResourceNodeDef;

	class FarmZones
	{
	public:
		static constexpr int kMostPlots = 400;      // the largest zone (20 x 20 plots, 40 m a side)
		static constexpr const char* kPrefix = "zone ";

		struct Zone
		{
			std::string Crop;            // "" = any the clan can sow now
			std::string Plan = "sow";    // "sow", "harvest" or "fallow"
			bool Auto = false;           // the clan chooses the crop (AutoCrop)
			std::vector<glm::vec2> Outline;   // the perimeter painted (x, z points; a rectangle is its 4 corners): the fences of tomorrow (MURALLAS.md)
			bool operator==(const Zone&) const = default;
		};
		struct Reply
		{
			bool Ok = false;
			std::string Text;   // English (the commands' answers and the tests read it)
		};

		// The player's clan in the colony: it marks out no field of its own.
		void SetPlayer(bool on) { m_Player = on; }
		bool Player() const { return m_Player; }

		// ---- the player's zones
		// Paints a zone over the rectangle of corners a and b (x, z; the plots' middles, 2 m apart), with a crop or "" for any. Refused: a crop the
		// data has not, more than kMostPlots plots, ground taken (a building, a site, a field: plot by plot) or a plot in the water. T337: a tree, a rock or a bush in a plot
		// is cleared first (see Blocked); a plot walled by what nobody can clear (a cliff) is left out of it. T343: a tree or a rock of the
		// scenery (Tribe::Scenery) is cleared too: the zone turns it into a node of the data ("tree", "rock") where it stood, worked as the others.
		Reply Paint(Tribe& tribe, const glm::vec2& a, const glm::vec2& b, const std::string& crop);
		// A zone of any shape: the perimeter's points (3 or more, in order); its plots are the cells of the grid (2 m, from the lowest x and z of
		// the points) whose middle falls inside it (or on its edge). The same refusals as Paint.
		Reply PaintOutline(Tribe& tribe, const std::vector<glm::vec2>& outline, const std::string& crop);
		// T281: PaintOutline's refusals without painting (the UI's preview while the perimeter is drawn): Ok with the plots it would have in
		// `plots`, else why (the same text PaintOutline answers). Changes nothing.
		// T337: a tree, a rock or a bush standing in a plot no longer refuses the zone (clearing it is the zone's first work): `clear`, the
		// middles of those plots; `walled`, the middles of the plots left out (nobody can stand there and nothing there can be cleared).
		static Reply Check(const Tribe& tribe, const std::vector<glm::vec2>& outline, const std::string& crop, int* plots = nullptr, std::vector<glm::vec2>* clear = nullptr,
			std::vector<glm::vec2>* walled = nullptr);
		Reply SetCrop(Tribe& tribe, const std::string& zone, const std::string& crop);
		Reply SetPlan(Tribe& tribe, const std::string& zone, const std::string& plan);
		Reply SetAuto(Tribe& tribe, const std::string& zone, bool on);
		// The zone and its plots go (what grows in it is lost; its store stays until it is emptied).
		Reply Remove(Tribe& tribe, const std::string& zone);
		// Every field of the clan in a line each: the zones with their crop and plan, the others "(the clan's)".
		std::string List(const Tribe& tribe) const;
		// The field a zone id names: "3" or "zone 3" (or a field's name, with _ for its spaces); nullptr: none.
		static const Field* Find(const Tribe& tribe, const std::string& zone, size_t* index = nullptr);
		const Zone* Of(const std::string& field) const;
		const std::map<std::string, Zone>& All() const { return m_Zones; }

		// ---- the rule of the AI
		// The fields the clan wants now (Tribe::FieldsWanted): FieldsFor its living members, or with plotsPerMember one more than it has while its
		// plots fall short (see above).
		int FieldsWanted(const Tribe& tribe) const;
		// The plots of the next field the clan marks out itself.
		static int NextPlots(const Tribe& tribe);
		// The crop the clan chooses for a field in "auto": of the crops it can sow now (its season, seed in the store, its technology known), the one
		// whose produce (the item it yields most of, not its seed) the store has least of; then the one that yields more; then by id. "" = none.
		// T344 (P-F15): with farming.foodDays > 0 the food comes first: of the food crops, the one whose produce the clan has least nutrition of
		// (in the store and in the bags, and what its sown plots will give); a crop that is no food (flax) only while the clan has foodDays days
		// of food or more and lacks its produce (a stock goal, the production stock, or a shortage of the plan), counting what grows of it.
		static std::string AutoCrop(const Tribe& tribe);
		// T344 (P-F13): whether the clan may sow a crop: it knows the crop's own technology (CropDef::Tech; none = yes). The field's technology
		// (FarmRules::Tech) is checked per member as before (Tribe::CanFarm). A crop the data has not: false.
		static bool Knows(const Tribe& tribe, const std::string& crop);

		// ---- the tribe's hooks
		// PostEconomyJobs: marks out a new field of the clan's own (not for the player's clan in the colony) and posts the farm jobs.
		void Post(Tribe& tribe) const;
		// The crop sown in a field now ("" = none can be: no seed, not its season, its plan sows nothing).
		std::string CropFor(const Tribe& tribe, const Field& field) const;
		// Whether a field's plan has work of an action ("till", "sow", "tend", "harvest").
		bool Works(const Field& field, const std::string& action) const;
		// ExecuteEconomy: up to `most` plots of the first field with work of `action`, and the crop to sow. False: none.
		bool Pick(const Tribe& tribe, const std::string& action, size_t most, size_t& fieldIndex, std::vector<size_t>& plots, std::string& crop) const;
		// Where a farm job is done, for the score and the option: the field Pick would choose (the first field while there is no zone, as before).
		glm::vec2 WorkPlace(const Tribe& tribe, const std::string& action) const;

		// ---- T337: clearing a zone
		// The plots of a zone that wait for what stands in them to be cleared (nullptr: none), and what is left to clear for people ("to clear
		// first: 3 tree, 1 rock (6 plots wait); no axe for tree"; "" = nothing). Both from the last Post (or the painting).
		const std::set<size_t>* Blocked(const std::string& field) const;
		std::string Clearing(const std::string& field) const;

		bool Empty() const { return m_Zones.empty(); }
		void Clear() { m_Zones.clear(); m_Blocked.clear(); m_ClearText.clear(); m_Clean.clear(); }
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		static void Logged(Tribe& tribe, const std::string& text);
		Zone* Edit(const Tribe& tribe, const std::string& zone, std::string& name);
		// T337: for each plot (a cell of the grid from `lo`), the nodes that stand in it: those whose foot is in its square and, where a member
		// cannot stand (the body says), the nearest within farming.clearReach. Refused: a plot nobody can stand on with no node there (scenery).
		// T343: a tree or a rock of the scenery (Tribe::Scenery) counts as a node: under the name of the node it becomes (SceneryNode), kept with
		// the prop in `scenery`; a plot is walled only by what is neither (a cliff) or by scenery the data has no node for.
		static Reply Blockers(const Tribe& tribe, const glm::vec2& lo, int columns, float spacing, const std::vector<int>& cells, std::vector<std::vector<std::string>>& perCell,
			std::vector<size_t>* stuck = nullptr, std::map<std::string, SceneryProp>* scenery = nullptr);   // (`stuck`: such plots are listed there instead of refused)
		// The zones' nodes in the way: marked to clear (ColonyDesignations::Clear, worked as any mark: the wood, stone or berries go to the
		// store), and a spent one taken away for good (it would grow again in the field); the plots that still wait and the text.
		void RefreshClearing(Tribe& tribe) const;
		// Blockers for a zone being painted: the walled plots taken out of `cells` (and listed in `walled`); refused when none is left.
		static Reply Plots(const Tribe& tribe, std::vector<int>& cells, const glm::vec2& lo, int columns, float spacing, std::vector<std::vector<std::string>>& perCell,
			std::vector<glm::vec2>* walled);
		static void ClearAway(Tribe& tribe, const std::string& node, const std::string& zone);
		// T343: the node type a tree or a rock of the scenery becomes ("tree", "rock": nullptr when the data has none) and its name ("scenery tree 12").
		static const ResourceNodeDef* SceneryDef(const Tribe& tribe, PropKind kind);
		static std::string SceneryNode(const ResourceNodeDef& def, int index);
		// T343: the prop goes from the scenery (and its wall from the navigation) and a node of its type stands where it stood, with its ability
		// system: nullptr when it cannot (no node type, the name taken, the host has no such prop).
		static AbilitySystemComponent* Convert(Tribe& tribe, const SceneryProp& prop, const std::string& node, const std::string& zone);

		std::map<std::string, Zone> m_Zones;   // field name -> the player's settings
		bool m_Player = false;                  // the host's switch (not saved)
		// T337, not saved: worked out again from the world at the next Post (a cleared node is gone from the scene, which the save keeps)
		mutable std::map<std::string, std::set<size_t>> m_Blocked;   // zone -> its plots that wait to be cleared
		mutable std::map<std::string, std::string> m_ClearText;      // zone -> what is left to clear
		mutable std::set<std::string> m_Clean;                       // zones with nothing left in them (nodes do not grow where there were none)
	};
}
