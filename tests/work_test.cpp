// Roles and the work of the clan (H7: E3 aptitudes by practice, E8 job board). Checked: the "work" section of assets/data/era1.json says what the code
// says, round-trips and is refused whole on a mistake; skills rise with practice (with diminishing returns), fall slowly without it and the trade
// boosts them; the board posts the clan's needs, ages what nobody takes, lets claims lapse and round-trips; the trades of a new clan come from its
// needs; a clan that works gets better at what it does, posts and claims jobs, saves and loads its skills and its board (and an old save loads with
// none), and two runs end identically; a review of the trades moves a hand to where the clan lacks it. CPU only, stand-in bodies as in tribe_test.
#include "GB/Game/GameContent.h"
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
	constexpr float dt = 1.0f / 60.0f;
}

using namespace GB;

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
		Orders.push_back(tasks);
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
	std::vector<std::string> Orders;   // every task list it was given
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

// A clan of `n`: names from the data (MemberName) and trades from its needs (PlanRoles), on a patch scaled to it, as `tribe build` does in the editor.
struct Clan
{
	explicit Clan(int n)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		const ResourceCounts c = ScaledResources(n);
		BuildResources(Entities, glm::vec3(0.0f), Defs, c.Trees, c.Rocks, c.Bushes, c.Grass);
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);   // (bound before the trades are planned: PlanRoles weighs the nodes)
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
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		Tribe_.Config.Camp = camp;
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
			Resources.SyncVisuals(Gameplay, Entities);
			Fires.Update(dt, &Entities);
			Tribe_.Update(dt);
			for (const auto& [id, job] : Tribe_.Board().Jobs())
			{
				Posted[job.Kind + " " + job.Target] = true;
				for (const JobClaim& c : job.Claims) Claimed[job.Kind] = true, ClaimedBy[c.Member] = true;
				if (static_cast<int>(job.Claims.size()) > job.Slots) Overbooked = true;
			}
			if (i % 30 == 0)   // the "job" factor on the options a member weighed (a claim is a factor, not a new chooser)
				for (const TribeMember& m : Tribe_.Members())
					for (const UtilityOption& o : m.Mind.Options)
						for (const ScoreFactor& f : o.Factors) JobFactor = JobFactor || f.Name == "job";
		}
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
	std::map<std::string, bool> Posted, Claimed, ClaimedBy;
	bool Overbooked = false, JobFactor = false;
};

