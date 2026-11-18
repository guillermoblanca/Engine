// T280 (A1 of documentation/01_diseno/AGRICULTURA.md): the farm zones (GB/Game/FarmZones.h). Checked: a zone the player paints is tilled and sown
// with its crop (and the player's clan marks out no field of its own); a zone on taken ground, of an unknown crop or too big is refused; the crop,
// the plan (fallow: nothing tilled) and auto (the crop the store has least of) change what is sown; a zone is removed; the zones save and load;
// the clans of the AI mark out their zones by the data's rule (plotsPerMember, zonePlots) and by the old one without it; a crop's own pace by
// season (CropDef::Seasons) reads, saves and grows. CPU only, the stand-ins of economy_test.
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Game/GameContent.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/Spoilage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	constexpr float dt = 1.0f / 20.0f;
}

using namespace GB;

// File-local (T51: several tests share one executable, where two classes with one name would break the one-definition rule)
namespace
{
// The same stand-in as tribe_test: it runs the text of a task list against an ability system (goto and wait take time, "ability A node" waits for it).
class FakeBody : public ITribeBody
{
public:
	FakeBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
	bool Busy() const override { return !m_Steps.empty(); }
	bool Failed() const override { return m_Failed; }
	glm::vec2 Position() const override { return m_Pos; }
	Inventory& Bag() override { return m_Bag; }
	bool Do(const std::string& tasks, std::string& error) override
	{
		m_Steps.clear();
		m_Failed = false;
		std::stringstream ss(tasks);
		std::string part;
		while (std::getline(ss, part, '|'))
		{
			std::stringstream ws(part);
			Step s;
			ws >> s.Kind;
			if (s.Kind == "goto") ws >> s.X >> s.Z;
			else if (s.Kind == "wait") ws >> s.Seconds;
			else if (s.Kind == "ability") { ws >> s.Ability >> s.Node; for (char& c : s.Node) if (c == '_') c = ' '; }
			else { error = "unknown task " + s.Kind; m_Steps.clear(); return false; }
			m_Steps.push_back(s);
		}
		m_Started = false;
		return true;
	}
	void Update(float step)
	{
		AbilitySystemComponent* me = m_Gameplay.Find(m_Name);
		ResourceSystem::SyncTools(*me, m_Bag, *m_Items);
		if (m_Steps.empty()) return;
		Step& s = m_Steps.front();
		if (!m_Started)
		{
			m_Started = true;
			if (s.Kind == "goto") { s.Seconds = glm::distance(m_Pos, glm::vec2(s.X, s.Z)) / 3.0f; m_Target = { s.X, s.Z }; }
			if (s.Kind == "ability")
			{
				const EntityId id = m_Entities.FindByName(s.Node);
				const glm::vec4 p = id == NullEntity ? glm::vec4(0.0f) : m_Entities.WorldMatrix(id)[3];
				s.Seconds = glm::distance(m_Pos, glm::vec2(p.x, p.z)) / 3.0f;
				m_Target = { p.x, p.z };
				s.Walking = true;
			}
		}
		if (s.Seconds > 0.0f) { s.Seconds -= step; if (s.Seconds > 0.0f) return; m_Pos = m_Target; }
		if (s.Kind == "ability" && s.Walking)
		{
			s.Walking = false;
			if (!me->Activate(s.Ability, m_Gameplay.Find(s.Node), nullptr, s.Node)) { m_Failed = true; m_Steps.clear(); return; }
			return;
		}
		if (s.Kind == "ability" && me->AnyActive()) return;
		m_Steps.erase(m_Steps.begin());
		m_Started = false;
	}
	const ItemLibrary* m_Items = nullptr;
private:
	struct Step { std::string Kind, Ability, Node; float X = 0, Z = 0, Seconds = 0; bool Walking = false; };
	std::string m_Name;
	GameplayWorld& m_Gameplay;
	EntityWorld& m_Entities;
	glm::vec2 m_Pos, m_Target{ 0.0f };
	Inventory m_Bag{ 12, 0.0f };
	std::vector<Step> m_Steps;
	bool m_Started = false, m_Failed = false;
};

// A clan of `n` on a patch scaled to it, as `tribe build` does (trades from its needs). `extended`: with the game's economy section too (the grain
// crop and the fields).
struct Clan
{
	explicit Clan(int n, bool extended = false)
	{
		RegisterEraOneItems(Items, Recipes, Defs);   // (the whole catalogue: seeds, grain, the hoe and dried berries are in it)
		for (const char* far : { "oak_acorns", "granite_rock", "pine_resin", "wild_grain", "salt_flat", "old_bones", "fallen_trunk" }) Defs.erase(far);   // (not placed here)
		if (extended)
			Check(LoadEconomyRulesFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", Tribe_.Config.Econ).empty(), "and the economy section of the game, with its crops");
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		const ResourceCounts c = ScaledResources(n);
		BuildResources(Entities, glm::vec3(0.0f), Defs, c.Trees, c.Rocks, c.Bushes, c.Grass);
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		const std::vector<std::string> roles = Tribe_.PlanRoles(n);
		const glm::vec2 camp(0.0f, -8.0f);
		for (int i = 0; i < n; ++i)
		{
			const std::string name = MemberName(Tribe_.Config.Work, static_cast<size_t>(i));
			Entity& e = *Entities.Find(Entities.Create(name));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Stoke" };
			Names.push_back(name);
		}
		Gameplay.Load(Entities);
		for (int i = 0; i < n; ++i)
		{
			bodies.push_back(std::make_unique<FakeBody>(Names[static_cast<size_t>(i)], Gameplay, Entities, camp + glm::vec2(static_cast<float>(i % 5) * 0.7f, static_cast<float>(i / 5) * 0.7f)));
			bodies.back()->m_Items = &Items;
		}
		Resources.Bind(Gameplay, Defs, [this](const std::string& who, const std::string& item, int k)
		{
			for (size_t i = 0; i < Names.size(); ++i) if (Names[i] == who) bodies[i]->Bag().Add(Items, item, k);
		});
		Fires.Items = &Items;
		Fires.Bind(Gameplay, [this](const std::string& who, const std::string& item, int k)
		{
			for (size_t i = 0; i < Names.size(); ++i) if (Names[i] == who) return bodies[i]->Bag().Remove(item, k);
			return 0;
		}, [](const std::string&, const std::string&, int) { return 0; });
		Tribe_.Config.Economy.Camp = camp;
		Tribe_.Store().Add(Items, "wood", 2 + 2 * n);
		Tribe_.Store().Add(Items, "stone", 3 * n);
		for (int i = 0; i < n; ++i) Tribe_.AddMember(Names[static_cast<size_t>(i)], roles[static_cast<size_t>(i)], bodies[static_cast<size_t>(i)].get());
	}
	void Run(double seconds)
	{
		const int frames = static_cast<int>(seconds / dt);
		for (int i = 0; i < frames; ++i)
		{
			Gameplay.Update(dt);
			Resources.Update(Gameplay, dt);
			for (auto& b : bodies) b->Update(dt);
			Fires.Update(dt, &Entities);
			Tribe_.Update(dt);
			for (const auto& [id, job] : Tribe_.Board().Jobs())
			{
				Posted[job.Kind + " " + job.Target] = true;
				for (const JobClaim& cl : job.Claims) Claimed[job.Kind + " " + job.Target] = cl.Member;
			}
			for (const ItemSlot& s : Tribe_.Store().Stacks()) bad = bad || s.Count < 1;
		}
	}
	size_t Index(const std::string& role) const
	{
		for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Role == role) return i;
		return 0;
	}
	bool Logged(const std::string& part) const
	{
		return std::any_of(Tribe_.Log().begin(), Tribe_.Log().end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
	}
	long First(const std::string& part) const
	{
		const auto& log = Tribe_.Log();
		const auto it = std::find_if(log.begin(), log.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
		return it == log.end() ? -1 : static_cast<long>(it - log.begin());
	}
	void Dump(const std::string& filter = std::string()) const
	{
		for (const std::string& l : Tribe_.Log()) if (filter.empty() || l.find(filter) != std::string::npos) std::fprintf(stderr, "  %s\n", l.c_str());
	}
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Fires;
	Tribe Tribe_;
	std::vector<std::string> Names;
	std::vector<std::unique_ptr<FakeBody>> bodies;
	std::map<std::string, bool> Posted;
	std::map<std::string, std::string> Claimed;
	bool bad = false;
	void Clock(double seconds)
	{
		const int frames = static_cast<int>(seconds / dt);
		for (int i = 0; i < frames; ++i) { Now += dt; Tribe_.SetWorldClock(Now); Run(dt); }
	}
	int Plots(const std::string& field, const std::string& state) const
	{
		for (const Field& f : Tribe_.Economy().Fields) if (f.Name == field) return f.Count(state);
		return -1;
	}
	double Now = 0.0;
};
}

int GBTest_farm_zones(int, char**)
{
	// ---- the player paints a zone: it is tilled and sown with its crop; the player's clan marks out no field of its own
	{
		Clan k(4, true);   // (no technologies: everyone may farm)
		Tribe& t = k.Tribe_;
		t.Zones().SetPlayer(true);
		t.SetWorldClock(0.0);   // the first day of spring
		t.Store().Add(k.Items, "grain_seed", 30);
		t.Store().Add(k.Items, "stone_hoe", 1);
		const FarmZones::Reply r = t.Zones().Paint(t, { 10.0f, 0.0f }, { 16.0f, 4.0f }, "grain");
		std::fprintf(stderr, "  paint: %s\n", r.Text.c_str());
		Check(r.Ok && t.Economy().Fields.size() == 1 && t.Economy().Fields[0].Name == "zone 1" && t.Economy().Fields[0].Plots.size() == 12, "a zone of 4 x 3 plots is painted");
		Check(t.FindStockpile("zone 1 store") != nullptr, "with a store beside it");
		Check(!t.Zones().Paint(t, { 12.0f, 2.0f }, { 20.0f, 6.0f }, "grain").Ok, "a zone over another is refused");
		Check(!t.Zones().Paint(t, { 30.0f, 0.0f }, { 34.0f, 4.0f }, "nope").Ok, "a crop the data has not is refused");
		Check(!t.Zones().Paint(t, { -100.0f, 40.0f }, { 0.0f, 140.0f }, "").Ok, "a zone of more than 400 plots is refused");
		k.Clock(300.0);
		std::fprintf(stderr, "  %s\n", t.Zones().List(t).c_str());
		Check(t.Economy().Fields.size() == 1, "the player's clan marks out no field of its own");
		Check(k.Posted["farm till"] && k.Posted["farm sow"], "the zone's work goes on the board (till, sow)");
		const int growing = k.Plots("zone 1", "sown") + k.Plots("zone 1", "sprout") + k.Plots("zone 1", "grown");
		Check(growing >= 6, "the zone is tilled and sown");
		bool grainOnly = true;
		for (const FieldPlot& p : t.Economy().Fields[0].Plots) grainOnly = grainOnly && (p.Crop.empty() || p.Crop == "grain");
		Check(grainOnly && k.Logged("plots of zone 1 with grain"), "with its crop");

		// ---- crop, plan, auto
		Check(!t.Zones().SetCrop(t, "1", "nope").Ok && !t.Zones().SetCrop(t, "9", "grain").Ok && !t.Zones().SetPlan(t, "1", "dance").Ok, "a bad crop, zone or plan is refused");
		CropDef odd;
		odd.Id = "odd";
		odd.Seed = "flint";
		odd.Yield = { { "stone", 1 } };
		t.Config.Econ.Crops["odd"] = odd;
		t.Store().Add(k.Items, "flint", 5);
		t.Store().Add(k.Items, "grain", 30);
		const Field& zone1 = t.Economy().Fields[0];
		Check(t.Zones().CropFor(t, zone1) == "grain", "a zone with its crop sows it");
		Check(t.Zones().SetAuto(t, "1", true).Ok && t.Zones().Of("zone 1")->Auto, "auto on");
		std::fprintf(stderr, "  auto: %s (grain %d, stone %d)\n", t.Zones().CropFor(t, zone1).c_str(), t.Stored("grain"), t.Stored("stone"));
		Check(t.Zones().CropFor(t, zone1) == (t.Stored("stone") < t.Stored("grain") ? "odd" : "grain"), "in auto the clan sows the crop whose produce the store has least of");
		Check(t.Zones().SetCrop(t, "zone_1", "odd").Ok && !t.Zones().Of("zone 1")->Auto && t.Zones().CropFor(t, zone1) == "odd", "a crop given turns auto off");
		t.Zones().SetCrop(t, "1", "grain");
		t.Config.Econ.Crops.erase("odd");
		Check(t.Zones().SetPlan(t, "1", "harvest").Ok && t.Zones().CropFor(t, zone1).empty() && !t.Zones().Works(zone1, "till") && t.Zones().Works(zone1, "harvest"), "plan harvest: nothing new is sown");
		t.Zones().SetPlan(t, "1", "sow");
		const FarmZones::Reply z2 = t.Zones().Paint(t, { 10.0f, 12.0f }, { 14.0f, 14.0f }, "grain");
		Check(z2.Ok && t.Economy().Fields.size() == 2, "a second zone");
		Check(t.Zones().SetPlan(t, "2", "fallow").Ok, "plan fallow");
		k.Clock(120.0);
		Check(k.Plots("zone 2", "grass") == 6, "a fallow zone is not tilled");

		// ---- saving
		const std::string saved = t.ToJson();
		Check(saved.find("\"farmzones\"") != std::string::npos, "the zones are saved in their section");
		Clan l(4, true);
		Check(l.Tribe_.FromJson(saved, false), "the tribe loads");
		Check(l.Tribe_.Zones().All() == t.Zones().All() && l.Tribe_.Economy().Fields == t.Economy().Fields, "with its zones and their plots");
		Check(l.Tribe_.ToJson() == saved, "and saves the same again");
		FarmZones bad;
		Check(!bad.FromJson("{\"zones\":{\"zone 1\":{\"plan\":\"dance\"}}}") && bad.Empty(), "a bad plan in a save is refused");

		// ---- remove
		Check(t.Zones().Remove(t, "1").Ok && t.Economy().Fields.size() == 1 && t.Economy().Fields[0].Name == "zone 2" && !t.Zones().Of("zone 1"), "a zone is removed");
		Check(!t.Zones().Remove(t, "1").Ok, "twice is refused");
		const FarmZones::Reply z3 = t.Zones().Paint(t, { 24.0f, 0.0f }, { 26.0f, 0.0f }, "");
		Check(z3.Ok && t.Economy().Fields.back().Name == "zone 1" && t.Zones().Of("zone 1")->Auto, "its name is free again; with no crop the zone is in auto");
		const Stockpile* store = t.FindStockpile("zone 1 store");
		Check(store && glm::distance(store->Pos, glm::vec2(22.0f, 0.0f)) < 0.01f, "with a store of its own beside it (the old one went or was renamed)");
		k.Clock(30.0);   // (a farmer on the way to a removed zone comes to nothing)
	}

	// ---- a zone painted as a perimeter of points: the cells inside, saved with its outline
	{
		Clan k(4, true);
		Tribe& t = k.Tribe_;
		t.Zones().SetPlayer(true);
		t.SetWorldClock(0.0);
		t.Store().Add(k.Items, "grain_seed", 20);
		t.Store().Add(k.Items, "stone_hoe", 1);
		// a right triangle with its legs on x = 10 and z = 0, 8 m each: cells (i, j) with i + j <= 4 -> 15 plots
		const FarmZones::Reply r = t.Zones().PaintOutline(t, { { 10.0f, 0.0f }, { 18.0f, 0.0f }, { 10.0f, 8.0f } }, "grain");
		std::fprintf(stderr, "  poly: %s\n", r.Text.c_str());
		Check(r.Ok && t.Economy().Fields.size() == 1 && t.Economy().Fields[0].Plots.size() == 15 && t.Economy().Fields[0].Cells.size() == 15, "a triangle of 15 plots");
		const Field& f = t.Economy().Fields[0];
		bool inside = true;
		for (size_t i = 0; i < f.Plots.size(); ++i) { const glm::vec2 p = f.PlotPos(i) - glm::vec2(10.0f, 0.0f); inside = inside && p.x + p.y <= 8.01f && p.x >= 0.0f && p.y >= 0.0f; }
		Check(inside, "every plot inside the perimeter");
		Check(t.Zones().Of("zone 1")->Outline.size() == 3, "the perimeter is kept");
		Check(!t.Zones().PaintOutline(t, { { 10.0f, 0.0f }, { 12.0f, 0.0f } }, "grain").Ok, "two points are no zone");
		k.Clock(200.0);
		Check(k.Plots("zone 1", "sown") + k.Plots("zone 1", "sprout") + k.Plots("zone 1", "grown") > 0, "it is tilled and sown");
		const std::string saved = t.ToJson();
		Clan l(4, true);
		Check(l.Tribe_.FromJson(saved, false) && l.Tribe_.Economy().Fields == t.Economy().Fields && l.Tribe_.Zones().All() == t.Zones().All() && l.Tribe_.ToJson() == saved,
			"its cells and its perimeter save and load");
	}

	// ---- two crops in two zones: each zone is sown with its own (the first crop by id is not the one of every zone)
	{
		Clan k(4, true);
		Tribe& t = k.Tribe_;
		t.Zones().SetPlayer(true);
		t.SetWorldClock(0.0);
		CropDef aaa;
		aaa.Id = "aaa";   // (before grain by id)
		aaa.Seed = "tin_ore";
		aaa.Yield = { { "copper_ore", 2 } };
		t.Config.Econ.Crops["aaa"] = aaa;
		t.Store().Add(k.Items, "grain_seed", 20);
		t.Store().Add(k.Items, "tin_ore", 20);
		t.Store().Add(k.Items, "stone_hoe", 2);
		Check(t.Zones().Paint(t, { 10.0f, 0.0f }, { 14.0f, 2.0f }, "grain").Ok && t.Zones().Paint(t, { 10.0f, 10.0f }, { 14.0f, 12.0f }, "aaa").Ok, "a zone of grain and one of the other crop");
		k.Clock(400.0);
		std::fprintf(stderr, "  two crops: %s\n", t.Zones().List(t).c_str());
		auto only = [&](const std::string& zone, const std::string& crop)
		{
			int sown = 0;
			bool own = true;
			for (const Field& f : t.Economy().Fields)
				if (f.Name == zone)
					for (const FieldPlot& p : f.Plots) { if (!p.Crop.empty()) { ++sown; own = own && p.Crop == crop; } }
			return own && sown > 0;
		};
		Check(only("zone 1", "grain"), "zone 1 grows only grain");
		Check(only("zone 2", "aaa"), "zone 2 grows only its own crop");
	}

	// ---- the clans of the AI mark out their zones: by the data's rule, and by the old one without it
	{
		Clan a(6, true);
		Tribe& t = a.Tribe_;
		FarmRules& f = t.Config.Econ.Farming;
		f.PlotsPerMember = 5;
		f.ZonePlotsMin = 20;
		f.ZonePlotsMax = 40;
		f.Columns = 5;
		t.SetWorldClock(0.0);
		t.Store().Add(a.Items, "grain_seed", 40);
		t.Store().Add(a.Items, "stone_hoe", 1);
		Check(t.Zones().FieldsWanted(t) == 1, "6 members x 5 plots want one zone");
		a.Clock(10.0);
		std::fprintf(stderr, "  AI: %s\n", t.FieldReport().c_str());
		Check(t.Economy().Fields.size() == 1 && t.Economy().Fields[0].Plots.size() == 30 && t.Economy().Fields[0].Columns == 5, "the AI marks out a zone of 30 plots");
		Check(t.Zones().FieldsWanted(t) == 1 && FarmZones::NextPlots(t) == 20, "and wants no more");
		Check(t.Zones().Empty() && t.ToJson().find("farmzones") == std::string::npos, "the AI's fields save nothing of the player's");
		f.PlotsPerMember = 10;   // (60 plots wanted: 30 more, in a zone of up to 40)
		Check(t.Zones().FieldsWanted(t) == 2 && FarmZones::NextPlots(t) == 30, "a bigger need wants a second zone");
		a.Clock(10.0);
		Check(t.Economy().Fields.size() == 2 && t.Economy().Fields[1].Plots.size() == 30, "and marks it out");

		Clan b(6, true);
		b.Tribe_.SetWorldClock(0.0);
		b.Tribe_.Store().Add(b.Items, "grain_seed", 40);
		b.Tribe_.Config.Econ.Farming.PlotsPerMember = 0;   // the published data has the zone rule (A1); this checks the old one
		const FarmRules& old = b.Tribe_.Config.Econ.Farming;
		Check(old.PlotsPerMember == 0 && b.Tribe_.Zones().FieldsWanted(b.Tribe_) == old.FieldsFor(6), "without plotsPerMember the old rule");
		b.Clock(10.0);
		Check(!b.Tribe_.Economy().Fields.empty() && static_cast<int>(b.Tribe_.Economy().Fields[0].Plots.size()) == old.Plots, "fields of `plots` plots");
	}

	// ---- the data: a crop's pace by season, plotsPerMember and zonePlots read, save and read back
	{
		EconomyRules rules = DefaultEconomyRules();
		const std::string json = R"({ "economy": { "farming": { "plotsPerMember": 5, "zonePlots": [20, 40], "columns": 5 },
			"crops": [ { "id": "grain", "seed": "grain_seed", "yield": { "grain": 10, "grain_seed": 2 }, "stageSeconds": 4200, "seasons": { "spring": 1.2, "summer": 1.0, "autumn": 0.6 } } ] } })";
		Check(LoadEconomyRules(json, rules).empty(), "the farming rules and a crop with seasons read");
		Check(rules.Farming.PlotsPerMember == 5 && rules.Farming.ZoneMin() == 20 && rules.Farming.ZoneMax() == 40, "plotsPerMember and zonePlots");
		const CropDef& c = rules.Crops.at("grain");
		Check(c.Seasons.size() == 3 && std::fabs(c.Rate(Season::Autumn) - 0.6f) < 1e-5f && c.Rate(Season::Winter) == 0.0f, "the crop's pace (and the common curve where it says nothing)");
		EconomyRules back = DefaultEconomyRules();
		Check(LoadEconomyRules(std::string("{\"economy\":") + EconomyRulesToJson(rules) + "}", back).empty() && back.Farming == rules.Farming && back.Crops == rules.Crops, "and save and read back");
		EconomyRules wrong = DefaultEconomyRules();
		Check(!LoadEconomyRules(R"({ "economy": { "crops": [ { "id": "x", "seed": "grain_seed", "yield": { "grain": 1 }, "seasons": { "monsoon": 1 } } ] } })", wrong).empty(), "an unknown season is refused");
		Check(!LoadEconomyRules(R"({ "economy": { "farming": { "zonePlots": [40, 20] } } })", wrong).empty(), "zonePlots min over max is refused");
		CropDef fast = c;
		fast.Seasons = { { "spring", 2.4f } };
		std::map<std::string, CropDef> slow{ { "grain", c } }, quick{ { "grain", fast } };
		Field s("a", glm::vec2(0.0f), 1, 1, 2.0f), q("b", glm::vec2(0.0f), 1, 1, 2.0f);
		s.Plots[0] = q.Plots[0] = FieldPlot{ "sown", "grain", 0.0f, 1000.0f };
		s.Update(100.0f, Season::Spring, false, 1000.0f, slow);
		q.Update(100.0f, Season::Spring, false, 1000.0f, quick);
		Check(std::fabs(q.Plots[0].Growth - 2.0f * s.Plots[0].Growth) < 1e-5f && s.Plots[0].Growth > 0.0f, "a crop grows at its own pace");
	}

	if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
	else std::printf("farm zones: ok\n");
	return failures ? 1 : 0;
}
