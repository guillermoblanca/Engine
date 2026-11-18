// T306 + T307 (C2, documentation/01_diseno/MODO_COLONIA.md §4.3): the table of work priorities of each person (GB/Game/WorkPriorities.h) and
// the stock goals (GB/Game/StockGoals.h). Checked, on a clan with no player and stand-in bodies (as colony_designations_test), with the
// table switched on as the colony's host does:
//   (0) every kind of work the board posts or the brain offers is in a category of work.categories, or free;
//   (a) Cleo with crafting 1 and every other category at "-" only does crafting work (and eats, drinks, rests...), and does some;
//   (b) the order is by category, then by job: a category 1 with jobs at 3 and 5 is taken before a category 2 with a job at 1, whatever
//       the scores (the pure order, and a clan where the wood job is worth more to her than the crafting one);
//   (c) a category at "-" turns all its jobs off, even one set to 1 by itself;
//   (d) a critical hunger wins over the table: with food at "-" and nothing in the store, she goes for food;
//   (e) a goal of 3 stone axes with the materials in the store makes someone make them up to 3, and no more;
//   (f) a goal that cannot be met says why;
//   (g) saving and loading keeps the tables and the goals; with neither nothing is saved.
#include "GB/Log.h"
#include "GB/Game/StockGoals.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/WorkPriorities.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
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

// File-local (T51: several tests share one executable)
namespace
{
class WorkBody : public ITribeBody
{
public:
	WorkBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
	bool Busy() const override { return !m_Steps.empty(); }
	bool Failed() const override { return m_Failed; }
	glm::vec2 Position() const override { return m_Pos; }
	Inventory& Bag() override { return m_Bag; }
	void Teleport(const glm::vec2& p) override { m_Pos = p; m_Steps.clear(); m_Started = false; }
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
			std::string why;
			if (!me->Activate(s.Ability, m_Gameplay.Find(s.Node), &why, s.Node)) { m_Failed = true; m_Steps.clear(); return; }
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

// A clan of four with a patch of resource nodes, a well and a workbench, no player among them; the colony's table switched on.
struct Camp
{
	explicit Camp(int food = 200)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		RegisterBuildingContent(Gameplay.Library);
		BuildResourcePatch(Entities, glm::vec3(0.0f), Defs, 4);
		BuildInteractive(Entities, DefaultInteractives().at("well"), "Well", NullEntity, { 4.0f, 0.0f, -14.0f });
		const char* names[] = { "Ana", "Biel", "Cleo", "Dan" };
		const char* roles[] = { "forager", "woodcutter", "miner", "forager" };
		for (const char* n : names)
		{
			Entity& e = *Entities.Find(Entities.Create(n));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "Build" };
		}
		Gameplay.Load(Entities);
		for (const char* n : names)
		{
			bodies.push_back(std::make_unique<WorkBody>(n, Gameplay, Entities, Home));
			bodies.back()->m_Items = &Items;
		}
		auto bagOf = [this](const std::string& who) -> Inventory* { for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == who) return &bodies[i]->Bag(); return nullptr; };
		Resources.Bind(Gameplay, Defs, [this, bagOf](const std::string& who, const std::string& item, int n) { if (Inventory* b = bagOf(who)) b->Add(Items, item, n); });
		Fires.Items = &Items;
		Fires.Bind(Gameplay, [bagOf](const std::string& who, const std::string& item, int n) { Inventory* b = bagOf(who); return b ? b->Remove(item, n) : 0; },
			[this, bagOf](const std::string& who, const std::string& item, int n) { Inventory* b = bagOf(who); return b ? b->Add(Items, item, n) : 0; });
		Builds.Bind(Gameplay);
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		Tribe_.SetBuildings(&Builds.Defs);
		Tribe_.Config.Economy.Camp = Home;
		Tribe_.Config.Simulation.LogLines = 1000000;   // (the whole log is read)
		Tribe_.Config.PlanSeconds = 1e9f;              // (the planner does not start sites of its own)
		Tribe_.Store().Add(Items, "wood", 10);
		Tribe_.Store().Add(Items, "stone", 12);
		if (food > 0) Tribe_.Store().Add(Items, "berry", food);
		for (size_t i = 0; i < 4; ++i) Tribe_.AddMember(names[i], roles[i], bodies[i].get());
		Tribe_.WorkTable().SetActive(true);   // (the host's switch: WorldDirector::Update in the colony)
	}
	void Step()
	{
		Gameplay.Update(dt);
		Resources.Update(Gameplay, dt);
		for (auto& b : bodies) b->Update(dt);
		Resources.SyncVisuals(Gameplay, Entities);
		Fires.Update(dt, &Entities);
		Builds.Update(dt, &Entities);
		Tribe_.Update(dt);
	}
	void Run(float seconds) { for (int i = 0; i < static_cast<int>(seconds / dt); ++i) Step(); }
	bool Workbench()
	{
		std::string name;
		return Tribe_.RaiseBuilding("workbench", Home + glm::vec2(6.0f, 4.0f), 0.0f, &name).empty();
	}
	const TribeMember& Member(const std::string& name) const { return *Tribe_.FindMember(name); }
	WorkBody& Body(const std::string& name) { for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == name) return *bodies[i]; return *bodies[0]; }
	void Need(const std::string& name, const char* need, float value) { Gameplay.Find(name)->Attributes().SetBase(need, value); }
	// Every category of a person at `number`, but `except` at `other`.
	void All(const std::string& who, int number, const std::string& except = {}, int other = 0)
	{
		for (const WorkCategoryDef& c : Tribe_.Config.Work.Categories.List)
			Check(Tribe_.WorkTable().Set(Tribe_, who, c.Id, c.Id == except ? other : number).Ok, "a category is set");
	}
	// The job of the table the member is doing now ("" = free work: eating, resting...).
	std::string TableJob(const std::string& who) const
	{
		const TribeMember& m = Member(who);
		return WorkPriorities::JobOf(Tribe_, &m, m.Job, m.Detail);
	}
	const glm::vec2 Home{ 0.0f, -8.0f };
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Fires;
	BuildingSystem Builds;
	Tribe Tribe_;
	std::vector<std::unique_ptr<WorkBody>> bodies;
};

	// The jobs of a member's work that are not crafting (Tribe's m.Job; "craft" and "make" are; eating, sleeping, resting are no work).
	bool OtherWork(const std::string& job)
	{
		static const std::set<std::string> other = { "gather", "approach", "explore", "stoke", "fire", "build", "deliver", "haul", "farm", "hunt", "experiment", "pave", "care", "expedition" };
		return other.count(job) != 0;
	}
}

