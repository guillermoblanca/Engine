// Exploration, expeditions and migration (H12). Checked, on the CPU and on worlds the generator makes without GL:
//  * the "exploration" section of assets/data/era1.json says what the code says, round-trips, and a mistake changes nothing (also inside a pack);
//  * the regions cover the map exactly once, are connected, named once, deterministic for a seed and different for another, and count every wild node;
//  * a clan that lacks something nobody knows where to find sends an expedition that comes back having found it, and then gathers it;
//  * a clan whose home has been stripped bare moves its camp (the campfire goes too) and a clan with a good home does not;
//  * a save in the middle of an expedition loads with it, and the loaded clan brings it home; a save from before H12 loads with none.
// Stand-in bodies as in work_test (they walk in straight lines at 3 m/s).
#include "GB/Game/GameContent.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/World/Regions.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <nlohmann/json.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
		else std::printf("ok: %s\n", what);
	}
	constexpr float dt = 1.0f / 20.0f;
}

using namespace GB;

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
				const glm::vec4 p = id == NullEntity ? glm::vec4(m_Pos.x, 0.0f, m_Pos.y, 1.0f) : m_Entities.WorldMatrix(id)[3];
				s.Seconds = glm::distance(m_Pos, glm::vec2(p.x, p.z)) / 3.0f;
				m_Target = { p.x, p.z };
				s.Walking = true;
			}
		}
		if (s.Seconds > 0.0f)
		{
			const float total = s.Seconds;
			s.Seconds -= step;
			if (s.Kind != "wait" && total > 0.0f) m_Pos += (m_Target - m_Pos) * std::min(1.0f, step / total);   // it walks there (others see it on the way)
			if (s.Seconds > 0.0f) return;
			if (s.Kind != "wait") m_Pos = m_Target;
		}
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

// A generated world (CPU only), its wild nodes and its regions.
struct Land
{
	Land(WorldType type, uint32_t seed)
	{
		WorldDesc desc;
		desc.Type = type;
		desc.Seed = seed;
		World = WorldGenerator::GenerateData(desc);
		ItemLibrary items;
		RecipeBook recipes;
		RegisterEraOneItems(items, recipes, Defs);
		Spots = PlanWorldResources(ResourceGroundOf(World), Defs, seed, 1.0f);
		Regions = RegionMap::Build(World, DefaultExplorationRules().Regions, RegionThingsOf(Spots));
	}
	float Height(float x, float z) const { return World.TerrainRef ? World.TerrainRef->GetHeight(x, z) : 0.0f; }
	GeneratedWorld World;
	std::map<std::string, ResourceNodeDef> Defs;
	std::vector<ResourceSpot> Spots;
	RegionMap Regions;
};

