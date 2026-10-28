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

	void AddGameContentSections(ContentPacks& packs, ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes,
		std::map<std::string, TribeRole>& roles, TribeAgenda* agenda, TribeEconomy* economy, WorkRules* work, TechTree* techs)
	{
		packs.AddSection(techs ? ItemContentSection(items, recipes, nodes, *techs) : ItemContentSection(items, recipes, nodes));
		packs.AddSection(RoleContentSection(roles));
		if (agenda && economy) packs.AddSection(TribeRulesContentSection(*agenda, *economy));
		if (work) packs.AddSection(WorkContentSection(*work));
	}
}
