// T255: the store buildings as the place a clan's things are kept (GB/Game/ClanStores.h). Checked: without a store building everything is left by
// the fire, as before; with a storage the members walk to it and leave things there (the deposit counts at the building, not at the fire), the
// pile by the fire is carried in, a building site's materials are fetched from it, and the ledger never holds more than the store; the ledger saves
// and loads, a save from before loads with everything by the fire, and an older save's store building (a stockpile of its name) moves into the
// ledger; the totals the clan reads (Store, game status) are the same wherever the things lie. CPU only, stand-in bodies as in settlement_test.
#include "GB/Log.h"
#include "GB/Game/ClanStores.h"
#include "GB/Game/StoreLayout.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
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

	// The text without one key of the top object (and its value), for a "save from before" (as in economy_test).
	std::string DropKey(const std::string& json, const std::string& key)
	{
		const size_t at = json.find("\"" + key + "\":");
		if (at == std::string::npos) return json;
		size_t i = at + key.size() + 3;
		int depth = 0;
		bool text = false;
		for (; i < json.size(); ++i)
		{
			const char c = json[i];
			if (text) { if (c == '\\') ++i; else if (c == '"') text = false; continue; }
			if (c == '"') text = true;
			else if (c == '{' || c == '[') ++depth;
			else if (c == '}' || c == ']') { if (--depth == 0) { ++i; break; } }
			else if (depth == 0 && c == ',') break;
		}
		size_t from = at, to = i;
		if (to < json.size() && json[to] == ',') ++to;
		else if (from > 0 && json[from - 1] == ',') --from;
		return json.substr(0, from) + json.substr(to);
	}
}

using namespace GB;