// A clan of `n` on a land, camped at `camp`, with the wild nodes `keep` lets stand and a well by the camp.
struct Clan
{
	Clan(const Land& land, glm::vec2 camp, int n, const ExplorationRules& rules, const std::function<bool(const ResourceSpot&)>& keep = nullptr)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		std::vector<ResourceSpot> spots;
		for (const ResourceSpot& s : land.Spots) if (!keep || keep(s)) spots.push_back(s);
		BuildWorldResources(Entities, spots, Defs);
		const glm::vec2 well = camp + glm::vec2(4.0f, -6.0f);
		BuildInteractive(Entities, DefaultInteractives().at("well"), "Well", NullEntity, { well.x, land.Height(well.x, well.y), well.y });
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		const std::vector<std::string> roles = Tribe_.PlanRoles(n);
		for (int i = 0; i < n; ++i)
		{
			const std::string name = MemberName(Tribe_.Config.Work, static_cast<size_t>(i));
			Entity& e = *Entities.Find(Entities.Create(name));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "Stoke", "DrawWater" };
			Names.push_back(name);
		}
		Gameplay.Load(Entities);
		for (int i = 0; i < n; ++i)
		{
			bodies.push_back(std::make_unique<FakeBody>(Names[static_cast<size_t>(i)], Gameplay, Entities, camp + glm::vec2(static_cast<float>(i % 5) * 0.7f, static_cast<float>(i / 5) * 0.7f)));
			bodies.back()->m_Items = &Items;
		}
		auto give = [this](const std::string& who, const std::string& item, int k)
		{
			for (size_t i = 0; i < Names.size(); ++i) if (Names[i] == who) return bodies[i]->Bag().Add(Items, item, k);
			return 0;
		};
		Resources.Bind(Gameplay, Defs, [give](const std::string& who, const std::string& item, int k) { give(who, item, k); });
		Fires.Items = &Items;
		Fires.Bind(Gameplay, [this](const std::string& who, const std::string& item, int k)
		{
			for (size_t i = 0; i < Names.size(); ++i) if (Names[i] == who) return bodies[i]->Bag().Remove(item, k);
			return 0;
		}, give);
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		Tribe_.Config.Economy.Camp = camp;
		Tribe_.Config.Economy.CampY = land.Height(camp.x, camp.y);
		Tribe_.Config.Explore = rules;
		Tribe_.SetRegions(&land.Regions, [&land](float x, float z) { return land.Height(x, z); });
		Tribe_.Store().Add(Items, "wood", 2 + 2 * n);
		Tribe_.Store().Add(Items, "stone", 3 * n);
		Tribe_.Store().Add(Items, "berry", 5 * n);
		for (int i = 0; i < n; ++i) Tribe_.AddMember(Names[static_cast<size_t>(i)], roles[static_cast<size_t>(i)], bodies[static_cast<size_t>(i)].get());
	}
	// Runs until `stop` says so (checked every simulated second) or for `seconds`; true when it stopped.
	bool Run(double seconds, const std::function<bool()>& stop = nullptr)
	{
		const int frames = static_cast<int>(seconds / static_cast<double>(dt));
		for (int i = 0; i < frames; ++i)
		{
			Gameplay.Update(dt);
			Resources.Update(Gameplay, dt);
			for (auto& b : bodies) b->Update(dt);
			Resources.SyncVisuals(Gameplay, Entities);
			Fires.Update(dt, &Entities);
			Tribe_.Update(dt);
			if (stop && i % 20 == 0 && stop()) return true;
		}
		return false;
	}
	bool Knows(const std::string& item) const
	{
		for (const auto& [name, e] : Tribe_.ClanMemory().All())
			if (const auto d = Defs.find(e.Kind); d != Defs.end() && d->second.Item == item) return true;
		return false;
	}
	int Holds(const std::string& item)
	{
		int n = Tribe_.Store().Count(item);
		for (auto& b : bodies) n += b->Bag().Count(item);
		return n;
	}
	int Alive() const { int n = 0; for (const TribeMember& m : Tribe_.Members()) n += m.Dead ? 0 : 1; return n; }
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
};

