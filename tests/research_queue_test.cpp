// T324 (documentation/01_diseno/ARBOL_TECNICO.md §11.2.4, §11.7.6): the player's research queue (GB/Game/ResearchQueue.h). Checked, on a clan
// of four with stand-in bodies (as colony_priorities_test) and a tree in memory of four ideas (alpha, beta, gamma; delta requires gamma):
//   (a) refused: a technology the tree has not, one the clan knows, one already queued;
//   (b) the clan's next steps (ClanKnowledge::Goals, which the experiments read) start with the researchable entries in the queue's order; one
//       whose prerequisite is unknown waits; every member's next experiment is the first of them; top and remove reorder it;
//   (c) a learnt entry leaves the queue at the next plan and the next one leads;
//   (d) run: with the queue [beta, delta] the clan works out beta before delta (by itself it takes delta first: the readiest, T181);
//   (e) saving and loading keeps the queue; with none nothing is saved; a bad section is refused.
#include "GB/Log.h"
#include "GB/Game/ResearchQueue.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Techs.h"
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

TechDef Idea(const std::string& id, std::vector<std::string> needs = {})
{
	TechDef t;
	t.Id = id;
	t.Name = id;
	t.Requires = std::move(needs);
	t.Complexity = 0.0f;               // (anyone can work it out)
	t.Discovery.Experiment = 1.0f;     // (one try, with what it requires known)
	t.Discovery.Seconds = 3.0f;
	return t;
}

