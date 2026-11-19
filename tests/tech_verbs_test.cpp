// T322 (documentation/01_diseno/ARBOL_TECNICO.md §11.3, §11.7.1): technique -> verb (GB/Game/TechVerbs.h). Checked: the "verbs" of a technology
// read, save and are validated (a verb the game does not know, one twice, a bad farm action); a verb no technology names stays free; without the
// technology that names it the board posts no job of the verb (farm:till), the brain drops its options, an order (goto), a mark (designate) and
// a farm zone are refused with "does not know how to ..."; with it everything is back; KnowsVerb and the list for the UI. Data in memory only
// (the published pack is not read for the verbs). CPU only, the stand-ins of farm_zones_test.
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Game/GameContent.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/Spoilage.h"
#include "GB/Gameplay/Techs.h"
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

// File-local (T51: several tests share one executable, where two classes with one name would break the one-definition rule)
namespace
{
// The same stand-in as tribe_test: it runs the text of a task list against an ability system (goto and wait take time, "ability A node" waits for it).
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

// A clan of `n` on a patch scaled to it, as `tribe build` does (trades from its needs). `extended`: with the game's economy section too (the grain
// crop and the fields).
struct Clan
{
	explicit Clan(int n, bool extended = false)
	{
		RegisterEraOneItems(Items, Recipes, Defs);   // (the whole catalogue: seeds, grain, the hoe and dried berries are in it)
		for (const char* far : { "oak_acorns", "granite_rock", "pine_resin", "wild_grain", "salt_flat", "old_bones", "fallen_trunk" }) Defs.erase(far);   // (not placed here)
		if (extended)
			Check(LoadShippedSection(Tribe_.Config.Econ, [](const std::string& doc, EconomyRules& r) { return LoadEconomyRules(doc, r); }).empty(), "and the economy section of the game, with its crops");
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		const ResourceCounts c = ScaledResources(n);
		BuildResources(Entities, glm::vec3(0.0f), Defs, c.Trees, c.Rocks, c.Bushes, c.Grass);
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		const std::vector<std::string> roles = Tribe_.PlanRoles(n);
		const glm::vec2 camp(0.0f, -8.0f);
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
			bodies.push_back(std::make_unique<FakeBody>(Names[static_cast<size_t>(i)], Gameplay, Entities, camp + glm::vec2(static_cast<float>(i % 5) * 0.7f, static_cast<float>(i / 5) * 0.7f)));
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
		Tribe_.Config.Economy.Camp = camp;
		Tribe_.Store().Add(Items, "wood", 2 + 2 * n);
		Tribe_.Store().Add(Items, "stone", 3 * n);
		for (int i = 0; i < n; ++i) Tribe_.AddMember(Names[static_cast<size_t>(i)], roles[static_cast<size_t>(i)], bodies[static_cast<size_t>(i)].get());
	}
	void Run(double seconds)
	{
		const int frames = static_cast<int>(seconds / dt);
		for (int i = 0; i < frames; ++i)
		{
			Gameplay.Update(dt);
			Resources.Update(Gameplay, dt);
			for (auto& b : bodies) b->Update(dt);
			Fires.Update(dt, &Entities);
			Tribe_.Update(dt);
			for (const auto& [id, job] : Tribe_.Board().Jobs())
			{
				Posted[job.Kind + " " + job.Target] = true;
				for (const JobClaim& cl : job.Claims) Claimed[job.Kind + " " + job.Target] = cl.Member;
			}
			for (const ItemSlot& s : Tribe_.Store().Stacks()) bad = bad || s.Count < 1;
		}
	}
	size_t Index(const std::string& role) const
	{
		for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Role == role) return i;
		return 0;
	}
	bool Logged(const std::string& part) const
	{
		return std::any_of(Tribe_.Log().begin(), Tribe_.Log().end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
	}
	long First(const std::string& part) const
	{
		const auto& log = Tribe_.Log();
		const auto it = std::find_if(log.begin(), log.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
		return it == log.end() ? -1 : static_cast<long>(it - log.begin());
	}
	void Dump(const std::string& filter = std::string()) const
	{
		for (const std::string& l : Tribe_.Log()) if (filter.empty() || l.find(filter) != std::string::npos) std::fprintf(stderr, "  %s\n", l.c_str());
	}
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
	std::map<std::string, bool> Posted;
	std::map<std::string, std::string> Claimed;
	bool bad = false;
	void Clock(double seconds)
	{
		const int frames = static_cast<int>(seconds / dt);
		for (int i = 0; i < frames; ++i) { Now += dt; Tribe_.SetWorldClock(Now); Run(dt); }
	}
	int Plots(const std::string& field, const std::string& state) const
	{
		for (const Field& f : Tribe_.Economy().Fields) if (f.Name == field) return f.Count(state);
		return -1;
	}
	double Now = 0.0;
};
}

namespace
{
	// The tree of the test: tillage unlocks till and sow, reaping the verb to come "reap", "marking" the marks on trees, "orders" the walk.
	const char* kVerbTree = R"({ "techs": [
		{ "id": "tillage", "name": "Tillage", "verbs": [ "farm:till", "farm:sow" ], "discovery": { "teach": 0.2 } },
		{ "id": "reaping", "name": "Reaping", "requires": [ "tillage" ], "verbs": [ "reap" ] },
		{ "id": "marking", "name": "Marking", "verbs": [ "designate:tree" ] },
		{ "id": "orders", "name": "Orders", "verbs": [ "goto" ] },
		{ "id": "hoeing", "name": "Hoeing", "verbs": [ "farm:till" ] } ] })";

	bool Has(const std::vector<UtilityOption>& opts, const std::string& kind, const std::string& target)
	{
		return std::any_of(opts.begin(), opts.end(), [&](const UtilityOption& o) { return o.Kind == kind && o.Target == target; });
	}
}

