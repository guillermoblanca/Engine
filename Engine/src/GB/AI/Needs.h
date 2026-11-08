#pragma once
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Vitals.h"
#include <string>

namespace GB
{
	// The needs of an agent (H6): hunger, thirst, rest, warmth, safety and company. Each is an attribute of its ability system, 0..100 where 100 is
	// satisfied, so they are saved and loaded with the rest of the gameplay state and anything that speaks attributes (abilities, effects, the
	// inspector) can read them. They fall with time at rates that depend on the season and the hour, rise when the agent eats, drinks, sleeps, warms
	// up or keeps company, and when one reaches zero "Health" drains; at zero Health the agent dies ("State.Dead" and "Death.<Need>").
	// Low needs also slow the agent down: effects "Need.Hungry", "Need.Thirsty", "Need.Tired", "Need.Cold", "Need.Afraid", "Need.Lonely"
	// (and "Need.Starving") scale the attribute "Vigor" (1 = at full strength) and grant the tag of the same name.
	// Deterministic: no randomness, only the arguments.

	// NeedName, the moods, RegisterNeedContent and IsDead are in Gameplay/Vitals.h (combat shares them).

	struct NeedsConfig
	{
		float DaySeconds = 1200.0f;          // seconds of simulation in a day (the clock's default)
		float StartHour = 8.0f;              // hour of the day at simulation second 0
		float HungerSeconds = 900.0f;        // from full to empty awake in spring
		float ThirstSeconds = 1500.0f;
		float RestSeconds = 1000.0f;
		float SleepGain = 0.4f;              // rest per second while asleep
		float WarmGain = 0.5f;               // warmth per second beside a lit fire
		float StarveDamage = 0.33f;          // health per second with nothing to eat
		float ThirstDamage = 0.45f;
		float ColdDamage = 0.04f;
		float HealPerSecond = 0.08f;
		float NightFrom = 21.0f, NightTo = 6.0f;
		int FoodHunger = 25;                 // what a berry gives back
		int FoodThirst = 6;
		int WaterThirst = 35;                // what a unit of water gives back
	};

	// What the agent is doing and where it is, for this step.
	struct NeedContext
	{
		Season Time = Season::Spring;
		float Hour = 12.0f;
		bool Sleeping = false;
		bool NearFire = false;     // beside a lit fire
		bool WithOthers = false;   // someone of the clan is close
		bool Sheltered = false;    // at the camp
	};

	// Hour of the day (0..24) at simulation time `seconds`.
	float HourOfDay(const NeedsConfig& cfg, double seconds);
	bool IsNight(const NeedsConfig& cfg, float hour);

	// Defines the need attributes on an ability system (those it already has are left as they are).
	void DefineNeeds(AbilitySystemComponent& who);
	// Moves the needs forward by `dt` seconds. Does nothing to a dead agent.
	void UpdateNeeds(AbilitySystemComponent& who, const NeedsConfig& cfg, const NeedContext& ctx, float dt);

	// Adds to (or takes from) a need, clamped to 0..100.
	void SatisfyNeed(AbilitySystemComponent& who, const char* need, float amount);
	// "Hunger", "Thirst", "Warmth" once dead, "" while alive.
	std::string CauseOfDeath(const AbilitySystemComponent& who);
}
