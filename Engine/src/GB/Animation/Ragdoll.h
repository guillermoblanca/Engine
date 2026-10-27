#pragma once
#include "GB/Animation/Pose.h"
#include "GB/Animation/Rig.h"
#include "GB/World/Physics.h"
#include <vector>

namespace GB
{
	struct RagdollConfig
	{
		float TotalMass = 75.0f;         // kg for a character of scale 1 (scaled by the cube of the character's size)
		float Friction = 0.8f;
		float Restitution = 0.05f;
		float LinearDamping = 0.08f;
		float AngularDamping = 1.5f;     // limbs flail less than free bodies
		float InertiaScale = 4.0f;       // and slender boxes spin less wildly than their true inertia would let them
		float MaxElbowFlex = 2.5f;       // radians
		float MaxKneeFlex = 2.4f;
		float Density = 1000.0f;         // only used to tell how deep the body sits in water (the mass is set directly)
	};

	// A character as rigid bodies joined by constraints, living in a PhysicsWorld: it collides with the terrain, the static colliders and the
	// other bodies, floats in water, is carried by wind and tornadoes, and is thrown by explosions like anything else in the world. The bodies
	// are placed from the pose the character has when it is activated (so it falls from where it stands, moving as it moves) and, while it is
	// active, the pose of the skeleton is read back from them. Thirteen bodies (pelvis, chest, head, upper arms, forearms, thighs, calves,
	// feet) and twelve joints with limits (hinges at the elbows and knees, cones at the shoulders, hips, neck, spine and ankles).
	class Ragdoll
	{
	public:
		Ragdoll(const Model& model, const HumanoidRig& rig, PhysicsWorld& world, const RagdollConfig& config = {});
		~Ragdoll() { Deactivate(); }
		Ragdoll(const Ragdoll&) = delete;
		Ragdoll& operator=(const Ragdoll&) = delete;

		// The rig needs the arms, the legs, the spine and the head.
		bool Valid() const { return m_Valid; }
		RagdollConfig Config;

		// Creates the bodies and joints for `pose` (model space) of a character placed by `characterToWorld`; `velocity` is the character's own
		// speed (world, m/s). False when the rig is incomplete.
		bool Activate(const Pose& pose, const glm::mat4& characterToWorld, const glm::vec3& velocity);
		void Deactivate();
		bool Active() const { return m_Active; }

		// The pose from the bodies, for a character placed by `characterToWorld` (the placement may follow the pelvis: the pose is
		// relative to it): bones with a body follow it, the others keep the local transforms they have in `pose`.
		void ReadPose(Pose& pose, const glm::mat4& characterToWorld) const;

		// A blow: the body nearest to `worldPoint` takes the impulse (N s).
		void Hit(const glm::vec3& worldPoint, const glm::vec3& impulse);

		glm::vec3 PelvisPosition() const;           // world
		glm::vec3 HeadDirection() const;            // from the pelvis to the head, world, normalized
		bool LyingDown() const;                     // the chest is far from upright
		bool FacingUp() const;                      // the front of the chest points up
		float GetUpYaw() const;                     // the facing the character gets up with (radians, atan2(x, z)): along the way its head points when it lies
		float Speed() const;                        // mean body speed, m/s
		bool AtRest() const;                        // every body is asleep or nearly still
		size_t BodyCount() const { return m_Parts.size(); }
		int Body(size_t part) const { return m_Parts[part].Body; }
		const std::vector<int>& Joints() const { return m_Joints; }   // indices in the physics world

	private:
		struct Part
		{
			int Bone = -1;
			int Body = -1;
			glm::mat4 Offset{ 1.0f };    // the bone's transform in the body's frame
		};
		const Model& m_Model;
		HumanoidRig m_Rig;
		PhysicsWorld& m_World;
		bool m_Valid = false, m_Active = false;
		int m_Group = 0;
		std::vector<Part> m_Parts;
		std::vector<int> m_Joints;
		int m_Pelvis = -1, m_Chest = -1, m_Head = -1;   // indices into m_Parts
	};
}
