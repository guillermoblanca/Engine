#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/AbilityTasks.h"
#include <algorithm>

namespace GB
{
	namespace
	{
		constexpr float kEps = 1e-5f;
		bool Contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
	}

	std::vector<MontageDesc> AbilityMontages(const GameplayLibrary& library, std::vector<MontageDesc> base)
	{
		for (const auto& [name, ability] : library.Abilities())
		{
			std::vector<MontageNotify> notifies;
			for (const AbilityEventDef& e : ability.Events)
				if (!e.Notify.empty()) notifies.push_back({ e.Time, e.Notify });
			if (notifies.empty()) continue;
			for (const std::string& montage : ability.Montages)
				for (MontageDesc& d : base)
				{
					if (d.Name != montage) continue;
					// the montage owns its notifies (the Timeline panel edits them, the JSON files keep them): the times of the ability's events are
					// only the default for a notify the montage does not have yet, never an overwrite of one it already has
					for (const MontageNotify& n : notifies)
					{
						bool has = false;
						for (const MontageNotify& own : d.Notifies) has = has || own.Name == n.Name;
						if (!has) d.Notifies.push_back(n);
					}
				}
		}
		return base;
	}

	AbilityInstance::AbilityInstance() = default;
	AbilityInstance::~AbilityInstance() = default;

	int RegisterAbilityMontages(const GameplayLibrary& library, AnimInstance& anim, const std::vector<MontageDesc>& base)
	{
		int added = 0;
		for (const MontageDesc& d : AbilityMontages(library, base))
			added += anim.Montages().Add(d) ? 1 : 0;
		return added;
	}

	int RegisterAbilityMontages(const GameplayLibrary& library, AnimInstance& anim)
	{
		return RegisterAbilityMontages(library, anim, MixamoMontages());
	}

	AbilitySystemComponent::~AbilitySystemComponent() { CancelAll(); }

	// ---- effects

	bool AbilitySystemComponent::ApplyEffect(const std::string& name)
	{
		const GameplayEffectDef* def = m_Library->Effect(name);
		return def && ApplyEffect(*def);
	}

	bool AbilitySystemComponent::ApplyEffect(const GameplayEffectDef& def)
	{
		for (const std::string& t : def.RemovedTags) m_Tags.RemoveAll(t);
		if (def.Policy == EffectPolicy::Instant)
		{
			for (const AttributeModifier& m : def.Modifiers) m_Attributes.ApplyInstant(m);
			for (const std::string& t : def.GrantedTags) m_Tags.Add(t);
			return true;
		}
		if (def.Unique)
			for (ActiveEffect& e : m_Effects)
				if (e.Def.Name == def.Name)
				{
					e.Remaining = def.Duration;   // applied again: lasts as long as a new one would
					return true;
				}
		ActiveEffect e;
		e.Id = m_NextEffectId++;
		e.Def = def;
		e.Remaining = def.Duration;
		if (def.Period <= 0.0f)
			for (const AttributeModifier& m : def.Modifiers) m_Attributes.AddModifier(e.Id, m);
		for (const std::string& t : def.GrantedTags) m_Tags.Add(t);
		m_Effects.push_back(std::move(e));
		return true;
	}

	void AbilitySystemComponent::Expire(size_t index)
	{
		const ActiveEffect e = m_Effects[index];
		m_Effects.erase(m_Effects.begin() + static_cast<std::ptrdiff_t>(index));
		m_Attributes.RemoveModifiers(e.Id);
		for (const std::string& t : e.Def.GrantedTags) m_Tags.Remove(t);
	}

	int AbilitySystemComponent::RemoveEffect(const std::string& name)
	{
		int removed = 0;
		for (size_t i = 0; i < m_Effects.size();)
			if (m_Effects[i].Def.Name == name) { Expire(i); ++removed; }
			else ++i;
		return removed;
	}

	bool AbilitySystemComponent::EffectActive(const std::string& name) const
	{
		for (const ActiveEffect& e : m_Effects)
			if (e.Def.Name == name) return true;
		return false;
	}

	// ---- abilities

	bool AbilitySystemComponent::Grant(const std::string& ability)
	{
		if (!Def(ability)) return false;
		if (!Contains(m_Granted, ability)) m_Granted.push_back(ability);
		return true;
	}

	bool AbilitySystemComponent::Revoke(const std::string& ability)
	{
		auto it = std::find(m_Granted.begin(), m_Granted.end(), ability);
		if (it == m_Granted.end()) return false;
		Cancel(ability);
		m_Granted.erase(it);
		return true;
	}

	bool AbilitySystemComponent::HasAbility(const std::string& ability) const { return Contains(m_Granted, ability); }

