#pragma once
// T323 (documentation/01_diseno/ARBOL_TECNICO.md §11.2.1, §11.4 and §11.7.5): the steps of the tree of technologies. Every technology has a step
// (TechDef::Tier, 0..4 in the data); a clan opens the steps one after another with the goals of its settlement (KnowledgeRules::Tiers, the
// "tiers" of the "knowledge" section: people, a longhouse, the seasons lived, ...). INVENTING a technology (experimenting, learning it by using
// its materials, the player's research) needs its step open; learning it from someone who knows it (a lesson at the camp, watching, a talk,
// an envoy of another clan, trade) skips the step: contact is what makes a neighbour worth having.
//
// The open step is ClanKnowledge's (it only grows, and it is saved in its "knowledge-clan" section); this class works it out from the tribe and
// keeps the clan's goals within it. The tribe calls Update from one line of Tribe::UpdateGoals and Raise from its clock of lessons. Without steps in the data nothing is held back
// (Open = kNoTierGate), so a tree of a test or a pack without "tiers" works as before. CPU only, deterministic (it reads the tribe's state and clock).
#include "GB/Gameplay/Techs.h"
#include <string>
#include <vector>

namespace GB
{
	class Tribe;

	class TechTiers
	{
	public:
		// ---- for the UI (the tree, the research list) and the commands
		// The clan's open step (0..4), or kNoTierGate (99) when the data has no steps.
		static int Open(const Tribe& tribe);
		// The step `tech` waits for: its tier when it is above the open step, else 0 ("needs tier N" in the tree; 0 = nothing missing).
		static int Missing(const Tribe& tribe, const std::string& tech);
		// "tier 2 open; tier 3: 1/2 goals (people 18/25, seasons 0/1)" ("no tiers" without steps): `game tiers`, GBSim --report.
		static std::string Text(const Tribe& tribe);

		// ---- the rules
		// Whether the tribe meets a goal now, and how far it is (value / target) for Text.
		static bool Met(const Tribe& tribe, const TierGoal& goal, int* value = nullptr);
		// The highest step the tribe's goals reach now (each step needs the one before it); 0 with none met.
		static int Reached(const Tribe& tribe);

		// ---- the tribe's hook (Tribe::UpdateGoals): opens what the goals reach (saying it in the log and the telemetry, kind "tier") and drops the
		// goals of the plan whose step is not open (the clan would gather for an experiment it cannot make).
		static void Update(Tribe& tribe, std::vector<std::string>& goals);
		// Only the opening (the tribe's clock of lessons, every KnowledgeRules::LessonSeconds: a clan with no buildings to plan opens its steps too).
		static void Raise(Tribe& tribe);
		// The same without a plan (the editor's console and the tests, after a building is finished): true when a step opened.
		static bool Refresh(Tribe& tribe);
	};
}
