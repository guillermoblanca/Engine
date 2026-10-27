#pragma once
#include "GB/Animation/ClipAnalysis.h"
#include "GB/Animation/Pose.h"
#include "GB/Game/MotionState.h"
#include <string>

namespace GB
{
	// The jump / fall pose of a character, driven by the controller's own flight (MotionState::Grounded) instead of by a
	// timer: when the feet leave the ground the jump clip plays up to its flight phase and holds there for as long as the
	// character is in the air; on landing it plays the rest of the clip (the absorb) while it fades out. The clip's height
	// gain is removed (the controller already moves the character up and down), keeping its crouches.
	class AirLayer
	{
	public:
		AirLayer(const Model& model, const ClipAnalysisConfig& analysis = {});

		// The clip needs a flight phase (ClipStats::AirStart / AirEnd). False otherwise; nothing changes.
		bool SetClip(const std::string& name);
		bool HasClip() const { return m_Clip >= 0; }

		void Update(const MotionState& state, float dt);
		void Apply(Pose& pose);
		float Weight() const { return m_Weight; }
		bool Active() const { return m_Weight > 0.0f; }
		bool InAir() const { return m_InAir; }

		float LeadIn = 0.08f;       // seconds of take-off played before the flight starts
		float BlendIn = 0.1f;       // seconds
		float BlendOut = 0.3f;      // seconds after landing

	private:
		const Model& m_Model;
		int m_Root = -1, m_Clip = -1;
		ClipStats m_Stats;
		bool m_InAir = false;
		float m_Time = 0.0f, m_Weight = 0.0f;
		Pose m_Scratch;
	};
}
