// Social life (docs/VIDA_SOCIAL.md). Checked: the "social" section of assets/data/social.json says what the code says, round-trips and is
// refused whole on a mistake, and the era1 pack loads strictly with social.json (new foods, nodes, recipes, technologies, species and hunts);
// conversations move bonds and moods, and two runs are identical; a death brings a funeral that eases the grief of the partner; the winter
// solstice gathers the clan in a circle; the taboo of the sacred place keeps the hunters off the game there; a varied diet lifts the mood; a
// group hunt kills a deer and its butchering gives bone, sinew and fat; hunting empties the rabbits near the camp where a quiet camp keeps them;
// clans with shared beliefs come to trust each other (and opposed ones do not); a rite saved in the middle goes on after loading; a snare
// catches a rabbit; and the QA invariants (social, combat, deaths) hold throughout. CPU only, stand-in bodies as in combat_test.
#include "GB/Game/Clans.h"
#include "GB/Game/GameContent.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Gameplay/ContentPack.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
	const std::string kData = std::string(GB_ASSETS_DIR) + "/data";
}

using namespace GB;

// The stand-in body of tribe_test: it runs the text of a task list (goto and wait take time; "ability A node" walks there and activates it).
class FakeBody : public ITribeBody
{
public:
	FakeBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
	bool Busy() const override { return !m_Steps.empty(); }
	bool Failed() const override { return m_Failed; }
	glm::vec2 Position() const override { return m_Pos; }
	Inventory& Bag() override { return m_Bag; }
	void Teleport(const glm::vec2& at) override { m_Pos = at; }
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
		if (!me) return;
		ResourceSystem::SyncTools(*me, m_Bag, *m_Items);
		if (m_Steps.empty()) return;
		Step& s = m_Steps.front();
		const float speed = 3.0f * std::max(0.3f, me->Attributes().Has(NeedName::Vigor) ? me->Attribute(NeedName::Vigor) : 1.0f);
		if (!m_Started)
		{
			m_Started = true;
			if (s.Kind == "goto") m_Target = { s.X, s.Z };
			if (s.Kind == "ability")
			{
				const EntityId id = m_Entities.FindByName(s.Node);
				const glm::vec4 p = id == NullEntity ? glm::vec4(0.0f) : m_Entities.WorldMatrix(id)[3];
				m_Target = { p.x, p.z };
				s.Walking = true;
			}
		}
		if (s.Kind == "goto" || (s.Kind == "ability" && s.Walking))
		{
			const glm::vec2 to = m_Target - m_Pos;
			const float d = glm::length(to);
			if (d > speed * step) { m_Pos += to / d * speed * step; return; }
			m_Pos = m_Target;
		}
		else if (s.Kind == "wait" && s.Seconds > 0.0f) { s.Seconds -= step; if (s.Seconds > 0.0f) return; }
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

// A world with era 1's content plus social.json (foods, nodes, recipes, species, hunts), combat, an ecology and clans with a social life.
struct World
{
	World()
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		Check(LoadContentFile(kData + "/social.json", Items, Recipes, Defs).empty(), "(the world: social.json's items, recipes and nodes load)");
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		Check(LoadCombatRulesFile(kData + "/social.json", Combat.Rules).empty(), "(the world: social.json's hunts load)");
		Combat.Bind(Entities, Gameplay);
		Hostility.FromRules(Combat.Rules);
		Check(LoadSpeciesFile(kData + "/social.json", Ecology.Species).empty(), "(the world: social.json's species load)");
		Ecology.Bind(Entities, Gameplay);
		Resources.Bind(Gameplay, Defs, [this](const std::string& who, const std::string& item, int k)
		{
			if (FakeBody* b = Body(who)) b->Bag().Add(Items, item, k);
		});
		Fires.Items = &Items;
		Fires.Bind(Gameplay, [this](const std::string& who, const std::string& item, int k)
		{
			FakeBody* b = Body(who);
			return b ? b->Bag().Remove(item, k) : 0;
		}, [](const std::string&, const std::string&, int) { return 0; });
	}
	~World()
	{
		if (!std::getenv("GB_SOCIAL_LOG")) return;   // GB_SOCIAL_LOG=1: every clan's log, for a person to read
		for (const auto& t : Clans) { std::fprintf(stderr, "---- %s\n", t->ClanName().c_str()); Dump(*t); }
	}
	void Patch(const glm::vec3& at, int n)
	{
		const ResourceCounts c = ScaledResources(n);
		BuildResources(Entities, at, Defs, c.Trees, c.Rocks, c.Bushes, c.Grass);
	}
	// A clan of `n` at `camp` named `clan` with the social rules of the code (DefaultSocialRules, what social.json says).
	Tribe& AddClan(const std::string& clan, int n, const glm::vec2& camp, const std::string& prefix, bool social = true)
	{
		Clans.push_back(std::make_unique<Tribe>());
		Tribe& t = *Clans.back();
		t.SetClanName(clan);
		t.Bind(Entities, Gameplay, Items, Recipes, Defs);
		t.Config.Camp = camp;
		t.Config.Work.Jobs.SurvivalDays = 0.0f;   // (these tests are about talk, rites and the hunt: the pyramid of needs has its own test)
		t.Config.Work.Jobs.CriticalHunger = 0.0f;
		if (social) t.Config.Social = DefaultSocialRules();
		t.SetCombat(&Combat, &Hostility);
		const std::vector<std::string> roles = t.PlanRoles(std::max(1, n));
		std::vector<std::string> names;
		for (int i = 0; i < n; ++i)
		{
			const std::string name = prefix + MemberName(t.Config.Work, static_cast<size_t>(i));
			Entity& e = *Entities.Find(Entities.Create(name));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke" };
			names.push_back(name);
		}
		if (n > 0) { Gameplay.Store(Entities); Gameplay.Load(Entities); }
		for (int i = 0; i < n; ++i)
		{
			Bodies.push_back(std::make_unique<FakeBody>(names[static_cast<size_t>(i)], Gameplay, Entities, camp + glm::vec2(static_cast<float>(i % 5) * 0.7f, static_cast<float>(i / 5) * 0.7f)));
			Bodies.back()->m_Items = &Items;
			BodyNames.push_back(names[static_cast<size_t>(i)]);
			t.AddMember(names[static_cast<size_t>(i)], roles[static_cast<size_t>(i)], Bodies.back().get());
		}
		return t;
	}
	FakeBody* Body(const std::string& name)
	{
		for (size_t i = 0; i < BodyNames.size(); ++i) if (BodyNames[i] == name) return Bodies[i].get();
		return nullptr;
	}
	void Run(double seconds)
	{
		const int frames = static_cast<int>(seconds / dt);
		for (int i = 0; i < frames; ++i)
		{
			Clock += static_cast<double>(dt);
			Gameplay.Update(dt);
			Resources.Update(Gameplay, dt);
			for (auto& b : Bodies) b->Update(dt);
			Fires.Update(dt, &Entities);
			Ecology.Update(Clock, dt);
			Combat.Update(dt);
			for (auto& t : Clans) t->Update(dt);
			if (i % 10 == 0)
			{
				while (Monitors.size() < Clans.size()) Monitors.push_back(std::make_unique<SimMonitor>());
				for (size_t k = 0; k < Clans.size(); ++k)
				{
					SimView view;
					view.Clan = Clans[k].get();
					view.Gameplay = &Gameplay;
					view.Entities = &Entities;
					view.Clock = Clock;
					Monitors[k]->Step(view);
				}
			}
		}
	}
	int Broken(const std::string& check) const
	{
		int n = 0;
		for (const auto& mon : Monitors)
			for (const SimFinding& f : mon->Findings())
				if (f.Check == check) { ++n; std::fprintf(stderr, "  [%s] %s: %s\n", f.Check.c_str(), f.Subject.c_str(), f.What.c_str()); }
		return n;
	}
	bool Logged(const Tribe& t, const std::string& part) const
	{
		return std::any_of(t.Log().begin(), t.Log().end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
	}
	void Dump(const Tribe& t, const std::string& filter = std::string()) const
	{
		for (const std::string& l : t.Log()) if (filter.empty() || l.find(filter) != std::string::npos) std::fprintf(stderr, "  %s\n", l.c_str());
	}
	int Count(const std::string& species) const { return Ecology.Count(species); }
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Fires;
	EcologySystem Ecology;
	CombatSystem Combat;
	HostilityTable Hostility;
	std::vector<std::unique_ptr<Tribe>> Clans;
	std::vector<std::unique_ptr<FakeBody>> Bodies;
	std::vector<std::unique_ptr<SimMonitor>> Monitors;
	std::vector<std::string> BodyNames;
	double Clock = 0.0;
};

namespace
{
	float MeanMood(const Tribe& t)
	{
		float sum = 0.0f;
		int n = 0;
		for (const TribeMember& m : t.Members()) if (!m.Dead) { sum += t.Mood(m.Name); ++n; }
		return n ? sum / static_cast<float>(n) : 0.0f;
	}
	int Held(const Tribe& t, const std::string& rite)
	{
		const auto it = t.Social().Counts.find("rite." + rite);
		return it == t.Social().Counts.end() ? 0 : it->second;
	}
	// the clan's beliefs set by hand (instead of the draw from its name)
	void Believe(Tribe& t, float animism, float ancestors, float sky)
	{
		t.Social().Beliefs = { { "animism", animism }, { "ancestors", ancestors }, { "sky", sky } };
	}
	// a quiet clan: no rites of the seasons nor talk, only what the test is about
	void Quiet(Tribe& t, bool talk, bool rites)
	{
		if (!talk) t.Config.Social.Talk.Kinds.clear();
		if (!rites) t.Config.Social.Rites.clear();
		t.Config.Social.Beliefs.Omens.clear();
	}
}

int main()
{
	std::fprintf(stderr, "-- %s\n", "the data: the social section of social.json says what the ");
	// ---- the data: the "social" section of social.json says what the code says, round-trips, and a mistake changes nothing
	{
		SocialRules file;
		Check(LoadSocialRulesFile(kData + "/social.json", file).empty(), "the social section of assets/data/social.json loads");
		Check(file == DefaultSocialRules(), "and says what DefaultSocialRules says");
		if (!(file == DefaultSocialRules())) std::fprintf(stderr, "  expected:\n{ \"social\": %s }\n", SocialRulesToJson(DefaultSocialRules()).c_str());
		SocialRules back;
		Check(LoadSocialRules("{ \"social\": " + SocialRulesToJson(DefaultSocialRules()) + " }", back).empty() && back == DefaultSocialRules(), "the social section round-trips");
		SocialRules keep = DefaultSocialRules();
		Check(!LoadSocialRules(R"({ "social": { "mood": { "start": 0.4 }, "talk": { "radius": -1 } } })", keep).empty() && keep == DefaultSocialRules(), "a bad number is refused and nothing changes (not even the part before it)");
		Check(!LoadSocialRules(R"({ "social": { "parties": {} } })", keep).empty() && !LoadSocialRules(R"({ "social": { "mood": { "joy": 1 } } })", keep).empty(), "an unknown part or key is refused");
		Check(!LoadSocialRules(R"({ "social": { "rites": { "dance": { "event": "birth", "beliefs": { "thunder": 1 } } } } })", keep).empty(), "a rite that honours an unknown belief is refused");
		Check(!LoadSocialRules(R"({ "social": { "rites": { "dance": { "season": "monsoon" } } } })", keep).empty(), "a rite of an unknown season is refused");
		Check(LoadSocialRules(R"({ "items": [] })", keep).empty() && keep == DefaultSocialRules(), "a file without the section changes nothing");
		SocialRules more = DefaultSocialRules();
		Check(LoadSocialRules(R"({ "social": { "rites": { "dance": { "name": "Rain dance", "season": "summer", "at": 0.2 } } } })", more).empty() && more.Rites.size() == DefaultSocialRules().Rites.size() + 1
			&& more.Rites["funeral"] == DefaultSocialRules().Rites["funeral"], "a pack adds a rite (the others stay)");
		SocialRules off;
		Check(!off.On && DefaultSocialRules().On && TribeConfig().Social == SocialRules(), "a plain tribe has no social life; the rules of the content turn it on");
		CombatRules hunts = DefaultCombatRules();
		Check(LoadCombatRulesFile(kData + "/social.json", hunts).empty() && hunts.Hunts.count("deer") && hunts.Hunts["deer"].Group == 2 && hunts.Hunts["deer"].Yields.at("bone") == 2
			&& hunts.Hunts["rabbit"] == DefaultCombatRules().Hunts["rabbit"], "social.json adds deer (a group hunt that yields bone), boar and grouse to the hunts");
		CombatRules hb;
		Check(LoadCombatRules("{ \"combat\": " + CombatRulesToJson(hunts) + " }", hb).empty() && hb == hunts, "and the hunts with their butchering round-trip");
		// the pack: era1 loads strictly with social.json (its foods and gear can be had, its technologies discovered)
		ContentPacks packs;
		ItemLibrary pi; RecipeBook pr; std::map<std::string, ResourceNodeDef> pn; std::map<std::string, TribeRole> roles;
		std::map<std::string, BuildingDef> buildings;
		TechTree techs;
		SocialRules live;
		CombatRules combat;
		std::map<std::string, InteractiveDef> interactives;
		std::map<std::string, SpeciesDef> species = DefaultSpecies();
		RegisterEraOneItems(pi, pr, pn);
		AddGameContentSections(packs, pi, pr, pn, roles, nullptr, nullptr, nullptr, &techs, &buildings);
		AddCombatContentSection(packs, combat);
		AddWorldContentSections(packs, interactives, species);
		AddSocialContentSection(packs, live);
		ContentReport report;
		ContentPacks::Options strict;
		strict.Strict = true;
		const std::string error = packs.Load("era1", kData, &report, strict);
		Check(error.empty() && report.Clean(), "the era1 pack loads strictly with social.json");
		if (!error.empty() || !report.Clean()) std::fprintf(stderr, "  %s\n  %s\n", error.c_str(), report.Text().c_str());
		Check(live == DefaultSocialRules(), "and the social section it read is the default one");
		Check(pi.Find("stew") && pi.Find("fish") && pn.count("fishing_spot") && techs.Find("trapping") && species.count("deer") && combat.Hunts.count("boar"),
			"with the new foods, the fishing spot, trapping, the deer and the boar hunt");
		Check(CheckCombat(combat, pi).empty(), "every item a hunt yields is an item");
		Check(pi.Find("stew")->Perishable() && !pi.Find("honey")->Perishable(), "a stew spoils, honey does not");
	}

	std::fprintf(stderr, "-- %s\n", "conversations: they move bonds and moods, and two runs are t");
	// ---- conversations: they move bonds and moods, and two runs are the same
	{
		auto run = [](std::string& state, std::string& bonds, int& talks, float& mood)
		{
			World w;
			w.Patch({ 0.0f, 0.0f, 0.0f }, 5);
			Tribe& clan = w.AddClan("Clan", 5, { 0.0f, -8.0f }, "");
			Quiet(clan, true, false);
			clan.Store().Add(w.Items, "berry", 20);
			clan.Store().Add(w.Items, "wood", 10);
			clan.Store().Add(w.Items, "stone", 12);
			w.Run(1300.0);   // (a day and its evening: the time for talk)
			state = clan.Social().ToJson();
			bonds.clear();
			for (const TribeMember& m : clan.Members()) bonds += clan.RelationsReport(m.Name);
			talks = 0;
			for (const auto& [k, n] : clan.Social().Counts) if (k.rfind("talk.", 0) == 0) talks += n;
			mood = MeanMood(clan);
			if (talks == 0) w.Dump(clan);
			Check(w.Broken("social") == 0, "the social invariants hold while they talk");
		};
		std::string s1, b1, s2, b2;
		int t1 = 0, t2 = 0;
		float m1 = 0.0f, m2 = 0.0f;
		run(s1, b1, t1, m1);
		run(s2, b2, t2, m2);
		Check(t1 >= 4, "the members talk to each other (four times or more in a day)");
		Check(m1 > 0.5f, "and the talks lift their mood");
		Check(b1.find("+") != std::string::npos || b1.find("0.") != std::string::npos, "and leave bonds between them");
		Check(s1 == s2 && b1 == b2 && t1 == t2, "two runs are the same, talk by talk");
	}

	std::fprintf(stderr, "-- %s\n", "consoling: one who grieves is consoled by a friend");
	// ---- consoling: one who grieves is consoled by a friend
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 3);
		Tribe& clan = w.AddClan("Clan", 3, { 0.0f, -8.0f }, "");
		Quiet(clan, true, false);
		clan.Store().Add(w.Items, "berry", 20);
		const std::string sad = clan.Members()[1].Name;
		w.Run(1.0);
		clan.Social().Members[sad].Grief = 0.8f;
		w.Run(1100.0);   // (the evening at the camp comes; before the day ends and grief fades by itself)
		const int consoled = clan.Social().Counts.count("talk.console") ? clan.Social().Counts.at("talk.console") : 0;
		Check(consoled >= 1 && w.Logged(clan, "consoles " + sad), "a grieving member is consoled");
		Check(clan.Grief(sad) < 0.6f, "and its grief eases");
	}

	std::fprintf(stderr, "-- %s\n", "a death: grief for its partner, and a funeral at the sacred ");
	// ---- a death: grief for its partner, and a funeral at the sacred place that eases it
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		Quiet(clan, false, true);
		for (auto it = clan.Config.Social.Rites.begin(); it != clan.Config.Social.Rites.end();) it = it->first == "funeral" ? std::next(it) : clan.Config.Social.Rites.erase(it);
		clan.Store().Add(w.Items, "berry", 20);
		const std::string a = clan.Members()[0].Name, b = clan.Members()[1].Name;
		Check(clan.People().Marry(clan.Config.Society, a, b), "(two of them are partners)");
		w.Run(5.0);
		w.Gameplay.Find(a)->Tags().Add("Death.Wounds");   // (as a fight would leave it: a cause the QA knows)
		w.Gameplay.Find(a)->Tags().Add("State.Dead");
		w.Run(2.0);
		Check(clan.Members()[0].Dead, "(one dies)");
		const float grief = clan.Grief(b);
		Check(grief >= 0.6f, "its partner grieves");
		bool funeral = false;
		for (int i = 0; i < 200 && !funeral; ++i) { w.Run(1.0); funeral = Held(clan, "funeral") > 0; }
		Check(funeral && w.Logged(clan, "holds Funeral for " + a), "the clan holds a funeral for the dead");
		if (!funeral) w.Dump(clan);
		Check(clan.Grief(b) < grief - 0.2f, "and it eases the partner's grief");
		Check(!clan.Social().History.empty() && clan.Social().History.back().find("Funeral") != std::string::npos, "the rite is in the clan's history");
		Check(w.Broken("social") == 0 && w.Broken("deaths") == 0, "the invariants hold through the funeral");
	}

	std::fprintf(stderr, "-- %s\n", "the winter solstice gathers the clan in a circle at the fire");
	// ---- the winter solstice gathers the clan in a circle at the fire
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 5);
		Tribe& clan = w.AddClan("Clan", 5, { 0.0f, -8.0f }, "");
		Quiet(clan, false, true);
		clan.Store().Add(w.Items, "berry", 20);
		clan.Store().Add(w.Items, "wood", 14);
		clan.Store().Add(w.Items, "stone", 12);
		const float before = MeanMood(clan);
		bool held = false;
		for (int i = 0; i < 1000 && !held; ++i) { w.Run(1.0); held = Held(clan, "solstice") > 0; }
		Check(held && w.Logged(clan, "calls everyone to Winter solstice"), "the clan calls everyone to the winter solstice");
		if (!held) w.Dump(clan);
		const std::string& h = clan.Social().History.back();
		int there = 0, of = 0;
		std::sscanf(h.substr(h.find(": ") + 2).c_str(), "%d of %d", &there, &of);
		Check(h.find("Winter solstice") != std::string::npos && of == 5 && there >= 3, "most of the clan stands in the circle");
		Check(MeanMood(clan) > before, "and the festival lifts the mood");
		Check(w.Broken("social") == 0, "the social invariants hold through the festival");
	}

	std::fprintf(stderr, "-- %s\n", "the taboo of the sacred place: a clan that keeps it does not");
	// ---- the taboo of the sacred place: a clan that keeps it does not hunt the rabbits there; one that does not, does
	{
		auto hunted = [](bool taboo)
		{
			World w;
			w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
			Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
			Quiet(clan, false, false);
			clan.Store().Add(w.Items, "wood", 10);
			clan.Store().Add(w.Items, "stone", 12);
			Believe(clan, taboo ? 0.9f : 0.1f, 0.2f, 0.4f);
			clan.Config.Social.Beliefs.Taboos["sacred_hunt"].Radius = 80.0f;   // (the whole valley is sacred: the rabbits and their young wander)
			clan.Social().Sacred = { 8.0f, -14.0f };
			clan.Social().HasSacred = true;
			w.Ecology.Spawn({ 8.0f, 0.0f, -14.0f }, 3, "rabbit");
			for (int i = 0; i < 240 && clan.Conflict().Hunted.empty(); ++i) w.Run(1.0);
			const int n = clan.Conflict().Hunted.count("Animal.Rabbit") ? clan.Conflict().Hunted.at("Animal.Rabbit") : 0;
			if (taboo && n > 0) { w.Dump(clan, "unt"); std::fprintf(stderr, "  allowed at the sacred place: %d\n", clan.HuntAllowed({ 8.0f, -14.0f }) ? 1 : 0); }
			return n;
		};
		Check(hunted(false) >= 1, "a clan that does not keep the taboo hunts the rabbits by its sacred place");
		Check(hunted(true) == 0, "a clan that keeps it leaves them alone");
	}

	std::fprintf(stderr, "-- %s\n", "a varied diet lifts the mood more than a dull one");
	// ---- a varied diet lifts the mood more than a dull one
	{
		auto mood = [](bool varied)
		{
			World w;
			const ResourceCounts c = ScaledResources(3);
			BuildResources(w.Entities, { 0.0f, 0.0f, 0.0f }, w.Defs, c.Trees, c.Rocks, 0, c.Grass);   // (no berry bushes: they eat from the store)
			Tribe& clan = w.AddClan("Clan", 3, { 0.0f, -8.0f }, "");
			Quiet(clan, false, false);
			clan.Config.Social.Diet.Window = 3;
			if (varied) for (const char* f : { "berry", "roots", "nuts", "eggs", "honey" }) clan.Store().Add(w.Items, f, 3);
			else clan.Store().Add(w.Items, "berry", 30);
			w.Run(1800.0);
			const int meals = clan.Social().Counts.count(varied ? "meal.varied" : "meal.monotonous") ? clan.Social().Counts.at(varied ? "meal.varied" : "meal.monotonous") : 0;
			Check(meals > 0, varied ? "varied meals are counted" : "monotonous meals are counted");
			return MeanMood(clan);
		};
		const float dull = mood(false), rich = mood(true);
		Check(rich > 0.5f && dull < 0.5f && rich > dull, "a varied diet lifts the mood, a monotonous one lowers it");
	}

	std::fprintf(stderr, "-- %s\n", "a group hunt: two or more go after a deer (one drives it), k");
	// ---- a group hunt: two or more go after a deer (one drives it), kill it, and the butchering gives meat, hide, bone, sinew and fat
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		Quiet(clan, false, false);
		Believe(clan, 0.2f, 0.2f, 0.4f);   // (no taboo: the kill stays with the hunter)
		clan.Config.Social.Beliefs.Omens = DefaultSocialRules().Beliefs.Omens;   // (a great kill is an omen)
		clan.Config.Social.Beliefs.Omens.erase("eclipse");
		clan.Store().Add(w.Items, "wood", 10);
		clan.Store().Add(w.Items, "stone", 12);
		Check(w.Ecology.Spawn({ 10.0f, 0.0f, -18.0f }, 1, "deer") == 1, "(a deer near the camp)");
		bool posted = false;
		for (int i = 0; i < 400 && !clan.Conflict().Hunted.count("Animal.Deer"); ++i)
		{
			w.Run(1.0);
			for (const auto& [id, job] : clan.Board().Jobs()) posted = posted || (job.Kind == "hunt" && job.Target == "Animal.Deer");
		}
		Check(posted, "the clan posts a deer hunt when it has no food");
		Check(clan.Conflict().Hunted.count("Animal.Deer") == 1, "and kills the deer");
		Check(w.Logged(clan, "drives Deer") && w.Logged(clan, "waits for Deer") && w.Logged(clan, "with the spear"), "in a group hunt: one drives it, another waits with the spear");
		if (!clan.Conflict().Hunted.count("Animal.Deer") || !w.Logged(clan, "drives Deer")) w.Dump(clan, "unt");
		std::map<std::string, int> got;
		for (const char* item : { "meat", "hide", "bone", "sinew", "fat" })
		{
			got[item] = clan.Store().Count(item);
			for (const TribeMember& m : clan.Members()) got[item] += m.Body->Bag().Count(item);
		}
		Check(got["bone"] == 2 && got["sinew"] == 2 && got["fat"] == 1 && got["hide"] == 2, "the butchering gives 2 bone, 2 sinew, 1 fat and 2 hides");
		Check(w.Logged(clan, "reads an omen: A great kill"), "a great kill is a good omen");
		Check(w.Broken("combat") == 0 && w.Broken("social") == 0, "the combat and social invariants hold through the hunt");
	}

	std::fprintf(stderr, "-- %s\n", "overhunting: a hungry clan empties the rabbits near its camp");
	// ---- overhunting: a hungry clan empties the rabbits near its camp, where a fed one leaves them
	{
		auto rabbits = [](bool hungry)
		{
			World w;
			w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
			Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
			Quiet(clan, false, false);
			Believe(clan, 0.2f, 0.2f, 0.4f);
			clan.Store().Add(w.Items, "wood", 10);
			clan.Store().Add(w.Items, "stone", 12);
			if (!hungry) clan.Store().Add(w.Items, "berry", 40);
			w.Ecology.Spawn({ 8.0f, 0.0f, -14.0f }, 4, "rabbit");
			w.Run(600.0);
			return w.Count("rabbit");
		};
		const int fed = rabbits(false), hunted = rabbits(true);
		Check(hunted < fed && hunted <= 1, "hunting empties the rabbits near the camp (overhunting)");
		std::fprintf(stderr, "  rabbits after 600 s: fed clan %d, hungry clan %d\n", fed, hunted);
	}

	std::fprintf(stderr, "-- %s\n", "beliefs between clans: shared beliefs make trust, opposed on");
	// ---- beliefs between clans: shared beliefs make trust, opposed ones distrust; meeting brings them closer
	{
		World w;
		Tribe& a = w.AddClan("Hill", 2, { 0.0f, 0.0f }, "H");
		Tribe& b = w.AddClan("River", 2, { 60.0f, 0.0f }, "R");
		Tribe& c = w.AddClan("Lake", 2, { -60.0f, 0.0f }, "L");
		Believe(a, 0.8f, 0.7f, 0.2f);
		Believe(b, 0.8f, 0.7f, 0.2f);
		Believe(c, 0.1f, 0.1f, 0.9f);
		ClanWorld clans;
		const ClanId ia = clans.Add("Hill", &a).Id, ib = clans.Add("River", &b).Id, ic = clans.Add("Lake", &c).Id;
		const float before = std::abs(a.Social().Beliefs["sky"] - c.Social().Beliefs["sky"]);
		for (int i = 0; i < 6; ++i) { clans.ShareBeliefs(ia, ib); clans.ShareBeliefs(ia, ic); }
		Check(clans.Trust(ia, ib) > 0.0f && clans.Attitude(ia, ib) > 0.0f, "clans with shared beliefs come to trust each other");
		Check(clans.Trust(ia, ic) == 0.0f && clans.Attitude(ia, ic) < 0.0f, "clans with opposed beliefs do not");
		std::fprintf(stderr, "  shared: trust %.3f attitude %.3f; opposed: trust %.3f attitude %.3f\n", clans.Trust(ia, ib), clans.Attitude(ia, ib), clans.Trust(ia, ic), clans.Attitude(ia, ic));
		Check(clans.Trust(ia, ib) > clans.Trust(ia, ic), "shared beliefs, more trust");
		Check(std::abs(a.Social().Beliefs["sky"] - c.Social().Beliefs["sky"]) < before, "and beliefs spread: meeting brings them closer");
	}

	std::fprintf(stderr, "-- %s\n", "a rite saved in the middle goes on after loading; the state ");
	// ---- a rite saved in the middle goes on after loading; the state round-trips
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		Quiet(clan, false, true);
		clan.Store().Add(w.Items, "berry", 20);
		w.Run(2.0);
		std::string why;
		Check(clan.StartRite("naming", "Ana", &why), "a rite can be called by hand (rite start)");
		for (int i = 0; i < 40 && !clan.Social().Rite.Active(); ++i) w.Run(0.5);
		Check(clan.Social().Rite.Active(), "the rite is under way");
		const std::string saved = clan.ToJson();
		Check(saved.find("\"social\"") != std::string::npos && saved.find("naming") != std::string::npos, "the tribe's save carries the rite in course");
		SocialState back;
		Check(back.FromJson(clan.Social().ToJson()) && back == clan.Social(), "the social state round-trips (rite, agenda, moods, beliefs)");
		Check(!back.FromJson(R"({ "rite": { "rite": "naming" } })") && back == clan.Social(), "a broken one is refused and nothing changes");
		Check(clan.FromJson(saved, true), "the tribe loads in the middle of the rite");
		bool held = false;
		for (int i = 0; i < 120 && !held; ++i) { w.Run(1.0); held = Held(clan, "naming") > 0; }
		Check(held, "and the rite is held after the load");
	}

	std::fprintf(stderr, "-- %s\n", "a snare: with no food the clan sets one where rabbits are, a");
	// ---- a snare: with no food the clan sets one where rabbits are, and it catches one
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		Quiet(clan, false, false);
		Believe(clan, 0.2f, 0.2f, 0.4f);
		clan.SetCombat(nullptr, nullptr);   // (no hunts of H14: only the snare)
		clan.Store().Add(w.Items, "wood", 10);
		clan.Store().Add(w.Items, "stone", 12);
		clan.Store().Add(w.Items, "snare", 2);
		w.Ecology.Spawn({ 8.0f, 0.0f, -14.0f }, 4, "rabbit");
		int caught = 0;
		for (int i = 0; i < 400 && caught == 0; ++i)
		{
			w.Run(1.0);
			caught = clan.Social().Counts.count("trap.snare.caught") ? clan.Social().Counts.at("trap.snare.caught") : 0;
		}
		Check(clan.Social().Counts.count("trap.snare.set") && w.Logged(clan, "sets Snare"), "the clan sets a snare where the rabbits are");
		Check(caught >= 1, "and the snare catches a rabbit");
		if (!caught) w.Dump(clan);
		w.Run(120.0);
		Check(w.Logged(clan, "brings home Rabbit"), "someone brings the catch home");
		Check(w.Broken("social") == 0, "the social invariants hold with the snares");
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("social: all checks passed\n");
	return failures ? 1 : 0;
}
