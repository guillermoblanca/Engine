// T270 (C1-d, documentation/01_diseno/MODO_COLONIA.md): the work the player designates to the clan and the real order of its sites
// (GB/Game/ColonyDesignations.h). Checked, on a clan with no player and stand-in bodies (as colony_orders_test):
//   (a) of two nodes of a kind at the same distance from the camp, the marked one (the one the clan would take last: it is later by name) is
//       gathered first, by whoever takes the job, and with no mark the other one is;
//   (b) `all` marks the node's stand: every node of its kind linked to it, and only those; `off` lifts them;
//   (c) of two plans with their materials in the store, the first of the queue stands first; after `top` on the second, it is the other way;
//   (d) saving and loading keeps the marks and the queue; with neither nothing is saved;
//   (e) a member critically hungry eats before it works the mark it holds.
#include "GB/Log.h"
#include "GB/Game/ColonyDesignations.h"
#include "GB/Game/ColonyOrders.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
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

// File-local (T51: several tests share one executable)
namespace
{
class MarkBody : public ITribeBody
{
public:
	MarkBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
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

// A clan of four with a patch of resource nodes and a well (as clan_stores_test's village), no player among them.
struct Yard
{
	Yard()
	{
		for (const auto& [id, t] : Techs.Techs)
			for (const std::string& b : t.Buildings) Builds.Defs.erase(b);
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
			bodies.push_back(std::make_unique<MarkBody>(n, Gameplay, Entities, Camp));
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
		Tribe_.Config.Economy.Camp = Camp;
		Tribe_.Config.Simulation.LogLines = 1000000;   // (the whole log is read)
		Tribe_.Config.PlanSeconds = 1e9f;              // (the planner does not start sites of its own: the test does)
		Tribe_.Store().Add(Items, "wood", 10);
		Tribe_.Store().Add(Items, "stone", 12);
		for (size_t i = 0; i < 4; ++i) Tribe_.AddMember(names[i], roles[i], bodies[i].get());
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
	bool Logged(const std::string& part) const
	{
		for (const std::string& l : Tribe_.Log()) if (l.find(part) != std::string::npos) return true;
		return false;
	}
	const TribeMember& Member(const std::string& name) const { return *Tribe_.FindMember(name); }
	MarkBody& Body(const std::string& name) { for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == name) return *bodies[i]; return *bodies[0]; }
	void Need(const std::string& name, const char* need, float value) { Gameplay.Find(name)->Attributes().SetBase(need, value); }
	ColonyOrders::Reply Order(const std::string& who, const std::string& verb, std::vector<std::string> args = {}) { return Tribe_.Colony().Give(Tribe_, who, verb, args); }
	// A node of a kind (the first by name), and how much it holds now.
	std::string NodeOf(const std::string& kind) const
	{
		for (const GameplayWorld::Tagged& t : Gameplay.WithTag("Resource.Node." + kind)) return *t.Name;
		return {};
	}
	int Amount(const std::string& node) const { const AbilitySystemComponent* a = Gameplay.Find(node); return a ? ResourceSystem::Amount(*a) : -1; }
	const glm::vec2 Camp{ 0.0f, -8.0f };
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Fires;
	BuildingSystem Builds;
	TechTree Techs = DefaultTechs();
	Tribe Tribe_;
	std::vector<std::unique_ptr<MarkBody>> bodies;
};
}

namespace
{
	// The nodes of a kind, by name.
	std::vector<std::string> NodesOf(const Yard& v, const std::string& kind)
	{
		std::vector<std::string> out = v.Gameplay.NamesWithTag("Resource.Node." + kind);
		std::sort(out.begin(), out.end());
		return out;
	}
	void Place(Yard& v, const std::string& node, const glm::vec2& at)
	{
		Entity& e = *v.Entities.Find(v.Entities.FindByName(node));
		e.Transform.Position = glm::vec3(at.x, 0.0f, at.y);
	}
	// (a): two fibre tufts at 7 m on either side of the camp, the others far away. Returns the run's seconds until each was first worked (-1: never).
	std::pair<float, float> FirstWorked(bool mark, std::string* first, std::string* second)
	{
		Yard v;
		const std::vector<std::string> tufts = NodesOf(v, "fiber_grass");
		if (tufts.size() < 2) return { -1.0f, -1.0f };
		for (size_t i = 0; i < tufts.size(); ++i)
			Place(v, tufts[i], i == 0 ? v.Camp + glm::vec2(-7.0f, 0.0f) : i == 1 ? v.Camp + glm::vec2(7.0f, 0.0f) : glm::vec2(70.0f + 4.0f * static_cast<float>(i), 70.0f));
		*first = tufts[0];
		*second = tufts[1];
		if (mark) Check(v.Tribe_.Designations().Designate(v.Tribe_, tufts[1]).Ok, "(a) the second tuft is marked");
		const int a0 = v.Amount(tufts[0]), b0 = v.Amount(tufts[1]);
		float ta = -1.0f, tb = -1.0f;
		for (int i = 0; i < static_cast<int>(240.0f / dt) && (ta < 0.0f || tb < 0.0f); ++i)
		{
			v.Step();
			const float t = static_cast<float>(i) * dt;
			if (ta < 0.0f && v.Amount(tufts[0]) < a0) ta = t;
			if (tb < 0.0f && v.Amount(tufts[1]) < b0) tb = t;
			if (mark && tb >= 0.0f) break;
		}
		if (mark)
		{
			bool taken = false;
			for (const std::string& l : v.Tribe_.Log()) taken = taken || l.find("takes the job designate " + tufts[1]) != std::string::npos;
			Check(taken, "(a) someone takes the job of the mark from the board");
		}
		return { ta, tb };
	}

