// T331 (documentation/01_diseno/ARBOL_TECNICO.md §11.7.7, §11.8): the generic modifiers of the technologies (GB/Game/TechModifiers.h). On a
// clan of four with stand-in bodies (the village of transplant_test) and the game's technologies:
//   (data) the game's tree has modifiers only of the known keys; a wrong key or factor is refused; they survive TechsToJson;
//   (each) for every technology of the game with a modifier, the number it changes is measured at the point the simulation reads it, on a clan
//          that knows only that technology and on one that does not: the meat of a kill, the healing of wounds, the grief at a partner's death,
//          the mood after a day, the work held back at night, the ageing of the food in the store, the slots of a delivery to a building site.
#include "GB/Log.h"
#include "GB/Game/TechModifiers.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Gameplay/Techs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
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

// File-local (T51: several tests share one executable)
namespace
{
class ModBody : public ITribeBody
{
public:
	ModBody(glm::vec2 start) : m_Pos(start) {}
	bool Busy() const override { return m_Wait > 0.0f; }
	bool Failed() const override { return false; }
	glm::vec2 Position() const override { return m_Pos; }
	Inventory& Bag() override { return m_Bag; }
	void Teleport(const glm::vec2& p) override { m_Pos = p; m_Wait = 0.0f; }
	bool Do(const std::string& tasks, std::string&) override
	{
		// (walks at once and waits: what is measured here is no walk)
		m_Wait = 0.0f;
		std::stringstream ss(tasks);
		std::string part;
		while (std::getline(ss, part, '|'))
		{
			std::stringstream ws(part);
			std::string kind;
			ws >> kind;
			float a = 0.0f, b = 0.0f;
			if (kind == "goto" && (ws >> a >> b)) m_Pos = { a, b };
			else if (kind == "wait" && (ws >> a)) m_Wait += a;
		}
		return true;
	}
	void Update(float step) { m_Wait = std::max(0.0f, m_Wait - step); }
private:
	glm::vec2 m_Pos;
	float m_Wait = 0.0f;
	Inventory m_Bag{ 12, 0.0f };
};

// A clan of four with a patch of resource nodes, a combat system and an ecology (for a rabbit), no player among them.
struct Village
{
	explicit Village(const TechTree& tree)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		RegisterBuildingContent(Gameplay.Library);
		BuildResourcePatch(Entities, glm::vec3(0.0f), Defs, 4);
		const char* names[] = { "Ana", "Biel", "Cleo", "Dan" };
		const char* roles[] = { "forager", "woodcutter", "miner", "forager" };
		for (const char* n : names)
		{
			Entity& e = *Entities.Find(Entities.Create(n));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "Build" };
		}
		Gameplay.Load(Entities);
		Combat.Bind(Entities, Gameplay);
		Ecology.Bind(Entities, Gameplay);
		for (int i = 0; i < 4; ++i) bodies.push_back(std::make_unique<ModBody>(Camp));
		Builds.Bind(Gameplay);
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		Tribe_.SetBuildings(&Builds.Defs);
		Tribe_.SetCombat(&Combat);
		Tribe_.Config.Economy.Camp = Camp;
		Tribe_.Config.PlanSeconds = 1e9f;   // (no sites of its own)
		Tribe_.Config.Social = DefaultSocialRules();   // (a social life: mood and grief)
		Tribe_.Store().Add(Items, "wood", 10);
		Tribe_.Store().Add(Items, "stone", 12);
		for (size_t i = 0; i < 4; ++i) Tribe_.AddMember(names[i], roles[i], bodies[i].get());
		Tribe_.SetTechs(&tree);
	}
	void Step()
	{
		Gameplay.Update(dt);
		for (auto& b : bodies) b->Update(dt);
		Combat.Update(dt);
		Builds.Update(dt, &Entities);
		Tribe_.Update(dt);
	}
	void Run(float seconds) { for (int i = 0; i < static_cast<int>(seconds / dt); ++i) Step(); }
	ModBody& Body(const std::string& name) { for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == name) return *bodies[i]; return *bodies[0]; }
	const glm::vec2 Camp{ 0.0f, -8.0f };
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	CombatSystem Combat;
	EcologySystem Ecology;
	BuildingSystem Builds;
	Tribe Tribe_;
	std::vector<std::unique_ptr<ModBody>> bodies;
};

