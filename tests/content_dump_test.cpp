// T347 (AUDITORIA_DATOS_2026-11-19 step 0a): the canonical dump of the content the game loads. Every section of the game's pack is read the way
// GBSim reads it (Sim/src/main.cpp, EraContent: the same starting values) plus the visuals the editor adds, and the second way the data reaches the
// code (ShippedDocuments: every Default*() that reads its section, and Shipped(), the catalogue) is dumped beside it. Each *ToJson is parsed again
// so the keys come out sorted (arrays keep their order: the needs and the steps of the tree are lists). A change that only moves data between the
// files of the pack must leave the dump identical:
//     GBRenderTests content_dump <out.json>      writes the dump
//     GBRenderTests content_dump                  checks the pack loads and that two dumps are the same text
#include "GB/Game/DataNeeds.h"
#include "GB/Game/Diplomacy.h"
#include "GB/Game/Economy.h"
#include "GB/Game/Exploration.h"
#include "GB/Game/FoundingRules.h"
#include "GB/Game/GameCamera.h"
#include "GB/Game/GameContent.h"
#include "GB/Game/JobBoard.h"
#include "GB/Game/PlayerMember.h"
#include "GB/Game/Social.h"
#include "GB/Game/Society.h"
#include "GB/Game/SocietyMetrics.h"
#include "GB/Game/Trails.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/WorldAlerts.h"
#include "GB/Game/WorldDirector.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/Log.h"
#include "GB/Visual/VisualContent.h"
#include "GB/Visual/VisualDefs.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <string>

namespace
{
	using namespace GB;
	using Json = nlohmann::json;
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	// One *ToJson text as a value (a text that does not parse is kept as a string, so the dump still shows it).
	Json Value(const std::string& text)
	{
		Json j = Json::parse(text, nullptr, false);
		return j.is_discarded() ? Json(text) : j;
	}

