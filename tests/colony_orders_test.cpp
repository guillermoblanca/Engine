// T268 (C1-a, documentation/01_diseno/MODO_COLONIA.md): direct orders to any member of a clan, the mandate and what cuts it
// (GB/Game/ColonyOrders.h). Checked, on a clan with no player and stand-in bodies (as clan_stores_test):
//   (a) `goto` takes a member that is not the player to within 1.5 m of the point, and then it goes back to its own life;
//   (b) `use <node>` makes it gather from that node: the node goes down and its bag or the store goes up;
//   (c) while it holds the order its brain does not give it other work (no job of the board, no other job) though the clan is short of food;
//   (d) with hunger forced under the cut, the order is cut, a notice and a line say why, and it goes to eat; someone that hungry refuses one;
//   (e) saving and loading with an order in course keeps it, and the member carries it out after the load; without orders nothing is saved;
//   (f) `cancel` lets it go; and `build <site>` carries the materials and works on the site.
#include "GB/Log.h"
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
class OrderBody : public ITribeBody
{
public:
	OrderBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
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
struct Village
{
	Village()
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
			bodies.push_back(std::make_unique<OrderBody>(n, Gameplay, Entities, Camp));
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
	OrderBody& Body(const std::string& name) { for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == name) return *bodies[i]; return *bodies[0]; }
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
	std::vector<std::unique_ptr<OrderBody>> bodies;
};
}

