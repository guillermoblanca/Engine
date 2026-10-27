#pragma once
#include "GB/Game/CharacterController.h"
#include <glm/glm.hpp>

namespace GB
{
	// What a character is doing, in the terms the animation needs. It is derived from a CharacterController after each
	// step, so animation never depends on who or what produced the input (a player, an AI, a script, a replay).
	struct MotionState
	{
		glm::vec3 Position{ 0.0f };      // at the feet
		float Yaw = 0.0f;                // radians; forward is (sin yaw, 0, cos yaw), left is (cos yaw, 0, -sin yaw)
		glm::vec3 Velocity{ 0.0f };
		glm::vec2 LocalVelocity{ 0.0f }; // x toward the character's left, y forward (what the locomotion blend takes)
		float Speed = 0.0f;              // horizontal m/s
		glm::vec2 LocalAcceleration{ 0.0f };   // m/s^2, same axes
		float YawRate = 0.0f;            // degrees per second (positive = turning left)
		float WadeDepth = 0.0f;          // meters of water at the feet
		bool Blocked = false;            // pushing against something
		bool Grounded = true;            // false while jumping or falling
		float VerticalSpeed = 0.0f;      // m/s, positive up
		bool Landed = false;             // touched the ground this step (a heavy step)
		float LandingSpeed = 0.0f;       // downward m/s of that landing
	};

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
