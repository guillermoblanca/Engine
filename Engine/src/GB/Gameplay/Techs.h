#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GB
{
	// Technologies (H11, E4 + E5): what a clan can know, as data. A technology is a piece of know-how ("flint knapping") that unlocks recipes (who does
	// not know it cannot make them), resource nodes (who does not know it does not see them as worth working) and, later, buildings. It may need other
	// technologies first, and says how it can be found: by EXPERIMENTING with the materials of one of its recipes at hand, by OBSERVING someone who
	// uses it, or by being TAUGHT at the camp. Pure data with no scene, read from the "techs" section of a content file; the knowledge of each agent
	// and of the clan, and the discovery itself, are GB/Game/Knowledge.h.
	//
	// A recipe or node that no technology names is common knowledge: everyone can make or work it, as before technologies existed. Several
	// technologies may name the same recipe: knowing any of them is enough.

	// How a technology can be found. Every number is per event, so the pace is data (and 0 closes that way).
	struct TechDiscovery
	{
		float Experiment = 0.0f;   // chance that one try succeeds (0..1); each failed try makes the next likelier (KnowledgeRules::Growth). 0: not by trying
		float Observe = 0.0f;      // understanding (0..1) gained each time the agent sees someone use it; at 1 it is learnt. 0: not by watching
		float Teach = 0.0f;        // understanding gained in one lesson at the camp from someone who knows it. 0: it cannot be taught
		float Seconds = 20.0f;     // how long one try takes (the agent works at the camp all that time)
		float Waste = 0.0f;        // chance that a failed try spoils one unit of the first input of the recipe it tried (0..1)
		bool operator==(const TechDiscovery&) const = default;
	};

	// T181: what the world must give before a technology can be learnt at all (a hard gate, unlike `requires`): every material of the list found by
	// the clan (in hand, in the store or in its memory of the land) and one of the stations there. "physical": { "materials": [], "stations": [] }.
	// Smelting needs copper ore and a kiln or a forge (a fire hot enough), not a campfire. Empty: nothing physical (an idea, a way of working).
	struct TechPhysical
	{
		std::vector<std::string> Materials;   // all of them found
		std::vector<std::string> Stations;    // one of them there (empty: none needed)
		bool Empty() const { return Materials.empty() && Stations.empty(); }
		bool operator==(const TechPhysical&) const = default;
	};

	struct TechDef
	{
		std::string Id;
		std::string Name;
		bool Known = false;                    // known from the start: every agent of a clan knows it when it joins
		// T181: technologies that make it easier (no longer a gate): each one known speeds every way of learning it by KnowledgeRules::PrereqBonus,
		// each one missing slows it by KnowledgeRules::MissingPrereqFactor. A clan may skip a step if the world gives it the chance.
		std::vector<std::string> Requires;
		std::vector<std::string> Recipes;      // recipes it unlocks
		std::vector<std::string> Nodes;        // resource nodes (ids of their defs) that only those who know it work
		std::vector<std::string> Buildings;    // buildings it unlocks (ids of the "buildings" section, H8)
		TechPhysical Physical;                 // T181: the hard gate (the material and the process it needs)
		// T181 (2): how hard it is to work out (0..1; < 0 = KnowledgeRules::DefaultComplexity). Only who has the intellect for it invents it; who has
		// less still learns it from someone who knows it, slower (KnowledgeRules::GraspFloor, GraspSlope).
		float Complexity = -1.0f;
		TechDiscovery Discovery;
		bool operator==(const TechDef&) const = default;
	};

	// T181: learning by using a material, and curiosity ("knowledge": { "use": { ... } }; GB/Game/UseLearning.h). There are no eras.
	struct UseRules
	{
		// material (item id) -> technology -> understanding one use gives (a gather trip, a recipe made, a phase built with it)
		std::map<std::string, std::map<std::string, float>> Materials;
		std::string Effect = "learn";   // the option key of the society's effects (trait curiosity, norm inquiry) that scales learning
		float Pull = 0.5f;              // how much a found material pulls the clan towards its technologies (+2 x have / Enough; the basics ladder pulls 1)
		float Enough = 20.0f;           // this much of a material at hand (or known of) is a full pull
		float Near = 300.0f;            // metres at which a remembered node counts half
		float Damp = 0.15f;             // the experiments while the basics are not covered are worth this much (a priority, not a block)
		float BasicsFood = 1.0f;        // the basics (Maslow): food in the store per living member ...
		bool BasicsFire = true;         // ... a campfire ...
		float BasicsBeds = 0.5f;        // ... and beds for this share of the living
		// T181 (2): the station a lit campfire gives to TechPhysical::Stations (cooking, smoking: the fire is the process, no building needed). "" = none
		std::string FireStation = "campfire";
		float PaceMin = 0.5f;           // T181 (2): the society's factor on Effect is kept within these (curiosity decides how much one tries, ...
		float PaceMax = 1.5f;           // ... not a gap of nine times between two members)
		bool operator==(const UseRules&) const = default;
	};


	// The pace of knowledge for the whole clan (JSON "knowledge" beside "techs").
	struct KnowledgeRules
	{
		float LessonSeconds = 20.0f;   // how often the camp holds a lesson: each learner there gets Teach of one technology from a teacher there
		float ObserveRadius = 12.0f;   // how far away an agent sees someone else use a technology
		float Growth = 0.5f;           // each failed try multiplies the next try's chance by (1 + Growth x tries so far)
		uint32_t Seed = 1;             // the seed of the rolls: the same seed, names and clock give the same discoveries
		float ExperimentProgress = 1.0f;   // U6: a failed try adds its chance x this to the agent's understanding (Progress): at 1 it is found. 0: luck only
		// The ladder a clan climbs on its own ("progression": [ tech ids ]): the first it does not know yet (and what that requires) is one of
		// its goals, beside the technologies of the buildings it needs (Tribe::UpdateGoals). Empty = only the buildings lead it.
		std::vector<std::string> Progression;
		// T181: a known prerequisite multiplies the progress of every way of learning by PrereqBonus, a missing one by MissingPrereqFactor
		float PrereqBonus = 1.1f;
		float MissingPrereqFactor = 0.25f;
		UseRules Use;                  // T181: learning by use and curiosity
		// T181 (2): intelligence. Inventing (experimenting) needs the gene IntellectGene >= the technology's complexity (a hard gate); learning
		// it from someone who knows it is multiplied by max(GraspFloor, 1 - GraspSlope x max(0, complexity - intellect)). A society without the
		// gene: everyone has the intellect for everything.
		std::string IntellectGene = "intellect";
		float DefaultComplexity = 0.3f;
		float GraspFloor = 0.25f;
		float GraspSlope = 2.0f;
		// V6: the years the world leaps between two lives (0: none). T181 left one value where each era had its own: the player's death and its
		// tests still go through a leap; taking the leap out is a task of its own.
		float LeapYears = 0.0f;
		bool operator==(const KnowledgeRules&) const = default;
	};

	struct TechTree
	{
		std::map<std::string, TechDef> Techs;
		KnowledgeRules Rules;

		const TechDef* Find(const std::string& id) const;
		// The technologies that unlock a recipe / a node (empty: everyone may make or work it), in id order.
		std::vector<std::string> ForRecipe(const std::string& recipe) const;
		std::vector<std::string> ForNode(const std::string& node) const;
		// The technologies known from the start (TechDef::Known).
		std::vector<std::string> Starting() const;
		bool operator==(const TechTree&) const = default;
	};

	// The technologies of the game's data ("techs" and "knowledge" of every file of the game's pack: one tree, no eras): stone tools, fire making and
	// cordage are known from the start (so the tribe does what it did before technologies existed); the rest is found (docs/PROGRESION.md).
	TechTree DefaultTechs();
	// T181: how much easier `tech` is for someone who knows `known`: PrereqBonus for each prerequisite known, MissingPrereqFactor for each missing.
	float PrereqFactor(const TechTree& tree, const std::string& tech, const std::set<std::string>& known);
	// T181 (2): the complexity of `tech` (its own, else the rules' default; 0 for a technology that is not in the tree).
	float Complexity(const TechTree& tree, const std::string& tech);
	// T181 (2): how fast someone of `intellect` learns `tech` from others: max(GraspFloor, 1 - GraspSlope x max(0, complexity - intellect)).
	float GraspFactor(const TechTree& tree, const std::string& tech, float intellect);

	// Reads the "techs" and "knowledge" sections of a content file:
	//   { "techs": [ { "id", "name", "known", "requires": [], "recipes": [], "nodes": [], "buildings": [],
	//                  "physical": { "materials": [], "stations": [] }, "complexity",
	//                  "discovery": { "experiment", "observe", "teach", "seconds", "waste" } } ],
	//     "knowledge": { "lessonSeconds", "observeRadius", "growth", "seed", "progression": [], "prereqBonus", "missingPrereqFactor",
	//                    "intellectGene", "defaultComplexity", "graspFloor", "graspSlope",
	//                    "use": { "materials": { "<item>": { "<tech>": rate } }, "effect", "pull", "enough", "near", "damp",
	//                             "basics": { "food", "fire", "beds" }, "paceMin", "paceMax" },
	//                    "leapYears" } }
	// A technology is added or replaced whole by id; "knowledge" replaces the rules. A file without them changes nothing. Values are checked (chances
	// 0..1, seconds above 0); names that point elsewhere (a recipe, a required technology) are checked by CheckReachability, since they may come in a
	// later file. On a mistake it returns why and changes nothing.
	std::string LoadTechs(const std::string& json, TechTree& tree);
	// The tree as the sections read it back (one JSON object with "techs" and "knowledge"; only the discovery numbers that are not the defaults).
	std::string TechsToJson(const TechTree& tree);
}
