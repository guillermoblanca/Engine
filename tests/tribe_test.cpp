// The first tribe (H2 + H3): four workers with no one steering them gather, carry to the store, make their tools and light a campfire, with
// stand-in bodies that run the same task text a real actor runs. Checked: it reaches the goals, no count goes negative, nothing is claimed twice,
// and two runs end identically. Also the data of the tribe (roles with their way of walking and their agenda, the agenda and the stock of the
// "tribe" section) and the abstract agent on a navigation grid: it goes round a lake and never stands in it (H5). CPU only.
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Navigation/NavGrid.h"
#include "GB/Telemetry/TelemetrySink.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <cstdio>
#include <sqlite3.h>
#include <sstream>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	constexpr float dt = 1.0f / 60.0f;
}

using namespace GB;

// Runs the text of a task list against an ability system: goto and wait take time, "ability A node" activates it and waits until it ends.
class FakeBody : public ITribeBody
{
public:
	FakeBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
	bool Busy() const override { return !m_Steps.empty(); }
	bool Failed() const override { return m_Failed; }
	glm::vec2 Position() const override { return m_Pos; }
	Inventory& Bag() override { return m_Bag; }
	// What the tribe hands over before sending it: the profile of its role with the load of its bag (kept to be checked).
	void SetNavigation(const NavProfile& p) override { Profile = p; MaxLoad = std::max(MaxLoad, p.Load); ++Profiles; }
	bool Route(const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points) const override { ++Routes; return ITribeBody::Route(from, to, reach, points); }
	void SetSuspended(bool on) override { m_Suspended = on; }
	void Teleport(const glm::vec2& p) override { m_Pos = p; m_Steps.clear(); m_Started = false; }
	NavProfile Profile;
	float MaxLoad = 0.0f;
	int Profiles = 0;
	mutable int Routes = 0;
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
	void Update(float dt)
	{
		if (m_Suspended) return;   // the tribe moves it as an abstract agent
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
		if (s.Seconds > 0.0f) { s.Seconds -= dt; if (s.Seconds > 0.0f) return; m_Pos = m_Target; }
		if (s.Kind == "ability" && s.Walking)
		{
			s.Walking = false;
			std::string why;
			if (!me->Activate(s.Ability, m_Gameplay.Find(s.Node), &why, s.Node)) { static int shown = 0; if (shown++ < 3) std::fprintf(stderr, "  [fake] %s %s: %s\n", s.Ability.c_str(), s.Node.c_str(), why.c_str()); m_Failed = true; m_Steps.clear(); return; }
			s.Activated = true;
			return;
		}
		if (s.Kind == "ability" && me->AnyActive()) return;
		m_Steps.erase(m_Steps.begin());
		m_Started = false;
	}
	const ItemLibrary* m_Items = nullptr;
private:
	struct Step { std::string Kind, Ability, Node; float X = 0, Z = 0, Seconds = 0; bool Walking = false, Activated = false; };
	std::string m_Name;
	GameplayWorld& m_Gameplay;
	EntityWorld& m_Entities;
	glm::vec2 m_Pos, m_Target{ 0.0f };
	Inventory m_Bag{ 12, 0.0f };
	std::vector<Step> m_Steps;
	bool m_Started = false, m_Failed = false, m_Suspended = false;
};

// A world with a lake between two shores (deep in the middle, a wadeable rim) and nothing else: the abstract agent must go round it.
static CharacterWorld LakeWorld()
{
	CharacterWorld w;
	w.GroundHeight = [](float x, float z) { const float r = glm::length(glm::vec2(x, z)); return r < 12.0f ? -3.0f * (1.0f - (r / 12.0f) * (r / 12.0f)) : 0.0f; };
	w.WaterLevel = [](float x, float z) { return glm::length(glm::vec2(x, z)) < 12.0f ? 0.0f : -1e9f; };
	w.BoundsMin = { -30.0f, -30.0f };
	w.BoundsMax = { 30.0f, 30.0f };
	w.Obstacles.push_back({ { 0.0f, 18.0f }, 1.0f });   // a rock on the north shore
	return w;
}