// A clan of four with a patch of resource nodes and a well, no player among them, plenty of food; the tree of four ideas.
struct Camp
{
	Camp()
	{
		for (const TechDef& t : { Idea("alpha"), Idea("beta"), Idea("gamma"), Idea("delta", { "gamma" }) }) Tree.Techs[t.Id] = t;
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
		Tribe_.SetTechs(&Tree);
		Tribe_.Config.Economy.Camp = Home;
		Tribe_.Config.Simulation.LogLines = 1000000;   // (the whole log is read)
		Tribe_.Config.PlanSeconds = 5.0f;              // (the clan's next steps are set again often)
		Tribe_.Store().Add(Items, "wood", 10);
		Tribe_.Store().Add(Items, "stone", 12);
		Tribe_.Store().Add(Items, "berry", 200);
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
	// what a member would experiment with now (with time to spare and nothing in its bag)
	std::string Next(const std::string& who) const
	{
		Inventory bag(12, 0.0f);
		return Tribe_.Knowledge().NextExperiment(who, 1e9, Tribe_.Store(), bag, Recipes);
	}
	bool AllNext(const std::string& tech) const
	{
		for (const char* n : { "Ana", "Biel", "Cleo", "Dan" })
			if (Next(n) != tech) { std::fprintf(stderr, "  %s would try '%s', not %s\n", n, Next(n).c_str(), tech.c_str()); return false; }
		return true;
	}
	void Plan() { Tribe_.SetAim("", ""); }   // (sets the clan's next steps again now, as the plan does every PlanSeconds)
	const glm::vec2 Home{ 0.0f, -8.0f };
	TechTree Tree;
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

	bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
	std::vector<std::string> Goals(const Tribe& t) { return t.Knowledge().Goals(); }
	using List = std::vector<std::string>;
}

int GBTest_research_queue(int, char**)
{
	Log::Init();

	// ---- (a) what is refused
	{
		Camp c;
		Tribe& t = c.Tribe_;
		ResearchQueue& q = t.Research();
		Check(q.Empty() && c.AllNext("alpha"), "(a) with no queue the experiments go as before (the first by id)");
		Check(t.Knowledge().GrantClan("alpha").empty(), "(a) the clan is taught alpha");
		ResearchQueue::Reply r = q.Add(t, "omega");
		Check(!r.Ok && Has(r.Text, "no technology 'omega'"), "(a) an unknown technology is refused");
		r = q.Add(t, "alpha");
		Check(!r.Ok && Has(r.Text, "already knows"), "(a) a known technology is refused");
		Check(q.Add(t, "beta").Ok && !q.Add(t, "beta").Ok, "(a) one already queued is refused");
		Check(!q.Remove(t, "gamma").Ok && !q.Top(t, "omega").Ok, "(a) remove and top refuse what is not there");
	}

	// ---- (b) the order
	{
		Camp c;
		Tribe& t = c.Tribe_;
		ResearchQueue& q = t.Research();
		Check(q.Add(t, "delta").Ok && q.Add(t, "gamma").Ok && q.Add(t, "beta").Ok, "(b) three entries are added");
		Check(q.All() == List{ "delta", "gamma", "beta" }, "(b) in the order they were added");
		Check(q.Current(t) == "gamma", "(b) delta waits for gamma: gamma leads");
		Check(Goals(t) == List{ "gamma", "beta" }, "(b) the clan's next steps are the researchable entries, in order");
		Check(c.AllNext("gamma"), "(b) every member's next experiment is gamma");
		const std::string list = q.List(t);
		std::printf("%s\n", list.c_str());
		Check(Has(list, "1. delta 0% waits for gamma") && Has(list, "2. gamma 0% researching") && Has(list, "3. beta 0% next") && Has(list, "can research: alpha;"), "(b) game research lists the queue");
		Check(!Has(list, "delta 0% next") && Has(list, "? 1 more"), "(b) what it cannot research yet is not named among what it can");
		Check(q.Top(t, "beta").Ok && q.All() == List{ "beta", "delta", "gamma" } && c.AllNext("beta"), "(b) top puts beta first, and they try it");
		Check(q.Remove(t, "beta").Ok && q.All() == List{ "delta", "gamma" } && c.AllNext("gamma"), "(b) remove takes it out: gamma again");
		Check(q.Top(t, "alpha").Ok && q.All().front() == "alpha" && c.AllNext("alpha"), "(b) top of one not queued adds it first");
		// T339 (K-c): move to a place (1-based, clamped); the list's text does not change
		Check(q.Move(t, "alpha", 3).Ok && q.All() == List{ "delta", "gamma", "alpha" } && c.AllNext("gamma"), "(b) move alpha to 3: gamma leads again");
		Check(q.Move(t, "alpha", 1).Ok && q.All() == List{ "alpha", "delta", "gamma" } && c.AllNext("alpha"), "(b) move alpha back to 1");
		Check(q.Move(t, "gamma", 99).Ok && q.All() == List{ "alpha", "delta", "gamma" } && !q.Move(t, "beta", 1).Ok, "(b) a place past the end is the last; one not queued is refused");

		// ---- (c) a learnt entry leaves
		Check(t.Knowledge().GrantClan("alpha").empty() && t.Knowledge().GrantClan("gamma").empty(), "(c) the clan learns alpha and gamma");
		c.Plan();
		Check(q.All() == List{ "delta" } && q.Current(t) == "delta" && !Goals(t).empty() && Goals(t).front() == "delta" && c.AllNext("delta"), "(c) they leave the queue and delta leads");
		Check(q.Clear(t).Ok && q.Empty() && Goals(t).empty() && c.AllNext("delta"), "(c) clear: the clan chooses as before (delta, the readiest)");
	}

	// ---- (d) a run: the clan works out the queue's first, not the one it would choose by itself
	{
		Camp c;
		Tribe& t = c.Tribe_;
		Check(t.Knowledge().GrantClan("alpha").empty() && t.Knowledge().GrantClan("gamma").empty(), "(d) the clan knows alpha and gamma");
		Check(t.Research().Add(t, "beta").Ok && t.Research().Add(t, "delta").Ok, "(d) the queue is beta, delta");
		List order;
		int steps = 0;
		for (; steps < static_cast<int>(900.0f / dt) && order.size() < 2; ++steps)
		{
			c.Step();
			for (const char* tech : { "beta", "delta" })
				if (t.Knowledge().Clan().count(tech) && std::find(order.begin(), order.end(), tech) == order.end()) order.push_back(tech);
		}
		for (int i = 0; i < static_cast<int>(10.0f / dt); ++i) c.Step();   // (the next plan)
		std::printf("(d) learnt %zu of 2 in %.0f s:", order.size(), static_cast<double>(steps) * static_cast<double>(dt));
		for (const std::string& o : order) std::printf(" %s", o.c_str());
		std::printf("\n");
		Check(!order.empty() && order.front() == "beta", "(d) the clan works out beta first");
		Check(order.size() == 2 && order[1] == "delta", "(d) then delta, the next of the queue");
		Check(t.Research().Empty(), "(d) what it learnt leaves the queue");
	}

	// ---- (e) save and load
	{
		Camp v;
		Check(v.Tribe_.ToJson().find("\"research\"") == std::string::npos, "(e) with no queue nothing is saved");
		v.Tribe_.Research().Add(v.Tribe_, "delta");
		v.Tribe_.Research().Add(v.Tribe_, "beta");
		const std::string saved = v.Tribe_.ToJson();
		Check(saved.find("\"research\"") != std::string::npos, "(e) the queue is saved");
		Camp w;
		Check(w.Tribe_.FromJson(saved, true), "(e) the save loads");
		Check(w.Tribe_.Research().All() == List{ "delta", "beta" }, "(e) the same queue, in its order");
		Check(w.Tribe_.Research().ToJson() == v.Tribe_.Research().ToJson(), "(e) and saves it again the same");
		Camp x;
		x.Tribe_.Research().Add(x.Tribe_, "alpha");
		v.Tribe_.Research().Clear(v.Tribe_);
		Check(x.Tribe_.FromJson(v.Tribe_.ToJson(), true) && x.Tribe_.Research().Empty(), "(e) a save with no queue (an older one) leaves it empty");
		ResearchQueue bad;
		Check(!bad.FromJson("{\"version\":2,\"queue\":[]}") && !bad.FromJson("{\"version\":1,\"queue\":[\"a\",\"a\"]}") && !bad.FromJson("{\"version\":1,\"queue\":[3]}"),
			"(e) a bad section is refused");
	}

	if (failures == 0) std::printf("research_queue: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