int GBTest_colony_priorities(int, char**)
{
	Log::Init();

	// ---- (0) every kind of work is in a category, or free
	{
		const WorkCategories& cats = DefaultWorkRules().Categories;
		Check(!cats.Empty(), "(0) era1.json has work.categories");
		const char* kinds[] = { "build", "craft", "deliver", "designate", "expedition", "farm", "fire", "gather", "haul", "hunt", "make", "stoke",   // the board's
			"talk", "snare", "deposit", "warm", "tool", "teach", "socialize", "sleep", "rite", "rest", "regroup", "recover", "pave", "migrate", "gear",
			"explore", "experiment", "envoy", "eat", "drink", "care" };   // the brain's options
		for (const char* k : kinds)
		{
			const bool free = std::find(cats.Free.begin(), cats.Free.end(), k) != cats.Free.end();
			const bool ranked = !cats.JobOf(k, "", "", {}).empty();
			if (free == ranked) std::fprintf(stderr, "kind %s: free %d, ranked %d\n", k, free ? 1 : 0, ranked ? 1 : 0);
			Check(free != ranked, "(0) a kind of work is either in a category or free");
		}
		Check(cats.JobOf("gather", "berry", "berry", { "Item.Food" }) == "forage" && cats.JobOf("gather", "wood", "wood", { "Item.Resource.Wood" }) == "wood"
			&& cats.JobOf("make", "dried_berry", "dried_berry", { "Item.Food", "Item.Food.Preserved" }) == "preserve" && cats.JobOf("make", "stone_axe", "stone_axe", { "Item.Tool.Axe" }) == "tools"
			&& cats.JobOf("farm", "harvest", "", {}) == "harvest" && cats.JobOf("designate", "x", "flint", { "Item.Resource.Stone.Flint" }) == "stone"
			&& cats.JobOf("explore", "well", "", {}).empty(), "(0) the most specific pattern wins; aliases and free patterns");
		WorkCategories bad;
		Check(!LoadWorkCategories("{\"list\":[{\"id\":\"a\",\"jobs\":[{\"id\":\"a\",\"match\":[\"x\"]}]}]}", bad) .empty()
			&& !LoadWorkCategories("{\"list\":[{\"id\":\"a\",\"default\":11,\"jobs\":[{\"id\":\"b\",\"match\":[\"x\"]}]}]}", bad).empty() && bad.Empty(),
			"(0) a repeated id or a number past 10 is refused");
	}

	// ---- (b) the pure order: category, then job, then score
	{
		// category 1 with jobs at 3 and 5, category 2 with a job at 1; the scores the other way round
		const std::vector<WorkPriorities::Candidate> c = { { 2 * 16 + 1, 9.0f }, { 1 * 16 + 5, 4.0f }, { 1 * 16 + 3, 0.5f } };
		Check(WorkPriorities::Choose(c, -1, 0.3f) == 2, "(b) category 1 job 3 before category 1 job 5 before category 2 job 1");
		Check(WorkPriorities::Choose(c, 0, 0.3f) == 2, "(b) a job held of a worse rank is let go for a better one");
		const std::vector<WorkPriorities::Candidate> same = { { 20, 1.0f }, { 20, 1.2f } };
		Check(WorkPriorities::Choose(same, 0, 0.3f) == 0 && WorkPriorities::Choose(same, -1, 0.3f) == 1, "(b) in one rank the score chooses, with the margin for the one held");
		Check(WorkPriorities::Choose({ { 0, 0.1f }, { 17, 5.0f } }, -1, 0.3f) == 0, "(b) what the needs boost (rank 0) goes first");
	}

	// ---- (a) crafting 1, everything else "-": only crafting work
	{
		Camp v;
		Check(v.Workbench(), "(a) a workbench stands");
		v.Tribe_.Store().Add(v.Items, "fiber", 30);
		v.Tribe_.Store().Add(v.Items, "wood", 10);
		v.All("Cleo", WorkPriorities::kOff, "crafting", 1);
		Check(v.Tribe_.WorkTable().Number(v.Tribe_, "Cleo", "tools") == 1 && v.Tribe_.WorkTable().Number(v.Tribe_, "Cleo", "wood") == WorkPriorities::kOff, "(a) the table says so");
		std::set<std::string> jobs;
		bool other = false;
		const int made = v.Member("Cleo").Made;
		for (int i = 0; i < static_cast<int>(240.0f / dt); ++i)
		{
			v.Step();
			const TribeMember& m = v.Member("Cleo");
			jobs.insert(m.Job);
			if (OtherWork(m.Job)) { if (!other) std::printf("colony_priorities: (a) Cleo did %s %s\n", m.Job.c_str(), m.Detail.c_str()); other = true; }
		}
		std::string seen;
		for (const std::string& j : jobs) seen += " " + j;
		std::printf("colony_priorities: (a) Cleo's jobs:%s; made %d\n", seen.c_str(), v.Member("Cleo").Made - made);
		Check(!other, "(a) Cleo does no work but crafting");
		Check(jobs.count("craft") || jobs.count("make"), "(a) and she does crafting work");
		Check(v.Tribe_.WorkTable().Show(v.Tribe_, "Cleo").find("crafting 1 [tools 1, make 1]") != std::string::npos, "(a) her table in a line");
	}

	// ---- (a') the same with the clan short of food (nothing in the store): "-" still holds while she is not critically hungry
	{
		Camp v(0);
		Check(v.Workbench(), "(a') a workbench stands");
		v.Tribe_.Store().Add(v.Items, "fiber", 30);
		v.All("Cleo", WorkPriorities::kOff, "crafting", 1);
		bool other = false;
		int steps = 0;
		float shortage = 0.0f;
		for (int i = 0; i < static_cast<int>(240.0f / dt); ++i)
		{
			v.Step();
			shortage = std::max(shortage, v.Tribe_.FoodShortage());
			if (v.Gameplay.Find("Cleo")->Attribute(NeedName::Hunger) < v.Tribe_.Config.Work.Jobs.CriticalHunger) break;
			++steps;
			const TribeMember& m = v.Member("Cleo");
			if (OtherWork(m.Job)) { if (!other) std::printf("colony_priorities: (a') Cleo did %s %s\n", m.Job.c_str(), m.Detail.c_str()); other = true; }
		}
		std::printf("colony_priorities: (a') the clan short of food up to %.2f; %.0f s checked\n", static_cast<double>(shortage), static_cast<double>(steps) * dt);
		Check(shortage > 0.5f, "(a') the clan is short of food");
		Check(!other, "(a') Cleo does no work but crafting though the clan is short of food");
	}

	// ---- the marks of T270 ask the table: a fibre tuft marked, fibre at "-" for all but Dan: only Dan takes the mark
	{
		Camp v;
		std::vector<std::string> tufts = v.Gameplay.NamesWithTag("Resource.Node.fiber_grass");
		std::sort(tufts.begin(), tufts.end());
		Check(!tufts.empty() && v.Tribe_.Designations().Designate(v.Tribe_, tufts.front()).Ok, "(marks) a tuft is marked");
		for (const char* n : { "Ana", "Biel", "Cleo" }) v.Tribe_.WorkTable().Set(v.Tribe_, n, "fiber", WorkPriorities::kOff);
		v.Tribe_.WorkTable().Set(v.Tribe_, "Dan", "materials", 1);   // (his food, 1 for a forager, comes after it)
		v.Tribe_.WorkTable().Set(v.Tribe_, "Dan", "food", 2);
		std::set<std::string> holders;
		for (int i = 0; i < static_cast<int>(60.0f / dt); ++i)
		{
			v.Step();
			for (const TribeMember& m : v.Tribe_.Members())
				if (const Job* j = v.Tribe_.Board().ClaimOf(m.Name); j && j->Kind == "designate") holders.insert(m.Name);
		}
		std::string who;
		for (const std::string& h : holders) who += " " + h;
		std::printf("colony_priorities: (marks) held the mark:%s\n", who.c_str());
		Check(holders == std::set<std::string>{ "Dan" }, "(marks) only the one with fibre not at - takes the mark");
	}

	// ---- (b) on the board: crafting 1 with tools 3 and make 5 before materials 2 with wood 1
	{
		Camp v;
		Check(v.Workbench(), "(b) a workbench stands");
		v.Tribe_.Store().Add(v.Items, "fiber", 30);
		v.Tribe_.Store().Remove("wood", 10);   // (the store wants wood: a gather wood job is up, and she likes wood)
		v.All("Cleo", WorkPriorities::kOff, "crafting", 1);
		v.Tribe_.WorkTable().Set(v.Tribe_, "Cleo", "materials", 2);
		v.Tribe_.WorkTable().Set(v.Tribe_, "Cleo", "wood", 1);
		v.Tribe_.WorkTable().Set(v.Tribe_, "Cleo", "tools", 3);
		v.Tribe_.WorkTable().Set(v.Tribe_, "Cleo", "make", 5);
		std::string first;
		bool woodUp = false;
		for (int i = 0; i < static_cast<int>(60.0f / dt) && first.empty(); ++i)
		{
			v.Step();
			woodUp = woodUp || v.Tribe_.Board().Find("gather wood");
			if (const Job* j = v.Tribe_.Board().ClaimOf("Cleo")) first = j->Id;
		}
		std::printf("colony_priorities: (b) Cleo's first job: %s (gather wood on the board: %d)\n", first.c_str(), woodUp ? 1 : 0);
		Check(woodUp, "(b) there is wood to gather on the board");
		Check(first.rfind("make ", 0) == 0 || first.rfind("craft ", 0) == 0, "(b) Cleo takes a crafting job before the wood one");
	}

	// ---- (c) a category at "-" turns all its jobs off, even one at 1 by itself
	{
		Camp v;
		Check(v.Workbench(), "(c) a workbench stands");
		v.Tribe_.Store().Add(v.Items, "fiber", 30);
		v.All("Cleo", 3, "crafting", WorkPriorities::kOff);
		v.Tribe_.WorkTable().Set(v.Tribe_, "Cleo", "tools", 1);
		Check(v.Tribe_.WorkTable().Number(v.Tribe_, "Cleo", "tools") == WorkPriorities::kOff, "(c) the job is off with its category");
		bool crafted = false, worked = false;
		for (int i = 0; i < static_cast<int>(180.0f / dt); ++i)
		{
			v.Step();
			const TribeMember& m = v.Member("Cleo");
			crafted = crafted || m.Job == "craft" || m.Job == "make";
			if (const Job* j = v.Tribe_.Board().ClaimOf("Cleo")) crafted = crafted || j->Kind == "craft" || j->Kind == "make";
			worked = worked || OtherWork(m.Job);
		}
		std::printf("colony_priorities: (c) Cleo crafted %d, other work %d\n", crafted ? 1 : 0, worked ? 1 : 0);
		Check(!crafted, "(c) with crafting at -, Cleo never crafts");
		Check(worked, "(c) she does her other work");
	}

	// ---- (d) a critical hunger wins over the table
	{
		Camp v(0);
		v.All("Cleo", WorkPriorities::kOff, "crafting", 1);
		v.Run(2.0f);
		v.Need("Cleo", NeedName::Hunger, 8.0f);
		const int meals = v.Member("Cleo").Meals;
		bool food = false;
		std::string what;
		for (int i = 0; i < static_cast<int>(120.0f / dt) && !food; ++i)
		{
			v.Step();
			const TribeMember& m = v.Member("Cleo");
			if (v.TableJob("Cleo") == "forage" || m.Job == "eat" || m.Meals > meals) { food = true; what = m.Job + " " + m.Detail; }
		}
		std::printf("colony_priorities: (d) Cleo, food at - and hunger 8, went for food: %s\n", food ? what.c_str() : "no");
		Check(food, "(d) critically hungry with food at -, she goes for food");
	}

	// ---- (e) a goal of 3 stone axes: made up to 3, and no more
	{
		Camp v;
		Check(v.Workbench(), "(e) a workbench stands");
		v.Tribe_.Store().Add(v.Items, "fiber", 40);
		v.Tribe_.Store().Add(v.Items, "stone", 20);
		v.Tribe_.Store().Add(v.Items, "wood", 20);
		v.Body("Biel").Bag().Add(v.Items, "stone_axe", 1);       // (the woodcutter has his own: it counts)
		v.Body("Cleo").Bag().Add(v.Items, "stone_pickaxe", 1);   // (and the miner hers: nobody crafts a tool of a trade)
		v.Tribe_.WorkTable().Set(v.Tribe_, "Dan", "crafting", 1);   // (someone puts crafting first: the trades' suggestions put their own work before it)
		const StockGoals::Reply r = v.Tribe_.Goals().Set(v.Tribe_, "stone_axe", 3);
		std::printf("colony_priorities: (e) %s\n", r.Text.c_str());
		Check(r.Ok && StockGoals::Have(v.Tribe_, "stone_axe") == 1, "(e) the goal is set; the clan has 1");
		float done = -1.0f;
		for (int i = 0; i < static_cast<int>(900.0f / dt) && done < 0.0f; ++i)
		{
			v.Step();
			if (StockGoals::Have(v.Tribe_, "stone_axe") >= 3) done = static_cast<float>(i) * dt;
		}
		v.Run(240.0f);
		const int have = StockGoals::Have(v.Tribe_, "stone_axe");
		std::printf("colony_priorities: (e) 3 axes at %.0f s; %d after 240 s more; %s\n", static_cast<double>(done), have, v.Tribe_.Goals().List(v.Tribe_).c_str());
		Check(done >= 0.0f, "(e) the clan makes axes up to the goal");
		Check(have == 3 && !v.Tribe_.Board().Find("make stone_axe"), "(e) and stops at 3");
		Check(v.Tribe_.Goals().List(v.Tribe_) == "stone_axe 3 / 3 ok", "(e) the list says it is met");
		Check(v.Tribe_.Goals().Off("stone_axe").Ok && v.Tribe_.Goals().Empty() && !v.Tribe_.Goals().Off("stone_axe").Ok, "(e) off lifts it");
	}

	// ---- (f) a goal that cannot be met says why
	{
		Camp v;
		Check(v.Tribe_.Goals().Set(v.Tribe_, "copper_axe", 2).Ok, "(f) a goal of copper axes");
		Check(StockGoals::Status(v.Tribe_, "copper_axe", 2) == "no forge to make it at", "(f) no forge to make it at");
		Check(v.Tribe_.Goals().Set(v.Tribe_, "hide", 2).Ok && StockGoals::Status(v.Tribe_, "hide", 2) == "no way to get it", "(f) no way to get it (no hunts here)");
		v.Tribe_.Store().Remove("stone", 100);
		Check(StockGoals::Status(v.Tribe_, "stone_axe", 1).rfind("gathering ", 0) == 0, "(f) at the camp's workbench, with no fibre nor stone in the store: gathering them");
		Check(!v.Tribe_.Goals().Set(v.Tribe_, "no_such_thing", 2).Ok && !v.Tribe_.Goals().Set(v.Tribe_, "Item.Nothing", 2).Ok && !v.Tribe_.Goals().Set(v.Tribe_, "berry", 0).Ok,
			"(f) an unknown item or tag, or 0, is refused");
		const std::string kept = StockGoals::ItemFor(v.Tribe_, "Item.Food.Preserved");
		std::printf("colony_priorities: (f) preserved food is %s: %s\n", kept.c_str(), StockGoals::Status(v.Tribe_, "Item.Food.Preserved", 10).c_str());
		Check(v.Tribe_.Goals().Set(v.Tribe_, "Item.Food.Preserved", 10).Ok && !kept.empty() && kept != "dried_berry"
			&& StockGoals::Status(v.Tribe_, "Item.Food.Preserved", 10).rfind("lacks ", 0) == 0, "(f) a tag: preserved food, what the clan can make with no rack; it lacks the meat (no hunts here)");
		const std::string list = v.Tribe_.Goals().List(v.Tribe_);
		std::printf("colony_priorities: (f) %s\n", list.c_str());
		Check(list.find("copper_axe 0 / 2 no forge to make it at") != std::string::npos, "(f) game stock lists it with why");
	}

	// ---- (g) save and load
	{
		Camp v;
		v.Tribe_.WorkTable().Clear();
		Check(v.Tribe_.ToJson().find("\"priorities\"") == std::string::npos && v.Tribe_.ToJson().find("\"stock\"") == std::string::npos, "(g) with no table nor goal nothing is saved");
		v.All("Ana", 2, "care", WorkPriorities::kOff);
		v.Tribe_.WorkTable().Set(v.Tribe_, "Ana", "hunt", 7);
		v.Tribe_.Goals().Set(v.Tribe_, "cordage", 5);
		const std::string saved = v.Tribe_.ToJson();
		Check(saved.find("\"priorities\"") != std::string::npos && saved.find("\"stock\"") != std::string::npos, "(g) the tables and the goals are saved");
		Camp w;
		Check(w.Tribe_.FromJson(saved, true), "(g) the save loads");
		Check(w.Tribe_.WorkTable().Of("Ana") && *w.Tribe_.WorkTable().Of("Ana") == *v.Tribe_.WorkTable().Of("Ana") && w.Tribe_.WorkTable().Number(w.Tribe_, "Ana", "hunt") == 7
			&& w.Tribe_.WorkTable().Number(w.Tribe_, "Ana", "heal") == WorkPriorities::kOff, "(g) the same table");
		Check(w.Tribe_.WorkTable().Active(), "(g) the host's switch stays as it was");
		Check(w.Tribe_.Goals().All() == v.Tribe_.Goals().All(), "(g) the same goals");
		Check(w.Tribe_.WorkTable().ToJson() == v.Tribe_.WorkTable().ToJson() && w.Tribe_.Goals().ToJson() == v.Tribe_.Goals().ToJson(), "(g) and saves them again the same");
		WorkPriorities badTable;
		StockGoals badGoals;
		Check(!badTable.FromJson("{\"version\":2,\"people\":{}}") && !badTable.FromJson("{\"version\":1,\"people\":{\"Ana\":{\"categories\":{\"food\":11},\"jobs\":{}}}}") && badTable.Empty(),
			"(g) a bad priorities section is refused");
		Check(!badGoals.FromJson("{\"version\":1,\"goals\":{\"x\":0}}") && badGoals.Empty(), "(g) a bad stock section is refused");
		Check(v.Tribe_.WorkTable().Reset(v.Tribe_, "Ana").Ok && v.Tribe_.WorkTable().Number(v.Tribe_, "Ana", "forage") == 1, "(g) reset: back to a forager's suggestion (food 1)");
	}

	if (failures == 0) std::printf("colony_priorities: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