	float AbilitySystemComponent::CooldownRemaining(const std::string& ability) const
	{
		for (const ActiveEffect& e : m_Effects)
			if (e.Def.Name == "Cooldown." + ability) return std::max(0.0f, e.Remaining);
		return 0.0f;
	}

	bool AbilitySystemComponent::IsActive(const std::string& ability) const
	{
		for (const auto& r : m_Running)
			if (!r->Done && r->Def->Name == ability) return true;
		return false;
	}

	std::vector<std::string> AbilitySystemComponent::ActiveAbilities() const
	{
		std::vector<std::string> out;
		for (const auto& r : m_Running)
			if (!r->Done) out.push_back(r->Def->Name);
		return out;
	}

	bool AbilitySystemComponent::CanActivate(const std::string& ability, const AbilitySystemComponent* target, std::string* why) const
	{
		auto no = [&](const std::string& reason) { if (why) *why = reason; return false; };
		const AbilityDef* d = Def(ability);
		if (!d) return no("unknown ability '" + ability + "'");
		if (!HasAbility(ability)) return no("'" + m_Name + "' has not been granted '" + ability + "'");
		if (IsActive(ability)) return no("'" + ability + "' is already running");
		for (const std::string& t : d->RequiredTags)
			if (!m_Tags.Has(t)) return no("needs " + t);
		if (!d->RequiredProp.empty() && !m_Tags.Has("State.Carrying." + d->RequiredProp)) return no("needs to carry the " + d->RequiredProp);
		for (const std::string& t : d->BlockedTags)
			if (m_Tags.Has(t)) return no("blocked by " + t);
		if (m_Tags.Has("Cooldown.Ability." + ability)) return no("cooling down (" + std::to_string(CooldownRemaining(ability)).substr(0, 4) + " s)");
		if (!d->Cost.empty())
		{
			const GameplayEffectDef* cost = m_Library->Effect(d->Cost);
			if (cost && !m_Attributes.CanAfford(cost->Modifiers))
			{
				for (const AttributeModifier& m : cost->Modifiers)
					if (m.Op == ModOp::Add && m.Magnitude < 0.0f && m_Attributes.Base(m.Attribute) + m.Magnitude < m_Attributes.Min(m.Attribute) - kEps)
						return no("not enough " + m.Attribute);
				return no("cannot pay the cost");
			}
		}
		if (d->NeedsTarget)
		{
			if (!target) return no("needs a target");
			for (const std::string& t : d->TargetRequiredTags)
				if (!target->Tags().Has(t)) return no("the target '" + target->Name() + "' needs " + t);
			if (!d->TargetRequiredAnyTags.empty() && !target->Tags().HasAny(d->TargetRequiredAnyTags)) return no("the target '" + target->Name() + "' is not ready (needs " + d->TargetRequiredAnyTags.front() + "...)");
			for (const std::string& t : d->TargetBlockedTags)
				if (target->Tags().Has(t)) return no("the target '" + target->Name() + "' is " + t);
			for (const auto& [attr, minimum] : d->TargetMinAttributes)
				if (target->Attribute(attr) < minimum - kEps) return no("the target '" + target->Name() + "' has no " + attr);
		}
		return true;
	}

	bool AbilitySystemComponent::Activate(const std::string& ability, AbilitySystemComponent* target, std::string* why, const std::string& targetName)
	{
		if (!CanActivate(ability, target, why))
		{
			if (why) m_LastError = ability + ": " + *why;
			return false;
		}
		const AbilityDef* d = Def(ability);
		auto inst = std::make_unique<AbilityInstance>();
		inst->Def = d;
		inst->Target = d->NeedsTarget ? target : nullptr;
		inst->TargetName = !targetName.empty() ? targetName : target ? target->Name() : "";
		inst->Fired.assign(d->Events.size(), false);
		if (!d->Montages.empty())
		{
			const int use = m_Uses[ability]++;
			inst->Montage = d->Montages[static_cast<size_t>(use) % d->Montages.size()];
		}
		// with no character to walk there is no walk: it commits (pays and starts its cooldown) at once
		const bool moves = d->MoveToTarget && d->NeedsTarget && m_Runtime && m_Runtime->Controller && m_Runtime->World && m_Runtime->Resolve;
		if (moves) inst->Tasks.push_back(std::make_unique<MoveToTargetTask>());
		if (!d->Montages.empty()) inst->Tasks.push_back(std::make_unique<PlayMontageAndWaitTask>());
		if (!d->WaitEvent.empty()) inst->Tasks.push_back(std::make_unique<WaitGameplayEventTask>(d->WaitEvent, d->WaitTimeout));

		m_Tags.Add(d->Tag);
		for (const std::string& t : d->ActiveTags) m_Tags.Add(t);
		if (m_Runtime && m_Runtime->Equip)
		{
			if (!d->UnequipProp.empty()) m_Runtime->Equip(d->UnequipProp, false);
			if (!d->EquipProp.empty()) inst->Equipped = m_Runtime->Equip(d->EquipProp, true);
		}
		m_Running.push_back(std::move(inst));
		AbilityInstance& ref = *m_Running.back();
		if (!moves && !Commit(ref)) return true;   // (cannot fail: it was just checked; Commit has ended it if it did)
		if (ref.Tasks.empty() && !ref.Done) Finish(ref, true);
		if (m_Running.back()->Done)
			m_Running.erase(std::remove_if(m_Running.begin(), m_Running.end(), [](const auto& r) { return r->Done; }), m_Running.end());
		return true;
	}

