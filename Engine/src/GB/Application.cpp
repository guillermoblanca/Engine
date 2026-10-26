#include "Application.h"
#include "Log.h"
#include "Renderer/Framebuffer.h"
#include <GLFW/glfw3.h>

namespace GB
{
	std::string LaunchOptions::GetValue(const std::string& key, const std::string& fallback) const
	{
		const std::string prefix = "--" + key + "=";
		for (const std::string& arg : Extra)
			if (arg.compare(0, prefix.size(), prefix) == 0)
				return arg.substr(prefix.size());
		return fallback;
	}

	bool LaunchOptions::HasFlag(const std::string& flag) const
	{
		for (const std::string& arg : Extra)
			if (arg == "--" + flag)
				return true;
		return false;
	}

	LaunchOptions& GetLaunchOptions()
	{
		static LaunchOptions options;
		return options;
	}

	Application::Application(const ApplicationConfig& config)
	{
		const LaunchOptions& options = GetLaunchOptions();
		m_MaxFrames = options.Frames;
		m_Screenshot = options.Screenshot;

		if (config.CreateWindow)
		{
			WindowProps props;
			props.Title = config.Title;
			props.Width = config.Width;
			props.Height = config.Height;
			props.Visible = !options.Hidden;
			props.VSync = !options.NoVSync;
			m_Window = Window::Create(props);
			m_Valid = m_Window != nullptr;
		}
	}

	Application::~Application() = default;

	void Application::Run()
	{
		if (!m_Valid)
			return;

		double last = m_Window ? glfwGetTime() : 0.0;
		while (m_Running)
		{
			float dt = 0.0f;
			if (m_Window)
			{
				m_Window->PollEvents();
				const double now = glfwGetTime();
				dt = static_cast<float>(now - last);
				last = now;
			}

			OnUpdate(dt);
			OnRender();
			++m_FrameCount;

			const bool lastFrame = m_MaxFrames != 0 && m_FrameCount >= m_MaxFrames;
			if (m_Window)
			{
				if (lastFrame && !m_Screenshot.empty())
				{
					SaveBackbufferPNG(m_Screenshot.c_str(), m_Window->GetWidth(), m_Window->GetHeight());
					GB_CORE_INFO("Screenshot saved to {}", m_Screenshot);
				}
				m_Window->SwapBuffers();
				if (m_Window->ShouldClose())
					m_Running = false;
			}
			if (lastFrame)
				m_Running = false;
		}
		GB_CORE_INFO("Main loop finished after {} frames", m_FrameCount);
	}
}
