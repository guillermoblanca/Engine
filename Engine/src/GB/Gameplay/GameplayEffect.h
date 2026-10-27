#pragma once
#include "GB/Gameplay/AttributeSet.h"
#include <string>
#include <vector>

namespace GB
{
	enum class EffectPolicy
	{
		Instant,    // changes the base of the attributes once; the tags it grants stay until another effect removes them
		Duration,   // lasts `Duration` seconds
		Infinite    // lasts until it is removed
	};

	// What something does to an ability system: change attributes and grant or take away tags. The cost and the cooldown of an ability
	// are effects too (an instant one that spends, a timed one that grants "Cooldown.Ability.X").
	//  * Instant: the modifiers change the base values for good.
	//  * Duration / Infinite: the modifiers count while it lasts (see AttributeSet) and the tags it grants are held while it lasts.
	//  * Period > 0 (with a Duration or Infinite policy): the modifiers are applied as instant changes every `Period` seconds instead
	//    (regrowing fruit, regaining energy), and the tags are still held while it lasts.
	struct GameplayEffectDef
	{
		std::string Name;
		EffectPolicy Policy = EffectPolicy::Instant;
		float Duration = 0.0f;
		float Period = 0.0f;
		std::vector<AttributeModifier> Modifiers;
		std::vector<std::string> GrantedTags;
		std::vector<std::string> RemovedTags;   // taken away (whatever their count) when the effect is applied
		bool Unique = true;                     // applying it again while active refreshes it instead of stacking a second one
	};

	// An effect that is running on an ability system.
	struct ActiveEffect
	{
		int Id = 0;
		GameplayEffectDef Def;
		float Remaining = 0.0f;     // seconds left (Duration policy)
		float PeriodTimer = 0.0f;   // time since the last periodic application
	};

	// The "Cooldown.<ability>" effect: lasts `seconds` and grants the tag "Cooldown.Ability.<ability>".
	GameplayEffectDef MakeCooldownEffect(const std::string& abilityName, float seconds);
	// An instant effect that spends `amount` of an attribute (the shape of a cost).
	GameplayEffectDef MakeCostEffect(const std::string& name, const std::string& attribute, float amount);
}
