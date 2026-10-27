#pragma once
#include "GB/Animation/IK.h"
#include "GB/Animation/Rig.h"
#include <functional>

namespace GB
{
	// Puts the feet on uneven ground. Each frame, for each foot: the terrain height under it (relative to the height under the character)
	// is how far that foot has to move up or down; the pelvis drops to the lowest foot (never rises) so the legs can reach, and each leg is
	// solved with two-bone IK to its foot's height, with the sole tilted toward the ground's slope. Offsets are smoothed, so stepping over an
	// edge is a quick settle, not a pop. The clips keep deciding where the feet go on the ground plane.
	struct FootIKConfig
	{
		float Sharpness = 14.0f;      // 1/s: how fast the offsets follow the terrain
		float MaxOffset = 0.5f;       // meters (world) a foot may be moved from the clip
		float MaxTilt = 0.6f;         // radians the sole may tilt toward the slope
		float TiltWeight = 0.85f;
		float NormalSample = 0.15f;   // meters: spacing of the height samples that give the slope
	};

	class FootIK
	{
	public:
		FootIK(const Model& model, const HumanoidRig& rig) : m_Model(model), m_Rig(rig) {}

		FootIKConfig Config;
		// Terrain height at world x, z. Without it (or without legs in the rig) nothing happens.
		void SetGround(std::function<float(float, float)> ground) { m_Ground = std::move(ground); m_Started = false; }
		bool HasGround() const { return static_cast<bool>(m_Ground); }

		// Adjusts `pose` (model space) for a character placed by `characterToWorld` (its position y is the ground it stands on).
		// `weight` 0..1 fades the whole effect (airborne, in an action).
		void Update(Pose& pose, const glm::mat4& characterToWorld, float weight, float dt);

		float PelvisOffset() const { return m_Pelvis; }                      // meters (world), <= 0
		float FootOffset(bool left) const { return m_Foot[left ? 0 : 1]; }   // meters (world), relative to the clip

	private:
		const Model& m_Model;
		HumanoidRig m_Rig;
		std::function<float(float, float)> m_Ground;
		bool m_Started = false;
		float m_Pelvis = 0.0f, m_Foot[2] = { 0.0f, 0.0f };
		std::vector<glm::mat4> m_World;
	};
}
