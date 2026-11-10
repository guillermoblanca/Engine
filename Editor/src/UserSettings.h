// T88: the player's own settings, a file of its own laid over assets/data/player.json. player.json (versioned, written by hand) gives the
// defaults and the game never writes it; what the player chooses (today the graphics preset, controls.game.quality) goes to the user's file,
// by default <save dir>/settings.json (`saves/`, ignored by git, like saves/game.gbsave; GB_SAVE_DIR moves both), or `--settings=<path>`.
// The file has player.json's shape ({ "controls": { "game": { "quality": "high" } } }), so later it can also carry keys ("controls.keys") or
// the "input" section PlayerMode reads (PlayerMode.cpp) without a new format. No ImGui, no window: GBEditorTests covers it.
#pragma once
#include "GB/Game/PlayerMember.h"
#include <string>

namespace GB::Editor
{
	struct UserSettingsLoad
	{
		bool Found = false;          // the file is there
		std::string Error;           // why it was ignored ("" = applied or absent)
		std::string Quality;         // its controls.game.quality ("" = it says none)
	};

	// Lays the file's "controls" over `controls` (LoadControls: only what it says changes). An absent file changes nothing; a broken one (not
	// JSON, an unknown key, a wrong type) changes nothing and says why.
	UserSettingsLoad ApplyUserSettings(const std::string& path, ControlRules& controls);

	// Keeps `level` as controls.game.quality in the file: every other key of it stays, the folder is made if missing, and the result must load
	// through LoadControls before it replaces the file. A file that is there but broken is not overwritten (it may be the player's own edit).
	// Returns why it could not, or "".
	std::string SaveUserQuality(const std::string& path, const std::string& level);
}
