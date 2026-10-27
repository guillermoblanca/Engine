#pragma once
#include "GB/Renderer/Renderer.h"

namespace GB
{
	struct SkyDesc
	{
		float Hours = 12.0f;             // 0..24, 6 = sunrise, 18 = sunset
		float SunAzimuthDegrees = 30.0f; // compass direction of the sun's path
		float MaxElevationDegrees = 62.0f;
		float CloudCover = 0.25f;        // 0..1
		float Haze = 0.0f;               // 0..1: milkier horizon, softer light
	};

	// Time of day: sets the sun (or the moon at night), sky colors, ambient level, fog tint, stars, clouds and
	// exposure. Write the result into Weather::BaseEnvironment()/BaseSun() so weather keeps modulating it.
	struct SkyState
	{
		Environment Env;
		DirectionalLight Sun;
		float NightFactor = 0.0f;        // 0 day .. 1 night (lamps switch on with it)
	};

	class Sky
	{
	public:
		// Starts from `base` so shadow settings, bloom and so on are kept. Call it once on a fresh base (the exposure
		// and intensity are derived from the base values, so applying it twice compounds them).
		static SkyState Compute(const SkyDesc& desc, const Environment& base, const DirectionalLight& baseSun);
		static void Apply(const SkyDesc& desc, Environment& env, DirectionalLight& sun);
		// Direction towards the sun above the horizon (negative y below it).
		static glm::vec3 SunPosition(const SkyDesc& desc);
	};
}
