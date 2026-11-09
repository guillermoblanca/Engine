#pragma once
#include "GB/Platform/Input.h"
#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;

namespace GB
{
	struct WindowProps
	{
		std::string Title = "GB Engine";
		uint32_t Width = 1280;
		uint32_t Height = 720;
		bool Visible = true;   // false = hidden window, still a full GL context (headless/CI)
		bool VSync = true;
	};

	// One initialisation of the GLFW library (glfwInit), released with glfwTerminate when the last owner lets go.
	// GLFW itself is process-wide, so windows that exist at the same time must share one context: a window
	// holds a reference to it, and a second window is created with the first one's GetGlfw(). Replaces the
	// hidden reference counter the window code used to keep in a file-scope static.
	class GlfwContext
	{
	public:
		// Null (and logs why) when GLFW cannot be initialised.
		static std::shared_ptr<GlfwContext> Create();
		~GlfwContext();
		GlfwContext(const GlfwContext&) = delete;
		GlfwContext& operator=(const GlfwContext&) = delete;

	private:
		GlfwContext() = default;
	};

	// A GLFW window with an OpenGL 4.5 core context made current on the creating thread.
	class Window
	{
	public:
		// Returns nullptr (and logs why) if GLFW or the OpenGL context cannot be created. `glfw` is the library
		// context to join (another window's GetGlfw()); null starts a new one owned by this window.
		static std::unique_ptr<Window> Create(const WindowProps& props, std::shared_ptr<GlfwContext> glfw = nullptr);
		~Window();
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		void PollEvents();
		void SwapBuffers();
		bool ShouldClose() const;
		void RequestClose();

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		GLFWwindow* GetNative() const { return m_Handle; }
		// The keyboard, mouse and gamepads as this window sees them; updated by PollEvents.
		GlfwInput& GetInput() { return m_Input; }
		const GlfwInput& GetInput() const { return m_Input; }
		const std::shared_ptr<GlfwContext>& GetGlfw() const { return m_Glfw; }

	private:
		Window() = default;
		std::shared_ptr<GlfwContext> m_Glfw;   // declared first: released last, after the native window is gone
		GLFWwindow* m_Handle = nullptr;
		GlfwInput m_Input;
		uint32_t m_Width = 0, m_Height = 0;
	};
}
