#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	class Camera
	{
	public:
		glm::vec3 Position{ 0.0f, 2.0f, 8.0f };
		glm::vec3 Target{ 0.0f };
		glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
		float FovYDegrees = 45.0f;
		float NearPlane = 0.1f;
		float FarPlane = 500.0f;

		glm::mat4 GetView() const { return glm::lookAt(Position, Target, Up); }
		glm::mat4 GetProjection(float aspect) const
		{
			return glm::perspective(glm::radians(FovYDegrees), aspect, NearPlane, FarPlane);
		}
	};
}
