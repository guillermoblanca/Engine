// T323 (documentation/01_diseno/ARBOL_TECNICO.md §11.4, §11.7.5): the steps of the tree (GB/Game/TechTiers.h). Checked:
//   (a) the data: tier and branch read and written back (TechsToJson), a tier without a branch, a branch out of the list and steps out of order
//       refused; the shipped tree has a tier and a branch on every technology and four steps;
//   (b) a clan of four in step 1 (a tree in memory: alpha step 1, beta step 2, gamma step 3) does not invent beta: no experiment chooses it, a
//       long run does not find it, the research queue says "needs tier 2", and handling a material that leads to gamma teaches nothing;
//   (c) with a longhouse raised the next plan opens step 2 (said in the log) and the clan works beta out;
//   (d) learning from someone of another clan (ClanKnowledge::Instruct, as an envoy or trade does) skips the step: gamma in step 2;
//   (e) the open step is saved and loaded (an older save without it: 0); a tree without steps holds nothing back (99).
#include "GB/Log.h"
#include "GB/Game/ResearchQueue.h"
#include "GB/Game/TechTiers.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/UseLearning.h"
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
		for (const auto& [id, tier] : std::vector<std::pair<std::string, int>>{ { "alpha", 1 }, { "beta", 2 }, { "gamma", 3 } })
		{
			TechDef t = Idea(id);
			t.Tier = tier;
			t.Branch = "lore";
			t.Discovery.Teach = 0.5f;
			Tree.Techs[t.Id] = t;
		}
		TierStep one; one.Tier = 1; one.Goals = { TierGoal{ "population", "", 1 } };
		TierStep two; two.Tier = 2; two.Goals = { TierGoal{ "building", "longhouse", 1 } };
		TierStep three; three.Tier = 3; three.Goals = { TierGoal{ "seasons", "", 1 } };
		Tree.Rules.Tiers = { one, two, three };
		Tree.Rules.Use.Materials["stone"]["gamma"] = 1.0f;   // (handling stone would work gamma out at once, were its step open)
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
		Tribe_.Store().Add(Items, "nuts", 200);   // T360: food for days that lasts them (200 berries are the day and a half they keep)
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
	bool InLog(const Tribe& t, const std::string& part)
	{
		return std::any_of(t.Log().begin(), t.Log().end(), [&](const std::string& l) { return Has(l, part); });
	}
	void Run(Camp& c, float seconds) { for (int i = 0; i < static_cast<int>(seconds / dt); ++i) c.Step(); }
}

