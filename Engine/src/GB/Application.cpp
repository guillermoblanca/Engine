#include "Application.h"
#include "Log.h"

namespace GB
{
	Application::Application() = default;
	Application::~Application() = default;

	void Application::Run()
	{
		while (m_Running)
		{
			OnUpdate();
			++m_FrameCount;

			if (m_MaxFrames != 0 && m_FrameCount >= m_MaxFrames)
				m_Running = false;
		}
		GB_CORE_INFO("Main loop finished after {} frames", m_FrameCount);
	}
}
