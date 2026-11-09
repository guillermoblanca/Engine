#pragma once

// Platform detection and GB_ASSERT. GB_ASSERT logs through GB/Core/Log.h, which is included here
// (Log.h does not include this header, so there is no include cycle).

#include "GB/Core/Log.h"
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

#define GB_ASSERT(cond) \
	do { if (!(cond)) { GB_CORE_FATAL("Assertion failed: {}", #cond); std::abort(); } } while (0)
