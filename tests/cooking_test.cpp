// T358 (N1) and T300 (N3, P-A8) of documentation/01_diseno/CIERRE_AGRICULTURA_2026-11-19.md §4.3: what keeps food and what cooks it
// (GB/Game/ClanCooking.h), and how much the clan of the AI asks for (Tribe::PlanNow, TribeEconomy.cpp). Checked, with the game's items, recipes,
// diet and economy section:
//   the rules     a recipe keeps a food only if none of its other perishable inputs outlives the output (pottage does not keep greens: the
//                 lentils last a month); dried berries keep berries; parched grain, porridge, pottage and bread are meals, dried berries and
//                 smoked meat are not; production.preserveDays and production.meals read and write back.
//   (a) E6        with greens that go off and lentils that keep, no pottage is planned to save the greens; a surplus of berries is dried only
//                 up to preserveDays of what the clan eats of the fruit group (none once it keeps that much).
//   (b) meals     a clan that knows cooking, with grain and a hearth, cooks up to meals.perMember meals a living member and no more; raw meat
//                 about to go off is cooked (rawSoon); bread asks for its flour through the planner (a want, the quern makes it).
// CPU only: a tribe with no bodies (the plan reads the store, the stations and what the clan knows).
#include "GB/Game/ClanCooking.h"
#include "GB/Game/Economy.h"
#include "GB/Game/Social.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Scene/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

namespace
{
	using namespace GB;
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	struct Clan
	{
		explicit Clan(int n, const std::set<std::string>& stations)
		{
			RegisterEraOneItems(Items, Recipes, Defs);
			Check(LoadShippedSection(Tribe_.Config.Econ, [](const std::string& doc, EconomyRules& r) { return LoadEconomyRules(doc, r); }).empty(), "the economy section loads");
			Tribe_.Config.Social = DefaultSocialRules();   // (the diet: nutrition and groups)
			Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
			for (int i = 0; i < n; ++i) Tribe_.AddMember("m" + std::to_string(i), "gatherer", nullptr);
			Tribe_.SetStations([stations](const std::string& s, glm::vec2& at) { at = glm::vec2(3.0f, 3.0f); return stations.count(s) > 0; });
		}
		const ProductionStep* Step(const ProductionPlan& plan, const std::string& recipe) const
		{
			for (const ProductionStep& s : plan.Steps) if (s.Recipe == recipe) return &s;
			return nullptr;
		}
		ItemLibrary Items;
		RecipeBook Recipes;
		std::map<std::string, ResourceNodeDef> Defs;
		EntityWorld Entities;
		GameplayWorld Gameplay;
		Tribe Tribe_;
	};

	std::string Plan(const ProductionPlan& p) { return PlanReport(p); }
}

