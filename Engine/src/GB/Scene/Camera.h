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

		bool Orthographic = false;
		float OrthoHalfHeight = 5.0f;   // half of the visible height in world units (orthographic only)

		glm::mat4 GetView() const { return glm::lookAt(Position, Target, Up); }
		glm::mat4 GetProjection(float aspect) const
		{
			if (Orthographic)
			{
				const float h = OrthoHalfHeight, w = h * aspect;
				return glm::ortho(-w, w, -h, h, NearPlane, FarPlane);
			}
			return glm::perspective(glm::radians(FovYDegrees), aspect, NearPlane, FarPlane);
		}
		// T137: the projection the renderer draws the scene with. Reversed Z with clip-space depth in [0, 1] (glClipControl
		// GL_ZERO_TO_ONE): the near plane at depth 1, the far plane at 0, so a 32-bit float depth buffer keeps its precision far away
		// (a step at 2 km is under a millimetre; 24 bits from 0.1 m had 3 m). The same frustum as GetProjection (near and far swapped,
		// the far plane kept: it still clips). GetProjection stays GL's convention for the CPU: culling planes, picking rays, overlays.
		glm::mat4 GetReversedZProjection(float aspect) const
		{
			if (Orthographic)
			{
				const float h = OrthoHalfHeight, w = h * aspect;
				return glm::orthoRH_ZO(-w, w, -h, h, FarPlane, NearPlane);
			}
			return glm::perspectiveRH_ZO(glm::radians(FovYDegrees), aspect, FarPlane, NearPlane);
		}

		glm::vec3 GetForward() const { return glm::normalize(Target - Position); }

		// Cross-fade between two cameras (t = 0 -> a, t = 1 -> b): position, view direction, lens.
		static Camera Blend(const Camera& a, const Camera& b, float t)
		{
			t = glm::clamp(t, 0.0f, 1.0f);
			Camera r = t < 0.5f ? a : b;   // discrete settings (projection type) switch halfway
			const glm::vec3 dir = glm::normalize(glm::mix(a.GetForward(), b.GetForward(), t));
			r.Position = glm::mix(a.Position, b.Position, t);
			r.Target = r.Position + dir;
			r.Up = glm::normalize(glm::mix(a.Up, b.Up, t));
			r.FovYDegrees = glm::mix(a.FovYDegrees, b.FovYDegrees, t);
			r.NearPlane = glm::mix(a.NearPlane, b.NearPlane, t);
			r.FarPlane = glm::mix(a.FarPlane, b.FarPlane, t);
			r.OrthoHalfHeight = glm::mix(a.OrthoHalfHeight, b.OrthoHalfHeight, t);
			return r;
		}
	};
}
