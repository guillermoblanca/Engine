#include "Input.h"
#include <GLFW/glfw3.h>
#include <cmath>

namespace GB
{
	static GLFWwindow* s_Window = nullptr;

	void Input::Bind(GLFWwindow* window) { s_Window = window; }

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
