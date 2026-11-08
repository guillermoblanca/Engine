#pragma once
#include "GB/Gameplay/AbilitySystemComponent.h"

namespace GB
{
	// The vital state of a body as the gameplay rules see it: the names of the need attributes, the moods those needs drive (effects "Need.*" that
	// scale "Vigor" and grant the tag of the same name) and the "State.Dead" tag. They are the vocabulary the needs (AI/Needs.h) and the fights
	// (Combat) share, so it lives here, below both: combat reads it without depending on the AI (ARQUITECTURA_REVISION.md H7).

	namespace NeedName
	{
		inline constexpr const char* Hunger = "Hunger";
		inline constexpr const char* Thirst = "Thirst";
		inline constexpr const char* Rest = "Rest";
		inline constexpr const char* Warmth = "Warmth";
		inline constexpr const char* Safety = "Safety";
		inline constexpr const char* Company = "Company";
		inline constexpr const char* Health = "Health";
		inline constexpr const char* Vigor = "Vigor";
	}

	// A mood: the effect starts below `On` and ends above `Off` (so it does not flicker), and multiplies Vigor by `Slowdown` while it lasts.
	struct NeedMood
	{
		const char* Effect;
		const char* Need;
		float On, Off;
		float Slowdown;
	};

	// Every mood, in the order they are registered and synchronised.
	inline constexpr NeedMood kNeedMoods[] = {
		{ "Need.Hungry",   NeedName::Hunger,  35.0f, 45.0f, 0.8f },
		{ "Need.Starving", NeedName::Hunger,   5.0f, 12.0f, 0.6f },
		{ "Need.Thirsty",  NeedName::Thirst,  30.0f, 40.0f, 0.8f },
		{ "Need.Tired",    NeedName::Rest,    25.0f, 35.0f, 0.7f },
		{ "Need.Cold",     NeedName::Warmth,  30.0f, 40.0f, 0.85f },
		{ "Need.Afraid",   NeedName::Safety,  30.0f, 40.0f, 0.85f },
		{ "Need.Lonely",   NeedName::Company, 20.0f, 30.0f, 0.9f },
	};

	// The effects "Need.*" in the library.
	void RegisterNeedContent(GameplayLibrary& library);
	bool IsDead(const AbilitySystemComponent& who);
}
