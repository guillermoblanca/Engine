#include "GB/Animation/FootIK.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		glm::quat WorldRotation(const glm::mat4& m)
		{
			return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])))));
		}
	}

	void FootIK::Update(Pose& pose, const glm::mat4& characterToWorld, float weight, float dt)
	{
		if (!m_Ground || !m_Rig.HasLegs() || m_Rig.Hips < 0 || weight <= 0.001f)
		{
			m_Started = false;
			return;
		}
		const glm::mat4 inverse = glm::inverse(characterToWorld);
		const float baseY = characterToWorld[3].y;
		const bool left[2] = { true, false };
		const int feet[2] = { m_Rig.LeftLeg.Foot, m_Rig.RightLeg.Foot };

		PoseToWorld(m_Model, pose, m_World);
		float raw[2];
		glm::vec3 footWorld[2];
		for (int i = 0; i < 2; ++i)
		{
			footWorld[i] = glm::vec3(characterToWorld * m_World[static_cast<size_t>(feet[i])][3]);
			raw[i] = glm::clamp(m_Ground(footWorld[i].x, footWorld[i].z) - baseY, -Config.MaxOffset, Config.MaxOffset);
		}
		const float rawPelvis = std::min({ raw[0], raw[1], 0.0f });
		if (!m_Started)
		{
			m_Foot[0] = raw[0]; m_Foot[1] = raw[1]; m_Pelvis = rawPelvis;
			m_Started = true;
		}
		else
		{
			const float k = 1.0f - std::exp(-Config.Sharpness * std::max(dt, 0.0f));
			m_Foot[0] += (raw[0] - m_Foot[0]) * k;
			m_Foot[1] += (raw[1] - m_Foot[1]) * k;
			m_Pelvis += (rawPelvis - m_Pelvis) * k;
		}

		// the pelvis drops to the lowest foot: a world-space displacement, expressed in the hips' parent space
		{
			const int parent = m_Model.GetNodes()[static_cast<size_t>(m_Rig.Hips)].Parent;
			const glm::vec3 worldDown = glm::vec3(inverse * glm::vec4(0.0f, m_Pelvis * weight, 0.0f, 0.0f));   // model space (scale included)
			const glm::vec3 local = parent >= 0 ? glm::inverse(glm::mat3(m_World[static_cast<size_t>(parent)])) * worldDown : worldDown;
			pose[static_cast<size_t>(m_Rig.Hips)].T += local;
		}
		PoseToWorld(m_Model, pose, m_World);

		const glm::vec3 forward(0.0f, 0.0f, 1.0f);   // the character faces +Z in model space
		for (int i = 0; i < 2; ++i)
		{
			const HumanoidRig::Leg& leg = left[i] ? m_Rig.LeftLeg : m_Rig.RightLeg;
			const glm::vec3 foot(m_World[static_cast<size_t>(leg.Foot)][3]), knee(m_World[static_cast<size_t>(leg.Knee)][3]);
			// from where the foot is now (after the pelvis moved) to the height of its ground
			const glm::vec3 up = glm::vec3(inverse * glm::vec4(0.0f, m_Foot[i] - m_Pelvis, 0.0f, 0.0f));
			const glm::vec3 target = foot + up;

			// the slope under the foot, in model space
			const float d = Config.NormalSample;
			const glm::vec3 fw(characterToWorld * glm::vec4(foot, 1.0f));
			const glm::vec3 normalWorld = glm::normalize(glm::vec3(m_Ground(fw.x - d, fw.z) - m_Ground(fw.x + d, fw.z), 2.0f * d, m_Ground(fw.x, fw.z - d) - m_Ground(fw.x, fw.z + d)));
			const glm::vec3 normal = glm::normalize(glm::mat3(inverse) * normalWorld);
			glm::quat tilt = RotationBetween(glm::vec3(0.0f, 1.0f, 0.0f), normal);
			const float angle = 2.0f * std::acos(glm::clamp(std::fabs(tilt.w), 0.0f, 1.0f));
			if (angle > Config.MaxTilt) tilt = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), tilt, Config.MaxTilt / angle);
			tilt = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), tilt, Config.TiltWeight);
			const glm::quat footRotation = tilt * WorldRotation(m_World[static_cast<size_t>(leg.Foot)]);

			SolveTwoBoneIK(m_Model, pose, { leg.UpLeg, leg.Knee, leg.Foot }, target, knee + forward * 0.5f, weight, &footRotation);
		}
	}
}
