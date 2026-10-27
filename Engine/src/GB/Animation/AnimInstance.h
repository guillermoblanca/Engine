#pragma once
#include "GB/Animation/Locomotion.h"
#include "GB/Audio/FootstepSounds.h"
#include "GB/Game/MotionState.h"
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// A family of clips that move a character in one posture or while carrying one kind of thing: its idle, its walk,
	// its run, its turns. The clips are placed by what they do (their measured root velocity), so any mix works:
	// {idle, walk} for a character carrying a box, {idle, walk, run, strafes} for free movement.
	struct LocomotionSetDesc
	{
		std::string Name;
		std::vector<std::string> Clips;   // clip names in the model
	};

	// The per-character animation state: several locomotion sets (free, carrying a box, pushing a wheelbarrow...) of
	// which one is active. Changing the set (the "overlay") cross-fades the poses; the new set takes over at the same
	// gait phase and velocity, so feet stay in step through the change. It reads only the MotionState.
	class AnimInstance
	{
	public:
		AnimInstance(const Model& model, ModelInstance& instance, const LocomotionOptions& options = {});

		static constexpr const char* kDefaultSet = "default";

		// Adds a set made of the clips the model has (missing ones are skipped). False when none exists.
		bool AddSet(const LocomotionSetDesc& desc);
		// The set called "default" always exists (it starts empty and is the active one): clips can be added to it directly.
		LocomotionBlender& Default() { return *m_Sets[0].Blender; }
		std::vector<std::string> SetNames() const;
		bool HasSet(const std::string& name) const { return Find(name) >= 0; }

		// Makes `name` the active set, cross-fading over `fadeSeconds`. False for an unknown set (nothing changes).
		bool SetOverlay(const std::string& name, float fadeSeconds = 0.3f);
		const std::string& Overlay() const { return m_Sets[static_cast<size_t>(m_Active)].Name; }
		bool Transitioning() const { return m_From != -1; }
		float TransitionProgress() const { return m_From != -1 ? m_Progress : 1.0f; }

		void SetFootsteps(FootstepSounds* steps) { m_Steps = steps; }
		LocomotionBlender& Active() { return *m_Sets[static_cast<size_t>(m_Active)].Blender; }
		const std::vector<FootEvent>& Events() const { return m_Events; }

		// Poses the instance for this frame's motion; `characterToWorld` puts the footsteps where the feet are.
		void Update(const MotionState& state, float dt, const glm::mat4& characterToWorld);

	private:
		struct Set
		{
			std::string Name;
			std::unique_ptr<LocomotionBlender> Blender;
		};
		int Find(const std::string& name) const;

		const Model& m_Model;
		ModelInstance& m_Instance;
		LocomotionOptions m_Options;
		std::vector<Set> m_Sets;
		int m_Active = 0, m_From = -1;   // m_From: the set being left, -1 when not changing, -2 when leaving a frozen pose
		Pose m_FromSnapshot;
		float m_Progress = 1.0f, m_FadeSeconds = 0.3f;
		FootstepSounds* m_Steps = nullptr;
		int m_LeftFoot = -1, m_RightFoot = -1;
		Pose m_Pose;
		std::vector<FootEvent> m_Events;
	};

	// The sets the Mixamo locomotion and farming packs make (when the model has the clips): "free", "box", "holding",
	// "wheelbarrow". Add them with AnimInstance::AddSet.
	std::vector<LocomotionSetDesc> MixamoSets();
}