// Every tile of the map in exactly one region, every region connected and named once.
void CheckCut(const RegionMap& map, const char* what)
{
	char text[160];
	const int tiles = map.TilesX() * map.TilesZ();
	int sum = 0, cells = 0;
	bool connected = true, named = true, anchored = true;
	std::set<std::string> names;
	for (const RegionSummary& g : map.Regions())
	{
		sum += g.Tiles;
		cells += map.CellsIn(g.Id);
		named = named && !g.Name.empty() && names.insert(g.Name).second;
		const std::vector<int> mine = map.TilesOf(g.Id);
		std::set<int> seen{ mine.front() };
		std::vector<int> stack{ mine.front() };
		while (!stack.empty())
		{
			const int t = stack.back();
			stack.pop_back();
			const int x = t % map.TilesX(), z = t / map.TilesX();
			const int nx[4] = { x - 1, x + 1, x, x }, nz[4] = { z, z, z - 1, z + 1 };
			for (int k = 0; k < 4; ++k)
			{
				if (nx[k] < 0 || nz[k] < 0 || nx[k] >= map.TilesX() || nz[k] >= map.TilesZ()) continue;
				const int n = nz[k] * map.TilesX() + nx[k];
				if (map.TileRegion(n) == g.Id && seen.insert(n).second) stack.push_back(n);
			}
		}
		connected = connected && seen.size() == mine.size() && static_cast<int>(mine.size()) == g.Tiles;
		anchored = anchored && map.RegionAt(g.Anchor) == g.Id;
	}
	bool covered = true;
	for (int t = 0; t < tiles; ++t) covered = covered && map.TileRegion(t) >= 0 && map.TileRegion(t) < static_cast<int>(map.Regions().size());
	std::snprintf(text, sizeof text, "%s: %zu regions cover the %d tiles once (and the %d scout cells)", what, map.Regions().size(), tiles, map.CellCount());
	Check(covered && sum == tiles && cells == map.CellCount(), text);
	std::snprintf(text, sizeof text, "%s: every region is in one piece, has its own name and its anchor in it", what);
	Check(connected && named && anchored, text);
}

