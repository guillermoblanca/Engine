// T38: the named input actions (GB/Input/InputActions.h). The default bindings give exactly the character and camera input the fixed keys gave
// before (the 3889aee bodies of ReadCharacterInput, ThirdPersonCamera::ReadInput and ReadGameCameraInput are copied below as Legacy* and compared
// with ==) for keyboard, mouse, pad, mixes and the dead zone; assets/data/input.json says the same as DefaultInputActions(); a document that
// rebinds "jump" moves the jump to another key; a mistake changes nothing. ScriptedInput only: no window.
#include "GB/Game/CharacterController.h"
#include "GB/Game/GameCamera.h"
#include "GB/Input/InputActions.h"
#include "GB/Input/InputSource.h"
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
		else std::printf("ok: %s\n", what.c_str());
	}

	std::string ReadFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}
}

using namespace GB;

namespace Legacy
{
	// The fixed keys before T38 (3889aee), verbatim.
	CharacterInput ReadCharacterInput(const IInputSource& input, float cameraYaw)
	{
		CharacterInput in;
		in.CameraYaw = cameraYaw;
		if (input.IsKeyDown(Key::W)) in.Move.y += 1.0f;
		if (input.IsKeyDown(Key::S)) in.Move.y -= 1.0f;
		if (input.IsKeyDown(Key::D)) in.Move.x += 1.0f;
		if (input.IsKeyDown(Key::A)) in.Move.x -= 1.0f;
		const glm::vec2 stick(input.GetGamepadAxis(GamepadAxis::LeftX), -input.GetGamepadAxis(GamepadAxis::LeftY));
		in.Move += stick;
		in.Jump = input.IsKeyDown(Key::Space) || input.IsGamepadButtonDown(GamepadButton::A);
		in.Run = input.IsKeyDown(Key::LeftShift) || input.IsGamepadButtonDown(GamepadButton::LeftStick) || glm::length(stick) > 0.9f;
		return in;
	}

	void ReadInput(ThirdPersonCamera& cam, const IInputSource& input, float dt)
	{
		float yaw = 0.0f, pitch = 0.0f;
		if (input.IsKeyDown(Key::Left) || (cam.TurnWithQE && input.IsKeyDown(Key::Q))) yaw -= 1.0f;
		if (input.IsKeyDown(Key::Right) || (cam.TurnWithQE && input.IsKeyDown(Key::E))) yaw += 1.0f;
		if (input.IsKeyDown(Key::Up)) pitch += 1.0f;
		if (input.IsKeyDown(Key::Down)) pitch -= 1.0f;
		yaw += input.GetGamepadAxis(GamepadAxis::RightX);
		pitch -= input.GetGamepadAxis(GamepadAxis::RightY);
		cam.Rotate(yaw * cam.RotateSpeed * dt, pitch * cam.RotateSpeed * dt);
		const auto [dx, dy] = input.GetMouseDelta();
		if (input.IsMouseDown(MouseButton::Left) || input.IsMouseDown(MouseButton::Right))
			cam.Rotate(-static_cast<float>(dx) * cam.MouseSensitivity, static_cast<float>(dy) * cam.MouseSensitivity);
		cam.Zoom(-input.GetScrollDelta() * 0.1f);
	}

