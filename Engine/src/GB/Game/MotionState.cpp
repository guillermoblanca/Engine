#include "GB/Game/MotionState.h"
#include <cmath>

namespace GB
{
	MotionState MotionTracker::Update(const CharacterController& c, float dt)
	{
		MotionState s;
		s.Position = c.GetPosition();
		s.Yaw = c.GetYaw();
		s.Velocity = c.GetVelocity();
		s.Speed = glm::length(glm::vec2(s.Velocity.x, s.Velocity.z));
		s.WadeDepth = c.GetWadeDepth();
		s.Blocked = c.WasBlocked();
		const glm::vec3 forward(std::sin(s.Yaw), 0.0f, std::cos(s.Yaw)), left(std::cos(s.Yaw), 0.0f, -std::sin(s.Yaw));
		s.LocalVelocity = { glm::dot(s.Velocity, left), glm::dot(s.Velocity, forward) };
		if (m_Started && dt > 1e-5f)
		{
			s.LocalAcceleration = (s.LocalVelocity - m_PrevLocal) / dt;
			float d = s.Yaw - m_PrevYaw;
			while (d > 3.14159265f) d -= 6.2831853f;
			while (d < -3.14159265f) d += 6.2831853f;
			s.YawRate = glm::degrees(d) / dt;
		}
		m_Started = true;
		m_PrevYaw = s.Yaw;
		m_PrevLocal = s.LocalVelocity;
		return s;
	}
}
