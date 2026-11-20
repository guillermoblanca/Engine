// T367 (documentation/01_diseno/TALLERES.md section 10): the yards' data and the houses' state. Checked: the closures and sizes of yards.json read
// (by level, in their order) and a mistake is refused; the "yards" section of a clan (size, closure, the work in course with what it was brought)
// goes out and comes back the same, a wrong version is refused; the modules' section of the prototype still loads. And the works without `now`: a clan
// of four with a hut and the materials in its store closes its yard (fence) and grows the hut to M by delivering and working, in a short run. CPU only.
#include "GB/Game/HouseModules.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/Yard.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Log.h"
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

using namespace GB;

// File-local copies of settlement_test's body and clan (T51: one executable, one definition per name)
namespace
{
class YardBody : public ITribeBody
{
public:
	YardBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
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
	void Update(float dt)
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
		if (s.Seconds > 0.0f) { s.Seconds -= dt; if (s.Seconds > 0.0f) return; m_Pos = m_Target; }
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

// Four members by their camp with every building known (no technologies set) and the planner asleep: only the works asked for move.
struct YardClan
{
	YardClan()
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		RegisterBuildingContent(Gameplay.Library);
		const char* names[] = { "Ana", "Biel", "Cleo", "Dan" };
		for (const char* n : names)
		{
			Entity& e = *Entities.Find(Entities.Create(n));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "Build" };
		}
		Gameplay.Load(Entities);
		const glm::vec2 camp(0.0f, -8.0f);
		for (const char* n : names)
		{
			bodies.push_back(std::make_unique<YardBody>(n, Gameplay, Entities, camp));
			bodies.back()->m_Items = &Items;
		}
		Builds.Bind(Gameplay);
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		Tribe_.SetBuildings(&Builds.Defs);
		Tribe_.Config.Economy.Camp = camp;
		Tribe_.Config.PlanSeconds = 1.0e9f;
		for (const char* item : { "wood", "branch", "cordage", "stone", "daub", "reed", "fiber", "stake", "nuts", "cooked_meat", "water" }) Tribe_.Store().Add(Items, item, 60);   // (food too: a hungry clan does not build)
		for (size_t i = 0; i < 4; ++i) Tribe_.AddMember(names[i], "forager", bodies[i].get());
	}
	void Step(float dt)
	{
		Gameplay.Update(dt);
		for (auto& b : bodies) b->Update(dt);
		Builds.Update(dt, &Entities);
		Tribe_.Update(dt);
	}
	template <class Done> bool Run(float seconds, Done done)
	{
		for (float t = 0.0f; t < seconds; t += 0.25f) { Step(0.25f); if (done()) return true; }
		return false;
	}
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	BuildingSystem Builds;
	Tribe Tribe_;
	std::vector<std::unique_ptr<YardBody>> bodies;
};

void Tail(const Tribe& t, size_t n = 12)
{
	const auto& log = t.Log();
	for (size_t i = log.size() > n ? log.size() - n : 0; i < log.size(); ++i) std::fprintf(stderr, "  %s\n", log[i].c_str());
}
}