	GameCameraInput ReadGameCameraInput(const IInputSource& input, GameCameraMode mode, bool qeTurn)
	{
		GameCameraInput in;
		auto down = [&input](Key k) { return input.IsKeyDown(k); };
		if (qeTurn || mode == GameCameraMode::Overview)
		{
			if (down(Key::Q)) in.Turn -= 1.0f;
			if (down(Key::E)) in.Turn += 1.0f;
		}
		else
		{
			if (down(Key::Left)) in.Turn -= 1.0f;
			if (down(Key::Right)) in.Turn += 1.0f;
		}
		in.Turn += input.GetGamepadAxis(GamepadAxis::RightX);
		const auto [dx, dy] = input.GetMouseDelta();
		if (input.IsMouseDown(MouseButton::Right)) in.TurnDelta -= static_cast<float>(dx) * 0.005f;
		in.ZoomDelta -= input.GetScrollDelta() * 0.1f;
		if (mode == GameCameraMode::Overview)
		{
			if (down(Key::W) || down(Key::Up)) in.Pan.y += 1.0f;
			if (down(Key::S) || down(Key::Down)) in.Pan.y -= 1.0f;
			if (down(Key::D) || down(Key::Right)) in.Pan.x += 1.0f;
			if (down(Key::A) || down(Key::Left)) in.Pan.x -= 1.0f;
			in.Pan.x += input.GetGamepadAxis(GamepadAxis::LeftX);
			in.Pan.y -= input.GetGamepadAxis(GamepadAxis::LeftY);
			if (down(Key::Z)) in.Zoom -= 1.0f;
			if (down(Key::X)) in.Zoom += 1.0f;
			in.Zoom += 0.5f * (input.GetGamepadAxis(GamepadAxis::RightTrigger, 0, 0.0f) + 1.0f);
			in.Zoom -= 0.5f * (input.GetGamepadAxis(GamepadAxis::LeftTrigger, 0, 0.0f) + 1.0f);
		}
		return in;
	}
}

namespace
{
	bool Same(const CharacterInput& a, const CharacterInput& b)
	{
		return a.Move == b.Move && a.Run == b.Run && a.Jump == b.Jump && a.CameraYaw == b.CameraYaw;
	}
	bool Same(const GameCameraInput& a, const GameCameraInput& b)
	{
		return a.Pan == b.Pan && a.Turn == b.Turn && a.Zoom == b.Zoom && a.TurnDelta == b.TurnDelta && a.ZoomDelta == b.ZoomDelta;
	}

	struct Case
	{
		std::string Name;
		std::function<void(ScriptedInput&)> Set;
	};

