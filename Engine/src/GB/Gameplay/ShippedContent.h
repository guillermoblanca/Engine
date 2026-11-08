#pragma once
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/Techs.h"
#include "GB/Gameplay/WorldResources.h"
#include <map>
#include <vector>
#include <string>

namespace GB
{
	// The game's catalogue, as the data ships it: the one place where what exists is written (assets/data, the pack the game plays: today
	// "era1", whose manifest lists every file). The code says how things behave; what there is (items, recipes, resource nodes, technologies,
	// buildings, animals, interactives) is only data. DefaultBuildings, DefaultTechs, DefaultSpecies, DefaultInteractives and RegisterEraOneItems
	// hand out this, so the editor, the headless simulation and the tests all play the same content: a building, an item or a technology added
	// to the data is in the game, with no second list in C++ to keep in step.
	struct ShippedCatalog
	{
		ItemLibrary Items;
		RecipeBook Recipes;
		std::map<std::string, ResourceNodeDef> Nodes;
		TechTree Techs;
		std::map<std::string, BuildingDef> Buildings;
		std::map<std::string, SpeciesDef> Species;
		std::map<std::string, InteractiveDef> Interactives;
		NatureConfig Nature;   // the patches of wild nodes ("nature": bosques.json)
		std::string Error;     // "" when the pack loaded (what went wrong otherwise: the catalogue is then empty)
		// What the strict check of the pack says, and every node with no way into a world (UnplacedNodes): designed content that a game would
		// never show. The tests require it empty; the editor prints it at start.
		std::vector<std::string> Problems;
	};

	// The folder of the data (assets/data). By default the one of the source tree this build was made from (GB_CONTENT_DIR), else "assets/data"
	// beside the working directory. An application that ships its data elsewhere sets it before anything reads the catalogue.
	void SetContentDir(const std::string& dir);
	const std::string& ContentDir();
	// The pack the game plays (its manifest, <id>.pack.json in the content folder, lists the files and the packs it depends on).
	const char* GamePack();

	// The catalogue of GamePack(), loaded once on first use (any thread).
	const ShippedCatalog& Shipped();
	// The text of every file of GamePack(), in the order its manifest gives them (read once): a system whose default is a section of the data
	// (DefaultEconomyRules, DefaultCombatRules...) reads its section from these, over its own empty value.
	const std::vector<std::string>& ShippedDocuments();
	// One section read from every document of the game's pack, with the section's loader, over an empty value (a document that breaks the loader stops it).
	template <class T, class Load>
	T ShippedSection(Load load)
	{
		T value{};
		for (const std::string& doc : ShippedDocuments()) if (!load(doc, value).empty()) break;
		return value;
	}
}