int GBTest_yards(int, char**)
{
	Log::Init();
	{
		YardRules r;
		const std::string error = LoadYardRules(R"({ "yards": {
			"closures": [ { "id": "wicker", "level": 2, "needs": { "branch": 6 }, "defense": 0.03 }, { "id": "fence", "level": 1, "needs": { "branch": 3 } } ],
			"sizes": { "hut": { "L": { "order": 3, "building": "longhouse" }, "S": { "order": 1 }, "M": { "order": 2, "shelter": 6, "footprint": [ 5.2, 3.8 ] } } },
			"rules": { "defenseCap": 0.4 } } })", r);
		Check(error.empty(), "the yards' data reads");
		Check(r.Closures.size() == 2 && r.Closures[0].Id == "fence" && r.Closure(2) && r.Closure(2)->Id == "wicker", "the closures go by level");
		Check(r.Sizes.count("hut") && r.Sizes["hut"].size() == 3 && r.Sizes["hut"][0].Id == "S" && r.Sizes["hut"][2].Building == "longhouse", "the sizes go in their order");
		Check(r.Sizes["hut"][1].Shelter == 6 && r.DefenseCap > 0.39f && r.DefenseCap < 0.41f, "a size's values and the rules");
		YardRules bad;
		Check(!LoadYardRules(R"({ "yards": { "closures": [ { "id": "a", "level": 1 }, { "id": "b", "level": 1 } ] } })", bad).empty(), "two closures of one level are refused");
	}
	{
		HouseModules a;
		const std::string json = R"({ "version": 1, "houses": { "Wattle hut 1": { "size": "M", "closure": 2,
			"work": { "kind": "grow", "to": "L", "needs": { "wood": 7, "stone": 2 }, "substitutes": { "stone": [ "granite" ] }, "got": { "wood": 3 }, "done": 4.5, "needed": 15 } } } })";
		Check(a.HousesFromJson(json), "the houses' section loads");
		Check(a.ClosureOf("Wattle hut 1") == 2 && a.WorkOn("Wattle hut 1") && a.WorkOn("Wattle hut 1")->To == "L", "its closure and its work");
		Check(a.WorkOn("Wattle hut 1")->Missing().at("wood") == 4, "what the work still lacks");
		HouseModules b;
		Check(b.HousesFromJson(a.HousesToJson()) && b.HousesToJson() == a.HousesToJson(), "and goes out and comes back the same");
		Check(!b.HousesFromJson(R"({ "version": 9, "houses": {} })"), "a version it does not know is refused");
		HouseModules c;
		Check(c.FromJson(R"({ "Quern 1": { "host": "Wattle hut 1", "slot": "wall_W0" } })") && c.Of("Quern 1") && c.Of("Quern 1")->Slot == "wall_W0", "the prototype's modules still load");
	}
	// ---- the works without `now`: delivered from the store, worked by the members, completed
	{
		YardClan c;
		std::string hut;
		const std::string raised = c.Tribe_.RaiseBuilding("hut", { -14.0f, -18.0f }, 0.0f, &hut);
		if (!raised.empty()) std::fprintf(stderr, "  raise: %s\n", raised.c_str());
		Check(raised.empty() && !hut.empty(), "a standing hut");
		if (!hut.empty())
		{
			HouseModules& m = c.Tribe_.Modules();
			const HouseModules::Reply close = m.SetClosure(c.Tribe_, hut, "fence");
			if (!close.Ok) std::fprintf(stderr, "  closure: %s\n", close.Text.c_str());
			Check(close.Ok && m.ClosureOf(hut) == 0 && m.WorkOn(hut), "a closure without now is a work, not done yet");
			const bool closed = c.Run(600.0f, [&] { return m.ClosureOf(hut) == 1; });
			if (!closed) Tail(c.Tribe_);
			Check(closed && !m.WorkOn(hut), "the members bring the fence's materials, work it and the yard closes");
			const HouseModules::Reply grow = m.Grow(c.Tribe_, hut, "M");
			if (!grow.Ok) std::fprintf(stderr, "  grow: %s\n", grow.Text.c_str());
			Check(grow.Ok && m.SizeOf(c.Tribe_, hut) == "S", "a growth without now is a work, not done yet");
			const bool grown = c.Run(900.0f, [&] { return m.SizeOf(c.Tribe_, hut) == "M"; });
			if (!grown) Tail(c.Tribe_);
			Check(grown && !m.WorkOn(hut) && m.ClosureOf(hut) == 1, "and the hut grows to M, keeping its closure");
		}
	}
	if (failures) std::fprintf(stderr, "%d checks failed\n", failures);
	else std::printf("yards test passed\n");
	return failures ? 1 : 0;
}
