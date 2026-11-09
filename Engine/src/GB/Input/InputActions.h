#pragma once
#include "GB/Input/KeyCodes.h"
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <glm/vec2.hpp>

namespace GB
{
	class IInputSource;

	// Named actions over an IInputSource (T38). The game reads "move", "jump", "look_left"... instead of W, Space or the
	// left arrow, and which keys, mouse buttons, pad buttons and sticks drive each action is data: the "input" section
	// of assets/data/input.json (LoadInputActions), whose values are the compiled DefaultInputActions(). Plain data
	// read over a source the caller passes in: no global state, no window, no GLFW (tests drive it with ScriptedInput).
	//
	// Every action has a 2D value (a button or a 1D axis uses x) that is the sum of its bindings, in order:
	//   - a digital binding adds its Value once when ANY of its triggers is down (W and Up in the same binding give 1,
	//     not 2; W and S in two bindings cancel out);
	//   - an analog binding adds (raw + Offset) * Value: a pad axis (with its dead zone), the mouse delta (x and y
	//     separately) or the wheel.
	// An action is pressed when one of its bindings is active (a digital trigger down, an analog contribution not zero).

	// What a digital binding listens to.
	enum class InputTriggerKind : int
	{
		Key,          // Code = Key
		Mouse,        // Code = MouseButton
		PadButton,    // Code = GamepadButton (pad 0)
		StickAbove    // Code = 0 left stick, 1 right stick: down while its length (dead zone per axis) is above Above
	};

	struct InputTrigger
	{
		InputTriggerKind Kind = InputTriggerKind::Key;
		int Code = 0;
		float Above = 0.9f;        // StickAbove only
		float DeadZone = 0.15f;    // StickAbove only
		bool operator==(const InputTrigger&) const = default;
	};

	enum class InputAnalog : int
	{
		None,         // a digital binding
		PadAxis,      // Axis = GamepadAxis (pad 0), raw in [-1, 1] with DeadZone around zero
		MouseDelta,   // raw = the mouse movement this frame (pixels), x and y
		Scroll        // raw = the wheel notches this frame
	};

	struct InputBinding
	{
		glm::vec2 Value{ 1.0f, 0.0f };
		std::vector<InputTrigger> Any;    // digital: Value when any of these is down
		InputAnalog Analog = InputAnalog::None;
		int Axis = 0;                     // PadAxis: the GamepadAxis
		float Offset = 0.0f;              // analog: (raw + Offset) * Value (a trigger, -1 released .. 1 pressed, + 1 then * 0.5 is 0..1)
		float DeadZone = 0.15f;           // PadAxis
		bool operator==(const InputBinding&) const = default;
	};

	// The names the game reads (the defaults bind them as the controls were before T38).
	namespace InputAction
	{
		// The character (ReadCharacterInput).
		inline constexpr std::string_view Move = "move";              // 2D: WASD, left stick
		inline constexpr std::string_view Jump = "jump";              // Space, pad A
		inline constexpr std::string_view Run = "run";                // Left Shift, left stick button, left stick pushed past 90%
		// The cameras (ThirdPersonCamera::ReadInput, ReadGameCameraInput).
		inline constexpr std::string_view LookLeft = "look_left";     // arrows
		inline constexpr std::string_view LookRight = "look_right";
		inline constexpr std::string_view LookUp = "look_up";
		inline constexpr std::string_view LookDown = "look_down";
		inline constexpr std::string_view TurnLeft = "turn_left";     // Q / E (a game that uses E to interact turns them off)
		inline constexpr std::string_view TurnRight = "turn_right";
		inline constexpr std::string_view LookStick = "look_stick";   // 2D: right stick (x right, y up)
		inline constexpr std::string_view LookDrag = "look_drag";     // the mouse buttons that turn the third-person camera when dragged
		inline constexpr std::string_view CameraDrag = "camera_drag"; // ... and the game cameras
		inline constexpr std::string_view Pointer = "pointer";        // 2D: the mouse movement this frame (pixels)
		inline constexpr std::string_view ZoomWheel = "zoom_wheel";   // the wheel (notches)
		inline constexpr std::string_view Pan = "pan";                // 2D: the overview's pan (WASD / arrows / left stick)
		inline constexpr std::string_view Zoom = "zoom";              // the overview's zoom (Z / X / triggers)
	}

	class InputActions
	{
	public:
		std::map<std::string, std::vector<InputBinding>, std::less<>> Actions;

		glm::vec2 Value(const IInputSource& input, std::string_view action) const;
		float Axis(const IInputSource& input, std::string_view action) const { return Value(input, action).x; }
		bool Pressed(const IInputSource& input, std::string_view action) const;
		bool Has(std::string_view action) const { return Actions.find(action) != Actions.end(); }

		bool operator==(const InputActions&) const = default;
	};

	// The bindings the game ships with (assets/data/input.json says the same). A constant built once.
	const InputActions& DefaultInputActions();

	// { "input": { "actions": { "<action>": [ <binding>, ... ] } } } where a binding is
	//   digital: { "value": [x, y] | x, "keys": [ "W", "Up" ], "mouse": [ "Left" ], "pad": [ "A" ], "stickAbove": { "stick": "Left", "above": 0.9, "deadzone": 0.15 } }
	//   analog:  { "value": [x, y] | x, "padAxis": "LeftX", "deadzone": 0.15, "offset": 0 } | { "mouseDelta": true } | { "scroll": true }
	// ("value" defaults to [1, 0], [1, 1] for the mouse delta). Names are those of KeyCodes.h (Key, MouseButton, GamepadButton, GamepadAxis).
	// "actions" replaces the actions it names (an empty list unbinds one); the others keep their bindings. A document without "input" changes
	// nothing (so the same call reads input.json or a player's settings file); on a mistake (an unknown name or field, a wrong type) it returns
	// why and changes nothing.
	std::string LoadInputActions(const std::string& json, InputActions& actions);
	// Reads a file with LoadInputActions. A missing file is not a mistake: it returns "" and changes nothing.
	std::string LoadInputActionsFile(const std::string& path, InputActions& actions);
}
