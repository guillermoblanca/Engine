#pragma once
#include "GB/Gameplay/ContentPack.h"
#include "GB/Game/Diplomacy.h"
#include "GB/Game/GameCamera.h"
#include "GB/Game/Tribe.h"
#include "GB/Visual/VisualDefs.h"

namespace GB
{
	// The content sections of the game (H17): the systems that read a part of a pack register them here, so Items does not need to know them.
	// Today: "items" (items, recipes, nodes), "roles" (the tribe roles), "tribe" (its agenda, what is food and the stock it wants), "work" (names, aptitudes, job board), "interactives"
	// (the fire pit, the well, the gate...) and "species" (the animals of the ecology); the technologies of H11 ("techs", "knowledge") and the buildings of H8
	// ("buildings") travel inside "items" (the reachability check needs them all at once). A new system writes its own section (see StagedSection) and adds
	// it here or with ContentPacks::AddSection.

	// The "roles" section, through LoadRoles (atomic for one document).
	ContentSection RoleContentSection(std::map<std::string, TribeRole>& roles);

	// The "interactives" and "species" sections (H3), through LoadInteractives and LoadSpecies.
	ContentSection InteractiveContentSection(std::map<std::string, InteractiveDef>& defs);
	ContentSection SpeciesContentSection(std::map<std::string, SpeciesDef>& species);
	// Both world sections over the given live content (the editor's InteractiveSystem::Defs and EcologySystem::Species). They must outlive `packs`.
	void AddWorldContentSections(ContentPacks& packs, std::map<std::string, InteractiveDef>& interactives, std::map<std::string, SpeciesDef>& species);

	// The "cameras" section (H19): the tuning of the game cameras, through LoadGameCameraParams (atomic for one document). The editor's GameView::Params.
	ContentSection CameraContentSection(GameCameraParams& params);
	void AddCameraContentSections(ContentPacks& packs, GameCameraParams& params);

	// The "tribe" section, through LoadTribeRules: the daily agenda, what counts as food and the stock targets (atomic for one document).
	ContentSection TribeRulesContentSection(TribeAgenda& agenda, TribeEconomy& economy);

	// The "work" section (H7), through LoadWorkRules: the names of the clan, the aptitudes, the job board and the review of the trades (atomic for one document).
	ContentSection WorkContentSection(WorkRules& work);

	// The "exploration" section (H12), through LoadExplorationRules: regions, expeditions and migration (atomic for one document). Added on its own
	// (AddExplorationContentSection), so the list of AddGameContentSections stays as it was.
	ContentSection ExplorationContentSection(ExplorationRules& rules);
	void AddExplorationContentSection(ContentPacks& packs, ExplorationRules& rules);

	// The "society" section (H9 + H10), through LoadSocietyRules: life, births, genes, personality, relations and culture (atomic for one document).
	// Apart from AddGameContentSections so that adding it changes no signature; the editor adds it over TribeConfig::Society.
	ContentSection SocietyContentSection(SocietyRules& society);
	void AddSocietyContentSections(ContentPacks& packs, SocietyRules& society);
	// The "economy" section (H18), through LoadEconomyRules: production chains, spoilage, farming and crops, stores and hauling (atomic for one
	// document). Added on its own (AddEconomyContentSection), like "exploration". The items it names (a crop's seed, what it yields, the
	// intermediates kept) are not checked here, because the items of the same pack are still staged: CheckEconomy does it after the load.
	ContentSection EconomyContentSection(EconomyRules& rules);
	void AddEconomyContentSection(ContentPacks& packs, EconomyRules& rules);
	// The "combat" section (H14), through LoadCombatRules: weapons, blows, wounds, morale, threats, predators, hunts, raids and the default
	// hostility (atomic for one document). Added on its own, like "economy"; the editor adds it over its CombatSystem::Rules. The meat a hunt
	// gives is not checked here (the items of the same pack are still staged): CheckCombat does it after the load.
	ContentSection CombatContentSection(CombatRules& rules);
	void AddCombatContentSection(ContentPacks& packs, CombatRules& rules);


	// The "diplomacy" section (H13), through LoadDiplomacyRules: territory, encounters, relations between clans, trade, exchange of knowledge,
	// marriages and pacts (atomic for one document). On its own, like exploration; the editor adds it over its ClanWorld::Rules.
	ContentSection DiplomacyContentSection(DiplomacyRules& rules);
	void AddDiplomacyContentSection(ContentPacks& packs, DiplomacyRules& rules);

	// The "player" and "controls" sections (H15), through LoadPlayerRules and LoadControls: the player inside the society (standing, proposals,
	// gifts...) over TribeConfig::Player, and the keys, the interface and the start of the game mode (atomic for one document each).
	ContentSection PlayerContentSection(PlayerRules& rules);
	ContentSection ControlsContentSection(ControlRules& controls);
	void AddPlayerContentSections(ContentPacks& packs, PlayerRules& rules, ControlRules& controls);

	// The sections over the given live content (the "tribe" one only when an agenda and an economy are given, the "work" one only when its rules are).
	// With `techs`, the "items" section also reads the technologies ("techs" and "knowledge", H11), and with `buildings` the buildings (H8), and checks that
	// what they lock or need can be had (see ItemContentSection). The objects must outlive `packs`.
	void AddGameContentSections(ContentPacks& packs, ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes,
		std::map<std::string, TribeRole>& roles, TribeAgenda* agenda = nullptr, TribeEconomy* economy = nullptr, WorkRules* work = nullptr, TechTree* techs = nullptr,
		std::map<std::string, BuildingDef>* buildings = nullptr);

	// The "visuals" section (assets/data/visuals.json): what nodes, props, piles, creatures and buildings look like, through LoadVisuals (atomic for
	// one document). Apart from the others: the simulation never reads it, only the scene (SceneVisuals::Library).
	ContentSection VisualContentSection(VisualLibrary& library);
	void AddVisualContentSection(ContentPacks& packs, VisualLibrary& library);
}