// File-local (T51: several tests share one executable)
namespace
{
class FakeBody : public ITribeBody
{
public:
	FakeBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities), m_Pos(start) {}
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

// A clan of four with a patch of resource nodes, a well and the four buildings no technology locks (as settlement_test's plain settlement).
struct Village
{
	Village()
	{
		for (const auto& [id, t] : Techs.Techs)
			for (const std::string& b : t.Buildings) Builds.Defs.erase(b);
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		RegisterBuildingContent(Gameplay.Library);
		BuildResourcePatch(Entities, glm::vec3(0.0f), Defs, 4);
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
			bodies.push_back(std::make_unique<FakeBody>(n, Gameplay, Entities, Camp));
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
	bool Logged(const std::string& part) const
	{
		for (const std::string& l : Tribe_.Log()) if (l.find(part) != std::string::npos) return true;
		return false;
	}
	// for every item the buildings hold no more than the store
	bool LedgerWithinStore() const
	{
		for (const auto& [b, items] : Tribe_.Stores().Ledger())
			for (const auto& [id, n] : items) if (n <= 0 || Tribe_.Stores().InBuildings(id) > Tribe_.Store().Count(id)) return false;
		return true;
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
	std::vector<std::unique_ptr<FakeBody>> bodies;
};
}

int GBTest_clan_stores(int, char**)
{
	Log::Init();

	// ---- without a store building: everything is left by the fire, as before
	{
		Village v;
		for (int i = 0; i < static_cast<int>(240.0f / dt); ++i) v.Step();
		Check(!ClanStores::Active(v.Tribe_) && v.Tribe_.Stores().Ledger().empty(), "without a store building no building holds anything");
		Check(v.Logged("in the store") && !v.Logged("in the store at "), "and the deposits are left in the store by the fire");
		Check(StoreLayout::Of(v.Tribe_).InBuildings() == 0 && StoreLayout::Of(v.Tribe_).AtCampUnits() > 0, "the layout has it all by the fire");
	}

	// ---- with a storage: deposits walk to it, the pile by the fire is carried in, a site takes its materials from it
	{
		Village v;
		std::string storage;
		Check(v.Tribe_.RaiseBuilding("storage", v.Camp + glm::vec2(14.0f, 0.0f), 0.0f, &storage).empty() && !storage.empty(), "a storage stands 14 m from the fire");
		StoreLayout::Place shed;
		Check(ClanStores::Active(v.Tribe_) && ClanStores::Find(v.Tribe_, storage, shed) && shed.Room > 0, "it is a store building with room");
		Check(v.Tribe_.Economy().Stockpiles.empty(), "and no stockpile of its own (its things are the clan's store)");
		const int woodBefore = v.Tribe_.Store().Count("wood"), stoneBefore = v.Tribe_.Store().Count("stone");
		size_t seen = 0;
		int arrivals = 0, farArrivals = 0;
		bool within = true;
		for (int i = 0; i < static_cast<int>(400.0f / dt); ++i)
		{
			v.Step();
			within = within && v.LedgerWithinStore();
			// every "leaves N in the store at <storage>" is said where the member stands: at the storage, not at the fire
			const auto& log = v.Tribe_.Log();
			for (; seen < log.size(); ++seen)
			{
				const std::string& l = log[seen];
				if (l.find("in the store at " + storage) == std::string::npos || l.find(" leaves ") == std::string::npos) continue;
				for (size_t k = 0; k < v.Tribe_.Members().size(); ++k)
					if (l.find(" " + v.Tribe_.Members()[k].Name + " (") != std::string::npos)
					{
						++arrivals;
						if (glm::distance(v.bodies[k]->Position(), shed.Pos) > shed.Reach) ++farArrivals;
					}
			}
		}
		Check(arrivals > 0, "members carry things to the storage and leave them there");
		Check(farArrivals == 0, "each of them at the storage (within its reach), not by the fire");
		Check(v.Logged("picks up") && v.Logged("from the pile by the fire"), "the pile by the fire is carried to the storage");
		Check(v.Tribe_.Stores().Held(storage, "stone") > 0, "the storage holds the clan's stone");
		Check(within, "the buildings never hold more than the store, item by item");
		Check(v.Tribe_.Store().Count("wood") >= woodBefore - 2 && v.Tribe_.Store().Count("stone") >= stoneBefore - 2, "the store's totals count what lies in the storage");
		Check(StoreLayout::Of(v.Tribe_).InBuildings() == v.Tribe_.Stores().InBuildings(), "the layout the panel draws is the ledger");

		// a site's materials come out of the storage: the member fetches them there on its way
		const int stoneIn = v.Tribe_.Stores().Held(storage, "stone"), woodIn = v.Tribe_.Stores().Held(storage, "wood");
		Check(v.Tribe_.StartSite("workbench", v.Camp + glm::vec2(-10.0f, 4.0f)).empty(), "a workbench site is started");
		bool fetched = false;
		for (int i = 0; i < static_cast<int>(200.0f / dt) && !fetched; ++i)
		{
			v.Step();
			fetched = v.Logged("from the store at " + storage);
			within = within && v.LedgerWithinStore();
		}
		Check(fetched, "a member carries the site's materials from the storage");
		Check(v.Tribe_.Stores().Held(storage, "stone") < stoneIn || v.Tribe_.Stores().Held(storage, "wood") < woodIn, "and they leave the storage's share");
		Check(within, "the ledger still within the store");

		// ---- save and load: the ledger round-trips; a save from before loads with everything by the fire
		const std::string json = v.Tribe_.ToJson();
		const ClanStores::Items held = v.Tribe_.Stores().Ledger().count(storage) ? v.Tribe_.Stores().Ledger().at(storage) : ClanStores::Items();
		Check(json.find("\"stores\"") != std::string::npos, "the ledger is saved");
		Check(v.Tribe_.FromJson(json, false) && v.Tribe_.Stores().Ledger().count(storage) && v.Tribe_.Stores().Ledger().at(storage) == held, "and loads as it was");
		Check(v.Tribe_.ToJson() == json, "and saves again the same text");
		const int total = v.Tribe_.Store().Count("stone");
		Check(v.Tribe_.FromJson(DropKey(json, "stores"), false) && v.Tribe_.Stores().Ledger().empty() && v.Tribe_.Store().Count("stone") == total,
			"a save from before loads: the same store, all of it by the fire");

		// ---- an older save kept a store building's things in a stockpile of its name: they move into the store, in that building
		Stockpile* old = v.Tribe_.AddStockpile(storage, shed.Door, "stockpile");
		Check(old != nullptr, "an older save's stockpile of the storage");
		if (old) old->Store.Add(v.Items, "reed", 5);
		const int reed = v.Tribe_.Store().Count("reed");
		v.Step();
		Check(v.Tribe_.Store().Count("reed") >= reed + 5 - 1 && v.Tribe_.Stores().Held(storage, "reed") >= 4 && !v.Tribe_.FindStockpile(storage), "moves into the store and the storage's ledger");
	}

	std::printf("clan_stores: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
