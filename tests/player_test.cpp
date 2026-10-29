// H15: the player inside the society. A scripted "player" (a member driven by orders given here, as the keys of the game mode would, never by the
// brain) is counted by its clan: the brain never sends its body anywhere, it eats from the store, what it carries to the store and the units of its
// claimed job count (its standing rises), what it gathers beside others brings them closer, gifts move bonds, and its proposals are accepted or
// rejected deterministically by its standing, its bonds and the clan's culture (an accepted want shows on the job board). It teaches a technology,
// marries, keeps its health above the floor when the rules make it immortal, trades with another clan at that clan's camp, and saving and loading
// keeps its membership, standing and proposals. The "player" and "controls" sections of assets/data/player.json say what the code says. CPU only.
#include "GB/Game/Clans.h"
#include "GB/Game/GameContent.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/Techs.h"
#include "GB/Gameplay/Combat.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
		else std::printf("ok: %s\n", what);
	}
	constexpr float dt = 1.0f / 30.0f;
	std::string ReadFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}
}

using namespace GB;

// The stand-in of tribe_test and society_test (it runs a task list against an ability system), which also counts the orders it was given and can
// be put somewhere: the player's body is moved by the test, as a person would walk it.
class FakeBody : public ITribeBody
{
public:
	FakeBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
	bool Busy() const override { return !m_Steps.empty(); }
	bool Failed() const override { return m_Failed; }
	glm::vec2 Position() const override { return m_Pos; }
	Inventory& Bag() override { return m_Bag; }
	void Teleport(const glm::vec2& p) override { m_Pos = p; }
	bool Do(const std::string& tasks, std::string& error) override
	{
		++Orders;
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
		if (me && m_Items) ResourceSystem::SyncTools(*me, m_Bag, *m_Items);
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
			if (!me || !me->Activate(s.Ability, m_Gameplay.Find(s.Node), nullptr, s.Node)) { m_Failed = true; m_Steps.clear(); return; }
			return;
		}
		if (s.Kind == "ability" && me && me->AnyActive()) return;
		m_Steps.erase(m_Steps.begin());
		m_Started = false;
	}
	const ItemLibrary* m_Items = nullptr;
	int Orders = 0;
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

// A clan of `n` NPCs on a patch of resources (as in society_test) and one player, "You", standing at the camp.
struct Town
{
	explicit Town(int n, glm::vec2 camp = { 0.0f, -8.0f }, const std::string& prefix = std::string(), bool techs = false, bool combat = false) : Camp(camp), UseCombat(combat)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		if (combat)   // H14: the fights of the world, and a bandit of a hostile band beside the camp
		{
			Combat.Bind(Entities, Gameplay);
			Hostility.FromRules(Combat.Rules);
			Hostility.Set("Clan", "Raiders", 1.0f);
			T.SetCombat(&Combat, &Hostility);
			Entity& bandit = *Entities.Find(Entities.Create("Bandit"));
			bandit.Gameplay.emplace();
			bandit.Transform.Position = { camp.x + 3.0f, 0.0f, camp.y };
		}
		const ResourceCounts c = ScaledResources(n);
		BuildResources(Entities, glm::vec3(camp.x, 0.0f, camp.y + 8.0f), Defs, c.Trees, c.Rocks, c.Bushes, c.Grass);
		if (techs) { Techs = DefaultTechs(); T.SetTechs(&Techs); }
		T.Bind(Entities, Gameplay, Items, Recipes, Defs);
		const std::vector<std::string> roles = T.PlanRoles(n);
		for (int i = 0; i <= n; ++i)
		{
			const std::string name = i == n ? Player : prefix + MemberName(T.Config.Work, static_cast<size_t>(i));
			Entity& e = *Entities.Find(Entities.Create(name));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Stoke" };
			Names.push_back(name);
		}
		Gameplay.Load(Entities);
		for (int i = 0; i <= n; ++i)
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
		T.Config.Camp = Camp;
		T.Store().Add(Items, "wood", 2 + 2 * n);
		T.Store().Add(Items, "stone", 3 * n);
		T.Store().Add(Items, "berry", 6);
		for (int i = 0; i < n; ++i) T.AddMember(Names[static_cast<size_t>(i)], roles[static_cast<size_t>(i)], bodies[static_cast<size_t>(i)].get());
		T.AddPlayer(Player, bodies.back().get());
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
			if (UseCombat) Combat.Update(dt);
			T.Update(dt);
		}
	}
	// The player's own orders (what the keys of the game mode do): walk there and use an ability, and wait for it to end.
	bool Act(const std::string& orders, double most = 60.0)
	{
		std::string error;
		if (!Me()->Do(orders, error)) return false;
		--Me()->Orders;   // (the test's orders are not the brain's)
		for (double t = 0.0; t < most && Me()->Busy(); t += 0.5) Run(0.5);
		return !Me()->Failed();
	}
	FakeBody* Me() { return bodies.back().get(); }
	FakeBody* Body(const std::string& name) { for (size_t i = 0; i < Names.size(); ++i) if (Names[i] == name) return bodies[i].get(); return nullptr; }
	const TribeMember* Member(const std::string& name) const { return T.FindMember(name); }
	std::string Player = "You";
	glm::vec2 Camp;
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	TechTree Techs;
	bool UseCombat = false;
	CombatSystem Combat;
	HostilityTable Hostility;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Fires;
	Tribe T;
	std::vector<std::string> Names;
	std::vector<std::unique_ptr<FakeBody>> bodies;
};

