// Clans and diplomacy (H13). Checked, on the CPU and on a world the generator makes without GL:
//  * the "diplomacy" section of assets/data/era1.json says what the code says, round-trips, and a mistake changes nothing (also inside a pack);
//  * a barter takes what one clan lacks from what the other spares, balanced by value, and the same stores give the same deal;
//  * the territory: claims grow with time and strength around each camp, are deterministic, and overlap when camps are close;
//  * two clans on a generated world meet, send envoys who trade the surplus of one for the need of the other, the same in two runs;
//  * a friendly clan's envoy learns a technology from the other's at a meeting, and the clan comes to know it;
//  * a member moves to the other clan by marriage (with its person and knowledge), and clans that like each other wed on their own;
//  * hostile clans may raid (the hook for H14) until they parley and agree a truce;
//  * a clan world with two clans saves and loads the same, and a save of a single tribe (before H13) loads as clan 0.
// Stand-in bodies as in exploration_test (they walk in straight lines at 3 m/s).
#include "GB/Game/Clans.h"
#include "GB/Game/GameContent.h"
#include "GB/Game/SimQA.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/World/Regions.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <sstream>
#include <nlohmann/json.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
		else std::printf("ok: %s\n", what);
	}
	constexpr float dt = 1.0f / 20.0f;
}

using namespace GB;

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
				const glm::vec4 p = id == NullEntity ? glm::vec4(m_Pos.x, 0.0f, m_Pos.y, 1.0f) : m_Entities.WorldMatrix(id)[3];
				s.Seconds = glm::distance(m_Pos, glm::vec2(p.x, p.z)) / 3.0f;
				m_Target = { p.x, p.z };
				s.Walking = true;
			}
		}
		if (s.Seconds > 0.0f)
		{
			const float total = s.Seconds;
			s.Seconds -= step;
			if (s.Kind != "wait" && total > 0.0f) m_Pos += (m_Target - m_Pos) * std::min(1.0f, step / total);
			if (s.Seconds > 0.0f) return;
			if (s.Kind != "wait") m_Pos = m_Target;
		}
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

// A generated world (CPU only), its wild nodes and its regions.
struct Land
{
	Land(WorldType type, uint32_t seed)
	{
		WorldDesc desc;
		desc.Type = type;
		desc.Seed = seed;
		World = WorldGenerator::GenerateData(desc);
		ItemLibrary items;
		RecipeBook recipes;
		RegisterEraOneItems(items, recipes, Defs);
		Spots = PlanWorldResources(ResourceGroundOf(World), Defs, seed, 1.0f);
		Regions = RegionMap::Build(World, DefaultExplorationRules().Regions, RegionThingsOf(Spots));
	}
	float Height(float x, float z) const { return World.TerrainRef ? World.TerrainRef->GetHeight(x, z) : 0.0f; }
	GeneratedWorld World;
	std::map<std::string, ResourceNodeDef> Defs;
	std::vector<ResourceSpot> Spots;
	RegionMap Regions;
};

// What a clan starts with.
struct ClanSpec
{
	std::string Name;
	glm::vec2 Camp{ 0.0f };
	int Members = 4;
	std::map<std::string, int> Store;   // on top of the usual first day's wood, stone and berries
	int Berries = -1;                   // berries in the store at the start (-1: the usual 5 a member)
	int BerryWant = -1;                 // the clan's own target of berries in the store (-1: the economy's); a clan that wants more than it has lacks them whatever it forages
};

