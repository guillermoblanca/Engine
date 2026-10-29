#pragma once
#include "EnvironmentMap.h"
#include <glm/glm.hpp>

namespace GB
{
	struct Environment
	{
		glm::vec3 SkyTop{ 0.10f, 0.25f, 0.62f };
		glm::vec3 SkyHorizon{ 0.50f, 0.60f, 0.76f };
		glm::vec3 SkyGround{ 0.14f, 0.13f, 0.12f };
		float Intensity = 1.0f;

		glm::vec3 FogColor{ 0.55f, 0.62f, 0.72f };
		float FogDensity = 0.0f;
		float FogHeightFalloff = 0.0f;
		float Wetness = 0.0f;
		float SnowCover = 0.0f;
		const EnvironmentMap* EnvMap = nullptr;
		float Stars = 0.0f;
		float CloudCover = 0.0f;
		float Time = 0.0f;
		glm::vec3 Wind{ 2.0f, 0.0f, 0.8f };

		float Exposure = 1.0f;
		float BloomThreshold = 1.0f;
		float BloomIntensity = 0.5f;
		float BloomRadius = 1.0f;
		float Vignette = 0.25f;

		glm::vec3 ShadowCenter{ 0.0f };
		float ShadowRadius = 12.0f;
	};
}
