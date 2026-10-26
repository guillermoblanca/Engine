#pragma once

#include "Core.h"
#include <memory>
#include <spdlog/spdlog.h>

namespace GB
{
	class Log
	{
	public:
		static void Init();
		static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
	};
}

// Core log macros
#define GB_CORE_TRACE(...)   ::GB::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define GB_CORE_INFO(...)    ::GB::Log::GetCoreLogger()->info(__VA_ARGS__)
#define GB_CORE_WARN(...)    ::GB::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define GB_CORE_ERROR(...)   ::GB::Log::GetCoreLogger()->error(__VA_ARGS__)
#define GB_CORE_FATAL(...)   ::GB::Log::GetCoreLogger()->critical(__VA_ARGS__)
// Client log macros
#define GB_CLIENT_TRACE(...) ::GB::Log::GetClientLogger()->trace(__VA_ARGS__)
#define GB_CLIENT_INFO(...)  ::GB::Log::GetClientLogger()->info(__VA_ARGS__)
#define GB_CLIENT_WARN(...)  ::GB::Log::GetClientLogger()->warn(__VA_ARGS__)
#define GB_CLIENT_ERROR(...) ::GB::Log::GetClientLogger()->error(__VA_ARGS__)
#define GB_CLIENT_FATAL(...) ::GB::Log::GetClientLogger()->critical(__VA_ARGS__)