	bool AbilitySystemComponent::Commit(AbilityInstance& inst)
	{
		const AbilityDef& d = *inst.Def;
		const GameplayEffectDef* cost = d.Cost.empty() ? nullptr : m_Library->Effect(d.Cost);
		if (cost && !m_Attributes.CanAfford(cost->Modifiers)) { inst.Error = "cannot pay the cost"; Finish(inst, false); return false; }
		if (m_Tags.Has("Cooldown.Ability." + d.Name)) { inst.Error = "cooling down"; Finish(inst, false); return false; }
		inst.Committed = true;
		if (cost) ApplyEffect(*cost);
		if (d.CooldownSeconds > 0.0f) ApplyEffect(MakeCooldownEffect(d.Name, d.CooldownSeconds));
		for (const std::string& e : d.RemoveEffects) RemoveEffect(e);
		for (const std::string& e : d.ActivationEffects) ApplyEffect(e);
		FireNotify(inst, "");   // events with no notify happen at once
		return true;
	}

	void AbilitySystemComponent::Finish(AbilityInstance& inst, bool ok)
	{
		if (inst.Done) return;
		inst.Done = true;
		const AbilityDef& d = *inst.Def;
		m_Tags.Remove(d.Tag);
		for (const std::string& t : d.ActiveTags) m_Tags.Remove(t);
		if (inst.Equipped && !d.KeepProp && m_Runtime && m_Runtime->Equip) m_Runtime->Equip(d.EquipProp, false);
		if (ok)
		{
			for (const std::string& e : d.EndEffects) ApplyEffect(e);
			++m_Completed;
			m_LastError.clear();
		}
		else
			m_LastError = d.Name + ": " + (inst.Error.empty() ? "cancelled" : inst.Error);
	}

	bool AbilitySystemComponent::Cancel(const std::string& ability)
	{
		bool any = false;
		for (auto& r : m_Running)
		{
			if (r->Done || r->Def->Name != ability) continue;
			if (r->TaskBegun && r->Current < r->Tasks.size())
			{
				AbilityTaskContext ctx{ *this, *r, m_Runtime, m_Input };
				r->Tasks[r->Current]->Cancel(ctx);
			}
			r->Error = "cancelled";
			Finish(*r, false);
			any = true;
		}
		m_Running.erase(std::remove_if(m_Running.begin(), m_Running.end(), [](const auto& r) { return r->Done; }), m_Running.end());
		return any;
	}

	void AbilitySystemComponent::CancelAll()
	{
		for (const std::string& n : ActiveAbilities()) Cancel(n);
	}

	int AbilitySystemComponent::CancelWithTag(const std::string& tag)
	{
		int n = 0;
		for (const std::string& name : ActiveAbilities())
		{
			const AbilityDef* d = Def(name);
			if (d && GameplayTag::TagMatches(tag, d->Tag) && Cancel(name)) ++n;
		}
		return n;
	}

	// ---- events

	int AbilitySystemComponent::AddListener(const std::string& tag, Listener fn)
	{
		m_Listeners.push_back({ m_NextListener, tag, std::move(fn) });
		return m_NextListener++;
	}

	void AbilitySystemComponent::RemoveListener(int id)
	{
		m_Listeners.erase(std::remove_if(m_Listeners.begin(), m_Listeners.end(), [id](const ListenerEntry& l) { return l.Id == id; }), m_Listeners.end());
	}

	void AbilitySystemComponent::SendEvent(const GameplayEventData& event)
	{
		for (auto& r : m_Running)
			if (!r->Done) r->Received.push_back(event.Tag);
		const size_t count = m_Listeners.size();
		for (size_t i = 0; i < count && i < m_Listeners.size(); ++i)
		{
			if (!GameplayTag::TagMatches(m_Listeners[i].Tag, event.Tag)) continue;
			const Listener fn = m_Listeners[i].Fn;   // a copy: the listener may add or remove listeners
			fn(event);
		}
	}

