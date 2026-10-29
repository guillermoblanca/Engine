// Conflict, combat and violence (H14). Checked: the "combat" section of assets/data/era1.json says what the code says, round-trips and is refused
// whole on a mistake (also inside the era1 pack); a duel between two fighters is resolved through the Attack ability's event, is the same in two
// runs and is won by the stronger (and by the other one when their stats are swapped); a block takes most of a blow; wounds bleed, slow the body,
// heal with the days (faster resting and cared for) and too much of them kills; the choice between fighting and fleeing follows boldness, health
// and the odds; a member set upon by three flees and a clan at its camp stands up to a lone stranger; a hungry fox goes for a child alone and the
// child runs; the clan hunts rabbits and gets meat when its store has no food; a raid steals from a hostile clan's store, comes back and leaves a
// grievance (and the raided clan notices it); the fights save and load in the middle; nobody ends with a position that is not a number or stuck
// in a fight. CPU only, stand-in bodies as in tribe_test and economy_test.
#include "GB/Game/ClanHostility.h"
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

#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>


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

// A world with resources, a combat system, a hostility table, an ecology, and clans (each a Tribe with its own members and camp).
struct World
{
	World()
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		Combat.Bind(Entities, Gameplay);
		Hostility.FromRules(Combat.Rules);
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
		if (!std::getenv("GB_COMBAT_LOG")) return;   // GB_COMBAT_LOG=1: every clan's log, and the fights, for a person to read
		for (const auto& t : Clans) { std::fprintf(stderr, "---- %s\n", t->ClanName().c_str()); Dump(*t); }
		for (const CombatEvent& e : Combat.Events()) std::fprintf(stderr, "  %.1f %s %s %s %s %.1f\n", e.Time, e.Who.c_str(), e.Kind.c_str(), e.Other.c_str(), e.Text.c_str(), static_cast<double>(e.Amount));
	}
	void Patch(const glm::vec3& at, int n)
	{
		const ResourceCounts c = ScaledResources(n);
		BuildResources(Entities, at, Defs, c.Trees, c.Rocks, c.Bushes, c.Grass);
	}
	// A clan of `n` at `camp` named `clan`; its members are `prefix` + the names of the work rules.
	Tribe& AddClan(const std::string& clan, int n, const glm::vec2& camp, const std::string& prefix)
	{
		Clans.push_back(std::make_unique<Tribe>());
		Tribe& t = *Clans.back();
		t.SetClanName(clan);
		t.Bind(Entities, Gameplay, Items, Recipes, Defs);
		t.Config.Camp = camp;
		t.SetCombat(&Combat, &Hostility);
		const std::vector<std::string> roles = t.PlanRoles(std::max(1, n));
		std::vector<std::string> names;
		for (int i = 0; i < n; ++i)
		{
			const std::string name = prefix + MemberName(t.Config.Work, static_cast<size_t>(i));
			Entity& e = *Entities.Find(Entities.Create(name));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Stoke" };
			names.push_back(name);
		}
		if (n > 0) { Gameplay.Store(Entities); Gameplay.Load(Entities); }   // (every ability system again, from the components that keep their state: the combat arms the new ones by itself)
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
	// An armed stranger that the combat moves itself (an Entity fighter), of a side, at a place.
	Combatant* Stranger(const std::string& name, const std::string& side, const glm::vec2& at, float strength, const std::string& weapon = "axe")
	{
		Entity& e = *Entities.Find(Entities.Create(name));
		e.Transform.Position = { at.x, 0.0f, at.y };
		Gameplay.Create(name);
		Combatant* c = Combat.Join(name, side, true);
		if (!c) return nullptr;
		c->Weapon = weapon;
		c->Strength = strength;
		c->Endurance = strength;
		c->Dexterity = strength;
		c->Boldness = 0.9f;
		c->Morale = 1.0f;
		return c;
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
			// the invariants of the simulation QA (H20), with the combat ones of H14, every half second for every clan
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
			for (auto& t : Clans)
				for (const TribeMember& m : t->Members())
				{
					const glm::vec2 p = m.Body ? m.Body->Position() : glm::vec2(0.0f);
					Bad = Bad || !std::isfinite(p.x) || !std::isfinite(p.y);
				}
		}
	}
	// Findings of an invariant over every clan so far (they are printed).
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
	bool Bad = false;
};

// A duel between two Entity fighters: who wins, how much health the winner has left and how many blows were struck.
struct Duel
{
	std::string Winner;
	float Health = 0.0f;
	int Blows = 0;
	double Seconds = 0.0;
	bool operator==(const Duel&) const = default;
};

