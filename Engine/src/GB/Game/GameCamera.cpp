#include "GB/Game/GameCamera.h"
#include "GB/Input.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr float kPi = 3.14159265358979f;
		constexpr float kDegToRad = kPi / 180.0f;
		// Frame-rate independent smoothing factor (the same as the other controllers).
		float Damp(float sharpness, float dt) { return sharpness <= 0.0f ? 1.0f : 1.0f - std::exp(-sharpness * dt); }
		float WrapAngle(float a)
		{
			a = std::fmod(a + kPi, 2.0f * kPi);
			if (a < 0.0f) a += 2.0f * kPi;
			return a - kPi;
		}
		// Moves angle `a` toward `b` the short way round.
		float DampAngle(float a, float b, float sharpness, float dt) { return WrapAngle(a + WrapAngle(b - a) * Damp(sharpness, dt)); }
		// The OrbitController direction from the look point to the camera.
		glm::vec3 OrbitDirection(float yaw, float pitch) { return { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) }; }
		// The yaw (OrbitController convention) of a camera at `position` looking at `target`.
		float YawOf(const glm::vec3& position, const glm::vec3& target)
		{
			const glm::vec3 d = position - target;
			return std::fabs(d.x) + std::fabs(d.z) < 1e-6f ? 0.0f : std::atan2(d.x, d.z);
		}
	}

	// ---------------------------------------------------------------- data
	namespace
	{
		// One camera's block: each key names a float of the params. Reading checks every value before anything is written to `out`.
		struct FloatKey { const char* Name; float* Value; float Min; float Max; };

		std::string ReadBlock(const Json& section, const char* block, std::vector<FloatKey> keys)
		{
			const auto it = section.find(block);
			if (it == section.end()) return {};
			if (!it->is_object()) return std::string("cameras.") + block + ": expected an object";
			std::vector<std::pair<float*, float>> read;
			for (const auto& [key, v] : it->items())
			{
				const auto k = std::find_if(keys.begin(), keys.end(), [&](const FloatKey& f) { return key == f.Name; });
				if (k == keys.end()) return std::string("cameras.") + block + ": unknown key '" + key + "'";
				if (!v.is_number()) return std::string("cameras.") + block + "." + key + ": expected a number";
				const float f = v.get<float>();
				if (!(f >= k->Min && f <= k->Max))
					return std::string("cameras.") + block + "." + key + ": " + std::to_string(f) + " is outside " + std::to_string(k->Min) + ".." + std::to_string(k->Max);
				read.emplace_back(k->Value, f);
			}
			for (const auto& [p, f] : read) *p = f;
			return {};
		}

		std::vector<FloatKey> FollowKeys(FollowCameraParams& p)
		{
			return { { "distance", &p.Distance, 0.1f, 1000.0f }, { "minDistance", &p.MinDistance, 0.1f, 1000.0f }, { "maxDistance", &p.MaxDistance, 0.1f, 1000.0f },
				{ "pitch", &p.PitchDegrees, -89.0f, 89.0f }, { "minPitch", &p.MinPitchDegrees, -89.0f, 89.0f }, { "maxPitch", &p.MaxPitchDegrees, -89.0f, 89.0f },
				{ "targetHeight", &p.TargetHeight, -10.0f, 50.0f }, { "fov", &p.FovDegrees, 5.0f, 150.0f }, { "followSharpness", &p.FollowSharpness, 0.0f, 100.0f },
				{ "turnSharpness", &p.TurnSharpness, 0.0f, 100.0f }, { "recenterDelay", &p.RecenterDelay, 0.0f, 3600.0f }, { "rotateSpeed", &p.RotateSpeed, 0.0f, 50.0f },
				{ "zoomSpeed", &p.ZoomSpeed, 0.0f, 50.0f }, { "groundMargin", &p.GroundMargin, 0.0f, 50.0f } };
		}
		std::vector<FloatKey> OverviewKeys(OverviewCameraParams& p)
		{
			return { { "distance", &p.Distance, 0.1f, 5000.0f }, { "minDistance", &p.MinDistance, 0.1f, 5000.0f }, { "maxDistance", &p.MaxDistance, 0.1f, 5000.0f },
				{ "pitchNear", &p.PitchNearDegrees, 1.0f, 89.0f }, { "pitchFar", &p.PitchFarDegrees, 1.0f, 89.0f }, { "fov", &p.FovDegrees, 5.0f, 150.0f },
				{ "panSpeed", &p.PanSpeed, 0.0f, 50.0f }, { "rotateSpeed", &p.RotateSpeed, 0.0f, 50.0f }, { "zoomSpeed", &p.ZoomSpeed, 0.0f, 50.0f },
				{ "sharpness", &p.Sharpness, 0.0f, 100.0f }, { "edgeMargin", &p.EdgeMargin, 0.0f, 1000.0f }, { "groundMargin", &p.GroundMargin, 0.0f, 100.0f } };
		}
		std::vector<FloatKey> ObserveKeys(ObserveCameraParams& p)
		{
			return { { "pitch", &p.PitchDegrees, -89.0f, 89.0f }, { "fov", &p.FovDegrees, 5.0f, 150.0f }, { "framing", &p.Framing, 0.1f, 20.0f },
				{ "padding", &p.Padding, 0.0f, 100.0f }, { "minDistance", &p.MinDistance, 0.1f, 5000.0f }, { "maxDistance", &p.MaxDistance, 0.1f, 5000.0f },
				{ "orbitSpeed", &p.OrbitDegreesPerSecond, -360.0f, 360.0f }, { "targetHeight", &p.TargetHeight, -10.0f, 50.0f }, { "sharpness", &p.Sharpness, 0.0f, 100.0f },
				{ "rotateSpeed", &p.RotateSpeed, 0.0f, 50.0f }, { "zoomSpeed", &p.ZoomSpeed, 0.0f, 50.0f }, { "groundMargin", &p.GroundMargin, 0.0f, 50.0f } };
		}
	}

	std::string LoadGameCameraParams(const std::string& json, GameCameraParams& params)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("cameras");
		if (section == j.end()) return {};
		if (!section->is_object()) return "cameras: expected an object";
		for (const auto& [key, v] : section->items())
			if (key != "follow" && key != "overview" && key != "observe" && key != "transition") return "cameras: unknown key '" + key + "'";
		GameCameraParams read = params;   // the document may change only part of it
		if (std::string e = ReadBlock(*section, "follow", FollowKeys(read.Follow)); !e.empty()) return e;
		if (std::string e = ReadBlock(*section, "overview", OverviewKeys(read.Overview)); !e.empty()) return e;
		if (std::string e = ReadBlock(*section, "observe", ObserveKeys(read.Observe)); !e.empty()) return e;
		if (std::string e = ReadBlock(*section, "transition", { { "seconds", &read.TransitionSeconds, 0.0f, 60.0f } }); !e.empty()) return e;
		// Ranges that only make sense together.
		if (read.Follow.MinDistance > read.Follow.MaxDistance) return "cameras.follow: minDistance is above maxDistance";
		if (read.Follow.MinPitchDegrees > read.Follow.MaxPitchDegrees) return "cameras.follow: minPitch is above maxPitch";
		if (read.Overview.MinDistance > read.Overview.MaxDistance) return "cameras.overview: minDistance is above maxDistance";
		if (read.Observe.MinDistance > read.Observe.MaxDistance) return "cameras.observe: minDistance is above maxDistance";
		params = read;
		return {};
	}

	std::string GameCameraParamsToJson(const GameCameraParams& params)
	{
		GameCameraParams p = params;   // the key tables take non-const pointers; nothing is written through them here
		auto round = [](float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; };   // 0.9f is written as 0.9
		auto block = [&](const std::vector<FloatKey>& keys)
		{
			Json o = Json::object();
			for (const FloatKey& k : keys) o[k.Name] = round(*k.Value);
			return o;
		};
		Json cameras = Json::object();
		cameras["follow"] = block(FollowKeys(p.Follow));
		cameras["overview"] = block(OverviewKeys(p.Overview));
		cameras["observe"] = block(ObserveKeys(p.Observe));
		cameras["transition"] = Json{ { "seconds", round(p.TransitionSeconds) } };
		Json out = Json::object();
		out["cameras"] = cameras;
		return out.dump(2);
	}

	// ---------------------------------------------------------------- Follow
	void FollowCamera::Configure()
	{
		const FollowCameraParams& p = Params;
		m_Rig.GroundHeight = GroundHeight;
		m_Rig.MinDistance = p.MinDistance;
		m_Rig.MaxDistance = p.MaxDistance;
		m_Rig.MinPitch = p.MinPitchDegrees * kDegToRad;
		m_Rig.MaxPitch = p.MaxPitchDegrees * kDegToRad;
		m_Rig.TargetHeight = p.TargetHeight;
		m_Rig.FollowSharpness = p.FollowSharpness;
		m_Rig.GroundMargin = p.GroundMargin;
	}

	void FollowCamera::Snap()
	{
		Configure();
		m_Rig.Distance = std::clamp(Params.Distance, Params.MinDistance, Params.MaxDistance);
		m_Rig.Pitch = std::clamp(Params.PitchDegrees, Params.MinPitchDegrees, Params.MaxPitchDegrees) * kDegToRad;
		m_Rig.Yaw = m_Steered ? m_ViewYaw : WrapAngle(m_Facing + kPi);   // behind the actor
		if (m_Steered) m_Rig.Pitch = std::clamp(m_ViewPitch, m_Rig.MinPitch, m_Rig.MaxPitch);
		m_Rig.Snap(m_Feet);
		m_SinceInput = 1e9f;
		m_Snapped = true;
	}

	void FollowCamera::Update(float dt, const GameCameraInput& input)
	{
		if (!m_Snapped) Snap();
		Configure();
		if (m_Steered)
		{
			// The player's camera turns the view (and walks relative to it): take its angles, keep only our distance and smoothing.
			m_Rig.Yaw = m_ViewYaw;
			m_Rig.Pitch = std::clamp(m_ViewPitch, m_Rig.MinPitch, m_Rig.MaxPitch);
			m_Steered = false;
		}
		else
		{
			const float turn = input.Turn * Params.RotateSpeed * dt + input.TurnDelta;
			if (turn != 0.0f)
			{
				m_Rig.Rotate(turn, 0.0f);
				m_SinceInput = 0.0f;
			}
			else
				m_SinceInput += dt;
			if (m_SinceInput >= Params.RecenterDelay)   // nobody turned it lately: swing round behind the actor
				m_Rig.Yaw = DampAngle(m_Rig.Yaw, WrapAngle(m_Facing + kPi), Params.TurnSharpness, dt);
		}
		m_Rig.Zoom(input.Zoom * Params.ZoomSpeed * dt + input.ZoomDelta);
		m_Rig.Update(dt, m_Feet);
	}

	void FollowCamera::Apply(Camera& camera) const
	{
		m_Rig.Apply(camera);   // position above the ground, near 0.08, far 1500
		camera.FovYDegrees = Params.FovDegrees;
		camera.Orthographic = false;
	}

	// ---------------------------------------------------------------- Overview
	glm::vec2 OverviewCamera::Clamp(const glm::vec2& p) const
	{
		if (!m_HasBounds) return p;
		glm::vec2 lo = m_Min + glm::vec2(Params.EdgeMargin), hi = m_Max - glm::vec2(Params.EdgeMargin);
		for (int i = 0; i < 2; ++i)
			if (lo[i] > hi[i]) lo[i] = hi[i] = 0.5f * (m_Min[i] + m_Max[i]);   // a map smaller than the margins: its middle
		return glm::clamp(p, lo, hi);
	}

	void OverviewCamera::Reset(const glm::vec2& focus, float yaw, float distance)
	{
		m_WantFocus = m_Focus = Clamp(focus);
		m_WantYaw = m_Yaw = WrapAngle(yaw);
		m_WantDistance = m_Distance = std::clamp(distance, Params.MinDistance, Params.MaxDistance);
	}

	float OverviewCamera::PitchRadians() const
	{
		// From the near pitch to the far one along the log of the distance: every wheel notch tilts the same amount.
		const float lo = std::log(std::max(Params.MinDistance, 0.01f)), hi = std::log(std::max(Params.MaxDistance, 0.01f));
		const float t = hi > lo ? std::clamp((std::log(std::max(m_Distance, 0.01f)) - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
		return (Params.PitchNearDegrees + (Params.PitchFarDegrees - Params.PitchNearDegrees) * t) * kDegToRad;
	}

	void OverviewCamera::Update(float dt, const GameCameraInput& input)
	{
		m_WantYaw = WrapAngle(m_WantYaw + input.Turn * Params.RotateSpeed * dt + input.TurnDelta);
		m_WantDistance = std::clamp(m_WantDistance * std::exp(input.Zoom * Params.ZoomSpeed * dt + input.ZoomDelta), Params.MinDistance, Params.MaxDistance);
		// Pan along the ground in the directions the view shows: forward is away from the camera.
		const glm::vec2 forward(-std::sin(m_WantYaw), -std::cos(m_WantYaw));
		const glm::vec2 right(-forward.y, forward.x);
		const glm::vec2 pan = glm::length(input.Pan) > 1.0f ? glm::normalize(input.Pan) : input.Pan;
		m_WantFocus = Clamp(m_WantFocus + (right * pan.x + forward * pan.y) * Params.PanSpeed * m_WantDistance * dt);

		const float k = Damp(Params.Sharpness, dt);
		m_Focus = Clamp(m_Focus + (m_WantFocus - m_Focus) * k);
		m_Yaw = DampAngle(m_Yaw, m_WantYaw, Params.Sharpness, dt);
		m_Distance = std::exp(std::log(m_Distance) + (std::log(m_WantDistance) - std::log(m_Distance)) * k);   // in log: zooming feels even
	}

	glm::vec3 OverviewCamera::Focus() const { return { m_Focus.x, Ground(m_Focus.x, m_Focus.y), m_Focus.y }; }

	glm::vec3 OverviewCamera::Position() const
	{
		glm::vec3 p = Focus() + OrbitDirection(m_Yaw, PitchRadians()) * m_Distance;
		p.y = std::max(p.y, Ground(p.x, p.z) + Params.GroundMargin);   // a mountain between the focus and the camera: go over it
		return p;
	}

	void OverviewCamera::Apply(Camera& camera) const
	{
		camera.Target = Focus();
		camera.Position = Position();
		camera.Up = { 0.0f, 1.0f, 0.0f };
		camera.FovYDegrees = Params.FovDegrees;
		camera.NearPlane = 0.5f;
		camera.FarPlane = 1500.0f;
		camera.Orthographic = false;
	}

	// ---------------------------------------------------------------- Observe
	void ObserveCamera::SetGroup(const std::vector<glm::vec3>& feet)
	{
		if (feet.empty()) return;
		glm::vec3 c(0.0f);
		for (const glm::vec3& p : feet) c += p;
		c /= static_cast<float>(feet.size());
		float r = 0.0f;
		for (const glm::vec3& p : feet) r = std::max(r, glm::length(p - c));
		m_WantCenter = c + glm::vec3(0.0f, Params.TargetHeight, 0.0f);
		m_WantRadius = r + Params.Padding;
		m_HasGroup = true;
	}

	float ObserveCamera::FramingDistance(float radius) const
	{
		const float half = 0.5f * std::clamp(Params.FovDegrees, 5.0f, 150.0f) * kDegToRad;   // the vertical half-angle: the tighter side of a wide frame
		return radius / std::sin(half) * Params.Framing;
	}

	void ObserveCamera::Update(float dt, const GameCameraInput& input)
	{
		m_Yaw = WrapAngle(m_Yaw + Params.OrbitDegreesPerSecond * kDegToRad * dt + input.Turn * Params.RotateSpeed * dt + input.TurnDelta);
		m_ZoomScale = std::clamp(m_ZoomScale * std::exp(input.Zoom * Params.ZoomSpeed * dt + input.ZoomDelta), 0.2f, 5.0f);
		if (!m_HasGroup) return;
		const float wanted = std::clamp(FramingDistance(m_WantRadius) * m_ZoomScale, Params.MinDistance, Params.MaxDistance);
		if (!m_Snapped)
		{
			m_Center = m_WantCenter;
			m_Distance = wanted;
			m_Snapped = true;
			return;
		}
		const float k = Damp(Params.Sharpness, dt);
		m_Center += (m_WantCenter - m_Center) * k;
		m_Distance += (wanted - m_Distance) * k;
	}

	glm::vec3 ObserveCamera::Position() const
	{
		glm::vec3 p = m_Center + OrbitDirection(m_Yaw, Params.PitchDegrees * kDegToRad) * m_Distance;
		if (GroundHeight) p.y = std::max(p.y, GroundHeight(p.x, p.z) + Params.GroundMargin);
		return p;
	}

	void ObserveCamera::Apply(Camera& camera) const
	{
		camera.Target = m_Center;
		camera.Position = Position();
		camera.Up = { 0.0f, 1.0f, 0.0f };
		camera.FovYDegrees = Params.FovDegrees;
		camera.NearPlane = 0.1f;
		camera.FarPlane = 1500.0f;
		camera.Orthographic = false;
	}

	// ---------------------------------------------------------------- Rig
	const char* GameCameraModeName(GameCameraMode mode)
	{
		switch (mode)
		{
		case GameCameraMode::Follow: return "follow";
		case GameCameraMode::Overview: return "overview";
		case GameCameraMode::Observe: return "observe";
		}
		return "?";
	}

	GameCameraRig::GameCameraRig() { Configure(GameCameraParams(), nullptr); }

	void GameCameraRig::Configure(const GameCameraParams& params, GroundHeightFn ground)
	{
		m_Params = params;
		m_Ground = std::move(ground);
		m_Follow.Params = params.Follow;
		m_Follow.GroundHeight = m_Ground;
		m_Overview.Params = params.Overview;
		m_Overview.GroundHeight = m_Ground;
		m_Observe.Params = params.Observe;
		m_Observe.GroundHeight = m_Ground;
	}

	void GameCameraRig::SetMode(GameCameraMode mode, const Camera& from, bool blend)
	{
		m_Mode = mode;
		switch (mode)
		{
		case GameCameraMode::Follow:
			m_Follow.Snap();
			break;
		case GameCameraMode::Overview:
		{
			// Over the point the old view looked at (or, looking at the sky, the ground under the camera), with the same heading.
			const glm::vec3 look = from.Target;
			const glm::vec3 d = from.Target - from.Position;
			const float yaw = YawOf(from.Position, look);
			const float distance = std::max(glm::length(d), m_Params.Overview.Distance);
			m_Overview.Reset({ look.x, look.z }, yaw, distance);
			break;
		}
		case GameCameraMode::Observe:
			m_Observe.Reset(YawOf(from.Position, from.Target));
			break;
		}
		m_From = from;
		m_Blend = blend && m_Params.TransitionSeconds > 0.0f ? 0.0f : 1.0f;
	}

	Camera GameCameraRig::Active() const
	{
		Camera c = m_Output;
		switch (m_Mode)
		{
		case GameCameraMode::Follow: m_Follow.Apply(c); break;
		case GameCameraMode::Overview: m_Overview.Apply(c); break;
		case GameCameraMode::Observe: m_Observe.Apply(c); break;
		}
		return c;
	}

	void GameCameraRig::Update(float dt, const GameCameraInput& input)
	{
		switch (m_Mode)
		{
		case GameCameraMode::Follow: m_Follow.Update(dt, input); break;
		case GameCameraMode::Overview: m_Overview.Update(dt, input); break;
		case GameCameraMode::Observe: m_Observe.Update(dt, input); break;
		}
		const Camera to = Active();
		if (m_Blend < 1.0f)
		{
			m_Blend = std::min(1.0f, m_Blend + dt / m_Params.TransitionSeconds);
			const float t = m_Blend * m_Blend * (3.0f - 2.0f * m_Blend);   // smoothstep: leaves and arrives gently
			m_Output = Camera::Blend(m_From, to, t);
			// Both ends are above the ground, the straight line between them need not be (a hill between the old view and the new one).
			if (m_Ground)
			{
				const float floor = m_Ground(m_Output.Position.x, m_Output.Position.z) + 0.3f;
				if (m_Output.Position.y < floor)
				{
					const glm::vec3 dir = m_Output.GetForward();
					m_Output.Position.y = floor;
					m_Output.Target = m_Output.Position + dir;
				}
			}
		}
		else
			m_Output = to;
	}

	glm::vec3 GameCameraRig::Focus() const
	{
		switch (m_Mode)
		{
		case GameCameraMode::Follow: return m_Follow.LookPoint();
		case GameCameraMode::Overview: return m_Overview.Focus();
		case GameCameraMode::Observe: return m_Observe.LookPoint();
		}
		return glm::vec3(0.0f);
	}

	GameCameraInput ReadGameCameraInput(GameCameraMode mode)
	{
		GameCameraInput in;
		auto down = [](Key k) { return Input::IsKeyDown(k); };
		if (down(Key::Q)) in.Turn -= 1.0f;
		if (down(Key::E)) in.Turn += 1.0f;
		in.Turn += Input::GetGamepadAxis(GamepadAxis::RightX);
		const auto [dx, dy] = Input::GetMouseDelta();
		if (Input::IsMouseDown(MouseButton::Right)) in.TurnDelta -= static_cast<float>(dx) * 0.005f;
		in.ZoomDelta -= Input::GetScrollDelta() * 0.1f;
		if (mode == GameCameraMode::Overview)
		{
			if (down(Key::W) || down(Key::Up)) in.Pan.y += 1.0f;
			if (down(Key::S) || down(Key::Down)) in.Pan.y -= 1.0f;
			if (down(Key::D) || down(Key::Right)) in.Pan.x += 1.0f;
			if (down(Key::A) || down(Key::Left)) in.Pan.x -= 1.0f;
			in.Pan.x += Input::GetGamepadAxis(GamepadAxis::LeftX);
			in.Pan.y -= Input::GetGamepadAxis(GamepadAxis::LeftY);
			if (down(Key::Z)) in.Zoom -= 1.0f;
			if (down(Key::X)) in.Zoom += 1.0f;
			// Triggers report -1 (released) .. 1 (pressed).
			in.Zoom += 0.5f * (Input::GetGamepadAxis(GamepadAxis::RightTrigger, 0, 0.0f) + 1.0f);
			in.Zoom -= 0.5f * (Input::GetGamepadAxis(GamepadAxis::LeftTrigger, 0, 0.0f) + 1.0f);
		}
		return in;
	}
}
