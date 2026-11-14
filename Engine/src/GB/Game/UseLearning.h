#pragma once
#include "GB/Game/Knowledge.h"
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;
	struct BuildingDef;

	// T181: there are no eras. Every technology of the content is in one tree, and a clan goes as far as what it finds lets it go: knowledge is won
	// by USING a material (gathering it, making something of it, building with it), CURIOSITY speeds it, and the world is the only hard gate. All the
	// numbers are data: KnowledgeRules::Use and ::Leap, TechDef::Physical (GB/Gameplay/Techs.h) and the society's effects; none is written here.
	//
	// - PHYSICAL GATE: a technology can be learnt (by any way) only when every material of its TechDef::Physical has been found by the clan and one of
	//   its stations is there (smelting: copper ore, and a kiln or a forge). ClanKnowledge asks it through SetPhysical. What a technology `requires`
	//   is no gate: it speeds or slows the learning (KnowledgeRules::PrereqBonus, MissingPrereqFactor).
	// - USE: a member who works a node, makes a recipe or builds a phase handles materials (the node's item, the recipe's inputs, the phase's needs).
	//   Each material leads to technologies (Use.Materials: "clay" -> pottery, "grain_seed" -> tillage, "copper_ore" -> smelting): the member gains
	//   that much understanding (ClanKnowledge::Instruct) and shows the clan at the camp as with any other way of learning (ClanKnowledge::Share).
	// - CURIOSITY: the society's factor on the option Use.Effect ("learn": the trait curiosity and the norm inquiry) scales the progress of a use and
	//   of a failed experiment.
	// - SURVIVAL FIRST, a priority and not a block: while the basics are not covered (food in the store, a fire, beds) the experiments are worth
	//   Use.Damp of what they would be, times the member's curiosity: a very curious member may still sit down to try, at the cost of its meal or rest.
	// - FOUND: what the clan has at hand (the store, the bags) or knows where to get (the nodes its spatial memory holds). It pulls the clan towards
	//   the technologies its materials lead to (the next step of Tribe::UpdateGoals).
	//
	// Plain functions over a tribe (a friend of Tribe): no state of their own, so nothing new is saved (the understanding is ClanKnowledge's).
	class UseLearning
	{
	public:
		// `m` made `recipe` and/or worked the node of kind `node` ("" = none): what it learnt by using their materials (only the "learns" lines).
		static std::vector<KnowledgeEvent> Used(Tribe& t, const TribeMember& m, const std::string& recipe, const std::string& node);
		// `m` worked phase `phase` of a building: the materials of that phase.
		static std::vector<KnowledgeEvent> Built(Tribe& t, const TribeMember& m, const BuildingDef& def, int phase);
		// The same for any list of materials (the core of both).
		static std::vector<KnowledgeEvent> Handled(Tribe& t, const TribeMember& m, const std::vector<std::string>& items);

		// The world lets the clan learn `tech`: its physical materials found and one of its stations there.
		static bool Physical(const Tribe& t, const std::string& tech);
		// Forget the answers of Physical worked out so far (they are kept per tribe and instant): a new tree, or a tribe built where another was.
		static void Forget();
		// Hooks the tribe's ClanKnowledge to the world (Physical) and to its people (Intellect), forgetting what was worked out: Tribe::SetTechs.
		static void Bind(Tribe& t);
		// T181 (2): `name`'s intellect: the society's gene KnowledgeRules::IntellectGene (a member without it, as in an older save, has the gene's
		// mean). 1 when the society has no such gene: every idea is within reach, as before.
		static float Intellect(const Tribe& t, const std::string& name);
		// The basics are covered (Use.Basics).
		static bool Basics(const Tribe& t);
		// How fast `m` learns by use and by experimenting: the society's factor on Use.Effect (its curiosity, the clan's inquiry), within
		// Use.PaceMin..PaceMax. Curiosity says how much one tries; the intellect, how far one gets (Intellect).
		static float Pace(const Tribe& t, const TribeMember& m);
		// What an experiment is worth to `m` now: 1 with the basics covered, else Use.Damp x its curiosity (Pace).
		static float Room(const Tribe& t, const TribeMember& m);

		// How much of `item` the clan has at hand or knows of (the store, the bags, and the nodes it remembers: the nearer the camp, the more).
		static float Have(const Tribe& t, const std::string& item);
		// The next step the clan reaches for on its own: the first unknown of the knowledge rules' progression (the basics), or, once the basics are
		// covered, the technology its found materials pull hardest (Use.Pull + 2 x how much of them it has). "" = none.
		static std::string NextStep(const Tribe& t);
		// The years the world leaps between two lives (KnowledgeRules::LeapYears, one value of the data; 0 = none).
		static float LeapYears(const Tribe& t);
		// A line of knowledge was said (Tribe::Tell): when it is a technology learnt, the telemetry gets a "learn" event with the requirements the
		// learner lacked ("skipped: a, b") and whether the physical gate held ("physical: ok" / "physical: MISSING", which must never happen), and
		// the learner's intellect against the technology's complexity ("intellect: 0.62; complexity: 0.40").
		static void Noted(Tribe& t, const KnowledgeEvent& e);
	};
}
