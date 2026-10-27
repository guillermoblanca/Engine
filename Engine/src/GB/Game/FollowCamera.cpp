#include "GB/Game/FollowCamera.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	void FollowCamera::Rotate(float deltaYaw, float deltaPitch)
	{
		Yaw += deltaYaw;
		Pitch = std::clamp(Pitch + deltaPitch, glm::radians(m_Config.MinPitchDegrees), glm::radians(m_Config.MaxPitchDegrees));
	}

	void FollowCamera::Zoom(float factor)
	{
		m_Wanted = std::clamp(m_Wanted * factor, m_Config.MinDistance, m_Config.MaxDistance);
	}

	void FollowCamera::Snap(const glm::vec3& feet)
	{
		m_Pivot = feet;
		m_Snapped = true;
	}

	void FollowCamera::Update(const glm::vec3& feet, float dt, const PhysicsWorld* world)
	{
		if (!m_Snapped) Snap(feet);
		const float follow = m_Config.FollowTime > 1e-4f ? 1.0f - std::exp(-dt / m_Config.FollowTime) : 1.0f;
		m_Pivot += (feet - m_Pivot) * follow;

		const glm::vec3 look(std::sin(Yaw) * std::cos(Pitch), -std::sin(Pitch), std::cos(Yaw) * std::cos(Pitch));
		const glm::vec3 right(-std::cos(Yaw), 0.0f, std::sin(Yaw));
		const glm::vec3 pivot = m_Pivot + glm::vec3(0.0f, m_Config.PivotHeight, 0.0f) + right * m_Config.ShoulderOffset;

		// the nearest obstacle between the character and where the camera wants to be
		float allowed = m_Wanted;
		if (world)
		{
			const RayHit hit = world->Raycast(pivot, -look, m_Wanted + m_Config.CollisionRadius);
			if (hit.Hit) allowed = std::max(hit.Distance - m_Config.CollisionRadius, 0.3f);
		}
		// closer at once (never see through a wall), back out smoothly
		if (allowed < m_Distance) m_Distance = allowed;
		else
		{
			const float recover = m_Config.RecoverTime > 1e-4f ? 1.0f - std::exp(-dt / m_Config.RecoverTime) : 1.0f;
			m_Distance += (allowed - m_Distance) * recover;
		}

		glm::vec3 position = pivot - look * m_Distance;
		if (world)
		{
			const float floorY = world->GroundHeight(position.x, position.z) + 0.3f;   // not under the terrain
			if (position.y < floorY) position.y = floorY;
		}
		m_Camera.Position = position;
		m_Camera.Target = pivot;
		m_Camera.Up = { 0.0f, 1.0f, 0.0f };
		m_Camera.FovYDegrees = m_Config.FovDegrees;
	}
}
