#include "Input.h"
#include <GLFW/glfw3.h>
#include <cmath>

namespace GB
{
	static GLFWwindow* s_Window = nullptr;

	static double s_LastX = 0.0, s_LastY = 0.0, s_DeltaX = 0.0, s_DeltaY = 0.0;
	static bool s_HasLast = false;
	static float s_Scroll = 0.0f;

	void Input::Bind(GLFWwindow* window)
	{
		s_Window = window;
		s_HasLast = false;
		s_DeltaX = s_DeltaY = 0.0;
		s_Scroll = 0.0f;
	}

	void Input::NewFrame() { s_Scroll = 0.0f; }
	void Input::AddScroll(float amount) { s_Scroll += amount; }
	float Input::GetScrollDelta() { return s_Scroll; }
	std::pair<double, double> Input::GetMouseDelta() { return { s_DeltaX, s_DeltaY }; }

	void Input::PostPoll()
	{
		if (!s_Window)
			return;
		double x = 0.0, y = 0.0;
		glfwGetCursorPos(s_Window, &x, &y);
		s_DeltaX = s_HasLast ? x - s_LastX : 0.0;
		s_DeltaY = s_HasLast ? y - s_LastY : 0.0;
		s_LastX = x;
		s_LastY = y;
		s_HasLast = true;
	}

	bool Input::IsKeyDown(Key key)
	{
		return s_Window && glfwGetKey(s_Window, static_cast<int>(key)) == GLFW_PRESS;
	}

	bool Input::IsMouseDown(MouseButton button)
	{
		return s_Window && glfwGetMouseButton(s_Window, static_cast<int>(button)) == GLFW_PRESS;
	}

	std::pair<double, double> Input::GetMousePosition()
	{
		double x = 0.0, y = 0.0;
		if (s_Window)
			glfwGetCursorPos(s_Window, &x, &y);
		return { x, y };
	}

	static bool ReadPad(int pad, GLFWgamepadstate& state)
	{
		return pad >= 0 && pad <= GLFW_JOYSTICK_LAST &&
			glfwJoystickIsGamepad(pad) && glfwGetGamepadState(pad, &state);
	}

	bool Input::IsGamepadConnected(int pad)
	{
		return pad >= 0 && pad <= GLFW_JOYSTICK_LAST && glfwJoystickIsGamepad(pad);
	}

	bool Input::IsGamepadButtonDown(GamepadButton button, int pad)
	{
		GLFWgamepadstate state;
		return ReadPad(pad, state) && state.buttons[static_cast<int>(button)] == GLFW_PRESS;
	}

	float Input::GetGamepadAxis(GamepadAxis axis, int pad, float deadzone)
	{
		GLFWgamepadstate state;
		if (!ReadPad(pad, state))
			return 0.0f;
		const float v = state.axes[static_cast<int>(axis)];
		return std::fabs(v) < deadzone ? 0.0f : v;
	}
}
