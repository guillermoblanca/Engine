#include "GB/Animation/LookAt.h"
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

	LookAt::LookAt(const Model& model, const HumanoidRig& rig) : m_Model(model), m_Rig(rig)
	{
		if (rig.Head >= 0)
		{
			std::vector<glm::mat4> rest;
			PoseToWorld(model, RestPose(model), rest);
			m_RestHead = WorldRotation(rest[static_cast<size_t>(rig.Head)]);
		}
	}

	void LookAt::Update(Pose& pose, const glm::mat4& characterToWorld, float weight, float dt)
	{
		if (m_Rig.Head < 0) return;
		const float k = 1.0f - std::exp(-Config.Sharpness * std::max(dt, 0.0f));
		const float wantStrength = m_HasTarget ? 1.0f : 0.0f;
		m_Strength += (wantStrength - m_Strength) * k;
		if (!m_HasTarget && m_Strength < 1e-3f) { m_Yaw = m_Pitch = 0.0f; m_Strength = 0.0f; return; }
		if (weight <= 0.0f) return;

		PoseToWorld(m_Model, pose, m_World);
		const glm::vec3 head(m_World[static_cast<size_t>(m_Rig.Head)][3]);
		const glm::mat4 inverse = glm::inverse(characterToWorld);
		// the wanted direction in the character's frame, as a yaw and pitch from its forward (+Z, left = +X), clamped to the limits
		if (m_HasTarget)
		{
			const glm::vec3 d = glm::vec3(inverse * glm::vec4(m_Target, 1.0f)) - head;
			if (glm::length(d) > 1e-4f)
			{
				const float yaw = glm::clamp(std::atan2(d.x, d.z), -Config.MaxYaw, Config.MaxYaw);
				const float pitch = glm::clamp(std::atan2(d.y, std::hypot(d.x, d.z)), -Config.MaxPitch, Config.MaxPitch);
				m_Yaw += (yaw - m_Yaw) * k;
				m_Pitch += (pitch - m_Pitch) * k;
			}
		}
		else
		{
			m_Yaw -= m_Yaw * k;
			m_Pitch -= m_Pitch * k;
		}
		const glm::vec3 wanted(std::cos(m_Pitch) * std::sin(m_Yaw), std::sin(m_Pitch), std::cos(m_Pitch) * std::cos(m_Yaw));

		// where the head looks now: its forward in the rest pose is +Z
		const glm::quat headRotation = WorldRotation(m_World[static_cast<size_t>(m_Rig.Head)]);
		const glm::vec3 current = glm::normalize(headRotation * glm::inverse(m_RestHead) * glm::vec3(0.0f, 0.0f, 1.0f));
		const glm::quat turn = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), RotationBetween(current, wanted), glm::clamp(weight * m_Strength, 0.0f, 1.0f));

		// spread over the chest, the neck and the head: each bone takes its share of the turn in world space
		const float total = std::max(Config.HeadShare + Config.NeckShare + Config.ChestShare, 1e-4f);
		const int bones[3] = { m_Rig.Spine2, m_Rig.Neck, m_Rig.Head };
		const float shares[3] = { Config.ChestShare / total, Config.NeckShare / total, Config.HeadShare / total };
		for (int i = 0; i < 3; ++i)
		{
			if (bones[i] < 0 || shares[i] <= 0.0f) continue;
			PoseToWorld(m_Model, pose, m_World);
			const glm::quat share = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), turn, shares[i]);
			const int parent = m_Model.GetNodes()[static_cast<size_t>(bones[i])].Parent;
			const glm::quat parentRotation = parent >= 0 ? WorldRotation(m_World[static_cast<size_t>(parent)]) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			const glm::quat world = share * WorldRotation(m_World[static_cast<size_t>(bones[i])]);
			pose[static_cast<size_t>(bones[i])].R = glm::normalize(glm::inverse(parentRotation) * world);
		}
	}
}
