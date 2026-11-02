#include "Input.h"
#include <GLFW/glfw3.h>
#include <cmath>

namespace GB
{
	void GlfwInput::Bind(GLFWwindow* window)
	{
		m_Window = window;
		m_HasLast = false;
		m_DeltaX = m_DeltaY = 0.0;
		m_Scroll = 0.0f;
	}

	void GlfwInput::PostPoll()
	{
		if (!m_Window)
			return;
		double x = 0.0, y = 0.0;
		glfwGetCursorPos(m_Window, &x, &y);
		m_DeltaX = m_HasLast ? x - m_LastX : 0.0;
		m_DeltaY = m_HasLast ? y - m_LastY : 0.0;
		m_LastX = x;
		m_LastY = y;
		m_HasLast = true;
	}

	bool GlfwInput::IsKeyDown(Key key) const
	{
		return m_Window && glfwGetKey(m_Window, static_cast<int>(key)) == GLFW_PRESS;
	}

	bool GlfwInput::IsMouseDown(MouseButton button) const
	{
		return m_Window && glfwGetMouseButton(m_Window, static_cast<int>(button)) == GLFW_PRESS;
	}

	std::pair<double, double> GlfwInput::GetMousePosition() const
	{
		double x = 0.0, y = 0.0;
		if (m_Window)
			glfwGetCursorPos(m_Window, &x, &y);
		return { x, y };
	}

	namespace
	{
		bool ReadPad(int pad, GLFWgamepadstate& state)
		{
			return pad >= 0 && pad <= GLFW_JOYSTICK_LAST &&
				glfwJoystickIsGamepad(pad) && glfwGetGamepadState(pad, &state);
		}
	}

	bool GlfwInput::IsGamepadConnected(int pad) const
	{
		return pad >= 0 && pad <= GLFW_JOYSTICK_LAST && glfwJoystickIsGamepad(pad);
	}

	bool GlfwInput::IsGamepadButtonDown(GamepadButton button, int pad) const
	{
		GLFWgamepadstate state;
		return ReadPad(pad, state) && state.buttons[static_cast<int>(button)] == GLFW_PRESS;
	}

	float GlfwInput::GetGamepadAxis(GamepadAxis axis, int pad, float deadzone) const
	{
		GLFWgamepadstate state;
		if (!ReadPad(pad, state))
			return 0.0f;
		const float v = state.axes[static_cast<int>(axis)];
		return std::fabs(v) < deadzone ? 0.0f : v;
	}
}
