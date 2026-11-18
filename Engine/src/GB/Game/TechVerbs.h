#pragma once
// T322 (documentation/01_diseno/ARBOL_TECNICO.md §11.3 and §11.7.1): technique -> verb. A technology may name verbs in the data ("verbs":
// [ "farm:till", "farm:sow" ]; GB/Gameplay/Techs.h, CheckVerb): such a verb exists only for a clan that knows one of the technologies that name
// it (any of them, as the buildings: a living member knows it). A verb no technology names is free, as everything was before, so with no
// "verbs" in the data nothing here changes a run.
//
// A verb is "kind" or "kind:what", the patterns of WorkCategories: the work of the clan (a job of the board, an option of the brain) and the
// player's orders are matched by their kind and what they are about ("farm" + "till" -> "farm:till"; the more specific verb rules first).
// Without the verb:
//   - the board does not post the job (Withdraw, one line before JobBoard::End in Tribe::PostJobs);
//   - the brain does not choose the option (Options, one line in Tribe::Decide);
//   - an order (ColonyOrders::Give), a mark (ColonyDesignations::Designate) or a farm zone (FarmZones::PaintOutline, the verb farm:till) is
//     refused with "does not know how to <verb>";
//   - the clans of the AI mark out no field of their own (FarmZones::Post).
// For the UI (T327, §11.2.5: what is not known does not show): KnowsVerb and List (each verb some technology names, known or not, with those
// technologies), Unknown (how many there are to discover).
//
// Stateless (what the clan knows decides; nothing is saved), CPU only, deterministic (every list in id order).
#include "GB/AI/Utility.h"
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	class JobBoard;
	struct TechTree;

	class TechVerbs
	{
	public:
		struct Line
		{
			std::string Verb;                 // "farm:till"
			std::string Word;                 // "till" (VerbWord)
			bool Known = false;               // the clan knows one of its technologies
			std::vector<std::string> Techs;   // the technologies that unlock it, in id order
		};

		// The verb some technology of `tree` names that rules a work of `kind` on `what` ("kind:what" first, then "kind"); "" = free.
		static std::string Ruling(const TechTree* tree, const std::string& kind, const std::string& what);
		// The clan knows the verb: no technology names it, or a living member knows one that does.
		static bool Knows(const Tribe& tribe, const std::string& verb);
		// The clan may do a work of `kind` on `what` (the verb that rules it, if any, is known).
		static bool Allows(const Tribe& tribe, const std::string& kind, const std::string& what);
		// "" when it may, else "does not know how to <word>" (the replies of the orders read it).
		static std::string WhyNot(const Tribe& tribe, const std::string& kind, const std::string& what);
		// Every verb some technology names, in id order, known or not (empty: no verbs in the data).
		static std::vector<Line> List(const Tribe& tribe);
		// How many of them the clan does not know yet (the "?" of the UI).
		static int Unknown(const Tribe& tribe);
		// "verbs known: till (farm:till: tillage) | to discover 1: reap (reap: reaping)" ("" with no verbs in the data): `game verbs` and GBSim's
		// --report.
		static std::string Report(const Tribe& tribe);

		// ---- the tribe's hooks (one line each)
		// Tribe::PostJobs, before End: the jobs of a verb the clan does not know go back (JobBoard::Withdraw).
		static void Withdraw(const Tribe& tribe, JobBoard& board);
		// Tribe::Decide: the options of a verb the clan does not know go ("rest" always stays).
		static void Options(const Tribe& tribe, std::vector<UtilityOption>& options);
	};

	// For the UI: TechVerbs::Knows.
	bool KnowsVerb(const Tribe& clan, const std::string& verb);
}
