#pragma once
#include "Window.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// Set by the entry point from the command line before CreateApplication().
	struct LaunchOptions
	{
		uint64_t Frames = 0;        // --frames=N   run N frames then exit (0 = until closed)
		std::string Screenshot;     // --screenshot=file.png   save the last frame
		bool Hidden = false;        // --hidden     hidden window (headless / CI)
		bool NoVSync = false;       // --novsync
		std::string RecordDir;      // --record=dir   write frame_00001.png... with a fixed time step
		float RecordFps = 30.0f;    // --fps=N        time step (1/N s) and rate of the recorded frames
		std::vector<std::string> Extra;   // any other argument, for the application itself

		// Value of "--key=value" among the extra arguments, or `fallback`.
		std::string GetValue(const std::string& key, const std::string& fallback = "") const;
		bool HasFlag(const std::string& flag) const;   // "--flag"
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
		// What main returns: 0, or what a run (a scripted test) set with SetExitCode.
		void SetExitCode(int code) { m_ExitCode = code; }
		int ExitCode() const { return m_ExitCode; }

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
		int m_ExitCode = 0;
		uint64_t m_FrameCount = 0;
		uint64_t m_MaxFrames = 0;
	};

	// To be defined in CLIENT
	Application* CreateApplication();
}