	std::vector<Case> Cases()
	{
		using S = ScriptedInput;
		auto pad = [](S& s) { s.SetPadConnected(true); };
		return {
			{ "nothing", [](S&) {} },
			{ "W", [](S& s) { s.SetKey(Key::W, true); } },
			{ "W+S cancel", [](S& s) { s.SetKey(Key::W, true); s.SetKey(Key::S, true); } },
			{ "W+D+Shift", [](S& s) { s.SetKey(Key::W, true); s.SetKey(Key::D, true); s.SetKey(Key::LeftShift, true); } },
			{ "A+S", [](S& s) { s.SetKey(Key::A, true); s.SetKey(Key::S, true); } },
			{ "Space", [](S& s) { s.SetKey(Key::Space, true); } },
			{ "W+Up and A+Left (pan OR)", [](S& s) { s.SetKey(Key::W, true); s.SetKey(Key::Up, true); s.SetKey(Key::A, true); s.SetKey(Key::Left, true); } },
			{ "Left+Q together", [](S& s) { s.SetKey(Key::Left, true); s.SetKey(Key::Q, true); } },
			{ "Right+E+Up", [](S& s) { s.SetKey(Key::Right, true); s.SetKey(Key::E, true); s.SetKey(Key::Up, true); } },
			{ "Q alone", [](S& s) { s.SetKey(Key::Q, true); } },
			{ "E+Down", [](S& s) { s.SetKey(Key::E, true); s.SetKey(Key::Down, true); } },
			{ "all arrows", [](S& s) { s.SetKey(Key::Left, true); s.SetKey(Key::Right, true); s.SetKey(Key::Up, true); s.SetKey(Key::Down, true); } },
			{ "Z", [](S& s) { s.SetKey(Key::Z, true); } },
			{ "X", [](S& s) { s.SetKey(Key::X, true); } },
			{ "Z+X", [](S& s) { s.SetKey(Key::Z, true); s.SetKey(Key::X, true); } },
			{ "pad connected, at rest", pad },
			{ "pad disconnected, axes pushed", [](S& s) { s.SetPadAxis(GamepadAxis::LeftX, 1.0f); s.SetPadAxis(GamepadAxis::RightTrigger, 1.0f); s.SetPadButton(GamepadButton::A, true); } },
			{ "left stick 0.1 (dead zone)", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::LeftX, 0.1f); s.SetPadAxis(GamepadAxis::LeftY, -0.1f); } },
			{ "left stick 0.2 / -0.3", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::LeftX, 0.2f); s.SetPadAxis(GamepadAxis::LeftY, -0.3f); } },
			{ "left stick 0.95 (runs)", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::LeftX, 0.95f); } },
			{ "left stick 0.89 (walks)", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::LeftY, -0.89f); } },
			{ "left stick 0.7 / 0.7 (length 0.99, runs)", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::LeftX, 0.7f); s.SetPadAxis(GamepadAxis::LeftY, 0.7f); } },
			{ "left stick 0.14 / 0.9 (one axis in the dead zone)", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::LeftX, 0.14f); s.SetPadAxis(GamepadAxis::LeftY, 0.9f); } },
			{ "pad A", [pad](S& s) { pad(s); s.SetPadButton(GamepadButton::A, true); } },
			{ "pad left stick button", [pad](S& s) { pad(s); s.SetPadButton(GamepadButton::LeftStick, true); } },
			{ "right stick 0.5 / -0.4", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::RightX, 0.5f); s.SetPadAxis(GamepadAxis::RightY, -0.4f); } },
			{ "right stick 0.12 (dead zone)", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::RightX, 0.12f); } },
			{ "triggers RT 1, LT -1", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::RightTrigger, 1.0f); s.SetPadAxis(GamepadAxis::LeftTrigger, -1.0f); } },
			{ "triggers RT 0.3, LT 0.6", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::RightTrigger, 0.3f); s.SetPadAxis(GamepadAxis::LeftTrigger, 0.6f); } },
			{ "triggers 0.05 (no dead zone)", [pad](S& s) { pad(s); s.SetPadAxis(GamepadAxis::RightTrigger, 0.05f); } },
			{ "left-button drag", [](S& s) { s.SetMouseButton(MouseButton::Left, true); s.SetMouseDelta(12.0, -7.0); } },
			{ "right-button drag", [](S& s) { s.SetMouseButton(MouseButton::Right, true); s.SetMouseDelta(-33.5, 4.25); } },
			{ "middle-button drag", [](S& s) { s.SetMouseButton(MouseButton::Middle, true); s.SetMouseDelta(5.0, 5.0); } },
			{ "mouse moved, no button", [](S& s) { s.SetMouseDelta(40.0, 9.0); } },
			{ "scroll 2", [](S& s) { s.SetScroll(2.0f); } },
			{ "scroll -1.5", [](S& s) { s.SetScroll(-1.5f); } },
			{ "mix: W, Shift, Q, Left, right drag, scroll, left stick, right stick, triggers", [pad](S& s)
				{
					pad(s);
					s.SetKey(Key::W, true); s.SetKey(Key::LeftShift, true); s.SetKey(Key::Q, true); s.SetKey(Key::Left, true);
					s.SetMouseButton(MouseButton::Right, true); s.SetMouseDelta(7.0, -3.0); s.SetScroll(-1.0f);
					s.SetPadAxis(GamepadAxis::LeftX, 0.5f); s.SetPadAxis(GamepadAxis::LeftY, 0.33f);
					s.SetPadAxis(GamepadAxis::RightX, -0.6f); s.SetPadAxis(GamepadAxis::RightY, 0.25f);
					s.SetPadAxis(GamepadAxis::RightTrigger, 0.2f); s.SetPadAxis(GamepadAxis::LeftTrigger, -0.4f);
				} },
			{ "mix: D, Z, Space, pad A, stick against the keys", [pad](S& s)
				{
					pad(s);
					s.SetKey(Key::D, true); s.SetKey(Key::Z, true); s.SetKey(Key::Space, true); s.SetPadButton(GamepadButton::A, true);
					s.SetPadAxis(GamepadAxis::LeftX, -0.8f); s.SetPadAxis(GamepadAxis::LeftY, -0.2f);
				} },
		};
	}

	// Every case, every camera mode, both Q/E settings: the actions over `actions` give what the fixed keys gave.
	void CompareWithLegacy(const InputActions& actions, const std::string& label)
	{
		const float dt = 1.0f / 60.0f;
		int characterOk = 0, thirdOk = 0, gameOk = 0, total = 0;
		for (const Case& c : Cases())
		{
			ScriptedInput input;
			c.Set(input);
			++total;

			const bool character = Same(ReadCharacterInput(input, 0.7f, actions), Legacy::ReadCharacterInput(input, 0.7f));
			if (!character) Check(false, label + ": character, " + c.Name);
			characterOk += character;

			bool third = true;
			for (bool qe : { true, false })
			{
				ThirdPersonCamera a, b;
				a.TurnWithQE = b.TurnWithQE = qe;
				a.Pitch = b.Pitch = 0.4f;
				a.ReadInput(input, dt, actions);
				Legacy::ReadInput(b, input, dt);
				third = third && a.Yaw == b.Yaw && a.Pitch == b.Pitch && a.Distance == b.Distance;
			}
			if (!third) Check(false, label + ": third-person camera, " + c.Name);
			thirdOk += third;

			bool game = true;
			for (GameCameraMode mode : { GameCameraMode::Follow, GameCameraMode::Overview, GameCameraMode::Observe })
				for (bool qe : { true, false })
					game = game && Same(ReadGameCameraInput(input, mode, qe, actions), Legacy::ReadGameCameraInput(input, mode, qe));
			if (!game) Check(false, label + ": game camera, " + c.Name);
			gameOk += game;
		}
		Check(characterOk == total, label + ": ReadCharacterInput gives what the fixed keys gave (" + std::to_string(characterOk) + "/" + std::to_string(total) + " cases)");
		Check(thirdOk == total, label + ": ThirdPersonCamera::ReadInput turns and zooms as before, Q/E on and off (" + std::to_string(thirdOk) + "/" + std::to_string(total) + ")");
		Check(gameOk == total, label + ": ReadGameCameraInput gives what it gave, in follow, overview and observe, Q/E on and off (" + std::to_string(gameOk) + "/" + std::to_string(total) + ")");
	}
}

