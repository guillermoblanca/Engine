#pragma once
#include "GB/Window.h"
#include <string>

namespace GB::Editor
{
	// Owns the Dear ImGui context for a window (GLFW + OpenGL 3 backends, docking enabled).
	class ImGuiLayer
	{
	public:
		// iniPath: where the window layout is stored ("" = do not store it).
		ImGuiLayer(Window& window, const std::string& iniPath);
		~ImGuiLayer();
		ImGuiLayer(const ImGuiLayer&) = delete;
		ImGuiLayer& operator=(const ImGuiLayer&) = delete;

		void BeginFrame(float dt, uint32_t width, uint32_t height);
		void EndFrame();      // draws into the framebuffer that is bound (the window's)

	private:
		std::string m_IniPath;
	};
}
