#pragma once
// The kinds of the story, shared by the player's Story sheet (GameSheets.cpp, DrawStorySheet) and the god's world chronicle (ObserverView.cpp):
// which events of the world's history (WorldHistory, GB/Game/History.h) a chronicle tells and under which group, with the group's label (English,
// Lang translates it), its icon in the atlas and its colour. One table, so both chronicles group and colour an event the same way.
#include "GameTheme.h"
#include <string>

namespace GB::Editor
{
	struct StoryKind { const char* Id; const char* Label; const char* Icon; ImU32 Colour; };

	inline const StoryKind* StoryKinds()
	{
		static const StoryKind kinds[] = {
			{ "birth", "births", "state.birth", GameTheme::Good }, { "death", "deaths", "state.death", GameTheme::Bad }, { "build", "buildings", "build", GameTheme::Ochre },
			{ "know", "discoveries", "state.discovery", GameTheme::News }, { "clans", "between clans", "trade", GameTheme::Warn }, { "fight", "fights", "fight", GameTheme::Bad },
		};
		return kinds;
	}
	inline constexpr int StoryKindCount = 6;

	// the history's kinds as the chronicles group them ("" = not in the story)
	inline std::string SheetKind(const std::string& k)
	{
		if (k == "birth" || k == "pair" || k == "marriage") return "birth";
		if (k == "death" || k == "killed") return "death";
		if (k == "built" || k == "camp_moved") return "build";
		if (k == "discovery" || k == "tradition" || k == "shaman" || k == "skeptic" || k == "tired") return "know";   // (T241: the faith's, with the shaman's)
		if (k == "raid" || k == "wounded" || k == "theft") return "fight";
		if (k == "contact" || k == "found" || k == "alliance" || k == "truce" || k == "pact" || k == "break" || k == "move") return "clans";
		return {};
	}

	inline const StoryKind* FindStoryKind(const std::string& id)
	{
		for (int i = 0; i < StoryKindCount; ++i) if (id == StoryKinds()[i].Id) return &StoryKinds()[i];
		return nullptr;
	}
}
