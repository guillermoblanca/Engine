#include "GB/Game/MotionWarp.h"
#include <cmath>

namespace GB
{
	namespace
	{
		float WrapAngle(float a)
		{
			const float pi = 3.14159265f;
			while (a > pi) a -= 2.0f * pi;
			while (a < -pi) a += 2.0f * pi;
			return a;
		}
	}

	void MotionWarp::Start(const glm::vec2& fromXZ, float fromYaw, const glm::vec2& toXZ, float toYaw, float seconds)
	{
		m_From = fromXZ;
		m_To = toXZ;
		m_FromYaw = fromYaw;
		m_YawDelta = WrapAngle(toYaw - fromYaw);
		m_Time = 0.0f;
		m_Seconds = seconds > 0.0f ? seconds : 0.0f;
		m_Active = true;
	}

	void MotionWarp::Update(float dt, glm::vec2& xz, float& yaw)
	{
		if (!m_Active) return;
		m_Time += dt > 0.0f ? dt : 0.0f;
		const float t = m_Seconds > 0.0f ? glm::min(m_Time / m_Seconds, 1.0f) : 1.0f;
		const float s = t * t * (3.0f - 2.0f * t);
		xz = glm::mix(m_From, m_To, s);
		yaw = WrapAngle(m_FromYaw + m_YawDelta * s);
		if (t >= 1.0f) m_Active = false;
	}
}