int main()
{
	using Clock = std::chrono::steady_clock;
	const auto t0 = Clock::now();
	const ExplorationRules defaults = DefaultExplorationRules();

	// ---- the data
	{
		ExplorationRules file;
		Check(LoadExplorationRulesFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", file).empty() && file == defaults, "the exploration section of era1.json says what DefaultExplorationRules says");
		ExplorationRules back;
		Check(LoadExplorationRules("{ \"exploration\": " + ExplorationRulesToJson(defaults) + " }", back).empty() && back == defaults, "the exploration section round-trips");
		ExplorationRules keep = defaults;
		Check(!LoadExplorationRules(R"({ "exploration": { "migration": { "after": 10 }, "expeditions": { "minParty": 0 } } })", keep).empty() && keep == defaults,
			"a party of 0 is refused and nothing changes (not even the migration before it)");
		Check(!LoadExplorationRules(R"({ "exploration": { "regions": { "danger": { "Jungle": 0.5 } } } })", keep).empty() && keep == defaults, "an unknown biome is refused");
		Check(!LoadExplorationRules(R"({ "exploration": { "expeditions": { "minParty": 4, "maxParty": 3 } } })", keep).empty(), "a party smaller than its least is refused");
		Check(!LoadExplorationRules(R"({ "exploration": { "trips": {} } })", keep).empty() && !LoadExplorationRules(R"({ "exploration": { "migration": { "wings": 1 } } })", keep).empty(),
			"an unknown part or key is refused");
		ExplorationRules some = defaults;
		Check(LoadExplorationRules(R"({ "exploration": { "migration": { "after": 100 } } })", some).empty() && some.Migration.After == 100.0f && some.Migration.Every == MigrationRules{}.Every
			&& some.Regions == defaults.Regions, "a part replaces that part whole and leaves the others");
		Check(LoadExplorationRules(R"({ "items": [] })", some).empty(), "a file without the section changes nothing");

		ItemLibrary items; RecipeBook recipes; std::map<std::string, ResourceNodeDef> nodes; std::map<std::string, TribeRole> roles = DefaultRoles();
		ExplorationRules live;
		live.Expeditions.MinParty = 1;
		RegisterEraOneItems(items, recipes, nodes);
		ContentPacks packs;
		AddGameContentSections(packs, items, recipes, nodes, roles);
		AddExplorationContentSection(packs, live);
		Check(packs.Load("era1", std::string(GB_ASSETS_DIR) + "/data").empty() && live == defaults, "the era1 pack brings the exploration rules through their section");
		Check(!packs.LoadText(R"({ "exploration": { "migration": { "after": 5 } }, "roles": { "bad": { "tool": "x" } } })").empty() && live == defaults,
			"and a bad role in the same document leaves them as they were");
	}

	// ---- regions on a ground made by hand: a forest on the west, a meadow on the east, a lake in the south-east corner
	{
		BiomeMap maps;
		maps.N = 121;
		maps.Cell = 1.0f;
		maps.Min = { -60.0f, -60.0f };
		maps.Biomes.resize(121 * 121);
		maps.WaterDistance.resize(121 * 121);
		const glm::vec2 lake(40.0f, 40.0f);
		for (int j = 0; j < 121; ++j)
			for (int i = 0; i < 121; ++i)
			{
				const glm::vec2 p = maps.SamplePosition(i, j);
				const float d = glm::distance(p, lake) - 16.0f;
				const size_t s = static_cast<size_t>(j * 121 + i);
				maps.Biomes[s] = static_cast<uint8_t>(d < 0.0f ? Biome::Water : p.x < 0.0f ? Biome::Forest : Biome::Meadow);
				maps.WaterDistance[s] = std::max(d, 0.0f);
			}
		const std::vector<RegionThing> things = { { "tree", { -30.0f, 0.0f } }, { "tree", { -40.0f, 20.0f } }, { "berry_bush", { 30.0f, -30.0f } } };
		const RegionMap map = RegionMap::Build(maps, defaults.Regions, things);
		CheckCut(map, "a hand-made ground");
		const RegionSummary* forest = map.Find(map.RegionAt({ -30.0f, 0.0f }));
		const RegionSummary* meadow = map.Find(map.RegionAt({ 30.0f, -30.0f }));
		Check(forest && meadow && forest != meadow && forest->Dominant == Biome::Forest && meadow->Dominant == Biome::Meadow, "the forest and the meadow are two regions of their biomes");
		Check(forest && forest->Name.find("Woods") != std::string::npos && forest->Name.find("West") != std::string::npos, "the forest on the west is called West Woods");
		Check(forest && forest->Resources.at("tree") == 2 && meadow && meadow->Resources.at("berry_bush") == 1, "each region counts the wild nodes in it");
		float area = 0.0f;
		for (const RegionSummary& g : map.Regions()) area += g.Area;
		Check(std::fabs(area - 121.0f * 121.0f) < 1.0f, "the areas add up to the map");
		Check(map.WaterDistanceAt(lake) == 0.0f && map.WaterDistanceAt({ -50.0f, -50.0f }) > 50.0f, "the water distance follows the lake");
		Check(map.Fingerprint() == RegionMap::Build(maps, defaults.Regions, things).Fingerprint(), "the same ground cuts the same way");
		Check(map.Find(forest->Name) == forest && map.Find(std::to_string(meadow->Id)) == meadow, "a region is found by its name or its id");
		std::vector<glm::vec2> foxes = { { -30.0f, 5.0f }, { -35.0f, 5.0f } };
		RegionMap danger = map;
		danger.SetPredators(foxes);
		Check(danger.Find(forest->Id)->Predators == 2 && danger.Find(forest->Id)->Danger > forest->Danger, "predators living in a region make it more dangerous");
	}

	// ---- regions of generated worlds
	{
		struct Case { WorldType Type; uint32_t Seed; const char* Name; };
		const Case cases[] = { { WorldType::Temperate, 5, "temperate 5" }, { WorldType::Alpine, 3, "alpine 3" }, { WorldType::Desert, 2, "desert 2" } };
		uint64_t last = 0;
		for (const Case& c : cases)
		{
			const Land land(c.Type, c.Seed);
			const RegionMap& map = land.Regions;
			CheckCut(map, c.Name);
			char text[160];
			std::snprintf(text, sizeof text, "%s: a handful of regions (%zu)", c.Name, map.Regions().size());
			Check(map.Regions().size() >= 4 && map.Regions().size() <= 80, text);
			int nodes = 0, inside = 0;
			bool dry = true;
			for (const RegionSummary& g : map.Regions())
			{
				for (const auto& [kind, n] : g.Resources) nodes += n;
				if (!g.Water) dry = dry && land.World.BiomeAt(g.Anchor.x, g.Anchor.y) != Biome::Water;
			}
			for (const ResourceSpot& s : land.Spots)
				inside += s.Position.x >= map.WorldMin().x && s.Position.z >= map.WorldMin().y && s.Position.x <= map.WorldMax().x && s.Position.z <= map.WorldMax().y ? 1 : 0;
			std::snprintf(text, sizeof text, "%s: the regions count all %d wild nodes", c.Name, inside);
			Check(nodes == inside && inside > 0, text);
			std::snprintf(text, sizeof text, "%s: the anchor of every land region is on dry ground", c.Name);
			Check(dry, text);
			const Land again(c.Type, c.Seed);
			std::snprintf(text, sizeof text, "%s: the same seed gives the same regions", c.Name);
			Check(again.Regions.Fingerprint() == map.Fingerprint(), text);
			Check(map.Fingerprint() != last, "another world gives other regions");
			last = map.Fingerprint();
		}
		std::printf("%s", Land(WorldType::Temperate, 5).Regions.List().c_str());
	}
	std::printf("(regions: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	// ---- scarcity sends an expedition that finds what the clan lacks
	const Land land(WorldType::Temperate, 5);
	const glm::vec2 camp = land.World.Spawn;
	ExplorationRules quick = defaults;
	quick.Expeditions.Scarcity = 90.0f;
	quick.Expeditions.Gather = 60.0f;
	std::string savedMid;
	ExplorationState savedState;
	{
		Clan clan(land, camp, 4, quick);
		bool sent = false;
		clan.Run(3600.0, [&]
		{
			const Expedition& e = clan.Tribe_.Exploration().Current;
			if (e.Stage == "out" && savedMid.empty() && clan.Tribe_.Time() - e.Started > 30.0) { savedMid = clan.Tribe_.ToJson(); savedState = clan.Tribe_.Exploration(); }
			sent = sent || e.Stage == "out";
			for (const ExpeditionRecord& r : clan.Tribe_.Exploration().History) if (r.Found) return true;
			return false;
		});
		std::printf("%s", clan.Tribe_.ExploreReport().c_str());
		const auto& history = clan.Tribe_.Exploration().History;
		const auto found = std::find_if(history.begin(), history.end(), [](const ExpeditionRecord& r) { return r.Found; });
		Check(sent, "a clan that lacks what nobody knows where to find sends a party out");
		Check(found != history.end(), "an expedition comes back having found it");
		if (found != history.end())
		{
			Check(clan.Knows(found->Item), "the clan knows now where it grows");
			bool sized = true, group = false;
			for (const ExpeditionRecord& r : history)
			{
				sized = sized && !r.Party.empty() && static_cast<int>(r.Party.size()) <= quick.Expeditions.MaxParty;
				group = group || r.Party.size() >= 2;
			}
			Check(sized && group, "the parties were of one to three (one only when nobody else can gather the item), and some of two or more");
			const std::string item = found->Item;
			const int before = clan.Holds(item);
			bool taken = false;
			clan.Run(1800.0, [&]
			{
				if (const Job* j = clan.Tribe_.Board().Find("gather " + item); j && !j->Claims.empty()) taken = true;
				return clan.Holds(item) > before;
			});
			char text[160];
			std::snprintf(text, sizeof text, "and then it gathers %s (the clan holds %d, it had %d)", item.c_str(), clan.Holds(item), before);
			Check(clan.Holds(item) > before || taken, text);
		}
		int explored = 0;
		for (const auto& [region, lore] : clan.Tribe_.Exploration().Atlas.Lore) explored += lore.Explored > 0.0f ? 1 : 0;
		Check(explored >= 2, "the clan's map knows more than its home region");
		Check(clan.Tribe_.Exploration().Moves.empty(), "a clan with a good home does not move (with an expedition out)");
	}
	std::printf("(expedition: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	// ---- a save in the middle of an expedition
	Check(!savedMid.empty() && savedState.Current.Stage == "out", "the clan was saved with a party out");
	if (!savedMid.empty())
	{
		Clan loaded(land, camp, 4, quick);
		Check(loaded.Tribe_.FromJson(savedMid, false), "the save loads");
		Check(loaded.Tribe_.Exploration() == savedState, "with its map, its expedition and its party as they were");
		const int id = savedState.Current.Id;
		loaded.Run(1.6 * static_cast<double>(quick.Expeditions.MaxSeconds), [&]
		{
			for (const ExpeditionRecord& r : loaded.Tribe_.Exploration().History) if (r.Id == id) return true;
			return false;
		});
		const auto& h = loaded.Tribe_.Exploration().History;
		Check(std::any_of(h.begin(), h.end(), [&](const ExpeditionRecord& r) { return r.Id == id; }), "and the loaded clan brings that expedition home");

		nlohmann::json older_ = nlohmann::json::parse(savedMid);   // an H7 save: the same tribe without the "explore" key
		older_.erase("explore");
		const std::string old = older_.dump();
		Clan older(land, camp, 4, quick);
		Check(!older_.contains("explore") && older.Tribe_.FromJson(old, false) && !older.Tribe_.Exploration().Current.Active(), "a save from before H12 loads, with no expedition");
	}
	std::printf("(save: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	// ---- migration: a home stripped bare is left; a good one is kept
	ExplorationRules moving = quick;
	moving.Migration.Settle = 300.0f;
	moving.Migration.After = 400.0f;
	moving.Migration.Every = 10.0f;
	{
		Clan clan(land, camp, 4, moving);
		clan.Run(1800.0);
		Check(clan.Tribe_.Exploration().Moves.empty(), "a clan with food and water at home stays (half an hour, impatient rules)");
		Check(clan.Alive() == 4, "and lives");
	}
	{
		const float bare = 75.0f;
		Clan clan(land, camp, 4, moving, [&](const ResourceSpot& s) { return glm::distance(glm::vec2(s.Position.x, s.Position.z), camp) > bare; });
		clan.Tribe_.Store().Add(clan.Items, "berry", 20);
		const bool moved = clan.Run(5400.0, [&] { return !clan.Tribe_.Exploration().Moves.empty(); });
		std::printf("%s", clan.Tribe_.ExploreReport().c_str());
		Check(moved, "a clan whose home was stripped bare moves its camp");
		if (moved)
		{
			const MigrationRecord r = clan.Tribe_.Exploration().Moves.back();
			Check(glm::distance(r.From, camp) < 1.0f && glm::distance(r.To, camp) >= moving.Migration.HomeRadius, "away from the old home, at least a home radius");
			Check(clan.Tribe_.Config.Economy.Camp == r.To, "the camp (and the store with it) is the new one");
			if (clan.Tribe_.HasFire())
			{
				const EntityId fire = clan.Entities.FindByName(clan.Tribe_.Config.FireName);
				const glm::vec4 p = clan.Entities.WorldMatrix(fire)[3];
				Check(glm::distance(glm::vec2(p.x, p.z), r.To) < 3.0f, "the campfire is at the new camp");
			}
			const bool arrived = clan.Run(900.0, [&] { return !clan.Tribe_.Exploration().Moving; });
			int near = 0;
			for (const TribeMember& m : clan.Tribe_.Members()) if (!m.Dead && glm::distance(m.Body->Position(), r.To) < 25.0f) ++near;
			Check(arrived && near >= 1, "the members walk to the new camp");
		}
	}
	std::printf("(migration: %.1f s)\n", std::chrono::duration<double>(Clock::now() - t0).count());

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("exploration: all checks passed\n");
	return failures ? 1 : 0;
}