static void DataSections()
{
	const std::string text = ReadFile(std::string(GB_ASSETS_DIR) + "/data/player.json");
	Check(!text.empty(), "assets/data/player.json is there");
	PlayerRules rules;
	ControlRules controls;
	Check(LoadPlayerRules(text, rules).empty() && rules == DefaultPlayerRules(), "player.json's \"player\" section says what DefaultPlayerRules says");
	Check(LoadControls(text, controls).empty() && controls == DefaultControls(), "player.json's \"controls\" section says what DefaultControls says");
	PlayerRules back;
	ControlRules backControls;
	Check(LoadPlayerRules(PlayerRulesToJson(rules), back).empty() && back == rules, "the player rules go to JSON and back");
	Check(LoadControls(ControlsToJson(controls), backControls).empty() && backControls == controls, "the controls go to JSON and back");
	PlayerRules kept = DefaultPlayerRules();
	Check(!LoadPlayerRules("{\"player\":{\"reach\":\"far\"}}", kept).empty() && kept == DefaultPlayerRules(), "a wrong type is refused and changes nothing");
	Check(!LoadPlayerRules("{\"player\":{\"proposals\":{\"nope\":1}}}", kept).empty() && kept == DefaultPlayerRules(), "an unknown key is refused and changes nothing");
	ControlRules rebound = DefaultControls();
	Check(LoadControls("{\"controls\":{\"keys\":{\"interact\":\"Q\",\"help\":\"\"}}}", rebound).empty() && rebound.Keys["interact"] == "Q" && !rebound.Keys.count("help"), "a key is rebound and another unbound");
	ContentPacks packs;
	PlayerRules live;
	ControlRules liveControls;
	live.Leader = 5.0f;
	AddPlayerContentSections(packs, live, liveControls);
	Check(packs.Load("era1", std::string(GB_ASSETS_DIR) + "/data").empty() && live == DefaultPlayerRules() && liveControls == DefaultControls(), "the era1 pack brings the player and the controls through their sections");
}

