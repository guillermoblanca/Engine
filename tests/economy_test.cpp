// The economy of the clan (H18: production chains, E6 spoilage and preservation, E7 agriculture, E11 logistics). Checked: the "economy" section of
// assets/data/era1.json says what the code says, round-trips and is refused whole on a mistake, and era1_ampliado adds its crop; the strict check of the
// packs still passes; the planner makes intermediates for what is wanted (and for what is only to be tried); stacks age, go off as a whole, take the mean
// age when they mix and save their age; in play a clan makes cordage by itself so that flint knapping can be worked out and the flint axe made; food
// goes off in the store at its pace and a surplus is dried at a drying rack (a station a building gives); a field is marked out, tilled, sown, watered,
// ripens and is harvested over a season (and the frost ruins what winter finds); what is left at a stockpile is hauled to the main store; the economy
// saves and loads (and a save from before loads). CPU only, stand-in bodies as in tribe_test and work_test.
#include "GB/Game/GameContent.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/Spoilage.h"
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

	// The text without one key of the top object (and its value), for a "save from before" (no JSON library in the tests).
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

// A clan of `n` on a patch scaled to it, as `tribe build` does (trades from its needs). `extended`: the content of era1_ampliado too (seeds, grain,
// the hoe, dried berries and the grain crop).
struct Clan
{
	explicit Clan(int n, bool extended = false)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		if (extended)
		{
			const std::string data = std::string(GB_ASSETS_DIR) + "/data";
			Check(LoadContentFile(data + "/era1_ampliado.json", Items, Recipes, Defs).empty(), "era1_ampliado.json loads as content");
			Check(LoadEconomyRulesFile(data + "/era1_ampliado.json", Tribe_.Config.Econ).empty(), "and its economy section (the crops)");
			for (const char* far : { "oak_acorns", "granite_rock", "pine_resin", "wild_grain", "salt_flat", "old_bones", "fallen_trunk" }) Defs.erase(far);   // (not placed here)
		}
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
};

