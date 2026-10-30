#pragma once
#include "GB/Animation/BlendSpace.h"
#include "GB/Animation/Pose.h"
#include "GB/Assets/ModelInstance.h"
#include <cmath>
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
		// Tuning from the animation bindings: the limits of the speed factor, the smoothing and the gait share are replaced (the bone names are kept).
		void SetOptions(const LocomotionOptions& options) { m_Options.MinSpeedScale = options.MinSpeedScale; m_Options.MaxSpeedScale = options.MaxSpeedScale; m_Options.VelocitySmoothing = options.VelocitySmoothing; m_Options.MinGaitWeight = options.MinGaitWeight; }
		// Where a clip sits on the speed axis, and so the ground speed (m/s) at which it plays at its natural rate: its measured velocity is scaled to
		// `speed` (same direction). False when the clip is not in the blend or does not travel (an idle has no speed to set).
		bool SetClipSpeed(const std::string& clip, float speed);
		// The speed of the fastest clip (m/s): a set with only a walk cannot show anything faster, so a character in it should not be asked to.
		float MaxSpeed() const;

		// Commanded velocity in the character's frame: x toward its left, y forward (m/s).
		void SetVelocity(glm::vec2 velocity) { m_Target = velocity; }
		// For taking over from another blender mid-stride: continue at its cycle phase and velocity.
		void SetPhase(float phase) { m_Phase = phase - std::floor(phase); }
		void SnapVelocity(glm::vec2 velocity) { m_Target = m_Velocity = velocity; }
		// Advances the cycle and computes the pose; foot events since the previous call are in Events().
		void Update(float dt);
		// Level of detail (H16): off, Update still advances the cycle, the blend weights and the foot events (all that timing depends on) but does
		// not sample the clips: the pose stays the last one computed. What a far character skips on the frames it is not redrawn.
		void SetSampling(bool on) { m_Sample = on; }
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
		bool m_Sample = true;
		std::vector<BlendSpace::Weight> m_Weights;
		Pose m_Pose, m_Scratch;
		std::vector<FootEvent> m_Events;
		// A clip that does not close its loop (the walk with a box is an arc: the hips turn ~30 degrees by the last frame) would snap
		// back every cycle. For those, the rotation each bone has gained by the end of the clip, taken out in proportion to the time.
		std::vector<std::vector<glm::quat>> m_LoopFix;   // by blend space index; empty for a clip that loops by itself
	};
}