static void CountedByTheClan()
{
	Town w(4);
	const TribeMember* me = w.Member(w.Player);
	Check(me && me->Player && w.T.Members().size() == 5, "the player is a member of the clan (5 with the four NPCs)");
	Check(w.T.PlayerMember() == me, "the clan says who its player is");
	w.T.SetViewer({ 500.0f, 500.0f });   // everyone far from the viewer: the NPCs go abstract, the player never
	w.Run(120.0);
	me = w.Member(w.Player);
	Check(w.Me()->Orders == 0, "in two minutes the brain gave the player's body no orders");
	Check(!me->Abstract && w.T.AbstractCount() > 0, "far from the viewer the NPCs are abstract and the player is not");
	Check(!w.T.NeedLevels(*me).empty(), "the player has needs");

	// meals from the store
	const int berries = w.T.Store().Count("berry");
	const auto eat = w.T.PlayerEat(w.Player);
	Check(eat.Ok && w.Member(w.Player)->Meals == 1, "the player eats at the camp");
	Check(w.T.Store().Count("berry") <= berries - 1 || w.T.Store().Count("berry") < berries, "its meal came out of the store");
	w.Me()->Teleport(w.Camp + glm::vec2(40.0f, 0.0f));
	Check(!w.T.PlayerEat(w.Player).Ok, "far from the camp with nothing in the bag it cannot eat");
	w.Me()->Teleport(w.Camp);

	// the job board: a claim, the player's own work and its delivery
	w.T.PostJobs();
	const float before = w.T.Standing(w.Player);
	w.Me()->Bag().Add(w.Items, "fiber", 6);
	std::string jobId;
	for (const Job* j : w.T.Board().Ordered()) if (j->Kind == "gather" && j->Target == "fiber" && w.T.Board().HasRoom(*j)) jobId = j->Id;
	if (jobId.empty())   // the clan may want no fibre right now: any gather job with room, and that item in the bag
		for (const Job* j : w.T.Board().Ordered()) if (j->Kind == "gather" && w.T.Board().HasRoom(*j)) { jobId = j->Id; w.Me()->Bag().Add(w.Items, j->Target, 6); break; }
	Check(!jobId.empty(), "the board has a gather job with room");
	const auto claim = w.T.PlayerClaim(w.Player, jobId);
	Check(claim.Ok && w.T.Board().ClaimOf(w.Player) && w.T.Board().ClaimOf(w.Player)->Id == jobId, "the player claims it");
	const std::string target = w.T.Board().ClaimOf(w.Player)->Target;
	const int stored = w.T.Store().Count(target);
	const auto dep = w.T.PlayerDeposit(w.Player);
	Check(dep.Ok && w.T.Store().Count(target) >= stored + 6, "what it carries goes to the store");
	const Job* held = w.T.Board().ClaimOf(w.Player);
	const auto claimOf = held ? std::find_if(held->Claims.begin(), held->Claims.end(), [&](const JobClaim& c) { return c.Member == w.Player; }) : std::vector<JobClaim>::const_iterator();
	Check(held && claimOf != held->Claims.end() && claimOf->Done >= 6.0f, "the board counts the six units of its job as done by the player");
	Check(w.T.Standing(w.Player) > before + 5.0f, "its standing rose with the deposit and the job");
	Check(w.Member(w.Player)->Deposited >= 6, "the clan counts what it deposited");
	Check(w.T.PlayerRelease(w.Player).Ok && !w.T.Board().ClaimOf(w.Player), "it lets the job go");

	// its own work, through its own orders: the gathers practise its aptitude and are counted
	const std::string node = "berry_bush_1";
	const int bag = w.Me()->Bag().Count("berry");
	Check(w.Act("ability Gather " + node), "the player walks to a berry bush and gathers (its orders, not the brain's)");
	const int got = w.Me()->Bag().Count("berry") - bag;
	Check(got > 0, "the berries are in its bag");
	Check(w.T.PlayerWorked(w.Player, "gather", "berry_bush", static_cast<float>(got)).Ok && w.T.SkillOf(*w.Member(w.Player), "foraging") > 0.0f, "the clan hears of the gather: its foraging rises");
	Check(w.Me()->Orders == 0, "still no order from the brain");

	// a gift moves the bond
	const std::string ana = w.Names.front();
	w.Me()->Teleport(w.T.Position(*w.Member(ana)));
	w.Me()->Bag().Add(w.Items, "berry", 3);
	const float bond = w.T.People().Affinity(w.T.Config.Society, ana, w.Player);
	const auto gift = w.T.PlayerGive(w.Player, ana, "berry", 3);
	Check(gift.Ok && w.Body(ana)->Bag().Count("berry") >= 3, "the player gives three berries to the first member");
	Check(w.T.People().Affinity(w.T.Config.Society, ana, w.Player) > bond + 0.1f, "the gift brings them closer (food counts double)");
	const auto talk = w.T.PlayerTalk(w.Player, ana);
	Check(talk.Ok && talk.Text.find(ana) == 0 && talk.Text.find("you") != std::string::npos, "talking says what the member does and how it feels about the player");
	std::printf("  talk: %s\n", talk.Text.c_str());
	w.Me()->Teleport(w.Camp);

	// immortal by the rules: hunger weakens, it does not kill
	if (AbilitySystemComponent* asc = w.Gameplay.Find(w.Player))
	{
		SatisfyNeed(*asc, NeedName::Hunger, -100.0f);
		SatisfyNeed(*asc, NeedName::Thirst, -100.0f);
	}
	w.Run(400.0);
	Check(!w.Member(w.Player)->Dead, "with the default rules the player does not die of hunger and thirst");
	std::printf("  %s\n", w.T.PlayerReport(w.Player).c_str());
}

