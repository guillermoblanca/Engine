// W1: the food a clan lays by for the winter (GB/Game/Economy.h, WinterStockDays): gathered for from the autumn on, not in the winter itself (the
// reserve kept from births, WinterReserveDays, is what is left of it then), nothing in spring and summer, and only of a food that, fresh, keeps until growth returns in the store it would sit in. The "winterStock" part
// of the economy section reads, refuses its mistakes and writes back the same. CPU only, no Tribe.
#include "GB/Game/Economy.h"
#include "GB/Game/Society.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/AI/Needs.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
}

int GBTest_winter_stock(int, char**)
{
	using namespace GB;
	EconomyRules rules;
	rules.Spoilage.Seasons = { { "winter", 0.5f } };
	rules.WinterStock.Days = 14.0f;
	rules.WinterStock.From = 0.25f;
	SeasonClock clock;
	clock.SeasonSeconds = 16800.0f;
	const double S = 16800.0;
	ItemDef nuts; nuts.Id = "nuts"; nuts.ShelfLife = 14400.0f;
	ItemDef roots; roots.Id = "roots"; roots.ShelfLife = 7200.0f;
	ItemDef berry; berry.Id = "berry"; berry.ShelfLife = 1800.0f;
	ItemDef honey; honey.Id = "honey"; honey.ShelfLife = 0.0f;   // (never goes off)

	// spring and summer: nothing is laid by; autumn before From neither
	Check(WinterStockDays(rules, clock, 0.5 * S, nuts, 1.0f) == 0.0f && WinterStockDays(rules, clock, 1.5 * S, nuts, 1.0f) == 0.0f, "nothing is laid by in spring or summer");
	Check(WinterStockDays(rules, clock, 2.1 * S, nuts, 1.0f) == 0.0f, "nor in the autumn before From");
	// autumn after From: the whole winter, of what keeps (nuts in an open store: 0.5 x 16800 + 0.5 x 16800 = 16800 > 14400: they do not keep)
	Check(WinterStockDays(rules, clock, 2.5 * S, nuts, 1.0f) == 0.0f, "nuts in an open store at mid-autumn would go off before spring");
	Check(Near(WinterStockDays(rules, clock, 2.5 * S, nuts, 0.5f), 14.0f), "in a granary (0.5) they keep: the whole winter is wanted");
	Check(Near(WinterStockDays(rules, clock, 2.5 * S, honey, 1.0f), 14.0f), "what never goes off is always laid by");
	// roots (7200) in a jar store (0.35): late in autumn they keep, early they do not; berries never do
	Check(WinterStockDays(rules, clock, 2.26 * S, roots, 0.35f) == 0.0f, "roots gathered early in autumn would rot before spring");
	Check(Near(WinterStockDays(rules, clock, 2.8 * S, roots, 0.35f), 14.0f), "late in autumn they keep in a jar store");
	Check(WinterStockDays(rules, clock, 2.9 * S, berry, 0.35f) == 0.0f, "berries are not laid by: they are dried first");
	// in winter nothing is gathered for it (there is nothing to find); the reserve below still counts what is left of it
	Check(WinterStockDays(rules, clock, 3.5 * S, nuts, 1.0f) == 0.0f, "in winter nothing is gathered for the reserve");
	Check(WinterStockDays(rules, clock, 4.1 * S, nuts, 1.0f) == 0.0f, "and nothing in the next spring");
	// the reserve kept from what is to spare (births): the same days, whatever the food
	Check(Near(WinterReserveDays(rules, clock, 2.5 * S), 14.0f) && Near(WinterReserveDays(rules, clock, 3.5 * S), 7.0f) && WinterReserveDays(rules, clock, 1.5 * S) == 0.0f,
		"the reserve is the winter's days in autumn, what is left of it in winter, nothing in summer");
	// off: Days 0
	EconomyRules off = rules;
	off.WinterStock.Days = 0.0f;
	Check(WinterStockDays(off, clock, 2.9 * S, honey, 1.0f) == 0.0f, "days 0 lays nothing by");

	// the data: read, refuse and write back
	EconomyRules read;
	Check(LoadEconomyRules(R"({ "economy": { "winterStock": { "days": 10, "from": 0.5 } } })", read).empty() && Near(read.WinterStock.Days, 10.0f) && Near(read.WinterStock.From, 0.5f),
		"the winterStock part reads");
	EconomyRules keep = read;
	Check(!LoadEconomyRules(R"({ "economy": { "winterStock": { "days": 10, "from": 2 } } })", keep).empty() && keep == read, "from past 1 is refused and changes nothing");
	Check(!LoadEconomyRules(R"({ "economy": { "winterStock": { "weeks": 2 } } })", keep).empty() && keep == read, "an unknown key is refused");
	EconomyRules back;
	Check(LoadEconomyRules(std::string(R"({ "economy": )") + EconomyRulesToJson(read) + "}", back).empty() && back.WinterStock == read.WinterStock, "it writes back what it read");

	// W5: a building's foodCap (more room for each food only) reads and writes back
	std::map<std::string, BuildingDef> defs;
	Check(LoadBuildings(R"({ "buildings": { "pit": { "name": "Pit", "storeCap": 10, "foodCap": 60, "phases": [ { "name": "base", "work": 4, "needs": { "stone": 2 } } ] } } })", defs).empty()
		&& defs.count("pit") && defs.at("pit").StoreCap == 10 && defs.at("pit").FoodCap == 60, "foodCap reads beside storeCap");
	std::map<std::string, BuildingDef> defsBack;
	Check(LoadBuildings("{ \"buildings\": " + BuildingsToJson(defs) + " }", defsBack).empty() && defsBack.count("pit") && defsBack.at("pit").FoodCap == 60, "foodCap writes back");
	std::map<std::string, BuildingDef> bad;
	Check(!LoadBuildings(R"({ "buildings": { "pit": { "foodCap": -1, "phases": [ { "name": "base", "work": 4, "needs": { "stone": 2 } } ] } } })", bad).empty(), "a negative foodCap is refused");

	// W5: how fast hunger kills is data (society.life.starveDamage)
	SocietyRules society;
	Check(LoadSocietyRules(R"({ "society": { "life": { "starveDamage": 0.05 } } })", society).empty() && Near(society.Life.StarveDamage, 0.05f), "life starveDamage reads");
	SocietyRules societyKeep = society;
	Check(!LoadSocietyRules(R"({ "society": { "life": { "starveDamage": 0 } } })", societyKeep).empty() && societyKeep == society, "a starveDamage of 0 is refused (they would never starve)");
	SocietyRules societyBack;
	Check(LoadSocietyRules(std::string(R"({ "society": )") + SocietyRulesToJson(society) + "}", societyBack).empty() && Near(societyBack.Life.StarveDamage, 0.05f), "starveDamage writes back");
	Check(SocietyRules().Life.StarveDamage == NeedsConfig().StarveDamage, "without the data, the old rate of the needs");

	if (failures == 0) std::printf("winter_stock: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
