#pragma once
#include "GB/Game/CharacterMotor.h"
#include <string>
#include <vector>

namespace GB
{
	// Anything that decides where a character goes. The motor, the animation and the footsteps work from the
	// MoveInput it returns and the MotionState that results, so a player, an AI and a script are interchangeable.
	class CharacterController
	{
	public:
		virtual ~CharacterController() = default;
		virtual MoveInput Update(const MotionState& state, float dt) = 0;
	};

	// Raw player input, already independent of the device (keys, stick, replay).
	struct PlayerInput
	{
		glm::vec2 Move{ 0.0f };     // x right, y forward, each -1..1 (a stick gives intermediate speeds)
		bool Run = false;
		bool Jump = false;
	};
	// Reads the keyboard (WASD / arrows, Shift to run, Space to jump) and the first gamepad (left stick, A to jump,
	// right bumper to run) from GB::Input.
	PlayerInput SamplePlayerInput();

	// Camera-relative movement: pushing forward goes where the camera looks. `Strafe` keeps the character facing the
	// camera direction (aiming); otherwise it turns toward where it moves.
	class PlayerController : public CharacterController
	{
	public:
		float WalkSpeed = 1.7f, RunSpeed = 4.4f;
		bool Strafe = false;

		void SetInput(const PlayerInput& input) { m_Input = input; }
		void SetCameraYaw(float yaw) { m_CameraYaw = yaw; }
		MoveInput Update(const MotionState& state, float dt) override;

	private:
		PlayerInput m_Input;
		float m_CameraYaw = 0.0f;   // the camera looks along (sin yaw, 0, cos yaw)
		bool m_JumpHeld = false;
	};

	// Walks to a point or along a path, slowing down as it arrives.
	class AIController : public CharacterController
	{
	public:
		float WalkSpeed = 1.7f, RunSpeed = 4.4f;
		bool Run = false;
		float ArriveRadius = 0.25f;     // within this of the last point it has arrived
		float WaypointRadius = 0.6f;    // within this of an intermediate point it moves on
		float SlowRadius = 1.4f;        // starts slowing this far from the last point
		float StuckSeconds = 1.5f;      // gives up when it barely moves for this long

		void MoveTo(const glm::vec3& target) { SetPath({ target }); }
		void SetPath(std::vector<glm::vec3> points);
		void Stop() { m_Path.clear(); m_Index = 0; m_Arrived = false; m_Stuck = false; }
		bool Arrived() const { return m_Arrived; }
		bool Stuck() const { return m_Stuck; }
		bool HasTarget() const { return m_Index < m_Path.size(); }
		MoveInput Update(const MotionState& state, float dt) override;

	private:
		std::vector<glm::vec3> m_Path;
		size_t m_Index = 0;
		bool m_Arrived = false, m_Stuck = false;
		float m_StillTime = 0.0f;
	};

	// Runs a list of commands in order: for tests, cinematics and agents that play the game.
	//   move <x> <z> [speed]   walk there (speed in m/s; default the walk speed), done on arrival
	//   wait <seconds>
	//   jump
	//   face <degrees>         turn to face a direction (0 = +Z) while standing
	//   stop
	class ScriptedController : public CharacterController
	{
	public:
		float WalkSpeed = 1.7f;

		// `script` uses ';' or newlines between commands. False (and nothing queued) when a command is not understood.
		bool Load(const std::string& script, std::string* error = nullptr);
		bool Finished() const { return m_Index >= m_Commands.size(); }
		MoveInput Update(const MotionState& state, float dt) override;

	private:
		struct Command { std::string Name; float A = 0.0f, B = 0.0f, C = 0.0f; };
		std::vector<Command> m_Commands;
		size_t m_Index = 0;
		float m_Timer = 0.0f;
		bool m_Started = false;
		AIController m_Walker;
	};
}