	// (c): two shelters with their materials in the store, queued as the player's plans (the second put on top when `swap`): when each stands.
	std::pair<float, float> Race(bool swap, std::string* firstSite, std::string* secondSite)
	{
		Yard v;
		v.Tribe_.Store().Add(v.Items, "wood", 30);
		v.Tribe_.Store().Add(v.Items, "stone", 20);
		v.Tribe_.Store().Add(v.Items, "reed", 12);
		v.Tribe_.Store().Add(v.Items, "fiber", 12);
		v.Tribe_.Store().Add(v.Items, "berry", 20);
		v.Run(1.0f);
		std::string a, b;
		Check(v.Tribe_.StartSite("shelter", v.Camp + glm::vec2(-9.0f, 6.0f), &a).empty() && v.Tribe_.StartSite("shelter", v.Camp + glm::vec2(9.0f, 6.0f), &b).empty(), "two shelters are started");
		Check(v.Tribe_.Designations().QueueSite(v.Tribe_, a) && v.Tribe_.Designations().QueueSite(v.Tribe_, b), "both are queued (as the player's plans)");
		if (swap)
		{
			const ColonyDesignations::Reply r = v.Tribe_.Designations().MoveSite(v.Tribe_, b, "top");
			Check(r.Ok && r.Text == "site " + b + " is now 1 of 2 in the queue", "top puts the second first");
		}
		const std::vector<std::string> order = v.Tribe_.Designations().SiteOrder(v.Tribe_);
		Check(order.size() == 2 && order[0] == (swap ? b : a), "the order of the sites is the queue's");
		*firstSite = a;
		*secondSite = b;
		auto done = [&](const std::string& site)
		{
			const AbilitySystemComponent* s = v.Gameplay.Find(site);
			const BuildingDef* d = s ? BuildingOf(*s, v.Builds.Defs) : nullptr;
			return d && ReadSite(*s, *d).Done;
		};
		float ta = -1.0f, tb = -1.0f;
		for (int i = 0; i < static_cast<int>(1500.0f / dt) && (ta < 0.0f || tb < 0.0f); ++i)
		{
			v.Step();
			if (ta < 0.0f && done(a)) ta = static_cast<float>(i) * dt;
			if (tb < 0.0f && done(b)) tb = static_cast<float>(i) * dt;
		}
		return { ta, tb };
	}
}

int GBTest_colony_designations(int, char**)
{
	Log::Init();

	// ---- (a) the marked node is gathered first
	{
		std::string first, second;
		const auto [plainA, plainB] = FirstWorked(false, &first, &second);
		const auto [markA, markB] = FirstWorked(true, &first, &second);
		std::printf("colony_designations: %s / %s first worked: no mark %.1f / %.1f s; %s marked %.1f / %.1f s\n", first.c_str(), second.c_str(),
			static_cast<double>(plainA), static_cast<double>(plainB), second.c_str(), static_cast<double>(markA), static_cast<double>(markB));
		Check(plainA >= 0.0f && (plainB < 0.0f || plainA <= plainB), "(a) with no mark the clan takes the first of the two (by name) first");
		Check(markB >= 0.0f && (markA < 0.0f || markB < markA), "(a) with a mark on the second, the second is gathered first");
	}

	// ---- (b) all: the stand of the node; off lifts it
	{
		Yard v;
		const std::vector<std::string> trees = NodesOf(v, "tree");
		Check(trees.size() >= 3, "the patch has trees");
		Place(v, trees.back(), glm::vec2(200.0f, 200.0f));   // one far away: no part of the stand
		const ColonyDesignations::Reply r = v.Tribe_.Designations().DesignateAll(v.Tribe_, trees.front());
		const auto& marks = v.Tribe_.Designations().Marks();
		std::printf("colony_designations: all on %s -> %s (%zu marks of %zu trees)\n", trees.front().c_str(), r.Text.c_str(), marks.size(), trees.size());
		Check(r.Ok && marks.size() >= 2 && marks.size() == trees.size() - 1, "(b) all marks every tree of the stand, the far one left out");
		bool sameKind = true;
		for (const auto& [node, mark] : marks) sameKind = sameKind && ColonyDesignations::KindOf(v.Tribe_, node) == "tree" && mark.Priority == ColonyDesignations::kDefaultPriority;
		Check(sameKind && !v.Tribe_.Designations().MarkOf(trees.back()), "(b) only trees, none the far one");
		Check(!v.Tribe_.Designations().Designate(v.Tribe_, trees.front(), 11).Ok && !v.Tribe_.Designations().Designate(v.Tribe_, "Nothing").Ok, "a bad priority or node is refused");
		Check(v.Tribe_.Designations().Undesignate(v.Tribe_, trees.front(), true).Ok && v.Tribe_.Designations().Marks().empty(), "(b) all off lifts the whole stand");
		Check(v.Tribe_.Designations().Designate(v.Tribe_, trees.front(), 4).Ok && v.Tribe_.Designations().MarkOf(trees.front())->Priority == 4, "a mark with its priority");
		Check(v.Tribe_.Designations().Undesignate(v.Tribe_, trees.front()).Ok && !v.Tribe_.Designations().Undesignate(v.Tribe_, trees.front()).Ok, "off lifts one; twice has nothing to lift");
	}

	// ---- (c) the order of the sites: the first of the queue stands first, and after top on the second, the second
	{
		std::string a, b;
		const auto [inOrderA, inOrderB] = Race(false, &a, &b);
		const auto [swappedA, swappedB] = Race(true, &a, &b);
		std::printf("colony_designations: %s / %s stand at %.1f / %.1f s in order; %.1f / %.1f s after top on %s\n", a.c_str(), b.c_str(),
			static_cast<double>(inOrderA), static_cast<double>(inOrderB), static_cast<double>(swappedA), static_cast<double>(swappedB), b.c_str());
		Check(inOrderA >= 0.0f && (inOrderB < 0.0f || inOrderA < inOrderB), "(c) the first of the queue stands first");
		Check(swappedB >= 0.0f && (swappedA < 0.0f || swappedB < swappedA), "(c) after top on the second, the second stands first");
	}

	// ---- (d) save and load keep the marks and the queue
	{
		Yard v;
		v.Run(1.0f);
		Check(v.Tribe_.ToJson().find("\"designations\"") == std::string::npos, "with no mark nor queue nothing is saved under \"designations\"");
		const std::vector<std::string> rocks = NodesOf(v, "rock");
		std::string a, b;
		Check(!rocks.empty() && v.Tribe_.Designations().Designate(v.Tribe_, rocks.front(), 3).Ok, "a rock is marked with priority 3");
		Check(v.Tribe_.StartSite("workbench", v.Camp + glm::vec2(-9.0f, 6.0f), &a).empty() && v.Tribe_.StartSite("workbench", v.Camp + glm::vec2(9.0f, 6.0f), &b).empty(), "two plans");
		v.Tribe_.Designations().QueueSite(v.Tribe_, a);
		v.Tribe_.Designations().QueueSite(v.Tribe_, b);
		Check(v.Tribe_.Designations().MoveSite(v.Tribe_, b, "up").Ok, "the second goes up");
		const std::string saved = v.Tribe_.ToJson();
		Check(saved.find("\"designations\"") != std::string::npos, "the marks and the queue are saved");
		Yard w;
		w.Run(1.0f);
		Check(w.Tribe_.FromJson(saved, true), "the save loads");
		Check(w.Tribe_.Designations().Marks() == v.Tribe_.Designations().Marks() && w.Tribe_.Designations().MarkOf(rocks.front()) && w.Tribe_.Designations().MarkOf(rocks.front())->Priority == 3,
			"(d) the loaded tribe holds the same marks");
		Check(w.Tribe_.Designations().QueuedSites() == std::vector<std::string>{ b, a }, "(d) and the same queue");
		Check(w.Tribe_.Designations().ToJson() == v.Tribe_.Designations().ToJson(), "(d) and saves them again the same");
		ColonyDesignations bad;
		Check(!bad.FromJson("{\"version\":2}") && !bad.FromJson("{\"version\":1,\"marks\":{\"x\":{}}}") && bad.Empty(), "a bad section is refused and changes nothing");
	}

	// ---- (e) a critical need still wins over the mark
	{
		Yard v;
		v.Tribe_.Store().Add(v.Items, "berry", 20);
		v.Run(2.0f);
		const std::vector<std::string> tufts = NodesOf(v, "fiber_grass");
		Check(!tufts.empty() && v.Tribe_.Designations().Designate(v.Tribe_, tufts.front()).Ok, "a tuft is marked");
		std::string holder;
		for (int i = 0; i < static_cast<int>(30.0f / dt) && holder.empty(); ++i)
		{
			v.Step();
			for (const TribeMember& m : v.Tribe_.Members())
				if (const Job* j = v.Tribe_.Board().ClaimOf(m.Name); j && j->Kind == "designate") holder = m.Name;
		}
		Check(!holder.empty(), "(e) someone holds the job of the mark");
		if (!holder.empty())
		{
			v.Need(holder, NeedName::Hunger, 8.0f);
			const int meals = v.Member(holder).Meals;
			bool ate = false;
			for (int i = 0; i < static_cast<int>(60.0f / dt) && !ate; ++i)
			{
				v.Step();
				ate = v.Member(holder).Job == "eat" || v.Member(holder).Meals > meals;
			}
			std::printf("colony_designations: %s holds the mark; with hunger 8 it ate: %d\n", holder.c_str(), ate ? 1 : 0);
			Check(ate, "(e) the one holding the mark, critically hungry, goes to eat first");
		}
	}

	if (failures == 0) std::printf("colony_designations: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
