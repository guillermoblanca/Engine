// T204 (S1 of VISION_MUNDO_VIVO section 13): the measure of a society (GB/Game/SocietyMetrics.h). Checked on the CPU, with no window:
//  * the quality of life is the weighted mean of its parts; a part that cannot be told is left out and the rest weigh more;
//  * a clan with no food, no beds and nothing met is low, one with beds, food and its needs met is high;
//  * the "metrics" section reads and writes back, refuses what it does not know, and the game's data says the built-in weights;
//  * lineages are the founders with living descendants, through both parents and across clans;
//  * on a real clan (a SimRun): emptying the store and the bags makes the food part 0, filling the store makes it 1 and the index rises.
#include "GB/Game/SimQA.h"
#include "GB/Game/Society.h"
#include "GB/Game/SocietyMetrics.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
		else std::printf("ok: %s\n", what.c_str());
	}
	bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
}

using namespace GB;

int GBTest_society_metrics(int, char**)
{
	const SocietyMetricRules rules;
	float sum = 0.0f;
	for (const auto& [part, w] : rules.Weights) sum += w;
	Check(Near(sum, 1.0f) && rules.Weights.size() == QolParts().size(), "the built-in weights cover every part and add up to 1");

	// ---- the index from its parts
	const std::map<std::string, float> bare = { { "needs", 0.30f }, { "shelter", 0.0f }, { "food", 0.0f }, { "tools", 0.0f }, { "health", 0.9f }, { "longevity", -1.0f }, { "techs", 0.0f } };
	const std::map<std::string, float> good = { { "needs", 0.95f }, { "shelter", 1.0f }, { "food", 1.0f }, { "tools", 1.0f }, { "health", 1.0f }, { "longevity", -1.0f }, { "techs", 0.8f } };
	const float low = Qol(bare, rules), high = Qol(good, rules);
	Check(low < 0.3f, "a clan with no food, no beds, no tools and its needs low has a low quality of life (" + std::to_string(low) + ")");
	Check(high > 0.85f, "a clan with beds and food for all, its needs met and most of what betters life known has a high one (" + std::to_string(high) + ")");
	// longevity cannot be told (nobody died): the others weigh 1 / (1 - its weight) each
	float told = 0.0f, weighed = 0.0f;
	for (const auto& [part, w] : rules.Weights)
		if (good.at(part) >= 0.0f) { told += w * good.at(part); weighed += w; }
	const float expected = told / weighed;
	Check(weighed < 0.95f, "(longevity weighs something)");
	Check(Near(high, expected), "a part that cannot be told is left out and the rest weigh more (" + std::to_string(high) + " vs " + std::to_string(expected) + ")");
	Check(Qol({}, rules) == 0.0f, "nothing told: 0");
	std::map<std::string, float> over = good;
	over["food"] = 7.0f;
	Check(Near(Qol(over, rules), high), "a part over 1 counts as 1");

	// ---- the section
	{
		SocietyMetricRules r;
		const std::string error = LoadSocietyMetricRules(R"({ "metrics": { "qol": { "weights": { "needs": 1, "food": 1 }, "needs": [ "Hunger" ], "foodDays": 5, "bedsPerPerson": 0.5 } } })", r);
		Check(error.empty() && r.Weights.size() == 2 && r.Needs == std::vector<std::string>{ "Hunger" } && r.FoodDays == 5.0f && r.BedsPerPerson == 0.5f, "the section reads" + (error.empty() ? std::string() : ": " + error));
		Check(Near(Qol({ { "needs", 1.0f }, { "food", 0.0f }, { "shelter", 1.0f } }, r), 0.5f), "only the parts it weighs count");
		SocietyMetricRules back;
		const std::string again = LoadSocietyMetricRules("{ \"metrics\": " + SocietyMetricRulesToJson(r) + " }", back);
		Check(again.empty() && back == r, "and is written back as it reads");
		SocietyMetricRules same = r;
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "qol": { "weights": { "beauty": 1 } } } })", same).empty() && same == r, "an unknown part is a mistake and changes nothing");
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "qol": { "weights": { "needs": 0 } } } })", same).empty() && same == r, "weights adding up to 0 are a mistake");
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "comfort": {} } })", same).empty() && same == r, "an unknown key is a mistake");
		Check(LoadSocietyMetricRules(R"({ "items": [] })", same).empty() && same == r, "a file without the section changes nothing");
	}
	{
		std::ifstream in(std::string(GB_ASSETS_DIR) + "/data/era1.json", std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		SocietyMetricRules data;
		data.Weights.clear();
		const std::string error = LoadSocietyMetricRules(text, data);
		Check(error.empty() && data == SocietyMetricRules{}, "era1.json's \"metrics\" says the built-in weights (the editor, which does not read it yet, weighs the same)" + (error.empty() ? std::string() : ": " + error));
	}

	// ---- lineages
	{
		Person ana, biel, cai, dora, eli, fia, gus;
		ana.Generation = biel.Generation = cai.Generation = dora.Generation = 0;
		eli.Generation = 1; eli.Mother = "Ana"; eli.Father = "Biel";      // two founders
		fia.Generation = 1; fia.Mother = "Cai"; fia.Father = "Zed";       // a father nobody knows (another clan, gone): its own line
		gus.Generation = 2; gus.Mother = "Eli"; gus.Father = "Ivo";        // grandchild of Ana and Biel
		const std::map<std::string, const Person*> people = { { "Ana", &ana }, { "Biel", &biel }, { "Cai", &cai }, { "Dora", &dora }, { "Eli", &eli }, { "Fia", &fia }, { "Gus", &gus } };
		Check(Lineages(people, { "Ana", "Dora" }).empty(), "founders alone are no lineage yet");
		Check(Lineages(people, { "Gus" }) == std::vector<std::string>{ "Ana", "Biel", "Ivo" }, "a grandchild carries the lines of its grandparents (and of a parent nobody knows)");
		Check(Lineages(people, { "Eli", "Fia", "Gus", "Dora" }).size() == 5, "Ana, Biel, Cai, Zed and Ivo: Dora has no child");
	}

	// ---- a real clan
	{
		WorldDesc d;
		d.Type = WorldType::Temperate;
		d.Seed = 3;
		d.Resolution = 129;
		const GeneratedWorld world = WorldGenerator::GenerateData(d);
		SimSetup setup;
		setup.World = &world;
		setup.Members = 4;
		SimRun run(setup);
		Check(run.Ok(), "a clan of four" + (run.Ok() ? std::string() : ": " + run.Error()));
		if (run.Ok())
		{
			run.Run(5.0);
			Tribe& t = run.Brain();
			t.Store().SetStacks({});
			for (TribeMember& m : t.Members()) if (m.Body) m.Body->Bag().SetStacks({});
			const SocietyMeasure hungry = MeasureSociety(t, rules);
			Check(hungry.Alive == 4 && hungry.Generations == 0 && hungry.Lineages == 0 && hungry.Techs > 0, "four founders, generation 0, no lineage yet and the technologies of the start");
			Check(Near(hungry.Parts.at("food"), 0.0f) && Near(hungry.Parts.at("shelter"), 0.0f), "with nothing in the store and no buildings: no food, no shelter");
			Check(hungry.Parts.at("needs") > 0.0f && hungry.Parts.at("longevity") < 0.0f, "its needs are told and its longevity is not (nobody died)");
			const ItemLibrary* items = t.Items();
			Check(items && t.Store().Add(*items, "berry", 200) > 0, "berries for the store");
			const SocietyMeasure fed = MeasureSociety(t, rules);
			Check(Near(fed.Parts.at("food"), std::min(1.0f, t.FoodDays() / rules.FoodDays)) && fed.Parts.at("food") > 0.25f, "with food for days the food part is the days over FoodDays (" + std::to_string(fed.Parts.at("food")) + ")");
			Check(fed.Qol > hungry.Qol + 0.04f, "and the quality of life rises (" + std::to_string(hungry.Qol) + " -> " + std::to_string(fed.Qol) + ")");
			const std::string line = SocietyLine(fed);
			Check(line.rfind("society: ", 0) == 0 && line.find(" qol ") != std::string::npos && line.find(" lineages 0") != std::string::npos, "and it reads as a line: " + line);
		}
	}

	std::printf("society_metrics: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
