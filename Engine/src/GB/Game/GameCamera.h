#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Renderer/Camera.h"
#include <functional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace GB
{
	// Game cameras (H19): the views a player of the clan game uses, as opposed to the editor's orbit and fly cameras.
	//   follow   - third person behind one actor: swings round behind its facing, never goes into a hill (ThirdPersonCamera's ground test)
	//   overview - the clan view: an RTS-like camera over a point of the ground, panned, rotated and zoomed, kept inside the map
	//   observe  - the cinematic view: frames one actor or a group, slowly circling round it
	// GameCameraRig owns the three and blends from one to the next. Everything here is plain math over a ground-height function: no GL, no
	// window, no input polling (ReadGameCameraInput is the one function that reads GB::Input), so the tests drive it frame by frame and the same
	// inputs always give the same poses. The tuning is data: the "cameras" section of a content pack (assets/data/cameras.json), whose values are
	// the defaults below.

	// Angles are in degrees here (that is what the JSON says); the controllers convert.
	struct FollowCameraParams
	{
		float Distance = 5.5f;            // wanted distance to the look point (m)
		float MinDistance = 1.5f, MaxDistance = 14.0f;
		float PitchDegrees = 17.0f;       // above the horizon
		float MinPitchDegrees = -8.0f, MaxPitchDegrees = 70.0f;
		float TargetHeight = 1.3f;        // the camera looks this high above the actor's feet
		float FovDegrees = 50.0f;
		float FollowSharpness = 8.0f;     // how tightly the look point follows the actor (1/s)
		float TurnSharpness = 2.5f;       // how fast the camera swings round behind the actor's facing (1/s)
		float RecenterDelay = 2.0f;       // seconds after the user last turned the camera before it swings back behind
		float RotateSpeed = 2.0f;         // rad/s at full input
		float ZoomSpeed = 1.0f;           // log-distance per second at full input
		float GroundMargin = 0.4f;        // the camera keeps this far above the terrain (m)
		bool operator==(const FollowCameraParams&) const = default;
	};

	struct OverviewCameraParams
	{
		float Distance = 90.0f;           // starting distance from the focus point on the ground (m)
		float MinDistance = 25.0f, MaxDistance = 260.0f;
		float PitchNearDegrees = 38.0f;   // the camera tilts from this when zoomed all the way in...
		float PitchFarDegrees = 68.0f;    // ... to this when zoomed all the way out (looking down on the map)
		float FovDegrees = 45.0f;
		float PanSpeed = 0.9f;            // ground covered per second at full input, times the distance
		float RotateSpeed = 1.2f;         // rad/s at full input
		float ZoomSpeed = 1.2f;           // log-distance per second at full input
		float Sharpness = 7.0f;           // how fast the view catches up with the input (1/s)
		float EdgeMargin = 8.0f;          // the focus stays this far inside the map's edges (m)
		float GroundMargin = 2.0f;
		bool operator==(const OverviewCameraParams&) const = default;
	};

	struct ObserveCameraParams
	{
		float PitchDegrees = 20.0f;
		float FovDegrees = 35.0f;         // a longer lens than the game views: the cinematic look
		float Framing = 1.25f;            // > 1 leaves room round the group
		float Padding = 1.5f;             // added to the group's radius (an actor is not a point)
		float MinDistance = 4.0f, MaxDistance = 120.0f;
		float OrbitDegreesPerSecond = 6.0f;   // the slow turn round the subject (0 = still)
		float TargetHeight = 1.0f;        // the look point sits this high above the group's centre
		float Sharpness = 2.5f;           // smooth on purpose: a cinematic camera does not jerk
		float RotateSpeed = 1.0f;
		float ZoomSpeed = 0.8f;
		float GroundMargin = 0.6f;
		bool operator==(const ObserveCameraParams&) const = default;
	};

	struct GameCameraParams
	{
		FollowCameraParams Follow;
		OverviewCameraParams Overview;
		ObserveCameraParams Observe;
		float TransitionSeconds = 1.2f;   // the blend from one camera to another (0 = cut)
		bool operator==(const GameCameraParams&) const = default;
	};

	// Reads the "cameras" section of a content document into `params`: only the keys present change, everything is checked first and a mistake
	// changes nothing. "" or what is wrong. A document without the section is fine and changes nothing. Unknown keys are mistakes (a typo would
	// otherwise be silently ignored).
	std::string LoadGameCameraParams(const std::string& json, GameCameraParams& params);
	// The whole set as a document with a "cameras" section (every value written: it is also the reference of the defaults).
	std::string GameCameraParamsToJson(const GameCameraParams& params);
	// The "cameras" content section is CameraContentSection in GB/Game/GameContent.h.

	// What the player asks of the camera this frame. Rates are -1..1 (keys, sticks) and are scaled by the speeds and dt; the deltas are
	// already amounts (mouse drag in radians, wheel notches in log-distance).
	struct GameCameraInput
	{
		glm::vec2 Pan{ 0.0f };   // x = right, y = forward (overview)
		float Turn = 0.0f;       // > 0 turns the camera to the right (the view swings left to right)
		float Zoom = 0.0f;       // > 0 moves away
		float TurnDelta = 0.0f;  // radians
		float ZoomDelta = 0.0f;  // log-distance
		bool Any() const { return Pan != glm::vec2(0.0f) || Turn != 0.0f || Zoom != 0.0f || TurnDelta != 0.0f || ZoomDelta != 0.0f; }
	};

	using GroundHeightFn = std::function<float(float, float)>;

	// Third person behind one actor. The look point follows the actor; the yaw swings round behind its facing unless the user turned the view
	// lately (or SetView steers it, as the player's own camera does); the distance shrinks where a hill is in the way (ThirdPersonCamera).
	class FollowCamera
	{
	public:
		FollowCameraParams Params;
		GroundHeightFn GroundHeight;

		// The actor: its feet and its facing (atan2(dirX, dirZ), 0 = +Z, the CharacterController convention). Call every frame.
		void SetSubject(const glm::vec3& feet, float facingYaw) { m_Feet = feet; m_Facing = facingYaw; }
		// Steers the view for this frame only (OrbitController convention, radians): the player's camera decides where it looks.
		void SetView(float yaw, float pitch) { m_ViewYaw = yaw; m_ViewPitch = pitch; m_Steered = true; }
		void Snap();
		void Update(float dt, const GameCameraInput& input);
		void Apply(Camera& camera) const;
		glm::vec3 Position() const { return m_Rig.GetPosition(); }
		glm::vec3 LookPoint() const { return m_Rig.GetTarget(); }
		float Yaw() const { return m_Rig.Yaw; }

	private:
		void Configure();
		ThirdPersonCamera m_Rig;
		glm::vec3 m_Feet{ 0.0f };
		float m_Facing = 0.0f;
		float m_ViewYaw = 0.0f, m_ViewPitch = 0.0f;
		bool m_Steered = false;
		float m_SinceInput = 1e9f;
		bool m_Snapped = false;
	};

	// The clan view: a focus point on the ground, a yaw and a distance; the pitch comes from the distance (lower when close, looking down when
	// far). The wanted state moves with the input and is clamped to the map; the shown state follows it smoothly.
	class OverviewCamera
	{
	public:
		OverviewCameraParams Params;
		GroundHeightFn GroundHeight;

		// The map: the focus stays inside [min, max] shrunk by EdgeMargin. Without bounds it goes anywhere.
		void SetBounds(const glm::vec2& min, const glm::vec2& max) { m_Min = min; m_Max = max; m_HasBounds = true; }
		// Jumps to a place (yaw in radians, OrbitController convention: the camera sits toward (sin yaw, cos yaw) from the focus).
		void Reset(const glm::vec2& focus, float yaw, float distance);
		void Update(float dt, const GameCameraInput& input);
		void Apply(Camera& camera) const;
		glm::vec3 Focus() const;           // on the ground
		glm::vec3 Position() const;
		float Distance() const { return m_Distance; }
		float PitchRadians() const;
		const glm::vec2& WantedFocus() const { return m_WantFocus; }

	private:
		glm::vec2 Clamp(const glm::vec2& p) const;
		float Ground(float x, float z) const { return GroundHeight ? GroundHeight(x, z) : 0.0f; }
		glm::vec2 m_Min{ 0.0f }, m_Max{ 0.0f };
		bool m_HasBounds = false;
		glm::vec2 m_WantFocus{ 0.0f }, m_Focus{ 0.0f };
		float m_WantYaw = 0.0f, m_Yaw = 0.0f;
		float m_WantDistance = 90.0f, m_Distance = 90.0f;
	};

	// The cinematic view: frames the points it is given (one actor or a group) with the lens of the params, circling slowly.
	class ObserveCamera
	{
	public:
		ObserveCameraParams Params;
		GroundHeightFn GroundHeight;

		// The subjects' feet; call every frame. An empty list keeps the last framing.
		void SetGroup(const std::vector<glm::vec3>& feet);
		void Reset(float yaw) { m_Yaw = yaw; m_Snapped = false; }
		void Update(float dt, const GameCameraInput& input);
		void Apply(Camera& camera) const;
		glm::vec3 Position() const;
		glm::vec3 LookPoint() const { return m_Center; }
		float Distance() const { return m_Distance; }
		// The distance that frames a sphere of `radius` with this lens (before the min/max clamp).
		float FramingDistance(float radius) const;

	private:
		glm::vec3 m_WantCenter{ 0.0f }, m_Center{ 0.0f };
		float m_WantRadius = 1.0f;
		float m_Yaw = 0.0f, m_ZoomScale = 1.0f, m_Distance = 8.0f;
		bool m_HasGroup = false, m_Snapped = false;
	};

	enum class GameCameraMode { Follow, Overview, Observe };
	const char* GameCameraModeName(GameCameraMode mode);

	// The three cameras and the blend between them. Feed the subjects (SetFollowSubject / SetObserveGroup), Update, Apply.
	class GameCameraRig
	{
	public:
		GameCameraRig();
		// Sets the parameters of the three cameras (from the content) and the ground they keep above.
		void Configure(const GameCameraParams& params, GroundHeightFn ground);
		void SetBounds(const glm::vec2& min, const glm::vec2& max) { m_Overview.SetBounds(min, max); }

		// Switches camera. The view blends from `from` (what was on screen) over TransitionSeconds; the new camera starts from a place that
		// matches it (the overview over the point `from` looked at, at the same heading). `blend` false cuts.
		void SetMode(GameCameraMode mode, const Camera& from, bool blend = true);
		GameCameraMode Mode() const { return m_Mode; }
		bool Transitioning() const { return m_Blend < 1.0f; }

		void SetFollowSubject(const glm::vec3& feet, float facingYaw) { m_Follow.SetSubject(feet, facingYaw); }
		void SetFollowView(float yaw, float pitch) { m_Follow.SetView(yaw, pitch); }
		void SetObserveGroup(const std::vector<glm::vec3>& feet) { m_Observe.SetGroup(feet); }

		void Update(float dt, const GameCameraInput& input);
		// The camera on screen: the active one, or the blend into it. Never below the ground.
		void Apply(Camera& camera) const { camera = m_Output; }
		const Camera& Output() const { return m_Output; }
		// The point the view is about (the followed actor, the overview focus, the observed group): shadows and weather centre on it.
		glm::vec3 Focus() const;

		FollowCamera& Follow() { return m_Follow; }
		OverviewCamera& Overview() { return m_Overview; }
		ObserveCamera& Observe() { return m_Observe; }

	private:
		Camera Active() const;
		GameCameraParams m_Params;
		GroundHeightFn m_Ground;
		FollowCamera m_Follow;
		OverviewCamera m_Overview;
		ObserveCamera m_Observe;
		GameCameraMode m_Mode = GameCameraMode::Follow;
		Camera m_From, m_Output;
		float m_Blend = 1.0f;
	};

	// Keyboard, mouse and pad for a game camera (needs a window; the rest of this file does not):
	//   overview - WASD / arrows / left stick pan, Q/E or right-mouse drag turn, wheel / Z-X / triggers zoom
	//   follow, observe - Q/E / right stick / right-mouse drag turn, wheel zoom (WASD walk the player, so they are not read)
	GameCameraInput ReadGameCameraInput(GameCameraMode mode);
}
