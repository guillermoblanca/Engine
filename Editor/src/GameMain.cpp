// G1: GB.exe, the game. The same code as the editor (the GBEditor library) started with `--shell`: EditorApp then opens on the start menu and never
// shows the editor (GameShell.cpp). This file only decides where things are, before anything reads them:
//   - the assets: <exe folder>/assets when it is there (a package made by tools/package_game.ps1), else the source tree this build came from
//     (GB_ASSET_DIR, a build run from the repository); --assets=<dir> chooses. The readers of the data (ShippedContent, the rocks) are pointed there
//     first: some of EditorApp's members read the catalogue while it is being built.
//   - the saves and the sessions: <exe folder>/saves (--saves=<dir> chooses; GB_SAVE_DIR is the editor's).
//   - with no console (a double click: GB.exe is a window program), what the log prints goes to <saves>/last_run.log and the crash trace to
//     <saves>/last_run_errors.log; a session also keeps its own game.log.
// Options: those of the editor (--hidden, --script=, --exec=, --quality=, --settings=, --strict=1...) plus --windowed (no full screen).
#include "EditorApp.h"
#include "CrashTrace.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/World/Rocks.h"
#include "GB/Platform/EntryPoint.h"
#include "GB/Core/Log.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace
{
	namespace fs = std::filesystem;

#ifdef _WIN32
	const LPTOP_LEVEL_EXCEPTION_FILTER g_CrashTrace = SetUnhandledExceptionFilter(GB::Editor::CrashTrace);

	fs::path ExeFolder()
	{
		wchar_t path[MAX_PATH] = {};
		const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
		return n > 0 && n < MAX_PATH ? fs::path(path).parent_path() : fs::current_path();
	}

	// a console or a pipe (a terminal, ctest) to write to; a double click gives a window program none
	bool HasErrorStream()
	{
		const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
		return h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
	}
#else
	fs::path ExeFolder() { std::error_code ec; const fs::path self = fs::read_symlink("/proc/self/exe", ec); return ec ? fs::current_path() : self.parent_path(); }
	bool HasErrorStream() { return true; }
#endif
}

GB::Application* GB::CreateApplication(const GB::LaunchOptions& given)
{
	LaunchOptions options = given;
	const fs::path exe = ExeFolder();
	std::error_code ec;
	std::string assets = options.GetValue("assets");
	if (assets.empty())
	{
		assets = fs::exists(exe / "assets" / "data" / "player.json", ec) ? (exe / "assets").generic_string() : std::string(GB_ASSET_DIR);
		options.Extra.push_back("--assets=" + assets);
	}
	SetContentDir(assets + "/data");
	SetRockLibraryDir(assets + "/data");
	std::string saves = options.GetValue("saves");
	if (saves.empty())
	{
		saves = (exe / "saves").generic_string();
		options.Extra.push_back("--saves=" + saves);
	}
	if (!options.HasFlag("shell")) options.Extra.push_back("--shell");
	if (!HasErrorStream())
	{
		fs::create_directories(saves, ec);
		// The log's console sink took its handle in Log::Init, before this runs: redirecting stdout does not reach it, so the log gets a sink of its own.
		// stderr is reopened for the crash trace (CrashTrace.h writes with fprintf).
		// (never destroyed, and flushed line by line: the sink may still be called while the statics are torn down)
		static std::mutex* lastRunLock = new std::mutex();
		static std::ofstream* lastRun = new std::ofstream(fs::path(saves) / "last_run.log", std::ios::trunc);
		GB::Log::AddSink([](GB::Log::Channel, GB::Log::Level, std::string_view text)
		{
			std::lock_guard<std::mutex> lock(*lastRunLock);
			*lastRun << text << std::endl;
		});
		FILE* err = nullptr;
#ifdef _WIN32
		freopen_s(&err, (saves + "/last_run_errors.log").c_str(), "w", stderr);
#else
		err = std::freopen((saves + "/last_run_errors.log").c_str(), "w", stderr);
#endif
		(void)err;
	}
	return new GB::Editor::EditorApp(options);
}