int GBTest_cooking(int, char**)
{
	failures = 0;
	// ---- the rules
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> defs;
		RegisterEraOneItems(items, recipes, defs);
		const SocialRules social = DefaultSocialRules();
		ClanCooking::Query q;
		q.Items = &items;
		q.Recipes = &recipes;
		q.Nutrition = [&](const std::string& id) { const auto f = social.Diet.Foods.find(id); return f != social.Diet.Foods.end() ? f->second.Nutrition : 25.0f; };
		q.IsFood = [&](const std::string& id) { return id != "water" && items.Find(id) && items.Find(id)->Category == "food"; };   // (water is drunk, not eaten)
		auto r = [&](const char* id) { return recipes.Find(id); };
		Check(r("pottage") && r("dried_berry") && r("parched_grain") && r("porridge") && r("bread") && r("smoked_meat"), "the recipes of the test are in the data");
		if (failures) return 1;
		Check(!ClanCooking::Keeps(q, *r("pottage"), "greens"), "pottage does not keep greens: its lentils outlive it");
		Check(ClanCooking::Keeps(q, *r("dried_berry"), "berry") && ClanCooking::Keeps(q, *r("smoked_meat"), "meat"), "dried berries keep berries, smoked meat keeps meat");
		Check(!ClanCooking::Keeps(q, *r("dried_berry"), "berry", 9600.0f), "nothing keeps what already lasts as long as the output");
		Check(ClanCooking::IsMeal(q, *r("parched_grain")) && ClanCooking::IsMeal(q, *r("porridge")) && ClanCooking::IsMeal(q, *r("pottage")) && ClanCooking::IsMeal(q, *r("bread")),
			"parched grain, porridge, pottage and bread are cooked meals");
		Check(!ClanCooking::IsMeal(q, *r("dried_berry")) && !ClanCooking::IsMeal(q, *r("smoked_meat")), "dried berries and smoked meat are kept, not cooked");

		EconomyRules e;
		Check(LoadEconomyRules(R"({"economy":{"production":{"preserveDays":6,"meals":{"perMember":1.5,"rawSoon":600}}}})", e).empty()
			&& e.Production.PreserveDays == 6.0f && e.Production.Meals.PerMember == 1.5f && e.Production.Meals.RawSoon == 600.0f, "preserveDays and meals read");
		EconomyRules back;
		Check(LoadEconomyRules(R"({"economy":)" + EconomyRulesToJson(e) + "}", back).empty() && back == e, "and write back the same");
		EconomyRules bad;
		Check(!LoadEconomyRules(R"({"economy":{"production":{"meals":{"perMember":1,"often":2}}}})", bad).empty(), "an unknown key of meals is refused");
		Check(DefaultEconomyRules().Production.PreserveDays == 6.0f && DefaultEconomyRules().Production.Meals.PerMember == 2.0f, "the game's data: preserveDays 6, perMember 2");
	}

	// ---- (a) E6: no pottage to save greens; dried berries only up to preserveDays of what the clan eats of the fruit group
	{
		Clan k(4, { "hearth", "drying_rack" });
		k.Tribe_.Config.Econ.Production.Meals = {};   // (only E6 here: no meals for their own sake)
		k.Tribe_.Config.Econ.Logistics.Stores["main"].Cap = 1000;   // (the store's cap is not what stops it here)
		k.Tribe_.Store().Add(k.Items, "greens", 40);
		k.Tribe_.Store().Add(k.Items, "lentils", 30);
		k.Tribe_.Store().Add(k.Items, "roots", 20);
		const ProductionPlan greens = k.Tribe_.PlanNow();
		Check(!k.Step(greens, "pottage") && !k.Step(greens, "pottage_peas"), "(a) greens that go off are not saved with lentils that keep:\n" + Plan(greens));

		// what the clan eats of a group in six days: 6 days x the plain meals of a day x 4 living / 3 groups; dried berries sate 28 of 25
		const ProductionRules& p = k.Tribe_.Config.Econ.Production;
		const float perDay = (k.Tribe_.Config.Needs.DaySeconds / k.Tribe_.Config.Needs.HungerSeconds) * (100.0f / static_cast<float>(k.Tribe_.Config.Needs.FoodHunger));
		const float meals = p.PreserveDays * perDay * 4.0f / static_cast<float>(k.Tribe_.Config.Social.Diet.Varied);
		const int target = static_cast<int>(std::ceil(meals / (28.0f / 25.0f)));
		std::printf("cooking: (a) the fruit kept for %.0f days of 4: %d dried berries\n", static_cast<double>(p.PreserveDays), target);
		k.Tribe_.Store().Add(k.Items, "berry", 200);
		const ProductionPlan some = k.Tribe_.PlanNow();
		const ProductionStep* dry = k.Step(some, "dried_berry");
		Check(dry != nullptr, "(a) a surplus of berries is dried:\n" + Plan(some));
		k.Tribe_.Store().Add(k.Items, "dried_berry", target - 2);
		const ProductionPlan near = k.Tribe_.PlanNow();
		Check(k.Step(near, "dried_berry") && k.Step(near, "dried_berry")->Batches == 1, "(a) two short of the target: one batch, not three:\n" + Plan(near));
		k.Tribe_.Store().Add(k.Items, "dried_berry", 2);
		const ProductionPlan full = k.Tribe_.PlanNow();
		Check(!k.Step(full, "dried_berry"), "(a) at preserveDays of the fruit group: no more dried berries, whatever the surplus:\n" + Plan(full));
	}

	// ---- (b) meals: up to perMember a living member, and no more
	{
		Clan k(4, { "hearth" });
		k.Tribe_.Store().Add(k.Items, "grain", 40);
		const int wanted = static_cast<int>(std::ceil(k.Tribe_.Config.Econ.Production.Meals.PerMember * 4.0f));
		const ProductionPlan cook = k.Tribe_.PlanNow();
		const ProductionStep* parched = k.Step(cook, "parched_grain");
		Check(parched && parched->Ready && parched->Batches * 3 >= wanted && parched->Batches * 3 < wanted + 3, "(b) " + std::to_string(wanted) + " meals for 4: parched grain at the hearth:\n" + Plan(cook));
		Check(!k.Step(cook, "porridge") && !k.Step(cook, "porridge_barley"), "(b) no porridge without water in the store (and no want of it)");
		Check(!cook.Raw.count("water") && !cook.Raw.count("barley"), "(b) and nothing to gather for a meal it cannot make");
		k.Tribe_.Store().Add(k.Items, "parched_grain", wanted);
		const ProductionPlan enough = k.Tribe_.PlanNow();
		Check(!k.Step(enough, "parched_grain"), "(b) with " + std::to_string(wanted) + " cooked meals in hand, no more:\n" + Plan(enough));

		k.Tribe_.Store().Add(k.Items, "meat", 2, 500.0f);   // (400 s left of 900: under rawSoon)
		const ProductionPlan meat = k.Tribe_.PlanNow();
		Check(k.Step(meat, "cooked_meat") || k.Step(meat, "roast"), "(b) meat about to go off is cooked:\n" + Plan(meat));

		Clan b(4, { "hearth", "quern", "oven" });
		b.Tribe_.Store().Add(b.Items, "grain", 40);
		b.Tribe_.Store().Add(b.Items, "parched_grain", wanted - 2);
		b.Tribe_.Store().Add(b.Items, "lentils", 10);
		b.Tribe_.Store().Add(b.Items, "greens", 5);
		b.Tribe_.Store().Add(b.Items, "roots", 5);
		const ProductionPlan mixed = b.Tribe_.PlanNow();
		Check(b.Step(mixed, "pottage") != nullptr, "(b) the group it holds fewest meals of first: pottage beside the parched grain:\n" + Plan(mixed));
		b.Tribe_.Store().Remove("parched_grain", wanted - 2);
		b.Tribe_.Store().Remove("lentils", 10);
		const ProductionPlan bread = b.Tribe_.PlanNow();
		Check(b.Step(bread, "flatbread") || (b.Step(bread, "bread") && b.Step(bread, "flour")), "(b) with an oven the grain is baked (bread asks the quern for flour):\n" + Plan(bread));
		Check(!bread.Raw.count("water"), "(b) and nothing to gather for it");
	}
	return failures ? 1 : 0;
}
