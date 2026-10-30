// The society of the clan (H9 demography and inheritance + H10 relations, personality and culture). Checked: the "society" section of
// assets/data/era1.json says what the code says, round-trips and is refused whole on a mistake (also inside a pack); founders are deterministic;
// a population of 30 grows, holds or dies out over 200 simulated years depending on its food, and saving it halfway and going on gives the same
// people; inheritance follows the parents (regression to the mid-parent, mean and spread held over the generations, across seeds); relations move
// with what happens, fade, stay sparse, and decide partners and teachers; a lesson prefers the parents and a child learns faster; personality
// changes what the brain chooses; traditions are founded by repeated events; and a tribe with stand-in bodies has a child (a new body, small), loses
// an elder to old age, makes bonds from shared work and the camp, and saves and loads (an old save without the society loads too). CPU only.
#include "GB/Game/GameContent.h"
#include "GB/Game/Society.h"
#include "GB/Game/Tribe.h"
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
	constexpr float dt = 1.0f / 30.0f;
}

using namespace GB;

// The same stand-in as tribe_test and work_test: it runs the text of a task list against an ability system; it also remembers how grown it is.
class FakeBody : public ITribeBody
{
public:
	FakeBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
	bool Busy() const override { return !m_Steps.empty(); }
	bool Failed() const override { return m_Failed; }
	glm::vec2 Position() const override { return m_Pos; }
	Inventory& Bag() override { return m_Bag; }
	void SetGrowth(float g) override { Growth = g; }
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
	float Growth = 1.0f;
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

// A clan of `n` on a patch of resources, as in work_test, with a spawner that gives a newborn a stand-in body and an ability system.
struct Clan
{
	explicit Clan(int n, const SocietyRules& rules = DefaultSocietyRules())
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		const ResourceCounts c = ScaledResources(n);
		BuildResources(Entities, glm::vec3(0.0f), Defs, c.Trees, c.Rocks, c.Bushes, c.Grass);
		Tribe_.Config.Society = rules;
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		const std::vector<std::string> roles = Tribe_.PlanRoles(n);
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
			bodies.push_back(std::make_unique<FakeBody>(Names[static_cast<size_t>(i)], Gameplay, Entities, Camp + glm::vec2(static_cast<float>(i % 5) * 0.7f, static_cast<float>(i / 5) * 0.7f)));
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
		Tribe_.Config.Economy.Camp = Camp;
		Tribe_.Store().Add(Items, "wood", 2 + 2 * n);
		Tribe_.Store().Add(Items, "stone", 3 * n);
		for (int i = 0; i < n; ++i) Tribe_.AddMember(Names[static_cast<size_t>(i)], roles[static_cast<size_t>(i)], bodies[static_cast<size_t>(i)].get());
		Tribe_.SetSpawner([this](const std::string& name, const glm::vec2& at, float growth) -> ITribeBody*
		{
			AbilitySystemComponent* asc = Gameplay.Create(name);
			for (const char* a : { "Chop", "Mine", "Gather", "PullFiber", "Stoke" }) asc->Grant(a);
			Names.push_back(name);
			bodies.push_back(std::make_unique<FakeBody>(name, Gameplay, Entities, at));
			bodies.back()->m_Items = &Items;
			bodies.back()->Growth = growth;
			return bodies.back().get();
		});
	}
	void Run(double seconds)
	{
		const int frames = static_cast<int>(seconds / dt);
		for (int i = 0; i < frames; ++i)
		{
			Gameplay.Update(dt);
			Resources.Update(Gameplay, dt);
			for (size_t b = 0; b < bodies.size(); ++b) bodies[b]->Update(dt);
			Resources.SyncVisuals(Gameplay, Entities);
			Fires.Update(dt, &Entities);
			Tribe_.Update(dt);
		}
	}
	int Gathered() const
	{
		int n = 0;
		for (const TribeMember& m : Tribe_.Members()) n += m.Deposited;
		return n;
	}
	FakeBody* Body(const std::string& name) { for (size_t i = 0; i < Names.size(); ++i) if (Names[i] == name) return bodies[i].get(); return nullptr; }
	bool Logged(const std::string& text) const
	{
		return std::any_of(Tribe_.Log().begin(), Tribe_.Log().end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
	}
	glm::vec2 Camp{ 0.0f, -8.0f };
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
};

// A population on its own, day by day for `years`: `food` is what the land gives the clan (its store), so the food per member falls as it grows.
struct Population
{
	SocietyRules Rules = DefaultSocietyRules();
	Society People;
	int Next = 0;
	std::vector<std::pair<float, float>> Strength;   // (mid-parent, child) of every birth
	std::vector<int> Peak;
	void Found(int n)
	{
		for (int i = 0; i < n; ++i) People.Join(Rules, "F" + std::to_string(i));
	}
	void Run(float years, float food, bool shelter = true)
	{
		const int days = static_cast<int>(years * Rules.Life.DaysPerYear);
		for (int d = 0; d < days; ++d)
		{
			ClanState clan;
			clan.Food = food;
			clan.Alive = People.Alive();
			clan.Shelter = shelter;
			for (const SocietyEvent& e : People.Day(Rules, clan))
				if (e.Kind == "birth")
				{
					const std::string child = "C" + std::to_string(Next++);
					People.Bear(Rules, child, e.Who, e.Other);
					const float mid = 0.5f * (People.Find(e.Who)->Genes.at("strength") + People.Find(e.Other)->Genes.at("strength"));
					Strength.emplace_back(mid, People.Find(child)->Genes.at("strength"));
				}
		}
	}
};

int main()
{
	// ---- the data: the section of era1.json says what the code says, round-trips, and a mistake changes nothing
	{
		SocietyRules file;
		Check(LoadSocietyRulesFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", file).empty(), "the society section of assets/data/era1.json loads");
		Check(file == DefaultSocietyRules(), "and says what DefaultSocietyRules says");
		SocietyRules back;
		Check(LoadSocietyRules("{ \"society\": " + SocietyRulesToJson(DefaultSocietyRules()) + " }", back).empty() && back == DefaultSocietyRules(), "the society section round-trips");
		SocietyRules keep = DefaultSocietyRules();
		Check(!LoadSocietyRules(R"({ "society": { "life": { "daysPerYear": 8 }, "genes": { "strength": { "mean": 2 } } } })", keep).empty() && keep == DefaultSocietyRules(), "a gene mean above 1 is refused and nothing changes (not even the life before it)");
		Check(!LoadSocietyRules(R"({ "society": { "life": { "childUntil": 60 } } })", keep).empty(), "a child older than an elder is refused");
		Check(!LoadSocietyRules(R"({ "society": { "culture": { "norms": { "x": { "trait": "nope" } } } } })", keep).empty(), "a norm that follows no trait is refused");
		Check(!LoadSocietyRules(R"({ "society": { "genes": { "curiosity": {} } } })", keep).empty(), "a gene named like a trait is refused");
		Check(!LoadSocietyRules(R"({ "society": { "wings": 1 } })", keep).empty() && !LoadSocietyRules(R"({ "society": { "births": { "chance": 2 } } })", keep).empty(), "an unknown part or a chance above 1 is refused");
		Check(LoadSocietyRules(R"({ "items": [] })", keep).empty() && keep == DefaultSocietyRules(), "a file without the section changes nothing");
		SocietyRules some = DefaultSocietyRules();
		Check(LoadSocietyRules(R"({ "society": { "births": { "chance": 0.5 } } })", some).empty() && some.Births.Chance == 0.5f && some.Births.Spacing == BirthRules{}.Spacing
			&& some.Genes == DefaultSocietyRules().Genes, "a part replaces that part whole (what it does not say is the default) and leaves the others");

		// a section of the content packs: era1 brings it, all or nothing with the rest of a document
		ContentPacks packs;
		SocietyRules live;
		live.Births.Chance = 0.9f;
		AddSocietyContentSections(packs, live);
		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes; std::map<std::string, TribeRole> roles = DefaultRoles();
		RegisterEraOneItems(items, recipes, nodes);
		AddGameContentSections(packs, items, recipes, nodes, roles);
		Check(packs.Load("era1", std::string(GB_ASSETS_DIR) + "/data").empty() && live == DefaultSocietyRules(), "the era1 pack brings the society through its section");
		Check(!packs.LoadText(R"({ "society": { "seed": 7 }, "roles": { "bad": { "tool": "x" } } })").empty() && live.Seed == 1u, "a bad role in the same document leaves it as it was");
		Check(packs.LoadText(R"({ "society": { "seed": 7 } })").empty() && live.Seed == 7u, "a good one changes it");
	}

	// ---- founders: deterministic, within their bounds, two sexes
	{
		const SocietyRules rules = DefaultSocietyRules();
		Society a, b;
		for (int i = 0; i < 6; ++i) { a.Join(rules, "F" + std::to_string(i)); b.Join(rules, "F" + std::to_string(i)); }
		Check(a == b, "the same founders twice are the same people");
		int women = 0;
		bool bounded = true;
		for (const auto& [name, p] : a.People())
		{
			women += p.Female ? 1 : 0;
			bounded = bounded && p.Age >= rules.Life.FounderMin && p.Age <= rules.Life.FounderMax && p.Genes.size() == rules.Genes.size() + rules.Traits.size() && p.Traits.size() == rules.Traits.size();
			for (const auto& [g, v] : p.Genes) bounded = bounded && v >= 0.0f && v <= 1.0f;
		}
		Check(bounded, "founders have their age between founderMin and founderMax and every gene and trait in 0..1");
		Check(women == 3, "six founders are three women and three men");
		SocietyRules other = rules;
		other.Seed = 2;
		Society c;
		for (int i = 0; i < 6; ++i) c.Join(other, "F" + std::to_string(i));
		Check(!(c == a), "another seed, other people");
		Check(a.Join(rules, "F0").Age == b.Find("F0")->Age, "joining again changes nothing");
	}

	// ---- demography: 200 years from 30, with plenty, with enough and with nothing to spare
	{
		Population rich, fair, poor;
		for (Population* p : { &rich, &fair, &poor }) p->Found(30);
		rich.Run(200.0f, 300.0f);   // food for 200 at 1.5 each: it grows to the limit of what it has
		fair.Run(200.0f, 50.0f);    // food for some 33: it holds
		poor.Run(200.0f, 0.0f);     // nothing to spare: no children, the founders grow old and die
		std::printf("population after 200 years: rich %d (born %d), fair %d (born %d), poor %d; rich generations %d\n", rich.People.Alive(), rich.People.Births(),
			fair.People.Alive(), fair.People.Births(), poor.People.Alive(), [&] { int g = 0; for (const auto& [n, p] : rich.People.People()) g = std::max(g, p.Generation); return g; }());
		Check(rich.People.Alive() > 60, "with plenty of food a clan of 30 grows over 200 years");
		Check(fair.People.Alive() >= 15 && fair.People.Alive() <= 60, "with food for about its size it holds (15 to 60 after 200 years)");
		Check(poor.People.Alive() == 0 && poor.People.Births() == 0, "with no food to spare it has no children and dies out");
		Check(rich.People.Deaths() > 30, "and the founders and their children die of old age");
		int oldest = 0;
		for (const auto& [n, p] : rich.People.People()) if (p.Dead) oldest = std::max(oldest, static_cast<int>(p.Age));
		Check(oldest <= static_cast<int>(rich.Rules.Life.MaxAge + rich.Rules.Life.Longevity) + 1, "nobody outlives maxAge plus its longevity");
		Population none;
		none.Found(30);
		none.Run(30.0f, 300.0f, false);
		Check(none.People.Births() == 0, "without a fire (shelter) there are no children");

		// determinism, and a population saved halfway goes on as if nothing had happened
		Population again;
		again.Found(30);
		again.Run(200.0f, 50.0f);
		Check(again.People.ToJson() == fair.People.ToJson(), "two runs give the same people");
		Population first;
		first.Found(30);
		first.Run(100.0f, 50.0f);
		Population resumed;
		Check(resumed.People.FromJson(first.People.ToJson()), "a population reads back");
		resumed.Next = first.Next;
		Check(resumed.People == first.People, "and is the same");
		resumed.Run(100.0f, 50.0f);
		Check(resumed.People.ToJson() == fair.People.ToJson(), "saved at 100 years and resumed, it ends the same as without stopping");
		Check(!resumed.People.FromJson("[1]") && resumed.People.ToJson() == fair.People.ToJson(), "junk does not read and changes nothing");
	}

	// ---- inheritance: a child follows its parents, and over generations the trait keeps its mean and its spread (several seeds)
	{
		double slopeSum = 0.0;
		int seeds = 0;
		bool held = true;
		for (uint32_t seed = 1; seed <= 4; ++seed)
		{
			Population p;
			p.Rules.Seed = seed;
			p.Found(30);
			p.Run(200.0f, 300.0f);
			const auto& pairs = p.Strength;
			if (pairs.size() < 50) { held = false; continue; }
			double mx = 0, my = 0;
			for (const auto& [x, y] : pairs) { mx += x; my += y; }
			mx /= static_cast<double>(pairs.size()); my /= static_cast<double>(pairs.size());
			double sxy = 0, sxx = 0, syy = 0;
			for (const auto& [x, y] : pairs) { sxy += (x - mx) * (y - my); sxx += (x - mx) * (x - mx); syy += (y - my) * (y - my); }
			const double slope = sxx > 0.0 ? sxy / sxx : 0.0;
			slopeSum += slope;
			++seeds;
			// the last generations: mean near the founders' and some spread left (the mutation keeps it from collapsing)
			std::vector<float> late;
			for (size_t i = pairs.size() * 3 / 4; i < pairs.size(); ++i) late.push_back(pairs[i].second);
			double m = 0, v = 0;
			for (const float x : late) m += x;
			m /= static_cast<double>(late.size());
			for (const float x : late) v += (x - m) * (x - m);
			const double sd = std::sqrt(v / static_cast<double>(late.size()));
			std::printf("seed %u: %zu births, slope child/mid-parent %.2f, late mean %.2f sd %.3f\n", seed, pairs.size(), slope, m, sd);
			held = held && m > 0.3 && m < 0.7 && sd > 0.02 && sd < 0.25;
		}
		Check(seeds == 4 && slopeSum / seeds > 0.6 && slopeSum / seeds < 1.3, "a child's gene follows the mid-parent (regression slope about 1, over four seeds)");
		Check(held, "and over the generations the gene keeps a mean near the founders' and a spread (mutation)");

		const SocietyRules rules = DefaultSocietyRules();
		Society s;
		s.Join(rules, "Ma");
		s.Join(rules, "Pa");
		for (const auto& [id, g] : rules.Genes) { s.SetGene("Ma", id, 0.9f); s.SetGene("Pa", id, 0.9f); }
		s.SetTrait("Ma", "curiosity", 1.0f);
		s.SetTrait("Pa", "curiosity", 1.0f);
		const Person& kid = s.Bear(rules, "Kid", "Ma", "Pa");
		Check(kid.Age == 0.0f && kid.Generation == 1 && kid.Mother == "Ma" && kid.Father == "Pa" && s.Find("Ma")->Children == 1, "a child is born at 0 to its parents");
		Check(std::fabs(kid.Genes.at("strength") - 0.9f) <= rules.Mutation + 1e-4f, "two strong parents have a strong child (within the mutation)");
		Check(kid.Traits.at("curiosity") > 0.6f, "curious parents bring up a curious child (gene and upbringing)");
		Check(s.Kin("Kid", "Ma") == "mother" && s.Kin("Ma", "Kid") == "child" && s.Affinity(rules, "Kid", "Ma") > rules.Relations.Kin, "a child is family, and close to its parents from birth");
		Check(s.LearnRate(rules, "Kid", "crafting") > s.LearnRate(rules, "Ma", std::string()) && s.StageOf(rules, "Kid") == Society::Stage::Child, "a child learns faster; the dexterous learn crafting faster");
		s.SetGene("Ma", "dexterity", 0.0f);
		s.SetGene("Pa", "dexterity", 1.0f);
		Check(s.LearnRate(rules, "Pa", "crafting") > s.LearnRate(rules, "Ma", "crafting"), "the learning rate of a family is inherited (genes), not the skill");
		Check(s.Growth(rules, "Kid") == rules.Life.ChildHeight && s.Growth(rules, "Ma") == 1.0f, "a newborn is a child's height, an adult its full height");
	}

	// ---- relations: events, fading, sparse, family, partners, teachers
	{
		SocietyRules rules = DefaultSocietyRules();
		Society s;
		for (const char* n : { "A", "B", "C", "D", "E", "F" }) s.Join(rules, n);   // A, C, E women; B, D, F men
		for (const char* n : { "A", "B", "C", "D", "E", "F" }) s.SetAge(n, 25.0f);
		for (int i = 0; i < 5; ++i) s.Relate(rules, "A", "D", "work");
		s.Relate(rules, "A", "B", "theft");
		Check(s.Bond("A", "D") > 0.09f && s.Bond("D", "A") == s.Bond("A", "D") && s.Bond("A", "B") < -0.3f, "working together brings closer, a theft drives apart (and a bond is the same both ways)");
		s.Relate(rules, "C", "F", "rescue");
		Check(s.Bond("C", "F") > 0.35f, "a rescue makes a strong bond");
		s.Relate(rules, "C", "F", "nothing");
		Check(s.Bond("C", "F") > 0.35f, "an event the data does not know does nothing");
		ClanState clan;
		clan.Alive = 6;
		const std::vector<SocietyEvent> day = s.Day(rules, clan);
		Check(s.Find("A")->Partner == "D" && s.Find("C")->Partner == "F", "partners are chosen by affinity (A likes D, not B whom she robbed... or who robbed her)");
		Check(s.Find("E")->Partner == "B", "and who is left pairs with who is left");
		Check(std::count_if(day.begin(), day.end(), [](const SocietyEvent& e) { return e.Kind == "pair"; }) == 3, "three pairs on the first day");
		Check(s.Kin("A", "D") == "partner" && s.Affinity(rules, "A", "D") > s.Bond("A", "D"), "a partner is family");
		const float before = s.Bond("A", "B");
		for (int i = 0; i < 30; ++i) s.Day(rules, clan);
		Check(std::fabs(s.Bond("A", "B")) < std::fabs(before) * 0.5f, "a bond fades with the days");
		for (int i = 0; i < 200; ++i) s.Day(rules, clan);
		Check(s.Bond("A", "B") == 0.0f && s.BondsOf("A").empty() == (s.Bond("A", "D") == 0.0f), "and goes once it is weak (the graph stays sparse)");
		rules.Relations.MaxPerMember = 3;
		Society big;
		for (int i = 0; i < 10; ++i) big.Join(rules, "M" + std::to_string(i));
		for (int i = 1; i < 10; ++i) big.Adjust(rules, "M0", "M" + std::to_string(i), 0.05f * static_cast<float>(i));
		Check(big.BondsOf("M0").size() == 3 && big.BondsOf("M0").front().first == "M9", "a member keeps only its strongest bonds");
		Check(big.Mentors(rules, "M0", 2) == std::vector<std::string>{ "M9", "M8" }, "it would rather learn from those it likes best");
		Society fam;
		fam.Join(rules, "Ma");
		fam.Join(rules, "Pa");
		fam.Join(rules, "Other");
		fam.Bear(rules, "Kid", "Ma", "Pa");
		fam.Bear(rules, "Kid2", "Ma", "Pa");
		fam.Adjust(rules, "Kid", "Other", 0.9f);
		Check(fam.Mentors(rules, "Kid", 3) == std::vector<std::string>{ "Ma", "Pa", "Other" }, "but from its parents first");
		Check(fam.Kin("Kid", "Kid2") == "sibling" && fam.Favourite(rules, "Kid", { "Pa", "Other" }) == "Other", "siblings are family; the favourite is the best liked");
		fam.Died(rules, "Ma", "Hunger");
		Check(fam.Find("Pa")->Partner.empty() && fam.Mentors(rules, "Kid", 3).front() == "Pa" && fam.Find("Ma")->Dead, "the dead teach no more and free their partner");
		Check(fam.Relations(rules, "Kid").find("Pa") != std::string::npos && fam.Family(rules, "Kid").find("mother Ma (dead)") != std::string::npos, "the reports say who is who");
	}

	// ---- teaching: the parents first, a child learns faster, an elder teaches better (Knowledge lessons)
	{
		const TechTree techs = DefaultTechs();
		ClanKnowledge k;
		k.SetTechs(&techs);
		for (const char* n : { "Kid", "Stranger", "Ma" }) k.Join(n);
		k.Grant("Stranger", "flint_knapping");
		k.Grant("Ma", "flint_knapping");
		std::vector<KnowledgeAgent> view(3);
		const char* names[] = { "Stranger", "Ma", "Kid" };
		for (size_t i = 0; i < 3; ++i) { view[i].Name = names[i]; view[i].AtCamp = true; view[i].Idle = true; }
		std::vector<KnowledgeEvent> events = k.Lesson(view);
		Check(!events.empty() && events.front().Other == "Stranger", "without mentors a learner learns from the first who knows");
		ClanKnowledge k2;
		k2.SetTechs(&techs);
		for (const char* n : { "Kid", "Stranger", "Ma" }) k2.Join(n);
		k2.Grant("Stranger", "flint_knapping");
		k2.Grant("Ma", "flint_knapping");
		view[2].Mentors = { "Ma" };
		view[2].Learn = 1.6f;
		view[1].Teach = 1.5f;
		events = k2.Lesson(view);
		const float teach = techs.Find("flint_knapping")->Discovery.Teach;
		Check(!events.empty() && events.front().Other == "Ma", "with its mother at the camp, the child learns from her");
		const AgentKnowledge* kid = k2.Of("Kid");
		Check(kid && (kid->Known.count("flint_knapping") || std::fabs(kid->Progress.at("flint_knapping") - std::min(1.0f, teach * 1.6f * 1.5f)) < 1e-4f), "a child learns faster from an elder who teaches better");
	}

	// ---- personality and culture on the options
	{
		SocietyRules rules = DefaultSocietyRules();
		Society s;
		s.Join(rules, "Curious");
		s.Join(rules, "Dull");
		s.SetTrait("Curious", "curiosity", 1.0f);
		s.SetTrait("Dull", "curiosity", 0.0f);
		Check(s.OptionFactor(rules, "Curious", "experiment", "crafting").Nature > 1.5f * s.OptionFactor(rules, "Dull", "experiment", "crafting").Nature, "curiosity makes experimenting worth more");
		s.SetAge("Dull", 5.0f);
		Check(s.OptionFactor(rules, "Dull", "gather", "woodcutting").Life == rules.Life.ChildWork && s.OptionFactor(rules, "Dull", "gather", "foraging").Life == 1.0f, "a child leaves heavy work (felling) but gathers berries");
		s.SetAge("Dull", 60.0f);
		Check(s.OptionFactor(rules, "Dull", "gather", "mining").Life == rules.Life.ElderWork && s.TeachRate(rules, "Dull") == rules.Life.ElderTeach && s.WorkPace(rules, "Dull", "") < 1.0f, "an elder works less and slower and teaches better");
		const float plain = s.OptionFactor(rules, "Curious", "warm", "").Culture;
		std::vector<SocietyEvent> founded;
		for (int i = 0; i < 30; ++i) for (SocietyEvent& e : s.Note(rules, "stoke")) founded.push_back(e);
		Check(founded.size() == 1 && founded[0].Kind == "tradition" && founded[0].Who == "fire_vigil" && s.Traditions().size() == 1, "the 30th time the fire is fed founds the Fire vigil, once");
		Check(s.OptionFactor(rules, "Curious", "warm", "").Culture > plain, "and the tradition makes warming at the fire worth more");
		// the norms drift towards the adults' traits
		Society drift;
		for (int i = 0; i < 6; ++i) { drift.Join(rules, "D" + std::to_string(i)); drift.SetTrait("D" + std::to_string(i), "diligence", 1.0f); drift.SetAge("D" + std::to_string(i), 30.0f); }
		ClanState clan;
		clan.Alive = 6;
		bool told = false;
		for (int d = 0; d < 40; ++d) for (const SocietyEvent& e : drift.Day(rules, clan)) told = told || (e.Kind == "norm" && e.Who == "industry");
		Check(drift.Norm(rules, "industry") > 0.8f && told, "a diligent clan comes to value industry (and the log says so)");
		Check(drift.OptionFactor(rules, "D0", "rest", "").Culture < 0.9f, "and resting is worth less to everyone in it");
	}

	// ---- a tribe with stand-in bodies: an elder dies of old age, a couple has a child (a new, small body), and it all saves
	{
		SocietyRules rules = DefaultSocietyRules();
		rules.Life.DaySeconds = 30.0f;   // a day of the society every 30 s and a year every two
		rules.Life.DaysPerYear = 2.0f;
		rules.Births.Chance = 1.0f;
		rules.Births.NeedsFire = false;
		rules.Births.FoodPerMember = 0.0f;
		rules.Births.Spacing = 1.0f;
		auto make = [&](Clan& c)
		{
			c.Tribe_.People().SetAge("Ana", 20.0f);
			c.Tribe_.People().SetAge("Biel", 22.0f);
			c.Tribe_.People().SetAge("Cleo", 50.0f);   // (too old for Biel: Ana and Biel are the couple)
			c.Tribe_.People().SetAge("Dan", 85.0f);
		};
		Clan c(4, rules);
		make(c);
		c.Run(100.0);
		const Society& people = c.Tribe_.People();
		const TribeMember* dan = nullptr;
		for (const TribeMember& m : c.Tribe_.Members()) if (m.Name == "Dan") dan = &m;
		Check(dan && dan->Dead && dan->Cause == "OldAge" && people.Find("Dan")->Dead, "an old member dies of old age on the first day");
		Check(people.Find("Ana")->Partner == "Biel" && c.Logged("pairs with Biel"), "Ana and Biel pair");
		Check(c.Tribe_.Members().size() >= 5 && c.Tribe_.Members()[4].Name == "Eli" && c.Logged("has a child: Eli"), "and have a child, Eli, the next name of the data");
		Check(people.Find("Eli") && people.Find("Eli")->Mother == "Ana" && people.Find("Eli")->Father == "Biel" && c.Tribe_.Members()[4].Role == rules.Life.ChildRole, "Eli is their child and has the children's trade");
		Check(c.Body("Eli") && c.Body("Eli")->Growth < 0.7f, "Eli has a body of its own, at a child's height");
		Check(people.Find("Ana")->Children >= 1 && people.Births() >= 1 && people.Deaths() == 1, "the society counts the birth and the death");
		std::printf("%s\n%s\n", c.Tribe_.FamilyReport("Eli").c_str(), c.Tribe_.CultureReport().c_str());

		// saving: the tribe (with its society) round-trips, a save from before the society loads, and two runs end the same
		const std::string saved = c.Tribe_.ToJson();
		Clan d(4, rules);
		for (size_t i = 4; i < c.Tribe_.Members().size(); ++i)   // the children are recreated before loading, as the editor does with their actors
		{
			const std::string name = c.Tribe_.Members()[i].Name;
			AbilitySystemComponent* asc = d.Gameplay.Create(name);
			for (const char* a : { "Chop", "Mine", "Gather", "PullFiber", "Stoke" }) asc->Grant(a);
			d.Names.push_back(name);
			d.bodies.push_back(std::make_unique<FakeBody>(name, d.Gameplay, d.Entities, d.Camp));
			d.bodies.back()->m_Items = &d.Items;
			d.Tribe_.AddMember(name, c.Tribe_.Members()[i].Role, d.bodies.back().get());
		}
		Check(d.Tribe_.FromJson(saved, false) && d.Tribe_.People() == c.Tribe_.People(), "the society goes and comes back with the tribe");
		Check(d.Body("Eli") && std::fabs(d.Body("Eli")->Growth - c.Body("Eli")->Growth) < 1e-5f, "a loaded child's body is given its height again");
		// a save from before the society: the same without its "society" object (cut out by its braces)
		std::string old = saved;
		if (const size_t at = old.find("\"society\":"); at != std::string::npos)
		{
			size_t end = old.find('{', at);
			for (int depth = 0; end < old.size(); ++end)
			{
				depth += old[end] == '{' ? 1 : old[end] == '}' ? -1 : 0;
				if (depth == 0) break;
			}
			old.erase(at, end + 1 - at + (end + 1 < old.size() && old[end + 1] == ',' ? 1 : 0));
		}
		Clan e(4, rules);
		Check(old.find("\"society\"") == std::string::npos && e.Tribe_.FromJson(old, false) && e.Tribe_.People().Find("Ana") && e.Tribe_.People().Alive() == 4, "a save from before the society loads, its members founders");
		Clan c2(4, rules);
		make(c2);
		c2.Run(100.0);
		Check(c2.Tribe_.ToJson() == saved, "two runs end identically");
		Check(people.OptionFactor(rules, "Eli", "gather", "woodcutting").Life == rules.Life.ChildWork, "the child leaves heavy work");
	}

	// ---- a clan that works side by side and sits together at the camp makes bonds; a diligent clan gathers more than an idle one
	{
		Clan c(6);
		c.Run(600.0);
		const Society& people = c.Tribe_.People();
		size_t bonds = people.BondCount();
		bool positive = false;
		for (const std::string& n : c.Names) for (const auto& [o, b] : people.BondsOf(n)) positive = positive || b > 0.0f;
		std::printf("bonds after 10 minutes: %zu; %s\n", bonds, c.Tribe_.RelationsReport("Ana").c_str());
		Check(bonds > 0 && positive, "relations grow from shared work, food and the camp");
		Check(people.Find("Ana")->Age > 0.0f, "the members have ages");

		auto run = [](float diligence)
		{
			Clan k(4);
			for (const std::string& n : k.Names) k.Tribe_.People().SetTrait(n, "diligence", diligence);
			k.Run(400.0);
			return k.Gathered();
		};
		const int keen = run(1.0f), idle = run(0.0f);
		std::printf("gathered in 400 s: diligent %d, idle %d\n", keen, idle);
		Check(keen > idle, "personality changes what the brain chooses: a diligent clan carries more to the store than an idle one");
	}

	if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
	else std::printf("society: all checks passed\n");
	return failures ? 1 : 0;
}
