#pragma once
#include <cstdint>
#include <map>
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

	struct TechDef
	{
		std::string Id;
		std::string Name;
		int Era = 1;
		bool Known = false;                    // known from the start of its era: every agent of a clan knows it when it joins
		std::vector<std::string> Requires;     // technologies the agent must know before it can find or learn this one
		std::vector<std::string> Recipes;      // recipes it unlocks
		std::vector<std::string> Nodes;        // resource nodes (ids of their defs) that only those who know it work
		std::vector<std::string> Buildings;    // buildings it unlocks (names: the buildings are not data yet, E9)
		TechDiscovery Discovery;
		bool operator==(const TechDef&) const = default;
	};

	// The pace of knowledge for the whole clan (JSON "knowledge" beside "techs").
	struct KnowledgeRules
	{
		float LessonSeconds = 20.0f;   // how often the camp holds a lesson: each learner there gets Teach of one technology from a teacher there
		float ObserveRadius = 12.0f;   // how far away an agent sees someone else use a technology
		float Growth = 0.5f;           // each failed try multiplies the next try's chance by (1 + Growth x tries so far)
		uint32_t Seed = 1;             // the seed of the rolls: the same seed, names and clock give the same discoveries
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

	// The built-in technologies of era 1 (assets/data/era1.json says the same in its "techs" and "knowledge" sections): stone tools, fire making and
	// cordage are known from the start (so the tribe does what it did before technologies existed); flint knapping and basketry are found, and
	// smelting waits for the forge (E10).
	TechTree DefaultTechs();

	// Reads the "techs" and "knowledge" sections of a content file:
	//   { "techs": [ { "id", "name", "era", "known", "requires": [], "recipes": [], "nodes": [], "buildings": [],
	//                  "discovery": { "experiment", "observe", "teach", "seconds", "waste" } } ],
	//     "knowledge": { "lessonSeconds", "observeRadius", "growth", "seed" } }
	// A technology is added or replaced whole by id; "knowledge" replaces the rules. A file without them changes nothing. Values are checked (chances
	// 0..1, seconds above 0); names that point elsewhere (a recipe, a required technology) are checked by CheckReachability, since they may come in a
	// later file. On a mistake it returns why and changes nothing.
	std::string LoadTechs(const std::string& json, TechTree& tree);
	// The tree as the sections read it back (one JSON object with "techs" and "knowledge"; only the discovery numbers that are not the defaults).
	std::string TechsToJson(const TechTree& tree);
}
