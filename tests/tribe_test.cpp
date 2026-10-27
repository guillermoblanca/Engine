// The first tribe (H2 + H3): four workers with no one steering them gather, carry to the store, make their tools and light a campfire, with
// stand-in bodies that run the same task text a real actor runs. Checked: it reaches the goals, no count goes negative, nothing is claimed twice,
// and two runs end identically. CPU only.
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <cmath>
#include <memory>
#include <cstdio>
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
	bool m_Started = false, m_Failed = false;
};

struct Village
{
	Village()
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		BuildResources(Entities, glm::vec3(0.0f), Defs);
		for (const char* n : { "Ana", "Biel", "Cleo", "Dan" })
		{
			Entity& e = *Entities.Find(Entities.Create(n));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather" };
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
		for (auto& b : bodies) for (const ItemSlot& s : b->Bag().Stacks()) if (s.Count <= 0) bad = true;
	}
	std::string Fingerprint() const
	{
		std::string f = Tribe_.Store().ToJson() + (Tribe_.HasFire() ? "F" : "-");
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
	Tribe Tribe_;
	std::vector<std::unique_ptr<FakeBody>> bodies;
	bool bad = false;
};

int main()
{
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
	Check(t.Store().Count("fiber") + t.Store().Count("berry") + t.Store().Count("wood") + t.Store().Count("stone") > 0, "the store holds what was brought");
	int deposited = 0;
	for (const TribeMember& m : t.Members()) { deposited += m.Deposited; Check(m.Failures == 0, "no trip failed"); }
	Check(deposited >= 20, "the tribe carried a good amount to the store");

	Village w;
	w.Run(600.0);
	Check(w.Fingerprint() == v.Fingerprint(), "two runs end identically");

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("tribe: ok");
	return 0;
}
