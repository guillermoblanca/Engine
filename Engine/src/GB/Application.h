#pragma once
#include <cstdint>

namespace GB
{
	class Application
	{
	public:
		Application();
		virtual ~Application();

		// Runs the main loop until Close() is called or the frame limit is reached.
		void Run();
		void Close() { m_Running = false; }

		// 0 = unlimited. Lets agents and tests run the engine headless for N frames.
		void SetMaxFrames(uint64_t frames) { m_MaxFrames = frames; }
		uint64_t GetFrameCount() const { return m_FrameCount; }

	protected:
		virtual void OnUpdate() {}

	private:
		bool m_Running = true;
		uint64_t m_FrameCount = 0;
		uint64_t m_MaxFrames = 0;
	};

	// To be defined in CLIENT
	Application* CreateApplication();
}
