#pragma once
#include "KeyCodes.h"
#include <utility>

struct GLFWwindow;

namespace GB
{
	// Polling input for keyboard, mouse and gamepads. Bound to a window by Window.
	class Input
	{
	public:
		static void Bind(GLFWwindow* window);

		static bool IsKeyDown(Key key);
		static bool IsMouseDown(MouseButton button);
		static std::pair<double, double> GetMousePosition();
		// Movement since the previous frame (pixels) and wheel notches this frame. Valid after Application
		// calls NewFrame()/PostPoll(); always zero without a window.
		static std::pair<double, double> GetMouseDelta();
		static float GetScrollDelta();

		// Frame bookkeeping, called by Application around PollEvents.
		static void NewFrame();    // before polling events: clears the scroll accumulator
		static void PostPoll();    // after polling events: computes the mouse delta
		static void AddScroll(float amount);

		static bool IsGamepadConnected(int pad = 0);
		static bool IsGamepadButtonDown(GamepadButton button, int pad = 0);
		// Returns [-1,1] (triggers [-1,1] too, as GLFW reports) with a radial-free deadzone applied.
		static float GetGamepadAxis(GamepadAxis axis, int pad = 0, float deadzone = 0.15f);
	};
}
