#pragma once

#include <cstdlib>

#if defined(_WIN32)
	#define GB_PLATFORM_WINDOWS
#elif defined(__linux__)
	#define GB_PLATFORM_LINUX
#elif defined(__APPLE__)
	#define GB_PLATFORM_MACOS
#else
	#error GB: unsupported platform
#endif

// Requires GB/Log.h to be included where used.
#define GB_ASSERT(cond) \
	do { if (!(cond)) { GB_CORE_FATAL("Assertion failed: {}", #cond); std::abort(); } } while (0)