int main()
{
	// ---- the data: the "work" section of era1.json says what the code says, round-trips, and a mistake changes nothing
	{
		WorkRules file;
		Check(LoadWorkRulesFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", file).empty(), "the work section of assets/data/era1.json loads");
		Check(file == DefaultWorkRules(), "and says what DefaultWorkRules says");
		WorkRules back;
		Check(LoadWorkRules("{ \"work\": " + WorkRulesToJson(DefaultWorkRules()) + " }", back).empty() && back == DefaultWorkRules(), "the work section round-trips");
		WorkRules keep = DefaultWorkRules();
		Check(!LoadWorkRules(R"({ "work": { "names": [ "Zoe" ], "skills": { "gain": -1 } } })", keep).empty() && keep == DefaultWorkRules(), "a negative gain is refused and nothing changes (not even the names before it)");
		Check(!LoadWorkRules(R"({ "work": { "names": [ "Ana", "Ana" ] } })", keep).empty(), "a name twice is refused");
		Check(!LoadWorkRules(R"({ "work": { "names": [ "Ana Bel" ] } })", keep).empty(), "a name with a space is refused (it is an actor's name in commands)");
		Check(!LoadWorkRules(R"({ "work": { "jobs": { "wings": 1 } } })", keep).empty() && !LoadWorkRules(R"({ "work": { "chores": {} } })", keep).empty(), "an unknown key or part is refused");
		Check(LoadWorkRules(R"({ "items": [] })", keep).empty() && keep == DefaultWorkRules(), "a file without the section changes nothing");
		WorkRules some = DefaultWorkRules();
		Check(LoadWorkRules(R"({ "work": { "jobs": { "bonus": 1.0 } } })", some).empty() && some.Jobs.Bonus == 1.0f && some.Jobs.Every == JobRules{}.Every && some.Jobs.Priority.empty()
			&& some.Names == DefaultWorkRules().Names, "a part replaces that part whole (what it does not say is the default) and leaves the others");
		Check(MemberName(file, 0) == "Ana" && MemberName(file, 19) == "Unai" && MemberName(file, 20) == "Ana2" && MemberName(file, 41) == "Biel3", "names from the data, then again with a number");
		WorkRules none;
		Check(MemberName(none, 0) == "Member1", "and a list with no names still names everyone");

		// it is a section of the content packs: era1 brings it, all or nothing with the rest
		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes; std::map<std::string, TribeRole> roles = DefaultRoles();
		TribeAgenda agenda; TribeEconomy economy; WorkRules work;
		RegisterEraOneItems(items, recipes, nodes);
		ContentPacks packs;
		AddGameContentSections(packs, items, recipes, nodes, roles, &agenda, &economy, &work);
		Check(packs.Load("era1", std::string(GB_ASSETS_DIR) + "/data").empty() && work == DefaultWorkRules(), "the era1 pack brings the work rules through their section");
		const WorkRules before = work;
		Check(!packs.LoadText(R"({ "work": { "names": [ "Zoe" ] }, "roles": { "bad": { "tool": "x" } } })").empty() && work == before, "and a bad role in the same document leaves them as they were");
		Check(packs.LoadText(R"({ "work": { "names": [ "Zoe", "Yan" ] } })").empty() && work.Names.size() == 2 && work.Names[0] == "Zoe", "a good one changes them");
	}

	// ---- aptitudes: practice with diminishing returns, slow decay, the trade's boost, JSON
	{
		const SkillRules rules = DefaultSkillRules();
		SkillSet s;
		Check(Practise(s, rules, "mining", 1.0f, false) == rules.Gain && SkillLevel(s, "mining") == rules.Gain, "a first unit of practice gives Gain");
		const float early = Practise(s, rules, "mining", 10.0f, false);
		for (int i = 0; i < 200; ++i) Practise(s, rules, "mining", 10.0f, false);
		const float late = Practise(s, rules, "mining", 10.0f, false);
		Check(late < early * 0.2f && SkillLevel(s, "mining") <= rules.Max && SkillLevel(s, "mining") > 90.0f, "the returns diminish and the level never passes Max");
		SkillSet a, b;
		Practise(a, rules, "woodcutting", 5.0f, false);
		Practise(b, rules, "woodcutting", 5.0f, true);
		Check(SkillLevel(b, "woodcutting") > SkillLevel(a, "woodcutting") * 1.3f, "its own trade learns faster (Profession)");
		Check(SkillEffect(b, rules, "woodcutting", true) > SkillEffect(b, rules, "woodcutting", false), "and works better at it");
		SkillSet d = { { "foraging", 10.0f }, { "mining", 1.0f } };
		DecaySkills(d, rules, 1200.0f, 1200.0f);
		Check(std::fabs(SkillLevel(d, "foraging") - (10.0f - rules.DecayPerDay)) < 1e-4f && d.count("mining") == 0, "a day without practice costs DecayPerDay, and a level at 0 goes");
		Check(SkillsFromJson(SkillsToJson(a)) == a && SkillsFromJson("[1]").empty(), "skills round-trip (and junk reads as none)");
		Check(BestSkill({ { "a", 3.0f }, { "b", 7.0f }, { "c", 7.0f } }) == "b" && BestSkill({}).empty(), "the best skill (ties: the first by name)");
		std::map<std::string, ResourceNodeDef> defs;
		ItemLibrary items; RecipeBook recipes;
		RegisterEraOneItems(items, recipes, defs);
		Check(SkillFamily(rules, "flint_outcrop", &defs) == "mining" && SkillFamily(rules, "old_tree", &defs) == "woodcutting" && SkillFamily(rules, "berry_bush", &defs) == "foraging"
			&& SkillFamily(rules, "craft", &defs) == "crafting" && SkillFamily(rules, "nothing", &defs).empty(), "the family of a node by its kind, of an option by its name");
	}

	// ---- the board on its own: posting, claims and slots, ageing, lapsing, JSON
	{
		JobRules rules = DefaultWorkRules().Jobs;
		JobBoard board;
		board.Begin();
		board.Post("gather", "wood", "woodcutting", 8.0f, 1.0f, 2, "the store has 2 of 10 wood", 0.0, rules);
		board.Post("craft", "stone_axe", "crafting", 1.0f, 1.4f, 1, "", 0.0, rules);
		board.End(0.0, rules);
		Check(board.Jobs().size() == 2 && board.Ordered().front()->Id == "craft stone_axe", "the jobs are posted and ordered by priority");
		Check(board.Claim("gather wood", "Ana", 1.0) && board.Claim("gather wood", "Biel", 1.0) && !board.Claim("gather wood", "Cleo", 1.0), "a job takes as many claimants as it has slots");
		Check(board.Claim("craft stone_axe", "Ana", 2.0) && board.ClaimOf("Ana")->Id == "craft stone_axe" && board.Find("gather wood")->Claims.size() == 1, "a new claim lets go of the old one");
		board.Begin();
		board.Post("gather", "wood", "woodcutting", 8.0f, 1.0f, 2, "", 300.0, rules);
		board.Post("craft", "stone_axe", "crafting", 1.0f, 1.4f, 1, "", 300.0, rules);
		board.Progress("Biel", 2.0f, 200.0);
		const std::vector<std::string> gone = board.End(300.0, rules);
		Check(gone.size() == 1 && gone[0] == "Ana craft stone_axe" && board.ClaimOf("Biel"), "a claim with no progress for ClaimSeconds lapses; one that moved stays");
		board.Begin();
		board.Post("gather", "wood", "woodcutting", 8.0f, 1.0f, 2, "", 900.0, rules);
		const Job& axe = board.Post("craft", "stone_axe", "crafting", 1.0f, 1.4f, 1, "", 900.0, rules);
		Check(axe.Priority > axe.Base && board.Find("gather wood")->Priority == 1.0f, "a job nobody holds rises with the wait; a held one does not");
		board.Progress("Biel", 1.0f, 900.0);
		board.End(900.0, rules);
		JobBoard loaded;
		Check(loaded.FromJson(board.ToJson()) && loaded.ToJson() == board.ToJson() && !loaded.FromJson("{}"), "the board round-trips (and junk is refused)");
		board.Begin();
		board.Post("craft", "stone_axe", "crafting", 1.0f, 1.4f, 1, "", 905.0, rules);
		const std::vector<std::string> met = board.End(905.0, rules);
		Check(!board.Find("gather wood") && met.size() == 1 && met[0] == "Biel gather wood" && !board.ClaimOf("Biel"), "a need that is met goes from the board with its claims");
	}

	// ---- the trades of a new clan come from its needs
	{
		Clan probe(1);
		const std::vector<std::string> four = probe.Tribe_.PlanRoles(4), again = probe.Tribe_.PlanRoles(4), twenty = probe.Tribe_.PlanRoles(20);
		Check(four == again && four.size() == 4, "the plan is the same every time");
		auto count = [](const std::vector<std::string>& v, const char* r) { return static_cast<int>(std::count(v.begin(), v.end(), std::string(r))); };
		Check(count(four, "forager") >= 1 && count(four, "woodcutter") >= 1 && count(four, "miner") >= 1, "four members: every trade has a hand");
		Check(count(twenty, "woodcutter") >= 3 && count(twenty, "miner") >= 3 && count(twenty, "forager") > count(twenty, "miner") && twenty.size() == 20,
			"twenty: shared out by the needs (food per mouth makes most of them foragers), no trade left with one or two");
		std::fprintf(stderr, "  plan for 4:");
		for (const std::string& r : four) std::fprintf(stderr, " %s", r.c_str());
		std::fprintf(stderr, "\n  plan for 20: %d foragers, %d woodcutters, %d miners\n", count(twenty, "forager"), count(twenty, "woodcutter"), count(twenty, "miner"));
		probe.Tribe_.Config.Roles["guard"].Fixed = true;
		probe.Tribe_.Config.Roles["guard"].Weights = { { "Resource.Stone", 5.0f }, { "fire", 5.0f } };
		Check(count(probe.Tribe_.PlanRoles(20), "guard") == 0, "a fixed role is never handed out");
		Check(probe.Tribe_.PlanRoles(0).empty(), "nobody: no roles");
	}

	// ---- a clan that works: skills rise, the board is posted and claimed, nothing breaks
	Clan v(4);
	v.Run(600.0);
	const Tribe& t = v.Tribe_;
	std::fprintf(stderr, "%s%s", t.Report().c_str(), t.JobsReport().c_str());
	{
		bool rose = true, trade = false;
		for (const TribeMember& m : t.Members())
		{
			rose = rose && !m.Skills.empty();
			const auto role = t.Config.Roles.find(m.Role);
			if (role != t.Config.Roles.end() && BestSkill(m.Skills) == role->second.Skill) trade = true;
		}
		Check(rose, "every member practised something in ten minutes");
		Check(trade, "and at least one is best at its own trade");
		bool better = false;
		for (const std::string& line : t.Log()) better = better || line.find("is better at") != std::string::npos;
		Check(better, "the log says when someone gets better at something");
		Check(v.Posted.size() >= 3 && v.Claimed.count("gather") && v.ClaimedBy.size() >= 2, "the board posted the clan's needs and members claimed jobs");
		Check(!v.Overbooked, "no job ever held more claimants than its slots");
		bool claimLine = false;
		for (const std::string& line : t.Log()) claimLine = claimLine || line.find("takes the job") != std::string::npos;
		Check(claimLine, "claims are in the log");
		Check(v.JobFactor, "a claim is a factor of the options (the chooser stays the same)");
		Check(t.HasFire(), "the clan of four still lights its campfire");
		bool skilledOrders = false;
		for (const auto& b : v.bodies)
			for (const std::string& o : b->Orders) skilledOrders = skilledOrders || (o.find("ability") != std::string::npos && o.find("wait 0.") != std::string::npos);
		Check(skilledOrders, "a practised hand works faster (shorter waits between gathers)");
	}

	// ---- saved and loaded: skills and board come back; an old save (no skills, no board) loads with none
	{
		const std::string saved = t.ToJson();
		Clan back(4);
		Check(back.Tribe_.FromJson(saved, false), "the tribe with skills and board loads");
		bool same = back.Tribe_.Board().ToJson() == t.Board().ToJson();
		for (size_t i = 0; i < t.Members().size(); ++i) same = same && back.Tribe_.Members()[i].Skills == t.Members()[i].Skills;
		Check(same, "with the same skills and the same board");
		Check(back.Tribe_.ToJson() == saved, "and saves again to the same text");

		Clan fresh(4);
		std::string old = fresh.Tribe_.ToJson();
		for (const char* key : { "\"board\":[],", "\"boardTimer\":0.0,", "\"reviewTimer\":0.0," })   // (the keys are in name order: "board" is the first)
		{
			const size_t at = old.find(key);
			Check(at != std::string::npos, "(a fresh save has the new keys)");
			if (at != std::string::npos) old.erase(at, std::string(key).size());
		}
		for (size_t at; (at = old.find("\"skills\":{},")) != std::string::npos;) old.erase(at, 12);
		for (size_t at; (at = old.find(",\"workKind\":\"\",\"workStart\":0")) != std::string::npos;) old.erase(at, 28);
		Clan older(4);
		Check(older.Tribe_.FromJson(old, false) && older.Tribe_.Members()[0].Skills.empty() && older.Tribe_.Board().Jobs().empty(), "an old save loads with no skills and an empty board");
	}

	// ---- two runs end identically (skills, board, roles and all)
	{
		Clan w(4);
		w.Run(600.0);
		Check(w.Tribe_.ToJson() == t.ToJson(), "two runs end identically");
	}

	// ---- the review of the trades: a clan of four foragers moves a hand to the trade it lacks (one change per review), and a fixed role stays
	{
		Clan c(4);
		for (TribeMember& m : c.Tribe_.Members()) m.Role = "forager";
		for (auto& b : c.bodies) { b->Bag().Add(c.Items, "stone_axe", 1); b->Bag().Add(c.Items, "stone_pickaxe", 1); }   // any of them could take up any trade
		c.Tribe_.Members()[1].Skills = { { "mining", 40.0f }, { "woodcutting", 40.0f } };   // whichever trade is short, it is the one good at it
		Check(c.Tribe_.ReviewRoles(), "the review changes something");
		int foragers = 0;
		for (const TribeMember& m : c.Tribe_.Members()) if (m.Role == "forager") ++foragers;
		Check(foragers == 3, "one member at a time");
		Check(c.Tribe_.Members()[1].Role != "forager", "the one already good at it takes the trade the clan lacks");
		Clan d(4);
		for (TribeMember& m : d.Tribe_.Members()) m.Role = "forager";
		for (auto& b : d.bodies) { b->Bag().Add(d.Items, "stone_axe", 1); b->Bag().Add(d.Items, "stone_pickaxe", 1); }
		d.Tribe_.Members()[1].Skills = { { "mining", 40.0f }, { "woodcutting", 40.0f } };
		d.Tribe_.ReviewRoles();
		Check(d.Tribe_.Members()[1].Role == c.Tribe_.Members()[1].Role, "the same review twice gives the same answer");
		Clan e(4);
		e.Tribe_.Config.Roles["forager"].Fixed = true;
		for (TribeMember& m : e.Tribe_.Members()) m.Role = "forager";
		Check(!e.Tribe_.ReviewRoles(), "members of a fixed role keep it");
		Clan two(2);
		Check(!two.Tribe_.ReviewRoles(), "a clan smaller than MinClan does not specialise");
	}

	// ---- a clan of twenty organises itself: every trade, many hands on the board, and it lives through the run
	{
		Clan big(20);
		big.Run(900.0);
		int dead = 0;
		std::map<std::string, int> trades;
		for (const TribeMember& m : big.Tribe_.Members()) { if (m.Dead) ++dead; ++trades[m.Role]; }
		std::fprintf(stderr, "  twenty: %s", big.Tribe_.JobsReport().c_str());
		Check(dead == 0, "nobody of the twenty dies in fifteen minutes");
		Check(trades.size() >= 3, "all three trades are held");
		Check(big.ClaimedBy.size() >= 8 && big.Claimed.count("gather") && big.Claimed.count("craft"), "many members took jobs from the board, gathering and making tools");
		Check(!big.Overbooked, "and no job was ever overbooked");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("work: ok");
	return 0;
}
