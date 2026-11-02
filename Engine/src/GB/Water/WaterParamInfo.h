#pragma once
#include "GB/World/Water.h"
#include <array>

namespace GB
{
	// What a tool needs to edit a WaterParams without knowing its members: the name of each scalar, which member it is
	// and the range that makes sense for it. The editor builds its property panel from this table, so a new parameter
	// is added here once instead of in every UI that shows water.
	struct WaterParamField
	{
		const char* Name;
		float WaterParams::* Member;
		float Min, Max;
	};

	inline constexpr std::array<WaterParamField, 7> kWaterParamFields = { {
		{ "waveHeight", &WaterParams::WaveHeight, 0.0f, 1.5f },
		{ "waveLength", &WaterParams::WaveLength, 1.0f, 40.0f },
		{ "steepness", &WaterParams::Steepness, 0.0f, 1.0f },
		{ "choppiness", &WaterParams::Choppiness, 0.0f, 3.0f },
		{ "clarity", &WaterParams::Clarity, 0.1f, 12.0f },
		{ "foam", &WaterParams::Foam, 0.0f, 2.0f },
		{ "roughness", &WaterParams::Roughness, 0.01f, 0.6f },
	} };
}
