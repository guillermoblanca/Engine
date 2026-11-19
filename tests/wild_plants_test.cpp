// T328 + T335 (ARBOL_TECNICO §11.5; informe D60): what a clan of the AI does by itself with the wild plants, on a clan with stand-in bodies (the
// village of transplant_test, without the nodes the case is about) and the game's technologies and data:
//   (seed)  a clan that knows tillage and holds no seed goes for the wild grain it knows 150 m off, past its home ground, and brings seed to the
//           store; with economy.farming.wildSeedReach 0 (off), or without tillage, it does not; and it finds one it does not know by looking
//           (the search out to the reach);
//   (bush)  a clan that knows plant care and has no berry bush by its camp orders one it knows moved there (Transplant, T325), and it stands
//           within transplantNear of the camp afterwards; without plant care, or with transplantNear 0 (off), nothing is ordered.
#include "GB/Log.h"
#include "GB/Game/BushKeeper.h"
#include "GB/Game/ColonyOrders.h"
#include "GB/Game/Economy.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/WildSeeds.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/Techs.h"
#include "GB/Gameplay/WorldResources.h"
#include <cmath>
#include <cstdio>
#include <functional>
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
class WildBody : public ITribeBody
{
public:
	WildBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
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
			if (s.Kind == "goto") { s.Seconds = glm::distance(m_Pos, glm::vec2(s.X, s.Z)) / 3.0f; m_From = m_Pos; m_Target = { s.X, s.Z }; m_Total = s.Seconds; }
			if (s.Kind == "ability")
			{
				const EntityId id = m_Entities.FindByName(s.Node);
				const glm::vec4 p = id == NullEntity ? glm::vec4(0.0f) : m_Entities.WorldMatrix(id)[3];
				s.Seconds = glm::distance(m_Pos, glm::vec2(p.x, p.z)) / 3.0f;
				m_From = m_Pos;
				m_Target = { p.x, p.z };
				m_Total = s.Seconds;
				s.Walking = true;
			}
		}
		if (s.Seconds > 0.0f)
		{
			s.Seconds -= step;
			if (s.Seconds > 0.0f)
			{
				if ((s.Kind == "goto" || s.Walking) && m_Total > 0.0f) m_Pos = m_From + (m_Target - m_From) * (1.0f - s.Seconds / m_Total);   // (it walks: it sees on the way)
				return;
			}
			if (s.Kind == "goto" || s.Walking) m_Pos = m_Target;
		}
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
	glm::vec2 m_Pos, m_From{ 0.0f }, m_Target{ 0.0f };
	float m_Total = 0.0f;
	Inventory m_Bag{ 12, 0.0f };
	std::vector<Step> m_Steps;
	bool m_Started = false, m_Failed = false;
};

