#pragma once
#include "GB/World/Physics.h"
#include <glm/glm.hpp>

namespace GB
{
	// What anything that moves a character says it wants this frame. Players, AI, scripts and replays all produce
	// exactly this; the motor and the animation never know which one it was.
	struct MoveInput
	{
		glm::vec2 Velocity{ 0.0f };      // desired horizontal velocity in the world: x, z (m/s)
		bool HasYaw = false;             // true: face DesiredYaw (aiming, strafing); false: face where it moves
		float DesiredYaw = 0.0f;         // radians; forward is (sin yaw, 0, cos yaw)
		bool Jump = false;
	};

	// What the character is doing, as the animation and the rest of the game see it.
	struct MotionState
	{
		glm::vec3 Position{ 0.0f };      // at the feet
		float Yaw = 0.0f;                // radians; forward is (sin yaw, 0, cos yaw), left is (cos yaw, 0, -sin yaw)
		glm::vec3 Velocity{ 0.0f };
		glm::vec2 LocalVelocity{ 0.0f }; // x toward the character's left, y forward (what the locomotion blend takes)
		float Speed = 0.0f;              // horizontal m/s
		glm::vec2 LocalAcceleration{ 0.0f };   // m/s^2, same axes
		float YawRate = 0.0f;            // degrees per second (positive = turning left)
		bool Grounded = false;
		bool Sliding = false;            // on ground too steep to stand on
		glm::vec3 GroundNormal{ 0, 1, 0 };
		float SlopeDegrees = 0.0f;
		float TimeInAir = 0.0f;
		bool Jumped = false;             // started a jump this step
		bool Landed = false;             // touched the ground this step
		float LandingSpeed = 0.0f;       // downward m/s of the landing (when Landed)
		float WaterDepth = 0.0f;         // meters of water around the feet
		bool InWater = false;            // wading: deep enough to slow the character
	};

	struct CharacterConfig
	{
		float Radius = 0.3f;
		float Height = 1.8f;
		float StepHeight = 0.4f;         // ledges up to this height are walked onto
		float MaxSlopeDegrees = 50.0f;   // steeper ground cannot be stood on or climbed
		float JumpHeight = 1.1f;         // meters
		float Gravity = 22.0f;           // m/s^2 (a bit above real for a snappier jump)
		float Acceleration = 22.0f;      // m/s^2 on the ground
		float Deceleration = 30.0f;
		float AirControl = 0.25f;        // fraction of the ground acceleration in the air
		float TurnRate = 540.0f;         // degrees per second
		float CoyoteTime = 0.1f;         // seconds after leaving a ledge in which a jump still works
		float WadeDepth = 0.5f;          // water deeper than this slows the character
		float WadeSpeedFactor = 0.55f;
		float MaxFallSpeed = 50.0f;
	};

	// A capsule character moved by kinematic rules: it walks on terrain and on top of boxes (steps, crates, platforms),
	// slides along walls, cannot climb ground steeper than MaxSlopeDegrees (and slides down it), jumps and falls. It
	// reads the world from a PhysicsWorld (ground functions, static colliders, water) and is deterministic.
	class CharacterMotor
	{
	public:
		explicit CharacterMotor(const CharacterConfig& config = {}) : m_Config(config) {}

		void SetPosition(const glm::vec3& feet, float yaw = 0.0f);
		void Step(const MoveInput& input, float dt, const PhysicsWorld& world);

		const MotionState& State() const { return m_State; }
		const CharacterConfig& Config() const { return m_Config; }

	private:
		// Highest walkable surface under the footprint no higher than `maxY`; returns its normal too.
		float SupportHeight(const glm::vec2& xz, float maxY, const PhysicsWorld& world, glm::vec3& normal) const;
		// Pushes the character's body out of walls and obstacles (the capsule above the step height).
		glm::vec2 ResolveWalls(const glm::vec3& feet, const glm::vec2& wanted, const PhysicsWorld& world) const;

		CharacterConfig m_Config;
		MotionState m_State;
		glm::vec2 m_PrevLocalVelocity{ 0.0f };
		float m_SinceGrounded = 0.0f;
		bool m_Started = false;
	};
}
