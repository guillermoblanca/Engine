#pragma once
#include "GB/Gameplay/Farming.h"
#include "GB/Gameplay/Items.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// The economy of a clan (H18: E6 spoilage and preservation, E7 agriculture, E11 logistics, and the production chains): the rules, as data
	// (the "economy" section of a content file, see LoadEconomyRules), and the state the tribe keeps (fields, stockpiles, counters), saved inside
	// the tribe's own section. What the tribe does with them is in GB/Game/TribeEconomy.cpp; the pure parts are GB/Game/Production.h (planning
	// chains), GB/Gameplay/Spoilage.h (ageing) and GB/Gameplay/Farming.h (fields).

	// Production chains (GB/Game/Production.h): what the clan wants made and how deep it plans.
	struct ProductionRules
	{
		int Depth = 3;                           // levels of inputs planned under a want
		int MaxBatches = 3;                      // most batches one "make" job asks for
		float Seconds = 4.0f;                    // seconds of work a batch takes at its station
		float Priority = 1.1f;                   // base priority of a "make" job on the board
		std::map<std::string, int> Stock{ { "cordage", 2 } };   // intermediates the store keeps: no node gives them, so only this makes the clan make them
		std::vector<std::string> CampStations{ "workbench" };   // stations the camp has without any building ("workbench"); the others come from the buildings (H8)
		bool Upgrade = true;                     // a better tool of a member's trade (more power) is wanted once the clan knows how to make it
		bool Experiment = true;                  // the inputs of a recipe the clan does not know are wanted too, to try it (E5)
		float ContainersPerMember = 1.0f;        // containers (Item.Container: baskets) wanted per member that carries none (E11); 0 = none
		float PreserveAbove = 1.0f;              // perishable food above this many times its stock target is preserved when a recipe and a station allow (E6)
		bool operator==(const ProductionRules&) const = default;
	};

	// Spoilage (GB/Gameplay/Spoilage.h): how often things age and how fast each place ages them.
	struct SpoilageRules
	{
		float Step = 10.0f;                      // seconds between two ageing passes (the stacks age by the whole step at once: cheap)
		float Bag = 1.0f;                        // rate in a member's bag
		std::map<std::string, float> Seasons{ { "winter", 0.5f } };   // season -> factor on every rate (the cold keeps food); unlisted = 1
		bool operator==(const SpoilageRules&) const = default;
	};

	// Agriculture (GB/Gameplay/Farming.h): when a clan marks out a field and how it works it.
	struct FarmRules
	{
		std::string Tech = "tillage";            // the technology a member must know to till and sow ("" = anyone)
		std::string ToolTag = "Item.Tool.Hoe";   // the tool that tills ("" = bare hands)
		int Plots = 6;
		int Columns = 3;
		float Spacing = 2.0f;
		glm::vec2 Offset{ 9.0f, 4.0f };          // where the field goes from the camp (H8's choice of a site can move it)
		float MoistureSeconds = 120.0f;          // a watered plot stays moist this long
		float WorkSeconds = 2.0f;                // seconds of work per plot
		int PlotsPerTrip = 3;                    // plots one member works in one trip
		float Priority = 1.2f;                   // base priority of a "farm" job
		int Fields = 1;                          // fields the clan marks out (one is enough for era 1): with MembersPerField, the most it marks out
		int MembersPerField = 0;                 // a field for every this many living members (0: always Fields), so a growing village feeds itself
		int FieldsFor(int alive) const { return MembersPerField > 0 ? std::clamp((alive + MembersPerField - 1) / MembersPerField, 1, std::max(1, Fields)) : Fields; }
		// T280 (A1 of documentation/01_diseno/AGRICULTURA.md §2.4, §7.1): the zones the clans of the AI mark out themselves. With PlotsPerMember > 0
		// a clan wants that many plots per living member, in zones of ZonePlotsMin..ZonePlotsMax plots (Columns a row), in place of Fields and
		// MembersPerField; 0 (the default, data without "plotsPerMember") keeps the old rule of FieldsFor fields of Plots plots. A zone bound of 0
		// is Plots.
		int PlotsPerMember = 0;
		int ZonePlotsMin = 0, ZonePlotsMax = 0;
		int ZoneMin() const { return ZonePlotsMin > 0 ? ZonePlotsMin : Plots; }
		int ZoneMax() const { return std::max(ZoneMin(), ZonePlotsMax > 0 ? ZonePlotsMax : Plots); }
		// T337: the priority (1 first .. 10 last, as a mark of the player: ColonyDesignations) of clearing what stands in a painted zone (trees,
		// rocks, bushes): the zone's first work, before its plots are tilled (FarmZones)
		int ClearPriority = 2;
		// ... and how far from a plot nobody can stand on a node may be and still be taken as what walls it (a big rock's wall reaches over;
		// a trunk's hardly): metres
		float ClearReach = 3.0f;
		// T344 (P-F15 of documentation/01_diseno/ALIMENTACION.md §16.3): with this > 0, the crop a field in "auto" gets (FarmZones::AutoCrop) is
		// weighed in food, not in units: of the food crops the one whose produce the clan has least nutrition of (stored and growing), and a crop
		// that is no food (flax) only while the clan has at least these days of food (Tribe::FoodDays) and lacks its produce. 0 = the old rule
		// (the produce the store has least units of).
		float FoodDays = 0.0f;
		// T328: a clan of the AI that may till and holds no seed of a crop it may sow goes for wild seed it knows within this of its camp, past
		// its home ground (WildSeeds.h): metres, 0 = off (the old rule: only the home ground's gathers and searches)
		float WildSeedReach = 0.0f;
		// T335: a clan of the AI with the verb of a berry bush's transplant and none within this of its camp moves one it knows there
		// (BushKeeper.h): metres, 0 = off
		float TransplantNear = 0.0f;
		bool operator==(const FarmRules&) const = default;
	};

	// A kind of store (E11): the main store at the camp and the stockpiles (by a work site, at the field).
	struct StoreKind
	{
		int Cap = 40;                            // units of one item after which the clan gathers no more of it into this store
		int Slots = 40;
		float SpoilRate = 1.0f;                  // how fast it ages what it holds (a cool, covered store less)
		std::vector<std::string> Accepts;        // item tags it takes ("Item.Food", "Item.Seed"); empty = anything
		bool Haul = false;                       // what it holds is hauled to the main store
		bool operator==(const StoreKind&) const = default;
	};

	struct LogisticsRules
	{
		std::map<std::string, StoreKind> Stores{ { "main", StoreKind() }, { "stockpile", StoreKind{ 20, 12, 1.0f, {}, true } } };   // "main" (the camp's store) and the kinds of stockpile
		float PerBagSlot = 1.0f;                 // units more a member carries before it goes to the store for each slot its container adds (a basket: 6)
		float Priority = 0.9f;                   // base priority of a "haul" job
		int HaulAt = 1;                          // a stockpile with this many units of things to haul posts a haul job
		std::string FieldStore = "stockpile";    // the kind of stockpile put beside a new field ("" = none: the harvest goes to the main store)
		// T255: what the fire keeps at hand once the clan has store buildings (item -> units; "food" = any food together): deposits fill it
		// first, the rest goes to the buildings (GB/Game/ClanStores.h)
		std::map<std::string, int> CampReserve;
		// T255: food that keeps less than this (its shelf life, s) goes to the store building that keeps best; food that keeps longer to the one
		// that keeps worst with room (it leaves the good keepers to what goes off soon)
		float KeepBestBelow = 4800.0f;
		// T256: the clan answers food that goes off and food that finds no room: when the food it lost lately (fading over SpoilMemory s) and the
		// food by the fire that no building takes reach FoodStoreAt units, it raises its best food store (the lowest keep it knows), up to one
		// for every FoodStoresPer living; and it rebuilds a food store into a better one it knows (BuildingDef::Supersedes). 0: none of this
		int FoodStoreAt = 0;
		int FoodStoresPer = 5;
		float SpoilMemory = 1200.0f;
		bool operator==(const LogisticsRules&) const = default;
	};

	// Construction (H8): how many building sites the clan keeps open at once. One for every MembersPerSite living members (at least one), at most
	// MaxSites: a clan of four raises one building at a time, a clan of eight two.
	struct ConstructionRules
	{
		int MaxSites = 3;
		int MembersPerSite = 4;                  // 0: always MaxSites
		// A phase that waits for materials is worth more with time (F4 in documentation/04_produccion/qa/QA_SIMULACION.md: shelters waited days for reeds or one stone): the
		// options that carry to it, work it and gather for it are multiplied by 1 + min(AgeMax, waiting / AgeSeconds)
		float AgeSeconds = 300.0f;
		float AgeMax = 2.0f;
		bool operator==(const ConstructionRules&) const = default;
		int SitesFor(int alive) const { return MembersPerSite > 0 ? std::clamp(alive / MembersPerSite, 1, std::max(1, MaxSites)) : std::max(1, MaxSites); }
	};

	// W1: the food laid by for a season in which nothing grows (winter: SeasonClock::GrowthRate 0; the wild nodes do not regrow, the fields freeze,
	// the animals do not breed). One rule for every source of food (gathering now; the hunt of CAZA.md §3.3 is to read the same WinterStockDays).
	struct WinterStockRules
	{
		float Days = 0.0f;                       // days of food per member the clan wants in hand, of each food that keeps until growth returns (0 = none)
		float From = 0.0f;                       // how far (0..1) through the season before the lean one it starts to lay it by
		bool operator==(const WinterStockRules&) const = default;
	};

	struct EconomyRules
	{
		ProductionRules Production;
		SpoilageRules Spoilage;
		WinterStockRules WinterStock;
		FarmRules Farming;
		std::map<std::string, CropDef> Crops;    // by id: the crops a field can grow (a later file adds to them)
		LogisticsRules Logistics;
		ConstructionRules Construction;
		bool operator==(const EconomyRules&) const = default;
		const StoreKind& Main() const;
	};

	EconomyRules DefaultEconomyRules();   // assets/data/era1.json says the same in its "economy" section
	// { "economy": { "production": { "depth", "maxBatches", "seconds", "priority", "stock": { "cordage": 2 }, "campStations": [ "workbench" ], "upgrade",
	//   "experiment", "containersPerMember", "preserveAbove" }, "spoilage": { "step", "bag", "seasons": { "winter": 0.5 } }, "farming": { "tech", "tool",
	//   "plots", "columns", "spacing", "offset": [ x, z ], "moistureSeconds", "workSeconds", "plotsPerTrip", "priority", "fields", "foodDays" },
	//   "crops": [ { "id", "seed", "yield": { "grain": 3 }, "stageSeconds", "sow": [ "spring" ], "seasons": { "spring": 1.2 }, "tech" } ],
	//   "logistics": { "stores": { "main": { "cap", "slots", "spoilRate", "accepts": [], "haul" } }, "perBagSlot", "priority", "haulAt", "fieldStore" },
	//   "construction": { "maxSites", "membersPerSite" } } }.
	// Each of "production", "spoilage", "farming" and "logistics" replaces that part whole (what it does not say is the default, and a map or a list it
	// gives, such as "stock", "seasons" or "stores", replaces the default one); "crops" adds or
	// replaces crops by id (an era pack adds its own). A file without the section changes nothing; on a mistake it returns why and changes nothing.
	// Items are not checked here (a crop's seed is an item of the items section): CheckEconomy does that against a library.
	std::string LoadEconomyRules(const std::string& json, EconomyRules& rules);
	std::string LoadEconomyRulesFile(const std::string& path, EconomyRules& rules);
	std::string EconomyRulesToJson(const EconomyRules& rules);   // one JSON object, as the section reads it back
	// Problems of the rules against the items they name (a crop's seed and yield, the stock of an intermediate): "" = none.
	std::string CheckEconomy(const EconomyRules& rules, const ItemLibrary& items);

	// W1: the days of `food` (per member) the clan wants to lay by at `now`: WinterStock.Days from WinterStock.From of the way through the season before a
	// lean one, 0 otherwise (in the lean season itself there is nothing to gather); and 0 for a food that, fresh, would go off before growth returns in a store
	// that ages things at `storeRate` (its spoilRate times the best keep of its buildings) and at the season's factor (spoilage.seasons). Pure: a
	// function of the clock, so there is nothing to save.
	float WinterStockDays(const EconomyRules& rules, const SeasonClock& clock, double now, const ItemDef& food, float storeRate);
	// The days of food per member laid by for the lean season at `now`, whatever the food (WinterStockDays without its test of what keeps): the
	// part of the store that is not to spare (a couple has a child on what is left over, not on the winter's food).
	float WinterReserveDays(const EconomyRules& rules, const SeasonClock& clock, double now);

	// A stockpile: a store away from the camp (by a work site, at the field) with its own kind (capacity, what it takes, how fast it ages things).
	struct Stockpile
	{
		std::string Name;                        // "field 1 store"
		std::string Kind = "stockpile";
		glm::vec2 Pos{ 0.0f };
		Inventory Store{ 12, 0.0f };
	};

	// What the clan's economy keeps between frames, saved with the tribe ("economy" in its section; a save from before has none: no fields, no
	// stockpiles, and every stack fresh).
	struct EconomyState
	{
		std::vector<Field> Fields;
		std::vector<Stockpile> Stockpiles;
		float SpoilTimer = 0.0f;                 // seconds since the last ageing pass
		std::map<std::string, int> Spoiled;      // item -> units that went off
		std::map<std::string, int> Made;         // item -> units made by "make" jobs
		std::map<std::string, int> Harvested;    // item -> units harvested
		int Hauled = 0;                          // units hauled to the main store
		int Ruined = 0;                          // plots the seasons ruined
		double DayStart = 0.0;                   // start of the day the food counter is for
		int FoodToday = 0, FoodYesterday = 0;    // food units brought to the stores this day and the day before (the clan's carrying capacity)
		std::map<std::string, Inventory> Pantries;   // N1b: household id -> what it keeps at home (Tribe::ToPantry)
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake
	};
}