// A clan of four by a patch of the game's nodes and a well, without the kinds `leave` says, and the nodes of `far` placed where the test says.
struct Wilds
{
	Wilds(const std::function<bool(const ResourceNodeDef&)>& leave, const std::vector<ResourceSpot>& far)
	{
		for (const auto& [id, t] : Techs.Techs)
			for (const std::string& b : t.Buildings) Builds.Defs.erase(b);
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		RegisterBuildingContent(Gameplay.Library);
		std::map<std::string, ResourceNodeDef> patch;
		for (const auto& [id, def] : Defs) if (!leave(def)) patch[id] = def;
		BuildResourcePatch(Entities, glm::vec3(0.0f), patch, 4);
		BuildWorldResources(Entities, far, Defs);
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
			bodies.push_back(std::make_unique<WildBody>(n, Gameplay, Entities, Camp));
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
		Tribe_.SetTechs(&Techs);
		Tribe_.Config.Econ = DefaultEconomyRules();   // (the game's economy section, as GameContent gives the game: the rules of the case are there)
		Tribe_.Config.Economy.Camp = Camp;
		Tribe_.Config.Simulation.LogLines = 1000000;   // (the whole log is read)
		Tribe_.Config.PlanSeconds = 1e9f;              // (the planner does not start sites of its own)
		Tribe_.Store().Add(Items, "wood", 10);
		Tribe_.Store().Add(Items, "stone", 12);
		Tribe_.Store().Add(Items, "berry", 20);        // (fed: the case is not about hunger)
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
	bool Logged(const std::string& part) const
	{
		for (const std::string& l : Tribe_.Log()) if (l.find(part) != std::string::npos) return true;
		return false;
	}
	glm::vec2 PosOf(const std::string& name) const
	{
		const EntityId id = Entities.FindByName(name);
		if (id == NullEntity) return glm::vec2(-1e6f);
		const glm::vec4 p = Entities.WorldMatrix(id)[3];
		return { p.x, p.z };
	}
	std::string NodeOf(const std::string& kind) const
	{
		for (const GameplayWorld::Tagged& t : Gameplay.WithTag("Resource.Node." + kind)) return *t.Name;
		return {};
	}
	const glm::vec2 Camp{ 0.0f, -8.0f };
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Fires;
	BuildingSystem Builds;
	TechTree Techs = DefaultTechs();
	Tribe Tribe_;
	std::vector<std::unique_ptr<WildBody>> bodies;
};

ResourceSpot Spot(const std::string& node, float x, float z)
{
	ResourceSpot s;
	s.Node = node;
	s.Position = { x, 0.0f, z };
	return s;
}

const std::set<std::string> kSeeds = { "grain_seed", "barley_seed", "lentil_seed", "pea_seed", "flax_seed" };
bool GivesSeed(const ResourceNodeDef& d) { return kSeeds.count(d.Item) || kSeeds.count(d.ExtraItem); }
int Seeds(const Tribe& t) { int n = 0; for (const std::string& s : kSeeds) n += t.Stored(s); return n; }

// (seed): `reach` < 0 keeps the data's; `tillage` teaches it; `known` puts the far wild grain in every member's memory. When the first seed
// came to the store (-1: never), the plots sown after `seconds` (the clan marks a field and sows what it brought), the farthest search for
// wild grain from the camp, and the first line of the log about the node.
struct SeedRun { double First = -1.0; int Sown = 0; float Search = 0.0f; float Reach = 0.0f; bool Wanted = false; std::string Line; };
SeedRun RunSeeds(float reach, bool tillage, bool known, float seconds)
{
	const glm::vec2 grain(0.0f, 142.0f);   // 150 m from the camp: past the home ground (HomeRadius + HomeFalloff)
	Wilds w(GivesSeed, { Spot("wild_grain", grain.x, grain.y) });
	if (reach >= 0.0f) w.Tribe_.Config.Econ.Farming.WildSeedReach = reach;
	SeedRun out;
	out.Reach = w.Tribe_.Config.Econ.Farming.WildSeedReach;
	if (tillage) Check(w.Tribe_.Knowledge().GrantClan("tillage").empty(), "(the clan learns tillage)");
	const std::string node = w.NodeOf("wild_grain");
	Check(!node.empty() && glm::distance(w.PosOf(node), w.Camp) > 140.0f, "(a wild grain 150 m from the camp)");
	for (int i = 0; i < 20; ++i) w.Step();
	if (known)
		for (TribeMember& m : w.Tribe_.Members())
			m.Mind.Memory.Observe({ node, "wild_grain", w.PosOf(node), 6, 1.0, false });
	out.Wanted = WildSeeds::Wanted(w.Tribe_);
	for (int i = 0; i < static_cast<int>(seconds / dt); ++i)
	{
		w.Step();
		if (out.First < 0.0 && Seeds(w.Tribe_) > 0) out.First = static_cast<double>(i) * dt;
	}
	for (const Field& f : w.Tribe_.Economy().Fields) out.Sown += f.Count("sown") + f.Count("sprout") + f.Count("grown");
	for (const std::string& l : w.Tribe_.Log())
	{
		if (out.Line.empty() && l.find(node) != std::string::npos) out.Line = l;
		const size_t at = l.find("goes to look for wild_grain at ");
		float x = 0.0f, z = 0.0f;
		if (at != std::string::npos && std::sscanf(l.c_str() + at + 31, "%f, %f", &x, &z) == 2) out.Search = std::max(out.Search, glm::distance(glm::vec2(x, z), w.Camp));
	}
	return out;
}
}

