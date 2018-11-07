#pragma once

#ifdef GB_PLATFORM_WINDOWS
	#ifdef GB_BUILD_DLL
	#define GB_API __declspec(dllexport)
	#else
	#define	GB_API __declspec(dllimport)
	#endif
#else
#error GB only supports Windows!
#endif // GB_PLATFORM_WINDOWS
