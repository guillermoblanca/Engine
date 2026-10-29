#pragma once
#include "GB/Gameplay/Combat.h"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	// The clan's side of conflict (H14): what a clan keeps between frames about threats, fights, hunts, the wounded and its raids, and the pure
	// rules a clan uses to decide (fight or flee, raid or not, who goes). The core of the fight is GB/Gameplay/Combat.h (shared by every clan and
	// the animals of a world); what the tribe does with both is GB/Game/TribeConflict.cpp. Deterministic: no randomness, every list in name order.

	// ------------------------------------------------------------------ fight or flight
	// What a member weighs when it sees a threat. Odds: the worth of its side near it over the worth of the threat's side near the threat
	// (CombatSystem::Strength). Culture: the society's factors for the options "fight" and "flee" (norms and traditions, H10).
	struct ThreatView
	{
		float Boldness = 0.5f;
		float Health = 100.0f;
		float Safety = 100.0f;
		float Odds = 1.0f;
		bool Kin = false;          // the threat attacks one of its kin or a child of the clan
		bool Camp = false;         // the threat is at the camp
		bool Child = false;
		float Defense = 0.0f;      // the camp's defense (its palisades): it counts when the threat is at the camp
		float FightCulture = 1.0f, FleeCulture = 1.0f;
	};

	struct FightChoice
	{
		float Fight = 0.0f, Flee = 0.0f;
		std::vector<std::pair<std::string, float>> Factors;   // what made each (for the log and the inspector)
		bool Fights() const { return Fight > Flee; }
		std::string Why() const;   // "fight 1.42 (bold 0.60, health 0.80, odds 0.50) > flee 0.40 (fear 0.12, timid 0.28)"
	};

	FightChoice FightOrFlight(const ThreatRules& rules, const MoraleRules& morale, const ThreatView& view);

	// ------------------------------------------------------------------ raids
	struct RaidMotive
	{
		float Score = 0.0f;
		float Boldness = 0.0f, Hunger = 0.0f, Grievance = 0.0f, Hostility = 0.0f, Defense = 0.0f;   // (Defense is taken off)
		std::string Why() const;
	};

	// Why a clan would raid a target: its adults' boldness, how short of food it is (0..1), its grievance against the target and its hostility, less
	// what the target's defense (its palisades, RaidTarget::Defense) puts it off.
	RaidMotive WeighRaid(const RaidRules& rules, float meanBoldness, float hunger, float grievance, float hostility, float defense = 0.0f);

	// A candidate raider: who, how fit (health 0..100), how bold and strong, whether it is a child or busy elsewhere.
	struct RaiderCandidate
	{
		std::string Name;
		float Health = 100.0f, Boldness = 0.5f, Strength = 0.5f;
		bool Child = false, Busy = false;
	};

	// The party: adults with at least PartyHealth that are not busy, the boldest and strongest first (ties by name), at most MaxParty; empty when
	// fewer than Party can go.
	std::vector<std::string> ChooseRaidParty(const RaidRules& rules, std::vector<RaiderCandidate> candidates);

	// The raid a clan is on (one at a time).
	struct RaidState
	{
		std::string Phase;                       // "" = none, "march", "steal", "retreat"
		std::string Target;                      // the clan raided
		glm::vec2 TargetCamp{ 0.0f };
		std::vector<std::string> Party;
		std::map<std::string, std::map<std::string, int>> Took;   // raider -> item -> units it took from the target's store (it has stolen: it goes home)
		std::map<std::string, bool> Home;        // raider -> back home (or out of the raid: dead, fled)
		std::map<std::string, int> Loot;         // item -> units brought home
		double Started = 0.0, NextAllowed = -1e9;
		std::string Why;
		int Raids = 0;                           // raids made
		int Stolen = 0;                          // units brought home in all
		bool operator==(const RaidState&) const = default;
		bool Active() const { return !Phase.empty(); }
	};

	// What the clan keeps per member about conflict.
	struct MemberConflict
	{
		std::string Threat;                      // the threat it knows of
		double SeenAt = -1e9;                    // when it last saw or heard it
		double AlarmedAt = -1e9;                 // when it last cried the alarm for it
		glm::vec2 Goal{ 0.0f };                  // where its fight or flight orders send it
		bool Ordered = false;
		double CareUntil = -1e9;                 // someone cared for it: its wounds heal faster until then
		bool operator==(const MemberConflict&) const = default;
	};

	struct ConflictState
	{
		RaidState Raid;
		std::map<std::string, MemberConflict> Members;
		uint64_t Seq = 0;                        // the last combat event read (CombatSystem::Events)
		float Timer = 0.0f;                      // seconds since the last scan of the animals and the stats
		float RaidTimer = 0.0f;                  // seconds since the clan last weighed a raid
		std::map<std::string, int> Hunted;       // animal tag -> kills
		int Fights = 0, Flights = 0, Kills = 0, Deaths = 0, Cares = 0, Raided = 0;
		std::map<std::string, double> RaidedBy;  // clan -> when the clan last saw it raid its store
		bool operator==(const ConflictState&) const = default;
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake
	};
}