Duel RunDuel(float strengthA, float strengthB, const std::string& weaponA = "axe", const std::string& weaponB = "axe", double saveAt = -1.0)
{
	World w;
	Combatant* a = w.Stranger("Arn", "North", { 0.0f, 0.0f }, strengthA, weaponA);
	Combatant* b = w.Stranger("Bod", "South", { 4.0f, 0.0f }, strengthB, weaponB);
	if (!a || !b) return {};
	a->Boldness = b->Boldness = 1.0f;   // (nobody runs: the duel is to the end)
	w.Combat.Rules.Morale.FleeBelow = 0.0f;
	w.Combat.Rules.Morale.FleeHealth = 0.0f;
	w.Combat.Engage("Arn", "Bod");
	Duel d;
	for (double t = 0.0; t < 240.0; t += dt)
	{
		if (saveAt >= 0.0 && t >= saveAt && t < saveAt + dt * 0.5)
		{
			// the middle of the fight to text and back: the ability systems (through the scene), the positions and the fights
			const std::string fights = w.Combat.ToJson();
			w.Gameplay.Store(w.Entities);
			const glm::vec3 pa = w.Entities.Find(w.Entities.FindByName("Arn"))->Transform.Position, pb = w.Entities.Find(w.Entities.FindByName("Bod"))->Transform.Position;
			w.Combat.Clear();
			w.Gameplay.Load(w.Entities);
			w.Entities.Find(w.Entities.FindByName("Arn"))->Transform.Position = pa;
			w.Entities.Find(w.Entities.FindByName("Bod"))->Transform.Position = pb;
			Check(w.Combat.FromJson(fights), "a fight in the middle loads");
			Check(w.Combat.ToJson() == fights, "and saves again as it was");
			Check(w.Combat.InFight("Arn") && w.Combat.InFight("Bod"), "both are still fighting after the load");
		}
		w.Gameplay.Update(dt);
		w.Combat.Update(dt);
		if (w.Combat.IsDead("Arn") || w.Combat.IsDead("Bod")) { d.Seconds = t; break; }
	}
	const bool aDead = w.Combat.IsDead("Arn"), bDead = w.Combat.IsDead("Bod");
	d.Winner = aDead == bDead ? "" : aDead ? "Bod" : "Arn";
	if (!d.Winner.empty()) d.Health = w.Gameplay.Find(d.Winner)->Attribute("Health");
	for (const auto& [n, c] : w.Combat.All()) d.Blows += c.Landed + c.Missed;
	return d;
}