int GBTest_tech_verbs(int, char**)
{
	// ---- the data: "verbs" of a technology, validated, saved and read back
	{
		TechTree tree;
		Check(LoadTechs(kVerbTree, tree).empty(), "a tree with verbs loads");
		Check(tree.Find("tillage")->Verbs == std::vector<std::string>({ "farm:till", "farm:sow" }), "tillage unlocks till and sow");
		Check(tree.ForVerb("farm:till") == std::vector<std::string>({ "hoeing", "tillage" }), "two technologies may unlock one verb (any of them is enough)");
		Check(tree.ForVerb("farm:harvest").empty(), "a verb no technology names: none unlocks it");
		Check(tree.Verbs() == std::vector<std::string>({ "designate:tree", "farm:sow", "farm:till", "goto", "reap" }), "every verb named, in id order");
		TechTree back;
		Check(LoadTechs(TechsToJson(tree), back).empty() && back == tree, "the verbs save and read back");
		Check(TechsToJson(TechTree{}).find("verbs") == std::string::npos, "a tree with no verbs writes none");

		TechTree t;
		const std::string twice = LoadTechs(R"({ "techs": [ { "id": "x", "verbs": [ "reap", "reap" ] } ] })", t);
		std::fprintf(stderr, "  twice: %s\n", twice.c_str());
		Check(twice.find("twice") != std::string::npos && t.Techs.empty(), "a verb twice in one technology is refused (and nothing changes)");
		const std::string unknown = LoadTechs(R"({ "techs": [ { "id": "x", "verbs": [ "juggle" ] } ] })", t);
		std::fprintf(stderr, "  unknown: %s\n", unknown.c_str());
		Check(unknown.find("not a verb") != std::string::npos && t.Techs.empty(), "a verb the game does not know is refused");
		Check(!LoadTechs(R"({ "techs": [ { "id": "x", "verbs": [ "farm:dance" ] } ] })", t).empty(), "a farm action that is none is refused");
		Check(!LoadTechs(R"({ "techs": [ { "id": "x", "verbs": [ "farm:" ] } ] })", t).empty() && !LoadTechs(R"({ "techs": [ { "id": "x", "verbs": "reap" } ] })", t).empty()
			&& !LoadTechs(R"({ "techs": [ { "id": "x", "verbs": [ 3 ] } ] })", t).empty(), "an empty action, a text where a list goes and a number are refused");
		Check(LoadTechs(R"({ "techs": [ { "id": "x", "verbs": [ "transplant", "tame", "dig_ditch", "select_seed", "transplant_tree", "drive", "gather:wood", "farm:harvest" ] } ] })", t).empty(),
			"the verbs to come and the work of today are verbs");
		Check(CheckVerb("farm:till").empty() && !CheckVerb("").empty(), "CheckVerb");
		Check(VerbWord("farm:till") == "till" && VerbWord("farm:tend") == "water" && VerbWord("dig_ditch") == "dig a ditch" && VerbWord("gather:wood") == "gather wood" && VerbWord("goto") == "go",
			"each verb has a word for people");
		Check(TechVerbs::Ruling(&tree, "farm", "till") == "farm:till" && TechVerbs::Ruling(&tree, "farm", "harvest").empty() && TechVerbs::Ruling(&tree, "designate", "tree") == "designate:tree"
			&& TechVerbs::Ruling(&tree, "designate", "rock").empty() && TechVerbs::Ruling(&tree, "goto", "").empty() == false && TechVerbs::Ruling(nullptr, "farm", "till").empty(),
			"a work is ruled by kind:what, then kind; nothing without verbs");
	}

	// ---- in play: without tillage there is no tilling (board, brain, zone); with it, there is
	{
		Clan k(4, true);
		Tribe& t = k.Tribe_;
		t.Config.Econ.Farming.Tech.clear();   // (the old gate of farming by a technology off: only the verbs decide here)
		t.Zones().SetPlayer(true);
		t.SetWorldClock(0.0);   // the first day of spring
		t.Store().Add(k.Items, "grain_seed", 30);
		t.Store().Add(k.Items, "stone_hoe", 2);

		// with no verbs in the data every work is free, as before
		Check(TechVerbs::List(t).empty() && TechVerbs::Report(t).empty() && KnowsVerb(t, "farm:till"), "no technologies: every verb is free");
		const FarmZones::Reply z1 = t.Zones().Paint(t, { 10.0f, 0.0f }, { 16.0f, 4.0f }, "grain");
		Check(z1.Ok, "a zone is painted while tilling is free");

		TechTree tree;
		Check(LoadTechs(kVerbTree, tree).empty(), "(the tree)");
		t.SetTechs(&tree);
		Check(!KnowsVerb(t, "farm:till") && !KnowsVerb(t, "farm:sow") && !KnowsVerb(t, "reap") && !KnowsVerb(t, "goto"), "without the technologies the clan does not know their verbs");
		Check(KnowsVerb(t, "farm:harvest") && KnowsVerb(t, "farm:tend") && KnowsVerb(t, "gather"), "a verb no technology names stays free");
		Check(TechVerbs::Allows(t, "farm", "harvest") && !TechVerbs::Allows(t, "farm", "till") && TechVerbs::Allows(t, "gather", "wood"), "Allows by kind and what");
		const std::vector<TechVerbs::Line> lines = TechVerbs::List(t);
		Check(lines.size() == 5 && TechVerbs::Unknown(t) == 5, "the list for the UI: 5 verbs, all to discover");
		const auto line = std::find_if(lines.begin(), lines.end(), [](const TechVerbs::Line& l) { return l.Verb == "farm:till"; });
		Check(line != lines.end() && !line->Known && line->Word == "till" && line->Techs == std::vector<std::string>({ "hoeing", "tillage" }), "each line: the verb, its word, known, its technologies");
		std::fprintf(stderr, "  %s\n", TechVerbs::Report(t).c_str());

		// the board: no tilling or sowing posted
		k.Posted.clear();
		k.Clock(120.0);
		Check(!k.Posted["farm till"] && !k.Posted["farm sow"], "without tillage the board posts no tilling or sowing");
		Check(k.Plots("zone 1", "grass") == 12, "and the zone stays grass");
		// the brain: an option of an unknown verb goes, the rest stay
		std::vector<UtilityOption> opts(4);
		opts[0].Kind = "farm"; opts[0].Target = "till";
		opts[1].Kind = "farm"; opts[1].Target = "harvest";
		opts[2].Kind = "rest";
		opts[3].Kind = "gather"; opts[3].Target = "wood";
		TechVerbs::Options(t, opts);
		Check(opts.size() == 3 && !Has(opts, "farm", "till") && Has(opts, "farm", "harvest") && Has(opts, "rest", "") && Has(opts, "gather", "wood"), "the brain does not choose an option of an unknown verb");
		// the zone, the order, the mark
		const FarmZones::Reply z2 = t.Zones().Paint(t, { 10.0f, 12.0f }, { 14.0f, 14.0f }, "grain");
		std::fprintf(stderr, "  paint: %s\n", z2.Text.c_str());
		Check(!z2.Ok && z2.Text.find("does not know how to till") != std::string::npos && t.Economy().Fields.size() == 1, "a farm zone is refused without the verb to till");
		const std::string who = t.Members()[0].Name;
		const ColonyOrders::Reply o1 = t.Colony().Give(t, who, "goto", { "2", "-8" });
		std::fprintf(stderr, "  order: %s\n", o1.Text.c_str());
		Check(!o1.Ok && o1.Text == who + " does not know how to go" && !t.Colony().Has(who), "an order of an unknown verb is refused");
		const std::vector<std::string> trees = k.Gameplay.NamesWithTag("Resource.Node.tree");
		Check(!trees.empty(), "(a tree to mark)");
		if (!trees.empty())
		{
			const ColonyDesignations::Reply d1 = t.Designations().Designate(t, trees.front());
			std::fprintf(stderr, "  mark: %s\n", d1.Text.c_str());
			bool own = false;   // (T337: zone 1 lies over some nodes of the patch, which it marks to clear itself: only the player's own marks count here)
			for (const auto& [node, mark] : t.Designations().Marks()) own = own || mark.Field.empty();
			Check(!d1.Ok && d1.Text.find("does not know how to") != std::string::npos && !own, "a mark of an unknown verb is refused");
		}
		const std::vector<std::string> rocks = k.Gameplay.NamesWithTag("Resource.Node.rock");
		if (!rocks.empty()) Check(t.Designations().Designate(t, rocks.front()).Ok, "a mark of a kind no verb names is free");

		// with tillage: the verbs are back
		Check(t.Knowledge().GrantClan("tillage").empty(), "(the clan learns tillage)");
		Check(KnowsVerb(t, "farm:till") && KnowsVerb(t, "farm:sow") && !KnowsVerb(t, "reap") && TechVerbs::Unknown(t) == 3, "with tillage the clan knows to till and sow, not to reap");
		k.Posted.clear();
		k.Clock(300.0);
		Check(k.Posted["farm till"] && k.Posted["farm sow"], "with tillage the board posts tilling and sowing");
		Check(k.Plots("zone 1", "grass") < 12, "and the zone is tilled");
		Check(t.Zones().Paint(t, { 10.0f, 12.0f }, { 14.0f, 14.0f }, "grain").Ok, "and a zone can be painted");
		Check(t.Knowledge().GrantClan("orders").empty() && t.Colony().Give(t, who, "goto", { "2", "-8" }).Ok, "with the technology the order is taken");
		t.SetTechs(nullptr);
	}

	// ---- the clans of the AI mark out no field of their own without the verb to till
	{
		Clan k(4, true);
		Tribe& t = k.Tribe_;
		t.Config.Econ.Farming.Tech.clear();
		t.SetWorldClock(0.0);
		t.Store().Add(k.Items, "grain_seed", 30);
		t.Store().Add(k.Items, "stone_hoe", 2);
		TechTree tree;
		LoadTechs(kVerbTree, tree);
		t.SetTechs(&tree);
		k.Clock(60.0);
		Check(t.Economy().Fields.empty(), "without tillage the AI marks out no field");
		t.Knowledge().GrantClan("hoeing");   // (any technology that names the verb is enough)
		k.Clock(60.0);
		Check(!t.Economy().Fields.empty(), "with one of the technologies of the verb it does");
		t.SetTechs(nullptr);
	}

	if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
	else std::printf("tech verbs: ok\n");
	return failures ? 1 : 0;
}