int main()
{
	const InputActions& defaults = DefaultInputActions();
	for (std::string_view name : { InputAction::Move, InputAction::Jump, InputAction::Run, InputAction::LookLeft, InputAction::LookRight, InputAction::LookUp,
			InputAction::LookDown, InputAction::TurnLeft, InputAction::TurnRight, InputAction::LookStick, InputAction::LookDrag, InputAction::CameraDrag,
			InputAction::Pointer, InputAction::ZoomWheel, InputAction::Pan, InputAction::Zoom })
		if (!defaults.Has(name)) Check(false, "the defaults bind " + std::string(name));
	Check(defaults.Actions.size() == 16, "the defaults bind the 16 actions the game reads");

	// The defaults are the controls of before.
	CompareWithLegacy(defaults, "defaults");
	// The default arguments are the defaults.
	{
		ScriptedInput input;
		input.SetKey(Key::W, true); input.SetKey(Key::Space, true);
		Check(Same(ReadCharacterInput(input, 0.2f), ReadCharacterInput(input, 0.2f, defaults)), "ReadCharacterInput without actions reads the defaults");
	}

	// assets/data/input.json says what DefaultInputActions says (read into an empty set, so nothing comes from the defaults).
	const std::string text = ReadFile(std::string(GB_ASSETS_DIR) + "/data/input.json");
	Check(!text.empty(), "assets/data/input.json is there");
	{
		InputActions fromFile;
		const std::string error = LoadInputActions(text, fromFile);
		if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
		Check(error.empty() && fromFile == defaults, "input.json's \"input\" section says what DefaultInputActions says");
		CompareWithLegacy(fromFile, "input.json");
	}
	{
		InputActions fromFile = defaults;
		Check(LoadInputActionsFile(std::string(GB_ASSETS_DIR) + "/data/input.json", fromFile).empty() && fromFile == defaults, "LoadInputActionsFile reads the same");
		Check(LoadInputActionsFile(std::string(GB_ASSETS_DIR) + "/data/no_such_input.json", fromFile).empty() && fromFile == defaults, "a missing file is not a mistake and changes nothing");
		Check(LoadInputActionsFile(std::string(GB_ASSETS_DIR) + "/data/player.json", fromFile).empty() && fromFile == defaults, "player.json has no \"input\" section today: it changes nothing");
	}

	// Rebinding: jump on J instead of Space / pad A; the rest keeps its bindings.
	{
		InputActions actions = defaults;
		const std::string error = LoadInputActions(R"({ "input": { "actions": { "jump": [ { "keys": [ "J" ] } ] } } })", actions);
		Check(error.empty(), "a document that rebinds jump loads");
		ScriptedInput space, j, padA;
		space.SetKey(Key::Space, true);
		j.SetKey(Key::J, true);
		padA.SetPadConnected(true); padA.SetPadButton(GamepadButton::A, true);
		Check(!ReadCharacterInput(space, 0.0f, actions).Jump && ReadCharacterInput(j, 0.0f, actions).Jump && !ReadCharacterInput(padA, 0.0f, actions).Jump,
			"after it J jumps and Space and pad A do not");
		Check(ReadCharacterInput(j, 0.0f).Jump == false && ReadCharacterInput(space, 0.0f).Jump, "the defaults are untouched (Space still jumps there)");
		ScriptedInput w;
		w.SetKey(Key::W, true);
		Check(ReadCharacterInput(w, 0.0f, actions).Move == glm::vec2(0.0f, 1.0f), "and W still walks");
		InputActions expected = defaults;
		expected.Actions["jump"] = { InputBinding{ glm::vec2(1.0f, 0.0f), { InputTrigger{ InputTriggerKind::Key, static_cast<int>(Key::J) } } } };
		Check(actions == expected, "only jump changed");
	}
	// A pad binding with its own dead zone and a value, read back.
	{
		InputActions actions;
		Check(LoadInputActions(R"({ "input": { "actions": { "steer": [ { "value": [ 2, 0 ], "padAxis": "RightX", "deadzone": 0.5 } ], "fire": [ { "mouse": [ "Middle" ], "pad": [ "RightBumper" ] } ] } } })", actions).empty(),
			"an action of any name loads");
		ScriptedInput in;
		in.SetPadConnected(true);
		in.SetPadAxis(GamepadAxis::RightX, 0.4f);
		Check(actions.Axis(in, "steer") == 0.0f && !actions.Pressed(in, "steer"), "inside its dead zone it reads 0");
		in.SetPadAxis(GamepadAxis::RightX, 0.6f);
		Check(actions.Axis(in, "steer") == 1.2f && actions.Pressed(in, "steer"), "outside it reads value * axis");
		Check(!actions.Pressed(in, "fire"), "fire is not pressed");
		in.SetPadButton(GamepadButton::RightBumper, true);
		Check(actions.Pressed(in, "fire") && actions.Value(in, "fire") == glm::vec2(1.0f, 0.0f), "a pad button presses it");
		Check(actions.Value(in, "nothing bound") == glm::vec2(0.0f) && !actions.Pressed(in, "nothing bound"), "an unknown action reads nothing");
		Check(LoadInputActions(R"({ "input": { "actions": { "fire": [] } } })", actions).empty() && !actions.Pressed(in, "fire"), "an empty list unbinds an action");
	}
	// Mistakes change nothing.
	{
		InputActions actions = defaults;
		Check(!LoadInputActions(R"({ "input": { "actions": { "jump": [ { "keys": [ "Spacebar" ] } ] } } })", actions).empty() && actions == defaults, "an unknown key name is refused");
		Check(!LoadInputActions(R"({ "input": { "actions": { "jump": [ { "keys": [ "J" ], "padAxis": "LeftX" } ] } } })", actions).empty() && actions == defaults, "a binding both digital and analog is refused");
		Check(!LoadInputActions(R"({ "input": { "actions": { "jump": [ { "key": [ "J" ] } ] } } })", actions).empty() && actions == defaults, "an unknown field is refused");
		Check(!LoadInputActions(R"({ "input": { "actions": { "run": [ { "keys": [ "R" ] } ], "jump": [ { "value": "up", "keys": [ "J" ] } ] } } })", actions).empty() && actions == defaults,
			"a mistake in one action changes no other (atomic)");
		Check(!LoadInputActions(R"({ "input": { "bindings": {} } })", actions).empty() && actions == defaults, "an unknown field of the section is refused");
		Check(!LoadInputActions("{ not json", actions).empty() && actions == defaults, "text that is not JSON is refused");
		Check(LoadInputActions(R"({ "controls": { "keys": {} } })", actions).empty() && actions == defaults, "a document without \"input\" changes nothing");
	}

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
