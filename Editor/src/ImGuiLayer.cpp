#include "ImGuiLayer.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace GB::Editor
{
	ImGuiLayer::ImGuiLayer(Window& window, const std::string& iniPath) : m_IniPath(iniPath)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
		io.IniFilename = m_IniPath.empty() ? nullptr : m_IniPath.c_str();

		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 3.0f; style.FrameRounding = 3.0f; style.GrabRounding = 3.0f; style.TabRounding = 3.0f;
		style.FramePadding = ImVec2(6.0f, 3.0f); style.ItemSpacing = ImVec2(6.0f, 4.0f); style.WindowBorderSize = 1.0f;
		ImVec4* c = style.Colors;
		c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.105f, 0.12f, 1.0f);
		c[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.095f, 0.11f, 1.0f);
		c[ImGuiCol_Header] = ImVec4(0.20f, 0.28f, 0.40f, 0.7f);
		c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.38f, 0.56f, 0.8f);
		c[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.18f, 0.26f, 1.0f);
		c[ImGuiCol_Tab] = ImVec4(0.13f, 0.16f, 0.22f, 1.0f);
		c[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.32f, 0.48f, 1.0f);
		c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
		c[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.62f, 0.95f, 1.0f);

		ImGui_ImplGlfw_InitForOpenGL(window.GetNative(), true);
		ImGui_ImplOpenGL3_Init("#version 450");
	}

	ImGuiLayer::~ImGuiLayer()
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::BeginFrame(float dt, uint32_t width, uint32_t height)
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGuiIO& io = ImGui::GetIO();
		io.DeltaTime = dt > 1e-5f ? dt : 1.0f / 60.0f;
		// A hidden window reports a zero framebuffer on some platforms: always use the size we render at.
		io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
		ImGui::NewFrame();
	}

	void ImGuiLayer::EndFrame()
	{
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}
}
