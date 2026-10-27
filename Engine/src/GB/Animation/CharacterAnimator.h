#pragma once
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/Character.h"
#include "GB/Game/MotionState.h"

namespace GB
{
	// The animation side of a character: it reads only the MotionState, so it neither knows nor cares whether the
	// motion came from a player, an AI or a script. A thin wrapper over an AnimInstance (locomotion sets with
	// cross-faded overlays) that also plays footsteps and a landing thud.
	class CharacterAnimator
	{
	public:
		CharacterAnimator(const Model& model, ModelInstance& instance, const LocomotionOptions& options = {})
			: m_Anim(model, instance, options) {}

		// Adds the usual Mixamo free-movement set to the default set when the model has it. Returns how many clips were found.
		int AddDefaultLocomotionClips();
		// Fills the default set from the clips a character check found for the idle, walk, run and strafe roles (any skeleton and clip naming).
		int AddRoleClips(const CharacterProfile& profile);
		// Registers the other Mixamo sets (box, holding, wheelbarrow...) the model has clips for; returns their names.
		std::vector<std::string> AddMixamoSets();

		// Registers what the Mixamo packs give besides locomotion: the "UpperBody" slot, the farming actions as montages and the
		// jump clip for the air layer. Returns how many montages the model has clips for.
		int AddMixamoActions();

		AnimInstance& Anim() { return m_Anim; }
		LocomotionBlender& Locomotion() { return m_Anim.Active(); }
		void SetFootsteps(FootstepSounds* steps) { m_Anim.SetFootsteps(steps); }

		// `characterToWorld`: the character's placement (position and yaw), to put the footsteps where the feet are.
		void Update(const MotionState& state, float dt, const glm::mat4& characterToWorld) { m_Anim.Update(state, dt, characterToWorld); }

	private:
		AnimInstance m_Anim;
	};
}
