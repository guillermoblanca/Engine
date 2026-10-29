#include "GB/Animation/ReachLayer.h"
#include "GB/Animation/IK.h"
#include "GB/Animation/Pose.h"
#include "GB/Assets/Model.h"
#include <algorithm>

namespace GB
{
	void ReachLayer::Reach(bool right, const glm::vec3& target, float weight)
	{
		Hand& h = m_Hand[right ? 1 : 0];
		h.Target = target;
		h.Wanted = std::clamp(weight, 0.0f, 1.0f);
	}

	void ReachLayer::Update(const MotionState& state, float dt, const std::string& set)
	{
		(void)state;
		(void)set;
		for (Hand& h : m_Hand)
		{
			const float step = Speed * std::max(dt, 0.0f);
			h.Weight = h.Weight < h.Wanted ? std::min(h.Weight + step, h.Wanted) : std::max(h.Weight - step, h.Wanted);
		}
	}

	void ReachLayer::Apply(Pose& pose)
	{
		if (!m_Rig.HasArms()) return;
		for (int side = 0; side < 2; ++side)
		{
			const Hand& h = m_Hand[side];
			if (h.Weight <= 0.0f) continue;
			const HumanoidRig::Arm& arm = side ? m_Rig.Right : m_Rig.Left;
			PoseToWorld(m_Model, pose, m_World);
			const glm::vec3 elbow(m_World[static_cast<size_t>(arm.ForeArm)][3]);
			// ease in and out (smoothstep of the weight): the hand does not snap onto the point
			const float w = h.Weight * h.Weight * (3.0f - 2.0f * h.Weight);
			SolveTwoBoneIK(m_Model, pose, { arm.UpperArm, arm.ForeArm, arm.Hand }, h.Target, elbow, w);
		}
	}
}
