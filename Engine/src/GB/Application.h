#pragma once
#include "Window.h"
#include <cstdint>
#include <memory>
#include <string>

namespace GB
{
	// Set by the entry point from the command line before CreateApplication().
	struct LaunchOptions
	{
		uint64_t Frames = 0;        // --frames=N   run N frames then exit (0 = until closed)
		std::string Screenshot;     // --screenshot=file.png   save the last frame
		bool Hidden = false;        // --hidden     hidden window (headless / CI)
		bool NoVSync = false;       // --novsync
	};
	LaunchOptions& GetLaunchOptions();

	struct ApplicationConfig
	{
		std::string Title = "GB Engine";
		uint32_t Width = 1280;
		uint32_t Height = 720;
		bool CreateWindow = false;  // false = pure logic loop, no OpenGL
	};

	class Application
	{
	public:
		explicit Application(const ApplicationConfig& config = {});
		virtual ~Application();

		// Runs until Close(), the window is closed or the frame limit is reached.
		void Run();
		void Close() { m_Running = false; }

		// 0 = unlimited. Lets agents and tests run the engine headless for N frames.
		void SetMaxFrames(uint64_t frames) { m_MaxFrames = frames; }
		void SetScreenshotPath(std::string path) { m_Screenshot = std::move(path); }
		uint64_t GetFrameCount() const { return m_FrameCount; }

		// False when a window was requested but could not be created.
		bool IsValid() const { return m_Valid; }
		Window* GetWindow() const { return m_Window.get(); }

	protected:
		virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
		virtual void OnRender() {}

	private:
		std::unique_ptr<Window> m_Window;
		std::string m_Screenshot;
		bool m_Valid = true;
		bool m_Running = true;
		uint64_t m_FrameCount = 0;
		uint64_t m_MaxFrames = 0;
	};

	// To be defined in CLIENT
	Application* CreateApplication();
}
