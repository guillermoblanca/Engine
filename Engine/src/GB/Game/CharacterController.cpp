#include "CharacterController.h"
#include "GB/Input.h"
#include "GB/World/Terrain.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;
		constexpr float kNoWater = -1e8f;
		float Damp(float sharpness, float dt) { return sharpness <= 0.0f ? 1.0f : 1.0f - std::exp(-sharpness * dt); }
		float WrapAngle(float a)
		{
			a = std::fmod(a + kPi, 2.0f * kPi);
			if (a < 0.0f) a += 2.0f * kPi;
			return a - kPi;
		}

		float Ground(const CharacterWorld& w, float x, float z) { return w.GroundHeight ? w.GroundHeight(x, z) : 0.0f; }
		float Depth(const CharacterWorld& w, float x, float z)
		{
			if (!w.WaterLevel)
				return 0.0f;
			const float level = w.WaterLevel(x, z);
			return level <= kNoWater ? 0.0f : std::max(0.0f, level - Ground(w, x, z));
		}
	}

	// ------------------------------------------------------------ input
	CharacterInput ReadCharacterInput(float cameraYaw)
	{
		CharacterInput in;
		in.CameraYaw = cameraYaw;
		if (Input::IsKeyDown(Key::W)) in.Move.y += 1.0f;
		if (Input::IsKeyDown(Key::S)) in.Move.y -= 1.0f;
		if (Input::IsKeyDown(Key::D)) in.Move.x += 1.0f;
		if (Input::IsKeyDown(Key::A)) in.Move.x -= 1.0f;
		const glm::vec2 stick(Input::GetGamepadAxis(GamepadAxis::LeftX), -Input::GetGamepadAxis(GamepadAxis::LeftY));
		in.Move += stick;
		in.Jump = Input::IsKeyDown(Key::Space) || Input::IsGamepadButtonDown(GamepadButton::A);
		in.Run = Input::IsKeyDown(Key::LeftShift) || Input::IsGamepadButtonDown(GamepadButton::LeftStick) || glm::length(stick) > 0.9f;
		return in;
	}

	std::vector<CharacterScriptStep> ParseCharacterScript(const std::string& text)
	{
		std::vector<CharacterScriptStep> steps;
		size_t pos = 0;
		while (pos <= text.size())
		{
			size_t end = text.find(',', pos);
			if (end == std::string::npos)
				end = text.size();
			const std::string token = text.substr(pos, end - pos);
			pos = end + 1;
			const size_t colon = token.find(':');
			if (colon == std::string::npos)
				continue;
			CharacterScriptStep step;
			step.Seconds = static_cast<float>(std::atof(token.c_str() + colon + 1));
			for (size_t i = 0; i < colon; ++i)
			{
				switch (token[i])
				{
				case 'W': case 'w': step.Move.y += 1.0f; break;
				case 'S': case 's': step.Move.y -= 1.0f; break;
				case 'D': case 'd': step.Move.x += 1.0f; break;
				case 'A': case 'a': step.Move.x -= 1.0f; break;
				case 'R': case 'r': step.Run = true; break;
				case 'J': case 'j': step.Jump = true; break;
				case 'Q': case 'q': step.CameraTurn -= 1.0f; break;
				case 'E': case 'e': step.CameraTurn += 1.0f; break;
				default: break;
				}
			}
			if (step.Seconds > 0.0f)
				steps.push_back(step);
		}
		return steps;
	}

	const CharacterScriptStep* SampleCharacterScript(const std::vector<CharacterScriptStep>& script, float time)
	{
		for (const CharacterScriptStep& step : script)
		{
			if (time < step.Seconds)
				return &step;
			time -= step.Seconds;
		}
		return nullptr;
	}

	CharacterWorld MakeTerrainCharacterWorld(const Terrain& terrain)
	{
		CharacterWorld world;
		const Terrain* t = &terrain;
		world.GroundHeight = [t](float x, float z) { return t->GetHeight(x, z); };
		world.WaterLevel = [t](float x, float z)
		{
			const float lake = t->HasLake() && t->GetHeight(x, z) < t->GetWaterLevel() ? t->GetWaterLevel() : -1e9f;
			return std::max(lake, t->GetRiverLevelAt(x, z));
		};
		world.BoundsMin = t->GetHeightmap().GetMin() + 1.0f;
		world.BoundsMax = t->GetHeightmap().GetMax() - 1.0f;
		return world;
	}

	glm::vec2 FindFreeSpot(const CharacterWorld& world, const glm::vec2& near, float clearance, float maxRadius)
	{
		auto isFree = [&](const glm::vec2& p)
		{
			if (Depth(world, p.x, p.y) > 0.0f)
				return false;
			for (const CircleObstacle& o : world.Obstacles)
				if (glm::distance(o.Center, p) < o.Radius + clearance)
					return false;
			return p.x >= world.BoundsMin.x && p.y >= world.BoundsMin.y && p.x <= world.BoundsMax.x && p.y <= world.BoundsMax.y;
		};
		if (isFree(near))
			return near;
		for (float r = 1.5f; r < maxRadius; r += 1.5f)
			for (int k = 0; k < 12; ++k)
			{
				const float a = 0.5235988f * static_cast<float>(k);
				const glm::vec2 p = near + r * glm::vec2(std::cos(a), std::sin(a));
				if (isFree(p))
					return p;
			}
		return near;
	}

	// ------------------------------------------------------------ character
	void CharacterController::SetPosition(const CharacterWorld& world, float x, float z, float yawRadians)
	{
		m_Position = { x, Ground(world, x, z), z };
		m_Velocity = glm::vec3(0.0f);
		m_Yaw = WrapAngle(yawRadians);
		m_WadeDepth = Depth(world, x, z);
		m_Blocked = false;
		m_Airborne = m_Landed = m_JumpHeld = false;
		m_VerticalSpeed = m_LandingSpeed = 0.0f;
	}

	void CharacterController::Step(const CharacterInput& input, const CharacterWorld& world, float dt)
	{
		if (dt <= 0.0f)
			return;
		dt = std::min(dt, 0.1f);   // a hitch must not teleport the character through a tree
		const CharacterConfig& c = Config;

		glm::vec2 move = input.Move;
		const float inputLength = glm::length(move);
		if (inputLength > 1.0f)
			move /= inputLength;
		const float amount = std::min(inputLength, 1.0f);

		// Camera-relative wish direction (forward of the camera on the ground plane, and its right).
		const glm::vec2 forward(-std::sin(input.CameraYaw), -std::cos(input.CameraYaw));
		const glm::vec2 right(-forward.y, forward.x);
		const glm::vec2 wish = forward * move.y + right * move.x;
		const bool hasInput = amount > 1e-3f && glm::length(wish) > 1e-4f;

		m_WadeDepth = Depth(world, m_Position.x, m_Position.z);
		const float wade = c.MaxWadeDepth > 0.0f ? glm::clamp(m_WadeDepth / c.MaxWadeDepth, 0.0f, 1.0f) : 0.0f;
		const float speedCap = (input.Run ? c.RunSpeed : c.WalkSpeed) * amount * glm::mix(1.0f, c.WadeSpeedFactor, wade);
		const glm::vec2 wishDir = hasInput ? glm::normalize(wish) : glm::vec2(0.0f);

		// Velocity: constant acceleration toward the target (braking is stronger than starting), so the
		// speed ramps linearly, reversals pass through a brief slide, and the result does not depend on dt.
		glm::vec2 v(m_Velocity.x, m_Velocity.z);
		// Opt-in: standing still and asked to go somewhere well behind or beside, turn first (the animation steps through the turn).
		const float wantedYaw = hasInput ? std::atan2(wishDir.x, wishDir.y) : m_Yaw;
		const bool turnFirst = hasInput && c.TurnInPlaceAngle > 0.0f && !m_Airborne && glm::length(v) < 0.4f
			&& std::fabs(WrapAngle(wantedYaw - m_Yaw)) > glm::radians(c.TurnInPlaceAngle);
		const glm::vec2 delta = (turnFirst ? glm::vec2(0.0f) : wishDir * speedCap) - v;
		const float deltaLength = glm::length(delta);
		const float maxChange = (hasInput ? c.Acceleration : c.Deceleration) * dt * (m_Airborne ? c.AirControl : 1.0f);
		v = deltaLength <= maxChange ? (turnFirst ? glm::vec2(0.0f) : wishDir * speedCap) : v + delta * (maxChange / deltaLength);

		// Heading: turn toward the direction of travel by the shortest way, with a rate cap.
		if (turnFirst)
		{
			const float maxStep = c.TurnInPlaceRate * dt;
			m_Yaw = WrapAngle(m_Yaw + glm::clamp(WrapAngle(wantedYaw - m_Yaw), -maxStep, maxStep));
		}
		else if (hasInput)
		{
			const float diff = WrapAngle(std::atan2(wishDir.x, wishDir.y) - m_Yaw);
			const float maxStep = c.MaxTurnRate * dt;
			m_Yaw = WrapAngle(m_Yaw + glm::clamp(diff * Damp(c.TurnSharpness, dt), -maxStep, maxStep));
		}

		// Move, refusing steps onto steep ground or into deep water (sliding along the axes if one still works).
		const glm::vec2 from(m_Position.x, m_Position.z);
		const float fromDepth = Depth(world, from.x, from.y);
		const float fromGround = Ground(world, from.x, from.y);
		auto allowed = [&](const glm::vec2& to)
		{
			const float dist = glm::distance(from, to);
			if (dist < 1e-6f)
				return true;
			if (Depth(world, to.x, to.y) > std::max(c.MaxWadeDepth, fromDepth))
				return false;
			if (m_Airborne)   // in the air only what is higher than the feet is in the way
				return Ground(world, to.x, to.y) <= m_Position.y + 0.3f;
			return (Ground(world, to.x, to.y) - fromGround) / dist <= c.MaxSlope;
		};
		const glm::vec2 step = v * dt;
		glm::vec2 to = from + step;
		m_Blocked = false;
		if (!allowed(to))
		{
			m_Blocked = true;
			if (allowed(from + glm::vec2(step.x, 0.0f)))
				to = from + glm::vec2(step.x, 0.0f);
			else if (allowed(from + glm::vec2(0.0f, step.y)))
				to = from + glm::vec2(0.0f, step.y);
			else
				to = from;
		}

		// Trees and rocks: push out of the circles; the velocity becomes what actually happened.
		for (int pass = 0; pass < 2; ++pass)
		{
			for (const CircleObstacle& o : world.Obstacles)
			{
				const glm::vec2 d = to - o.Center;
				const float reach = o.Radius + c.Radius;
				const float d2 = glm::dot(d, d);
				if (d2 >= reach * reach)
					continue;
				const float dist = std::sqrt(d2);
				const glm::vec2 n = dist > 1e-5f ? d / dist : glm::vec2(1.0f, 0.0f);
				to = o.Center + n * reach;
				m_Blocked = m_Blocked || reach - dist > 1e-3f;
			}
		}
		to = glm::clamp(to, world.BoundsMin, world.BoundsMax);

		v = (to - from) / dt;
		const float cap = glm::length(wishDir * speedCap);
		if (m_Blocked && glm::length(v) > std::max(cap, glm::length(step) / dt) + 1e-3f)
			v = glm::normalize(v) * std::max(cap, glm::length(step) / dt);   // a shove out of an overlap is not speed
		m_Velocity = { v.x, 0.0f, v.y };

		// Vertical: standing on the ground (climbs snap, descents ease), or a jump / fall under gravity.
		const float g = Ground(world, to.x, to.y);
		m_Landed = false;
		const bool jumpPressed = input.Jump && !m_JumpHeld;
		m_JumpHeld = input.Jump;
		if (!m_Airborne)
		{
			if (jumpPressed && c.JumpSpeed > 0.0f)
			{
				m_Airborne = true;
				m_VerticalSpeed = c.JumpSpeed;
			}
			else if (m_Position.y - g > c.FallDrop)   // walked off an edge
			{
				m_Airborne = true;
				m_VerticalSpeed = 0.0f;
			}
		}
		if (m_Airborne)
		{
			m_VerticalSpeed -= c.Gravity * dt;
			const float y = m_Position.y + m_VerticalSpeed * dt;
			if (m_VerticalSpeed <= 0.0f && y <= g)
			{
				m_Airborne = false;
				m_Landed = true;
				m_LandingSpeed = -m_VerticalSpeed;
				m_VerticalSpeed = 0.0f;
				m_Position = { to.x, g, to.y };
			}
			else
				m_Position = { to.x, std::max(y, g), to.y };
		}
		else
		{
			const float y = m_Position.y + (g - m_Position.y) * Damp(c.GroundSharpness, dt);
			m_Position = { to.x, std::max(y, g), to.y };
		}
		m_WadeDepth = Depth(world, to.x, to.y);
	}

	Locomotion NextLocomotion(Locomotion current, float speed, const LocomotionParams& p)
	{
		switch (current)
		{
		case Locomotion::Idle: return speed > p.WalkToRun ? Locomotion::Run : speed > p.IdleToWalk ? Locomotion::Walk : Locomotion::Idle;
		case Locomotion::Walk: return speed > p.WalkToRun ? Locomotion::Run : speed < p.WalkToIdle ? Locomotion::Idle : Locomotion::Walk;
		case Locomotion::Run:  return speed < p.WalkToIdle ? Locomotion::Idle : speed < p.RunToWalk ? Locomotion::Walk : Locomotion::Run;
		}
		return current;
	}

	CharacterAnimation CharacterAnimation::ForModel(const Model& model)
	{
		CharacterAnimation a;
		a.IdleClip = model.FindClip("Survey");
		a.RunClip = model.FindClip("Run");
		a.WalkClip = std::max(model.FindClip("Walk"), 0);
		if (a.HasLocomotionClips())   // the fox is small: its strides are short
		{
			a.WalkRefSpeed = 1.8f;
			a.RunRefSpeed = 5.0f;
		}
		return a;
	}

	void CharacterAnimation::Start(ModelInstance& instance)
	{
		State = Locomotion::Idle;
		if (HasLocomotionClips())
			instance.Play(IdleClip, true, 0.0f);
		else
			instance.SetSpeed(0.0f);
	}

	void CharacterAnimation::Update(ModelInstance& instance, float groundSpeed)
	{
		const Locomotion next = NextLocomotion(State, groundSpeed);
		if (next != State)
		{
			State = next;
			if (HasLocomotionClips())
				instance.Play(next == Locomotion::Idle ? IdleClip : next == Locomotion::Walk ? WalkClip : RunClip, true, 0.25f);
		}
		if (HasLocomotionClips())
			instance.SetSpeed(State == Locomotion::Idle ? 1.0f : std::clamp(groundSpeed / (State == Locomotion::Walk ? WalkRefSpeed : RunRefSpeed), 0.5f, 1.6f));
		else
			instance.SetSpeed(std::clamp(groundSpeed / WalkRefSpeed, 0.0f, 3.0f));
	}

	// ------------------------------------------------------------ camera
	void ThirdPersonCamera::Rotate(float deltaYaw, float deltaPitch)
	{
		Yaw = WrapAngle(Yaw + deltaYaw);
		Pitch = std::clamp(Pitch + deltaPitch, MinPitch, MaxPitch);
	}

	void ThirdPersonCamera::Zoom(float amount)
	{
		Distance = std::clamp(Distance * std::exp(amount), MinDistance, MaxDistance);
	}

	void ThirdPersonCamera::Snap(const glm::vec3& characterPosition)
	{
		m_Target = characterPosition + glm::vec3(0.0f, TargetHeight, 0.0f);
		m_Effective = ClearDistance();
		m_Initialized = true;
	}

	float ThirdPersonCamera::ClearDistance() const
	{
		if (!GroundHeight)
			return Distance;
		const glm::vec3 dir(std::sin(Yaw) * std::cos(Pitch), std::sin(Pitch), std::cos(Yaw) * std::cos(Pitch));
		auto blocked = [&](float d)
		{
			const glm::vec3 p = m_Target + dir * d;
			return p.y < GroundHeight(p.x, p.z) + GroundMargin;
		};
		constexpr int kSamples = 16;
		float clear = Distance, lastFree = 0.0f;
		for (int i = 1; i <= kSamples; ++i)
		{
			const float d = Distance * static_cast<float>(i) / static_cast<float>(kSamples);
			if (!blocked(d))
			{
				lastFree = d;
				continue;
			}
			float lo = lastFree, hi = d;   // the ground is somewhere between the last free sample and this one
			for (int k = 0; k < 6; ++k)
			{
				const float mid = 0.5f * (lo + hi);
				(blocked(mid) ? hi : lo) = mid;
			}
			clear = lo;
			break;
		}
		return std::max(clear, std::min(0.8f, Distance));
	}

	void ThirdPersonCamera::Update(float dt, const glm::vec3& characterPosition)
	{
		if (!m_Initialized)
			Snap(characterPosition);
		const glm::vec3 wanted = characterPosition + glm::vec3(0.0f, TargetHeight, 0.0f);
		m_Target += (wanted - m_Target) * Damp(FollowSharpness, dt);
		const float clear = ClearDistance();
		m_Effective += (clear - m_Effective) * Damp(clear < m_Effective ? ClearingSharpness : RecoverSharpness, dt);
		m_Effective = std::min(m_Effective, Distance);
	}

	void ThirdPersonCamera::ReadInput(float dt)
	{
		float yaw = 0.0f, pitch = 0.0f;
		if (Input::IsKeyDown(Key::Left) || (TurnWithQE && Input::IsKeyDown(Key::Q))) yaw -= 1.0f;
		if (Input::IsKeyDown(Key::Right) || (TurnWithQE && Input::IsKeyDown(Key::E))) yaw += 1.0f;
		if (Input::IsKeyDown(Key::Up)) pitch += 1.0f;
		if (Input::IsKeyDown(Key::Down)) pitch -= 1.0f;
		yaw += Input::GetGamepadAxis(GamepadAxis::RightX);
		pitch -= Input::GetGamepadAxis(GamepadAxis::RightY);
		Rotate(yaw * RotateSpeed * dt, pitch * RotateSpeed * dt);
		const auto [dx, dy] = Input::GetMouseDelta();
		if (Input::IsMouseDown(MouseButton::Left) || Input::IsMouseDown(MouseButton::Right))
			Rotate(-static_cast<float>(dx) * MouseSensitivity, static_cast<float>(dy) * MouseSensitivity);
		Zoom(-Input::GetScrollDelta() * 0.1f);
	}

	glm::vec3 ThirdPersonCamera::GetPosition() const
	{
		const glm::vec3 dir(std::sin(Yaw) * std::cos(Pitch), std::sin(Pitch), std::cos(Yaw) * std::cos(Pitch));
		glm::vec3 p = m_Target + dir * m_Effective;
		if (GroundHeight)
			p.y = std::max(p.y, GroundHeight(p.x, p.z) + GroundMargin);   // smoothing may lag behind a rising slope; never clip
		return p;
	}

	void ThirdPersonCamera::Apply(Camera& camera) const
	{
		camera.Target = m_Target;
		camera.Position = GetPosition();
		camera.Up = { 0.0f, 1.0f, 0.0f };
		camera.NearPlane = 0.08f;
		camera.FarPlane = 1500.0f;
	}
}