	std::string Dump(std::string& error)
	{
		Json out;
		// ---- the game's load: the sections over the values GBSim starts from (EraContent), and the visuals and cameras of the editor
		{
			ItemLibrary items;
			RecipeBook recipes;
			std::map<std::string, ResourceNodeDef> nodes;
			TribeConfig config;
			TechTree techs;
			std::map<std::string, BuildingDef> buildings;
			CombatRules combat;
			DiplomacyRules diplomacy = DefaultDiplomacyRules();
			std::map<std::string, SpeciesDef> species = DefaultSpecies();
			std::map<std::string, InteractiveDef> interactives;
			ControlRules controls;
			NatureConfig nature;
			SocietyMetricRules metrics;
			DirectorRules director;
			AlertRules alerts;
			GameCameraParams cameras;
			VisualLibrary visuals;
			ContentTargets to;
			to.Items = &items; to.Recipes = &recipes; to.Nodes = &nodes; to.Techs = &techs; to.Buildings = &buildings;
			to.Roles = &config.Roles; to.Agenda = &config.Agenda; to.Economy = &config.Economy; to.Work = &config.Work;
			to.Explore = &config.Explore; to.Econ = &config.Econ; to.Combat = &combat; to.Player = &config.Player; to.Controls = &controls;
			to.Social = &config.Social; to.Society = &config.Society; to.Nature = &nature; to.Diplomacy = &diplomacy; to.Species = &species; to.Interactives = &interactives;
			to.Needs = &config.DataNeeds; to.Climate = &config.Seasons.Climate; to.Metrics = &metrics; to.Director = &director; to.Alerts = &alerts;
			to.Founding = &config.Founding; to.Cameras = &cameras; to.Trails = &config.Trails;
			ContentPacks packs;
			AddContentSections(packs, to);
			AddVisualContentSection(packs, visuals);
			ContentReport report;
			error = packs.Load(GamePack(), std::string(GB_ASSETS_DIR) + "/data", &report);
			Json& g = out["load"];
			g["error"] = error;
			g["problems"] = report.Problems;
			g["unplaced"] = UnplacedNodes(nodes, nature);
			g["content"] = Value(ContentToJson(items, recipes, nodes));
			g["techs"] = Value(TechsToJson(techs));
			g["buildings"] = Value(BuildingsToJson(buildings));
			g["roles"] = Value(RolesToJson(config.Roles));
			g["tribe"] = Value(TribeRulesToJson(config));
			g["work"] = Value(WorkRulesToJson(config.Work));
			g["exploration"] = Value(ExplorationRulesToJson(config.Explore));
			g["economy"] = Value(EconomyRulesToJson(config.Econ));
			g["combat"] = Value(CombatRulesToJson(combat));
			g["player"] = Value(PlayerRulesToJson(config.Player));
			g["controls"] = Value(ControlsToJson(controls));
			g["social"] = Value(SocialRulesToJson(config.Social));
			g["society"] = Value(SocietyRulesToJson(config.Society));
			g["nature"] = Value(NatureConfigToJson(nature));
			g["diplomacy"] = Value(DiplomacyRulesToJson(diplomacy));
			g["species"] = Value(SpeciesToJson(species));
			g["interactives"] = Value(InteractivesToJson(interactives));
			g["needs"] = Value(NeedsToJson(config.DataNeeds));
			g["climate"] = Value(ClimateToJson(config.Seasons.Climate));
			g["metrics"] = Value(SocietyMetricRulesToJson(metrics));
			g["alerts"] = Value(AlertRulesToJson(alerts));
			g["founding"] = Value(FoundingRulesToJson(config.Founding));
			g["cameras"] = Value(GameCameraParamsToJson(cameras));
			g["trails"] = Value(TrailRulesToJson(config.Trails));
			g["visuals"] = Value(VisualsToJson(visuals));
			// (the director's rules have no *ToJson; director.json is not one of the files the data pass moves)
		}
		// ---- the second way in: the text of the pack's files (ShippedDocuments) read by each Default*() and by the catalogue
		{
			Json& d = out["defaults"];
			d["economy"] = Value(EconomyRulesToJson(DefaultEconomyRules()));
			d["work"] = Value(WorkRulesToJson(DefaultWorkRules()));
			d["combat"] = Value(CombatRulesToJson(DefaultCombatRules()));
			d["society"] = Value(SocietyRulesToJson(DefaultSocietyRules()));
			d["social"] = Value(SocialRulesToJson(DefaultSocialRules()));
			d["diplomacy"] = Value(DiplomacyRulesToJson(DefaultDiplomacyRules()));
			d["exploration"] = Value(ExplorationRulesToJson(DefaultExplorationRules()));
			d["climate"] = Value(ClimateToJson(DefaultClimate()));
			d["roles"] = Value(RolesToJson(DefaultRoles()));
			TribeConfig tribe;
			tribe.Agenda = DefaultAgenda();
			tribe.Economy = DefaultEconomy();
			d["tribe"] = Value(TribeRulesToJson(tribe));
			d["player"] = Value(PlayerRulesToJson(DefaultPlayerRules()));
			d["controls"] = Value(ControlsToJson(DefaultControls()));
			d["founding"] = Value(FoundingRulesToJson(DefaultFoundingRules()));
			d["trails"] = Value(TrailRulesToJson(DefaultTrailRules()));
			d["species"] = Value(SpeciesToJson(DefaultSpecies()));
			d["interactives"] = Value(InteractivesToJson(DefaultInteractives()));
			d["buildings"] = Value(BuildingsToJson(DefaultBuildings()));
			d["techs"] = Value(TechsToJson(DefaultTechs()));
			const ShippedCatalog& s = Shipped();
			Json& c = out["shipped"];
			c["error"] = s.Error;
			c["problems"] = s.Problems;
			c["content"] = Value(ContentToJson(s.Items, s.Recipes, s.Nodes));
			c["techs"] = Value(TechsToJson(s.Techs));
			c["buildings"] = Value(BuildingsToJson(s.Buildings));
			c["species"] = Value(SpeciesToJson(s.Species));
			c["interactives"] = Value(InteractivesToJson(s.Interactives));
			c["nature"] = Value(NatureConfigToJson(s.Nature));
			c["documents"] = ShippedDocuments().size();
		}
		return out.dump(1) + "\n";
	}
}

int GBTest_content_dump(int argc, char** argv)
{
	Log::Init();
	std::printf("content: %s/data (the catalogue reads %s)\n", GB_ASSETS_DIR, ContentDir().c_str());
	std::string error;
	const std::string first = Dump(error);
	if (argc > 1)
	{
		std::ofstream file(argv[1], std::ios::binary);
		file << first;
		Check(static_cast<bool>(file), std::string("the dump is written to ") + argv[1]);
		std::printf("%zu bytes -> %s\n", first.size(), argv[1]);
	}
	Check(error.empty(), "the game's pack loads: " + error);
	std::string again;
	Check(Dump(again) == first, "two dumps are the same text (the dump is stable)");
	Check(first.size() > 10000, "the dump has the content in it");
	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::printf("content_dump: ok\n");
	return 0;
}
