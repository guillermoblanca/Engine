#include "RHI.h"

namespace GB::RHI
{
	namespace
	{
		Device* g_Current = nullptr;
		std::unique_ptr<Device>& DefaultDevice()
		{
			static std::unique_ptr<Device> device;
			return device;
		}
	}

	Device& GetDevice()
	{
		if (g_Current)
			return *g_Current;
		std::unique_ptr<Device>& fallback = DefaultDevice();
		if (!fallback)
			fallback = CreateGLDevice();
		return *fallback;
	}

	void SetDevice(Device* device) { g_Current = device; }
}