// The H5 checks of the abstract agent: on a route from the grid it never stands in deep water, it goes round, and a goal it cannot reach fails.
static void AbstractOnTheGrid()
{
	const CharacterWorld world = LakeWorld();
	NavGrid grid;
	Check(grid.Build(world), "the lake world has a grid");
	const NavProfile walker;
	AbstractAgent::Route route = [&](const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points)
	{
		NavPath path;
		const bool found = grid.FindPath(walker, from, to, path);
		if (path.Points.empty() || (!found && glm::distance(path.Points.back(), to) > reach + grid.CellSize())) return false;
		points = path.Points;
		return true;
	};
	GameplayWorld gameplay;
	auto locate = [](const std::string&, glm::vec2&) { return false; };
	auto deepest = [&](const glm::vec2& p) { int i, j; return grid.CellAt(p, i, j) ? grid.Depth(i, j) : 0.0f; };

	AbstractAgent straight;   // with no route: the old straight line, through the lake
	straight.Place({ -20.0f, 0.0f });
	std::string error;
	Check(straight.Do("goto 20 0", error), "the agent takes a goto");
	float wettest = 0.0f;
	for (int i = 0; i < 2000 && straight.Busy(); ++i) { straight.Update(dt, nullptr, gameplay, locate, 3.0f); wettest = std::max(wettest, deepest(straight.Position())); }
	Check(!straight.Busy() && wettest > 2.0f, "without a route it walks straight through the deep water (what it did before)");

	AbstractAgent agent;
	agent.Place({ -20.0f, 0.0f });
	Check(agent.Do("goto 20 0 | wait 1", error), "the routed agent takes the same orders");
	wettest = 0.0f;
	float walked = 0.0f;
	glm::vec2 last = agent.Position();
	bool walls = false;
	for (int i = 0; i < 4000 && agent.Busy(); ++i)
	{
		agent.Update(dt, nullptr, gameplay, locate, 3.0f, route);
		wettest = std::max(wettest, deepest(agent.Position()));
		walls = walls || !grid.Walkable(agent.Position(), walker);
		walked += glm::distance(last, agent.Position());
		last = agent.Position();
	}
	Check(!agent.Busy() && !agent.Failed() && glm::distance(agent.Position(), glm::vec2(20.0f, 0.0f)) < 0.01f, "with the grid's route it gets there");
	Check(wettest <= walker.MaxWadeDepth && !walls, "and it never stands in water too deep to wade or inside a wall");
	Check(walked > 42.0f, "because it went round the lake");
	Check(grid.Walkable(agent.SafePosition(), walker), "the place it hands back to the body is walkable");

	AbstractAgent drowned;
	drowned.Place({ -20.0f, 0.0f });
	Check(drowned.Do("goto 0 0", error), "a goto into the middle of the lake is taken");
	for (int i = 0; i < 100 && drowned.Busy(); ++i) drowned.Update(dt, nullptr, gameplay, locate, 3.0f, route);
	Check(drowned.Failed() && drowned.Error().find("no way") != std::string::npos && drowned.Position() == glm::vec2(-20.0f, 0.0f),
		"but a goal it cannot reach fails at once, as the full actor's would, and it stays on the shore");
}

struct Village
{
	Village()
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);   // the campfire is the fire pit interactive, fed with Stoke
		BuildResources(Entities, glm::vec3(0.0f), Defs);
		for (const char* n : { "Ana", "Biel", "Cleo", "Dan" })
		{
			Entity& e = *Entities.Find(Entities.Create(n));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Stoke" };
		}
		Gameplay.Load(Entities);
		const glm::vec2 camp(0.0f, -8.0f);
		const struct { const char* name; const char* role; } crew[] = { { "Ana", "forager" }, { "Biel", "woodcutter" }, { "Cleo", "miner" }, { "Dan", "forager" } };
		for (const auto& c : crew)
		{
			bodies.push_back(std::make_unique<FakeBody>(c.name, Gameplay, Entities, camp));
			bodies.back()->m_Items = &Items;
		}
		Resources.Bind(Gameplay, Defs, [this](const std::string& who, const std::string& item, int n)
		{
			for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == who) bodies[i]->Bag().Add(Items, item, n);
		});
		Fires.Items = &Items;
		Fires.Bind(Gameplay, [this](const std::string& who, const std::string& item, int n)
		{
			for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == who) return bodies[i]->Bag().Remove(item, n);
			return 0;
		}, [](const std::string&, const std::string&, int) { return 0; });
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		Tribe_.Config.Camp = camp;
		Tribe_.Store().Add(Items, "wood", 4);
		Tribe_.Store().Add(Items, "stone", 5);
		for (size_t i = 0; i < 4; ++i) Tribe_.AddMember(crew[i].name, crew[i].role, bodies[i].get());
	}
	void Run(double seconds)
	{
		const int frames = static_cast<int>(seconds / dt);
		for (int i = 0; i < frames; ++i)
		{
			Gameplay.Update(dt);
			Resources.Update(Gameplay, dt);
			for (auto& b : bodies) b->Update(dt);
			Resources.SyncVisuals(Gameplay, Entities);
			Fires.Update(dt, &Entities);
			Tribe_.Update(dt);
			Invariants();
		}
	}
	void Invariants()
	{
		for (const std::string& n : Gameplay.Names())
		{
			const AbilitySystemComponent* a = Gameplay.Find(n);
			if (a->HasTag("Resource.Node") && ResourceSystem::Amount(*a) < 0) bad = true;
		}
		for (const ItemSlot& s : Tribe_.Store().Stacks()) if (s.Count <= 0) bad = true;
		std::map<std::string, int> goingTo;   // nobody goes to a node another one is already walking to
		for (const TribeMember& m : Tribe_.Members()) if (m.Job == "gather" && ++goingTo[m.Detail] > 1) bad = true;
		for (auto& b : bodies) for (const ItemSlot& s : b->Bag().Stacks()) if (s.Count <= 0) bad = true;
	}
	std::string Fingerprint() const
	{
		std::string f = Tribe_.Store().ToJson() + (Tribe_.HasFire() ? "F" + std::to_string(Tribe_.FireFuel()) : "-");
		for (const auto& b : bodies) f += b->Bag().ToJson();
		for (const std::string& l : Tribe_.Log()) f += l;
		return f;
	}
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Fires;
	Tribe Tribe_;
	std::vector<std::unique_ptr<FakeBody>> bodies;
	bool bad = false;
};

