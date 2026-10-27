#include "GB/Gameplay/GameplayEffect.h"

namespace GB
{
	GameplayEffectDef MakeCooldownEffect(const std::string& abilityName, float seconds)
	{
		GameplayEffectDef e;
		e.Name = "Cooldown." + abilityName;
		e.Policy = EffectPolicy::Duration;
		e.Duration = seconds;
		e.GrantedTags = { "Cooldown.Ability." + abilityName };
		return e;
	}

	GameplayEffectDef MakeCostEffect(const std::string& name, const std::string& attribute, float amount)
	{
		GameplayEffectDef e;
		e.Name = name;
		e.Policy = EffectPolicy::Instant;
		e.Modifiers.push_back({ attribute, ModOp::Add, -amount });
		return e;
	}
}
