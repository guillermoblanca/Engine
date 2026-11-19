// T359 (N2) and T360 (N4) of documentation/01_diseno/CIERRE_AGRICULTURA_2026-11-19.md §4.3, and the eating order of T300's second pass:
//   days      (N4) Tribe::FoodDays (FoodStock::Days) counts a unit only for what it lasts: 1000 berries about to go off are no days of food, nor
//             of the winter (WinterOutlook); 1000 fresh berries are the days they last (1.5), not 47; what keeps counts whole, as before.
//   ceiling   (N2) with work.jobs.foodCeilingDays a clan with that many days of food in hand posts no food gathering on its board; under it, it
//             does; with 0 there is no ceiling.
//   eat       a clan that keeps cooked meals eats one before the berries (unless something is about to go off).
// CPU only: a tribe with no bodies.
#include "GB/Game/ClanCooking.h"
#include "GB/Game/Economy.h"
#include "GB/Game/FoodCeiling.h"
#include "GB/Game/FoodStock.h"
#include "GB/Game/Social.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/WinterOutlook.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Scene/Entity.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	using namespace GB;
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	// A body that stands still (the board counts the living with a body)
	class StillBody : public ITribeBody
	{
	public:
		bool Busy() const override { return false; }
		bool Failed() const override { return false; }
		glm::vec2 Position() const override { return glm::vec2(0.0f); }
		bool Do(const std::string&, std::string&) override { return true; }
		Inventory& Bag() override { return m_Bag; }
	private:
		Inventory m_Bag{ 12, 0.0f };
	};

	struct Clan
	{
		explicit Clan(int n)
		{
			RegisterEraOneItems(Items, Recipes, Defs);
			Check(LoadShippedSection(Tribe_.Config.Econ, [](const std::string& doc, EconomyRules& r) { return LoadEconomyRules(doc, r); }).empty(), "the economy section loads");
			Tribe_.Config.Work = DefaultWorkRules();
			Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
			Bodies.resize(static_cast<size_t>(n));
			for (int i = 0; i < n; ++i) Tribe_.AddMember("m" + std::to_string(i), "gatherer", &Bodies[static_cast<size_t>(i)]);
		}
		ItemLibrary Items;
		RecipeBook Recipes;
		std::map<std::string, ResourceNodeDef> Defs;
		EntityWorld Entities;
		GameplayWorld Gameplay;
		std::vector<StillBody> Bodies;
		Tribe Tribe_;
	};
}

int GBTest_food_ceiling(int, char**)
{
	failures = 0;
	const float perDay = 4.0f * (1200.0f / 900.0f) * 4.0f;   // (4 living, 100 hunger in 900 s of a 1200 s day, 25 a plain meal)
	// ---- days (N4)
	{
		Clan k(4);
		k.Tribe_.Store().Add(k.Items, "berry", 1000, 1790.0f);   // 10 s left
		const float rotten = k.Tribe_.FoodDays();
		const WinterOutlook w = ForecastWinter(k.Tribe_);
		std::printf("food_ceiling: 1000 berries 10 s from going off: %.2f days, winter outlook %.2f\n", static_cast<double>(rotten), static_cast<double>(w.FoodDays));
		Check(rotten < 0.05f && w.FoodDays < 0.05f, "(days) 1000 berries about to go off are no days of food, nor of the winter");

		Clan f(4);
		f.Tribe_.Store().Add(f.Items, "berry", 1000);
		const float fresh = f.Tribe_.FoodDays();
		std::printf("food_ceiling: 1000 fresh berries: %.2f days (they last 1.5)\n", static_cast<double>(fresh));
		Check(std::fabs(fresh - 1.5f) < 0.05f, "(days) 1000 fresh berries are the 1.5 days they last, not " + std::to_string(1000.0f / perDay));

		Clan g(4);
		g.Tribe_.Store().Add(g.Items, "berry", 20);
		g.Tribe_.Store().Add(g.Items, "honey", 50);   // (never goes off)
		const float whole = g.Tribe_.FoodDays();
		const float expected = (20.0f + 50.0f * FoodStock::Meals(g.Tribe_, "honey")) / perDay;
		Check(std::fabs(whole - expected) < 1e-3f, "(days) what lasts until its turn counts whole, as before");
	}

	// ---- ceiling (N2)
	{
		Clan k(4);
		Check(k.Tribe_.Config.Work.Jobs.FoodCeilingDays == 8.0f, "(ceiling) the game's data: foodCeilingDays 8");
		k.Tribe_.PostJobs();
		Check(k.Tribe_.Board().Find("gather berry") != nullptr, "(ceiling) with no food the board asks for berries");
		Check(!FoodCeiling::Reached(k.Tribe_), "(ceiling) not reached with no food");
		const int honey = static_cast<int>(std::ceil(9.0f * perDay / FoodStock::Meals(k.Tribe_, "honey")));
		k.Tribe_.Store().Add(k.Items, "honey", honey);   // nine days of food that keeps
		Check(FoodCeiling::Reached(k.Tribe_), "(ceiling) reached with nine days of food");
		k.Tribe_.PostJobs();
		Check(k.Tribe_.Board().Find("gather berry") == nullptr && k.Tribe_.Board().Find("gather roots") == nullptr, "(ceiling) past it the board asks for no food");
		k.Tribe_.Config.Work.Jobs.FoodCeilingDays = 0.0f;
		Check(!FoodCeiling::Reached(k.Tribe_), "(ceiling) with 0 there is none");
	}

	// ---- eat: cooked meals first (ClanCooking::EatFirst, what Tribe::FoodToEat asks first while economy.production.meals.perMember > 0)
	{
		Clan k(4);
		const SocialRules social = DefaultSocialRules();
		ClanCooking::Query q;
		q.Items = &k.Items;
		q.Recipes = &k.Recipes;
		q.IsFood = [&](const std::string& id) { return k.Tribe_.IsFood(id); };
		q.Nutrition = [&](const std::string& id) { const auto f = social.Diet.Foods.find(id); return f != social.Diet.Foods.end() ? f->second.Nutrition : 25.0f; };
		const float rawSoon = k.Tribe_.Config.Econ.Production.Meals.RawSoon;
		Check(k.Tribe_.Config.Econ.Production.Meals.PerMember > 0.0f && rawSoon > 0.0f, "(eat) the game's data keeps cooked meals");
		Inventory inv(20, 0.0f);
		inv.Add(k.Items, "berry", 10);
		inv.Add(k.Items, "grain", 10);
		inv.Add(k.Items, "parched_grain", 3);
		Check(ClanCooking::EatFirst(q, inv, rawSoon) == "parched_grain", "(eat) a cooked meal before the berries");
		Inventory soon(20, 0.0f);
		soon.Add(k.Items, "berry", 10, 1500.0f);   // (300 s left: under rawSoon)
		soon.Add(k.Items, "parched_grain", 3);
		Check(ClanCooking::EatFirst(q, soon, rawSoon).empty(), "(eat) but berries about to go off first (the old rule)");
		Inventory raw(20, 0.0f);
		raw.Add(k.Items, "berry", 10);
		Check(ClanCooking::EatFirst(q, raw, rawSoon).empty(), "(eat) no cooked meal: the old rule");
	}
	return failures ? 1 : 0;
}
