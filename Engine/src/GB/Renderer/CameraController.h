#pragma once
#include "Camera.h"
#include <functional>

namespace GB
{
	// Camera controllers. Each has pure methods (Rotate, Move, ...) that are easy to drive from scripts
	// or tests, plus Update(dt) which polls keyboard, mouse and gamepad through GB::Input.

	// Orbits around a target. Mouse: left drag rotates, middle/right drag pans, wheel zooms.
	// Keyboard: arrows or WASD rotate, Q/E zoom. Gamepad: right stick rotates, triggers zoom.
	//
	// KeyScheme::Walk (the editor): WASD / arrows walk the target over the ground (relative to where the camera looks), Q/E turn, Shift runs, and the wheel
	// zooms towards the point under the cursor (CursorPoint) instead of the orbit centre, so a close look at something far from the target is one scroll away.
	// With Ground set the target stays that far (TargetHeight) above the terrain and the camera never goes under it. TopDown looks straight down.
	class OrbitController
	{
	public:
		enum class KeyScheme { Rotate, Walk };
		KeyScheme Keys = KeyScheme::Rotate;   // Rotate: the original keys (WASD turn, Q/E zoom), kept for the sandbox
		std::function<float(float, float)> Ground;          // terrain height at (x, z); empty = flat, nothing is clamped
		std::function<bool(glm::vec3&)> CursorPoint;         // the ground point under the mouse (false: none); empty = zoom towards the target
		float TargetHeight = 1.0f;                           // how far above the ground the target is kept
		float CameraClearance = 0.6f;                        // the camera stays at least this far above the ground
		float WalkSpeed = 0.7f;                              // fraction of the distance per second (never under MinWalk)
		float MinWalk = 4.0f;                                // metres per second
		float WalkBoost = 3.0f;                              // Shift
		float FollowSharpness = 8.0f;                        // FollowTo: higher = tighter
		bool TopDown = false;                                // straight down; the pitch is not changed by the mouse
		bool Following = false;                              // FollowTo drives the target: the walk keys do nothing

		void Walk(float right, float forward, float seconds, float speedMultiplier = 1.0f);   // right/forward in [-1,1] on the ground plane
		void PanGround(float deltaRight, float deltaForward);                                   // like Pan, but along the ground plane (scaled by the distance)
		void ZoomToward(float amount, const glm::vec3& point);                                 // zoom keeping `point` fixed on screen (as far as the clamps allow)
		void FollowTo(const glm::vec3& goal, float seconds);                                   // the target eases to `goal` (frame-rate independent); orbiting still works
		void SetTopDown(bool on);                                                               // remembers the pitch to come back to
		void KeepAboveGround();                                                                 // target at TargetHeight over the ground, camera over it too

		glm::vec3 Target{ 0.0f };
		float Yaw = 0.0f;          // radians, 0 = camera on +Z looking toward -Z
		float Pitch = 0.2f;        // radians, clamped to (-89, 89) degrees
		float Distance = 8.0f;
		float MinDistance = 0.1f, MaxDistance = 1000.0f;
		float RotateSpeed = 1.5f;  // rad/s for keys and sticks
		float MouseSensitivity = 0.005f;
		float ZoomSpeed = 0.5f;    // fraction of the distance per second (keys) / per wheel notch * 0.1

		void Rotate(float deltaYaw, float deltaPitch);
		void Zoom(float amount);                          // > 0 moves away (multiplicative)
		void Pan(float deltaRight, float deltaUp);        // in camera-plane units scaled by the distance
		void Update(float dt);
		void Apply(Camera& camera) const;
		glm::vec3 GetPosition() const;

	private:
		float m_PitchBeforeTop = 0.3f;
	};

	// Free-flying camera. WASD move, Q/E down/up, Shift = faster, right mouse button + move looks around.
	// Gamepad: left stick moves, right stick looks, triggers move down/up.
	class FlyController
	{
	public:
		glm::vec3 Position{ 0.0f, 2.0f, 8.0f };
		float Yaw = 0.0f;          // radians, 0 = looking toward -Z
		float Pitch = 0.0f;
		float Speed = 5.0f;        // units per second
		float BoostMultiplier = 4.0f;
		float LookSpeed = 2.0f;    // rad/s for sticks
		float MouseSensitivity = 0.003f;

		void Look(float deltaYaw, float deltaPitch);
		// forward/right/up in [-1,1] relative to the view direction; distance = axis * Speed * dt.
		void Move(float forward, float right, float up, float dt, float speedMultiplier = 1.0f);
		void LookAt(const glm::vec3& point);
		void Update(float dt);
		void Apply(Camera& camera) const;
		glm::vec3 GetForward() const;
	};

	// Third-person chase camera: sits at a fixed offset behind a moving target and follows with
	// exponential smoothing (frame-rate independent). Call SetTarget every frame.
	class FollowController
	{
	public:
		glm::vec3 Offset{ 0.0f, 2.0f, 5.0f };    // in the target's local space (+Z = behind)
		glm::vec3 LookOffset{ 0.0f, 1.0f, 0.0f };  // look at target + this
		float PositionSharpness = 6.0f;            // higher = tighter; 0 = no smoothing
		float LookSharpness = 10.0f;

		void SetTarget(const glm::vec3& position, float yawRadians) { m_TargetPos = position; m_TargetYaw = yawRadians; }
		void Snap();               // jump to the desired pose without smoothing
		void Update(float dt);
		void Apply(Camera& camera) const;

	private:
		glm::vec3 DesiredPosition() const;
		glm::vec3 m_TargetPos{ 0.0f };
		float m_TargetYaw = 0.0f;
		glm::vec3 m_Position{ 0.0f, 2.0f, 5.0f };
		glm::vec3 m_Look{ 0.0f };
		bool m_Initialized = false;
	};
}
