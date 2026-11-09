#pragma once
#include "GB/Input/KeyCodes.h"
#include <array>
#include <cmath>
#include <set>
#include <utility>

namespace GB
{
	// Where a controller reads the keyboard, the mouse and the gamepads from. Controllers take a reference to one of
	// these instead of reaching for a global, so the same camera or character code runs with a window (GlfwInput,
	// GB/Platform/Input.h), without one (NoInput) or fed by a test (ScriptedInput) -- the headless QA CLAUDE.md asks for.
	class IInputSource
	{
	public:
		virtual ~IInputSource() = default;

		virtual bool IsKeyDown(Key key) const = 0;
		virtual bool IsMouseDown(MouseButton button) const = 0;
		virtual std::pair<double, double> GetMousePosition() const = 0;
		// Movement since the previous frame (pixels) and wheel notches this frame.
		virtual std::pair<double, double> GetMouseDelta() const = 0;
		virtual float GetScrollDelta() const = 0;

		virtual bool IsGamepadConnected(int pad = 0) const = 0;
		virtual bool IsGamepadButtonDown(GamepadButton button, int pad = 0) const = 0;
		// [-1,1] (triggers too, as GLFW reports them) with a dead zone applied around zero.
		virtual float GetGamepadAxis(GamepadAxis axis, int pad = 0, float deadzone = 0.15f) const = 0;
	};

	// An input source with nothing pressed: what an application without a window reads.
	class NoInput final : public IInputSource
	{
	public:
		bool IsKeyDown(Key) const override { return false; }
		bool IsMouseDown(MouseButton) const override { return false; }
		std::pair<double, double> GetMousePosition() const override { return { 0.0, 0.0 }; }
		std::pair<double, double> GetMouseDelta() const override { return { 0.0, 0.0 }; }
		float GetScrollDelta() const override { return 0.0f; }
		bool IsGamepadConnected(int) const override { return false; }
		bool IsGamepadButtonDown(GamepadButton, int) const override { return false; }
		float GetGamepadAxis(GamepadAxis, int, float) const override { return 0.0f; }
	};

	// An input source a test or a script drives by hand: press keys, move the mouse, push a stick, and the code under
	// test reads exactly that.
	class ScriptedInput final : public IInputSource
	{
	public:
		void SetKey(Key key, bool down) { if (down) m_Keys.insert(key); else m_Keys.erase(key); }
		void SetMouseButton(MouseButton button, bool down) { if (down) m_Buttons.insert(button); else m_Buttons.erase(button); }
		void SetMousePosition(double x, double y) { m_MouseX = x; m_MouseY = y; }
		void SetMouseDelta(double dx, double dy) { m_DeltaX = dx; m_DeltaY = dy; }
		void SetScroll(float notches) { m_Scroll = notches; }
		void SetPadAxis(GamepadAxis axis, float value) { m_Axes[static_cast<size_t>(axis) % m_Axes.size()] = value; }
		void SetPadButton(GamepadButton button, bool down) { if (down) m_PadButtons.insert(button); else m_PadButtons.erase(button); }
		void SetPadConnected(bool connected) { m_PadConnected = connected; }
		// Forgets the per-frame values (mouse delta, scroll), as a new frame does.
		void NewFrame() { m_DeltaX = m_DeltaY = 0.0; m_Scroll = 0.0f; }

		bool IsKeyDown(Key key) const override { return m_Keys.count(key) != 0; }
		bool IsMouseDown(MouseButton button) const override { return m_Buttons.count(button) != 0; }
		std::pair<double, double> GetMousePosition() const override { return { m_MouseX, m_MouseY }; }
		std::pair<double, double> GetMouseDelta() const override { return { m_DeltaX, m_DeltaY }; }
		float GetScrollDelta() const override { return m_Scroll; }
		bool IsGamepadConnected(int pad) const override { return m_PadConnected && pad == 0; }
		bool IsGamepadButtonDown(GamepadButton button, int pad) const override { return IsGamepadConnected(pad) && m_PadButtons.count(button) != 0; }
		float GetGamepadAxis(GamepadAxis axis, int pad, float deadzone) const override
		{
			if (!IsGamepadConnected(pad)) return 0.0f;
			const float v = m_Axes[static_cast<size_t>(axis) % m_Axes.size()];
			return std::fabs(v) < deadzone ? 0.0f : v;
		}

	private:
		std::set<Key> m_Keys;
		std::set<MouseButton> m_Buttons;
		std::set<GamepadButton> m_PadButtons;
		std::array<float, 8> m_Axes{};
		double m_MouseX = 0.0, m_MouseY = 0.0, m_DeltaX = 0.0, m_DeltaY = 0.0;
		float m_Scroll = 0.0f;
		bool m_PadConnected = false;
	};
}
