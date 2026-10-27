#pragma once
#include "GB/Renderer/Camera.h"
#include "GB/World/Physics.h"
#include <glm/glm.hpp>

namespace GB
{
	struct FollowCameraConfig
	{
		float Distance = 4.5f;
		float MinDistance = 1.2f, MaxDistance = 12.0f;
		float PivotHeight = 1.5f;            // above the character's feet
		float ShoulderOffset = 0.0f;         // sideways shift of the pivot (over-the-shoulder; positive = right)
		float MinPitchDegrees = -15.0f, MaxPitchDegrees = 70.0f;   // positive looks down
		float FollowTime = 0.06f;            // seconds to catch up with the character (0 = rigid)
		float RecoverTime = 0.25f;           // seconds to move back out after a wall pushed the camera in
		float CollisionRadius = 0.25f;
		float FovDegrees = 50.0f;
	};

	// Third-person camera: orbits the character, never goes through walls or the ground (the world is asked for a ray
	// from the character to where the camera wants to be), eases back out when the obstacle is gone.
	class FollowCamera
	{
	public:
		explicit FollowCamera(const FollowCameraConfig& config = {}) : m_Config(config), m_Distance(config.Distance), m_Wanted(config.Distance) {}

		// The camera looks along (sin yaw, 0, cos yaw) on the ground, so yaw = the character's yaw looks the way it faces.
		float Yaw = 0.0f;
		float Pitch = glm::radians(15.0f);   // positive looks down

		void Rotate(float deltaYaw, float deltaPitch);
		void Zoom(float factor);              // > 1 moves away
		void Snap(const glm::vec3& feet);     // jump to the character without easing
		// Follows `feet` (the character's position at its feet); `world` (optional) keeps the camera out of walls.
		void Update(const glm::vec3& feet, float dt, const PhysicsWorld* world = nullptr);

		const Camera& GetCamera() const { return m_Camera; }
		float CurrentDistance() const { return m_Distance; }
		FollowCameraConfig& Config() { return m_Config; }

	private:
		FollowCameraConfig m_Config;
		Camera m_Camera;
		glm::vec3 m_Pivot{ 0.0f };
		bool m_Snapped = false;
		float m_Distance, m_Wanted;
	};
}
