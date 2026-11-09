#pragma once
#include "GB/InputSource.h"

struct GLFWwindow;

namespace GB
{
	// Polling input for keyboard, mouse and gamepads over one GLFW window. An ordinary object: the Window that
	// owns the GLFW handle owns one of these (Window::GetInput), so two windows have two independent inputs and
	// nothing here is process-wide.
	class GlfwInput final : public IInputSource
	{
	public:
		explicit GlfwInput(GLFWwindow* window = nullptr) : m_Window(window) {}
		// Binds (or, with null, unbinds) the window and forgets the mouse history.
		void Bind(GLFWwindow* window);

		bool IsKeyDown(Key key) const override;
		bool IsMouseDown(MouseButton button) const override;
		std::pair<double, double> GetMousePosition() const override;
		// Valid after the window calls NewFrame()/PostPoll() around its event polling; always zero without a window.
		std::pair<double, double> GetMouseDelta() const override { return { m_DeltaX, m_DeltaY }; }
		float GetScrollDelta() const override { return m_Scroll; }

		bool IsGamepadConnected(int pad = 0) const override;
		bool IsGamepadButtonDown(GamepadButton button, int pad = 0) const override;
		float GetGamepadAxis(GamepadAxis axis, int pad = 0, float deadzone = 0.15f) const override;

		// Frame bookkeeping, called by Window::PollEvents around glfwPollEvents.
		void NewFrame() { m_Scroll = 0.0f; }   // before polling events: clears the scroll accumulator
		void PostPoll();                        // after polling events: computes the mouse delta
		void AddScroll(float amount) { m_Scroll += amount; }

	private:
		GLFWwindow* m_Window = nullptr;
		double m_LastX = 0.0, m_LastY = 0.0, m_DeltaX = 0.0, m_DeltaY = 0.0;
		bool m_HasLast = false;
		float m_Scroll = 0.0f;
	};
}