// ---- what each key changes, measured where the simulation reads it ("" = a clan that knows none of the technologies with modifiers)
using Measure = std::function<float(const TechTree&, const std::string& tech)>;

void Learn(Village& v, const std::string& tech)
{
	if (!tech.empty()) Check(v.Tribe_.Knowledge().GrantClan(tech).empty(), "(the clan learns the technology)");
}

// hunt: the meat one kill of a rabbit puts in the hunter's bag
float MeatOfAKill(const TechTree& tree, const std::string& tech)
{
	Village v(tree);
	Learn(v, tech);
	v.Run(0.5f);
	const int before = v.Body("Ana").Bag().Count("meat");
	if (v.Ecology.Spawn({ 4.0f, 0.0f, -10.0f }, 1, "rabbit") != 1) return -1.0f;
	std::string rabbit;
	for (const std::string& n : v.Gameplay.Names()) if (n.rfind("Rabbit", 0) == 0) rabbit = n;
	if (!v.Combat.JoinAnimal(rabbit)) return -1.0f;
	v.Combat.Hurt(rabbit, 1000.0f, "cut", 1.0f, "Ana");
	v.Run(0.5f);
	return static_cast<float>(v.Body("Ana").Bag().Count("meat") - before);
}

// heal: how fast the wounds of a member heal (the combatant's Heal, which the combat system multiplies its healing by)
float Healing(const TechTree& tree, const std::string& tech)
{
	Village v(tree);
	Learn(v, tech);
	v.Run(0.5f);
	const Combatant* c = v.Combat.Find("Ana");
	return c ? c->Heal : -1.0f;
}

// grief: the grief of a member at its partner's death
float GriefAtADeath(const TechTree& tree, const std::string& tech)
{
	Village v(tree);
	Learn(v, tech);
	v.Run(0.5f);
	v.Tribe_.People().Marry(v.Tribe_.Config.Society, "Ana", "Dan");
	const float before = v.Tribe_.Grief("Ana");
	v.Tribe_.Kill("Dan", "Wounds");
	return v.Tribe_.Grief("Ana") - before;
}

// mood: everyone's mood after a day passes (it returns towards its rest)
float MoodAfterADay(const TechTree& tree, const std::string& tech)
{
	Village v(tree);
	Learn(v, tech);
	v.Run(0.5f);
	const double day = static_cast<double>(std::max(1.0f, v.Tribe_.Config.Needs.DaySeconds));
	v.Tribe_.Social().DayTimer = day - 0.001;
	v.Step();
	return v.Tribe_.Mood("Ana");
}

// night: what the agenda makes of a work held back at night (the first such kind of the agenda)
float NightWork(const TechTree& tree, const std::string& tech)
{
	Village v(tree);
	Learn(v, tech);
	const auto night = v.Tribe_.Config.Agenda.Factors.find("night");
	if (night == v.Tribe_.Config.Agenda.Factors.end()) return -1.0f;
	for (const auto& [kind, f] : night->second)
		if (f < 1.0f) return v.Tribe_.AgendaFactor(kind, 23.5f, Season::Summer);
	return -1.0f;
}

// spoil: how much the berries in the clan's store have aged in a while
float StoreAgeing(const TechTree& tree, const std::string& tech)
{
	Village v(tree);
	Learn(v, tech);
	v.Tribe_.Store().Add(v.Items, "berry", 30);
	v.Run(30.0f);
	return v.Tribe_.Store().OldestAge("berry");
}

// carry: the slots of the delivery to a building site (one per trip: a trip carries more, fewer trips)
float DeliverySlots(const TechTree& tree, const std::string& tech)
{
	Village v(tree);
	Learn(v, tech);
	v.Tribe_.Config.HaulAt = 2;   // (the shelter's foundation asks 3 stones: 2 trips of 2, one of 3)
	v.Run(0.5f);
	std::string site;
	if (!v.Tribe_.StartSite("shelter", v.Camp + glm::vec2(-9.0f, 6.0f), &site).empty()) return -1.0f;
	for (int i = 0; i < static_cast<int>(20.0f / dt); ++i)
	{
		v.Step();
		for (const auto& [id, job] : v.Tribe_.Board().Jobs())
			if (job.Kind == "deliver" && job.Target == site) return static_cast<float>(job.Slots);
	}
	return -1.0f;
}
}

