#pragma once
#include "GB/Game/Skills.h"
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// The job board of a clan (E8, H7): what the clan needs, posted as jobs that its members claim. The tribe posts them from its needs every few
	// seconds (a stock below its target, a tool a member lacks, a campfire running low or still to be lit, the upkeep of the camp) and each member,
	// when it decides, claims the job worth most to it (its priority, the member's aptitude for it, how far it is, how fit it is). A claim does not
	// replace the utility chooser of the brain (GB/AI/Utility.h): it is one more factor ("job") on the options that do that work, so the needs still
	// win (a hungry member eats) and the hysteresis of the chooser still holds. Rules against jams (PROPUESTA 5): a claim with no progress for a
	// while lapses, and a job nobody claims rises in priority as it waits.
	//
	// Plain data and CPU, deterministic: the jobs are kept by id ("gather wood", "craft stone_axe", "stoke Campfire") and every list is in id order.

	struct JobRules
	{
		float Every = 5.0f;             // seconds between two refreshes of the board
		float Bonus = 0.6f;             // an option that does the member's claimed job scores this much more (x 1.6)
		float SlotUnits = 6.0f;         // a gather job takes one claimant per this many units wanted ...
		int MaxSlots = 3;               // ... up to this many
		float ClaimSeconds = 240.0f;    // a claim with no progress for this long lapses
		float AgeBoost = 0.5f;          // a job with nobody on it gains this much priority (x) ...
		float AgeSeconds = 600.0f;      // ... every this many seconds it waits ...
		float MaxAge = 1.0f;            // ... up to this much
		float MinScore = 0.15f;         // a member does not claim a job worth less than this to it
		float Switch = 0.3f;            // a member that holds a claim only takes another one worth this much more
		std::map<std::string, float> Priority;   // the base priority of each kind of job: "gather", "craft", "stoke", "fire"
		std::map<std::string, float> Upkeep;     // item -> what the camp uses up per member (fiber 1): added to the stock the board asks for
		float FireWood = 4.0f;          // wood wanted on top of the stock while there is a campfire to keep
		// The pyramid of needs (Maslow): while the clan is short of food, staying alive comes first.
		float SurvivalDays = 1.5f;      // days of food (store and bags) the clan wants in hand before it thinks of anything else
		float SurvivalBoost = 2.0f;     // food jobs and food options score x (1 + this x how short of food the clan is)
		float SurvivalDamp = 0.25f;     // what is not about staying alive scores this much (x) when the clan is as short of food as can be
		float CriticalHunger = 55.0f;   // a member this hungry with no food in hand drops what it was doing for food (the same pressure, its own)
		bool operator==(const JobRules&) const = default;
	};

	// How the roles (professions) of a clan are chosen and changed (H7).
	struct RoleRules
	{
		float Review = 900.0f;          // seconds between two reviews of who does what (at most one member changes its trade in each)
		float Inertia = 0.3f;           // a member's skill in its own trade counts this much against moving it (x its level / 100)
		float SkillSwitch = 25.0f;      // a member whose skill in another trade is this many points above its own's takes that trade
		int MinClan = 3;                // a clan smaller than this does not specialise: nobody changes trade
		float FireDemand = 6.0f;        // what keeping the campfire weighs, in units of stock, when the trades are shared out
		bool operator==(const RoleRules&) const = default;
	};

	// The "work" section of a content file: the names of the clan, the aptitudes, the job board and the review of the trades.
	struct WorkRules
	{
		std::vector<std::string> Names;   // the names new members are given, in order (when they run out: the same names with a number)
		SkillRules Skills;
		JobRules Jobs;
		RoleRules Roles;
		bool operator==(const WorkRules&) const = default;
	};

	WorkRules DefaultWorkRules();   // assets/data/era1.json says the same in its "work" section
	// { "work": { "names": [ ... ], "skills": { "families": { "<key>": "<family>" }, "practice": { "gather": 1 }, "gain", "decayPerDay", "max", "yield", "speed",
	//   "weight", "profession" }, "jobs": { "every", "bonus", "slotUnits", "maxSlots", "claimSeconds", "ageBoost", "ageSeconds", "maxAge", "minScore", "switch",
	//   "priority": { "<kind>": p }, "upkeep": { "<item>": perMember }, "fireWood" }, "roles": { "review", "inertia", "skillSwitch", "minClan", "fireDemand" } } }.
	// Each of "names", "skills", "jobs" and "roles" replaces that part whole (what it does not say is the default); a file without the section changes
	// nothing; on a mistake it returns why and changes nothing.
	std::string LoadWorkRules(const std::string& json, WorkRules& rules);
	std::string LoadWorkRulesFile(const std::string& path, WorkRules& rules);
	std::string WorkRulesToJson(const WorkRules& rules);   // one JSON object, as the section reads it back
	// The name of the n-th member (0-based): Names[n], then the names again with a number ("Ana2"...). Never empty.
	std::string MemberName(const WorkRules& rules, size_t n);

	struct JobClaim
	{
		std::string Member;
		double Since = 0.0;             // when it claimed it
		double Progress = 0.0;          // the last time it did some of the work (lapses ClaimSeconds after this)
		float Done = 0.0f;              // units delivered (gather) or pieces of work done
		bool operator==(const JobClaim&) const = default;
	};

	struct Job
	{
		std::string Id;                 // kind + " " + target
		std::string Kind;               // "gather", "craft", "stoke", "fire"
		std::string Target;             // the item, the tool or the campfire
		std::string Family;             // the aptitude it calls for ("" = any)
		std::string Why;                // for people: "the store has 3 of 10 wood"
		float Amount = 0.0f;            // units wanted (gather), members lacking it (craft), 1 otherwise
		float Base = 1.0f;              // its priority from the need
		float Priority = 1.0f;          // with the waiting added
		int Slots = 1;                  // how many members can hold it at once
		double Posted = 0.0;
		double Waiting = 0.0;           // since when nobody holds it (the age that raises its priority)
		std::vector<JobClaim> Claims;   // in the order they were made
		bool operator==(const Job&) const = default;
	};

	class JobBoard
	{
	public:
		// A refresh: Begin, Post every job the clan needs now, End. A job posted again keeps its claims and its age; one not posted again goes.
		void Begin();
		Job& Post(const std::string& kind, const std::string& target, const std::string& family, float amount, float base, int slots, const std::string& why, double now, const JobRules& rules);
		// Drops the jobs that were not posted again, the claims that lapsed and the claims over a job's slots. Returns "<member> <job id>" for each claim that went.
		std::vector<std::string> End(double now, const JobRules& rules);

		const std::map<std::string, Job>& Jobs() const { return m_Jobs; }
		const Job* Find(const std::string& id) const;
		// The job a member holds (nullptr: none).
		const Job* ClaimOf(const std::string& member) const;
		bool HasRoom(const Job& job) const { return static_cast<int>(job.Claims.size()) < job.Slots; }
		// Claims a job (releasing any other the member held). False when it has no room or does not exist.
		bool Claim(const std::string& id, const std::string& member, double now);
		void Release(const std::string& member);
		// The member did `units` of the work of its job: the claim stays alive.
		void Progress(const std::string& member, float units, double now);
		void Clear() { m_Jobs.clear(); }

		// The jobs from the most to the least pressing (ties: by id), for panels and reports.
		std::vector<const Job*> Ordered() const;
		std::string Report() const;   // one line per job: "gather wood x6 (p 1.20): Ana, Biel | the store has 4 of 10"

		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		std::map<std::string, Job> m_Jobs;
		std::map<std::string, bool> m_Posted;   // during a refresh: the ids posted again
	};
}
