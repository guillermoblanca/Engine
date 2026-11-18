#pragma once
// T306 (C2 of documentation/01_diseno/MODO_COLONIA.md §4.3, with the director's note of 2026-11-18): the table of work priorities of each person
// of the player's clan, in two levels. Each person numbers every category of work (food, construction, crafting...: WorkCategories.h, data)
// from 1 (first) to 10 (last) or "-" (never); and, inside a category, every job (food: hunt, fish, forage, cook, preserve), which inherits the
// number of its category until the player gives it one of its own. A category at "-" turns all its jobs off; a job at "-" only itself.
//
// What it does. A person with a table takes the work of the lowest category number that has work for it now and, inside it, the lowest job
// number (rank = category x 16 + job): on the job board (Claim: the order is strict; the usual score only chooses among jobs of the same rank,
// and a job held of a worse rank is let go as soon as a better one is there) and on the options of its brain (Options: a work option of "-"
// scores 0 and one of a worse rank than the best one it has now is worth kBelow as much). What no job of the categories matches is free (eat,
// drink, sleep, talk...): the table does not touch it. The needs still win: a job or an option the pyramid of needs boosts (the clan short of
// food, the person hungry: TribeWork.cpp's SurvivalFactor and MaslowOptions) is out of the order, but a "-" only gives way to the person's own
// critical hunger or thirst (JobRules::CriticalHunger), never to the clan's shortage; and a direct order (T268) does not go through the
// decision at all. The marks of T270 ask it too (ColonyDesignations::MemberPriority).
//
// Whose. Only the player's clan in the colony mode: the host (WorldDirector::Update) turns it on (SetActive) for that clan's tribe. A person with
// no table when it is on is given the one its trade suggests (C-5: "suggest" of each category, else its "default"). Off, or with no categories
// in the data, nothing here runs: the clans of the AI and GBSim are the same as before.
//
// The tribe owns one (Tribe::WorkTable()) and calls it from one line in each place: ClaimJob (TribeWork.cpp) and Decide's options (Tribe.cpp).
// Saved under "priorities" when there is a table. CPU only, deterministic (every list in name or data order).
#include "GB/AI/Utility.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;
	struct Job;

	class WorkPriorities
	{
	public:
		static constexpr int kOff = 0;            // "-": never
		static constexpr int kMax = 10;
		static constexpr int kFree = 1000;        // the rank of a job of the board no category takes: after every ranked one
		static constexpr float kBelow = 0.1f;     // a work option of a worse rank than the best one the person has now

		struct Table
		{
			std::map<std::string, int> Categories;   // category -> 1..10, 0 = "-"
			std::map<std::string, int> Jobs;         // job -> 1..10, 0 = "-" (absent: the number of its category)
			bool operator==(const Table&) const = default;
		};
		struct Reply
		{
			bool Ok = false;
			std::string Text;   // English (the commands' answers and the tests read it)
		};
		// A job or an option weighed for the strict order: its rank (0: the needs boost it, before any rank) and its usual score.
		struct Candidate
		{
			int Rank = 0;
			float Score = 0.0f;
		};

		void SetActive(bool on) { m_Active = on; }
		bool Active() const { return m_Active; }

		// ---- the tables
		const Table* Of(const std::string& member) const;
		// The table a person's trade suggests (every category; no job of its own).
		static Table Suggested(const Tribe& tribe, const TribeMember& m);
		// Sets a category's or a job's number (1..10 or kOff) for a person (made from its suggestion first if it has none).
		Reply Set(const Tribe& tribe, const std::string& member, const std::string& what, int number);
		// Back to what its trade suggests.
		Reply Reset(const Tribe& tribe, const std::string& member);
		// The table of a person in a line: "Ana (forager): food 1 [hunt 1, fish 1, forage 1, cook 1, preserve 1], construction 4 [...], ..."
		std::string Show(const Tribe& tribe, const std::string& member) const;
		// The number a person gives a job now (its own or its category's; kOff when either is "-"); -1: no such job.
		int Number(const Tribe& tribe, const std::string& member, const std::string& job) const;
		// The job a piece of work falls under for the table ("" = free), with the item it is about read from the tribe (a node, a recipe...).
		static std::string JobOf(const Tribe& tribe, const TribeMember* m, const std::string& kind, const std::string& target);
		// "-" or the number.
		static std::string Text(int number) { return number == kOff ? std::string("-") : std::to_string(number); }
		// Reads "1".."10" or "-" (false: neither).
		static bool Parse(const std::string& word, int& number);

		// ---- the order (pure: what the tests weigh)
		// The candidate a person takes: the lowest rank, then the highest score (ties: the first). `held` (-1: none) is kept unless another one
		// has a better rank, or the same rank and `margin` more score. -1: none.
		static int Choose(const std::vector<Candidate>& candidates, int held, float margin);

		// ---- the tribe's hooks
		// ClaimJob: true when the table chose (or chose to keep) the member's job; false: the usual claim (no table, off). `score` is the
		// tribe's JobScore x SurvivalFactor for the member.
		bool Claim(Tribe& tribe, TribeMember& m, const std::function<float(const Job&)>& score);
		// Decide, before choosing: "-" work options to 0, the ones of a worse rank than the best one there to kBelow.
		void Options(const Tribe& tribe, const TribeMember& m, std::vector<UtilityOption>& options) const;
		// The marks (T270): 0 when the person has the gather of that node at "-", else 1.
		float MarkFactor(const Tribe& tribe, const TribeMember& m, const std::string& node) const;
		// Whether the table rules this person now (on, a table, not a player's character).
		bool Rules(const TribeMember& m) const;

		bool Empty() const { return m_Tables.empty(); }
		void Clear() { m_Tables.clear(); }
		void Drop(const std::string& member) { m_Tables.erase(member); }

		// Saving (the tribe's "priorities" section, only written when there is a table).
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		int Rank(const Tribe& tribe, const TribeMember& m, const std::string& job) const;   // -1 off; else category x 16 + job
		int JobRank(const Tribe& tribe, const TribeMember& m, const Job& job) const;          // -1 off, 0 the needs, kFree free, else Rank
		Table& TableFor(const Tribe& tribe, const TribeMember& m);
		static bool Critical(const Tribe& tribe, const TribeMember& m);   // hunger or thirst under the board's CriticalHunger

		bool m_Active = false;                  // the host's (not saved): the player's clan in the colony mode
		std::map<std::string, Table> m_Tables;  // member -> its table
	};
}
