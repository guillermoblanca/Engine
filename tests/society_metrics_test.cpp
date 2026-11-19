// T204 (S1 of VISION_MUNDO_VIVO section 13): the measure of a society (GB/Game/SocietyMetrics.h). Checked on the CPU, with no window:
//  * the quality of life is the weighted mean of its parts; a part that cannot be told is left out and the rest weigh more;
//  * a clan with no food, no beds and nothing met is low, one with beds, food and its needs met is high;
//  * the "metrics" section reads and writes back, refuses what it does not know, and the game's data says the built-in weights;
//  * lineages are the founders with living descendants, through both parents and across clans;
//  * on a real clan (a SimRun): emptying the store and the bags makes the food part 0, filling the store raises it and the index;
//  * D33: the curves that never fill (techs, food), the losses of the recent young dead (an elder's death or an old one takes nothing off; two
//    young ones today make the index fall visibly, and it comes back up as they fade).
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Game/History.h"
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

	// ---- D33: the curves that never fill and the losses
	Check(Saturate(0.0f, 10.0f) == 0.0f && Near(Saturate(10.0f, 10.0f), 0.5f) && Near(Saturate(30.0f, 10.0f), 0.75f), "n / (n + half): 0, a half at half, 3/4 at three halves");
	Check(Saturate(41.0f, 10.0f) - Saturate(40.0f, 10.0f) < Saturate(11.0f, 10.0f) - Saturate(10.0f, 10.0f) && Saturate(200.0f, 10.0f) < 1.0f,
		"each one more adds less than the one before, and it never fills (a tree of 200 does not saturate it)");
	Check(Losses(0.0f, 10, rules) == 0.0f, "no young dead: nothing taken off");
	Check(Near(Losses(1.0f, 9, rules), 0.1f * rules.LossScale), "one young dead today in a clan left with 9: a tenth (times LossScale)");
	Check(Near(Losses(6.0f, 4, rules), std::min(1.0f, 0.6f * rules.LossScale)), "six young dead and four alive (clan III of seed 5): six tenths, so it does not read as a well-off clan");
	Check(Losses(0.5f, 9, rules) < Losses(1.0f, 9, rules), "a death of some days ago weighs less than today's");

	// ---- the section
	{
		SocietyMetricRules r;
		const std::string error = LoadSocietyMetricRules(R"({ "metrics": { "qol": { "weights": { "needs": 1, "food": 1 }, "needs": [ "Hunger" ], "needsFloor": 0.4, "foodHalf": 3,
			"bedsPerPerson": 0.5, "bedComfort": 0.6, "toolPower": 3, "techsHalf": 25, "lossDays": 4, "lossScale": 2 } } })", r);
		Check(error.empty() && r.Weights.size() == 2 && r.Needs == std::vector<std::string>{ "Hunger" } && r.NeedsFloor == 0.4f && r.FoodHalf == 3.0f && r.BedsPerPerson == 0.5f
			&& r.BedComfort == 0.6f && r.ToolPower == 3.0f && r.TechsHalf == 25.0f && r.LossDays == 4.0f && r.LossScale == 2.0f, "the section reads" + (error.empty() ? std::string() : ": " + error));
		Check(Near(Qol({ { "needs", 1.0f }, { "food", 0.0f }, { "shelter", 1.0f } }, r), 0.5f), "only the parts it weighs count");
		SocietyMetricRules back;
		const std::string again = LoadSocietyMetricRules("{ \"metrics\": " + SocietyMetricRulesToJson(r) + " }", back);
		Check(again.empty() && back == r, "and is written back as it reads");
		SocietyMetricRules same = r;
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "qol": { "weights": { "beauty": 1 } } } })", same).empty() && same == r, "an unknown part is a mistake and changes nothing");
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "qol": { "weights": { "needs": 0 } } } })", same).empty() && same == r, "weights adding up to 0 are a mistake");
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "comfort": {} } })", same).empty() && same == r, "an unknown key is a mistake");
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "qol": { "foodDays": 10 } } })", same).empty() && same == r, "and so is the old foodDays (the food part is a curve now: foodHalf)");
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "qol": { "needsFloor": 1 } } })", same).empty() && same == r, "a needs floor of 1 is a mistake (nothing over it)");
		Check(!LoadSocietyMetricRules(R"({ "metrics": { "qol": { "techsHalf": 0 } } })", same).empty() && same == r, "a half of 0 is a mistake");
		SocietyMetricRules off = r;
		Check(LoadSocietyMetricRules(R"({ "metrics": { "qol": { "lossScale": 0 } } })", off).empty() && off.LossScale == 0.0f && Losses(3.0f, 5, off) == 0.0f, "a loss scale of 0 turns the losses off");
		Check(LoadSocietyMetricRules(R"({ "items": [] })", same).empty() && same == r, "a file without the section changes nothing");
	}
	{
		SocietyMetricRules data;   // T347: the section as the game's pack gives it (whatever file has it)
		data.Weights.clear();
		const std::string error = LoadShippedSection(data, [](const std::string& doc, SocietyMetricRules& r) { return LoadSocietyMetricRules(doc, r); });
		Check(error.empty() && data == SocietyMetricRules{}, "the pack's \"metrics\" says the built-in weights (the editor, which does not read it yet, weighs the same)" + (error.empty() ? std::string() : ": " + error));
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
			Check(items && t.Store().Add(*items, "nuts", 200) > 0, "nuts for the store");   // T360: food for days that lasts them (berries keep a day and a half)
			const SocietyMeasure fed = MeasureSociety(t, rules);
			Check(Near(fed.Parts.at("food"), Saturate(t.FoodDays(), rules.FoodHalf)) && fed.Parts.at("food") > 0.25f, "with food for days the food part is days / (days + FoodHalf) (" + std::to_string(fed.Parts.at("food")) + ")");
			Check(fed.Qol > hungry.Qol + 0.04f, "and the quality of life rises (" + std::to_string(hungry.Qol) + " -> " + std::to_string(fed.Qol) + ")");
			const std::string line = SocietyLine(fed);
			Check(line.rfind("society: ", 0) == 0 && line.find(" qol ") != std::string::npos && line.find(" lineages 0") != std::string::npos, "and it reads as a line: " + line);

			// D33: the young dead of the last days take off the index (read from the history: "death" or "killed", the dead in Whom)
			WorldHistory past;
			past.SetClock(t.WorldSeconds());
			t.SetHistory(&past, 0);
			const SocietyMeasure calm = MeasureSociety(t, rules);
			Check(calm.Losses == 0.0f && Near(calm.Qol, fed.Qol), "a history with no deaths takes nothing off");
			const float day = t.Config.Society.Life.DaySeconds > 0.0f ? t.Config.Society.Life.DaySeconds : t.Config.Needs.DaySeconds;
			auto bury = [&](const std::string& name, float age, double ago, const char* kind)
			{
				Society& people = t.People();
				people.Join(t.Config.Society, name);
				people.SetAge(name, age);
				people.Died(t.Config.Society, name, "Hunger");
				HistoryEvent e;
				e.At = t.WorldSeconds() - ago * day;
				e.Kind = kind;
				e.Whom = name;
				e.Clan = 0;
				past.Add(e);
			};
			bury("Old Una", t.Config.Society.Life.ElderFrom + 10.0f, 0.0, "death");             // an elder: that is longevity, not a loss
			bury("Long Gone", 20.0f, static_cast<double>(rules.LossDays) + 1.0, "death");      // too long ago
			const SocietyMeasure elders = MeasureSociety(t, rules);
			Check(elders.Losses == 0.0f, "an elder's death and a young one of long ago take nothing off (" + std::to_string(elders.YoungDead) + ")");
			bury("Young Ria", 20.0f, 0.0, "death");
			bury("Young Teo", 25.0f, 0.0, "killed");                                          // killed in a fight: its own kind of event
			const SocietyMeasure mourning = MeasureSociety(t, rules);
			Check(Near(mourning.YoungDead, 2.0f, 1e-3f) && Near(mourning.Losses, Losses(2.0f, 4, rules)), "two young dead today, one of hunger and one killed, both count (" + std::to_string(mourning.YoungDead) + ")");
			Check(mourning.Qol < 0.8f * elders.Qol, "and the index falls visibly (" + std::to_string(elders.Qol) + " -> " + std::to_string(mourning.Qol) + ")");
			past.SetClock(t.WorldSeconds() + 0.5 * static_cast<double>(rules.LossDays) * day);
			const SocietyMeasure later = MeasureSociety(t, rules);
			Check(later.YoungDead > 0.9f && later.YoungDead < 1.1f && later.Qol > mourning.Qol, "half the window later they weigh half and the index comes back up (" + std::to_string(later.Qol) + ")");
			t.SetHistory(nullptr, -1);
		}
	}

	std::printf("society_metrics: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
