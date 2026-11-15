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
	// QUALITY OF LIFE (qol, 0..1): how far a people has come from a bare survival, read as "how much this people has progressed" (it is what the
	// clan's bar shows instead of an era). It is the weighted mean of parts, each 0..1 (the weights and every number below are data, the "metrics"
	// section of assets/data/era1.json; documentation/02_tecnico/herramientas/LABORATORIO.md, "Indice de calidad de vida", says why each is what it
	// is), times what the clan lost lately:
	//   needs      how far above NeedsFloor the mean level of the listed needs (Hunger, Thirst, Rest, Warmth, Company; 100 = met) of the living is
	//              (a camp that only gets by sits at the floor: there it is 0, everything met is 1)
	//   shelter    the beds of finished buildings for the living (BedsPerPerson each), the best first, each worth its comfort (the building's rest
	//              plus warmth) over BedComfort: a lean-to is half a bed, a longhouse a whole one
	//   food       days of food of the clan (Tribe::FoodDays) on a curve that never fills: d / (d + FoodHalf) (FoodHalf days is 1/2)
	//   tools      of the living adults whose trade has a tool (TribeRole::ToolTag), the best one each carries: (1 + Power) / (1 + ToolPower)
	//              (a stone axe is a third, a flint one two thirds, a copper one whole; none is 0)
	//   health     the mean Health of the living (100 = whole)
	//   longevity  the mean, over the clan's dead, of the age each reached over the age of an elder (SocietyRules::Life.ElderFrom), each at most 1
	//   techs      the technologies the clan knows that make life better BY WHAT THEY DO (not a list): a building that gives beds, rest, warmth,
	//              store room, keeps food longer, heals, gathers people, stores or digs a well, or a recipe that makes food, a container or a tool (those
	//              known from the start left out), COUNTED, not shared out over the tree, on a curve that never fills: n / (n + TechsHalf). A tree
	//              that grows does not lower it, and each new one adds less than the one before
	// losses (0..1): the clan's young dead of the last LossDays days (who died before ElderFrom, of anything; a death weighs 1 the day it happens
	// and fades to nothing at LossDays) over the living plus them, times LossScale: qol = mean x (1 - losses). A clan that lost its hungry does
	// not read well.
	// A part that cannot be told yet (no deaths: longevity; no trade with a tool: tools; no Health need: health) is left out and the weights of the
	// others are scaled up to 1, so the index does not jump when it first can be told.
	struct SocietyMetricRules
	{
		std::map<std::string, float> Weights = { { "needs", 0.10f }, { "shelter", 0.15f }, { "food", 0.10f }, { "tools", 0.10f }, { "health", 0.05f },
			{ "longevity", 0.10f }, { "techs", 0.40f } };
		std::vector<std::string> Needs = { "Hunger", "Thirst", "Rest", "Warmth", "Company" };
		float NeedsFloor = 0.6f;       // the mean need level (0..1) of a camp that only gets by: the needs part is 0 there
		float FoodHalf = 5.0f;          // days of food for which the food part is 1/2
		float BedsPerPerson = 1.0f;     // beds a person needs for shelter to count whole
		float BedComfort = 0.9f;        // rest + warmth of a bed that counts whole
		float ToolPower = 2.0f;         // the Power of a tool that counts whole
		float TechsHalf = 20.0f;       // technologies that better life for which the techs part is 1/2
		float LossDays = 5.0f;          // days a young death weighs on the index (fading)
		float LossScale = 1.5f;        // how much the share of the recent young dead takes off (0: nothing)
		bool operator==(const SocietyMetricRules&) const = default;
	};

	// The parts qol knows, in the order they are written.
	const std::vector<std::string>& QolParts();
	// Reads the "metrics" section of a content file: { "metrics": { "qol": { "weights": { "<part>": w }, "needs": [ ... ], "needsFloor": f, "foodHalf": d,
	// "bedsPerPerson": b, "bedComfort": c, "toolPower": p, "techsHalf": n, "lossDays": d, "lossScale": s } } }.
	// A part it does not know, a negative weight or weights that add up to 0 are mistakes. A file without the section changes nothing; on a mistake
	// it returns why and changes nothing.
	std::string LoadSocietyMetricRules(const std::string& json, SocietyMetricRules& rules);
	std::string SocietyMetricRulesToJson(const SocietyMetricRules& rules);   // one JSON object, as the section reads it back

	// The index from its parts (part -> 0..1; a negative value or a missing part = it cannot be told): the weighted mean of what can be told, 0 when
	// nothing can.
	float Qol(const std::map<std::string, float>& parts, const SocietyMetricRules& rules);
	// What `youngDead` recent young dead (each weighed by how recent) take off the index of a clan of `alive`: LossScale x dead / (alive + dead), 0..1.
	float Losses(float youngDead, int alive, const SocietyMetricRules& rules);
	// The curve of the parts that never fill: n / (n + half) (0 for n <= 0).
	float Saturate(float n, float half);

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
		float YoungDead = 0.0f;               // the young dead of the last LossDays, each weighed by how recent (1 = today)
		float Losses = 0.0f;                  // what they take off the index (0..1)
		float Qol = 0.0f;                     // Qol(Parts) x (1 - Losses)
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
		int LifeTechs = 0;                    // of them, those that better life (not known from the start): what the techs part counts
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
