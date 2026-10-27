#include "GB/Gameplay/AbilityTasks.h"

namespace GB
{
	void MoveToTargetTask::Begin(AbilityTaskContext& c)
	{
		AbilityRuntime* rt = c.Runtime;
		if (!rt || !rt->Controller || !rt->World || !rt->Resolve) { Succeed(); return; }   // nowhere to walk: the logic runs in place
		InteractionTarget target;
		std::string error;
		if (!rt->Resolve(c.Ability.TargetName, c.Ability.Def->Point, target, &error)) { Fail(error.empty() ? "no interaction point to go to" : error); return; }
		target.Montage.clear();   // the montage is the next task's
		target.Requires.clear();
		target.Gives.clear();
		m_Runner.ActSeconds = 0.0f;
		if (!m_Runner.Start(target, rt->Anim, false)) Fail(m_Runner.Error());
	}

	void MoveToTargetTask::Tick(AbilityTaskContext& c, float dt)
	{
		AbilityRuntime* rt = c.Runtime;
		c.Input = m_Runner.Update(*rt->Controller, *rt->World, rt->Anim, dt);
		if (m_Runner.GetPhase() == InteractionRunner::Phase::Done) Succeed();
		else if (m_Runner.GetPhase() == InteractionRunner::Phase::Failed) Fail(m_Runner.Error());
	}

	void PlayMontageAndWaitTask::Begin(AbilityTaskContext& c)
	{
		m_Montage = c.Ability.Montage;
		m_Time = 0.0f;
		m_Real = false;
		AnimInstance* anim = c.Runtime ? c.Runtime->Anim : nullptr;
		if (anim && !m_Montage.empty() && anim->Montages().Has(m_Montage))
		{
			if (!anim->Montages().Play(m_Montage)) { Fail("could not play '" + m_Montage + "'"); return; }
			m_Real = true;
		}
	}

	void PlayMontageAndWaitTask::Tick(AbilityTaskContext& c, float dt)
	{
		const AbilityDef& def = *c.Ability.Def;
		if (m_Real)
		{
			// the events are those of the animation update that came after the last call: a montage started this frame has none yet
			if (m_Time == 0.0f && dt >= 0.0f) { m_Time = 1e-6f; return; }
			for (const MontageEvent& e : c.Runtime->Anim->Montages().Events())
			{
				if (e.Montage != m_Montage) continue;
				if (e.What == MontageEvent::Kind::Notify) c.Owner.FireNotify(c.Ability, e.Name);
				else if (e.What == MontageEvent::Kind::Finished) { c.Owner.FireUnfired(c.Ability); Succeed(); return; }
				else { Fail("the action was interrupted"); return; }
			}
			return;
		}
		m_Time += dt;
		const float total = def.Duration;
		for (const AbilityEventDef& e : def.Events)
			if (!e.Notify.empty() && e.Time * total <= m_Time + 1e-5f) c.Owner.FireNotify(c.Ability, e.Notify);
		if (m_Time + 1e-5f >= total)
		{
			c.Owner.FireUnfired(c.Ability);
			Succeed();
		}
	}

	void PlayMontageAndWaitTask::Cancel(AbilityTaskContext& c)
	{
		if (m_Real && c.Runtime && c.Runtime->Anim) c.Runtime->Anim->Montages().Stop(m_Montage);
	}

	bool WaitGameplayEventTask::Arrived(const AbilityTaskContext& c) const
	{
		for (const std::string& r : c.Ability.Received)
			if (GameplayTag::TagMatches(m_Tag, r)) return true;
		return false;
	}

	void WaitGameplayEventTask::Begin(AbilityTaskContext& c)
	{
		if (Arrived(c)) Succeed();
	}

	void WaitGameplayEventTask::Tick(AbilityTaskContext& c, float dt)
	{
		if (Arrived(c)) { Succeed(); return; }
		m_Time += dt;
		if (m_Time >= m_Timeout) Fail("no '" + m_Tag + "' event arrived");
	}
}
