#include "CameraController.h"
#include "GB/Input.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		constexpr float kMaxPitch = 1.553343f;   // 89 degrees
		// Frame-rate independent smoothing factor.
		float Damp(float sharpness, float dt) { return sharpness <= 0.0f ? 1.0f : 1.0f - std::exp(-sharpness * dt); }
	}

	// ---------------------------------------------------------------- Orbit
	glm::vec3 OrbitController::GetPosition() const
	{
		return Target + Distance * glm::vec3(std::sin(Yaw) * std::cos(Pitch), std::sin(Pitch), std::cos(Yaw) * std::cos(Pitch));
	}

	void OrbitController::Rotate(float deltaYaw, float deltaPitch)
	{
		Yaw += deltaYaw;
		Pitch = std::clamp(Pitch + deltaPitch, -kMaxPitch, kMaxPitch);
	}

	void OrbitController::Zoom(float amount)
	{
		Distance = std::clamp(Distance * std::exp(amount), MinDistance, MaxDistance);
	}

	void OrbitController::Pan(float deltaRight, float deltaUp)
	{
		const glm::vec3 forward = glm::normalize(Target - GetPosition());
		const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
		const glm::vec3 up = glm::cross(right, forward);
		Target += (right * deltaRight + up * deltaUp) * Distance;
	}

	void OrbitController::Update(float dt)
	{
		using GB::Key;
		float yaw = 0.0f, pitch = 0.0f, zoom = 0.0f;
		if (Input::IsKeyDown(Key::Left) || Input::IsKeyDown(Key::A))  yaw -= 1.0f;
		if (Input::IsKeyDown(Key::Right) || Input::IsKeyDown(Key::D)) yaw += 1.0f;
		if (Input::IsKeyDown(Key::Up) || Input::IsKeyDown(Key::W))    pitch += 1.0f;
		if (Input::IsKeyDown(Key::Down) || Input::IsKeyDown(Key::S))  pitch -= 1.0f;
		if (Input::IsKeyDown(Key::Q)) zoom -= 1.0f;
		if (Input::IsKeyDown(Key::E)) zoom += 1.0f;
		yaw += Input::GetGamepadAxis(GamepadAxis::RightX);
		pitch -= Input::GetGamepadAxis(GamepadAxis::RightY);
		// Triggers report -1 (released) .. 1 (pressed).
		zoom += 0.5f * (Input::GetGamepadAxis(GamepadAxis::RightTrigger, 0, 0.0f) + 1.0f) * 0.5f;
		zoom -= 0.5f * (Input::GetGamepadAxis(GamepadAxis::LeftTrigger, 0, 0.0f) + 1.0f) * 0.5f;
		Rotate(yaw * RotateSpeed * dt, pitch * RotateSpeed * dt);
		Zoom(zoom * ZoomSpeed * dt);

		const auto [dx, dy] = Input::GetMouseDelta();
		if (Input::IsMouseDown(MouseButton::Left))
			Rotate(-static_cast<float>(dx) * MouseSensitivity, static_cast<float>(dy) * MouseSensitivity);
		if (Input::IsMouseDown(MouseButton::Right) || Input::IsMouseDown(MouseButton::Middle))
			Pan(-static_cast<float>(dx) * 0.0015f, static_cast<float>(dy) * 0.0015f);
		Zoom(-Input::GetScrollDelta() * 0.1f);
	}

	void OrbitController::Apply(Camera& camera) const
	{
		camera.Target = Target;
		camera.Position = GetPosition();
		camera.Up = { 0.0f, 1.0f, 0.0f };
	}

	// ---------------------------------------------------------------- Fly
	glm::vec3 FlyController::GetForward() const
	{
		return { std::sin(Yaw) * -std::cos(Pitch), std::sin(Pitch), -std::cos(Yaw) * std::cos(Pitch) };
	}

	void FlyController::Look(float deltaYaw, float deltaPitch)
	{
		Yaw += deltaYaw;
		Pitch = std::clamp(Pitch + deltaPitch, -kMaxPitch, kMaxPitch);
	}

	void FlyController::Move(float forward, float right, float up, float dt, float speedMultiplier)
	{
		const glm::vec3 f = GetForward();
		const glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
		glm::vec3 direction = f * forward + r * right + glm::vec3(0, 1, 0) * up;
		if (glm::dot(direction, direction) > 1.0f)
			direction = glm::normalize(direction);   // no faster diagonals
		Position += direction * Speed * speedMultiplier * dt;
	}

	void FlyController::LookAt(const glm::vec3& point)
	{
		const glm::vec3 d = glm::normalize(point - Position);
		Pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
		Yaw = std::atan2(-d.x, -d.z);
	}

	void FlyController::Update(float dt)
	{
		using GB::Key;
		float forward = 0.0f, right = 0.0f, up = 0.0f;
		if (Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up))    forward += 1.0f;
		if (Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down))  forward -= 1.0f;
		if (Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right)) right += 1.0f;
		if (Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left))  right -= 1.0f;
		if (Input::IsKeyDown(Key::E)) up += 1.0f;
		if (Input::IsKeyDown(Key::Q)) up -= 1.0f;
		forward -= Input::GetGamepadAxis(GamepadAxis::LeftY);
		right += Input::GetGamepadAxis(GamepadAxis::LeftX);
		up += 0.5f * (Input::GetGamepadAxis(GamepadAxis::RightTrigger, 0, 0.0f) + 1.0f) * 0.5f;
		up -= 0.5f * (Input::GetGamepadAxis(GamepadAxis::LeftTrigger, 0, 0.0f) + 1.0f) * 0.5f;
		const bool boost = Input::IsKeyDown(Key::LeftShift) || Input::IsGamepadButtonDown(GamepadButton::LeftStick);
		Move(forward, right, up, dt, boost ? BoostMultiplier : 1.0f);

		float yaw = Input::GetGamepadAxis(GamepadAxis::RightX) * LookSpeed * dt;
		float pitch = -Input::GetGamepadAxis(GamepadAxis::RightY) * LookSpeed * dt;
		if (Input::IsMouseDown(MouseButton::Right))
		{
			const auto [dx, dy] = Input::GetMouseDelta();
			yaw -= static_cast<float>(dx) * MouseSensitivity;
			pitch -= static_cast<float>(dy) * MouseSensitivity;
		}
		Look(yaw, pitch);
		Speed = std::clamp(Speed * std::exp(Input::GetScrollDelta() * 0.1f), 0.1f, 500.0f);   // wheel = speed
	}

	void FlyController::Apply(Camera& camera) const
	{
		camera.Position = Position;
		camera.Target = Position + GetForward();
		camera.Up = { 0.0f, 1.0f, 0.0f };
	}

	// ---------------------------------------------------------------- Follow
	glm::vec3 FollowController::DesiredPosition() const
	{
		const float s = std::sin(m_TargetYaw), c = std::cos(m_TargetYaw);
		const glm::vec3 rotated(Offset.x * c + Offset.z * s, Offset.y, -Offset.x * s + Offset.z * c);   // rotate about +Y
		return m_TargetPos + rotated;
	}

	void FollowController::Snap()
	{
		m_Position = DesiredPosition();
		m_Look = m_TargetPos + LookOffset;
		m_Initialized = true;
	}

	void FollowController::Update(float dt)
	{
		if (!m_Initialized)
		{
			Snap();
			return;
		}
		m_Position = glm::mix(m_Position, DesiredPosition(), Damp(PositionSharpness, dt));
		m_Look = glm::mix(m_Look, m_TargetPos + LookOffset, Damp(LookSharpness, dt));
	}

	void FollowController::Apply(Camera& camera) const
	{
		camera.Position = m_Position;
		camera.Target = m_Look;
		camera.Up = { 0.0f, 1.0f, 0.0f };
	}
}
