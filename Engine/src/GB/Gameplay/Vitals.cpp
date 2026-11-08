#include "GB/Gameplay/Vitals.h"

namespace GB
{
	void RegisterNeedContent(GameplayLibrary& library)
	{
		for (const NeedMood& m : kNeedMoods)
		{
			GameplayEffectDef e;
			e.Name = m.Effect;
			e.Policy = EffectPolicy::Infinite;
			e.Modifiers = { { NeedName::Vigor, ModOp::Multiply, m.Slowdown } };
			e.GrantedTags = { m.Effect };
			library.AddEffect(e);
		}
	}

	bool IsDead(const AbilitySystemComponent& who) { return who.HasTag("State.Dead"); }
}