int GBTest_tech_modifiers(int, char**)
{
	Log::Init();
	const TechTree tree = DefaultTechs();

	// ---- (data) the keys, the checks and the round trip
	std::map<std::string, std::vector<std::string>> byKey;   // key -> technologies that name it
	for (const auto& [id, t] : tree.Techs)
		for (const auto& [key, f] : t.Modifiers)
		{
			byKey[key].push_back(id);
			Check(std::find(TechModifierKeys().begin(), TechModifierKeys().end(), key) != TechModifierKeys().end() && f > 0.0f && f != 1.0f, "(data) a modifier of the game has a known key and a factor that changes something");
		}
	for (const char* tech : { "butchery", "blade_cores", "spear_thrower", "herb_lore", "ritual", "cave_painting", "cold_hammering", "pottery", "potters_wheel", "megaliths", "the_wheel" })
	{
		const TechDef* t = tree.Find(tech);
		if (!t || t->Modifiers.empty()) std::fprintf(stderr, "  %s has no modifier\n", tech);
		Check(t && !t->Modifiers.empty(), "(data) the technologies §11.8 waits a modifier for have one");
	}
	{
		TechTree t;
		Check(!LoadTechs(R"({ "techs": [ { "id": "x", "modifiers": { "luck": 2 } } ] })", t).empty(), "(data) an unknown key is refused");
		Check(!LoadTechs(R"({ "techs": [ { "id": "x", "modifiers": { "hunt": 0 } } ] })", t).empty(), "(data) a factor of 0 is refused");
		Check(!LoadTechs(R"({ "techs": [ { "id": "x", "modifiers": [ 1 ] } ] })", t).empty(), "(data) modifiers must be an object");
		Check(LoadTechs(R"({ "techs": [ { "id": "x", "modifiers": { "hunt": 1.25, "night": 1.5 } } ] })", t).empty() && t.Find("x") && t.Find("x")->Modifiers.size() == 2, "(data) a good one is read");
		TechTree back;
		Check(LoadTechs(TechsToJson(tree), back).empty() && back.Techs == tree.Techs, "(data) the game's tree survives TechsToJson with its modifiers");
		Check(TechModifiers::Of(&tree, { "butchery", "blade_cores" }, "hunt") > TechModifiers::Of(&tree, { "butchery" }, "hunt") && TechModifiers::Of(nullptr, { "butchery" }, "hunt") == 1.0f, "(data) two technologies with a key multiply; no tree is 1");
		Check(TechModifiers::Night(1.6f, 2.0f) == 1.6f && TechModifiers::Night(0.4f, 1.5f) > 0.4f && TechModifiers::Night(0.8f, 2.0f) == 1.0f, "(data) the night lifts only work held back, never above 1");
	}

	// ---- (each) every technology with a modifier changes what it says, measured with it and without it
	const std::map<std::string, Measure> measures = { { "hunt", MeatOfAKill }, { "heal", Healing }, { "grief", GriefAtADeath }, { "mood", MoodAfterADay },
		{ "night", NightWork }, { "spoil", StoreAgeing }, { "carry", DeliverySlots } };
	for (const auto& [key, techs] : byKey)
	{
		const auto m = measures.find(key);
		Check(m != measures.end(), "(each) every key of the data has a measure here");
		if (m == measures.end()) continue;
		const float without = m->second(tree, "");
		for (const std::string& tech : techs)
		{
			const float f = tree.Find(tech)->Modifiers.at(key);
			const float with = m->second(tree, tech);
			std::printf("  %-14s %-6s x%.2f: %.3f without, %.3f with\n", tech.c_str(), key.c_str(), static_cast<double>(f), static_cast<double>(without), static_cast<double>(with));
			Check(without >= 0.0f && with >= 0.0f, "(each) the measure ran");
			const bool up = key == "carry" ? false : (f > 1.0f);   // (more carried: fewer slots)
			const bool ok = up ? with > without + 1e-4f : with < without - 1e-4f;
			if (!ok) std::fprintf(stderr, "  %s (%s): no effect in the expected direction\n", tech.c_str(), key.c_str());
			Check(ok, "(each) the technology's modifier changes what it says");
		}
	}

	if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
	else std::printf("tech_modifiers: ok\n");
	return failures ? 1 : 0;
}
