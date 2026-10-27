#pragma once
#include "GB/Animation/BlendSpace.h"
#include "GB/Animation/Pose.h"
#include "GB/Assets/ModelInstance.h"
#include <string>
#include <vector>

namespace GB
{
	// A foot landing in the blended walk/run cycle: the moment for a footstep sound, dust, a ground mark.
	struct FootEvent
	{
		bool Left = true;
		float Weight = 1.0f;     // how much of the pose is gait (idle blends have little)
	};

	struct LocomotionOptions
	{
		ClipAnalysisConfig Analysis;        // bone names (Mixamo by default)
		bool StripRootMotion = true;        // the controller moves the character: take the clips' own forward travel out
		float MinSpeedScale = 0.6f;         // limits of the playback speed factor that matches feet to the ground
		float MaxSpeedScale = 1.5f;
		float VelocitySmoothing = 0.08f;    // seconds: how fast the blend follows the commanded velocity
		float MinGaitWeight = 0.5f;         // foot events fire only above this share of gait in the blend
	};

	// Walk/run/strafe locomotion from a set of clips: a 2D blend space placed by each clip's measured root velocity,
	// one shared cycle phase aligned on the left foot landing (so feet never cross while blending), playback speed
	// scaled to the commanded speed (no foot sliding between the clips' own speeds) and foot events.
	// The controller moves the character; this only poses it.
	class LocomotionBlender
	{
	public:
		explicit LocomotionBlender(const Model& model, const LocomotionOptions& options = {});

		// Adds a clip, placed at its measured root velocity. False when the clip does not exist or has no root bone.
		bool AddClip(const std::string& name);
		bool AddClip(int clip);
		size_t ClipCount() const { return m_Space.Size(); }

		// Commanded velocity in the character's frame: x toward its left, y forward (m/s).
		void SetVelocity(glm::vec2 velocity) { m_Target = velocity; }
		// Advances the cycle and computes the pose; foot events since the previous call are in Events().
		void Update(float dt);
		void Apply(ModelInstance& instance) const { instance.SetPose(m_Pose); }

		const Pose& GetPose() const { return m_Pose; }
		const std::vector<FootEvent>& Events() const { return m_Events; }
		glm::vec2 Velocity() const { return m_Velocity; }     // the smoothed velocity actually used
		float Phase() const { return m_Phase; }               // 0..1, 0 = left foot landing
		float SpeedScale() const { return m_SpeedScale; }
		float GaitWeight() const { return m_GaitWeight; }
		struct Contribution { std::string Clip; float Weight; };
		std::vector<Contribution> Contributions() const;      // for debugging and the editor

	private:
		const Model& m_Model;
		LocomotionOptions m_Options;
		BlendSpace m_Space;
		int m_Root = -1;
		glm::vec2 m_Target{ 0.0f }, m_Velocity{ 0.0f };
		float m_Phase = 0.0f, m_SpeedScale = 1.0f, m_GaitWeight = 0.0f;
		std::vector<BlendSpace::Weight> m_Weights;
		Pose m_Pose, m_Scratch;
		std::vector<FootEvent> m_Events;
	};
}
