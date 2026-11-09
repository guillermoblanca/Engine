#pragma once
#include "GB/Input/InputSource.h"
#include "GB/Platform/Window.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// What the command line asked for. Parsed by the entry point into a local value and handed to CreateApplication(),
	// which gives it to the Application it builds: there is no process-wide copy, so a test or a tool can start an
	// Application with any options without touching global state.
	struct LaunchOptions
	{
		uint64_t Frames = 0;        // --frames=N   run N frames then exit (0 = until closed)
		std::string Screenshot;     // --screenshot=file.png   save the last frame
		bool Hidden = false;        // --hidden     hidden window (headless / CI)
		bool NoVSync = false;       // --novsync
		std::string RecordDir;      // --record=dir   write frame_00001.png... with a fixed time step
		float RecordFps = 30.0f;    // --fps=N        time step (1/N s) and rate of the recorded frames
		float StepFps = 0.0f;       // --step=N       every frame advances 1/N s whatever the wall clock says (a scripted test: the same run on a fast or a slow machine)
		std::vector<std::string> Extra;   // any other argument, for the application itself

		// Value of "--key=value" among the extra arguments, or `fallback`.
		std::string GetValue(const std::string& key, const std::string& fallback = "") const;
		bool HasFlag(const std::string& flag) const;   // "--flag"
	};

	struct ApplicationConfig
	{
		LaunchOptions Launch;       // the command line this application was started with (Application::Options())
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
		// Keyboard, mouse and gamepads of this application's window; with no window, an input where nothing is ever pressed.
		const IInputSource& GetInput() const { return m_Window ? static_cast<const IInputSource&>(m_Window->GetInput()) : m_NoInput; }
		// The options this application was started with. Mutable: an application may fill in defaults for what the command line left out.
		LaunchOptions& Options() { return m_Options; }
		const LaunchOptions& Options() const { return m_Options; }

	protected:
		virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
		virtual void OnRender() {}

	private:
		LaunchOptions m_Options;
		NoInput m_NoInput;
		std::unique_ptr<Window> m_Window;
		std::string m_Screenshot;
		bool m_Valid = true;
		bool m_Running = true;
		int m_ExitCode = 0;
		uint64_t m_FrameCount = 0;
		uint64_t m_MaxFrames = 0;
	};

	// To be defined in CLIENT: builds the application for the options the entry point parsed.
	Application* CreateApplication(const LaunchOptions& options);
}
