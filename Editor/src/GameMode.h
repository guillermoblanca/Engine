#pragma once
#include "GB/Game/PlayerMember.h"
#include <deque>
#include <string>
#include <vector>

namespace GB::Editor
{
	// H15: the game mode of the editor (`play`, `Editor --play`, play.txt; the code is EditorApp's, in GameMode.cpp). One command makes a world,
	// places the clans with the site chooser, gives the player a body (the X Bot of packs/full, or the fox) as a member of clan 0, follows it with the
	// game camera (H19) and hides the editor behind a small interface: the needs, what the player carries, its job, its standing, the clock, what
	// the interact key would do and the news of the clan. Everything the keys do is also a `game <action>` command, so a script plays the same game
	// headless (the test editor_play). The keys, the interface and the start are data (ControlRules: the "controls" section, player.json).
	struct GameModeState
	{
		bool Active = false;
		std::string Player;                // the player's member and actor
		std::string Model;                 // what the characters were made from
		bool Paused = false;
		bool ClanPanel = false, Help = false;
		std::string View = "follow";       // "follow" (behind the player) or "top" (the overview from above, `game view`)
		float SavedPitchNear = -1.0f, SavedPitchFar = -1.0f;   // the overview's tilt before the top view raised it (restored on leaving)
		bool Over = false;                 // a mortal player died (PlayerRules::Mortal): the game is over
		double DownAt = -1.0;              // when its body fell as a ragdoll (it stands up a moment later)
		// the interact key now: what it would do, the command it runs and how far the thing is
		std::string Prompt, PromptCommand;
		double PromptAt = -1.0;
		// an ability the player was sent to use (gather, stoke, build...): when its tasks end the clan hears of it
		std::string PendingKind, PendingKey, PendingItem;
		int PendingBefore = 0;
		// news: the clan's log lines (and the game's own) with the real time they came, the last ones shown
		struct Note { std::string Text; double At = 0.0; };
		std::deque<Note> Notes;
		size_t LogSeen = 0;
		std::string LastLog;
		double Clock = 0.0;                // real seconds since `play`
		// `quality auto`: the frames looked at and whether the switch to `minimum` was made
		int Frames = 0;
		bool AutoQuality = false, Lowered = false;
		struct SavedPanels { bool Hierarchy, Inspector, Properties, World, Stats, Console, Assets, Model, Animation, Abilities, Tribe, WorldState, Tests, QA; };
		SavedPanels Panels{};
		bool HasPanels = false;
	};
}