int GBTest_tech_tiers(int, char**)
{
	Log::Init();

	// ---- (a) the data
	{
		TechTree tree;
		const std::string ok = R"({ "techs": [ { "id": "a", "tier": 2, "branch": "field", "requires": [], "recipes": [] }, { "id": "b", "requires": [], "recipes": [] } ],
			"knowledge": { "tiers": [ { "tier": 1, "goals": [ { "type": "fire" } ] }, { "tier": 2, "need": 1, "goals": [ { "type": "building", "building": "longhouse" }, { "type": "population", "count": 20 } ] } ] } })";
		Check(LoadTechs(ok, tree).empty(), "(a) a tree with tiers and branches loads");
		Check(tree.Find("a")->Tier == 2 && tree.Find("a")->Branch == "field" && tree.Find("b")->Tier == -1 && tree.Find("b")->TierOrZero() == 0, "(a) tier and branch are read");
		Check(tree.Rules.Tiers.size() == 2 && tree.Rules.Tiers[1].Need == 1 && tree.Rules.Tiers[1].Goals[0].Building == "longhouse" && tree.Rules.Tiers[1].Goals[1].Count == 20, "(a) the steps are read");
		TechTree again;
		const std::string written = TechsToJson(tree);
		Check(Has(written, "\"tier\": 2") && Has(written, "\"branch\": \"field\"") && Has(written, "\"tiers\""), "(a) TechsToJson writes tier, branch and the steps");
		Check(LoadTechs(written, again).empty() && again == tree, "(a) and reads them back the same");
		TechTree bad;
		Check(!LoadTechs(R"({ "techs": [ { "id": "a", "tier": 1, "requires": [], "recipes": [] } ] })", bad).empty(), "(a) a tier without a branch is refused");
		Check(!LoadTechs(R"({ "techs": [ { "id": "a", "tier": 1, "branch": "magic", "requires": [], "recipes": [] } ] })", bad).empty(), "(a) a branch out of the list is refused");
		Check(!LoadTechs(R"({ "techs": [ { "id": "a", "tier": 7, "branch": "lore", "requires": [], "recipes": [] } ] })", bad).empty(), "(a) a tier above 4 is refused");
		Check(!LoadTechs(R"({ "knowledge": { "tiers": [ { "tier": 2, "goals": [ { "type": "fire" } ] } ] } })", bad).empty(), "(a) steps out of order are refused");
		Check(!LoadTechs(R"({ "knowledge": { "tiers": [ { "tier": 1, "goals": [ { "type": "luck" } ] } ] } })", bad).empty(), "(a) an unknown goal type is refused");
		const TechTree shipped = DefaultTechs();
		int counts[kMaxTier + 1] = {};
		bool all = true;
		for (const auto& [id, t] : shipped.Techs)
		{
			if (t.Tier < 0) { std::printf("  %s (a later era) has no tier: it waits for the last step\n", id.c_str()); continue; }
			if (t.Branch.empty()) { all = false; std::fprintf(stderr, "  %s has a tier and no branch\n", id.c_str()); }
			++counts[t.Tier];
		}
		std::printf("(a) shipped: %zu techs; by tier 0..4: %d %d %d %d %d; %zu steps\n", shipped.Techs.size(), counts[0], counts[1], counts[2], counts[3], counts[4], shipped.Rules.Tiers.size());
		Check(all, "(a) every shipped technology with a tier has a branch");
		Check(shipped.Rules.Tiers.size() == 4, "(a) the shipped knowledge has four steps");
	}

	// ---- (b) step 1: beta (step 2) is not invented
	{
		Camp c;
		Tribe& t = c.Tribe_;
		TechTiers::Refresh(t);
		Check(TechTiers::Open(t) == 1, "(b) the clan opens step 1 (people)");
		std::printf("(b) %s\n", TechTiers::Text(t).c_str());
		Check(InLog(t, "opens tier 1"), "(b) the log says so");
		Check(TechTiers::Missing(t, "alpha") == 0 && TechTiers::Missing(t, "beta") == 2 && TechTiers::Missing(t, "gamma") == 3, "(b) Missing: alpha nothing, beta step 2, gamma step 3");
		Check(t.Knowledge().GrantClan("alpha").empty(), "(b) the clan is taught alpha");
		Check(!t.Knowledge().CanInvent("Ana", "beta") && c.Next("Ana").empty(), "(b) nobody would experiment with beta");
		ResearchQueue::Reply r = t.Research().Add(t, "beta");
		Check(r.Ok && Has(r.Text, "needs tier 2") && t.Research().Current(t).empty(), "(b) the research queue keeps beta waiting: needs tier 2");
		t.Research().Clear(t);
		const std::vector<KnowledgeEvent> used = UseLearning::Handled(t, t.Members()[0], { "stone" });
		Check(used.empty() && !t.Knowledge().Knows(t.Members()[0].Name, "gamma"), "(b) handling stone does not work gamma out (its step is closed)");
		Run(c, 900.0f);
		Check(!t.Knowledge().Clan().count("beta") && !t.Knowledge().Clan().count("gamma"), "(b) in 900 s the clan finds neither beta nor gamma");

		// ---- (c) the longhouse opens step 2
		Check(t.RaiseBuilding("longhouse", { 14.0f, -20.0f }).empty(), "(c) a longhouse is raised");
		Check(TechTiers::Refresh(t) && TechTiers::Open(t) == 2 && InLog(t, "opens tier 2"), "(c) the next plan opens step 2");
		Check(TechTiers::Missing(t, "beta") == 0 && c.Next("Ana") == "beta", "(c) beta is open: the next experiment");
		r = t.Research().Add(t, "beta");   // (the clan's next step: as in step 1, where the queue kept it waiting)
		Check(r.Ok && t.Research().Current(t) == "beta", "(c) the research queue now researches beta");
		Run(c, 900.0f);
		Check(t.Knowledge().Clan().count("beta") == 1, "(c) the clan works beta out");
		Check(!t.Knowledge().Clan().count("gamma") && TechTiers::Open(t) == 2, "(c) gamma (step 3) still not");

		// ---- (d) another clan teaches gamma: the step is skipped
		const std::string learner = t.Members()[1].Name;
		const std::vector<KnowledgeEvent> taught = t.Knowledge().Instruct(learner, "gamma", 1.0f, "from an envoy of the Elk");
		Check(t.Knowledge().Knows(learner, "gamma") && !taught.empty() && !taught.back().Tech.empty(), "(d) an envoy of another clan teaches gamma in step 2");

		// ---- (e) save and load (the clan's knowledge section, ClanKnowledge::ToJson: the editor's "knowledge-clan" and the clan world's "knowledge")
		const std::string saved = t.Knowledge().ToJson();
		Check(Has(saved, "\"tier\":2"), "(e) the open step is saved");
		Camp w;
		Check(w.Tribe_.Knowledge().FromJson(saved) && TechTiers::Open(w.Tribe_) == 2, "(e) and loads: step 2 (no longhouse in this camp)");
		Camp old;
		old.Tribe_.Knowledge().RaiseTier(3);
		Check(old.Tribe_.Knowledge().FromJson(R"({"known":[],"lesson":0})") && TechTiers::Open(old.Tribe_) == 0, "(e) a save without it: step 0");
		TechTiers::Refresh(old.Tribe_);
		Check(TechTiers::Open(old.Tribe_) == 1, "(e) and the next plan opens again what the goals reach (step 1)");
		Camp none;
		none.Tree.Rules.Tiers.clear();
		Check(TechTiers::Open(none.Tribe_) == kNoTierGate && TechTiers::Missing(none.Tribe_, "gamma") == 0 && none.Tribe_.Knowledge().CanInvent("Ana", "gamma"), "(e) a tree without steps holds nothing back");
		Camp later;
		later.Tree.Techs["gamma"].Tier = -1;   // (a technology with no tier in a tree with steps: a later era)
		Check(later.Tribe_.Knowledge().TierOf("gamma") == kMaxTier && TechTiers::Missing(later.Tribe_, "gamma") == kMaxTier, "(e) a technology with no tier waits for the last step");
		Check(TechTiers::Text(none.Tribe_) == "no tiers", "(e) Text says so");
	}

	if (failures == 0) std::printf("tech_tiers: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