// Several clans in one world: one scene, one set of abilities, the wild nodes of the land and a well by each camp.
struct Valley
{
	Valley(const Land& land, const std::vector<ClanSpec>& specs, const DiplomacyRules& rules, bool techs = false)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		Techs = DefaultTechs();
		BuildWorldResources(Entities, land.Spots, Defs);
		Clans.Rules = rules;
		Clans.SetRegions(&land.Regions);
		const WorkRules work = DefaultWorkRules();
		size_t index = 0;
		std::vector<std::vector<std::string>> names(specs.size());
		for (size_t c = 0; c < specs.size(); ++c)
		{
			const glm::vec2 well = specs[c].Camp + glm::vec2(4.0f, -6.0f);
			BuildInteractive(Entities, DefaultInteractives().at("well"), "Well " + specs[c].Name, NullEntity, { well.x, land.Height(well.x, well.y), well.y });
			for (int i = 0; i < specs[c].Members; ++i)
			{
				const std::string name = MemberName(work, index++);   // one list of names for the whole world: an actor's name is unique
				Entity& e = *Entities.Find(Entities.Create(name));
				e.Gameplay.emplace();
				e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Stoke", "DrawWater" };
				names[c].push_back(name);
			}
		}
		Gameplay.Load(Entities);
		auto body = [this](const std::string& who) -> FakeBody*
		{
			const auto it = ByName.find(who);
			return it == ByName.end() ? nullptr : it->second;
		};
		Resources.Bind(Gameplay, Defs, [body](const std::string& who, const std::string& item, int k) { if (FakeBody* b = body(who)) b->Bag().Add(*b->m_Items, item, k); });
		Fires.Items = &Items;
		Fires.Bind(Gameplay, [body](const std::string& who, const std::string& item, int k) { FakeBody* b = body(who); return b ? b->Bag().Remove(item, k) : 0; },
			[body](const std::string& who, const std::string& item, int k) { FakeBody* b = body(who); return b ? b->Bag().Add(*b->m_Items, item, k) : 0; });
		const TribeConfig config = Tribe().Config;   // the defaults of a tribe (its roles, agenda, economy...), as the editor's content gives them
		for (size_t c = 0; c < specs.size(); ++c)
		{
			const ClanSpec& s = specs[c];
			Clan& clan = Clans.Add(s.Name, nullptr, -1, &config);
			Tribe& t = *clan.Folk;
			t.Bind(Entities, Gameplay, Items, Recipes, Defs);
			if (techs) t.SetTechs(&Techs);
			t.Config.Work.Jobs.SurvivalDays = 0.0f;   // (these tests are about diplomacy: a clan short of food would go gathering in its neighbour's land, and the pyramid has its own test)
			t.Config.Work.Jobs.CriticalHunger = 0.0f;
			if (s.BerryWant >= 0) t.Config.Economy.Stock["berry"] = { static_cast<float>(s.BerryWant), 0.0f };
			t.Config.Camp = s.Camp;
			t.Config.CampY = land.Height(s.Camp.x, s.Camp.y);
			t.SetRegions(&land.Regions, [&land](float x, float z) { return land.Height(x, z); });
			const int n = s.Members;
			t.Store().Add(Items, "wood", 2 + 2 * n);
			t.Store().Add(Items, "stone", 3 * n);
			t.Store().Add(Items, "berry", s.Berries >= 0 ? s.Berries : 5 * n);
			for (const auto& [item, k] : s.Store) t.Store().Add(Items, item, k);
			const std::vector<std::string> roles = t.PlanRoles(n);
			for (int i = 0; i < n; ++i)
			{
				const std::string& name = names[c][static_cast<size_t>(i)];
				Bodies.push_back(std::make_unique<FakeBody>(name, Gameplay, Entities, s.Camp + glm::vec2(static_cast<float>(i % 5) * 0.7f, static_cast<float>(i / 5) * 0.7f)));
				Bodies.back()->m_Items = &Items;
				ByName[name] = Bodies.back().get();
				t.AddMember(name, roles[static_cast<size_t>(i)], Bodies.back().get());
			}
		}
	}
	bool Run(double seconds, const std::function<bool()>& stop = nullptr)
	{
		const int frames = static_cast<int>(seconds / static_cast<double>(dt));
		for (int i = 0; i < frames; ++i)
		{
			Gameplay.Update(dt);
			Resources.Update(Gameplay, dt);
			for (auto& b : Bodies) b->Update(dt);
			Resources.SyncVisuals(Gameplay, Entities);
			Fires.Update(dt, &Entities);
			Clans.Update(dt);
			if (stop && i % 20 == 0 && stop()) return true;
		}
		return false;
	}
	Tribe& T(size_t i) { return *Clans.All()[i]->Folk; }
	// The invariants of the clans as the simulation QA (H20) runs them: what its "clans" and "territory" checks report now.
	std::vector<SimFinding> Findings()
	{
		SimView view;
		view.Clan = &T(0);
		view.Clans = &Clans;
		view.Gameplay = &Gameplay;
		view.Entities = &Entities;
		SimTrack track;
		SimReport report;
		for (const SimInvariant& inv : SimInvariants())
			if (inv.Name == "clans" || inv.Name == "territory") { report.Begin(inv.Name); inv.Check(view, track, report); }
		for (const SimFinding& f : report.Items) std::printf("  invariant %s: %s %s\n", f.Check.c_str(), f.Subject.c_str(), f.What.c_str());
		return report.Items;
	}
	ClanId Id(size_t i) const { return Clans.All()[i]->Id; }
	const MeetingRecord* Done(const std::string& purpose, bool met = true) const
	{
		for (const MeetingRecord& r : Clans.History()) if ((purpose.empty() || r.Purpose == purpose) && r.Met == met) return &r;
		return nullptr;
	}
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	TechTree Techs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Fires;
	ClanWorld Clans;
	std::vector<std::unique_ptr<FakeBody>> Bodies;
	std::map<std::string, FakeBody*> ByName;
};

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);   // (every line, even if a check crashes)
	using Clock = std::chrono::steady_clock;
	const auto t0 = Clock::now();
	const DiplomacyRules defaults = DefaultDiplomacyRules();
	char text[256];

	// ---- the data
	{
		DiplomacyRules file;
		if (LoadDiplomacyRulesFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", file).empty() && !(file == defaults))
			std::printf("era1.json: %s\ncode:      %s\n", DiplomacyRulesToJson(file).c_str(), DiplomacyRulesToJson(defaults).c_str());
		Check(LoadDiplomacyRulesFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", file).empty() && file == defaults, "the diplomacy section of era1.json says what DefaultDiplomacyRules says");
		DiplomacyRules back;
		Check(LoadDiplomacyRules("{ \"diplomacy\": " + DiplomacyRulesToJson(defaults) + " }", back).empty() && back == defaults, "the diplomacy section round-trips");
		DiplomacyRules keep = defaults;
		Check(!LoadDiplomacyRules(R"({ "diplomacy": { "trade": { "every": 5 }, "encounters": { "radius": -1 } } })", keep).empty() && keep == defaults,
			"a negative radius is refused and nothing changes (not even the trade before it)");
		Check(!LoadDiplomacyRules(R"({ "diplomacy": { "wars": {} } })", keep).empty() && !LoadDiplomacyRules(R"({ "diplomacy": { "pacts": { "forever": 1 } } })", keep).empty(),
			"an unknown part or key is refused");
		Check(!LoadDiplomacyRules(R"({ "diplomacy": { "relations": { "hostile": 0.5, "friendly": 0.2 } } })", keep).empty() && keep == defaults, "hostile above friendly is refused");
		DiplomacyRules some = defaults;
		Check(LoadDiplomacyRules(R"({ "diplomacy": { "relations": { "friendly": 0.3 } } })", some).empty() && some.Relations.Friendly == 0.3f
			&& some.Relations.Events == defaults.Relations.Events && some.Trade == defaults.Trade, "a part replaces that part (its maps stay the default when it does not give them)");
		Check(LoadDiplomacyRules(R"({ "items": [] })", some).empty(), "a file without the section changes nothing");

		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes; std::map<std::string, TribeRole> roles = DefaultRoles();
		DiplomacyRules live;
		live.Trade.Every = 1.0f;
		RegisterEraOneItems(items, recipes, nodes);
		ContentPacks packs;
		AddGameContentSections(packs, items, recipes, nodes, roles);
		AddDiplomacyContentSection(packs, live);
		Check(packs.Load("era1", std::string(GB_ASSETS_DIR) + "/data").empty() && live == defaults, "the era1 pack brings the diplomacy rules through their section");
		Check(!packs.LoadText(R"({ "diplomacy": { "trade": { "every": 5 } }, "roles": { "bad": { "tool": "x" } } })").empty() && live == defaults,
			"and a bad role in the same document leaves them as they were");
	}

	// ---- barter
	{
		StoreView a, b;
		a.Have = { { "berry", 40 }, { "stone", 10 } };
		b.Have = { { "flint", 30 }, { "berry", 0 } };
		for (const auto& [item, value] : defaults.Trade.Values) { a.Want[item] = 12.0f; b.Want[item] = 12.0f; }
		const Barter deal = PlanBarter(defaults.Trade, a, b, false);
		std::snprintf(text, sizeof text, "a barter: the clan rich in berries gives berries for the flint it lacks (%d berry for %d flint)",
			deal.Give[0].count("berry") ? deal.Give[0].at("berry") : 0, deal.Give[1].count("flint") ? deal.Give[1].at("flint") : 0);
		Check(deal.Give[0].count("berry") && deal.Give[1].count("flint") && !deal.Give[0].count("stone"), text);
		Check(deal.Value[0] <= deal.Value[1] * defaults.Trade.Balance && deal.Value[1] <= deal.Value[0] * defaults.Trade.Balance, "it is balanced by value (flint is worth two berries)");
		const Barter again = PlanBarter(defaults.Trade, a, b, false);
		Check(again.Give[0] == deal.Give[0] && again.Give[1] == deal.Give[1], "the same stores give the same deal");
		StoreView poor = b;
		poor.Have = { { "berry", 0 } };
		Check(PlanBarter(defaults.Trade, a, poor, false).Empty() && !PlanBarter(defaults.Trade, a, poor, true).Give[0].empty(),
			"with nothing to give back there is no barter, but a fond clan gives what the other lacks");
	}

	const Land land(WorldType::Temperate, 5);
	const glm::vec2 spawn = land.World.Spawn;

	// ---- territory
	{
		ClanFootprint near{ 0, spawn, 6, 0.0f }, far{ 1, spawn + glm::vec2(120.0f, 0.0f), 4, 0.0f };
		TerritoryMap one, two;
		for (int k = 0; k < 40; ++k)
		{
			one.Step(land.Regions, { near, far }, defaults.Territory, 120.0f, 1200.0f);
			two.Step(land.Regions, { near, far }, defaults.Territory, 120.0f, 1200.0f);
		}
		Check(one == two && !one.Claims().empty(), "the claims are deterministic");
		const int home = land.Regions.RegionAt(spawn);
		Check(one.Owner(home, defaults.Territory) == 0, "a clan holds the region of its camp");
		Check(one.Claim(0, home) > 0.9f * TerritoryMap::Reachable(*land.Regions.Find(home), near, home, defaults.Territory), "and in four days its claim there is nearly all it can hold");
		TerritoryMap young;
		young.Step(land.Regions, { near }, defaults.Territory, 120.0f, 1200.0f);
		Check(young.Claim(0, home) < one.Claim(0, home), "a claim grows with time");
		TerritoryMap big;
		ClanFootprint built = near;
		built.Buildings = 5.0f;
		for (int k = 0; k < 40; ++k) big.Step(land.Regions, { built }, defaults.Territory, 120.0f, 1200.0f);
		Check(big.Claim(0, home) > one.Claim(0, home) && big.Held(0, defaults.Territory).size() >= one.Held(0, defaults.Territory).size(), "buildings make a claim stronger");
		Check(!one.Overlaps(defaults.Territory).empty(), "two clans 120 m apart claim some of the same land");
		TerritoryMap back;
		Check(back.FromJson(one.ToJson()) && back == one, "the claims round-trip");
		std::printf("(territory: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());
	}

	// ---- two clans meet and trade the surplus of one for the need of the other
	const ClanSpec red{ "Red", spawn, 4, { { "berry", 40 } } };
	const ClanSpec blue{ "Blue", spawn + glm::vec2(40.0f, 10.0f), 4, { { "flint", 30 } }, 0, 40 };   // (Blue wants 40 berries and starts with none: it forages some in its first minute, which used to be all it needed to turn the barter down; with the pause on a failed job members go foraging earlier)
	std::string savedMid, finalOne;
	double savedAt = -1.0;
	{
		Valley v(land, { red, blue }, defaults);
		const ClanId a = v.Id(0), b = v.Id(1);
		const bool met = v.Run(1800.0, [&] { return v.Clans.Met(a, b); });
		std::snprintf(text, sizeof text, "two clans 41 m apart meet (in %.0f s)", v.Clans.Time());
		Check(met, text);
		const bool traded = v.Run(3600.0, [&]
		{
			if (savedMid.empty() && !v.Clans.Meetings().empty() && v.Clans.Meetings().front().Leg[0] >= 1) { savedMid = v.Clans.ToJson(); savedAt = v.Clans.Time(); }
			return v.Done("trade") != nullptr;
		});
		std::printf("%s%s", v.Clans.RelationsReport().c_str(), v.Clans.MeetingsReport().c_str());
		Check(traded, "their envoys meet and trade");
		if (const MeetingRecord* r = v.Done("trade"))
		{
			std::snprintf(text, sizeof text, "the rich in berries gives berries for flint (%s)", r->Outcome.c_str());
			Check(r->Outcome.find("berry") != std::string::npos && r->Outcome.find("flint") != std::string::npos, text);
		}
		Check(v.T(1).Store().Count("berry") > 0 || v.Clans.Relation(a, b)->Trades >= 1, "the clan that lacked berries got some");
		Check(v.Clans.Relation(a, b)->Trades >= 1 && v.Clans.Attitude(a, b) > 0.0f, "the trade brings the clans closer");
		Check(!v.Clans.Territory().Claims().empty() && v.Findings().empty(), "the invariants of the QA hold: each member in one clan, the claims deterministic");
		finalOne = v.Clans.ToJson();
	}
	{
		Valley v(land, { red, blue }, defaults);
		v.Run(1800.0, [&] { return v.Clans.Met(v.Id(0), v.Id(1)); });
		v.Run(3600.0, [&] { return v.Done("trade") != nullptr; });
		Check(v.Clans.ToJson() == finalOne, "two runs end the same");
	}
	std::printf("(trade: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	// ---- save and load with two clans (in the middle of a meeting)
	Check(!savedMid.empty(), "the clan world was saved with envoys on their way");
	if (!savedMid.empty())
	{
		const auto saved = ClanWorld::SavedClans(savedMid);
		Check(saved.size() == 2 && saved[0].Name == "Red" && saved[1].Members.size() == 4, "the save names its clans and their members");
		Valley v(land, { red, blue }, defaults);
		Check(v.Clans.FromJson(savedMid), "the save loads");
		Check(v.Clans.ToJson() == savedMid, "and saves again the same");
		Check(v.Clans.Meetings().size() == 1 && std::fabs(v.Clans.Time() - savedAt) < 1e-6, "with its meeting and its clock");
		const bool done = v.Run(3600.0, [&] { return v.Done("") != nullptr || v.Done("", false) != nullptr; });
		Check(done, "and the loaded clans finish the meeting");
	}

	// ---- a single tribe of before H13 loads as clan 0
	{
		Valley one(land, { red }, defaults);
		one.Run(60.0);
		const std::string ai = one.T(0).ToJson();   // what the "ai" section held before there were clans
		Valley loaded(land, { red }, defaults);
		Check(loaded.T(0).FromJson(ai, false) && loaded.Clans.Count() == 1 && loaded.T(0).Members().size() == 4, "a save of one tribe loads as the first clan");
		Check(!loaded.Clans.FromJson("{}") && loaded.Clans.Count() == 1, "with no clans section the clan world stays as it was");
		Tribe alone;   // a host tribe that the clan world does not keep
		ClanWorld world;
		world.Add("", &alone);
		Check(world.Count() == 1 && world.Find(0) && world.Find(0)->Name == "Clan 1" && !world.Find(0)->Kept(), "the host's tribe is clan 0");
		Check(world.FromJson(R"({ "clans": [ { "id": 0, "name": "Ours", "kept": false } ] })") && world.Find(0)->Name == "Ours", "and a clans section without relations loads");
	}
	std::printf("(save: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	// ---- a technology spreads to a friendly clan
	{
		DiplomacyRules rules = defaults;
		rules.Exchange.Teach = 1.0f;
		Valley v(land, { red, blue }, rules, true);
		const ClanId a = v.Id(0), b = v.Id(1);
		Check(v.T(0).Knowledge().GrantClan("basketry").empty() && !v.T(1).Knowledge().Clan().count("basketry"), "the Red know basketry and the Blue do not");
		v.Clans.Relate(a, b, "gift", 3.0f, "a test makes them friends");
		v.Run(1800.0, [&] { return v.Clans.Met(a, b); });
		Check(v.Clans.Stance(a, b) == "friendly" || v.Clans.Stance(a, b) == "allied", "they are friendly");
		const std::string why = v.Clans.ProposeVisit(a, b);
		Check(why.empty() || !v.Clans.Meetings().empty(), "a visit is posted");
		std::string learner;
		const bool learnt = v.Run(3600.0, [&]
		{
			for (const TribeMember& m : v.T(1).Members()) if (v.T(1).Knowledge().Knows(m.Name, "basketry")) { learner = m.Name; return true; }
			return false;
		});
		std::printf("%s", v.Clans.MeetingsReport().c_str());
		std::snprintf(text, sizeof text, "the Blue envoy learns basketry from the Red one (%s)", learner.c_str());
		Check(learnt, text);
		const bool clan = v.Run(1800.0, [&] { return v.T(1).Knowledge().Clan().count("basketry") > 0; });
		Check(clan, "and shows it to its clan at the camp");
	}
	std::printf("(knowledge: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	// ---- marriage: a member moves to the other clan
	{
		Valley v(land, { red, blue }, defaults, true);
		const ClanId a = v.Id(0), b = v.Id(1);
		const std::string she = v.T(0).Members()[0].Name, he = v.T(1).Members()[1].Name;
		const Person* p = v.T(0).People().Find(she);
		const Person* q = v.T(1).People().Find(he);
		Check(p && q && p->Female != q->Female, "two founders of different clans and sexes");
		const float age = q->Age;
		v.T(1).Knowledge().Grant(he, "basketry");
		Check(v.Clans.Wed(she, he).empty(), "they wed");
		const Clan* now = v.Clans.ClanOf(he);
		Check(now && now->Id == a && v.T(0).Members().size() == 5 && v.T(1).Members().size() == 3, "he moves to her clan (equal clans: the one of the higher id moves)");
		const Person* moved = v.T(0).People().Find(he);
		Check(moved && moved->Partner == she && v.T(0).People().Find(she)->Partner == he && std::fabs(moved->Age - age) < 1e-4f && !v.T(1).People().Find(he),
			"with his age and person, as her partner");
		Check(v.T(0).Knowledge().Knows(he, "basketry") && v.Clans.Relation(a, b)->Marriages == 1, "and what he knows");
		v.Run(120.0);
		Check(v.T(0).Members().back().Name == he && !v.T(0).Members().back().Dead, "and lives on in his new clan");
	}
	{
		DiplomacyRules eager = defaults;
		eager.Marriage.Every = 30.0f;
		eager.Marriage.Chance = 1.0f;
		eager.Marriage.MinBond = 0.0f;
		eager.Marriage.MinAttitude = -1.0f;
		Valley v(land, { red, blue }, eager);
		const bool wed = v.Run(3600.0, [&] { const ClanRelation* r = v.Clans.Relation(v.Id(0), v.Id(1)); return r && r->Marriages > 0; });
		Check(wed && v.T(0).Members().size() + v.T(1).Members().size() == 8 && v.T(0).Members().size() != v.T(1).Members().size(), "clans that met wed on their own and a member moves");
		Check(v.Findings().empty(), "and after the move every member is still in exactly one clan");
	}
	std::printf("(marriage: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	// ---- hostility (the hooks for H14) and a parley
	{
		Valley v(land, { red, blue }, defaults);
		const ClanId a = v.Id(0), b = v.Id(1);
		std::vector<std::string> events;
		v.Clans.Listen([&](const DiplomacyEvent& e) { events.push_back(e.Kind); });
		v.Run(1800.0, [&] { return v.Clans.Met(a, b); });
		Check(!v.Clans.MayRaid(a, b), "clans that just met do not raid each other");
		v.Clans.Relate(a, b, "raid", 1.0f, "a test raid of the Blue on the Red", a);
		Check(v.Clans.Stance(a, b) == "hostile" && v.Clans.MayRaid(a, b) && v.Clans.Hostility(a, b) > v.Clans.Hostility(b, a) && v.Clans.Grievance(a, b) >= 1.0f,
			"after a raid they are hostile, may raid, and the victim holds the grievance");
		Check(std::find(events.begin(), events.end(), "raid") != events.end() && std::find(events.begin(), events.end(), "contact") != events.end(), "the listener hears of the contact and the raid");
		// a lesser quarrel between open clans ends in a parley
		Valley w(land, { red, blue }, defaults);
		w.Run(1800.0, [&] { return w.Clans.Met(a, b); });
		w.Clans.Relate(a, b, "raid", 1.5f, "a quarrel without a victim");
		Check(w.Clans.Stance(a, b) == "hostile", "a quarrel makes them hostile");
		const bool truce = w.Run(3600.0, [&] { return w.Clans.HasPact(a, b, "truce"); });
		std::printf("%s", w.Clans.RelationsReport().c_str());
		Check(truce && !w.Clans.MayRaid(a, b) && w.Clans.Hostility(a, b) == 0.0f, "they parley and agree a truce: no raids");
	}
	std::printf("(hostility: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("clans: all checks passed\n");
	return failures ? 1 : 0;
}
