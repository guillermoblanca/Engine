#pragma once
#include "GB/World/Regions.h"
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GB
{
	// Exploration, expeditions and migration of a clan (H12). The world is cut into regions (GB/World/Regions.h); a clan learns them as its members
	// walk them and tell the camp what they saw (ClanAtlas); when it needs something no member knows where to find for long enough it sends a small
	// party to the most promising region it does not know (Expedition); and when its home has run dry for long it moves the camp (migration).
	// The rules are data: the "exploration" section of a content file (LoadExplorationRules; registered in the packs as "exploration").
	// The tribe's part is in GB/Game/TribeExplore.cpp; this file is the plain data and its JSON. Deterministic, CPU only.

	// When and how a clan sends an expedition.
	struct ExpeditionRules
	{
		float Every = 10.0f;            // seconds between two looks at what the clan lacks
		float Scarcity = 180.0f;        // a gather job that nobody can take because nobody knows where its item grows, for this long, sends a party
		int MinParty = 2;               // the party sets off with this many (or with whoever it has after Gather seconds) ...
		int MaxParty = 3;               // ... and takes at most this many
		float Gather = 120.0f;          // seconds the party waits for its members before it goes with fewer (at least one)
		int ScoutPoints = 3;            // places in the region it walks to and looks around
		float TravelScale = 150.0f;     // metres at which the distance halves the worth of a region
		float MaxDistance = 400.0f;     // farther than this a region is not worth the trip
		float RiskWeight = 1.0f;        // how much the danger of a region counts against it (x its danger, 0..1)
		float Retry = 900.0f;           // seconds before an item is looked for again in a region that did not have it
		float MaxSeconds = 1500.0f;     // an expedition out for this long comes home, found or not
		float Priority = 1.3f;          // its priority on the job board
		float Score = 1.4f;             // what going on with the expedition scores for a member of the party (the needs still win)
		int Food = 2;                   // food each member takes from the store as it sets off
		float SeeEvery = 2.0f;          // seconds between two marks of what each member has in view (the ground it walks becomes known)
		bool operator==(const ExpeditionRules&) const = default;
	};

	// When a clan moves its camp.
	struct MigrationRules
	{
		float Every = 30.0f;            // seconds between two looks at the home
		float HomeRadius = 60.0f;       // the home is the ground this close to the camp
		float Settle = 600.0f;          // a camp younger than this is not judged (the clan is still learning what is near)
		float Poor = 0.25f;             // the home is poor when what it is known to offer falls below this share of the best it offered
		float WaterLost = 60.0f;        // ... or no well is known at home and the water is farther than this
		float After = 3600.0f;          // seconds of a poor home in a row before the clan moves (a good look takes back Recover x the time)
		float Recover = 2.0f;
		float MinGain = 0.25f;          // the new place must be worth this much more than home (SiteScore)
		float Cooldown = 7200.0f;       // seconds after a move before the next one
		float Arrive = 8.0f;            // a member this close to the new camp has arrived
		float Score = 1.8f;             // what walking to the new camp scores
		bool operator==(const MigrationRules&) const = default;
	};

	// The "exploration" section of a content file.
	struct ExplorationRules
	{
		RegionRules Regions;
		ExpeditionRules Expeditions;
		MigrationRules Migration;
		bool operator==(const ExplorationRules&) const = default;
	};

	ExplorationRules DefaultExplorationRules();   // assets/data/era1.json says the same in its "exploration" section
	// { "exploration": { "regions": { "tile", "blockTiles", "minTiles", "waterShare", "scoutCell", "nearWater", "predatorDanger", "danger": { "<biome>": d },
	//   "names": { "<biome>": "<word>" } }, "expeditions": { "every", "scarcity", "minParty", "maxParty", ... }, "migration": { "every", "homeRadius", ... } } }.
	// Each of the three parts replaces that part whole (what it does not say is the default); a file without the section changes nothing; on a mistake
	// it returns why and changes nothing.
	std::string LoadExplorationRules(const std::string& json, ExplorationRules& rules);
	std::string LoadExplorationRulesFile(const std::string& path, ExplorationRules& rules);
	std::string ExplorationRulesToJson(const ExplorationRules& rules);   // one JSON object, as the section reads it back

	// What a clan knows of a region (built from what its members tell at the camp).
	struct RegionLore
	{
		int SeenCells = 0;                       // scout cells of it the clan has had in view
		float Explored = 0.0f;                   // SeenCells / the region's cells
		std::map<std::string, int> Resources;    // node type -> nodes of it the clan last knew there (it keeps them when the memory of each fades)
		double LastVisit = -1.0;                 // when a member last had some of it in view (-1 = never)
		int Expeditions = 0;                     // parties sent there
		bool operator==(const RegionLore&) const = default;
	};

	// The map of a clan: which scout cells it has seen and what it knows of each region. Members mark what they have in view in their own pending
	// list, which joins the clan's when they are at the camp (as their memories do).
	struct ClanAtlas
	{
		std::vector<uint8_t> Seen;                               // per scout cell of the region map (empty until regions are known)
		std::map<int, RegionLore> Lore;                          // region id -> lore (only regions it knows something of)
		std::map<std::string, std::map<int, double>> Pending;    // member -> cell -> when it last had it in view (not told yet)
		bool operator==(const ClanAtlas&) const = default;
	};

	// A party out to find an item.
	struct Expedition
	{
		int Id = 0;
		std::string Item;                        // what it looks for ("clay")
		int Region = -1;                         // where it goes
		std::string RegionName;
		std::string Stage;                       // "forming" (on the board, waiting for its party), "out" (gone), "" = none
		std::vector<std::string> Party;
		std::map<std::string, int> Leg;          // member -> the next point it goes to (Points.size() = home, Points.size() + 1 = back at the camp)
		std::vector<glm::vec2> Points;           // where it scouts
		double Posted = 0.0, Started = 0.0;
		std::string Why;                         // for people: why it was sent and why there
		bool Active() const { return !Stage.empty(); }
		bool operator==(const Expedition&) const = default;
	};

	// What became of an expedition (the log keeps the sentence; this is for the AI and the tests).
	struct ExpeditionRecord
	{
		int Id = 0;
		std::string Item, RegionName;
		int Region = -1;
		bool Found = false;                      // the clan came to know a node of the item while it was out
		double Started = 0.0, Ended = 0.0;
		std::vector<std::string> Party;
		bool operator==(const ExpeditionRecord&) const = default;
	};

	struct MigrationRecord
	{
		glm::vec2 From{ 0.0f }, To{ 0.0f };
		std::string Region, Why;
		double At = 0.0;
		bool operator==(const MigrationRecord&) const = default;
	};

	// Everything of H12 the tribe saves (in its "ai" section, under "explore"; all optional: an older save loads with none).
	struct ExplorationState
	{
		ClanAtlas Atlas;
		Expedition Current;
		std::vector<ExpeditionRecord> History;   // the last ones (at most 32)
		int NextId = 1;
		std::map<std::string, float> Unmet;      // item -> seconds its gather job has had nobody who knows where it grows
		std::map<std::string, double> Tried;     // "<item>@<region>" -> when an expedition found none of it there
		float Timer = 0.0f, SeeTimer = 0.0f, HomeTimer = 0.0f;
		// the home and the moves
		double CampSince = 0.0;                  // when the clan set up its camp where it is
		float Peak = 0.0f;                       // the most the home was known to offer (SiteScore)
		float Poor = 0.0f;                       // seconds of a poor home (falls back when it is not)
		std::string PoorWhy;
		std::vector<MigrationRecord> Moves;
		std::set<std::string> Arrived;           // during a move: the members already at the new camp
		bool Moving = false;
		bool operator==(const ExplorationState&) const = default;

		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on malformed text
	};

	// Where a clan could set its camp, and what the place is worth: the input and output of a camp-site chooser. The default chooser (in
	// TribeExplore.cpp) scores the anchors of the regions the clan knows with what it knows grows near them, the water and the danger; the
	// settlement work (H8) can put its own in its place with Tribe::SetSiteChooser.
	struct CampSiteRequest
	{
		glm::vec2 From{ 0.0f };                  // the camp now
		float HomeRadius = 60.0f;
		float HomeScore = 0.0f;                  // what the camp now is worth
		std::vector<std::string> Wanted;         // the items the clan wants (its stock targets)
	};
	struct CampSite
	{
		glm::vec2 At{ 0.0f };
		float Score = 0.0f;
		int Region = -1;
		std::string Why;
	};
	using CampSiteChooser = std::function<bool(const CampSiteRequest&, CampSite&)>;
}
