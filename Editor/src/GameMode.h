#pragma once
#include "GB/Game/PlayerMember.h"
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace GB::Editor
{
	// H15: the game mode of the editor (`play`, `Editor --play`, play.txt; the code is EditorApp's, in GameMode.cpp). One command makes a world,
	// places the clans with the site chooser, gives the player a body (the X Bot of packs/full, or the fox) as a member of clan 0, follows it with the
	// game camera (H19) and hides the editor behind a small interface: the needs, what the player carries, its job, its standing, the clock, what
	// the interact key would do and the news of the clan. Everything the keys do is also a `game <action>` command, so a script plays the same game
	// headless (the test editor_play). The keys, the interface and the start are data (ControlRules: the "controls" section, player.json).
	// A sheet of the clan book (GamePanels.cpp): the `game` command that opens it, its group ("me", "clan", "world"; "keys" for the keys), the
	// English of its tab (Lang translates it) and the action whose key opens it.
	struct BookSheet { const char* Action; const char* Group; const char* Label; const char* KeyAction; };
	const std::vector<BookSheet>& BookSheets();

	struct GameModeState
	{
		bool Active = false;
		std::string Player;                // the player's member and actor
		std::string Model;                 // what the characters were made from
		bool Paused = false;
		bool ClanPanel = false, Help = false;
		std::string Selected;              // what a click picked: a character or a node (the Selection panel shows its stats)
		float PressX = -1.0f, PressY = -1.0f;
		float PressRX = -1.0f, PressRY = -1.0f;   // where the right button went down (-1 = not down)   // where the mouse went down in the viewport (a click is a press and a release near it)
		bool Menu = false;                 // Esc with nothing open: the pause menu (resume, save, load, keys, back to the editor)
		bool Inventory = false, People = false, Build = false;   // the panels of the bag, of the people around and of the buildings (I, T, B)
		// the sheets that look at the clan and the world (GameSheets.cpp): the village at a glance, the households, the other clans, the
		// technologies and eras, the map of areas and the story so far (U, Y, O, K, M, N)
		bool Village = false, Homes = false, Clans = false, Tech = false, Map = false, Story = false;
		std::map<std::string, std::string> BookLast;   // the sheet last open in each group of the clan book ("me", "clan", "world"): `game book <group>` goes back to it
		// the story: what mattered (births, deaths, buildings, discoveries, new clans, raids, pacts), kept for the whole game, with its day
		struct Event { std::string Text, Kind; int Day = 0; };
		std::deque<Event> History;
		size_t ClanLogSeen = 0;
		// where the interface's parts ended last frame (they place the next ones: the selection under the needs, the prompt over the action bar, the
		// news left of the clan book)
		float HudBottom = 0.0f, HudRight = 0.0f, ClockBottom = 0.0f, BarTop = 0.0f, BookWidth = 0.0f;
		std::string BookTab;               // the sheet the clan book shows ("" closed): Inventory, People, Build, ClanPanel and Help are its tabs, one at a time
		bool TechTree = false;             // the tree of knowledge over the game (K, `game tech`)
		std::string TreePick;              // the technology whose card it shows
		bool Console = false;              // the console inside the game (`game console`): the editor's panels are not drawn while playing
		std::string View = "follow";       // "follow" (behind the player) or "top" (the overview from above, `game view`)
		float SavedPitchNear = -1.0f, SavedPitchFar = -1.0f;   // the overview's tilt before the top view raised it (restored on leaving)
		bool Over = false;                 // a mortal player died (PlayerRules::Mortal): the game is over
		bool Heir = false;                 // V3: the player's character died: the game waits for the player to choose who carries on (game heir)
		std::string Dead;                  // ... and who died
		// V3: the state of the conversations: how many times the player has spoken to each person lately (its patience runs out) and since when
		struct Patience { int Times = 0; double Since = 0.0; };
		std::map<std::string, Patience> Talks;
		std::string Approach;             // V2: the person the character runs to (game approach), until it is within reach
		double ApproachAt = -1.0;
		double DownAt = -1.0;              // when its body fell as a ragdoll (it stands up a moment later)
		// the interact key now: what it would do, the command it runs and how far the thing is
		std::string Prompt, PromptCommand;
		glm::vec3 PromptPos{ 0.0f };   // where the thing the prompt is about stands (the label under it in the world)
		bool HasPromptPos = false;
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
