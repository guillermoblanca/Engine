#pragma once
// T331 (documentation/01_diseno/ARBOL_TECNICO.md §11.7.7 and the column «Espera» of §11.8): the generic modifiers of a technology. A technology
// may say in the data how it changes a number the simulation already has ("modifiers": { "hunt": 1.25 }; TechDef::Modifiers, checked against
// TechModifierKeys() by LoadTechs). A clan that knows it (ClanKnowledge::Clan: what its members told it at the camp, and the start) has that
// number multiplied; several technologies with the same key multiply each other. Nothing here is per technology: the data says who changes what.
//
// The keys and where each is read (one line each, at a point that existed before):
//   hunt   the meat a kill gives (HuntRule::Units, Tribe::ConflictEvents), rounded to the nearest unit (a knife: more meat per animal)
//   heal   how fast wounds heal (Combatant::Heal, with the healer's hut; Tribe::ConflictTurn)
//   grief  the grief a death brings to kin and friends (Tribe::SocialDied): < 1 is a burial that eases it
//   mood   where everyone's mood returns to with the days (MoodRules::Rest, Tribe::SocialDay): > 1 is art, ornaments
//   night  the work done at night: a part of the agenda that holds a work back at night (factor < 1) is multiplied by it, never above 1 (a lamp;
//          Tribe::AgendaFactor). Sleep (> 1) is never touched.
//   spoil  how fast the food in the clan's store and its houses' pantries goes off (Tribe::StepEconomy): < 1 is glazed jars
//   carry  the units one trip carries to a building site (TribeConfig::HaulAt, Tribe::SiteWork and the haul jobs): a sledge, a wheel
//
// Without a tree, or with no technology that names a key, every factor is 1, so a run is as before. Stateless (what the clan knows decides;
// nothing is saved), CPU only, deterministic (the tree in id order).
#include <set>
#include <string>

namespace GB
{
	class Tribe;
	struct TechTree;

	class TechModifiers
	{
	public:
		// The product of `key` over the technologies of `tree` in `known` (1: none, or no tree).
		static float Of(const TechTree* tree, const std::set<std::string>& known, const std::string& key);
		// The same for what the clan of `tribe` knows.
		static float Of(const Tribe& tribe, const std::string& key);

		// ---- what the hooks apply (pure, so a test reads the effect)
		// Units of a kill with the factor: round(units x f), at least 1 when units > 0.
		static int Units(int units, float factor);
		// A night factor of the agenda with the factor: work held back (< 1) is multiplied, up to 1; anything else is as it was.
		static float Night(float agenda, float factor);

		// ---- the tribe's hooks
		static int Hunt(const Tribe& tribe, int units) { return Units(units, Of(tribe, "hunt")); }
		static int Carry(const Tribe& tribe, int units) { return Units(units, Of(tribe, "carry")); }
		// A factor of the agenda at night (the tree is only read for work held back).
		static float AtNight(const Tribe& tribe, float agenda) { return agenda < 1.0f ? Night(agenda, Of(tribe, "night")) : agenda; }
	};
}