int main()
{
	const std::string data = std::string(GB_ASSETS_DIR) + "/data";

	// ---- the data: the "combat" section of era1.json says what the code says, round-trips, and a mistake changes nothing
	{
		CombatRules file;
		Check(LoadCombatRulesFile(data + "/era1.json", file).empty(), "the combat section of assets/data/era1.json loads");
		Check(file == DefaultCombatRules(), "and says what DefaultCombatRules says");
		CombatRules back;
		Check(LoadCombatRules("{ \"combat\": " + CombatRulesToJson(DefaultCombatRules()) + " }", back).empty() && back == DefaultCombatRules(), "the combat section round-trips");
		CombatRules keep = DefaultCombatRules();
		Check(!LoadCombatRules(R"({ "combat": { "hits": { "base": 0.5 }, "injuries": { "lethal": -1 } } })", keep).empty() && keep == DefaultCombatRules(), "a bad number is refused and nothing changes (not even the part before it)");
		Check(!LoadCombatRules(R"({ "combat": { "duels": {} } })", keep).empty() && !LoadCombatRules(R"({ "combat": { "morale": { "courage": 1 } } })", keep).empty(), "an unknown part or key is refused");
		Check(!LoadCombatRules(R"({ "combat": { "predators": { "wolf": { "tag": "Animal.Wolf", "prey": "babies" } } } })", keep).empty(), "an unknown kind of prey is refused");
		Check(!LoadCombatRules(R"({ "combat": { "predators": { "wolf": { "tag": "Animal.Wolf", "weapon": "claws" } } } })", keep).empty(), "a predator with a weapon that does not exist is refused");
		Check(!LoadCombatRules(R"({ "combat": { "unarmed": "sword" } })", keep).empty() && keep == DefaultCombatRules(), "an unarmed weapon that does not exist is refused");
		Check(LoadCombatRules(R"({ "items": [] })", keep).empty() && keep == DefaultCombatRules(), "a file without the section changes nothing");
		CombatRules wolf = DefaultCombatRules();
		Check(LoadCombatRules(R"({ "combat": { "predators": { "wolf": { "tag": "Animal.Wolf", "prey": "weak", "health": 60 } }, "hostile": [ [ "Clan", "Riverfolk" ] ] } })", wolf).empty()
			&& wolf.Predators.size() == 2 && wolf.Predators["wolf"].Health == 60.0f && wolf.Predators["fox"] == DefaultCombatRules().Predators["fox"] && wolf.Hostile.size() == 1,
			"a pack adds a predator (the fox stays) and a hostile pair");
		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes;
		RegisterEraOneItems(items, recipes, nodes);
		Check(CheckCombat(file, items).empty() && items.Find("meat") && items.Find("meat")->Perishable(), "the meat of the hunt is an item of era 1, and it spoils");
		CombatRules odd = file;
		odd.Hunts["deer"] = odd.Hunts["rabbit"];
		odd.Hunts["deer"].Item = "venison";
		Check(!CheckCombat(odd, items).empty(), "a hunt that gives an item nobody has is reported");
		Check(DefaultSocietyRules().Relations.Events.count("care") == 1, "tending a wounded clanmate is an event between two (the society section)");
		// the section inside the packs: era1 loads with it, strictly
		ContentPacks packs;
		ItemLibrary pi; RecipeBook pr; std::map<std::string, ResourceNodeDef> pn; std::map<std::string, TribeRole> roles;
		CombatRules live;
		std::map<std::string, BuildingDef> buildings;   // (the stations of era 1's recipes come from its buildings: the hearth, the tannery, the forge...)
		AddGameContentSections(packs, pi, pr, pn, roles, nullptr, nullptr, nullptr, nullptr, &buildings);
		AddCombatContentSection(packs, live);
		ContentReport report;
		ContentPacks::Options strict;
		strict.Strict = true;
		const std::string loadError = packs.Load("era1", data, &report, strict);
		Check(loadError.empty() && report.Clean(), "the era1 pack loads strictly with the combat section (the meat is given by the hunt)");
		if (!loadError.empty() || !report.Clean()) std::fprintf(stderr, "  %s\n  %s\n", loadError.c_str(), report.Text().c_str());
		CombatRules expected = DefaultCombatRules();   // (social.json, a file of era1, adds the deer, the boar and the grouse: docs/VIDA_SOCIAL.md)
		Check(LoadCombatRulesFile(data + "/social.json", expected).empty() && live == expected, "and the section it read is the default one, with the hunts of social.json");
	}

	// ---- the montages of the combat abilities are those of the delivered animation manifest (the X Bot packs, packs/full)
	{
		std::ifstream f(std::string(GB_ASSETS_DIR) + "/animation/mixamo.manifest.json");
		std::stringstream ss;
		ss << f.rdbuf();
		const nlohmann::json manifest = nlohmann::json::parse(ss.str(), nullptr, false);
		std::set<std::string> known;
		if (!manifest.is_discarded() && manifest.contains("montages"))
			for (const auto& m : manifest["montages"]) known.insert(m.value("name", std::string()));
		GameplayLibrary lib;
		RegisterCombatContent(lib, DefaultCombatRules());
		std::string missing;
		for (const auto& [name, ability] : lib.Abilities())
			for (const std::string& m : ability.Montages) if (!known.count(m)) missing += " " + name + ":" + m;
		Check(!known.empty() && missing.empty(), "every montage of the combat abilities is in the animation manifest");
		if (!missing.empty()) std::fprintf(stderr, "  missing:%s\n", missing.c_str());
	}

	// ---- the pure rules: fight or flight, the motive of a raid, the party
	{
		const CombatRules r = DefaultCombatRules();
		ThreatView bold;
		bold.Boldness = 0.9f; bold.Odds = 3.0f;
		ThreatView timid = bold;
		timid.Boldness = 0.1f; timid.Odds = 0.3f; timid.Safety = 40.0f;
		Check(FightOrFlight(r.Threats, r.Morale, bold).Fights(), "a bold member with the odds on its side fights");
		Check(!FightOrFlight(r.Threats, r.Morale, timid).Fights(), "a timid one, outnumbered and afraid, flees");
		ThreatView hurt = bold;
		hurt.Health = 10.0f; hurt.Odds = 0.8f;
		Check(!FightOrFlight(r.Threats, r.Morale, hurt).Fights(), "a badly hurt one flees");
		ThreatView child = bold;
		child.Child = true;
		Check(!FightOrFlight(r.Threats, r.Morale, child).Fights(), "a child never fights");
		ThreatView kin = timid;
		kin.Odds = 0.6f; kin.Kin = true; kin.Camp = true; kin.Boldness = 0.4f;
		ThreatView stranger = kin;
		stranger.Kin = false; stranger.Camp = false;
		Check(FightOrFlight(r.Threats, r.Morale, kin).Fights() && !FightOrFlight(r.Threats, r.Morale, stranger).Fights(), "the same member defends its kin at the camp, and runs elsewhere");
		Check(FightOrFlight(r.Threats, r.Morale, bold).Why().find("fight") != std::string::npos, "the choice explains itself");
		Check(WeighRaid(r.Raids, 0.8f, 0.8f, 0.0f, 0.5f).Score > WeighRaid(r.Raids, 0.2f, 0.0f, 0.0f, 0.5f).Score, "a bold, hungry clan wants a raid more than a timid, fed one");
		Check(WeighRaid(r.Raids, 0.5f, 0.0f, 2.0f, 0.5f).Score >= r.Raids.Want, "a grievance is reason enough");
		std::vector<RaiderCandidate> c = { { "Ana", 100.0f, 0.2f, 0.5f }, { "Biel", 100.0f, 0.9f, 0.6f }, { "Cai", 30.0f, 0.9f, 0.9f }, { "Dua", 100.0f, 0.5f, 0.5f, true },
			{ "Eli", 90.0f, 0.6f, 0.6f }, { "Fen", 100.0f, 0.95f, 0.9f, false, true } };
		const std::vector<std::string> party = ChooseRaidParty(r.Raids, c);
		Check(party.size() == 3 && party[0] == "Biel" && party[1] == "Eli" && party[2] == "Ana", "the party: fit adults not busy, the boldest and strongest first");
		RaidRules big = r.Raids;
		big.Party = 4;
		Check(ChooseRaidParty(big, c).empty(), "and nobody goes when too few can");
	}

	// ---- hostility: the table, the predators, grievances and their memory
	{
		HostilityTable h;
		h.FromRules(DefaultCombatRules());
		Check(h.Hostility("Animal.Fox", "Clan") == 1.0f && h.Hostility("Clan", "Animal.Fox") == 0.0f && h.Hostility("Clan", "Riverfolk") == 0.0f, "a predator is hostile to anyone; clans are not, by default");
		h.Set("Clan", "Riverfolk", 0.7f);
		Check(h.Hostility("Clan", "Riverfolk") == 0.7f && h.Hostility("Riverfolk", "Clan") == 0.7f, "the table sets both ways");
		h.Wronged("Hillmen", "Clan", "raid", 1.0f);
		Check(h.Grievance("Hillmen", "Clan") == 1.0f && h.Hostility("Hillmen", "Clan") == 0.5f && h.Hostility("Clan", "Hillmen") == 0.0f, "a grievance makes the victim hostile to the culprit");
		Inventory store(10);
		h.Settled("Hillmen", { 30.0f, 0.0f }, &store);
		h.Settled("Riverfolk", { -30.0f, 0.0f }, &store);
		h.Settled("Clan", { 0.0f, 0.0f }, &store);
		Check(h.RaidTargets("Clan").size() == 1 && h.RaidTargets("Clan")[0].Clan == "Riverfolk" && h.RaidTargets("Hillmen").size() == 1
			&& h.RaidTargets("Hillmen")[0].Clan == "Clan", "a clan may raid only those it is hostile to");
		HostilityTable back;
		Check(back.FromJson(h.ToJson()) && back.Table() == h.Table() && back.Grievances() == h.Grievances(), "the table and the grievances round-trip");
		h.Forget(5.0f);
		Check(h.Grievance("Hillmen", "Clan") > 0.0f && h.Grievance("Hillmen", "Clan") < 1.0f, "grievances fade with the days");
		h.Forget(10.0f);
		Check(h.Grievance("Hillmen", "Clan") == 0.0f, "and are forgotten");
	}

	// ---- a duel: resolved by the ability's event, deterministic, won by the stronger
	{
		const Duel strong = RunDuel(0.85f, 0.2f), again = RunDuel(0.85f, 0.2f), swapped = RunDuel(0.2f, 0.85f);
		Check(!strong.Winner.empty() && strong.Blows > 2, "a duel ends with a winner after several blows");
		Check(strong == again, "two runs of the same duel are the same, blow by blow");
		Check(strong.Winner == "Arn" && swapped.Winner == "Bod", "the stronger wins, whichever side it is on");
		const Duel armed = RunDuel(0.5f, 0.5f, "axe", "fists");
		Check(armed.Winner == "Arn", "an axe beats bare fists between equals");
		const Duel loaded = RunDuel(0.85f, 0.2f, "axe", "axe", 4.0);
		Check(loaded.Winner == strong.Winner, "a duel saved and loaded in the middle ends the same way");

		// the blow lands only through the Attack ability (its montage's notify): a swing cancelled before it does nothing
		World w;
		Combatant* a = w.Stranger("Arn", "North", { 0.0f, 0.0f }, 0.5f);
		w.Stranger("Bod", "South", { 1.0f, 0.0f }, 0.5f);
		a->Weapon = "axe";
		w.Combat.Update(dt);   // (a new weapon: its ability is granted with the next step)
		AbilitySystemComponent* aa = w.Gameplay.Find("Arn");
		Check(aa->HasAbility("Attack.axe") && aa->HasAbility("Block") && aa->HasAbility("Flee") && aa->Attributes().Has("Stamina"), "a fighter has the combat abilities and stamina");
		Check(aa->Activate("Attack.axe", w.Gameplay.Find("Bod"), nullptr, "Bod") && aa->HasTag("State.Attacking"), "an attack starts");
		aa->Cancel("Attack.axe");
		for (int i = 0; i < 40; ++i) w.Gameplay.Update(dt);
		Check(w.Combat.Find("Arn")->Landed + w.Combat.Find("Arn")->Missed == 0, "a cancelled swing never lands");
		Check(aa->Attribute("Stamina") < w.Combat.Rules.Hits.MaxStamina, "but its stamina was spent");
		// a block takes most of a blow
		for (const std::string& n : { "Arn", "Bod" }) w.Gameplay.Find(n)->Attributes().SetBase("Stamina", 100.0f);
		w.Combat.Rules.Hits.Min = w.Combat.Rules.Hits.Max = 1.0f;   // (every blow lands)
		const float before = w.Gameplay.Find("Bod")->Attribute("Health");
		Check(w.Gameplay.Find("Bod")->Activate("Block", nullptr), "the defender blocks");
		Check(aa->Activate("Attack.axe", w.Gameplay.Find("Bod"), nullptr, "Bod"), "the attacker swings into the block");
		for (int i = 0; i < 40; ++i) w.Gameplay.Update(dt);
		const float blocked = before - w.Gameplay.Find("Bod")->Attribute("Health");
		const float before2 = w.Gameplay.Find("Bod")->Attribute("Health");
		Check(aa->Activate("Attack.axe", w.Gameplay.Find("Bod"), nullptr, "Bod"), "and again, unblocked");
		for (int i = 0; i < 40; ++i) w.Gameplay.Update(dt);
		const float clean = before2 - w.Gameplay.Find("Bod")->Attribute("Health");
		Check(blocked > 0.0f && clean > blocked * 2.5f && w.Combat.Find("Bod")->Blocked == 1, "a block lets through a quarter of the blow");
	}

	// ---- wounds: bleeding, slowing, healing (faster resting and cared for), and too much of them kills
	{
		auto heal = [](bool resting, bool cared, float& severity, float& vigorWounded, float& health)
		{
			World w;
			w.Stranger("Wen", "Clan", { 0.0f, 0.0f }, 0.5f);
			AbilitySystemComponent* asc = w.Gameplay.Find("Wen");
			w.Combat.Hurt("Wen", 10.0f, "cut", 0.6f);
			w.Gameplay.Update(dt);
			vigorWounded = asc->Attribute(NeedName::Vigor);
			Combatant* c = w.Combat.Find("Wen");
			c->Resting = resting;
			c->Cared = cared;
			for (int i = 0; i < static_cast<int>(600.0 / dt); ++i) { w.Gameplay.Update(dt); w.Combat.Update(dt); }
			severity = c->Severity();
			health = asc->Attribute("Health");
		};
		float awake = 0, resting = 0, cared = 0, vigor = 0, v2 = 0, v3 = 0, h1 = 0, h2 = 0, h3 = 0;
		heal(false, false, awake, vigor, h1);
		heal(true, false, resting, v2, h2);
		heal(true, true, cared, v3, h3);
		Check(vigor < 0.8f, "a serious wound slows the body down (Vigor)");
		Check(awake < 0.6f && awake > resting && resting > cared, "wounds heal with time, faster resting, faster still when someone cares for them");
		Check(h1 < 90.0f - 0.0f && h3 > h1, "a fresh wound bleeds (and bleeds less when it is tended)");
		World w;
		w.Stranger("Wen", "Clan", { 0.0f, 0.0f }, 0.5f);
		AbilitySystemComponent* asc = w.Gameplay.Find("Wen");
		for (int i = 0; i < 200 && !w.Combat.IsDead("Wen"); ++i) { w.Combat.Hurt("Wen", 0.0f, "cut", 0.5f, "Bod"); w.Combat.Update(dt); }
		Check(w.Combat.IsDead("Wen") && asc->HasTag("State.Dead") && CauseOfDeath(*asc) == "Wounds", "too many wounds kill, and the cause of death says so");
		Check(std::any_of(w.Combat.Events().begin(), w.Combat.Events().end(), [](const CombatEvent& e) { return e.Kind == "death" && e.Other == "Wen"; }), "and the death is told");
	}

	// ---- fight or flight in a clan: one set upon by three runs; the clan at its camp stands up to a lone stranger
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		w.Hostility.Set("Clan", "Raiders", 1.0f);
		Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		clan.Store().Add(w.Items, "berry", 20);
		w.Run(5.0);
		const TribeMember& lone = clan.Members()[0];
		clan.People().SetTrait(lone.Name, "boldness", 0.5f);
		const glm::vec2 at = lone.Body->Position();
		for (int i = 0; i < 3; ++i)
		{
			const std::string name = "Raider " + std::to_string(i + 1);
			w.Stranger(name, "Raiders", at + glm::vec2(6.0f + static_cast<float>(i), 2.0f * static_cast<float>(i)), 0.8f);
			w.Combat.Engage(name, lone.Name);
		}
		w.Run(3.0);
		Check(w.Logged(clan, lone.Name + " (" + lone.Role + ") flees from Raider"), "a member set upon by three flees");
		if (!w.Logged(clan, "flees from")) w.Dump(clan, "Raider");
		Check(w.Logged(clan, "is attacked by Raider"), "and cries out that it is attacked");

		World v;
		v.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		v.Hostility.Set("Clan", "Raiders", 1.0f);
		Tribe& home = v.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		home.Store().Add(v.Items, "berry", 20);
		v.Run(1.0);
		v.Stranger("Thief", "Raiders", { 1.0f, -9.0f }, 0.3f, "fists");
		v.Combat.Find("Thief")->Boldness = 0.2f;
		v.Run(30.0);
		Check(home.Conflict().Fights >= 1 && v.Logged(home, "stands up to Thief"), "the clan at its camp stands up to a lone stranger");
		Check(v.Combat.IsDead("Thief") || v.Combat.Find("Thief")->Mode == FightMode::Flee || v.Combat.Find("Thief")->Taken > 0, "and the stranger is beaten (dead, running or hurt)");
		if (home.Conflict().Fights < 1) v.Dump(home);
		v.Run(60.0);
		bool stuck = false;
		for (const TribeMember& m : home.Members())
			if (!m.Dead && v.Combat.InFight(m.Name)) stuck = true;
		Check(!stuck && !v.Bad && !w.Bad, "a minute later nobody is stuck in a fight, and every position is a number");
		Check(v.Broken("combat") == 0 && w.Broken("combat") == 0 && v.Broken("deaths") == 0 && w.Broken("deaths") == 0, "the combat and death invariants of the QA hold through the fights");
		Check(home.ConflictReport().find("fights") != std::string::npos, "the report says what happened");
	}

	// ---- a hungry fox goes for a child alone; the child runs and lives
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		w.Ecology.Bind(w.Entities, w.Gameplay);
		Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		clan.Store().Add(w.Items, "berry", 20);
		const std::string kid = clan.Members()[3].Name;
		clan.People().SetAge(kid, 7.0f);
		w.Run(1.0);
		FakeBody* body = w.Body(kid);
		body->Teleport({ 30.0f, 25.0f });
		std::string error;
		body->Do("wait 30", error);
		Check(w.Ecology.Spawn({ 38.0f, 0.0f, 25.0f }, 1, "fox") == 1, "a fox comes");
		std::string fox;
		for (const std::string& n : w.Gameplay.Names()) if (n.rfind("Fox", 0) == 0) fox = n;
		w.Gameplay.Find(fox)->Attributes().SetBase("Food", 20.0f);   // (hungry)
		w.Run(40.0);
		Check(std::any_of(w.Combat.Events().begin(), w.Combat.Events().end(), [&](const CombatEvent& e) { return e.Kind == "prey" && e.Who == fox && e.Other == kid; }), "the hungry fox goes for the child that is alone");
		Check(w.Logged(clan, kid + " (") && w.Logged(clan, "flees from " + fox), "the child runs from it");
		if (!w.Logged(clan, "flees from " + fox)) w.Dump(clan, kid);
		Check(!clan.Members()[3].Dead, "and lives");
		Check(!w.Gameplay.Find(fox) || !w.Gameplay.Find(fox)->HasTag("State.Held") || w.Combat.InFight(fox), "the fox is held only while it fights");
		// an adult alone is left in peace (the rule says children)
		World a;
		a.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		a.Ecology.Bind(a.Entities, a.Gameplay);
		Tribe& adults = a.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		adults.Store().Add(a.Items, "berry", 20);
		a.Run(1.0);
		a.Body(adults.Members()[3].Name)->Teleport({ 30.0f, 25.0f });
		a.Body(adults.Members()[3].Name)->Do("wait 30", error);
		a.Ecology.Spawn({ 38.0f, 0.0f, 25.0f }, 1, "fox");
		for (const std::string& n : a.Gameplay.Names()) if (n.rfind("Fox", 0) == 0) a.Gameplay.Find(n)->Attributes().SetBase("Food", 20.0f);
		a.Run(20.0);
		Check(std::none_of(a.Combat.Events().begin(), a.Combat.Events().end(), [](const CombatEvent& e) { return e.Kind == "prey"; }), "a fox does not dare go for an adult");
	}

	// ---- hunting: with no food in the store the clan hunts rabbits, and a kill gives meat
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		w.Ecology.Bind(w.Entities, w.Gameplay);
		Tribe& clan = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		clan.Store().Add(w.Items, "wood", 10);
		clan.Store().Add(w.Items, "stone", 12);
		Check(w.Ecology.Spawn({ 8.0f, 0.0f, -14.0f }, 3, "rabbit") == 3, "rabbits near the camp");
		bool posted = false;
		for (int i = 0; i < 240 && clan.Conflict().Hunted.empty(); ++i)
		{
			w.Run(1.0);
			for (const auto& [id, job] : clan.Board().Jobs()) posted = posted || job.Kind == "hunt";
		}
		Check(posted, "the clan posts a hunt when it has no food and rabbits are near");
		w.Run(1.0);   // (the ecology takes the dead rabbit away in the next step)
		Check(clan.Conflict().Hunted.count("Animal.Rabbit") == 1, "a member hunts a rabbit down");

		if (clan.Conflict().Hunted.empty()) w.Dump(clan, "hunt");
		int meat = clan.Store().Count("meat");
		for (const TribeMember& m : clan.Members()) meat += m.Body->Bag().Count("meat");
		Check(meat > 0 || std::any_of(clan.Members().begin(), clan.Members().end(), [](const TribeMember& m) { return m.Meals > 0; }), "and the kill gives meat (carried, stored or eaten)");
		Check(w.Logged(clan, "kills Rabbit") && w.Ecology.Killed() >= 1, "the log and the ecology count it");
		if (w.Ecology.Killed() < 1)
		{
			std::fprintf(stderr, "  ecology: %s\n", w.Ecology.Describe().c_str());
			w.Ecology.Update(w.Clock, dt);
			std::fprintf(stderr, "  after one more update: %s, killed %d\n", w.Ecology.Describe().c_str(), w.Ecology.Killed());
			for (const std::string& n : w.Gameplay.Names())
				if (n.rfind("Rabbit", 0) == 0) std::fprintf(stderr, "  %s dead %d tags %zu entity %d age %.1f\n", n.c_str(), w.Gameplay.Find(n)->HasTag("State.Dead") ? 1 : 0, w.Gameplay.Find(n)->Tags().Names().size(),
					w.Entities.FindByName(n) != NullEntity ? 1 : 0, static_cast<double>(w.Gameplay.Find(n)->Attribute("Age")));
		}
		Check(!w.Bad, "nobody's position is broken by the chase");
		Check(w.Broken("combat") == 0, "the combat invariants hold through the hunt");
	}

	// ---- a raid: a hostile clan's store is robbed, the raiders come home, the victim holds a grievance and notices it
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		w.Hostility.Set("Clan", "Riverfolk", 1.0f);
		Tribe& raiders = w.AddClan("Clan", 4, { 0.0f, -8.0f }, "");
		raiders.Store().Add(w.Items, "wood", 10);
		raiders.Store().Add(w.Items, "stone", 12);
		Tribe& river = w.AddClan("Riverfolk", 0, { 40.0f, 30.0f }, "R");
		river.Store().Add(w.Items, "berry", 12);
		river.Store().Add(w.Items, "fiber", 6);
		w.Run(70.0);
		Check(w.Logged(raiders, "the clan raids Riverfolk"), "a hungry clan raids a hostile one by itself");
		Check(raiders.Conflict().Raid.Active() || raiders.Conflict().Raid.Raids > 0, "the raid is on");
		for (int i = 0; i < 120 && raiders.Conflict().Raid.Active(); ++i) w.Run(1.0);
		const RaidState& r = raiders.Conflict().Raid;
		Check(!r.Active() && r.Raids == 1, "the raid ends");
		Check(r.Stolen > 0 && r.Loot.count("berry") == 1 && river.Store().Count("berry") < 12, "the raiders took food first and brought it home");
		if (r.Stolen == 0) w.Dump(raiders, "raid");
		Check(w.Hostility.Grievance("Riverfolk", "Clan") > 0.0f, "the victim holds a grievance against the raiders");
		Check(river.Conflict().Raided == 1 && w.Logged(river, "a raid"), "and the raided clan saw it");
		Check(w.Logged(raiders, "is back from the raid on Riverfolk"), "the log tells the end of it");
		// the clan's state of conflict saves and loads (with a raid on, too)
		ConflictState s = raiders.Conflict();
		s.Raid.Phase = "march";
		s.Raid.Took["Ana"]["berry"] = 2;
		ConflictState back;
		Check(back.FromJson(s.ToJson()) && back == s, "the clan's conflict state round-trips");
		Check(!back.FromJson(R"({ "raid": { "phase": "pillage" } })") && back == s, "a bad raid phase is refused and nothing changes");
		Tribe copy;
		copy.Bind(w.Entities, w.Gameplay, w.Items, w.Recipes, w.Defs);
		const std::string saved = raiders.ToJson();
		Check(saved.find("\"conflict\"") != std::string::npos, "the tribe's save carries its conflict state");
		Check(!w.Bad, "nobody's position is broken by the raid");
		Check(w.Broken("combat") == 0 && w.Broken("deaths") == 0, "the combat invariants hold through the raid");

	}

	// ---- the clans of H13: the hostility comes from their diplomacy (ClanHostility); a raid leaves a grievance there, and a truce stops raids
	{
		World w;
		w.Patch({ 0.0f, 0.0f, 0.0f }, 4);
		Tribe& hill = w.AddClan("Hillfolk", 4, { 0.0f, -8.0f }, "");
		hill.Store().Add(w.Items, "wood", 10);
		Tribe& river = w.AddClan("Riverfolk", 0, { 40.0f, 30.0f }, "R");
		river.Store().Add(w.Items, "berry", 12);
		ClanWorld clans;
		const ClanId a = clans.Add("Hillfolk", &hill).Id, b = clans.Add("Riverfolk", &river).Id;
		ClanHostility diplomacy;
		diplomacy.Bind(&clans, &w.Hostility);
		for (auto& t : w.Clans) t->SetCombat(&w.Combat, &diplomacy);
		Check(diplomacy.Hostility("Hillfolk", "Riverfolk") == 0.0f && diplomacy.RaidTargets("Hillfolk").empty(), "clans that never met are not hostile and may not raid");
		Check(diplomacy.Hostility("Animal.Fox", "Hillfolk") == 1.0f, "a predator is still hostile (the table behind the clans)");
		clans.Relate(a, b, "trade", 0.0f);   // (they have dealt once: they know each other)
		clans.Relate(b, a, "raid", 1.0f, "Riverfolk took the Hillfolk's berries", a);   // the Hillfolk hold a grievance
		Check(clans.MayRaid(a, b) && diplomacy.Hostility("Hillfolk", "Riverfolk") > 0.0f, "a grievance makes the Hillfolk hostile: the diplomacy says they may raid");
		Check(diplomacy.RaidTargets("Hillfolk").size() == 1 && diplomacy.RaidTargets("Hillfolk")[0].Store == &river.Store(), "and the target is the Riverfolk's camp and store");
		const float before = clans.Grievance(b, a);
		std::string why;
		Check(hill.StartRaid("Riverfolk", &why), "the Hillfolk raid the Riverfolk");
		if (!hill.Conflict().Raid.Active()) std::fprintf(stderr, "  no raid: %s\n", why.c_str());
		for (int i = 0; i < 150 && hill.Conflict().Raid.Active(); ++i) w.Run(1.0);
		Check(!hill.Conflict().Raid.Active() && hill.Conflict().Raid.Stolen > 0, "the raid steals and comes home");
		Check(clans.Grievance(b, a) > before && clans.Relation(a, b)->Raids >= 2, "the raid leaves the Riverfolk a grievance in the diplomacy (Relate \"raid\")");
		// a truce: no raid may start, and one under way is called off
		std::string text = clans.ToJson();
		nlohmann::json j = nlohmann::json::parse(text);
		for (auto& r : j["relations"]) r["pacts"] = nlohmann::json::array({ { { "kind", "truce" }, { "since", 0.0 }, { "until", 1e9 } } });
		Check(clans.FromJson(j.dump()), "a truce between them (from a save)");
		Check(!clans.MayRaid(a, b) && diplomacy.Hostility("Hillfolk", "Riverfolk") == 0.0f && diplomacy.RaidTargets("Hillfolk").empty(), "under a truce they are not hostile");
		w.Run(1300.0);   // (past the cooldown of the last raid)
		Check(!hill.StartRaid("Riverfolk", &why) && hill.Conflict().Raid.Raids == 1, "and no raid starts while the truce holds");
		Check(w.Broken("combat") == 0, "the combat invariants hold with the clans");
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("combat: all checks passed\n");
	return failures ? 1 : 0;
}