static void Proposals()
{
	// standing 10, no bonds, an average culture: the clan says no to ten wood
	Town a(4);
	const auto no = a.T.Propose(a.Player, "gather", "wood", 10);
	Check(!no.Ok && no.Text.find("rejected") != std::string::npos, "a newcomer's proposal is rejected");
	std::printf("  %s\n", no.Text.c_str());
	Check(!a.T.Propose(a.Player, "gather", "wood", 10).Ok && a.T.Players().Proposals.size() == 1, "a second proposal at once waits for the cooldown");
	// the same in a second clan: the same answer
	Town b(4);
	const auto no2 = b.T.Propose(b.Player, "gather", "wood", 10);
	Check(no2.Text == no.Text, "the vote is deterministic: the same clan answers the same");
	// a player the clan likes: yes
	Town c(4);
	for (const std::string& n : c.Names) if (n != c.Player) c.T.People().Adjust(c.T.Config.Society, n, c.Player, 0.3f);
	const int stock = c.T.Stored("wood");
	const auto yes = c.T.Propose(c.Player, "gather", "wood", 10);
	Check(yes.Ok && yes.Text.find("accepted") != std::string::npos, "a well-liked player's proposal is accepted");
	std::printf("  %s\n", yes.Text.c_str());
	const Job* gather = c.T.Board().Find("gather wood");
	Check(gather && gather->Amount >= 9.0f, "the accepted want is on the job board: gather wood");
	if (gather) std::printf("  board: gather wood x%.0f (%s)\n", static_cast<double>(gather->Amount), gather->Why.c_str());
	// a leader: its standing alone carries the vote
	Town d(4);
	d.T.Credit(d.Player, "teach", 10.0f);   // ten lessons: 70 of 100
	Check(d.T.Leads(d.Player), "with standing 70 the player leads");
	Check(d.T.Propose(d.Player, "make", "cordage", 2).Ok, "a leader's proposal is accepted with no bonds");
	// the culture: a sharing clan follows, a selfish one does not (the same player)
	ProposalRules rules = DefaultPlayerRules().Proposals;
	rules.Culture = { { "sharing", 1.0f } };
	Check(ProposalVote(rules, 0.0f, 30.0f, 100.0f, false, { { "sharing", 0.8f } }, 0.05f) > 0.0f, "a sharing clan (0.8) agrees with a middling player");
	Check(ProposalVote(rules, 0.0f, 30.0f, 100.0f, false, { { "sharing", 0.2f } }, 0.05f) <= 0.0f, "a selfish clan (0.2) does not");
	// the want is met: the proposal is done and the economy forgets it
	c.T.Store().Add(c.Items, "wood", stock + 12);
	c.Run(2.0);
	Check(!c.T.Players().Proposals.empty() && c.T.Players().Proposals.back().Status == "done", "once the store holds the wood the proposal is done");
}

