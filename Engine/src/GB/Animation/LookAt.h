#pragma once
#include "GB/Animation/IK.h"
#include "GB/Animation/Rig.h"

namespace GB
{
	// Turns the head (and a little of the neck and chest) toward a world point, within limits, smoothly. The character's own head
	// motion from the clips is kept and corrected: what is applied is the rotation that takes the head's current forward to the target.
	struct LookAtConfig
	{
		float MaxYaw = 1.2f;          // radians each side of the character's forward (about 70 degrees)
		float MaxPitch = 0.7f;        // radians up or down
		float Sharpness = 8.0f;       // 1/s: how fast the gaze follows the target
		float HeadShare = 0.5f, NeckShare = 0.3f, ChestShare = 0.2f;   // how the turn is spread (normalized)
	};

	class LookAt
	{
	public:
		LookAt(const Model& model, const HumanoidRig& rig);

		LookAtConfig Config;
		void SetTarget(const glm::vec3& worldPoint) { m_Target = worldPoint; m_HasTarget = true; }
		void ClearTarget() { m_HasTarget = false; }
		bool HasTarget() const { return m_HasTarget; }

		// Adjusts `pose` for a character placed by `characterToWorld`. `weight` fades the effect (also eased in and out by itself when the
		// target is set or cleared).
		void Update(Pose& pose, const glm::mat4& characterToWorld, float weight, float dt);

		float Yaw() const { return m_Yaw; }       // radians the gaze is turned from the character's forward (positive = to its left)
		float Pitch() const { return m_Pitch; }   // positive = up
		float Strength() const { return m_Strength; }

	private:
		const Model& m_Model;
		HumanoidRig m_Rig;
		glm::quat m_RestHead{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 m_Target{ 0.0f };
		bool m_HasTarget = false;
		float m_Yaw = 0.0f, m_Pitch = 0.0f, m_Strength = 0.0f;
		std::vector<glm::mat4> m_World;
	};
}
