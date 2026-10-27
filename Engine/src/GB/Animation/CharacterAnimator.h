#pragma once
#include "GB/Animation/Locomotion.h"
#include "GB/Audio/FootstepSounds.h"
#include "GB/Game/CharacterMotor.h"

namespace GB
{
	// The animation side of a character: it reads only the MotionState, so it neither knows nor cares whether the
	// motion came from a player, an AI or a script. Drives the locomotion blend, applies the pose to the instance and
	// plays footsteps (and a landing thud).
	class CharacterAnimator
	{
	public:
		CharacterAnimator(const Model& model, ModelInstance& instance, const LocomotionOptions& options = {});

		// Adds the usual Mixamo set when the model has it. Returns how many clips were found.
		int AddDefaultLocomotionClips();
		LocomotionBlender& Locomotion() { return m_Loco; }
		void SetFootsteps(FootstepSounds* steps) { m_Steps = steps; }

		// `characterToWorld`: the character's placement (position and yaw), to put the footsteps where the feet are.
		void Update(const MotionState& state, float dt, const glm::mat4& characterToWorld);

	private:
		const Model& m_Model;
		ModelInstance& m_Instance;
		LocomotionBlender m_Loco;
		FootstepSounds* m_Steps = nullptr;
		int m_LeftFoot = -1, m_RightFoot = -1;
	};
}
