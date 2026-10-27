#pragma once
#include "GB/Animation/ClipAnalysis.h"
#include "GB/Animation/Pose.h"
#include "GB/Game/MotionState.h"
#include <string>
#include <vector>

namespace GB
{
	// Turning on the spot: while a character is (almost) standing still and the controller turns it, a turn clip plays
	// over the locomotion so the feet step instead of the body spinning like a post. The clip's own rotation of the hips is
	// removed (the controller already turns the character), and so is its drift, leaving the stepping legs and the counter
	// twist of the spine. Which clip is the left turn and which the right is read from the clip itself (the sign of its
	// root yaw), and each locomotion set can have its own pair (free, carrying a box, injured...).
	class TurnLayer
	{
	public:
		TurnLayer(const Model& model, const ClipAnalysisConfig& analysis = {});

		// Registers a turn clip for a set (use "" for any set without its own). False when the clip is missing, does not
		// turn the root or the bones were not found; the side comes from the direction it turns.
		bool AddClip(const std::string& set, const std::string& clip);
		bool HasClips(const std::string& set) const { return Find(set) >= 0; }

		void Update(const MotionState& state, float dt, const std::string& set);
		void Apply(Pose& pose);
		float Weight() const { return m_Weight; }
		bool Active() const { return m_Weight > 0.0f; }
		int Side() const { return m_Side; }   // +1 turning left, -1 right, 0 none

		float StopSpeed = 0.4f;     // m/s: faster than this the legs walk, the turn is part of the gait
		float StartRate = 30.0f;    // degrees per second of yaw that start a turn
		float BlendIn = 0.12f;      // seconds
		float BlendOut = 0.25f;

	private:
		struct SetClips
		{
			std::string Set;
			int Clip[2] = { -1, -1 };      // [0] left (positive yaw), [1] right
			ClipStats Stats[2];
			glm::vec3 Axis[2];              // the axis the root turns around over the clip
			glm::quat Start[2];             // the root's rotation at the first frame
		};
		int Find(const std::string& set) const;

		const Model& m_Model;
		ClipAnalysisConfig m_Analysis;
		int m_Root = -1;
		std::vector<SetClips> m_Sets;
		int m_Current = -1, m_Side = 0;
		float m_Time = 0.0f, m_Weight = 0.0f;
		Pose m_Scratch;
	};
}
