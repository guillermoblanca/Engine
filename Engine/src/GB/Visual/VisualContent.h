#pragma once
#include "GB/Gameplay/ContentPack.h"
#include "GB/Visual/VisualDefs.h"

namespace GB
{
	// The "visuals" section (assets/data/visuals.json): what nodes, props, piles, creatures and buildings look like, through LoadVisuals (atomic for
	// one document). It lives with the visual layer, not with the game's sections (GB/Game/GameContent.h): the simulation never reads it, only the
	// scene (SceneVisuals::Library). An application that draws registers it next to AddContentSections; the headless simulation does not.
	ContentSection VisualContentSection(VisualLibrary& library);
	void AddVisualContentSection(ContentPacks& packs, VisualLibrary& library);
}
