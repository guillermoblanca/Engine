#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	class Inventory;
	class ItemLibrary;

	// T310 (C2, documentation/01_diseno/ARRANQUE_Y_LLEGADAS.md §2, §5.1): how the player's clan is founded, the "founding" block of
	// assets/data/colony.json. Plain data; the candidates and the choice are GB/Game/Founding.h.
	//
	//   "founding": { "candidates": 8, "party": { "min", "max", "group" }, "startSkill": { "min", "max", "neighbours" },
	//                 "trades": { "<gene>": "<role>" }, "kit": { "base": { "<item>": { "fixed", "perMember" } }, "extra": { "<people>": { "<item>": n } } } }
	//
	// The kit is what a clan finds lying around on the first day: the base (today's `wood 2 + 2 n`, `stone 3 n`) for every clan, and the extra of
	// its number of people only for the clan founded with a party (the player's: `shell new ... party <n>`, GBSim --party). With 6 people the extra
	// is nothing, so a run without a party is the same as before.
	struct FoundingRules
	{
		int Candidates = 8;              // the people the screen offers
		int PartyMin = 1, PartyMax = 6;  // how many may be chosen
		int Group = 4;                   // the "group" start (two couples)
		float SkillMin = 20.0f, SkillMax = 35.0f;   // the level of the aptitude of the suggested trade each chosen one starts with
		bool SkillNeighbours = false;    // ... also the clans of the AI (false: only the player's party; the neighbours stay as before)
		std::map<std::string, std::string> Trades;   // gene -> role: the suggested trade is the role of the candidate's strongest of these genes
		struct BaseItem { std::string Item; int Fixed = 0; int PerMember = 0; bool operator==(const BaseItem&) const = default; };
		std::vector<BaseItem> Base;      // in the order of the file (the store is filled in that order)
		std::map<int, std::vector<std::pair<std::string, int>>> Extra;   // people -> the extra items (in the order of the file)
		bool operator==(const FoundingRules&) const = default;
	};

	// The built-in rules: what assets/data/colony.json says (the founding test checks they are the same).
	FoundingRules DefaultFoundingRules();
	// Reads the "founding" block of a content file (a file without it changes nothing); on a mistake, why, and nothing changes.
	std::string LoadFoundingRules(const std::string& json, FoundingRules& rules);
	std::string FoundingRulesToJson(const FoundingRules& rules);

	// The kit of a clan of `people`: the base, plus the extra of that number when `party` (the player's founded clan). Items in the order of the
	// data, the base first; an item named in both comes once with the sum.
	std::vector<std::pair<std::string, int>> FoundingKit(const FoundingRules& rules, int people, bool party);
	// Puts the kit into a store. Returns "" or the items that did not go in whole ("hide 1 of 2 ...").
	std::string AddFoundingKit(Inventory& store, const ItemLibrary& items, const FoundingRules& rules, int people, bool party);
}