	void AbilitySystemComponent::FireNotify(AbilityInstance& inst, const std::string& notify)
	{
		for (size_t i = 0; i < inst.Def->Events.size(); ++i)
		{
			const AbilityEventDef& ev = inst.Def->Events[i];
			if (inst.Fired[i] || ev.Notify != notify) continue;
			inst.Fired[i] = true;
			for (const std::string& e : ev.SelfEffects) ApplyEffect(e);
			if (inst.Target)
				for (const std::string& e : ev.TargetEffects) inst.Target->ApplyEffect(e);
			if (ev.EventTag.empty()) continue;
			GameplayEventData data{ ev.EventTag, this, inst.Target };
			SendEvent(data);
			if (inst.Target && inst.Target != this) inst.Target->SendEvent(data);
		}
	}

	void AbilitySystemComponent::FireUnfired(AbilityInstance& inst)
	{
		for (size_t i = 0; i < inst.Def->Events.size(); ++i)
			if (!inst.Fired[i]) FireNotify(inst, inst.Def->Events[i].Notify);
	}

	// ---- time

	void AbilitySystemComponent::Update(float dt)
	{
		m_Input = CharacterInput();
		for (size_t i = 0; i < m_Effects.size();)
		{
			ActiveEffect& e = m_Effects[i];
			if (e.Def.Period > 0.0f)
			{
				e.PeriodTimer += dt;
				while (e.PeriodTimer >= e.Def.Period - kEps)
				{
					e.PeriodTimer -= e.Def.Period;
					for (const AttributeModifier& m : e.Def.Modifiers) m_Attributes.ApplyInstant(m);
				}
			}
			if (e.Def.Policy == EffectPolicy::Duration)
			{
				e.Remaining -= dt;
				if (e.Remaining <= kEps) { Expire(i); continue; }
			}
			++i;
		}

		for (size_t i = 0; i < m_Running.size(); ++i)
		{
			AbilityInstance& inst = *m_Running[i];
			if (inst.Done) continue;
			if (inst.Current >= inst.Tasks.size()) { Finish(inst, true); continue; }
			AbilityTaskContext ctx{ *this, inst, m_Runtime, m_Input };
			AbilityTask& task = *inst.Tasks[inst.Current];
			if (!inst.TaskBegun) { task.Begin(ctx); inst.TaskBegun = true; }
			if (task.GetState() == AbilityTask::State::Running) task.Tick(ctx, dt);
			if (task.GetState() == AbilityTask::State::Failed)
			{
				inst.Error = task.Error();
				Finish(inst, false);
			}
			else if (task.GetState() == AbilityTask::State::Succeeded)
			{
				const bool moved = std::string(task.Name()) == "MoveToTarget";
				++inst.Current;
				inst.TaskBegun = false;
				if (moved && !inst.Committed && !Commit(inst)) continue;
				if (inst.Current >= inst.Tasks.size()) Finish(inst, true);
			}
		}
		m_Running.erase(std::remove_if(m_Running.begin(), m_Running.end(), [](const auto& r) { return r->Done; }), m_Running.end());
	}

	// ---- scenes

	GameplayComponent AbilitySystemComponent::ToComponent() const
	{
		GameplayComponent c;
		GameplayTagContainer loose = m_Tags;   // the tags the active effects grant come back with the effects
		for (const ActiveEffect& e : m_Effects)
			for (const std::string& t : e.Def.GrantedTags) loose.Remove(t);
		for (const auto& [name, count] : loose.All())
			for (int i = 0; i < count; ++i) c.Tags.push_back(name);
		for (const AttributeSet::Attribute& a : m_Attributes.All()) c.Attributes.push_back({ a.Name, a.Base, a.Min, a.Max });
		c.Abilities = m_Granted;
		for (const ActiveEffect& e : m_Effects)
			if (e.Def.Policy == EffectPolicy::Infinite) c.Effects.push_back(e.Def.Name);
		return c;
	}

	void AbilitySystemComponent::FromComponent(const GameplayComponent& c)
	{
		CancelAll();
		m_Effects.clear();
		m_Attributes = AttributeSet();
		m_Tags.Clear();
		m_Granted.clear();
		for (const GameplayAttribute& a : c.Attributes) m_Attributes.Define(a.Name, a.Value, a.Min, a.Max);
		for (const std::string& t : c.Tags) m_Tags.Add(t);
		for (const std::string& a : c.Abilities) Grant(a);
		for (const std::string& e : c.Effects) ApplyEffect(e);
	}
}
