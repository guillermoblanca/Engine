#pragma once
#include <glm/glm.hpp>

namespace GB
{
	// Moves a character from where it is to an exact spot and facing over a short time (motion warping): an action whose clip does not
	// travel (picking fruit, milking) starts wherever the approach ended and settles on the interaction point while it blends in.
	// Position and yaw ease in and out; yaw takes the short way round.
	class MotionWarp
	{
	public:
		void Start(const glm::vec2& fromXZ, float fromYaw, const glm::vec2& toXZ, float toYaw, float seconds);
		// Advances the warp and gives the placement for this frame.
		void Update(float dt, glm::vec2& xz, float& yaw);
		bool Active() const { return m_Active; }
		float Progress() const { return m_Seconds > 0.0f ? glm::min(m_Time / m_Seconds, 1.0f) : 1.0f; }

	private:
		glm::vec2 m_From{ 0.0f }, m_To{ 0.0f };
		float m_FromYaw = 0.0f, m_YawDelta = 0.0f;   // the signed short-way turn
		float m_Time = 0.0f, m_Seconds = 0.0f;
		bool m_Active = false;
	};
}
