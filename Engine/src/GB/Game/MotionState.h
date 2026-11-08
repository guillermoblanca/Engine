#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Scene/MotionState.h"
#include <glm/glm.hpp>

namespace GB
{
	// Turns the controller's state into a MotionState, keeping the previous frame to get accelerations and turn rate.
	class MotionTracker
	{
	public:
		MotionState Update(const CharacterController& controller, float dt);
		void Reset() { m_Started = false; }

	private:
		bool m_Started = false;
		float m_PrevYaw = 0.0f;
		glm::vec2 m_PrevLocal{ 0.0f };
	};
}
