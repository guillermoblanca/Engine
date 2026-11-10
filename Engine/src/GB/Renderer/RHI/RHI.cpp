#include "RHI.h"
#include <cstdlib>
#include <string>

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

		// getenv without MSVC's deprecation warning (the library builds with warnings as errors), as in Qa.cpp.
		std::string Env(const char* name)
		{
#if defined(_WIN32)
			char* value = nullptr;
			size_t size = 0;
			std::string result;
			if (_dupenv_s(&value, &size, name) == 0 && value)
				result = value;
			std::free(value);
			return result;
#else
			const char* v = std::getenv(name);
			return v ? v : "";
#endif
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

	Options OptionsFromEnvironment()
	{
		Options options;
		options.UniformCache = Env("GB_RENDER_UNIFORM_CACHE") != "0";
		options.StateFilter = Env("GB_RENDER_STATE_FILTER") != "0";
		options.StringUniforms = Env("GB_RENDER_STRING_UNIFORMS") == "1";
		return options;
	}
}
