// The settlement planner alone (GB/Game/SettlementPlanner.h): what the clan builds and where, from a SettlementFacts and a SitePlacement, with no Tribe,
// no scene and no bodies. Checked: nothing without buildings, people or free sites; beds first and in proportion to who lacks one; a shelter being built
// answers the need; a better home supersedes the older one; a full store asks for a store once; provisions (healing, defense) weigh with the events of the
// clan; a station a recipe needs; the order of the answer is stable (urgency, then id); a spot clears the camp, the fire, the buildings and the nodes and
// is the same every time; with no ground to stand on there is none. CPU only.
#include "GB/Game/SettlementPlanner.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	GB::BuildingDef Def(const std::string& id, int shelter = 0)
	{
		GB::BuildingDef d;
		d.Id = id;
		d.Name = id;
		d.Shelter = shelter;
		return d;
	}
}

int main()
{
	std::map<std::string, GB::BuildingDef> defs;
	defs["shelter"] = Def("shelter", 2);
	defs["hut"] = Def("hut", 4);
	defs["hut"].Supersedes = { "shelter" };
	defs["store"] = Def("store");
	defs["store"].Store = "stockpile";
	defs["healer"] = Def("healer");
	defs["healer"].Healing = 0.5f;
	defs["wall"] = Def("wall");
	defs["wall"].Defense = 1.0f;
	defs["rack"] = Def("rack");
	defs["rack"].Station = "drying_rack";

	GB::SettlementFacts facts;
	facts.Buildings = &defs;
	facts.Alive = 4;
	facts.MaxOpenSites = 1;
	facts.StoreCap = 40;
	auto only = [&](std::initializer_list<const char*> ids)
	{
		std::map<std::string, GB::BuildingDef> keep;
		for (const char* id : ids) keep[id] = defs.at(id);
		return keep;
	};

	Check(GB::PlanBuildingNeeds(GB::SettlementFacts()).empty(), "no buildings, no needs");
	{
		GB::SettlementFacts none = facts;
		none.Alive = 0;
		Check(GB::PlanBuildingNeeds(none).empty(), "nobody alive, no needs");
	}

	// beds
	{
		const auto shelterOnly = only({ "shelter" });
		GB::SettlementFacts f = facts;
		f.Buildings = &shelterOnly;
		const auto needs = GB::PlanBuildingNeeds(f);
		Check(needs.size() == 1 && needs[0].Building == "shelter" && std::fabs(needs[0].Urgency - 3.0f) < 1e-5f, "four people with no bed: a shelter at urgency 3");
		const GB::BuildingDef& shelter = defs.at("shelter");
		f.Sites = { { &shelter, { 3.0f, 0.0f }, true } };
		const auto half = GB::PlanBuildingNeeds(f);
		Check(half.size() == 1 && std::fabs(half[0].Urgency - 1.5f) < 1e-5f && half[0].Why == "beds for 2 of 4", "two beds for four: half the urgency, and it says why");
		f.Sites.push_back({ &shelter, { -3.0f, 0.0f }, false });
		Check(GB::PlanBuildingNeeds(f).empty(), "a shelter being built answers the need (and as many sites are open as allowed)");
		f.MaxOpenSites = 2;
		Check(GB::PlanBuildingNeeds(f).empty(), "... and with room for another site there are still no beds lacking");
	}
	// superseded
	{
		const auto both = only({ "shelter", "hut" });
		GB::SettlementFacts f = facts;
		f.Buildings = &both;
		const auto needs = GB::PlanBuildingNeeds(f);
		Check(needs.size() == 1 && needs[0].Building == "hut", "the hut takes the place of the shelter once the clan can raise it");
		f.Known = [](const GB::BuildingDef& d) { return d.Id != "hut"; };
		const auto old = GB::PlanBuildingNeeds(f);
		Check(old.size() == 1 && old[0].Building == "shelter", "while it cannot, the shelter stays");
		f.Known = nullptr;
		f.Payable = [](const GB::BuildingDef& d) { return d.Id != "shelter" && d.Id != "hut"; };
		Check(GB::PlanBuildingNeeds(f).empty(), "what cannot be paid is not planned");
	}
	// store, healing, defense
	{
		const auto kit = only({ "store", "healer", "wall" });
		GB::SettlementFacts f = facts;
		f.Buildings = &kit;
		auto needs = GB::PlanBuildingNeeds(f);
		Check(needs.size() == 2 && needs[0].Building == "wall" && needs[1].Building == "healer", "a calm clan wants walls (0.35) before a healer (0.3), and no store while its own is not full");
		f.FullItem = "wood";
		f.FullCount = 39;
		f.Wounded = 2;
		f.Raided = 1;
		needs = GB::PlanBuildingNeeds(f);
		Check(needs.size() == 3 && needs[0].Building == "store" && needs[0].Urgency == 2.0f && needs[0].Why == "the store is full of wood (39/40)", "a full store asks for a store first (ties by id after it)");
		Check(needs[1].Building == "wall" && needs[1].Urgency == 2.0f && needs[2].Building == "healer" && needs[2].Urgency == 1.2f, "a raided clan weighs walls 2, wounded weigh a healer 1.2");
		const GB::BuildingDef& store = defs.at("store");
		f.Sites = { { &store, { 5.0f, 0.0f }, true } };
		bool again = false;
		for (const auto& n : GB::PlanBuildingNeeds(f)) again = again || n.Building == "store";
		Check(!again, "only the first of a kind is asked for");
	}
	// a station a recipe needs
	{
		const auto rack = only({ "rack" });
		GB::SettlementFacts f = facts;
		f.Buildings = &rack;
		f.StationThere = [](const std::string&) { return false; };
		f.RecipeNeeding = [](const std::string& s) { return s == "drying_rack" ? std::string("dried_berry") : std::string(); };
		auto needs = GB::PlanBuildingNeeds(f);
		Check(needs.size() == 1 && needs[0].Urgency == 1.5f && needs[0].Why == "the recipe dried_berry needs a drying_rack", "a recipe that needs a station asks for it");
		f.StationThere = [](const std::string&) { return true; };
		f.StationIsImprovised = [](const std::string&) { return true; };
		needs = GB::PlanBuildingNeeds(f);
		Check(needs.size() == 1 && needs[0].Urgency == 0.5f, "an improvised station asks for the real one, mildly");
		f.StationIsImprovised = nullptr;
		Check(GB::PlanBuildingNeeds(f).empty(), "a real station asks for nothing");
	}

	// where
	{
		GB::BuildingDef d = Def("hut", 2);
		GB::SitePlacement p;
		p.Def = &d;
		p.Camp = { 10.0f, 10.0f };
		p.Fire = { 11.5f, 10.0f };
		p.SiteRadius = 7.0f;
		const auto spot = GB::PlaceSite(p);
		Check(spot.has_value(), "a free camp has a place");
		Check(spot && std::fabs(glm::distance(*spot, p.Camp) - 7.0f) < 1e-4f, "the first ring is SiteRadius from the camp");
		Check(GB::PlaceSite(p) == spot, "the same scene gives the same place");
		GB::SitePlacement q = p;
		q.Others.push_back({ &d, *spot, true });
		const auto next = GB::PlaceSite(q);
		Check(next && glm::distance(*next, *spot) >= 2.0f * d.Radius() + 1.5f - 1e-4f, "a second building keeps clear of the first");
		q.Obstacles.push_back(*next);
		const auto third = GB::PlaceSite(q);
		Check(third && glm::distance(*third, *next) >= d.Radius() + 1.2f - 1e-4f, "and a spot keeps off the resource nodes");
		GB::SitePlacement water = p;
		water.CanStand = [](const glm::vec2&) { return false; };
		Check(!GB::PlaceSite(water).has_value(), "with no ground to stand on there is no place");
		GB::SitePlacement east = p;
		east.CanStand = [&](const glm::vec2& at) { return at.x > p.Camp.x; };
		const auto onEast = GB::PlaceSite(east);
		Check(onEast && onEast->x > p.Camp.x, "a spot only where a member can stand");
		Check(std::fabs(GB::SiteFront({ 0.0f, 0.0f }, d, 0.6f).x - 0.6f) < 1e-6f && GB::SiteFront({ 0.0f, 0.0f }, d).y > 0.5f * d.Footprint.y, "members work in front of the site");
	}

	if (failures == 0) std::printf("settlement planner: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
