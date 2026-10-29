#pragma once
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// H15: the player inside the society. The character a person controls (PlayerMode / `actor control` in the editor) is a member of a clan like
	// any other (GB/Game/Tribe.h, TribeMember::Player): it has needs, aptitudes, relations, knowledge, a bag and a trade, it eats from the store,
	// sleeps in a bed of the shelters, holds jobs of the board and is counted by the clan (its stock targets, its beds, its births and deaths). The
	// brain of the tribe never drives its body: what it does comes from the person (keys, or orders in a test) through the same abilities the NPCs
	// use, and the clan only hears of it (Tribe::PlayerWorked and the other Player* calls of TribePlayer.cpp).
	//
	// What the player adds to the society:
	//  * STANDING (0..Max): what the clan thinks of the player's contributions. Deposits, the units of a claimed job, gifts, lessons, crafts, a fed
	//    campfire, work on a site and trades with other clans raise it (Credit: points per unit, data); taking from the store lowers it; it fades a
	//    little every day. At Leader or more the player LEADS: the clan follows its proposals more readily.
	//  * PROPOSALS: the player proposes work to the clan ("gather <item> <units>", "make <item> <units>", "build <building>"). Every living adult
	//    member votes: Base + Affinity x its bond with the player + Standing x standing/Max + Leader (if it leads) + for each norm of the culture its
	//    weight x (2 norm - 1) - the cost of the work (per unit, per building); it agrees when that is above 0. With a Majority of agreeing members the
	//    clan takes it on: a gather or a make becomes a want of the clan (Tribe::SetDemand: the board then posts the gathers and makes), a building a
	//    construction site (Tribe::StartSite). Deterministic: no die is cast, the same clan answers the same.
	//  * GIFTS and TALK: an item given to a member moves their bond by GiftBond x the item's worth (food and tools count double); talking to a member
	//    now and then (TalkSeconds) moves it by TalkBond. Teaching a technology the player knows (or being taught one) is a lesson (Society "teach").
	//  * MARRIAGE: a member who likes the player at least MarryBond (and is free and of age) may become its partner (Society::Marry); children then
	//    come by the rules of the society (H9).
	//  * Other clans: the player can be its clan's envoy (ClanWorld::PlayerVisit): at another camp it trades what its clan lacks for what that one
	//    spares, or visits, the same barter and the same relations as two envoys at a meeting (H13).
	// All the numbers are data: the "player" section of a content file (PlayerRules, LoadPlayerRules; assets/data/player.json).

	struct ProposalRules
	{
		float Base = -0.1f;                    // a member with no bond, a player of no standing and an average culture says no
		float Affinity = 1.0f;                 // x its bond with the player (-1..1)
		float Standing = 0.6f;                 // x standing / Max
		float Leader = 0.4f;                   // added when the player leads
		std::map<std::string, float> Culture;  // norm -> weight on (2 norm - 1): a sharing clan follows more readily ("sharing": 0.3)
		float Majority = 0.5f;                 // share of the voters that must agree (more than this)
		float UnitCost = 0.005f;               // the cost of each unit asked for (a big want is harder to accept)
		float BuildCost = 0.15f;               // the cost of a building
		float Cooldown = 60.0f;                // seconds between two proposals of the same player
		int MaxUnits = 40;                     // a want is at most this many units
		bool operator==(const ProposalRules&) const = default;
	};

	struct PlayerRules
	{
		std::string Role = "forager";          // the trade a player starts with (the review of the trades never moves it)
		bool Mortal = false;                   // false: its health never falls below FloorHealth (hunger and cold weaken it, they do not kill it)
		float FloorHealth = 10.0f;
		float Reach = 4.0f;                    // how near a member, a node or an interactive must be to act on it
		float CampReach = 7.0f;                // how near the camp the player must be to use the store
		float StandingStart = 10.0f;
		float StandingMax = 100.0f;
		float Leader = 60.0f;                  // standing at which the player leads
		float DecayPerDay = 2.0f;              // standing lost every simulated day
		// Points of standing per unit of each contribution: "deposit", "job" (a unit of a claimed job, on top of the deposit), "gift", "teach", "craft",
		// "stoke", "build", "trade", "proposal" (accepted) and "take" (negative: taken from the store, food excepted).
		std::map<std::string, float> Credit;
		float GiftBond = 0.02f;                // bond per unit of worth given (food and tools are worth 2, the rest 1)
		float TalkBond = 0.01f;                // bond per talk ...
		float TalkSeconds = 60.0f;             // ... at most once this often with the same member
		float TeachAmount = 0.5f;              // understanding of a technology a lesson from (or to) the player gives
		float MarryBond = 0.3f;                // the bond a member must have with the player to marry it
		float VisitRadius = 15.0f;             // how near another clan's camp the player must be to trade or visit
		ProposalRules Proposals;
		bool operator==(const PlayerRules&) const = default;
	};

	// The keys and the interface of the game mode (the "controls" section of a content file, ControlRules, LoadControls; assets/data/player.json).
	// Key names are those of Dear ImGui ("E", "Tab", "F5", "Space", "LeftBracket"...); an action with no key is not bound.
	struct HudRules
	{
		int Notifications = 5;                 // lines of the clan's log shown
		float NotifySeconds = 12.0f;           // how long a line stays
		std::vector<std::string> Needs = { "Hunger", "Thirst", "Rest", "Warmth", "Health" };   // the bars, in this order
		bool Clock = true;                     // day, hour and season
		bool Prompt = true;                    // what the interact key would do now
		bool operator==(const HudRules&) const = default;
	};

	// How `play` starts a game (all can be overridden on the command line: play [type] [seed] [members] [clans]).
	struct GameSetup
	{
		std::string World = "temperate";
		int Seed = 5;
		float Resources = 2.0f;                // density of the wild resource nodes
		int Members = 4;                       // NPCs of the player's clan (the player is one more)
		int Clans = 2;                         // clans in the world, the player's included
		std::string Model = "packs/full/X Bot.fbx";   // the characters (relative to the working directory or the assets), ...
		std::string Fallback = "fox";          // ... or this when the file is not there
		std::string Name = "Player";           // the player's member and actor
		std::string Quality = "auto";          // "auto": low, and minimum as soon as a frame takes longer than AutoMs; or a preset name
		float AutoMs = 40.0f;
		int TimeScale = 1;
		bool operator==(const GameSetup&) const = default;
	};

	struct ControlRules
	{
		std::map<std::string, std::string> Keys;   // action -> key: interact, eat, drink, deposit, sleep, clan, jobs, claim, pause, faster, slower, save, load, help
		HudRules Hud;
		GameSetup Game;
		bool operator==(const ControlRules&) const = default;
	};

	PlayerRules DefaultPlayerRules();      // assets/data/player.json says the same in its "player" section
	ControlRules DefaultControls();        // ... and in its "controls" section
	// { "player": { "role", "mortal", "floorHealth", "reach", "campReach", "standing": { "start", "max", "leader", "decayPerDay", "credit": { "<kind>": p } },
	//   "giftBond", "talkBond", "talkSeconds", "teachAmount", "marryBond", "visitRadius", "proposals": { "base", "affinity", "standing", "leader", "culture": {},
	//   "majority", "unitCost", "buildCost", "cooldown", "maxUnits" } } }. What the section does not say keeps its value; a file without it changes nothing;
	// on a mistake (an unknown key, a wrong type) it returns why and changes nothing.
	std::string LoadPlayerRules(const std::string& json, PlayerRules& rules);
	std::string PlayerRulesToJson(const PlayerRules& rules);   // the whole "player" section (one JSON document)
	// { "controls": { "keys": { "<action>": "<key>" }, "hud": { "notifications", "notifySeconds", "needs": [], "clock", "prompt" },
	//   "game": { "world", "seed", "resources", "members", "clans", "model", "fallback", "name", "quality", "autoMs", "timeScale" } } }: "keys" replaces
	// the bindings it names (an empty string unbinds), the rest as above.
	std::string LoadControls(const std::string& json, ControlRules& controls);
	std::string ControlsToJson(const ControlRules& controls);

	// A proposal of the player to its clan and what became of it.
	struct Proposal
	{
		int Id = 0;
		std::string By;                        // the player
		std::string Kind;                      // "gather", "make" or "build"
		std::string Target;                    // the item or the building
		int Units = 0;
		glm::vec2 At{ 0.0f };                  // where a building goes
		std::string Status;                    // "accepted", "rejected", "done" (its want was met, its building finished) or "dropped"
		int Yes = 0, Voters = 0;
		std::string Why;                       // for people: the vote and its reasons
		std::string Site;                      // the construction site of an accepted building
		int Start = 0;                         // the units in the stores when it was accepted (a want is met at Start + Units)
		double Posted = 0.0;
		bool operator==(const Proposal&) const = default;
	};

	// What a clan keeps of each of its players.
	struct PlayerRecord
	{
		float Standing = 0.0f;
		std::map<std::string, float> Done;     // contribution kind -> units ("deposit" 34, "gift" 3, "job" 12...)
		std::map<std::string, double> TalkedAt;   // member -> when the player last talked to it
		double LastProposal = -1e9;
		bool Leader = false;                   // what the log last said
		bool operator==(const PlayerRecord&) const = default;
	};

	struct PlayerSociety
	{
		std::map<std::string, PlayerRecord> Players;
		std::vector<Proposal> Proposals;       // the latest first is at the back (the last 40 are kept)
		int NextProposal = 1;
		double DayTimer = 0.0;
		void Clear() { *this = PlayerSociety(); }
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (nothing changed) on a mistake
		bool operator==(const PlayerSociety&) const = default;
	};

	// The vote of one member on a proposal (see ProposalRules): its score, > 0 agrees. `norms`: the clan's norms (name -> 0..1).
	float ProposalVote(const ProposalRules& rules, float bond, float standing, float standingMax, bool leader, const std::map<std::string, float>& norms, float cost);
}