int GBTest_wild_plants(int, char**)
{
	Log::Init();

	// ---- (seed) T328
	{
		const SeedRun on = RunSeeds(-1.0f, true, true, 600.0f);
		std::fprintf(stderr, "  wild seed, reach %.0f m (data), tillage, known: wanted %d, the first seed in the store at %.0f s, %d plot(s) sown; %s\n", static_cast<double>(on.Reach),
			on.Wanted ? 1 : 0, on.First, on.Sown, on.Line.c_str());
		Check(on.Reach >= 150.0f, "(seed) the game's data sets a wild seed reach past 150 m (economy.farming.wildSeedReach)");
		Check(on.Wanted, "(seed) a clan that knows tillage and holds no seed wants wild seed");
		Check(on.First >= 0.0 && on.First < 600.0, "(seed) and brings seed of the wild grain 150 m off to its store within ten minutes");
		Check(on.Sown > 0, "(seed) and sows it");

		const SeedRun off = RunSeeds(0.0f, true, true, 600.0f);
		std::fprintf(stderr, "  reach 0: wanted %d, first seed at %.0f s, %d sown\n", off.Wanted ? 1 : 0, off.First, off.Sown);
		Check(!off.Wanted && off.First < 0.0 && off.Sown == 0, "(seed) with wildSeedReach 0 it does not go (off)");

		const SeedRun untaught = RunSeeds(-1.0f, false, true, 600.0f);
		std::fprintf(stderr, "  without tillage: wanted %d, first seed at %.0f s\n", untaught.Wanted ? 1 : 0, untaught.First);
		Check(!untaught.Wanted && untaught.First < 0.0, "(seed) without tillage it does not go");

		// knowing of none, it looks past the home ground's searches (ExploreRadius + 3 RadiusPerTrip: 46 m); whether a search finds one depends on
		// the bearing (one node here, a zone of wild grain in a world), so only the search is checked
		const SeedRun search = RunSeeds(-1.0f, true, false, 600.0f);
		std::fprintf(stderr, "  not known: the farthest search %.0f m from the camp, first seed at %.0f s\n", static_cast<double>(search.Search), search.First);
		Check(search.Search > 60.0f && search.Search <= search.Reach + 1.0f, "(seed) knowing of none, it searches for wild grain past the home ground, within the reach");

		const SeedRun again = RunSeeds(-1.0f, true, true, 600.0f);
		Check(again.First == on.First && again.Sown == on.Sown && again.Line == on.Line, "(seed) the same run does the same (deterministic)");
	}

	// ---- (bush) T335
	{
		auto movable = [](const ResourceNodeDef& d) { return !d.Transplant.Item.empty(); };   // (no bush, herb or young tree in the patch)
		auto run = [&](bool care, float near, int& orders, float& after, std::string& line)
		{
			Wilds w(movable, { Spot("berry_bush", 30.0f, 22.0f) });   // ~42 m from the camp: on the home ground, not by the camp
			if (near >= 0.0f) w.Tribe_.Config.Econ.Farming.TransplantNear = near;
			if (care) Check(w.Tribe_.Knowledge().GrantClan("plant_care").empty(), "(the clan learns plant care)");
			const std::string bush = w.NodeOf("berry_bush");
			orders = 0;
			bool held = false;
			for (int i = 0; i < static_cast<int>(240.0f / dt); ++i)
			{
				w.Step();
				bool now = false;
				for (const auto& [who, d] : w.Tribe_.Colony().All()) now = now || d.Verb == "transplant";
				if (now && !held) ++orders;
				held = now;
			}
			after = glm::distance(w.PosOf(bush), w.Camp);
			line.clear();
			for (const std::string& l : w.Tribe_.Log()) if (l.find("berries by the camp") != std::string::npos) { line = l; break; }
			return w.Tribe_.Config.Econ.Farming.TransplantNear;
		};
		int orders = 0;
		float after = 0.0f;
		std::string line;
		const float near = run(true, -1.0f, orders, after, line);
		std::fprintf(stderr, "  bush, near %.0f m (data), plant care: %d order(s), the bush %.1f m from the camp after; %s\n", static_cast<double>(near), orders, static_cast<double>(after), line.c_str());
		Check(near > 0.0f, "(bush) the game's data sets transplantNear (economy.farming.transplantNear)");
		Check(orders == 1 && !line.empty(), "(bush) with plant care and no bush by the camp the clan orders one moved (once)");
		Check(after <= near, "(bush) and it stands by the camp afterwards");

		run(false, -1.0f, orders, after, line);
		std::fprintf(stderr, "  without plant care: %d order(s), the bush %.1f m from the camp\n", orders, static_cast<double>(after));
		Check(orders == 0 && after > 40.0f, "(bush) without plant care nothing is ordered");

		run(true, 0.0f, orders, after, line);
		std::fprintf(stderr, "  transplantNear 0: %d order(s), the bush %.1f m from the camp\n", orders, static_cast<double>(after));
		Check(orders == 0 && after > 40.0f, "(bush) with transplantNear 0 nothing is ordered (off)");
	}

	if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
	else std::printf("wild_plants: ok\n");
	return failures ? 1 : 0;
}