int main()
{
	const std::string data = std::string(GB_ASSETS_DIR) + "/data";

	// ---- the data: the "economy" section of era1.json says what the code says, round-trips, and a mistake changes nothing
	{
		EconomyRules file;
		Check(LoadEconomyRulesFile(data + "/era1.json", file).empty(), "the economy section of assets/data/era1.json loads");
		Check(file == DefaultEconomyRules(), "and says what DefaultEconomyRules says");
		EconomyRules back;
		Check(LoadEconomyRules("{ \"economy\": " + EconomyRulesToJson(DefaultEconomyRules()) + " }", back).empty() && back == DefaultEconomyRules(), "the economy section round-trips");
		EconomyRules keep = DefaultEconomyRules();
		Check(!LoadEconomyRules(R"({ "economy": { "production": { "depth": 2 }, "spoilage": { "step": -1 } } })", keep).empty() && keep == DefaultEconomyRules(), "a negative step is refused and nothing changes (not even the part before it)");
		Check(!LoadEconomyRules(R"({ "economy": { "barter": {} } })", keep).empty() && !LoadEconomyRules(R"({ "economy": { "farming": { "plows": 2 } } })", keep).empty(), "an unknown part or key is refused");
		Check(!LoadEconomyRules(R"({ "economy": { "crops": [ { "id": "rye", "seed": "rye_seed", "yield": { "rye": 2 }, "sow": [ "monsoon" ] } ] } })", keep).empty(), "an unknown season is refused");
		Check(!LoadEconomyRules(R"({ "economy": { "logistics": { "fieldStore": "barn" } } })", keep).empty(), "a field store of a kind that does not exist is refused");
		Check(LoadEconomyRules(R"({ "items": [] })", keep).empty() && keep == DefaultEconomyRules(), "a file without the section changes nothing");
		EconomyRules ext = file;
		Check(LoadEconomyRulesFile(data + "/era1_ampliado.json", ext).empty() && ext.Crops.count("grain") == 1 && ext.Production == file.Production && ext.Logistics == file.Logistics,
			"era1_ampliado adds the grain crop and leaves the rest");
		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes;
		RegisterEraOneItems(items, recipes, nodes);
		Check(!CheckEconomy(ext, items).empty(), "the grain crop names items era 1 does not have");
		Check(LoadContentFile(data + "/era1_ampliado.json", items, recipes, nodes).empty() && CheckEconomy(ext, items).empty(), "and with era1_ampliado's items everything it names exists");
		Check(items.Find("berry")->ShelfLife == 1800.0f && items.Find("dried_berry")->ShelfLife > items.Find("berry")->ShelfLife && items.Find("wood")->ShelfLife == 0.0f,
			"berries spoil, dried berries keep longer, wood keeps");
		ItemLibrary bad; RecipeBook br; std::map<std::string, ResourceNodeDef> bn;
		Check(!LoadContent(R"({ "items": [ { "id": "meat", "shelfLife": 600, "spoilsTo": "carrion" } ] })", bad, br, bn).empty() && !bad.Find("meat"), "an item that spoils into an unknown item is refused");
		Check(!LoadContent(R"({ "items": [ { "id": "meat", "shelfLife": -1 } ] })", bad, br, bn).empty(), "a negative shelf life is refused");
	}

	// ---- the strict check of the real packs still passes with the economy section registered
	{
		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes;
		std::map<std::string, TribeRole> roles = DefaultRoles();
		TribeAgenda agenda; TribeEconomy economy; WorkRules work; TechTree techs; EconomyRules econ;
		RegisterEraOneItems(items, recipes, nodes);
		ContentPacks packs;
		AddGameContentSections(packs, items, recipes, nodes, roles, &agenda, &economy, &work, &techs);
		AddEconomyContentSection(packs, econ);
		ContentPacks::Options strict;
		strict.Strict = true;
		ContentReport report;
		const std::string error = packs.Load("era1_ampliado", data, &report, strict);
		Check(error.empty() && report.Clean(), "era1 and era1_ampliado load strict, with the technologies and the economy section");
		if (!error.empty() || !report.Clean()) std::fprintf(stderr, "  %s\n%s", error.c_str(), report.Text().c_str());
		Check(econ.Crops.count("grain") == 1 && econ.Production.Stock.at("cordage") == 2, "the economy section comes in with the packs");
		Check(CheckEconomy(econ, items).empty(), "and names only items that are loaded");
		EconomyRules kept = econ;
		Check(!packs.LoadText(R"({ "economy": { "farming": { "plots": 0 } } })", nullptr).empty() && econ == kept, "a broken economy section refuses the load and changes nothing");
	}

	// ---- the planner (pure): what is wanted and the intermediates under it
	{
		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes;
		RegisterEraOneItems(items, recipes, nodes);
		std::map<std::string, int> stock{ { "branch", 4 }, { "flint", 4 }, { "fiber", 6 } };
		ProductionQuery all;
		ProductionPlan p = PlanProduction({ { "flint_axe", 1, "test" } }, stock, recipes, all);
		Check(p.Steps.size() == 2 && p.Steps[0].Recipe == "flint_axe" && !p.Steps[0].Ready && p.Steps[1].Recipe == "cordage" && p.Steps[1].Ready && p.Steps[1].Depth == 1,
			"a flint axe asks for cordage, which can be made now (the axe waits for it)");
		ProductionQuery noKnapping;
		noKnapping.Known = [](const Recipe& r) { return r.Id != "flint_axe" && r.Id != "flint_pickaxe"; };
		p = PlanProduction({ { "flint_axe", 1, "test" } }, stock, recipes, noKnapping);
		Check(p.Steps.size() == 1 && p.Steps[0].Recipe == "cordage" && p.Steps[0].Ready && !p.Blocked.empty(), "a clan that cannot make the axe yet still makes the cordage to try it");
		noKnapping.Experiment = false;
		Check(PlanProduction({ { "flint_axe", 1, "test" } }, stock, recipes, noKnapping).Steps.empty(), "and without experimenting it makes nothing for it");
		ProductionQuery shallow;
		shallow.Depth = 0;
		p = PlanProduction({ { "flint_axe", 1, "test" } }, stock, recipes, shallow);
		Check(p.Steps.size() == 1 && p.Steps[0].Recipe == "flint_axe", "depth 0 plans only the want itself");
		p = PlanProduction({ { "flint_axe", 2, "test" } }, { { "fiber", 3 } }, recipes, all);
		Check(p.Raw["branch"] == 4 && p.Raw["flint"] == 4 && p.Raw["fiber"] == 3, "what nothing makes is a raw shortage (to gather)");
		Check(PlanProduction({ { "cordage", 2, "keep" } }, { { "cordage", 2 } }, recipes, all).Steps.empty(), "what the stock already holds is not made");
		ProductionQuery noRack;
		noRack.Station = [](const std::string& s) { return s != "forge"; };
		p = PlanProduction({ { "copper_ingot", 1, "test" } }, { { "copper_ore", 4 } }, recipes, noRack);
		Check(p.Steps.empty() && p.Blocked.size() == 1 && p.Blocked[0].find("forge") != std::string::npos, "a recipe whose station the clan lacks is blocked and says why");
		Check(PlanReport(PlanProduction({ { "flint_axe", 1, "test" } }, stock, recipes, all)) == PlanReport(PlanProduction({ { "flint_axe", 1, "test" } }, stock, recipes, all)), "deterministic");
	}

	// ---- spoilage (pure): a stack ages, goes off as a whole at its shelf life, mixes by the mean age and saves its age
	{
		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes;
		RegisterEraOneItems(items, recipes, nodes);
		LoadContentFile(data + "/era1_ampliado.json", items, recipes, nodes);
		Inventory store(40, 0.0f);
		store.Add(items, "berry", 20);
		store.Add(items, "dried_berry", 10);
		store.Add(items, "wood", 5);
		std::vector<SpoilEvent> gone;
		for (int i = 0; i < 179; ++i) { auto e = AgeInventory(store, items, 10.0f, 1.0f); gone.insert(gone.end(), e.begin(), e.end()); }
		Check(gone.empty() && store.Count("berry") == 20, "berries keep until their shelf life");
		gone = AgeInventory(store, items, 10.0f, 1.0f);
		Check(gone.size() == 1 && gone[0].Item == "berry" && gone[0].Count == 20 && store.Count("berry") == 0, "and then the whole stack goes off");
		Check(store.Count("dried_berry") == 10 && store.Count("wood") == 5 && store.Stacks().back().Age == 0.0f, "dried berries are still good and wood never ages");
		Inventory cool(40, 0.0f);
		cool.Add(items, "berry", 5);
		for (int i = 0; i < 180; ++i) AgeInventory(cool, items, 10.0f, 0.5f);
		Check(cool.Count("berry") == 5, "a store that ages at half the rate keeps them twice as long");
		Inventory mix(40, 0.0f);
		mix.Add(items, "berry", 10, 1000.0f);
		mix.Add(items, "berry", 10);
		Check(mix.Stacks().size() == 1 && std::fabs(mix.Stacks()[0].Age - 500.0f) < 0.01f, "fresh berries on an old stack take the mean age");
		mix.Add(items, "berry", 15, 300.0f);
		Check(mix.Stacks().size() == 2 && mix.Stacks()[1].Age == 300.0f && mix.OldestAge("berry") > 450.0f, "a new stack keeps its own age");
		Check(mix.RemoveOldest("berry", 3) == 3 && mix.Stacks()[0].Count == 17, "the oldest are eaten first");
		Inventory other(40, 0.0f);
		mix.Transfer(items, other, "berry", 35);
		float units = 0.0f;
		for (const ItemSlot& s : other.Stacks()) units += s.Age * static_cast<float>(s.Count);
		Check(other.Count("berry") == 32 && std::fabs(units - (15 * 300.0f + 17 * 500.0f)) < 0.5f && other.Stacks()[0].Age > 300.0f, "a transfer carries the age along (the stacks it joins take the mean)");
		Inventory loaded;
		Check(loaded.FromJson(other.ToJson()) && loaded.Stacks()[0].Age == static_cast<float>(std::round(other.Stacks()[0].Age * 100.0) / 100.0), "the age is saved with the stack");
		Inventory old;
		Check(old.FromJson(R"({"slots":8,"maxWeight":0,"items":[["berry",3]]})") && old.Stacks()[0].Age == 0.0f, "a stack saved before E6 loads fresh");
		Check(!old.FromJson(R"({"slots":8,"items":[["berry",3,-5]]})"), "a negative age is refused");
		Inventory rot(40, 0.0f);
		ItemLibrary meat = items;
		ItemDef m; m.Id = "meat"; m.MaxStack = 10; m.ShelfLife = 60.0f; m.SpoilsTo = "carrion"; meat.Add(m);
		ItemDef c; c.Id = "carrion"; c.MaxStack = 10; meat.Add(c);
		rot.Add(meat, "meat", 4);
		gone = AgeInventory(rot, meat, 61.0f, 1.0f);
		Check(gone.size() == 1 && gone[0].Into == "carrion" && rot.Count("carrion") == 4 && rot.Count("meat") == 0, "what spoils into something turns into it");
	}

	// ---- in play: the clan makes cordage by itself, so flint knapping can be worked out and the flint axe made (the gap of H11)
	{
		TechTree techs = DefaultTechs();
		techs.Techs["flint_knapping"].Discovery.Experiment = 0.3f;   // quicker than the game's, to keep the test short
		techs.Techs["flint_knapping"].Discovery.Seconds = 5.0f;
		auto run = [&](std::vector<std::string>& log, int& axes, int& cordage)
		{
			Clan k(4);
			k.Tribe_.SetTechs(&techs);
			k.Tribe_.Config.Roles["woodcutter"].Tool = "flint_axe";   // the woodcutter's trade asks for a flint axe: nobody knows how to make one
			k.Tribe_.Config.Roles["woodcutter"].ToolTag = "Item.Tool.Axe";
			k.Tribe_.Store().Add(k.Items, "branch", 6);
			k.Tribe_.Store().Add(k.Items, "flint", 6);
			k.Tribe_.Store().Add(k.Items, "fiber", 9);
			Check(k.Tribe_.Store().Count("cordage") == 0, "no cordage at the start");
			k.Tribe_.PostJobs();
			Check(k.Tribe_.Board().Find("make cordage") != nullptr, "the board asks for cordage: the flint axe needs it to be tried, and the store keeps some");
			k.Run(400.0);
			log = k.Tribe_.Log();
			const size_t wc = k.Index("woodcutter");
			axes = k.bodies[wc]->Bag().Count("flint_axe");
			cordage = k.Tribe_.Economy().Made.count("cordage") ? k.Tribe_.Economy().Made.at("cordage") : 0;
			Check(!k.bad, "nothing breaks");
			Check(cordage > 0, "the clan makes cordage");
			Check(k.Tribe_.Knowledge().Knows(k.Names[wc], "flint_knapping"), "the woodcutter works out flint knapping");
			if (axes == 0) k.Dump(k.Names[wc]);
		};
		std::vector<std::string> log1, log2;
		int axes1 = 0, axes2 = 0, cord1 = 0, cord2 = 0;
		run(log1, axes1, cord1);
		run(log2, axes2, cord2);
		Check(log1 == log2 && axes1 == axes2 && cord1 == cord2, "two runs tell the same story");
		const auto at = [&](const std::string& part) { const auto it = std::find_if(log1.begin(), log1.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; }); return it == log1.end() ? -1L : static_cast<long>(it - log1.begin()); };
		const long made = at("cordage for the store"), found = at("discovers Flint knapping"), axe = at("makes a flint_axe");
		Check(made >= 0 && found > made, "the cordage is made before the discovery");
		Check(axe > found && axes1 == 1, "and the flint axe after it, by the woodcutter");
	}

	// ---- in play: a clan that knows flint knapping makes a better axe for its woodcutter and hands it over (an upgrade, from data)
	{
		Clan k(4);
		k.Tribe_.Store().Add(k.Items, "branch", 6);
		k.Tribe_.Store().Add(k.Items, "flint", 6);
		k.Tribe_.Store().Add(k.Items, "cordage", 2);
		k.Run(240.0);
		const size_t wc = k.Index("woodcutter");
		Check(k.Posted["make flint_axe"], "a stone axe is not the best the clan can make: it posts a flint axe");
		Check(k.bodies[wc]->Bag().Count("flint_axe") == 1 && k.bodies[wc]->Bag().Count("stone_axe") == 0, "the woodcutter takes it and leaves its stone axe at the store");
		if (k.bodies[wc]->Bag().Count("flint_axe") != 1) k.Dump("flint");
	}

	// ---- in play: food goes off in the store at its pace; a surplus is dried at a drying rack, which a building gives (H8 interface)
	{
		Clan k(2);
		k.Tribe_.Store().Add(k.Items, "berry", 20, 1790.0f);   // nearly gone
		k.Run(30.0);
		Check(k.Tribe_.Economy().Spoiled.count("berry") && k.Tribe_.Economy().Spoiled.at("berry") > 0 && k.Logged("berry in the store: they went off"), "berries left too long go off, and the log says so");

		Clan d(4, true);
		d.Tribe_.Store().Add(d.Items, "berry", 40);
		d.Tribe_.PostJobs();
		Check(!d.Posted["make dried_berry"] && d.Tribe_.Board().Find("make dried_berry") == nullptr, "without a drying rack the clan cannot dry its surplus");
		d.Tribe_.SetStations([&](const std::string& station, glm::vec2& where) { if (station != "drying_rack") return false; where = d.Tribe_.Config.Camp + glm::vec2(3.0f, 3.0f); return true; });
		d.Run(120.0);
		Check(d.Posted["make dried_berry"], "with one (the buildings' station) the surplus of berries is posted to be dried");
		Check(d.Tribe_.Store().Count("dried_berry") > 0 || (d.Tribe_.Economy().Made.count("dried_berry") && d.Tribe_.Economy().Made.at("dried_berry") > 0), "and dried berries are made");
	}

	// ---- in play: a field over a season: marked out, tilled with the hoe, sown with seeds, watered, ripe and harvested into its store, then hauled
	{
		Clan k(4, true);   // (no technologies: everyone may farm)
		k.Tribe_.SetWorldClock(0.0);   // the first day of spring
		k.Tribe_.Store().Add(k.Items, "grain_seed", 6);
		k.Tribe_.Store().Add(k.Items, "stone_hoe", 1);
		double clock = 0.0;
		const int frames = static_cast<int>(400.0 / dt);
		bool sown = false, sprout = false;
		for (int i = 0; i < frames; ++i)
		{
			clock += dt;
			k.Tribe_.SetWorldClock(clock);
			k.Run(dt);
			for (const Field& f : k.Tribe_.Economy().Fields) { sown = sown || f.Count("sown") > 0; sprout = sprout || f.Count("sprout") > 0; }
		}
		const EconomyState& e = k.Tribe_.Economy();
		Check(e.Fields.size() == 1 && e.Stockpiles.size() == 1, "the clan marks out a field (with a store beside it)");
		Check(k.Posted["farm till"] && k.Posted["farm sow"] && k.Posted["farm harvest"], "it posts tilling, sowing and harvesting");
		Check(sown && sprout, "the seeds are sown and sprout");
		Check(e.Harvested.count("grain") && e.Harvested.at("grain") >= 3, "the ripe plots are harvested before winter");
		Check(e.Hauled > 0 && k.Tribe_.Store().Count("grain") + e.Stockpiles[0].Store.Count("grain") > 0, "the harvest is left at the field's store and hauled to the camp");
		if (!e.Harvested.count("grain")) { std::fprintf(stderr, "%s\n", k.Tribe_.FieldReport().c_str()); k.Dump("field"); }
		std::fprintf(stderr, "  field: %s\n", k.Tribe_.FieldReport().c_str());

		// the frost: winter ruins what is in the ground
		std::map<std::string, CropDef> crops = k.Tribe_.Config.Econ.Crops;
		Field f("test", glm::vec2(0.0f), 3, 3, 2.0f);
		f.Plots[0] = { "sown", "grain", 0.5f, 100.0f };
		f.Plots[1] = { "grown", "grain", 0.0f, 0.0f };
		const FieldEvents ev = f.Update(10.0f, Season::Winter, false, 120.0f, crops);
		Check(ev.Ruined == 2 && f.Count("dug") == 2 && f.Count("grass") == 1, "winter's frost ruins what is sown or ripe (the soil stays tilled)");
		Field g("test", glm::vec2(0.0f), 1, 1, 2.0f);
		g.Plots[0] = { "sown", "grain", 0.0f, 0.0f };
		g.Update(60.0f, Season::Spring, false, 120.0f, crops);
		Check(g.Plots[0].State == "sown" && g.Plots[0].Growth == 0.0f, "a dry plot does not grow");
		g.Update(60.0f, Season::Spring, true, 120.0f, crops);
		Check(g.Plots[0].Growth > 0.0f, "the rain waters it");
		Check(crops.at("grain").SowsIn(Season::Spring) && !crops.at("grain").SowsIn(Season::Autumn), "grain is sown in spring and summer, not in autumn");
	}

	// ---- logistics: what is left at a stockpile is hauled to the main store; a basket lets a member carry more
	{
		Clan k(3);
		Stockpile* pile = k.Tribe_.AddStockpile("north pile", k.Tribe_.Config.Camp + glm::vec2(12.0f, 0.0f));
		Check(pile && !k.Tribe_.AddStockpile("north pile", glm::vec2(0.0f)) && !k.Tribe_.AddStockpile("x", glm::vec2(0.0f), "barn"), "a stockpile is put once, of a known kind");
		pile->Store.Add(k.Items, "flint", 9);
		const int before = k.Tribe_.Store().Count("flint");
		k.Run(120.0);
		const Stockpile* p = k.Tribe_.FindStockpile("north pile");
		Check(k.Posted["haul north pile"], "the board asks to haul it");
		Check(p->Store.Count("flint") == 0 && k.Tribe_.Store().Count("flint") >= before + 9 - 2 && k.Tribe_.Economy().Hauled >= 9, "and it is carried to the main store (two trips or more)");
		Check(k.Tribe_.Stored("flint") == k.Tribe_.Store().Count("flint") + p->Store.Count("flint"), "the clan counts both stores");
		const TribeMember& m = k.Tribe_.Members()[0];
		const int plain = k.Tribe_.CarryLimit(m);
		k.bodies[0]->Bag().Add(k.Items, "basket_wicker", 1);
		Check(k.Tribe_.CarryLimit(m) == plain + 6, "a basket (+6 slots) lets it carry six more before it goes to the store");
	}

	// ---- saving: the economy state goes with the tribe, ages included; a save from before H18 loads with none
	{
		Clan k(3, true);
		k.Tribe_.Store().Add(k.Items, "berry", 5, 321.5f);
		k.Tribe_.MarkField(glm::vec2(10.0f, 0.0f));
		k.Tribe_.Economy().Fields[0].Plots[2] = { "sprout", "grain", 0.25f, 40.0f };
		k.Tribe_.FindStockpile("field 1 store")->Store.Add(k.Items, "grain", 4, 12.0f);
		k.Tribe_.Economy().Spoiled["berry"] = 7;
		const std::string saved = k.Tribe_.ToJson();
		Clan l(3, true);
		Check(l.Tribe_.FromJson(saved, false), "the tribe loads");
		Check(l.Tribe_.Economy().Fields == k.Tribe_.Economy().Fields && l.Tribe_.Economy().Spoiled == k.Tribe_.Economy().Spoiled, "with its fields and counters");
		Check(l.Tribe_.Economy().Stockpiles.size() == 1 && l.Tribe_.Economy().Stockpiles[0].Store.Stacks()[0].Age == 12.0f, "and its stockpile, ages included");
		Check(std::fabs(l.Tribe_.Store().OldestAge("berry") - 321.5f) < 0.01f, "the main store keeps the age of its berries");
		Check(l.Tribe_.ToJson() == saved, "and saves the same again");
		const std::string old = DropKey(saved, "economy");
		Clan o(3, true);
		Check(old.find("\"economy\"") == std::string::npos && o.Tribe_.FromJson(old, false) && o.Tribe_.Economy().Fields.empty() && o.Tribe_.Economy().Stockpiles.empty(),
			"a save from before H18 loads with no fields or stockpiles");
		std::string broken = saved;
		const std::string plots = "\"plots\":[[\"grass\"";
		const size_t plot = broken.find(plots);
		Check(plot != std::string::npos, "(the saved field has its plots)");
		if (plot != std::string::npos) broken.replace(plot, plots.size(), "\"plots\":[[\"flooded\"");
		Check(!o.Tribe_.FromJson(broken, false), "a damaged economy refuses the load");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("economy: ok");
	return 0;
}
