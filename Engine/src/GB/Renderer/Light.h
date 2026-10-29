#pragma once
#include <glm/glm.hpp>

namespace GB
{
	struct DirectionalLight
	{
		glm::vec3 Direction{ -0.5f, -1.0f, -0.35f };
		glm::vec3 Color{ 1.0f, 0.94f, 0.82f };
		float Intensity = 4.0f;
		bool CastShadows = true;
	};

	struct PointLight
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Color{ 1.0f };
		float Intensity = 20.0f;
		float Radius = 0.0f;
	};
}