int main()
{
	// Roles are data (assets/data/era1.json, "roles"): the file says what the code says, and a node the code never named is weighed by its kind.
	{
		std::map<std::string, TribeRole> fileRoles;
		Check(LoadRolesFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", fileRoles).empty(), "the roles of assets/data/era1.json load");
		Check(fileRoles == DefaultRoles(), "and say what DefaultRoles says");
		std::map<std::string, TribeRole> back;
		Check(LoadRoles("{ \"roles\": " + RolesToJson(DefaultRoles()) + " }", back).empty() && back == DefaultRoles(), "the roles round-trip");
		std::map<std::string, TribeRole> keep = DefaultRoles();
		Check(!LoadRoles(R"({ "roles": { "a": { "weights": { "x": 1 } }, "b": { "weights": { "x": -1 } } } })", keep).empty() && keep == DefaultRoles(),
			"a negative weight is refused and nothing changes");
		Check(!LoadRoles(R"({ "roles": { "c": { "tool": "stone_axe" } } })", keep).empty(), "a tool without its tag is refused");
		Check(LoadRoles(R"({ "items": [] })", keep).empty() && keep == DefaultRoles(), "a file without roles changes nothing");

		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> defs;
		RegisterEraOneItems(items, recipes, defs);
		EntityWorld entities;
		GameplayWorld gameplay;
		Tribe t;
		t.Bind(entities, gameplay, items, recipes, defs);
		Check(t.RoleWeight("miner", "flint_outcrop") == 1.0f && t.RoleWeight("miner", "copper_vein") == 1.0f, "a miner works flint and copper (Resource.Stone.*)");
		Check(t.RoleWeight("woodcutter", "old_tree") == 1.0f && t.RoleWeight("woodcutter", "young_tree") == 0.6f, "a woodcutter fells old trees and takes branches");
		Check(t.RoleWeight("forager", "fiber_grass") == 0.9f && t.RoleWeight("forager", "berry_bush") == 1.0f && t.RoleWeight("forager", "reed_bed") == 0.6f,
			"a forager: the node's id comes before its kind");
		Check(t.RoleWeight("forager", "rock") == 0.2f && t.RoleWeight("nobody", "rock") == 0.3f, "the old weights hold and an unknown role gets the default");
		ResourceNodeDef bog;
		bog.Id = "bog_iron";
		bog.Item = "stone";
		bog.Kind = "Stone.Iron.Bog";
		t.Config.Roles["smith"].Weights = { { "Resource.Stone.Iron", 0.8f } };
		defs[bog.Id] = bog;
		t.Bind(entities, gameplay, items, recipes, defs);
		Check(t.RoleWeight("miner", "bog_iron") == 1.0f && t.RoleWeight("smith", "bog_iron") == 0.8f && t.RoleWeight("smith", "tree") == 0.3f,
			"a new node and a new role need no code");
	}

	// The rest of the tribe's data: the "tribe" section (agenda, food, stock) of era1.json says what the code says, and a role's own agenda and way of walking.
	{
		TribeConfig file;
		file.Agenda = TribeAgenda();
		file.Economy = TribeEconomy();
		Check(LoadTribeRulesFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", file).empty(), "the tribe section of assets/data/era1.json loads");
		Check(file.Agenda == DefaultAgenda() && file.Economy == DefaultEconomy(), "and says what DefaultAgenda and DefaultEconomy say");
		TribeConfig back;
		back.Agenda = TribeAgenda();
		back.Economy = TribeEconomy();
		Check(LoadTribeRules("{ \"tribe\": " + TribeRulesToJson(file) + " }", back).empty() && back.Agenda == file.Agenda && back.Economy == file.Economy, "the tribe section round-trips");
		TribeConfig keep = file;
		Check(!LoadTribeRules(R"({ "tribe": { "agenda": { "night": { "sleep": 2 } }, "stock": { "wood": -1 } } })", keep).empty() && keep.Agenda == file.Agenda && keep.Economy == file.Economy,
			"a negative stock is refused and nothing changes (not even the agenda before it)");
		Check(!LoadTribeRules(R"({ "tribe": { "agenda": { "night": { "sleep": "a lot" } } } })", keep).empty(), "a factor that is not a number is refused");

		std::map<std::string, TribeRole> roles = DefaultRoles();
		Check(LoadRoles(R"({ "roles": { "watch": { "weights": { "fire": 1 }, "nav": { "slopeWeight": 1, "loadFactor": 4 },
			"agenda": { "night": { "gather": 1.2 }, "day": { "sleep": 1.5 } }, "nightShift": 8 } } })", roles).empty(), "a role with its own agenda and way of walking loads");
		const TribeRole& watch = roles["watch"];
		Check(watch.Nav.SlopeWeight == 1.0f && watch.Nav.LoadFactor == 4.0f && watch.Nav.WaterWeight == NavProfile{}.WaterWeight && watch.NightShift == 8.0f, "with what it says, and the rest default");
		std::map<std::string, TribeRole> again;
		Check(LoadRoles("{ \"roles\": " + RolesToJson(roles) + " }", again).empty() && again == roles, "and round-trips");
		Check(!LoadRoles(R"({ "roles": { "x": { "nav": { "wings": 1 } } } })", again).empty(), "a nav key the profile does not have is refused");

		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> defs;
		RegisterEraOneItems(items, recipes, defs);
		EntityWorld entities;
		GameplayWorld gameplay;
		Tribe t;
		t.Bind(entities, gameplay, items, recipes, defs);
		t.Config.Roles = roles;
		const float night = t.Config.Needs.NightFrom + 0.5f, dusk = t.Config.Needs.NightFrom - 1.0f, noon = 12.0f;
		Check(t.AgendaFactor("sleep", night, Season::Summer) == 1.6f && t.AgendaFactor("gather", night, Season::Summer) == 0.4f && t.AgendaFactor("eat", night, Season::Summer) == 1.0f,
			"the tribe's agenda: the night is for sleeping, not for work, and eating is untouched");
		Check(t.AgendaFactor("socialize", dusk, Season::Summer) == 2.5f && t.AgendaFactor("gather", noon, Season::Summer) == 1.0f, "the dusk is for company and the day for work");
		Check(t.AgendaFactor("forager", "gather", night, Season::Summer) == 0.4f && t.AgendaFactor("nobody", "sleep", night, Season::Summer) == 1.6f, "a role with no agenda of its own follows the tribe's");
		Check(t.AgendaFactor("watch", "sleep", noon + 8.0f - 12.0f + 12.0f, Season::Summer) == 1.5f, "the watch sleeps by day (its day is its own: day sleep 1.5)");
		Check(t.AgendaFactor("watch", "gather", night + 8.0f > 24.0f ? night + 8.0f - 24.0f : night + 8.0f, Season::Summer) == 1.2f, "and its night, eight hours later than the clan's, it works");
		Check(t.AgendaFactor("watch", "gather", night, Season::Summer) == 1.0f, "while the clan sleeps it is in its day");

		Check(t.IsFood("berry") && !t.IsFood("water") && !t.IsFood("wood"), "food is what is tagged Item.Food, and water is drunk, not eaten");
		Check(t.StockWanted("wood") == 10.0f && t.StockWanted("stone") == 8.0f && t.StockWanted("fiber") == 12.0f && t.StockWanted("berry") == 6.0f, "the stock targets of the data");
		FakeBody a("A", gameplay, entities, glm::vec2(0.0f)), b("B", gameplay, entities, glm::vec2(0.0f)), c("C", gameplay, entities, glm::vec2(0.0f));
		t.AddMember("A", "watch", &a);
		t.AddMember("B", "forager", &b);
		t.AddMember("C", "forager", &c);
		Check(t.StockWanted("berry") == 9.0f, "berries: 3 per member once that is more than 6");
		t.Config.Economy.Stock["grain"] = { 4.0f, 0.0f };
		Check(t.StockWanted("grain") == 4.0f, "a new item's stock is data");

		Check(t.Navigation(t.Members()[0]).Load == 0.0f && t.Navigation(t.Members()[0]).LoadFactor == 4.0f, "an empty bag is no load, and the role's profile comes with it");
		a.Bag().Add(items, "stone", 4);
		Check(std::abs(t.Navigation(t.Members()[0]).Load - 0.5f) < 1e-4f, "half of FullLoad in the bag is half a load");
		a.Bag().Add(items, "stone", 8);
		Check(t.Navigation(t.Members()[0]).Load == 1.0f, "a full bag is a full load");
		Inventory& limited = b.Bag();
		limited = Inventory(12, 10.0f);
		limited.Add(items, "wood", 2);
		const ItemDef* wood = items.Find("wood");
		Check(wood && std::abs(t.Navigation(t.Members()[1]).Load - std::min(1.0f, 2.0f * wood->Weight / 10.0f)) < 1e-4f, "a bag with a weight limit is loaded by the weight it carries");
	}

	// The pyramid of needs (Maslow): short of food, staying alive comes first; with the days of food in hand the rest of the board goes on (and says so).
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> defs;
		RegisterEraOneItems(items, recipes, defs);
		EntityWorld entities;
		GameplayWorld gameplay;
		Tribe t;
		t.Bind(entities, gameplay, items, recipes, defs);
		FakeBody a("A", gameplay, entities, glm::vec2(0.0f)), b("B", gameplay, entities, glm::vec2(0.0f)), c("C", gameplay, entities, glm::vec2(0.0f));
		t.AddMember("A", "forager", &a);
		t.AddMember("B", "forager", &b);
		t.AddMember("C", "miner", &c);
		Job food, craft, fire;
		food.Kind = "gather"; food.Target = "berry";
		craft.Kind = "craft"; craft.Target = "stone_axe";
		fire.Kind = "stoke"; fire.Target = "Campfire";
		Check(t.FoodDays() == 0.0f && t.FoodShortage() == 1.0f && t.MaslowTier() == "survival", "no food at all: zero days, as short as can be, the tier is survival");
		Check(t.SurvivalFactor(food) == 1.0f + t.Config.Work.Jobs.SurvivalBoost, "then a food job is worth (1 + boost) times");
		Check(std::abs(t.SurvivalFactor(craft) - t.Config.Work.Jobs.SurvivalDamp) < 1e-5f && t.SurvivalFactor(fire) == 1.0f, "and a tool waits (damp) while the fire is still staying alive");
		t.Store().Add(items, "berry", 40);
		const float days = t.FoodDays();
		Check(days > t.Config.Work.Jobs.SurvivalDays && t.FoodShortage() == 0.0f, "forty berries are more days than the clan wants for three members");
		Check(t.MaslowTier() != "survival" && t.SurvivalFactor(food) == 1.0f && t.SurvivalFactor(craft) == 1.0f, "with them the pyramid steps aside: the tier moves up and every job is worth its own");
		t.Store().Remove("berry", 30);
		Check(t.FoodShortage() > 0.0f && t.FoodShortage() < 1.0f && t.SurvivalFactor(craft) < 1.0f && t.SurvivalFactor(craft) > t.Config.Work.Jobs.SurvivalDamp, "ten berries: partly short, partly damped (the pressure is continuous)");
		a.Bag().Add(items, "berry", 5);
		Check(t.FoodDays() > days * 0.0f && t.FoodDays() > 10.0f / 16.0f, "what the members carry counts as food in hand");
		t.Record().Every = 10.0;
		t.Update(1.0f);
		Check(!t.Record().Rows().empty() && t.Record().Rows().front().Members.size() == 3, "the chronicle takes its first row, with the three members");
		Check(t.Record().Markdown().find("## Members") != std::string::npos && t.Record().Csv().find("member,role,job") != std::string::npos, "and writes the report and the series");
	}

	// ---- the telemetry: one SQLite file per run (docs/DATOS_INDIVIDUO.md)
	{
		const std::filesystem::path dir = std::filesystem::temp_directory_path();
		const std::string file = (dir / "gb_telemetry_test.db").string();
		for (const char* ext : { "", "-wal", "-shm" }) std::filesystem::remove(file + ext);
		auto scalar = [&file](const std::string& sql) -> std::string
		{
			sqlite3* h = nullptr;
			std::string out = "?";
			if (sqlite3_open_v2(file.c_str(), &h, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK)
			{
				sqlite3_stmt* s = nullptr;
				if (sqlite3_prepare_v2(h, sql.c_str(), -1, &s, nullptr) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW)
					out = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
				sqlite3_finalize(s);
			}
			sqlite3_close(h);
			return out;
		};
		Village without;
		without.Run(600.0);
		Village with;
		TelemetrySink sink;
		sink.FlushSeconds = 0.05;
		TelemetryRunInfo info;
		info.Seed = 7;
		info.Profile = "test";
		Check(sink.Start(file, info).empty() && sink.Active() && sink.RunId() == 1, "the telemetry opens its file and starts run 1");
		with.Tribe_.SetTelemetry(&sink, 3);
		with.Run(600.0);
		Check(without.Fingerprint() == with.Fingerprint() && !with.bad, "a run ends in the same state with the telemetry on or off");
		const size_t said = with.Tribe_.Log().size();
		sink.Stop(600.0);
		Check(!sink.Active() && sink.Stats().WriteErrors == 0, "it stops cleanly, with no write errors");
		Check(scalar("PRAGMA integrity_check") == "ok", "and the file passes the integrity check");
		Check(scalar("SELECT closed FROM runs WHERE run_id = 1") == "1" && scalar("SELECT sim_seconds FROM runs WHERE run_id = 1") == "600.0", "the run is closed with its length");
		Check(scalar("SELECT COUNT(*) FROM individuals WHERE clan_id = 3") == "4", "each member has its sheet, under the clan the host gave");
		Check(scalar("SELECT COUNT(*) FROM clan_samples") == "10" && scalar("SELECT COUNT(*) FROM samples") == "40", "a clan row and a row per member every 60 s");
		Check(scalar("SELECT COUNT(*) FROM events WHERE kind = 'say'") == std::to_string(said) && said > 0, "an event of kind say for every line of the log");
		Check(scalar("SELECT COUNT(*) FROM events WHERE kind = 'task_end'") != "0" && scalar("SELECT COUNT(*) FROM events WHERE kind = 'task_end' AND result = 'ok'") != "0", "and the end of every task, with its result");
		Check(scalar("SELECT COUNT(*) FROM samples WHERE hunger >= 0 AND x IS NOT NULL AND skills LIKE '{%'") == "40", "the samples carry the needs, the place and the skills");

		Check(scalar("SELECT COUNT(*) FROM tasks WHERE result = 'ok' AND end >= start") != "0", "a row in tasks for every task, with its start, end and result");
		Check(scalar("SELECT COUNT(*) FROM tasks") == scalar("SELECT COUNT(*) FROM events WHERE kind = 'task_end'"), "as many as task_end events");
		Check(scalar("SELECT COUNT(*) FROM decisions WHERE options LIKE '[{%'") != "0" && scalar("SELECT COUNT(*) FROM decisions WHERE chosen = ''") == "0", "the decisions carry the options it weighed");
		// a full queue loses samples, never events
		for (const char* ext : { "", "-wal", "-shm" }) std::filesystem::remove(file + ext);
		TelemetrySink small;
		small.SampleCapacity = 5;
		small.FlushSeconds = 0.05;
		Check(small.Start(file).empty(), "a second sink opens a fresh file");
		small.HoldWriter(true);
		for (int i = 0; i < 100; ++i)
		{
			TelemetrySample s;
			s.Member = "Ana";
			s.Time = i;
			small.Sample(s);
		}
		for (int i = 0; i < 10; ++i)
		{
			TelemetryEvent e;
			e.Kind = "say";
			e.Time = i;
			small.Event(e);
		}
		Check(small.Stats().SamplesDropped == 95, "with the writer behind, the samples past the queue are dropped and counted");
		small.Stop();
		Check(scalar("SELECT COUNT(*) FROM samples") == "5" && scalar("SELECT COUNT(*) FROM events") == "10", "and the events all arrive");
		Check(scalar("SELECT samples_dropped FROM runs") == "95" && scalar("SELECT events_written FROM runs") == "10", "the run says how much was dropped");
		small.Stop();   // twice is harmless
		for (const char* ext : { "", "-wal", "-shm" }) std::filesystem::remove(file + ext);
	}

	AbstractOnTheGrid();

	// Limits: a tribe nobody built, a tribe with no one in it, and a tribe in a land with nothing to gather (everyone goes hungry and says why).
	{
		Tribe unbound;
		unbound.Update(1.0f);
		Check(unbound.Time() == 0.0 && unbound.Log().empty(), "an unbound tribe does nothing (and does not crash)");

		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> defs;
		RegisterEraOneItems(items, recipes, defs);
		EntityWorld entities;
		GameplayWorld gameplay;
		Tribe none;
		none.Bind(entities, gameplay, items, recipes, defs);
		for (int i = 0; i < 600; ++i) none.Update(dt);
		Check(none.Members().empty() && none.Log().empty() && none.Time() > 9.9, "an empty tribe lets time pass and does nothing else");
		Check(none.Report().find("store: empty") == 0, "and reports an empty store");

		Tribe bare;
		bare.Bind(entities, gameplay, items, recipes, defs);
		FakeBody body("Lone", gameplay, entities, glm::vec2(0.0f));
		bare.AddMember("Lone", "woodcutter", &body);
		for (int i = 0; i < 60 * 100; ++i) bare.Update(dt);
		Check(bare.Members()[0].Meals == 0, "in a land with nothing to eat the worker eats nothing (and nothing crashes)");
		const size_t lines = bare.Log().size();
		for (int i = 0; i < 60 * 100; ++i) bare.Update(dt);
		Check(bare.Log().size() < lines + 4, "a worker with nothing to gather does not fill the log");
	}

	Village v;
	v.Run(600.0);
	const Tribe& t = v.Tribe_;
	for (const std::string& line : t.Log()) std::fprintf(stderr, "  %s\n", line.c_str());
	std::fprintf(stderr, "%s", t.Report().c_str());

	Check(!v.bad, "no node, store or bag ever holds a negative or empty count");
	Check(t.Members()[1].Made >= 1 && t.Members()[2].Made >= 1, "the woodcutter and the miner each made their tool");
	bool axe = false, pick = false;
	for (const auto& b : v.bodies)
	{
		axe = axe || b->Bag().Count("stone_axe") > 0;
		pick = pick || b->Bag().Count("stone_pickaxe") > 0;
	}
	Check(axe && pick, "the tools are in the bags of the ones who need them");
	Check(t.HasFire() && v.Entities.FindByName("Campfire") != NullEntity, "the campfire is lit");
	const AbilitySystemComponent* fire = v.Gameplay.Find("Campfire");
	Check(fire && fire->HasTag("Interactive.Campfire") && fire->Attributes().Has("Fuel"), "the campfire is the fire pit interactive (tags and Fuel)");
	Check(fire && std::fabs(fire->Attributes().Current("BurnSeconds") - t.Config.FireSeconds / t.Config.FireHeat) < 1e-4f, "it burns at the tribe's pace");
	bool fed = false, wentOut = false;
	for (const std::string& line : t.Log())
	{
		fed = fed || line.find("feeds the campfire") != std::string::npos;
		wentOut = wentOut || line.find("goes out") != std::string::npos;
	}
	Check(fed, "a member fed the campfire with Stoke (wood from the store)");
	Check(t.FireBurning() || wentOut, "it burns, or the log said it went out");
	Check(t.Store().Count("fiber") + t.Store().Count("berry") + t.Store().Count("wood") + t.Store().Count("stone") > 0, "the store holds what was brought");
	int deposited = 0;
	for (const TribeMember& m : t.Members()) { deposited += m.Deposited; Check(m.Failures == 0, "no trip failed"); }
	Check(deposited >= 20, "the tribe carried a good amount to the store");

	// A save from before the campfire was an interactive: the tribe's own fire/burning/fireTimer/fireHeat become the fire pit, with the heat left.
	{
		Village old;
		std::string text = old.Tribe_.ToJson();
		const std::string from = "\"fire\":0";
		Check(text.find(from) != std::string::npos, "(a fresh tribe saves no fire)");
		text.replace(text.find(from), from.size(), "\"fire\":1,\"burning\":1,\"fireTimer\":15.0,\"fireHeat\":1.0");
		Check(old.Tribe_.FromJson(text, false), "an old save loads");
		const AbilitySystemComponent* pit = old.Gameplay.Find("Campfire");
		Check(old.Tribe_.HasFire() && old.Tribe_.FireBurning() && pit && pit->HasTag("Interactive.Campfire"), "its burning fire is now the fire pit, lit");
		Check(pit && std::fabs(pit->Attributes().Current("Fuel") - 2.0f) < 1e-4f, "with the heat it had left (half a burn of 2 and 1 put in)");
		Check(old.Tribe_.ToJson().find("burning") == std::string::npos, "and it saves no fire state of its own any more");
		old.Run(5.0);
		Check(old.Entities.FindByName("Campfire Flame") != NullEntity && old.Entities.Find(old.Entities.FindByName("Campfire Flame"))->Active, "the interactive system shows its flame");

		Village cold;
		text = cold.Tribe_.ToJson();
		text.replace(text.find(from), from.size(), "\"fire\":1,\"burning\":0,\"fireTimer\":3.0");
		Check(cold.Tribe_.FromJson(text, false) && cold.Tribe_.HasFire() && !cold.Tribe_.FireBurning(), "an old save whose fire had gone out loads it out");
	}

	Village w;
	w.Run(600.0);
	Check(w.Fingerprint() == v.Fingerprint(), "two runs end identically");

	// The way they walk: before each order the body gets its role's profile with the load of its bag.
	{
		float loaded = 0.0f;
		for (const auto& b : v.bodies) loaded = std::max(loaded, b->MaxLoad);
		Check(v.bodies[0]->Profiles > 0 && loaded > 0.0f, "the bodies were given their profile, loaded when the bag was full");
		Check(v.bodies[1]->Profile.LoadFactor == 2.5f && v.bodies[2]->Profile.LoadFactor == 3.0f && v.bodies[0]->Profile.VegetationWeight == 0.8f, "each with its role's way of walking");
	}

	// Far from the viewer the members are abstract agents that walk the routes their bodies give them.
	{
		Village far;
		far.Tribe_.SetViewer({ 1000.0f, 1000.0f });
		far.Run(120.0);
		int routes = 0;
		for (const auto& b : far.bodies) routes += b->Routes;
		Check(far.Tribe_.AbstractCount() == 4 && routes > 0, "abstract members ask their bodies for the way");
		Check(!far.bad, "and nothing breaks");
	}

	// Knowledge (H11): with the technologies of era 1 the tribe does what it did (stone tools and fire are known from the start). A woodcutter whose trade
	// asks for a flint axe cannot make it until it works out flint knapping, which it tries at the camp with the materials in the store; the others see
	// it make the axe (or are taught at the camp) and learn too. Deterministic: two runs tell the same story.
	{
		TechTree techs = DefaultTechs();
		techs.Techs["flint_knapping"].Discovery.Experiment = 0.3f;   // quicker than the game's, to keep the test short
		techs.Techs["flint_knapping"].Discovery.Seconds = 5.0f;
		auto run = [&](std::vector<std::string>& log, int& axes)
		{
			Village k;
			k.Tribe_.SetTechs(&techs);
			k.Tribe_.Config.Roles["woodcutter"].Tool = "flint_axe";
			k.Tribe_.Config.Roles["woodcutter"].ToolTag = "Item.Tool.Axe";
			k.Tribe_.Store().Add(k.Items, "branch", 6);
			k.Tribe_.Store().Add(k.Items, "cordage", 3);
			k.Tribe_.Store().Add(k.Items, "flint", 6);
			Check(!k.Tribe_.Knowledge().Knows("Biel", "flint_knapping") && !k.Tribe_.Knowledge().Knows("Biel", "basketry") && k.Tribe_.Knowledge().Knows("Biel", "stone_tools"),
				"they start with the technologies of the start only");
			k.Run(300.0);
			log = k.Tribe_.Log();
			axes = k.bodies[1]->Bag().Count("flint_axe");
			Check(!k.bad, "knowledge breaks none of the tribe's invariants");
			Check(k.Tribe_.Knowledge().Knows("Biel", "flint_knapping"), "the woodcutter works out flint knapping");
			int learnt = 0;
			for (const char* n : { "Ana", "Cleo", "Dan" }) learnt += k.Tribe_.Knowledge().Knows(n, "flint_knapping") || !k.Tribe_.Knowledge().Of(n)->Progress.empty() ? 1 : 0;
			Check(learnt > 0, "and someone else learns it or is learning it (by watching, being taught or trying)");
			Check(k.Tribe_.Knowledge().Clan().count("flint_knapping") == 1, "it shows the clan at the camp");
		};
		std::vector<std::string> log1, log2;
		int axes1 = 0, axes2 = 0;
		run(log1, axes1);
		run(log2, axes2);
		Check(log1 == log2 && axes1 == axes2, "two runs tell the same story");
		const auto find = [&](const std::string& part) { return std::find_if(log1.begin(), log1.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; }); };
		const auto found = find("discovers Flint knapping"), made = find("makes a flint_axe");
		Check(found != log1.end(), "the log tells the discovery");
		Check(made != log1.end() && found < made && axes1 == 1, "and the flint axe is made only after it");
		Check(find("sits down at the camp to work out Flint knapping") != log1.end(), "it went to the camp to try");
		if (found == log1.end() || made == log1.end())
			for (const std::string& l : log1) if (l.find("Biel") != std::string::npos) std::fprintf(stderr, "  %s\n", l.c_str());
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("tribe: ok");
	return 0;
}
