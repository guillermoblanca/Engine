// GB Editor. Options: --script=file | --exec="cmd; cmd" (run commands at startup), --quality=minimum|low|medium|high|ultra, --settings=file (the player's settings,
// by default saves/settings.json: T88), --strict=1 (the exit code is 1 when a scripted line fails),
// plus the engine's --frames=N --screenshot=file.png --hidden --record=dir --fps=N. Type `help` in the console.
// The game's executable (GB.exe) is the same code started as the game: GameMain.cpp.
#include "EditorApp.h"
#include "CrashTrace.h"
#include "GB/Platform/EntryPoint.h"

#ifdef _WIN32
namespace
{
	const LPTOP_LEVEL_EXCEPTION_FILTER g_CrashTrace = SetUnhandledExceptionFilter(GB::Editor::CrashTrace);
}
#endif

GB::Application* GB::CreateApplication(const GB::LaunchOptions& options)
{
	return new GB::Editor::EditorApp(options);
}
