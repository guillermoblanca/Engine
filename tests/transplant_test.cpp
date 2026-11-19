// T325 (documentation/01_diseno/ARBOL_TECNICO.md §11.5, §11.7.2): moving a resource node (GB/Game/Transplant.h). On a clan with stand-in bodies
// (the village of colony_orders_test) and the game's technologies:
//   (a) without plant_care the order to transplant a berry bush is refused ("does not know how to transplant"), and a young tree is refused
//       without arboriculture even when plant_care is known;
//   (b) with plant_care a member digs the bush up (a cutting in its bag), carries it and plants it: the node stands at the new place, empty,
//       and after its regrowth time it has berries again, which a `use` order gathers there;
//   (c) an order in course with the cutting carried is saved ("carry") and loads the same; cancelled, the cutting goes back in the ground;
//   (d) the scene's save (DiffEntities/PatchEntities over the unmoved village) keeps the node at its new place with its amount;
//   (e) T333: the member carrying the cutting dies: the order ends with a notice, the cutting does not stay in the body's bag, the bush stays
//       where it was, nobody holds it, and another member moves it; (f) the same for a member that leaves the clan with it;
//   (g) T333: two clans on the same land (a ClanWorld): one may not move the bush the other is moving, and once it is planted elsewhere the
//       other clan's map and memories have nothing of where it was.
#include "GB/Log.h"
#include "GB/Game/Clans.h"
#include "GB/Game/ColonyOrders.h"
#include "GB/Game/Transplant.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Gameplay/Techs.h"
#include "GB/Save/SaveWorld.h"
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

