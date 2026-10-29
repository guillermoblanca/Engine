#pragma once
#include <glm/glm.hpp>

namespace GB
{
	enum class Tonemap { ACES, Neutral, Filmic };

	struct PostSettings
	{
		Tonemap Mapper = Tonemap::ACES;
		float SSAO = 0.0f;
		float SSAORadius = 0.7f;
		float SSAOBias = 0.04f;
		float DepthOfField = 0.0f;
		float FocusDistance = 12.0f;
		float FocusRange = 8.0f;
		bool AutoExposure = false;
		float AdaptSpeed = 1.2f;
		float ExposureKey = 0.18f;
		float MinExposure = 0.25f, MaxExposure = 6.0f;
		float DeltaTime = 1.0f / 60.0f;
		float Saturation = 1.0f;
		float Contrast = 1.0f;
		glm::vec3 Tint{ 1.0f };
		float ChromaticAberration = 0.0f;
		float Grain = 0.0f;
	};
}