static void Lessons()
{
	Town w(4, { 0.0f, -8.0f }, std::string(), true);
	const std::string ana = w.Names.front();
	std::string tech;
	for (const auto& [id, def] : w.Techs.Techs)
		if (!w.T.Knowledge().Knows(w.Player, id) && w.T.Knowledge().CanLearn(ana, id) && w.T.Knowledge().CanLearn(w.Player, id)) { tech = id; break; }
	Check(!tech.empty(), "a technology neither knows yet");
	w.T.Knowledge().Grant(w.Player, tech);
	w.Me()->Teleport(w.T.Position(*w.Member(ana)));
	const float before = w.T.Standing(w.Player);
	const auto lesson = w.T.PlayerTeach(w.Player, ana, tech);
	const AgentKnowledge* k = w.T.Knowledge().Of(ana);
	Check(lesson.Ok && k && (k->Known.count(tech) || (k->Progress.count(tech) && k->Progress.at(tech) > 0.0f)), "the player teaches it: the member understands some of it");
	Check(w.T.Standing(w.Player) > before, "a lesson raises the player's standing");
	std::printf("  %s\n", lesson.Text.c_str());

	// marriage: not before the bond, then yes
	Check(!w.T.PlayerMarry(w.Player, ana).Ok, "a member who hardly knows the player does not marry it");
	w.T.People().Adjust(w.T.Config.Society, ana, w.Player, 0.5f);
	Check(w.T.PlayerMarry(w.Player, ana).Ok && w.T.People().Find(w.Player)->Partner == ana, "a member who loves the player becomes its partner");
}

static void SaveLoad()
{
	Town w(4);
	for (const std::string& n : w.Names) if (n != w.Player) w.T.People().Adjust(w.T.Config.Society, n, w.Player, 0.3f);
	w.T.Propose(w.Player, "gather", "fiber", 6);
	w.Me()->Bag().Add(w.Items, "wood", 4);
	w.T.PlayerDeposit(w.Player);
	w.Run(20.0);
	const std::string saved = w.T.ToJson();

	Town again(4);
	// the members as the editor makes them again before a load: by name and role, the player as a plain member (the save says it is the player)
	again.T.Clear();
	for (const auto& [name, role] : Tribe::SavedMembers(saved)) again.T.AddMember(name, role, again.Body(name));
	Check(!again.T.FindMember(again.Player)->Player, "before the load the member is not a player");
	Check(again.T.FromJson(saved, false), "the tribe with its player loads");
	Check(again.T.FindMember(again.Player)->Player, "after the load it is the player again");
	Check(again.T.Players() == w.T.Players(), "its standing, what it did and its proposals are the same");
	Check(again.T.ToJson() == saved, "saving again gives the same text");
	// a save from before H15 (no "player" anywhere) loads with nobody as player
	Town plain(4);
	plain.T.Clear();
	for (const std::string& n : plain.Names) if (n != plain.Player) plain.T.AddMember(n, "forager", plain.Body(n));
	const std::string old = plain.T.ToJson();
	Town older(4);
	older.T.Clear();
	for (const auto& [name, role] : Tribe::SavedMembers(old)) older.T.AddMember(name, role, older.Body(name));
	Check(old.find("\"player\"") == std::string::npos && older.T.FromJson(old, false) && !older.T.PlayerMember(), "a save with no player loads, and nobody is the player");
}