int GBTest_colony_orders(int, char**)
{
	Log::Init();

	// ---- (a) goto: a member that is not the player walks to within 1.5 m of the point, then goes back to its own life
	{
		Village v;
		v.Run(5.0f);
		const glm::vec2 to(4.0f, 1.0f);
		Check(v.Tribe_.Colony().Empty() && v.Tribe_.ToJson().find("\"mandates\"") == std::string::npos, "with no order nothing is saved under \"orders\"");
		const ColonyOrders::Reply r = v.Order("Biel", "goto", { "4", "1" });
		Check(r.Ok && v.Tribe_.Colony().Has("Biel") && v.Member("Biel").Job == "goto", "goto is taken: Biel holds the order and walks");
		Check(v.Tribe_.Colony().Line().find("Biel goto 4.0 1.0") != std::string::npos, "the line of the orders names it");
		for (int i = 0; i < static_cast<int>(30.0f / dt) && v.Tribe_.Colony().Has("Biel"); ++i) v.Step();
		Check(glm::distance(v.Body("Biel").Position(), to) < 1.5f, "(a) goto takes Biel to within 1.5 m of the point");
		Check(!v.Tribe_.Colony().Has("Biel") && v.Logged("Biel (woodcutter) has done the order: go to 4.0, 1.0"), "and the order is done (a line says so)");
		Check(v.Member("Biel").Job != "goto", "then it goes back to its own life");
		Check(!v.Order("Biel", "goto", { "x" }).Ok && !v.Order("Nobody", "goto", { "1", "1" }).Ok && !v.Order("Biel", "dance").Ok, "a bad point, member or verb is refused");
	}

	// ---- (b) + (c) use <node>: it gathers that node; meanwhile nothing else is given to it, though the clan has no food at all
	{
		Village v;
		v.Run(2.0f);
		const std::string node = v.NodeOf("fiber_grass");
		Check(!node.empty(), "the patch has fiber grass");
		const int before = v.Amount(node);
		const int had = v.Body("Cleo").Bag().Count("fiber") + v.Tribe_.Store().Count("fiber");
		const ColonyOrders::Reply r = v.Order("Cleo", "use", { node });
		Check(r.Ok && v.Member("Cleo").Job == "gather" && v.Member("Cleo").Detail == node, "use is taken: Cleo goes to gather that node");
		bool onlyOrder = true, noBoard = true, jobs = false;
		int least = before;
		for (int i = 0; i < static_cast<int>(120.0f / dt) && v.Tribe_.Colony().Has("Cleo"); ++i)
		{
			v.Step();
			if (!v.Tribe_.Colony().Has("Cleo")) break;   // (the step that ended it: it decides for itself at once)
			const std::string& job = v.Member("Cleo").Job;
			if (onlyOrder && !(job == "gather" || job == "deposit")) std::printf("colony_orders: at %d Cleo's job is '%s'\n", i, job.c_str());
			if (noBoard && v.Tribe_.Board().ClaimOf("Cleo")) std::printf("colony_orders: at %d Cleo holds %s\n", i, v.Tribe_.Board().ClaimOf("Cleo")->Id.c_str());
			onlyOrder = onlyOrder && (job == "gather" || job == "deposit");
			noBoard = noBoard && v.Tribe_.Board().ClaimOf("Cleo") == nullptr;
			jobs = jobs || !v.Tribe_.Board().Jobs().empty();
			least = std::min(least, v.Amount(node));
		}
		const int has = v.Body("Cleo").Bag().Count("fiber") + v.Tribe_.Store().Count("fiber");
		std::printf("colony_orders: %s %d -> least %d; Cleo's fiber (bag + store) %d -> %d\n", node.c_str(), before, least, had, has);
		Check(least < before && has > had, "(b) use: the node goes down and Cleo's bag or the store goes up");
		Check(onlyOrder, "(c) while it holds the order it only gathers and carries for it (the brain gives it nothing else)");
		Check(noBoard, "(c) and the board gives it no job");
		Check(jobs, "(c) though the board has work posted for the clan");
		Check(!v.Tribe_.Colony().Has("Cleo") && v.Logged("Cleo (miner) has done the order: use " + node), "the node spent, what it gathered taken to the store, the order is done");
	}

	// ---- (d) hunger cuts the order, says why, and it goes to eat; someone that hungry refuses one
	{
		Village v;
		v.Tribe_.Store().Add(v.Items, "berry", 10);
		v.Run(2.0f);
		Check(v.Order("Dan", "goto", { "30", "30" }).Ok, "Dan is sent far away");
		v.Run(2.0f);
		v.Need("Dan", NeedName::Hunger, 10.0f);
		v.Run(3.0f);
		const auto& notices = v.Tribe_.Colony().Notices();
		Check(!v.Tribe_.Colony().Has("Dan"), "(d) hunger cuts the order");
		Check(!notices.empty() && notices.back().Member == "Dan" && notices.back().Reason == "very hungry", "(d) a notice says who and why (very hungry)");
		Check(v.Logged("Dan (forager) drops the order: very hungry"), "(d) and a line of the log");
		bool ate = false;
		for (int i = 0; i < static_cast<int>(60.0f / dt) && !ate; ++i) { v.Step(); ate = v.Member("Dan").Job == "eat" || v.Member("Dan").Meals > 0; }
		Check(ate, "(d) it goes to eat");
		v.Need("Ana", NeedName::Thirst, 5.0f);
		const ColonyOrders::Reply no = v.Order("Ana", "goto", { "10", "10" });
		Check(!no.Ok && no.Text.find("refuses the order: very thirsty") != std::string::npos && !v.Tribe_.Colony().Has("Ana"), "someone that thirsty refuses an order, with why");
	}

	// ---- (e) save and load with an order in course; (f) cancel; and build
	{
		Village v;
		v.Run(2.0f);
		Check(v.Order("Cleo", "goto", { "-20", "15" }).Ok, "Cleo is sent away");
		v.Run(1.0f);
		const std::string saved = v.Tribe_.ToJson();
		Check(saved.find("\"mandates\"") != std::string::npos, "an order in course is saved");
		Village w;
		w.Run(1.0f);
		w.Body("Cleo").Teleport(v.Body("Cleo").Position());   // (the body where the run had it: the scene's part of a save)
		Check(w.Tribe_.FromJson(saved, true), "the save loads");
		const ColonyOrders::Mandate* m = w.Tribe_.Colony().Of("Cleo");
		Check(m && v.Tribe_.Colony().Of("Cleo") && *m == *v.Tribe_.Colony().Of("Cleo"), "(e) the loaded tribe holds the same order");
		Check(w.Tribe_.Colony().ToJson() == v.Tribe_.Colony().ToJson() && w.Tribe_.ToJson().find("\"mandates\"") != std::string::npos, "(e) and saves it again the same");
		for (int i = 0; i < static_cast<int>(30.0f / dt) && w.Tribe_.Colony().Has("Cleo"); ++i) w.Step();
		Check(!w.Tribe_.Colony().Has("Cleo") && glm::distance(w.Body("Cleo").Position(), glm::vec2(-20.0f, 15.0f)) < 1.5f, "(e) after the load it carries the order out");

		// (f) cancel
		Check(v.Tribe_.Colony().Cancel(v.Tribe_, "Cleo").Ok && !v.Tribe_.Colony().Has("Cleo"), "(f) cancel lets it go");
		Check(v.Logged("Cleo (miner) is let off the order"), "(f) a line says so");
		Check(v.Member("Cleo").Job != "goto", "(f) and it decides for itself again");
		Check(!v.Tribe_.Colony().Cancel(v.Tribe_, "Cleo").Ok, "a second cancel has nothing to cancel");
		v.Run(1.0f);
		Check(v.Tribe_.ToJson().find("\"mandates\"") == std::string::npos, "with no order left nothing is saved under \"orders\"");

		// build: the site's materials are carried and it is worked on
		std::string site;
		Check(v.Tribe_.StartSite("workbench", v.Camp + glm::vec2(-10.0f, 4.0f), &site).empty() && !site.empty(), "a workbench site is started");
		const ColonyOrders::Reply b = v.Order("Ana", "build", { site });
		Check(b.Ok, "build is taken");
		bool worked = false;
		for (int i = 0; i < static_cast<int>(400.0f / dt) && v.Tribe_.Colony().Has("Ana"); ++i) v.Step();
		worked = v.Logged("Ana (forager) carries") && v.Logged("Ana (forager) builds the");
		const AbilitySystemComponent* a = v.Gameplay.Find(site);
		const BuildingDef* def = a ? BuildingOf(*a, v.Builds.Defs) : nullptr;
		Check(worked && def && ReadSite(*a, *def).Progress > 0.0f, "build: Ana carries the materials and works on the site");
	}

	if (failures == 0) std::printf("colony_orders: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
