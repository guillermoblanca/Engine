#pragma once
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

	// A GLFW window with an OpenGL 4.5 core context made current on the creating thread.
	class Window
	{
	public:
		// Returns nullptr (and logs why) if GLFW or the OpenGL context cannot be created.
		static std::unique_ptr<Window> Create(const WindowProps& props);
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

	private:
		Window() = default;
		GLFWwindow* m_Handle = nullptr;
		uint32_t m_Width = 0, m_Height = 0;
	};
}
