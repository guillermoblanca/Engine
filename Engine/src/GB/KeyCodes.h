#pragma once

// Values match GLFW so they can be passed straight through.
namespace GB
{
	enum class Key : int
	{
		Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
		D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
		A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Escape = 256, Enter, Tab, Backspace, Insert, Delete, Right, Left, Down, Up,
		PageUp, PageDown, Home, End,
		F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
		LeftShift = 340, LeftControl, LeftAlt, LeftSuper,
		RightShift, RightControl, RightAlt, RightSuper
	};

	enum class MouseButton : int { Left = 0, Right = 1, Middle = 2 };

	// Layout follows the GLFW gamepad mapping (Xbox naming).
	enum class GamepadButton : int
	{
		A = 0, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide,
		LeftStick, RightStick, DPadUp, DPadRight, DPadDown, DPadLeft
	};

	enum class GamepadAxis : int
	{
		LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger
	};
}
