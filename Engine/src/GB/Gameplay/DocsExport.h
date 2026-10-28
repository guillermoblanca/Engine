#pragma once
#include "GB/AI/Memory.h"
#include "GB/AI/Needs.h"
#include "GB/AI/Perception.h"
#include "GB/AI/Utility.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Garden.h"
#include "GB/Gameplay/Interactives.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	// Everything the game's rules say about themselves, as one JSON document: abilities (requirements, cost, cooldown, events, montages), effects, the
	// attributes and tags each kind of entity carries, the needs and how they move, the senses, the decision settings, the roles' weights, the seasons
	// and the settings of the ecology, the interactives and the garden. The wiki (`tools/make_wiki.py`, docs/wiki) is built from it, so what it shows is
	// what the engine does, not what someone remembered to write down. Pure and deterministic: the same inputs give the same text.
	struct DocsInputs
	{
		const GameplayLibrary* Library = nullptr;
		const GameplayWorld* World = nullptr;     // the entities to read attributes and tags from (any scene with the demos built)
		NeedsConfig Needs;
		VisionParams Vision;
		MemoryConfig Memory;
		ChooserConfig Chooser;
		std::map<std::string, std::map<std::string, float>> Roles;
		EcologyConfig Ecology;
		InteractiveSystem::Config Interactives;
		GardenConfig Garden;
		std::vector<std::pair<std::string, std::string>> Commands;   // the editor's commands and their help text
	};

	std::string ExportDocsJson(const DocsInputs& in);
}
