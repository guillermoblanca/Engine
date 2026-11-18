// G1/G2: the shell of the game's executable (GB.exe, GameMain.cpp): the same EditorApp started with `--shell`, which never shows the editor. It opens
// on the start menu (a new god's game with its biome, seed, clans and members or one of the presets of assets/data/presets.json, the saved games,
// the settings), plays the god's screen (U1: `observer screen`) with a pause menu of its own on Esc, and records every game as a session folder
// (<saves>/sessions/<date>_<biome>_<seed>/: game.log, autosaves every few days of game, telemetry.db, notes.md with the director's notes and their
// captures, session.json) that can be handed over and studied. The code is EditorApp's (GameShell.cpp); this header only holds its state and data.
#pragma once
#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace GB::Editor
{
	// How a session records the game: the top-level "session" section of assets/data/player.json (read by the editor's own content section,
	// GameShell.cpp). Unknown keys or wrong types refuse the section (the pack does not load) like any other section.
	struct SessionRules
	{
		int AutosaveDays = 1;              // a save into the session folder every so many days of game (0: none but the one on leaving)
		bool Telemetry = true;             // the telemetry of the game (the table GBSim writes) into the session folder
		double TelemetryEvery = 60.0;      // a row per member and clan every so many simulation seconds
		std::map<std::string, std::string> Keys = { { "note", "F8" }, { "shot", "F12" }, { "quicksave", "F5" }, { "quickload", "F9" } };
	};
	// "" or what is wrong; a document without "session" changes nothing.
	std::string LoadSessionRules(const std::string& json, SessionRules& rules);

	// A start of a game the menu offers (assets/data/presets.json, then the player's own in <saves>/presets.json, G5): its words, the world and the
	// clans. A shipped preset's label and tip are English keys of Lang; a player's are his own words, shown as they are.
	struct GamePreset
	{
		std::string Id, Label, Tip;
		std::string Biome = "temperate";
		int Seed = 5, Clans = 3, Members = 6, Speed = 1;
		bool Bare = false;                 // the clans start with nothing built (`village ... bare`)
		bool User = false;                 // saved by the player (<saves>/presets.json), not shipped
	};
	// "" or what is wrong (then `out` is left as it was).
	std::string LoadGamePresets(const std::string& json, std::vector<GamePreset>& out);

	struct ShellState
	{
		bool On = false;                   // GB.exe (`--shell`): no editor, ever
		enum class Screen { Menu, God, Player };
		Screen Now = Screen::Menu;
		std::string Page = "main";         // the start menu's page: main, new, load, settings
		bool Pause = false;                // the pause menu of a game (Esc)
		std::string PausePage = "main";    // its page: main, load, settings
		// the new game's form (the widgets edit it; the button runs `shell new ...` with it)
		int Preset = 0;                    // the chosen one of Presets, -1 custom (the form below)
		int Biome = 0;                     // 0 temperate, 1 alpine, 2 desert
		int Seed = 5, Clans = 3, Members = 6, Speed = 1;
		bool Bare = false;
		std::vector<GamePreset> Presets;   // the shipped ones first, then the player's (User)
		char PresetName[48] = {};          // the name the custom form is saved with as a preset of the player's
		// a long order (a new world) waits a couple of frames so the "generating" card is drawn first
		std::string Pending;
		int PendingFrames = 0;
		std::string Toast;                 // a short line over the game (saved, captured), and when it came
		double ToastAt = -1e9;
		bool Fullscreen = false;
		// the director's note (F8): the capture is taken first, the box opens on the next frame
		bool NoteOpen = false;
		int NoteOpenIn = 0;
		char NoteText[1024] = {};
		std::string NoteShot;
		// what `shell same` compares after a load: the day, the clans and the living of the moment `shell mark` was given
		int MarkDay = -1, MarkClans = -1, MarkAlive = -1;

		// the session: the folder of the game being played
		struct Session
		{
			bool Active = false;
			std::string Dir, Mode, Biome, Preset, LoadedFrom;
			uint32_t Seed = 0;
			int Clans = 0, Members = 0, Speed = 1;
			int Party = 0;                 // T310: the player's clan founded with this many (`shell new ... party <n>`); 0: `members` as the others
			bool Bare = false;
			std::string Started;           // "2026-11-15 10:42:07"
			std::chrono::steady_clock::time_point Clock{};
			int DayStart = 1, NextAutosaveDay = 0, Notes = 0, Shots = 0;
			std::vector<std::string> Saves;
			bool Telemetry = false;
		} Run;
		SessionRules Rules;
		// game.log: every line of the log while a session lasts (other threads log too)
		struct LogFile
		{
			std::mutex Lock;
			std::ofstream Out;
			int Sink = 0;
			std::chrono::steady_clock::time_point Flushed{};
		};
		std::shared_ptr<LogFile> Log = std::make_shared<LogFile>();
	};
}
