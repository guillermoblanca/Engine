#include "GB/Game/CharacterControllers.h"
#include "GB/Input.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace GB
{
	PlayerInput SamplePlayerInput()
	{
		PlayerInput in;
		float x = 0.0f, y = 0.0f;
		if (Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up)) y += 1.0f;
		if (Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down)) y -= 1.0f;
		if (Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right)) x += 1.0f;
		if (Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left)) x -= 1.0f;
		const float stickX = Input::GetGamepadAxis(GamepadAxis::LeftX), stickY = -Input::GetGamepadAxis(GamepadAxis::LeftY);
		if (std::fabs(stickX) > std::fabs(x)) x = stickX;
		if (std::fabs(stickY) > std::fabs(y)) y = stickY;
		in.Move = { x, y };
		in.Run = Input::IsKeyDown(Key::LeftShift) || Input::IsKeyDown(Key::RightShift) || Input::IsGamepadButtonDown(GamepadButton::RightBumper);
		in.Jump = Input::IsKeyDown(Key::Space) || Input::IsGamepadButtonDown(GamepadButton::A);
		return in;
	}

	MoveInput PlayerController::Update(const MotionState&, float)
	{
		MoveInput out;
		glm::vec2 move = m_Input.Move;
		const float len = glm::length(move);
		if (len > 1.0f) move /= len;
		// the camera's forward and right on the ground
		const glm::vec2 forward(std::sin(m_CameraYaw), std::cos(m_CameraYaw)), right(-std::cos(m_CameraYaw), std::sin(m_CameraYaw));
		const glm::vec2 dir = forward * move.y + right * move.x;
		// a stick gives any speed up to the top one; keys give the walk (or the run with Shift)
		const float top = m_Input.Run ? RunSpeed : WalkSpeed;
		out.Velocity = dir * top;
		if (Strafe) { out.HasYaw = true; out.DesiredYaw = m_CameraYaw; }
		out.Jump = m_Input.Jump && !m_JumpHeld;   // one jump per press
		m_JumpHeld = m_Input.Jump;
		return out;
	}

	void AIController::SetPath(std::vector<glm::vec3> points)
	{
		m_Path = std::move(points);
		m_Index = 0;
		m_Arrived = m_Stuck = false;
		m_StillTime = 0.0f;
	}

	MoveInput AIController::Update(const MotionState& s, float dt)
	{
		MoveInput out;
		if (m_Index >= m_Path.size()) return out;
		const bool last = m_Index + 1 == m_Path.size();
		glm::vec2 to = glm::vec2(m_Path[m_Index].x, m_Path[m_Index].z) - glm::vec2(s.Position.x, s.Position.z);
		float dist = glm::length(to);
		if (dist < (last ? ArriveRadius : WaypointRadius))
		{
			if (last) { m_Arrived = true; m_Path.clear(); m_Index = 0; return out; }
			++m_Index;
			return Update(s, dt);
		}
		const float top = Run ? RunSpeed : WalkSpeed;
		float speed = top;
		if (last && dist < SlowRadius) speed = std::max(top * dist / SlowRadius, 0.4f);   // ease in, never crawl
		out.Velocity = to / dist * speed;

		// not getting anywhere (a wall in the way): give up instead of pushing forever
		m_StillTime = s.Speed < 0.1f ? m_StillTime + dt : 0.0f;
		if (m_StillTime > StuckSeconds) { m_Stuck = true; m_Path.clear(); m_Index = 0; }
		return out;
	}

	bool ScriptedController::Load(const std::string& script, std::string* error)
	{
		std::vector<Command> parsed;
		std::string normalized = script;
		std::replace(normalized.begin(), normalized.end(), ';', '\n');
		std::istringstream lines(normalized);
		for (std::string line; std::getline(lines, line);)
		{
			std::istringstream words(line);
			Command c;
			if (!(words >> c.Name)) continue;
			words >> c.A >> c.B >> c.C;
			words.clear();
			const bool known = c.Name == "move" || c.Name == "wait" || c.Name == "jump" || c.Name == "face" || c.Name == "stop";
			if (!known)
			{
				if (error) *error = "unknown command '" + c.Name + "' (move, wait, jump, face, stop)";
				return false;
			}
			parsed.push_back(c);
		}
		m_Commands = std::move(parsed);
		m_Index = 0;
		m_Started = false;
		return true;
	}

	MoveInput ScriptedController::Update(const MotionState& s, float dt)
	{
		MoveInput out;
		if (Finished()) return out;
		const Command& c = m_Commands[m_Index];
		if (!m_Started)
		{
			m_Started = true;
			m_Timer = 0.0f;
			if (c.Name == "move")
			{
				m_Walker.WalkSpeed = c.C > 0.0f ? c.C : WalkSpeed;
				m_Walker.Run = false;
				m_Walker.MoveTo({ c.A, s.Position.y, c.B });
			}
		}
		bool done = false;
		if (c.Name == "move")
		{
			out = m_Walker.Update(s, dt);
			done = m_Walker.Arrived() || m_Walker.Stuck();
		}
		else if (c.Name == "wait") { m_Timer += dt; done = m_Timer >= c.A; }
		else if (c.Name == "jump") { out.Jump = true; done = true; }
		else if (c.Name == "face")
		{
			out.HasYaw = true;
			out.DesiredYaw = glm::radians(c.A);
			float d = out.DesiredYaw - s.Yaw;
			while (d > 3.14159265f) d -= 6.2831853f;
			while (d < -3.14159265f) d += 6.2831853f;
			done = std::fabs(d) < glm::radians(2.0f);
		}
		else done = true;   // stop
		if (done) { ++m_Index; m_Started = false; }
		return out;
	}
}
