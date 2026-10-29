#include "GB/Game/GameContent.h"

namespace GB
{
	ContentSection RoleContentSection(std::map<std::string, TribeRole>& roles)
	{
		return StagedSection("roles", { "roles" },
			[](const std::string& json, std::map<std::string, TribeRole>& r) { return LoadRoles(json, r); },
			nullptr, roles);
	}

	ContentSection InteractiveContentSection(std::map<std::string, InteractiveDef>& defs)
	{
		return StagedSection("interactives", { "interactives" },
			[](const std::string& json, std::map<std::string, InteractiveDef>& d) { return LoadInteractives(json, d); },
			nullptr, defs);
	}

	ContentSection SpeciesContentSection(std::map<std::string, SpeciesDef>& species)
	{
		return StagedSection("species", { "species" },
			[](const std::string& json, std::map<std::string, SpeciesDef>& s) { return LoadSpecies(json, s); },
			nullptr, species);
	}

	void AddWorldContentSections(ContentPacks& packs, std::map<std::string, InteractiveDef>& interactives, std::map<std::string, SpeciesDef>& species)
	{
		packs.AddSection(InteractiveContentSection(interactives));
		packs.AddSection(SpeciesContentSection(species));
	}

	ContentSection CameraContentSection(GameCameraParams& params)
	{
		return StagedSection("cameras", { "cameras" },
			[](const std::string& json, GameCameraParams& p) { return LoadGameCameraParams(json, p); },
			nullptr, params);
	}

	void AddCameraContentSections(ContentPacks& packs, GameCameraParams& params)
	{
		packs.AddSection(CameraContentSection(params));
	}

	ContentSection TribeRulesContentSection(TribeAgenda& agenda, TribeEconomy& economy)
	{
		return StagedSection("tribe", { "tribe" },
			[](const std::string& json, TribeAgenda& a, TribeEconomy& e) { return LoadTribeRules(json, a, e); },
			nullptr, agenda, economy);
	}

	ContentSection WorkContentSection(WorkRules& work)
	{
		return StagedSection("work", { "work" },
			[](const std::string& json, WorkRules& w) { return LoadWorkRules(json, w); },
			nullptr, work);
	}

	ContentSection ExplorationContentSection(ExplorationRules& rules)
	{
		return StagedSection("exploration", { "exploration" },
			[](const std::string& json, ExplorationRules& r) { return LoadExplorationRules(json, r); },
			nullptr, rules);
	}

	void AddExplorationContentSection(ContentPacks& packs, ExplorationRules& rules)
	{
		packs.AddSection(ExplorationContentSection(rules));
	}

	ContentSection SocietyContentSection(SocietyRules& society)
	{
		return StagedSection("society", { "society" },
			[](const std::string& json, SocietyRules& s) { return LoadSocietyRules(json, s); },
			nullptr, society);
	}

	void AddSocietyContentSections(ContentPacks& packs, SocietyRules& society) { packs.AddSection(SocietyContentSection(society)); }
	ContentSection EconomyContentSection(EconomyRules& rules)
	{
		return StagedSection("economy", { "economy" },
			[](const std::string& json, EconomyRules& r) { return LoadEconomyRules(json, r); },
			nullptr, rules);
	}

	void AddEconomyContentSection(ContentPacks& packs, EconomyRules& rules)
	{
		packs.AddSection(EconomyContentSection(rules));
	}

	ContentSection DiplomacyContentSection(DiplomacyRules& rules)
	{
		return StagedSection("diplomacy", { "diplomacy" },
			[](const std::string& json, DiplomacyRules& r) { return LoadDiplomacyRules(json, r); },
			nullptr, rules);
	}

	void AddDiplomacyContentSection(ContentPacks& packs, DiplomacyRules& rules) { packs.AddSection(DiplomacyContentSection(rules)); }

	ContentSection CombatContentSection(CombatRules& rules)
	{
		return StagedSection("combat", { "combat" },
			[](const std::string& json, CombatRules& r) { return LoadCombatRules(json, r); },
			nullptr, rules);
	}

	void AddCombatContentSection(ContentPacks& packs, CombatRules& rules)
	{
		packs.AddSection(CombatContentSection(rules));
	}

	ContentSection PlayerContentSection(PlayerRules& rules)
	{
		return StagedSection("player", { "player" },
			[](const std::string& json, PlayerRules& r) { return LoadPlayerRules(json, r); },
			nullptr, rules);
	}

	ContentSection ControlsContentSection(ControlRules& controls)
	{
		return StagedSection("controls", { "controls" },
			[](const std::string& json, ControlRules& c) { return LoadControls(json, c); },
			nullptr, controls);
	}

	void AddPlayerContentSections(ContentPacks& packs, PlayerRules& rules, ControlRules& controls)
	{
		packs.AddSection(PlayerContentSection(rules));
		packs.AddSection(ControlsContentSection(controls));
	}

	void AddGameContentSections(ContentPacks& packs, ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes,
		std::map<std::string, TribeRole>& roles, TribeAgenda* agenda, TribeEconomy* economy, WorkRules* work, TechTree* techs, std::map<std::string, BuildingDef>* buildings)
	{
		packs.AddSection(ItemContentSection(items, recipes, nodes, techs, buildings));   // (with neither, the plain items section)
		packs.AddSection(RoleContentSection(roles));
		if (agenda && economy) packs.AddSection(TribeRulesContentSection(*agenda, *economy));
		if (work) packs.AddSection(WorkContentSection(*work));
	}

	ContentSection VisualContentSection(VisualLibrary& library)
	{
		return StagedSection("visuals", { "visuals" },
			[](const std::string& json, VisualLibrary& l) { return LoadVisuals(json, l); },
			nullptr, library);
	}

	void AddVisualContentSection(ContentPacks& packs, VisualLibrary& library) { packs.AddSection(VisualContentSection(library)); }

	ContentSection SocialContentSection(SocialRules& rules)
	{
		return StagedSection("social", { "social" },
			[](const std::string& json, SocialRules& r) { return LoadSocialRules(json, r); },
			nullptr, rules);
	}

	void AddSocialContentSection(ContentPacks& packs, SocialRules& rules) { packs.AddSection(SocialContentSection(rules)); }
}
