#include "Sky.h"
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace GB
{
	glm::vec3 Sky::SunPosition(const SkyDesc& d)
	{
		const float angle = (d.Hours - 6.0f) / 12.0f * glm::pi<float>();   // 0 at sunrise, pi/2 noon, pi sunset
		const float elevation = std::sin(angle) * glm::radians(d.MaxElevationDegrees);
		const float azimuth = glm::radians(d.SunAzimuthDegrees) + std::cos(angle) * glm::radians(70.0f);   // sweeps across the sky
		return glm::normalize(glm::vec3(std::sin(azimuth) * std::cos(elevation), std::sin(elevation), std::cos(azimuth) * std::cos(elevation)));
	}

	SkyState Sky::Compute(const SkyDesc& d, const Environment& base, const DirectionalLight& baseSun)
	{
		SkyState st;
		st.Env = base;
		st.Sun = baseSun;

		const glm::vec3 sunPos = SunPosition(d);
		const float h = sunPos.y;                                            // sin(elevation)
		const float day = glm::smoothstep(-0.08f, 0.18f, h);                // 0 night .. 1 day
		const float twilight = std::exp(-std::pow(h / 0.16f, 2.0f)) * (1.0f - 0.5f * d.Haze);   // glow around the horizon
		st.NightFactor = glm::clamp(1.0f - glm::smoothstep(-0.12f, 0.06f, h), 0.0f, 1.0f);

		const glm::vec3 dayTop(0.14f, 0.30f, 0.62f), dayHorizon(0.64f, 0.74f, 0.88f), dayGround(0.22f, 0.21f, 0.19f);
		const glm::vec3 nightTop(0.004f, 0.008f, 0.028f), nightHorizon(0.018f, 0.026f, 0.055f), nightGround(0.006f, 0.007f, 0.012f);
		const glm::vec3 duskTop(0.10f, 0.13f, 0.34f), duskHorizon(1.0f, 0.46f, 0.20f);

		glm::vec3 top = glm::mix(nightTop, dayTop, day);
		glm::vec3 horizon = glm::mix(nightHorizon, dayHorizon, day);
		top = glm::mix(top, duskTop, twilight * 0.55f);
		horizon = glm::mix(horizon, duskHorizon, twilight * 0.8f);
		horizon = glm::mix(horizon, glm::vec3(0.78f, 0.80f, 0.84f) * (0.2f + 0.8f * day), d.Haze * 0.5f);
		st.Env.SkyTop = top;
		st.Env.SkyHorizon = horizon;
		st.Env.SkyGround = glm::mix(nightGround, dayGround, day) * glm::mix(1.0f, 0.8f, twilight);
		st.Env.Intensity = glm::mix(1.0f, base.Intensity, day);
		st.Env.FogColor = glm::mix(horizon * 0.85f, glm::vec3(0.02f, 0.03f, 0.06f), st.NightFactor * 0.5f);
		st.Env.Stars = st.NightFactor * (1.0f - d.CloudCover * 0.8f);
		st.Env.CloudCover = d.CloudCover;
		st.Env.Exposure = glm::mix(base.Exposure * 1.9f, base.Exposure, glm::smoothstep(-0.1f, 0.3f, h));   // eyes adapt to the dark

		// The directional light: the sun by day, tinted warm near the horizon; the moon at night.
		if (day > 0.02f || st.NightFactor < 0.5f)
		{
			const float low = 1.0f - glm::smoothstep(0.0f, 0.45f, h);
			st.Sun.Color = glm::mix(glm::vec3(1.0f, 0.95f, 0.85f), glm::vec3(1.0f, 0.55f, 0.28f), low);
			st.Sun.Intensity = baseSun.Intensity * glm::smoothstep(-0.04f, 0.22f, h) * (1.0f - 0.3f * d.Haze);
			st.Sun.Direction = -sunPos;
		}
		if (st.NightFactor > 0.0f)
		{
			// Moon: opposite the sun, cool and weak. It fades in as the sun's light fades out.
			const float moonUp = glm::clamp(-h * 3.0f + 0.3f, 0.0f, 1.0f);
			if (day <= 0.02f)
			{
				st.Sun.Color = { 0.55f, 0.66f, 1.0f };
				st.Sun.Intensity = 0.35f * moonUp;
				glm::vec3 moonPos = glm::normalize(glm::vec3(-sunPos.x, std::max(-sunPos.y, 0.25f), -sunPos.z));
				st.Sun.Direction = -moonPos;
			}
		}
		st.Sun.CastShadows = baseSun.CastShadows && st.Sun.Intensity > 0.05f;
		return st;
	}

	void Sky::Apply(const SkyDesc& desc, Environment& env, DirectionalLight& sun)
	{
		SkyState st = Compute(desc, env, sun);
		env = st.Env;
		sun = st.Sun;
	}
}
