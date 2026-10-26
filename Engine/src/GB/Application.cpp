#include "Application.h"
#include "Input.h"
#include "Log.h"
#include "Renderer/Framebuffer.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <filesystem>

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

		const LaunchOptions& options = GetLaunchOptions();
		const bool recording = m_Window && !options.RecordDir.empty() && options.RecordFps > 0.0f;
		if (recording)
		{
			std::filesystem::create_directories(options.RecordDir);
			GB_CORE_INFO("Recording to '{}' at {} fps (fixed time step)", options.RecordDir, options.RecordFps);
		}

		double last = m_Window ? glfwGetTime() : 0.0;
		while (m_Running)
		{
			float dt = 0.0f;
			if (m_Window)
			{
				Input::NewFrame();
				m_Window->PollEvents();
				Input::PostPoll();
				const double now = glfwGetTime();
				dt = static_cast<float>(now - last);
				last = now;
				if (recording)
					dt = 1.0f / options.RecordFps;   // deterministic, independent of render speed
			}

			OnUpdate(dt);
			OnRender();
			++m_FrameCount;

			const bool lastFrame = m_MaxFrames != 0 && m_FrameCount >= m_MaxFrames;
			if (recording)
			{
				char name[32];
				std::snprintf(name, sizeof(name), "frame_%05llu.png", static_cast<unsigned long long>(m_FrameCount));
				SaveBackbufferPNG((std::filesystem::path(options.RecordDir) / name).string().c_str(),
					m_Window->GetWidth(), m_Window->GetHeight());
			}
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
