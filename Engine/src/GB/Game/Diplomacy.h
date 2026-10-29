#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	// Diplomacy between clans (H13): the rules (data: the "diplomacy" section of a content file) and the plain state the clan world keeps about every
	// pair of clans (attitude, trust, grievances, pacts) and about the meetings their envoys hold (trade, visits, parleys). The logic that runs it is
	// in GB/Game/Clans.cpp; the claims on the land in GB/Game/Territory.h. Deterministic and CPU only, like the rest of the society.
	//
	// The model (see docs/CLANES.md):
	//  * ATTITUDE of a pair, -1..1 = a base moved by what happens between the clans (events: meeting, trade, gifts, teaching, marriages; overlapping
	//    claims, theft of claimed nodes, raids) + MemberWeight x the mean bond between their members who have met + minus GrievanceWeight x the
	//    grievance either holds. The base fades towards 0 (DecayPerDay); a grievance fades slower. TRUST (0..1) rises with good events and falls
	//    faster with bad ones. The culture of each clan (its norms and the mean personality of its adults) gives its OPENNESS: good events between
	//    open clans count for more. The same culture gives its AGGRESSION, which the conflict work (H14) reads through ClanWorld::Hostility.
	//  * STANCE, derived: "allied" (an alliance pact), "truce" (a no-raid pact), "hostile" (attitude at or under Hostile, or a grievance at or over
	//    GrievanceHostile), "friendly" (attitude at or over Friendly), "wary" (below 0), "neutral"; "unknown" before they ever met.
	//  * PACTS have a kind ("alliance": they share their territory and do not raid; "truce": they do not raid), a start and an end; an alliance breaks
	//    when the attitude falls under BreakBelow. They are agreed at meetings when the attitude and trust allow.

	// How a clan claims the land around its camp.
	struct TerritoryRules
	{
		float Every = 30.0f;            // seconds between two updates of the claims
		float Reach = 160.0f;           // a clan claims the regions whose anchor is this close to its camp (and the region its camp is in)
		float Scale = 60.0f;            // metres at which the distance halves what a clan can hold of a region
		float PerMember = 1.0f;         // strength each living member gives the clan ...
		float PerBuilding = 2.0f;       // ... and each building it has (Clan::Buildings, set by the settlement work, H8)
		float Grow = 1.0f;              // a simulated day, a claim moves this share of the way to what the clan can hold (time makes a claim)
		float Min = 1.0f;               // a claim under this does not count: no owner, no overlap
		bool operator==(const TerritoryRules&) const = default;
	};

	// When two members of different clans have met.
	struct EncounterRules
	{
		float Every = 2.0f;             // seconds between two looks for members of other clans close by
		float Radius = 10.0f;           // members this close greet each other ...
		float Cooldown = 300.0f;        // ... and the same two not again before this many seconds
		bool operator==(const EncounterRules&) const = default;
	};

	// What events do to a pair of clans, how it fades, and what the culture of a clan makes of it.
	struct ClanRelationRules
	{
		std::map<std::string, float> Events;      // event -> what it adds to the base attitude ("meet" 0.03, "trade" 0.12, "theft" -0.08, "raid" -0.5...)
		std::map<std::string, float> Grievances;  // event -> the grievance the clan that suffered it keeps ("theft" 0.25, "raid" 1)
		float TrustGain = 0.5f;         // trust moves by this x a good event ...
		float TrustLoss = 1.0f;         // ... and this x a bad one
		float DecayPerDay = 0.05f;      // the base attitude loses this share of itself a day (towards 0)
		float GrievanceDecay = 0.1f;    // a grievance loses this much a day
		float MemberWeight = 0.5f;      // the mean bond between members who met counts this much in the attitude
		float MemberBond = 0.05f;       // a greeting adds this to the bond of the two members (x the openness)
		float GrievanceWeight = 0.3f;   // the attitude loses this x the larger grievance
		float Friendly = 0.25f;         // attitude at which the clans are friendly ...
		float Hostile = -0.3f;          // ... and hostile
		float GrievanceHostile = 1.0f;  // a grievance this large makes them hostile whatever the attitude
		// The culture of a clan (GB/Game/Society.h): Openness = 1 + sum of weight x (2 v - 1) over its norms and the mean traits of its adults,
		// clamped to 0.2..2; Aggression the same with its own weights, clamped to 0..2 (1 = average). Keys are norm names or trait names.
		std::map<std::string, float> Openness;    // "sharing" 0.5, "sociability" 0.4, "boldness" -0.2
		std::map<std::string, float> Aggression;  // "boldness" 0.6, "daring" 0.4, "sharing" -0.3
		bool operator==(const ClanRelationRules&) const = default;
	};

	// Barter between clans: what is a surplus, what is a need, and how a deal is balanced.
	struct TradeRules
	{
		float Every = 60.0f;            // seconds between two looks for trade between every pair of clans
		float MinAttitude = -0.1f;      // clans less fond of each other than this do not trade
		float Cooldown = 600.0f;        // seconds after a meeting of a pair before the next one
		float Surplus = 1.25f;          // a clan has a surplus of what its store holds above this x its stock target (the "tribe" section)
		int MaxUnits = 6;               // at most this many of one item in one deal
		float Balance = 1.5f;           // the larger side of a deal is worth at most this x the smaller (by value)
		float Gift = 0.45f;             // a clan this fond of another gives what it lacks even when it gets nothing back
		float Wait = 240.0f;            // an envoy waits this long at the meeting point for the other
		float Score = 1.6f;             // what going on with a meeting scores for the envoy (the needs still win)
		float Meet = 6.0f;              // the envoys are at the meeting when this close to the point
		std::map<std::string, float> Values;   // item -> what it is worth in a barter: the goods clans trade (none listed: everything, worth 1)
		bool operator==(const TradeRules&) const = default;
	};

	// Knowledge between clans: friendly clans visit and teach each other at meetings.
	struct ExchangeRules
	{
		float MinAttitude = 0.15f;      // clans this fond of each other show what they know at their meetings
		float Teach = 0.5f;             // understanding an envoy gains of a technology the other shows it (H11: at 1 it knows it)
		float VisitEvery = 900.0f;      // friendly clans with nothing to trade but something to teach meet this often
		bool operator==(const ExchangeRules&) const = default;
	};

	// Marriage between members of different clans (one moves to the other's clan).
	struct MarriageRules
	{
		float Every = 600.0f;           // seconds between two looks for couples across clans
		float MinAttitude = 0.1f;       // only between clans this fond of each other ...
		float MinBond = 0.04f;          // ... and between two members who met and like each other this much
		float Chance = 0.5f;            // chance of a wedding at a look when a couple can be made
		bool operator==(const MarriageRules&) const = default;
	};

	// Pacts.
	struct PactRules
	{
		float AllianceAttitude = 0.45f; // an alliance is agreed at a meeting when the attitude ...
		float AllianceTrust = 0.4f;     // ... and the trust are this high
		float TruceGrievance = 0.6f;    // hostile clans whose grievances are under this ...
		float TruceOpenness = 0.9f;     // ... and whose openness is at least this send envoys to parley and agree a truce
		float Days = 8.0f;              // a pact lasts this many simulated days (renewed at a meeting if it still holds)
		float BreakBelow = -0.05f;      // an alliance breaks when the attitude falls under this
		bool operator==(const PactRules&) const = default;
	};

	struct DiplomacyRules
	{
		TerritoryRules Territory;
		EncounterRules Encounters;
		ClanRelationRules Relations;
		TradeRules Trade;
		ExchangeRules Exchange;
		MarriageRules Marriage;
		PactRules Pacts;
		uint32_t Seed = 1;
		bool operator==(const DiplomacyRules&) const = default;
	};

	DiplomacyRules DefaultDiplomacyRules();   // assets/data/era1.json says the same in its "diplomacy" section
	// { "diplomacy": { "seed": 1, "territory": { "every", "reach", ... }, "encounters": { ... }, "relations": { "events": {}, "grievances": {}, "openness": {},
	//   "aggression": {}, ... }, "trade": { ..., "values": {} }, "exchange": { ... }, "marriage": { ... }, "pacts": { ... } } }. Each part replaces that
	// part whole (what it does not say is the default); a file without the section changes nothing; on a mistake it returns why and changes nothing.
	std::string LoadDiplomacyRules(const std::string& json, DiplomacyRules& rules);
	std::string LoadDiplomacyRulesFile(const std::string& path, DiplomacyRules& rules);
	std::string DiplomacyRulesToJson(const DiplomacyRules& rules);   // one JSON object, as the section reads it back

	using ClanId = int;

	struct Pact
	{
		std::string Kind;               // "alliance" or "truce"
		double Since = 0.0, Until = 0.0;
		bool operator==(const Pact&) const = default;
	};

	// What two clans are to each other (one per pair, kept with the lower id first).
	struct ClanRelation
	{
		float Base = 0.0f;              // moved by events, fades towards 0
		float Trust = 0.0f;             // 0..1
		float Grievance[2] = { 0.0f, 0.0f };   // [0]: what the lower id holds against the higher; [1] the other way
		int Encounters = 0, Trades = 0, Gifts = 0, Visits = 0, Lessons = 0, Thefts = 0, Marriages = 0, Raids = 0;
		double FirstMet = -1.0, LastMet = -1.0;
		double LastMeeting = -1e9;      // when a meeting of the pair was last posted
		std::vector<Pact> Pacts;
		std::vector<std::string> Recent;   // the last few things that moved it, for people ("12.0 trade: 3 berry for 2 flint")
		bool operator==(const ClanRelation& o) const
		{
			return Base == o.Base && Trust == o.Trust && Grievance[0] == o.Grievance[0] && Grievance[1] == o.Grievance[1] && Encounters == o.Encounters
				&& Trades == o.Trades && Gifts == o.Gifts && Visits == o.Visits && Lessons == o.Lessons && Thefts == o.Thefts && Marriages == o.Marriages
				&& Raids == o.Raids && FirstMet == o.FirstMet && LastMet == o.LastMet && LastMeeting == o.LastMeeting && Pacts == o.Pacts && Recent == o.Recent;
		}
	};

	// A meeting of two envoys: each fetches the goods its clan gives from the store, walks to the point, waits for the other, they exchange the goods
	// (and what they know, and maybe a pact), and each goes home and puts what it got in its store.
	struct Meeting
	{
		int Id = 0;
		ClanId A = -1, B = -1;
		std::string Purpose;            // "trade", "gift", "visit" or "parley"
		glm::vec2 Point{ 0.0f };
		std::string Envoy[2];           // the member of A and of B
		std::map<std::string, int> Give[2];   // what A gives B, what B gives A
		// Each envoy's leg: 0 fetch the goods at its camp, 1 walk to the point, 2 wait there, 3 go home, 4 home (done).
		int Leg[2] = { 0, 0 };
		int Tries[2] = { 0, 0 };           // times each envoy was sent on the leg it is on (not saved): a way that never opens ends the errand, it does not loop
		double Posted = 0.0, Arrived[2] = { -1.0, -1.0 }, Met = -1.0;
		std::string Outcome;            // for people, once they met (or gave up)
		bool operator==(const Meeting& o) const
		{
			return Id == o.Id && A == o.A && B == o.B && Purpose == o.Purpose && Point == o.Point && Envoy[0] == o.Envoy[0] && Envoy[1] == o.Envoy[1]
				&& Give[0] == o.Give[0] && Give[1] == o.Give[1] && Leg[0] == o.Leg[0] && Leg[1] == o.Leg[1] && Posted == o.Posted
				&& Arrived[0] == o.Arrived[0] && Arrived[1] == o.Arrived[1] && Met == o.Met && Outcome == o.Outcome;
		}
	};

	// What became of a meeting.
	struct MeetingRecord
	{
		int Id = 0;
		ClanId A = -1, B = -1;
		std::string Purpose, Outcome;
		bool Met = false;
		double At = 0.0;
		bool operator==(const MeetingRecord&) const = default;
	};

	// A barter: what a gives b and what b gives a (item -> units), from the stores and stock targets of both. Empty sides when nothing fits.
	struct Barter
	{
		std::map<std::string, int> Give[2];
		float Value[2] = { 0.0f, 0.0f };
		bool Empty() const { return Give[0].empty() && Give[1].empty(); }
	};
	// One clan's side: per item, how many it holds and how many it wants (its stock target).
	struct StoreView
	{
		std::map<std::string, int> Have;
		std::map<std::string, float> Want;
	};
	// A deal: each side's needs (want - have) met from the other's surplus (have - Surplus x want), at most MaxUnits an item, the larger side cut
	// down until it is worth at most Balance x the smaller. When one side has nothing the other needs it is no barter (both sides empty) unless
	// `gift`: then the side that can give gives what the other lacks. Deterministic: needs by size, ties by id.
	Barter PlanBarter(const TradeRules& rules, const StoreView& a, const StoreView& b, bool gift);
	float ItemValue(const TradeRules& rules, const std::string& item);
}
