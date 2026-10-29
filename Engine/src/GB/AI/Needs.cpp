#include "GB/AI/Needs.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		constexpr const char* kDeathPrefix = "Death.";

		struct Mood
		{
			const char* Effect;
			const char* Need;
			float On, Off;       // the effect starts below `On` and ends above `Off` (so it does not flicker)
			float Slowdown;      // Vigor multiplier
		};
		const Mood kMoods[] = {
			{ "Need.Hungry",   NeedName::Hunger,  35.0f, 45.0f, 0.8f },
			{ "Need.Starving", NeedName::Hunger,   5.0f, 12.0f, 0.6f },
			{ "Need.Thirsty",  NeedName::Thirst,  30.0f, 40.0f, 0.8f },
			{ "Need.Tired",    NeedName::Rest,    25.0f, 35.0f, 0.7f },
			{ "Need.Cold",     NeedName::Warmth,  30.0f, 40.0f, 0.85f },
			{ "Need.Afraid",   NeedName::Safety,  30.0f, 40.0f, 0.85f },
			{ "Need.Lonely",   NeedName::Company, 20.0f, 30.0f, 0.9f },
		};

		float Chill(Season s)
		{
			switch (s)
			{
			case Season::Summer: return -0.05f;   // warms up by itself
			case Season::Spring: return 0.0f;
			case Season::Autumn: return 0.015f;
			default: return 0.06f;
			}
		}

		void Move(AttributeSet& a, const char* name, float delta) { a.SetBase(name, a.Base(name) + delta); }

		void SyncMoods(AbilitySystemComponent& who)
		{
			for (const Mood& m : kMoods)
			{
				const float level = who.Attribute(m.Need);
				const bool active = who.EffectActive(m.Effect);
				if (!active && level < m.On) who.ApplyEffect(m.Effect);
				else if (active && level > m.Off) who.RemoveEffect(m.Effect);
			}
		}
	}

	float HourOfDay(const NeedsConfig& cfg, double seconds)
	{
		const double day = std::max(1.0, static_cast<double>(cfg.DaySeconds));
		const double h = std::fmod(static_cast<double>(cfg.StartHour) + seconds / day * 24.0, 24.0);
		return static_cast<float>(h < 0.0 ? h + 24.0 : h);
	}

	bool IsNight(const NeedsConfig& cfg, float hour)
	{
		return cfg.NightFrom > cfg.NightTo ? (hour >= cfg.NightFrom || hour < cfg.NightTo) : (hour >= cfg.NightFrom && hour < cfg.NightTo);
	}

	void DefineNeeds(AbilitySystemComponent& who)
	{
		AttributeSet& a = who.Attributes();
		for (const char* n : { NeedName::Hunger, NeedName::Thirst, NeedName::Rest, NeedName::Warmth, NeedName::Safety, NeedName::Company, NeedName::Health })
			if (!a.Has(n)) a.Define(n, 100.0f, 0.0f, 100.0f);
		if (!a.Has(NeedName::Vigor)) a.Define(NeedName::Vigor, 1.0f, 0.0f, 1.0f);
	}

	void RegisterNeedContent(GameplayLibrary& library)
	{
		for (const Mood& m : kMoods)
		{
			GameplayEffectDef e;
			e.Name = m.Effect;
			e.Policy = EffectPolicy::Infinite;
			e.Modifiers = { { NeedName::Vigor, ModOp::Multiply, m.Slowdown } };
			e.GrantedTags = { m.Effect };
			library.AddEffect(e);
		}
	}

	void SatisfyNeed(AbilitySystemComponent& who, const char* need, float amount)
	{
		DefineNeeds(who);
		Move(who.Attributes(), need, amount);
		SyncMoods(who);
	}

	bool IsDead(const AbilitySystemComponent& who) { return who.HasTag("State.Dead"); }

	std::string CauseOfDeath(const AbilitySystemComponent& who)
	{
		if (!IsDead(who)) return {};
		for (const char* n : { NeedName::Hunger, NeedName::Thirst, NeedName::Warmth, "Wounds" })   // ("Wounds": killed in a fight, H14)
			if (who.HasTag(std::string(kDeathPrefix) + n)) return n;
		return "Unknown";
	}

	void UpdateNeeds(AbilitySystemComponent& who, const NeedsConfig& cfg, const NeedContext& ctx, float dt)
	{
		if (IsDead(who)) return;
		DefineNeeds(who);
		AttributeSet& a = who.Attributes();
		const bool night = IsNight(cfg, ctx.Hour);

		const float winter = ctx.Time == Season::Winter ? 1.2f : ctx.Time == Season::Autumn ? 1.05f : 1.0f;
		const float summer = ctx.Time == Season::Summer ? 1.3f : 1.0f;
		Move(a, NeedName::Hunger, -dt * 100.0f / cfg.HungerSeconds * winter * (ctx.Sleeping ? 0.5f : 1.0f));
		Move(a, NeedName::Thirst, -dt * 100.0f / cfg.ThirstSeconds * summer * (ctx.Sleeping ? 0.5f : 1.0f));
		Move(a, NeedName::Rest, ctx.Sleeping ? dt * cfg.SleepGain : -dt * 100.0f / cfg.RestSeconds);

		float chill = Chill(ctx.Time) + (night ? 0.02f : 0.0f);
		if (ctx.Sleeping) chill *= 0.5f;
		if (ctx.NearFire) Move(a, NeedName::Warmth, dt * cfg.WarmGain);
		else if (chill > 0.0f) Move(a, NeedName::Warmth, -dt * chill);
		else Move(a, NeedName::Warmth, dt * (0.1f - chill));   // a mild day: it recovers

		Move(a, NeedName::Safety, dt * (ctx.WithOthers || ctx.Sheltered ? 0.4f : 0.05f));
		Move(a, NeedName::Company, ctx.WithOthers ? dt * 0.3f : -dt * 0.02f);

		float damage = 0.0f;
		const char* cause = nullptr;
		float worst = 0.0f;
		auto hurt = [&](const char* need, float rate)
		{
			if (a.Base(need) > 0.0f) return;
			damage += rate;
			if (rate > worst) { worst = rate; cause = need; }
		};
		hurt(NeedName::Hunger, cfg.StarveDamage);
		hurt(NeedName::Thirst, cfg.ThirstDamage);
		hurt(NeedName::Warmth, cfg.ColdDamage);
		if (damage > 0.0f) Move(a, NeedName::Health, -damage * dt);
		else if (a.Base(NeedName::Hunger) > 40.0f && a.Base(NeedName::Thirst) > 40.0f) Move(a, NeedName::Health, cfg.HealPerSecond * dt);

		SyncMoods(who);
		if (a.Base(NeedName::Health) <= 0.0f)
		{
			who.Tags().Add("State.Dead");
			who.Tags().Add(std::string(kDeathPrefix) + (cause ? cause : "Unknown"));
			who.CancelAll();
		}
	}
}
