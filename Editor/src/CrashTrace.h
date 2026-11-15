// A crash leaves its trace in the log of the editor and of GB.exe (main.cpp, GameMain.cpp): the exception, and the return addresses of the stack as
// offsets into the executable (the build writes <exe>.map beside it: the function of an offset is the last one of the map that starts below it). The
// unwinding reads the tables every x64 image carries, so it works in a release build with no symbols. Windows only.
#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>

namespace GB::Editor
{
	inline LONG WINAPI CrashTrace(EXCEPTION_POINTERS* info)
	{
		const HMODULE module = GetModuleHandleW(nullptr);
		char path[MAX_PATH] = {};
		GetModuleFileNameA(module, path, MAX_PATH);
		const char* exe = path;
		for (const char* c = path; *c; ++c) if (*c == '\\' || *c == '/') exe = c + 1;
		const DWORD64 base = reinterpret_cast<DWORD64>(module);
		std::fprintf(stderr, "CRASH: exception 0x%08lx at %s+0x%llx\n", info->ExceptionRecord->ExceptionCode, exe,
			static_cast<unsigned long long>(reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress) - base));
		CONTEXT context = *info->ContextRecord;
		for (int frame = 0; frame < 48 && context.Rip; ++frame)
		{
			DWORD64 imageBase = 0;
			const DWORD64 rip = context.Rip;
			if (rip >= base && rip < base + 0x10000000ull) std::fprintf(stderr, "CRASH:   #%02d %s+0x%llx\n", frame, exe, static_cast<unsigned long long>(rip - base));
			else std::fprintf(stderr, "CRASH:   #%02d 0x%llx\n", frame, static_cast<unsigned long long>(rip));
			PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(rip, &imageBase, nullptr);
			if (!function)   // a leaf function: the return address is on top of the stack
			{
				context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
				context.Rsp += 8;
				continue;
			}
			void* handlerData = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, rip, function, &context, &handlerData, &establisher, nullptr);
		}
		std::fflush(stderr);
		std::fflush(stdout);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}
#endif
