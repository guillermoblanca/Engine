#pragma once
#include "GB/Gameplay/Items.h"
#include <map>
#include <string>

namespace GB
{
	// Aptitudes by practice (E3, H7): every member of a clan has a level from 0 to 100 in each FAMILY of work ("foraging", "woodcutting", "mining",
	// "crafting", "firekeeping"...). Doing the work raises it, with diminishing returns (each unit of practice gives Gain x (1 - level / 100)), and
	// not doing it lowers it slowly (DecayPerDay points a simulated day). A level makes the work modestly better (more gathers per trip, less time
	// between them) and makes the member like that work more (it adds to the role weight of the options of its family), so the trade of a member
	// is a consequence of what it has been doing, not only a label. The profession it holds (TribeRole::Skill) multiplies both the gain and the
	// effect of its own family (decision 4 of PROPUESTA_HABILIDADES_Y_ECONOMIA.md: aptitudes per agent, the trade boosts them).
	//
	// Everything here is data (the "skills" part of the "work" section, see WorkRules in GB/Game/JobBoard.h) and plain CPU: no randomness.

	struct SkillRules
	{
		// Which family an activity belongs to, looked up like a role weight: a node id ("fiber_grass"), then the node's kind from the most specific
		// up ("Resource.Stone.Flint", "Resource.Stone"), else the option's name ("craft", "fire", "stoke"). Nothing found: the activity trains nothing.
		std::map<std::string, std::string> Families;
		// Units of practice that one finished piece of work gives: "gather" per unit gathered, "craft" per thing made, "stoke" per stoke, "fire" per fire lit.
		std::map<std::string, float> Practice;
		float Gain = 0.6f;             // points per unit of practice at level 0 (at level L: Gain x (1 - L / Max)): a day of one trade is some 30 points
		float DecayPerDay = 4.0f;      // points every level loses per simulated day (it never goes below 0)
		float Max = 100.0f;
		float Yield = 0.5f;            // at level Max: this many more gathers per trip (+50%)
		float Speed = 0.4f;            // at level Max: the work goes this much faster (the wait between two gathers is 1 / (1 + 0.4))
		float Weight = 0.6f;           // at level Max: what it adds to the role weight of the options of its family
		float Profession = 1.5f;       // the family of the member's profession: its gain and its effect are multiplied by this
		bool operator==(const SkillRules&) const = default;
	};

	// The levels of one member: family -> level. A family it never practised is absent (level 0), so a save from before skills loads with none.
	using SkillSet = std::map<std::string, float>;

	// The built-in rules (assets/data/era1.json says the same in its "work" section).
	SkillRules DefaultSkillRules();

	// The family of an activity: `key` is a node id or an option name; `defs` gives the node's kind when it is a node. "" = none.
	std::string SkillFamily(const SkillRules& rules, const std::string& key, const std::map<std::string, ResourceNodeDef>* defs);
	float SkillLevel(const SkillSet& skills, const std::string& family);
	// Raises a family by `units` of practice (diminishing returns); `profession` = it is the family of the member's trade. Returns the points gained.
	float Practise(SkillSet& skills, const SkillRules& rules, const std::string& family, float units, bool profession);
	// Lowers every level for `seconds` of time without practice (`daySeconds` is the length of a day). Levels that reach 0 are dropped.
	void DecaySkills(SkillSet& skills, const SkillRules& rules, float seconds, float daySeconds);
	// The effect of a level in 0..Profession: level / Max, times Profession for the member's own trade.
	float SkillEffect(const SkillSet& skills, const SkillRules& rules, const std::string& family, bool profession);
	// The family with the highest level (ties: the first by name); "" when it has none.
	std::string BestSkill(const SkillSet& skills);

	// Reads and writes a SkillSet as a JSON object (family -> level). FromJson of anything that is not an object gives an empty set.
	std::string SkillsToJson(const SkillSet& skills);
	SkillSet SkillsFromJson(const std::string& json);
}
