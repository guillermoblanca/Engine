// GB Editor. Options: --script=file | --exec="cmd; cmd" (run commands at startup), --quality=minimum|low|medium|high|ultra, --settings=file (the player's settings,
// by default saves/settings.json: T88), --strict=1 (the exit code is 1 when a scripted line fails),
// plus the engine's --frames=N --screenshot=file.png --hidden --record=dir --fps=N. Type `help` in the console.
#include "EditorApp.h"
#include "GB/Platform/EntryPoint.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>

namespace
{
	// A crash leaves its trace in the log: the exception, and the return addresses of the stack as offsets into Editor.exe (the build writes
	// Editor.map beside it: the function of an offset is the last one of the map that starts below it). The unwinding reads the tables every
	// x64 image carries, so it works in a release build with no symbols.
	LONG WINAPI CrashTrace(EXCEPTION_POINTERS* info)
	{
		const HMODULE module = GetModuleHandleW(nullptr);
		const DWORD64 base = reinterpret_cast<DWORD64>(module);
		std::fprintf(stderr, "CRASH: exception 0x%08lx at Editor.exe+0x%llx\n", info->ExceptionRecord->ExceptionCode,
			static_cast<unsigned long long>(reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress) - base));
		CONTEXT context = *info->ContextRecord;
		for (int frame = 0; frame < 48 && context.Rip; ++frame)
		{
			DWORD64 imageBase = 0;
			const DWORD64 rip = context.Rip;
			if (rip >= base && rip < base + 0x10000000ull) std::fprintf(stderr, "CRASH:   #%02d Editor.exe+0x%llx\n", frame, static_cast<unsigned long long>(rip - base));
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

	const LPTOP_LEVEL_EXCEPTION_FILTER g_CrashTrace = SetUnhandledExceptionFilter(CrashTrace);
}
#endif

GB::Application* GB::CreateApplication(const GB::LaunchOptions& options)
{
	return new GB::Editor::EditorApp(options);
}