static void TradeWithAnotherClan()
{
	Town home(4, { 0.0f, -8.0f });
	Town other(4, { 120.0f, -8.0f }, "B");
	home.T.Store().Remove("berry", home.T.Store().Count("berry"));   // the home clan lacks food ...
	other.T.Store().Add(other.Items, "berry", 30);                    // ... the other has plenty
	other.T.Store().Remove("stone", other.T.Store().Count("stone"));
	home.T.Store().Add(home.Items, "stone", 30);
	ClanWorld clans;
	clans.Add("Red", &home.T);
	clans.Add("Blue", &other.T);
	const auto far = clans.PlayerVisit(home.Player, 1, "trade");
	Check(!far.empty(), "the player has to go to the other clan's camp first");
	home.Me()->Teleport(other.Camp + glm::vec2(3.0f, 0.0f));
	std::string outcome;
	const float standing = home.T.Standing(home.Player);
	const auto why = clans.PlayerVisit(home.Player, 1, "trade", &outcome);
	std::printf("  trade: %s%s\n", why.c_str(), outcome.c_str());
	Check(why.empty() && home.T.Store().Count("berry") > 0, "at their camp the player trades: berries come to the home store");
	Check(clans.Attitude(0, 1) > 0.0f, "the two clans think better of each other");
	Check(home.T.Standing(home.Player) > standing, "being the clan's envoy raises the player's standing");
	clans.Clear();
}

static void Fights()
{
	Town w(3, { 0.0f, -8.0f }, std::string(), false, true);
	w.Run(1.0);   // (the members join the fights with the conflict clock)
	Check(w.Combat.Join("Bandit", "Raiders", true, 40.0f) != nullptr, "a bandit of a hostile band stands by the camp");
	const auto attack = w.T.PlayerAttack(w.Player);
	std::printf("  %s\n", attack.Text.c_str());
	Check(attack.Ok && attack.Text.find("Bandit") != std::string::npos, "the player attacks the nearest foe (through the combat's abilities)");
	Check(w.T.Standing(w.Player) > w.T.Config.Player.StandingStart, "standing up to a raider at the camp counts as defending it");
	w.Run(30.0);
	const std::string condition = w.T.PlayerCondition(w.Player);
	std::printf("  %s | %s\n", condition.c_str(), w.Combat.Status("Bandit").c_str());
	Check(condition.find("health") == 0, "the interface reads the player's health, stamina and wounds");
	Check(w.Combat.Find(w.Player) && (w.Combat.Find(w.Player)->Landed + w.Combat.Find(w.Player)->Missed + w.Combat.Find(w.Player)->Blocked) > 0, "the player swung at the bandit");
	// a blow that would kill: the player falls and wakes at the camp (PlayerRules::Mortal is false)
	const float standing = w.T.Standing(w.Player);
	w.Me()->Teleport(w.Camp + glm::vec2(20.0f, 0.0f));
	w.Combat.Hurt(w.Player, 500.0f, "cut", 1.0f, "Bandit");
	w.Run(2.0);
	const bool woke = std::any_of(w.T.Log().begin(), w.T.Log().end(), [](const std::string& l) { return l.find("wakes at the camp") != std::string::npos; });
	Check(!w.Member(w.Player)->Dead && woke, "a deadly blow knocks the player out: it wakes at the camp, alive");
	Check(glm::distance(w.Me()->Position(), w.Camp) < 5.0f && w.T.Standing(w.Player) < standing, "it is carried home and the clan thinks a little less of it");
	Check(w.T.PlayerCondition(w.Player).find("cut") == std::string::npos, "its wounds were tended");
	// mortal by the rules: it dies like anyone
	Town m(3, { 0.0f, -8.0f }, std::string(), false, true);
	m.T.Config.Player.Mortal = true;
	m.Run(1.0);
	m.Combat.Hurt(m.Player, 500.0f, "cut", 1.0f, "Bandit");
	m.Run(2.0);
	Check(m.Member(m.Player)->Dead, "with mortal players a deadly blow kills (the game mode then says the game is over)");
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	DataSections();
	CountedByTheClan();
	Proposals();
	Lessons();
	SaveLoad();
	TradeWithAnotherClan();
	Fights();
	if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
	std::printf("player: all checks passed\n");
	return 0;
}
