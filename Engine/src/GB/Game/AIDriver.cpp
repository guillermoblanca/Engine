#include "GB/Game/AIDriver.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	void AIDriver::SetPath(std::vector<glm::vec2> points)
	{
		m_Path = std::move(points);
		m_Index = 0;
		m_Arrived = m_Stuck = false;
		m_StillTime = 0.0f;
	}

	CharacterInput AIDriver::InputFor(const glm::vec2& dir, float speed, const CharacterConfig& config)
	{
		CharacterInput in;
		if (speed <= 1e-4f || glm::length(dir) < 1e-6f) return in;
		const glm::vec2 d = glm::normalize(dir);
		// the controller takes camera-relative input: a camera whose forward is `d` and a push straight ahead
		in.CameraYaw = std::atan2(-d.x, -d.y);
		in.Run = speed > config.WalkSpeed + 1e-3f;
		const float top = in.Run ? config.RunSpeed : config.WalkSpeed;
		in.Move = { 0.0f, std::min(speed / top, 1.0f) };
		return in;
	}

	CharacterInput AIDriver::Update(const CharacterController& c, float dt)
	{
		if (m_Index >= m_Path.size()) return CharacterInput();
		const bool last = m_Index + 1 == m_Path.size();
		const glm::vec2 here(c.GetPosition().x, c.GetPosition().z);
		const glm::vec2 to = m_Path[m_Index] - here;
		const float dist = glm::length(to);
		if (dist < (last ? ArriveRadius : WaypointRadius))
		{
			if (last) { m_Arrived = true; m_Path.clear(); m_Index = 0; return CharacterInput(); }
			++m_Index;
			return Update(c, dt);
		}
		const float top = Run ? RunSpeed : WalkSpeed;
		float speed = top;
		if (last && dist < SlowRadius) speed = std::max(top * dist / SlowRadius, 0.4f);   // ease in, never crawl

		// pushing against something without getting anywhere: give up instead of pushing forever
		m_StillTime = c.WasBlocked() && c.GetSpeed() < 0.3f ? m_StillTime + dt : 0.0f;
		if (m_StillTime > StuckSeconds) { m_Stuck = true; m_Path.clear(); m_Index = 0; return CharacterInput(); }

		CharacterConfig config = c.Config;
		config.WalkSpeed = WalkSpeed;
		config.RunSpeed = RunSpeed;
		return InputFor(to / dist, speed, config);
	}
}
