#pragma once
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	class ClanWorld;
	class TelemetrySink;
	struct Person;

	// T204 (S1 of documentation/00_gdd/VISION_MUNDO_VIVO.md section 13): how a society progresses, measured. It ONLY OBSERVES: every number is read
	// from state the game already keeps and saves (members, the society's people, the store, the buildings, the trails, the knowledge, the world's
	// history and the relations between clans); nothing here is stored, rolls a number or is read back by the simulation, so a run is the same
	// with it or without it. The clan world writes a row per clan and day to the telemetry (ClanWorld::Day -> WriteSocietyDays: table
	// "society_days"), GBSim --report prints a "society:" line per clan and one for the world, and the clan's bar can show the quality of life.
	//
	// QUALITY OF LIFE (qol, 0..1): the weighted mean of parts, each 0..1 (the weights are data, the "metrics" section of assets/data/era1.json):
	//   needs      the mean level of the listed needs (Hunger, Thirst, Rest, Warmth, Company; 100 = met) of the living
	//   shelter    beds of finished buildings per living member (BedsPerPerson beds each is 1)
	//   food       days of food of the clan (Tribe::FoodDays) over FoodDays
	//   tools      of the living adults whose trade has a tool (TribeRole::ToolTag), those who carry one
	//   health     the mean Health of the living (100 = whole)
	//   longevity  the mean age at death of the clan's dead over the age of an elder (SocietyRules::Life.ElderFrom)
	//   techs      of the technologies of the tree that make life better BY WHAT THEY DO (not a list): a building that gives beds, rest, warmth,
	//              store room, keeps food longer, heals, gathers people, stores or digs a well, or a recipe that makes food or a container, the share
	//              the clan knows (those known from the start are left out of both counts)
	// A part that cannot be told yet (no deaths: longevity; no trade with a tool: tools; no Health need: health) is left out and the weights of the
	// others are scaled up to 1, so the index does not jump when it first can be told.
	struct SocietyMetricRules
	{
		std::map<std::string, float> Weights = { { "needs", 0.20f }, { "shelter", 0.15f }, { "food", 0.15f }, { "tools", 0.05f }, { "health", 0.10f },
			{ "longevity", 0.10f }, { "techs", 0.25f } };
		std::vector<std::string> Needs = { "Hunger", "Thirst", "Rest", "Warmth", "Company" };
		float FoodDays = 10.0f;         // days of food that count as secure (a store for a bad season, not for tomorrow)
		float BedsPerPerson = 1.0f;     // beds a person needs for shelter to count whole
		bool operator==(const SocietyMetricRules&) const = default;
	};

	// The parts qol knows, in the order they are written.
	const std::vector<std::string>& QolParts();
	// Reads the "metrics" section of a content file: { "metrics": { "qol": { "weights": { "<part>": w }, "needs": [ ... ], "foodDays": d, "bedsPerPerson": b } } }.
	// A part it does not know, a negative weight or weights that add up to 0 are mistakes. A file without the section changes nothing; on a mistake
	// it returns why and changes nothing.
	std::string LoadSocietyMetricRules(const std::string& json, SocietyMetricRules& rules);
	std::string SocietyMetricRulesToJson(const SocietyMetricRules& rules);   // one JSON object, as the section reads it back

	// The index from its parts (part -> 0..1; a negative value or a missing part = it cannot be told): the weighted mean of what can be told, 0 when
	// nothing can.
	float Qol(const std::map<std::string, float>& parts, const SocietyMetricRules& rules);

	// Lineages: the founders (generation 0, or the topmost ancestor known when a parent is not in `people`) of whom some of `living` descend (both
	// parents count: a child of two founders carries both lines). A founder alive with no living descendant is not a lineage yet. `people`: name ->
	// person, every society of the world together (a parent may live in another clan, or have died there).
	std::vector<std::string> Lineages(const std::map<std::string, const Person*>& people, const std::vector<std::string>& living);

	// One clan on one day.
	struct SocietyMeasure
	{
		int Clan = -1;
		std::string Name;
		int Alive = 0, Births = 0, Deaths = 0;
		std::map<std::string, float> Parts;   // the parts of qol (-1: cannot be told)
		float Qol = 0.0f;
		int Generations = 0;                  // the highest generation alive (founders 0)
		int Lineages = 0;                     // founders with living descendants in this clan
		int TrailCells = 0;                   // cells of worn path (worn or paved) round the camp
		int PathCells = 0;                    // cells of any trail (a dirt trail, worn or paved)
		int Splits = 0;                       // clans this one founded (its history's "found")
		int TradesIn = 0;                     // barters between households of the clan (S13: there are none yet, always 0)
		int TradesOut = 0;                    // barters with other clans (the relations' count)
		int Trades = 0;                       // TradesIn + TradesOut
		int Gifts = 0;                        // gifts between this clan and others
		int Roles = 0;                        // distinct trades of the living adults
		float SkillEntropy = 0.0f;            // the mean entropy (bits) of the aptitudes of each living adult: low = a specialist (the task entropy is the report's)
		int Techs = 0;                        // technologies the clan knows
		int Inventors = 0;                    // living members who were the first of a clan to work a technology out by experimenting
	};

	// What the world is: the clans together.
	struct SocietyWorld
	{
		int Clans = 0, Alive = 0, Splits = 0, TradesOut = 0, Lineages = 0, Generations = 0, Techs = 0, Inventors = 0;
		float Qol = 0.0f;                     // mean of the clans' weighted by their living
	};

	// Measures a clan (a tribe; `world`, when there is one, gives the trades with other clans and the people of the other clans for the lineages).
	SocietyMeasure MeasureSociety(const Tribe& tribe, const SocietyMetricRules& rules, int clan = 0, const ClanWorld* world = nullptr);
	// Every clan of the world, in id order.
	std::vector<SocietyMeasure> MeasureSocieties(const ClanWorld& world);
	SocietyWorld MeasureWorld(const ClanWorld& world, const std::vector<SocietyMeasure>& clans);
	// "society: <clan> qol 0.62 (needs .. ) gen 2 lineages 5 ...": the line of --report.
	std::string SocietyLine(const SocietyMeasure& m);
	std::string SocietyWorldLine(const SocietyWorld& w);
	// A row of "society_days" per clan (ClanWorld::Day; GBSim writes the last day if the clock stopped short of it).
	void WriteSocietyDays(const ClanWorld& world, TelemetrySink& sink, double time, int day);
}