// File-local (T51: several tests share one executable)
namespace
{
class PlantBody : public ITribeBody
{
public:
	PlantBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
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

// A clan of four with a patch of resource nodes and a well (as clan_stores_test's village), no player among them.
struct Garden
{
	Garden()
	{
		for (const auto& [id, t] : Techs.Techs)
			for (const std::string& b : t.Buildings) Builds.Defs.erase(b);
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		RegisterBuildingContent(Gameplay.Library);
		BuildResourcePatch(Entities, glm::vec3(0.0f), Defs, 4);
		BuildInteractive(Entities, DefaultInteractives().at("well"), "Well", NullEntity, { 4.0f, 0.0f, -14.0f });
		const char* names[] = { "Ana", "Biel", "Cleo", "Dan", "Eva", "Finn" };   // (Eva and Finn: no member of this clan; the other clan of (g))
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
			bodies.push_back(std::make_unique<PlantBody>(n, Gameplay, Entities, Camp));
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
		Tribe_.Config.Economy.Camp = Camp;
		Tribe_.Config.Simulation.LogLines = 1000000;   // (the whole log is read)
		Tribe_.Config.PlanSeconds = 1e9f;              // (the planner does not start sites of its own: the test does)
		Tribe_.Store().Add(Items, "wood", 10);
		Tribe_.Store().Add(Items, "stone", 12);
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
	void Run(float seconds) { for (int i = 0; i < static_cast<int>(seconds / dt); ++i) Step(); }
	bool Logged(const std::string& part) const
	{
		for (const std::string& l : Tribe_.Log()) if (l.find(part) != std::string::npos) return true;
		return false;
	}
	const TribeMember& Member(const std::string& name) const { return *Tribe_.FindMember(name); }
	PlantBody& Body(const std::string& name) { for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == name) return *bodies[i]; return name == "Eva" ? *bodies[4] : name == "Finn" ? *bodies[5] : *bodies[0]; }
	void Need(const std::string& name, const char* need, float value) { Gameplay.Find(name)->Attributes().SetBase(need, value); }
	ColonyOrders::Reply Order(const std::string& who, const std::string& verb, std::vector<std::string> args = {}) { return Tribe_.Colony().Give(Tribe_, who, verb, args); }
	// A node of a kind (the first by name), and how much it holds now.
	std::string NodeOf(const std::string& kind) const
	{
		for (const GameplayWorld::Tagged& t : Gameplay.WithTag("Resource.Node." + kind)) return *t.Name;
		return {};
	}
	int Amount(const std::string& node) const { const AbilitySystemComponent* a = Gameplay.Find(node); return a ? ResourceSystem::Amount(*a) : -1; }
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
	std::vector<std::unique_ptr<PlantBody>> bodies;
};
}


namespace
{
	glm::vec2 PosOf(const EntityWorld& w, const std::string& name)
	{
		const EntityId id = w.FindByName(name);
		if (id == NullEntity) return glm::vec2(-1e6f);
		const glm::vec4 p = w.WorldMatrix(id)[3];
		return { p.x, p.z };
	}
	void Until(Garden& g, const std::string& who, float seconds)
	{
		for (int i = 0; i < static_cast<int>(seconds / dt) && g.Tribe_.Colony().Has(who); ++i) g.Step();
	}
}

int GBTest_transplant(int, char**)
{
	Log::Init();
	const TechTree tree = DefaultTechs();
	const glm::vec2 to(-12.0f, -18.0f);
	const std::string dest = "to -12 -18";

	// ---- (data) the shipped pack, strict, with the new items and the nodes' transplants: no problem (what the packs test asks)
	for (const std::string& p : Shipped().Problems) std::fprintf(stderr, "  pack problem: %s\n", p.c_str());
	Check(Shipped().Error.empty() && Shipped().Problems.empty(), "(data) the game's pack loads strict with the transplants and their items");
	Check(Shipped().Items.Find("bush_cutting") && Shipped().Items.Find("herb_clump") && Shipped().Items.Find("sapling"), "(data) the cutting, the clump and the sapling are items");
	Check(Shipped().Nodes.count("young_tree") && Shipped().Nodes.at("young_tree").Transplant.Verb == "transplant_tree", "(data) a young tree is moved with transplant_tree");

	// ---- (a) the verb gates the order; (b) dig, carry, plant, regrow, gather
	{
		Garden g;
		g.Tribe_.SetTechs(&tree);
		g.Run(1.0f);
		const std::string bush = g.NodeOf("berry_bush");
		const ResourceNodeDef* def = Transplant::DefOf(g.Tribe_, bush);
		Check(!bush.empty() && def && def->Transplant.Item == "bush_cutting" && def->Transplant.Verb == "transplant", "(data) a berry bush can be moved as a cutting, with the verb transplant");
		const ColonyOrders::Reply no = g.Order("Ana", "transplant", { bush + " " + dest });
		std::fprintf(stderr, "  without plant_care: %s\n", no.Text.c_str());
		Check(!no.Ok && no.Text == "Ana does not know how to transplant" && !g.Tribe_.Colony().Has("Ana"), "(a) without plant_care the order is refused");
		Check(!Transplant::Assign(g.Tribe_, bush + " " + dest).Ok, "(a) and so is game transplant");

		Check(g.Tribe_.Knowledge().GrantClan("plant_care").empty(), "(the clan learns plant care)");
		const std::string sapling = g.NodeOf("young_tree");
		Check(!sapling.empty(), "(a young tree in the patch)");
		if (!sapling.empty())
		{
			const ColonyOrders::Reply t = g.Order("Biel", "transplant", { sapling + " " + dest });
			std::fprintf(stderr, "  a tree with plant_care only: %s\n", t.Text.c_str());
			Check(!t.Ok && t.Text == "Biel does not know how to transplant a tree", "(a) a young tree needs arboriculture (transplant_tree)");
		}
		Check(!g.Order("Ana", "transplant", { bush + " to 1 2 3" }).Ok, "(a) a malformed order is refused");

		const glm::vec2 from = PosOf(g.Entities, bush);
		const ColonyOrders::Reply yes = g.Order("Ana", "transplant", { bush + " " + dest });
		std::fprintf(stderr, "  with plant_care: %s\n", yes.Text.c_str());
		Check(yes.Ok && g.Tribe_.Colony().Has("Ana"), "(b) with plant_care the order is taken");
		bool carried = false;
		for (int i = 0; i < static_cast<int>(120.0f / dt) && g.Tribe_.Colony().Has("Ana"); ++i)
		{
			g.Step();
			carried = carried || g.Body("Ana").Bag().Count("bush_cutting") > 0;
		}
		Check(carried, "(b) the cutting was carried in the bag");
		Check(!g.Tribe_.Colony().Has("Ana") && g.Body("Ana").Bag().Count("bush_cutting") == 0, "(b) the order is done and the cutting left the bag");
		const glm::vec2 now = PosOf(g.Entities, bush);
		std::fprintf(stderr, "  %s: from %.1f %.1f to %.1f %.1f, amount %d\n", bush.c_str(), from.x, from.y, now.x, now.y, g.Amount(bush));
		Check(glm::distance(now, to) < 0.05f, "(b) the bush stands at the new place");
		Check(g.Logged("plants " + bush), "(b) a line says it was planted");
		Check(g.Amount(bush) == 0, "(b) it starts empty");
		g.Run(def ? def->RegenSeconds + 1.0f : 21.0f);
		const int grown = g.Amount(bush);
		Check(grown > 0, "(b) after its regrowth time it has berries again");
		const int store = g.Tribe_.Store().Count("berry");
		const int bag = g.Body("Cleo").Bag().Count("berry");
		const ColonyOrders::Reply use = g.Order("Cleo", "use", { bush });
		Until(g, "Cleo", 60.0f);
		std::fprintf(stderr, "  use: %s; berries in the store %d -> %d, in Cleo's bag %d -> %d\n", use.Text.c_str(), store, g.Tribe_.Store().Count("berry"), bag, g.Body("Cleo").Bag().Count("berry"));
		Check(use.Ok && (g.Tribe_.Store().Count("berry") > store || g.Body("Cleo").Bag().Count("berry") > bag), "(b) berries are gathered from it at the new place");

		// ---- (d) the scene's save: against the village as it was made, the moved bush keeps its place and its amount
		Garden base;
		g.Gameplay.Store(g.Entities);
		const Save::EntityDiff diff = Save::DiffEntities(base.Entities, g.Entities);
		EntityWorld loaded;
		const Save::Result patched = Save::PatchEntities(base.Entities, diff, loaded);
		GameplayWorld gp;
		gp.Load(loaded);
		const AbilitySystemComponent* back = gp.Find(bush);
		Check(patched.Ok && back && glm::distance(PosOf(loaded, bush), to) < 0.05f, "(d) the scene's save keeps the bush at its new place");
		Check(back && ResourceSystem::Amount(*back) == g.Amount(bush), "(d) and its amount");
	}

	// ---- (c) an order in course with the cutting carried: saved, loaded, and cancelled puts it back
	{
		Garden g;
		g.Tribe_.SetTechs(&tree);
		g.Run(1.0f);
		g.Tribe_.Knowledge().GrantClan("plant_care");
		const std::string bush = g.NodeOf("berry_bush");
		const glm::vec2 from = PosOf(g.Entities, bush);
		const ColonyOrders::Reply sent = Transplant::Assign(g.Tribe_, std::to_string(from.x) + " " + std::to_string(from.y) + " " + dest);
		std::fprintf(stderr, "  game transplant x z: %s\n", sent.Text.c_str());
		Check(sent.Ok, "(c) game transplant <x z> sends someone");
		std::string who;
		for (const auto& [name, d] : g.Tribe_.Colony().All()) if (d.Verb == "transplant") who = name;
		for (int i = 0; i < static_cast<int>(60.0f / dt) && !who.empty(); ++i)
		{
			g.Step();
			const ColonyOrders::Mandate* d = g.Tribe_.Colony().Of(who);
			if (!d || !d->Carry.empty()) break;
		}
		const ColonyOrders::Mandate* d = who.empty() ? nullptr : g.Tribe_.Colony().Of(who);
		Check(d && d->Carry == "berry_bush" && d->Target == bush, "(c) the bush is dug up and carried");
		const std::string saved = g.Tribe_.ToJson();
		Check(g.Tribe_.Colony().ToJson().find("\"carry\"") != std::string::npos, "(c) the order in course is saved with what it carries");
		Garden w;
		w.Tribe_.SetTechs(&tree);
		w.Run(1.0f);
		Check(w.Tribe_.FromJson(saved, true), "(c) the save loads");
		const ColonyOrders::Mandate* back = who.empty() ? nullptr : w.Tribe_.Colony().Of(who);
		Check(back && d && *back == *d && w.Tribe_.Colony().ToJson() == g.Tribe_.Colony().ToJson(), "(c) and holds the same order");
		Check(!who.empty() && g.Tribe_.Colony().Cancel(g.Tribe_, who).Ok && g.Body(who).Bag().Count("bush_cutting") == 0, "(c) cancelled, the cutting leaves the bag");
		Check(glm::distance(PosOf(g.Entities, bush), from) < 0.05f && g.Logged("back in the ground"), "(c) and the bush stays where it was");
		Check(g.Tribe_.Colony().Empty() && g.Tribe_.ToJson().find("\"mandates\"") == std::string::npos, "(c) no order left, nothing saved under orders");
	}

	// ---- (e) T333: the one who carries the cutting dies; (f) one leaves the clan with it
	for (const bool dies : { true, false })
	{
		Garden g;
		g.Tribe_.SetTechs(&tree);
		g.Run(1.0f);
		g.Tribe_.Knowledge().GrantClan("plant_care");
		const std::string bush = g.NodeOf("berry_bush");
		const glm::vec2 from = PosOf(g.Entities, bush);
		const std::string who = dies ? "Ana" : "Biel";
		const std::string tag = dies ? "(e)" : "(f)";
		auto say = [&tag](const char* what) { return tag + " " + what; };
		Check(g.Order(who, "transplant", { bush + " " + dest }).Ok, say("the order is taken").c_str());
		for (int i = 0; i < static_cast<int>(60.0f / dt); ++i)
		{
			const ColonyOrders::Mandate* d = g.Tribe_.Colony().Of(who);
			if (!d || !d->Carry.empty()) break;
			g.Step();
		}
		const ColonyOrders::Mandate* d = g.Tribe_.Colony().Of(who);
		Check(d && d->Carry == "berry_bush", say("the bush is dug up and carried").c_str());
		PlantBody& body = g.Body(who);
		Check(body.Bag().Count("bush_cutting") == 1, say("the cutting is in its bag").c_str());
		if (dies) Check(g.Tribe_.Kill(who, "Wounds"), "(e) it dies");
		else
		{
			MemberTransfer out;
			Check(g.Tribe_.Detach(who, out), "(f) it leaves the clan");
		}
		const auto& notices = g.Tribe_.Colony().Notices();
		std::fprintf(stderr, "  %s %s: notices %zu (%s), cutting in its bag %d, %s at %.1f %.1f\n", tag.c_str(), who.c_str(), notices.size(),
			notices.empty() ? "" : notices.back().Reason.c_str(), body.Bag().Count("bush_cutting"), bush.c_str(), PosOf(g.Entities, bush).x, PosOf(g.Entities, bush).y);
		Check(!g.Tribe_.Colony().Has(who) && g.Tribe_.Colony().Empty(), say("the order is gone").c_str());
		Check(!notices.empty() && notices.back().Member == who && notices.back().Reason == (dies ? "died" : "left"), say("with a notice that says why").c_str());
		Check(body.Bag().Count("bush_cutting") == 0, say("the cutting does not stay in its bag").c_str());
		Check(glm::distance(PosOf(g.Entities, bush), from) < 0.05f && g.Amount(bush) == 0 && g.Logged("back in the ground where it was"), say("the bush stays where it was, empty (it grows again there)").c_str());
		Check(g.Tribe_.ToJson().find("\"mandates\"") == std::string::npos, say("nothing of the order is saved").c_str());
		const ColonyOrders::Reply again = g.Order("Cleo", "transplant", { bush + " " + dest });
		std::fprintf(stderr, "  %s Cleo takes it on: %s\n", tag.c_str(), again.Text.c_str());
		Check(again.Ok, say("nobody holds the bush: another member takes the order").c_str());
		Until(g, "Cleo", 120.0f);
		Check(glm::distance(PosOf(g.Entities, bush), to) < 0.05f && g.Body("Cleo").Bag().Count("bush_cutting") == 0, say("and plants it").c_str());
	}

	// ---- (g) T333: two clans on the same land
	{
		Garden g;
		PlantBody& eva = g.Body("Eva");   // (the Garden's two bodies that are no member of its clan)
		PlantBody& finn = g.Body("Finn");
		eva.Teleport(g.Camp + glm::vec2(6.0f, 0.0f));
		finn.Teleport(g.Camp + glm::vec2(6.0f, 1.0f));
		Tribe other;   // (the Blues: Eva and Finn, their camp a little to the east)
		other.SetClanName("Blues");
		other.Bind(g.Entities, g.Gameplay, g.Items, g.Recipes, g.Defs);
		other.SetBuildings(&g.Builds.Defs);
		other.Config.Economy.Camp = g.Camp + glm::vec2(6.0f, 0.0f);
		other.Config.Simulation.LogLines = 1000000;
		other.Config.PlanSeconds = 1e9f;
		other.AddMember("Eva", "forager", &eva);
		other.AddMember("Finn", "forager", &finn);
		g.Tribe_.SetClanName("Reds");
		g.Tribe_.SetTechs(&tree);
		other.SetTechs(&tree);
		ClanWorld clans;
		clans.Add("Reds", &g.Tribe_);
		clans.Add("Blues", &other);
		auto tick = [&]() { g.Step(); other.Update(dt); };
		auto step = [&](float seconds) { for (int i = 0; i < static_cast<int>(seconds / dt); ++i) tick(); };
		step(1.0f);
		g.Tribe_.Knowledge().GrantClan("plant_care");
		other.Knowledge().GrantClan("plant_care");
		const std::string bush = g.NodeOf("berry_bush");
		const glm::vec2 from = PosOf(g.Entities, bush);
		step(10.0f);   // (the Blues look round their camp: the bush may be in their memories)
		bool seen = other.ClanMemory().Find(bush) != nullptr;
		for (const TribeMember& m : other.Members()) seen = seen || m.Mind.Memory.Find(bush);
		std::fprintf(stderr, "  (g) the Blues know %s before: %d\n", bush.c_str(), seen ? 1 : 0);
		Check(g.Order("Ana", "transplant", { bush + " " + dest }).Ok, "(g) the Reds move the bush");
		const ColonyOrders::Reply no = other.Colony().Give(other, "Eva", "transplant", { bush + " to -14 -20" });
		const ColonyOrders::Reply no2 = Transplant::Assign(other, bush + " to -14 -20");
		std::fprintf(stderr, "  (g) the Blues: %s / %s\n", no.Text.c_str(), no2.Text.c_str());
		Check(!no.Ok && no.Text.find("the Reds are moving " + bush) != std::string::npos && !other.Colony().Has("Eva"), "(g) the Blues may not move the bush the Reds are moving");
		Check(!no2.Ok, "(g) nor send anyone with game transplant");
		for (int i = 0; i < static_cast<int>(120.0f / dt) && g.Tribe_.Colony().Has("Ana"); ++i) tick();
		Check(!g.Tribe_.Colony().Has("Ana") && glm::distance(PosOf(g.Entities, bush), to) < 0.05f, "(g) the Reds plant it");
		auto stale = [&]()
		{
			bool old = false;
			if (const MemoryEntry* e = other.ClanMemory().Find(bush)) old = old || glm::distance(e->Pos, from) < 0.05f;
			for (const TribeMember& m : other.Members()) if (const MemoryEntry* e = m.Mind.Memory.Find(bush)) old = old || glm::distance(e->Pos, from) < 0.05f;
			return old;
		};
		Check(!stale(), "(g) the Blues remember nothing of where it was");
		step(5.0f);
		Check(!stale(), "(g) and their senses do not put it there again (their cache of places)");
		const ColonyOrders::Reply now = other.Colony().Give(other, "Eva", "transplant", { bush + " to -14 -20" });
		std::fprintf(stderr, "  (g) once planted, the Blues: %s\n", now.Text.c_str());
		Check(now.Ok, "(g) once it is planted nobody holds it: the Blues may move it now");
	}

	if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
	else std::printf("transplant: ok\n");
	return failures ? 1 : 0;
}
