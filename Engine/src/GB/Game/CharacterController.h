#pragma once
#include "GB/Assets/ModelInstance.h"
#include "GB/Renderer/Camera.h"
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// A third-person character: acceleration, turning toward the direction of travel, terrain following, and
	// collision against trees/rocks (XZ circles) and deep water. Everything is a pure step over plain data and
	// callbacks, so it runs without a window or GL (see tests/character_test.cpp); ReadInput() is the only
	// part that touches GB::Input.

	struct CharacterInput
	{
		glm::vec2 Move{ 0.0f };   // x = right, y = forward, relative to the camera; any length (clamped to 1)
		bool Run = false;
		bool Jump = false;        // held; the jump starts on the press (edge) while standing on the ground
		float CameraYaw = 0.0f;   // yaw of the OrbitController convention: the camera sits at (sin yaw, cos yaw) from the target
	};

	// Keyboard (WASD, Shift = run) and left stick (past ~90% = run, or hold the stick button). Zero without a window.
	CharacterInput ReadCharacterInput(float cameraYaw);

	// A timeline of input for headless runs and tests: "WD:1.5,RW:2,:0.5" = hold W+D for 1.5 s, then R(un)+W for 2 s,
	// then nothing for 0.5 s. W A S D move, R runs, J jumps, Q / E turn the camera left / right; the keys come before the colon.
	struct CharacterScriptStep
	{
		float Seconds = 1.0f;
		glm::vec2 Move{ 0.0f };
		bool Run = false;
		bool Jump = false;
		float CameraTurn = 0.0f;   // -1..1: fraction of the camera's rotate speed
	};
	std::vector<CharacterScriptStep> ParseCharacterScript(const std::string& text);
	// The step active at `time` seconds, or nullptr after the end.
	const CharacterScriptStep* SampleCharacterScript(const std::vector<CharacterScriptStep>& script, float time);

	struct CircleObstacle
	{
		glm::vec2 Center{ 0.0f };   // xz
		float Radius = 0.5f;
	};

	class NavGrid;   // GB/Navigation/NavGrid.h

	// What the character can stand on and bump into. Every callback is optional.
	struct CharacterWorld
	{
		std::function<float(float, float)> GroundHeight;   // terrain height at x,z (default 0)
		std::function<float(float, float)> WaterLevel;     // surface height, or <= -1e8 where there is no water
		std::vector<CircleObstacle> Obstacles;
		glm::vec2 BoundsMin{ -1e9f }, BoundsMax{ 1e9f };    // the character stays inside (xz)
		const NavGrid* Nav = nullptr;                       // where an AIDriver finds paths (null: it walks in a straight line); must outlive the world
		size_t DynamicFrom = static_cast<size_t>(-1);       // Obstacles from this index on are other characters (moving): the AIDriver steers around them
	};

	class Terrain;
	// Ground, water (lake and river) and the map edges of a terrain. The Terrain must outlive the result.
	// Obstacles are left for the caller (trees and rocks come from different places).
	CharacterWorld MakeTerrainCharacterWorld(const Terrain& terrain);
	// The nearest spot to `near` (searching outward) on dry ground, at least `clearance` m from every obstacle.
	// Returns `near` itself when nothing is found within `maxRadius`.
	glm::vec2 FindFreeSpot(const CharacterWorld& world, const glm::vec2& near, float clearance, float maxRadius = 60.0f);

	struct CharacterConfig
	{
		float WalkSpeed = 1.7f;        // m/s
		float RunSpeed = 4.6f;
		float Acceleration = 14.0f;    // m/s^2 toward the wanted velocity
		float Deceleration = 18.0f;    // m/s^2 when there is no input
		float TurnSharpness = 12.0f;   // exponential approach to the heading (1/s)
		float MaxTurnRate = 14.0f;     // rad/s cap, so reversing is a quick but visible turn
		float Radius = 0.35f;          // collision radius against obstacles
		float MaxWadeDepth = 1.0f;     // water deeper than this (surface - ground) is a wall
		float WadeSpeedFactor = 0.55f; // speed multiplier at full wading depth
		float MaxSlope = 0.85f;        // max rise over run of the ground the character walks onto (~40 degrees)
		float GroundSharpness = 22.0f; // smoothing of the height (hides terrain facets); never below the ground
		float JumpSpeed = 4.2f;        // m/s upward at take-off (with Gravity 12: 0.73 m high, 0.7 s in the air)
		float Gravity = 12.0f;         // m/s^2
		float AirControl = 0.3f;       // fraction of the ground acceleration available in the air
		float FallDrop = 0.6f;         // ground this far below the feet: the character falls instead of easing down
		float TurnInPlaceAngle = 0.0f; // degrees; > 0: from a standstill the character first turns on the spot when the wanted direction is further than this (0 = off)
		float TurnInPlaceRate = 3.0f;  // rad/s of that turn (a turn clip plays at about this speed)
	};

	class CharacterController
	{
	public:
		CharacterConfig Config;

		void SetPosition(const CharacterWorld& world, float x, float z, float yawRadians);
		void Step(const CharacterInput& input, const CharacterWorld& world, float dt);

		const glm::vec3& GetPosition() const { return m_Position; }
		const glm::vec3& GetVelocity() const { return m_Velocity; }   // xz plane (y = 0)
		float GetSpeed() const { return glm::length(m_Velocity); }
		float GetYaw() const { return m_Yaw; }                        // facing; atan2(dirX, dirZ), 0 = +Z
		glm::vec3 GetHeading() const { return { std::sin(m_Yaw), 0.0f, std::cos(m_Yaw) }; }
		float GetWadeDepth() const { return m_WadeDepth; }
		bool WasBlocked() const { return m_Blocked; }                 // the last step pushed against something
		bool IsAirborne() const { return m_Airborne; }                // jumping or falling
		float GetVerticalSpeed() const { return m_VerticalSpeed; }    // m/s, positive up
		bool JustLanded() const { return m_Landed; }                  // touched the ground in the last step
		float GetLandingSpeed() const { return m_LandingSpeed; }      // downward m/s of that landing

	private:
		glm::vec3 m_Position{ 0.0f };
		glm::vec3 m_Velocity{ 0.0f };
		float m_Yaw = 0.0f;
		float m_WadeDepth = 0.0f;
		bool m_Blocked = false;
		bool m_Airborne = false, m_Landed = false, m_JumpHeld = false;
		float m_VerticalSpeed = 0.0f, m_LandingSpeed = 0.0f;
	};

	// Locomotion state with hysteresis, so the animation does not flicker around a threshold.
	enum class Locomotion { Idle, Walk, Run };

	struct LocomotionParams
	{
		float IdleToWalk = 0.25f, WalkToIdle = 0.12f;   // m/s
		float WalkToRun = 3.1f, RunToWalk = 2.6f;
	};

	Locomotion NextLocomotion(Locomotion current, float speed, const LocomotionParams& p = {});

	// Picks idle / walk / run clips of a model and plays them from the ground speed: cross-fades when the state
	// changes and scales the playback rate so the feet do not skate. A model with a single clip (CesiumMan) just
	// plays it faster the faster the character moves, and freezes when it stops.
	struct CharacterAnimation
	{
		int IdleClip = -1, WalkClip = 0, RunClip = -1;   // "Survey", "Walk", "Run" when the model has them
		float WalkRefSpeed = 1.5f, RunRefSpeed = 4.6f;   // ground speed (m/s) at which a clip plays at its natural rate
		Locomotion State = Locomotion::Idle;

		static CharacterAnimation ForModel(const Model& model);
		bool HasLocomotionClips() const { return IdleClip >= 0 && RunClip >= 0; }
		void Start(ModelInstance& instance);                       // idle pose
		void Update(ModelInstance& instance, float groundSpeed);   // once per frame, before instance.Update
	};

	// Third-person orbit camera: the target follows the character smoothly, the mouse/stick turn it, and it never
	// sinks into the ground (it moves closer to the character, and above the terrain).
	class ThirdPersonCamera
	{
	public:
		float Yaw = 0.0f;                  // OrbitController convention
		float Pitch = 0.3f;                // radians above the horizon
		float Distance = 5.0f;             // wanted distance
		float MinDistance = 1.5f, MaxDistance = 25.0f;
		float MinPitch = -0.15f, MaxPitch = 1.3f;
		float RotateSpeed = 2.0f;          // rad/s for keys and sticks
		float MouseSensitivity = 0.005f;
		bool TurnWithQE = true;            // Q / E turn the camera as well as the arrows (a game that uses E to interact turns it off)
		float TargetHeight = 1.3f;         // the camera looks this high above the character's feet
		float FollowSharpness = 9.0f;
		float ClearingSharpness = 14.0f;   // how fast the camera moves in when the ground is in the way
		float RecoverSharpness = 2.5f;     // ... and out again once it is clear
		float GroundMargin = 0.35f;
		std::function<float(float, float)> GroundHeight;   // terrain height; optional

		void Rotate(float deltaYaw, float deltaPitch);
		void Zoom(float amount);           // > 0 moves away
		// Moves the target toward the character and works out the collision-free distance.
		void Update(float dt, const glm::vec3& characterPosition);
		void Snap(const glm::vec3& characterPosition);
		// Mouse drag (left or right button), arrows, right stick, wheel. Not WASD: those walk.
		void ReadInput(float dt);
		void Apply(Camera& camera) const;

		glm::vec3 GetPosition() const;
		const glm::vec3& GetTarget() const { return m_Target; }
		float GetEffectiveDistance() const { return m_Effective; }

	private:
		float ClearDistance() const;
		glm::vec3 m_Target{ 0.0f };
		float m_Effective = 5.0f;
		bool m_Initialized = false;
	};
}
