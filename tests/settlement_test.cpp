// Construction and settlements (H8, E9): the buildings as data (era1.json says what the code says, round-trip, a mistake changes nothing), a site
// built phase by phase with the ability Build, the choice of the camp on generated worlds (deterministic, by the water but not in it, on flat ground,
// near what the clan wants), a tribe that builds its shelter by itself, and a site saved and loaded in the middle of its work. CPU only.
#include "GB/Log.h"
#include "GB/Game/SceneIO.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/SiteChoice.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/World/WorldGen.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <sstream>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

using namespace GB;

// Runs the text of a task list against an ability system (as in tribe_test): goto and wait take time, "ability A node" walks to it, activates it
// and waits until it ends.
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
	void Update(float dt)
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
		if (s.Seconds > 0.0f) { s.Seconds -= dt; if (s.Seconds > 0.0f) return; m_Pos = m_Target; }
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

// A tribe of four with the patch of resource nodes a `tribe build` makes, a well, the era-1 buildings and the BuildingSystem. Plain (as before the
// progression): no technologies, and only the four buildings no technology locks (without technologies everything would be known, the longhouse too).
// `stoneAge`: the era-1 technologies and every building of the progression (docs/PROGRESION.md).
struct Settlement
{
	explicit Settlement(bool buildings = true, bool stoneAge = false)
	{
		if (!stoneAge)
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
		const glm::vec2 camp(0.0f, -8.0f);
		for (const char* n : names)
		{
			bodies.push_back(std::make_unique<FakeBody>(n, Gameplay, Entities, camp));
			bodies.back()->m_Items = &Items;
		}
		auto bagOf = [this](const std::string& who) -> Inventory* { for (size_t i = 0; i < Tribe_.Members().size(); ++i) if (Tribe_.Members()[i].Name == who) return &bodies[i]->Bag(); return nullptr; };
		Resources.Bind(Gameplay, Defs, [this, bagOf](const std::string& who, const std::string& item, int n) { if (Inventory* b = bagOf(who)) b->Add(Items, item, n); });
		Fires.Items = &Items;
		Fires.Bind(Gameplay, [bagOf](const std::string& who, const std::string& item, int n) { Inventory* b = bagOf(who); return b ? b->Remove(item, n) : 0; },
			[this, bagOf](const std::string& who, const std::string& item, int n) { Inventory* b = bagOf(who); return b ? b->Add(Items, item, n) : 0; });
		Builds.Bind(Gameplay);
		Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
		if (buildings) Tribe_.SetBuildings(&Builds.Defs);
		if (stoneAge) Tribe_.SetTechs(&Techs);
		Tribe_.Config.Economy.Camp = camp;
		Tribe_.Store().Add(Items, "wood", 10);
		Tribe_.Store().Add(Items, "stone", 12);
		for (size_t i = 0; i < 4; ++i) Tribe_.AddMember(names[i], roles[i], bodies[i].get());
	}
	void Step(float dt)
	{
		Gameplay.Update(dt);
		Resources.Update(Gameplay, dt);
		for (auto& b : bodies) b->Update(dt);
		Resources.SyncVisuals(Gameplay, Entities);
		Fires.Update(dt, &Entities);
		Builds.Update(dt, &Entities);
		Tribe_.Update(dt);
	}
	const AbilitySystemComponent* Shelter() const { return Gameplay.Find("Shelter 1"); }
	bool ShelterDone() const { const AbilitySystemComponent* s = Shelter(); return s && s->HasTag("Building.Done"); }
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

// A synthetic land 200 m across: a lake at (60, 0) of radius 15 with its bank, a steep hill to the west (x < -40), snow in the far north, berries and
// trees by the lake's south-west shore and a rocky field far to the east.
static ResourceGround Land()
{
	ResourceGround g;
	g.Min = { -100.0f, -100.0f };
	g.Max = { 100.0f, 100.0f };
	g.Height = [](float, float) { return 0.0f; };
	g.WaterDistance = [](float x, float z) { return std::max(0.0f, glm::length(glm::vec2(x - 60.0f, z)) - 15.0f); };
	g.Biome = [](float x, float z) -> std::string { return glm::length(glm::vec2(x - 60.0f, z)) < 15.0f ? "Water" : z > 70.0f ? "Snow" : x < -40.0f ? "Rock" : "Meadow"; };
	g.SlopeDegrees = [](float x, float) { return x < -40.0f ? 30.0f : 3.0f; };
	return g;
}

int main()
{
	Log::Init();

	// ---- the data: era1.json says what the code says; round-trip; mistakes change nothing
	{
		std::map<std::string, BuildingDef> file;
		Check(LoadBuildingsFile(std::string(GB_ASSETS_DIR) + "/data/era1.json", file).empty(), "the buildings of assets/data/era1.json load");
		Check(file == DefaultBuildings(), "and say what DefaultBuildings says");
		Check(file.size() == 16 && file.count("shelter") && file.count("storage") && file.count("workbench") && file.count("drying_rack") && file.count("hut") && file.count("forge"),
			"shelter, storage, workbench and drying rack, and the twelve of the Stone-Age progression (docs/PROGRESION.md)");
		Check(file.at("hut").Supersedes == std::vector<std::string>{ "shelter" } && file.at("granary").Keep == 0.5f && file.at("palisade").Defense == 1.0f && file.at("totem").Aura == 9.0f,
			"with their provisions");
		std::map<std::string, BuildingDef> odd = DefaultBuildings();
		Check(!LoadBuildings(R"({ "buildings": { "pit": { "phases": [ { "name": "x" } ], "keep": 0 } } })", odd).empty() && odd == DefaultBuildings(), "a store that keeps nothing (keep 0) is refused");
		Check(!LoadBuildings(R"({ "buildings": { "pit": { "phases": [ { "name": "x" } ], "supersedes": [ "pit" ] } } })", odd).empty(), "a building cannot supersede itself");
		std::map<std::string, BuildingDef> back;
		Check(LoadBuildings("{ \"buildings\": " + BuildingsToJson(DefaultBuildings()) + " }", back).empty() && back == DefaultBuildings(), "the buildings round-trip");
		std::map<std::string, BuildingDef> keep = DefaultBuildings();
		Check(!LoadBuildings(R"({ "buildings": { "a": { "phases": [ { "name": "x", "work": 3 } ] }, "b": { "phases": [ { "name": "y", "needs": { "wood": -1 } } ] } } })", keep).empty()
			&& keep == DefaultBuildings(), "a negative need is refused and nothing changes (not even the good building before it)");
		Check(!LoadBuildings(R"({ "buildings": { "a": { "phases": [] } } })", keep).empty(), "a building without phases is refused");
		Check(!LoadBuildings(R"({ "buildings": { "a b": { "phases": [ { "name": "x" } ] } } })", keep).empty(), "an id that cannot be a tag is refused");
		Check(!LoadBuildings(R"({ "buildings": { "a": { "phases": [ { "name": "x", "part": "dome" } ] } } })", keep).empty(), "an unknown part is refused");
		Check(!LoadBuildings(R"({ "buildings": { "a": { "phases": [ { "name": "x" }, { "name": "x" } ] } } })", keep).empty(), "two phases of the same name are refused");
		Check(LoadBuildings(R"({ "items": [] })", keep).empty() && keep == DefaultBuildings(), "a file without buildings changes nothing");
		Check(LoadBuildings(R"({ "buildings": { "kiln": { "name": "Kiln", "phases": [ { "name": "base", "needs": { "clay": 8 }, "part": "slab" } ], "station": "kiln", "tech": "firing" } } })", keep).empty()
			&& keep.at("kiln").Tech == "firing" && keep.at("kiln").Station == "kiln" && keep.size() == DefaultBuildings().size(), "a building (with a tech) is a few lines of data: the kiln is replaced");
	}

	// ---- one site, phase by phase, with the ability Build
	{
		const std::map<std::string, BuildingDef> defs = DefaultBuildings();
		const BuildingDef& def = defs.at("shelter");
		EntityWorld entities;
		GameplayWorld gameplay;
		RegisterBuildingContent(gameplay.Library);
		const EntityId id = BuildSite(entities, def, "Shelter 1", { 5.0f, 0.0f, 5.0f });
		Entity& worker = *entities.Find(entities.Create("Worker"));
		worker.Gameplay.emplace();
		worker.Gameplay->Abilities = { "Build" };
		Check(entities.Find(id) && entities.FindByName("Shelter 1 roof") != NullEntity, "the site is an entity with a part per phase");
		gameplay.Load(entities);
		BuildingSystem system;
		system.Bind(gameplay);
		system.Bind(gameplay);   // (bound again, as the editor does on every scene change: still one listener, or every Build would count twice)
		AbilitySystemComponent* site = gameplay.Find("Shelter 1");
		AbilitySystemComponent* me = gameplay.Find("Worker");
		SiteState s = ReadSite(*site, def);
		Check(s.Phase == 0 && !s.Ready && !s.Done && s.Missing.at("stone") == 3 && s.PhaseName == "foundation", "it starts at its foundation, lacking 3 stone");
		Check(!me->Activate("Build", site), "Build is refused while the phase lacks materials");
		Check(DeliverToSite(*site, def, "branch", 4) == 0, "the site takes nothing the phase does not need");
		Check(DeliverToSite(*site, def, "stone", 5) == 3 && site->HasTag("Building.Ready"), "it takes what the phase lacks, and then it can be worked");
		int uses = 0;
		for (int frame = 0; frame < 60 * 400 && !site->HasTag("Building.Done"); ++frame)
		{
			gameplay.Update(1.0f / 60.0f);
			system.Update(1.0f / 60.0f, &entities);
			if (!me->AnyActive() && site->HasTag("Building.Ready") && me->Activate("Build", site)) ++uses;
			const SiteState now = ReadSite(*site, def);
			if (!now.Ready && !now.Done)
				for (const auto& [item, n] : now.Missing) DeliverToSite(*site, def, item, n);   // the materials of the next phase
		}
		s = ReadSite(*site, def);
		Check(s.Done && site->HasTag("Building.Done") && !site->HasTag("Building.Site") && s.Progress == 1.0f, "Build after Build, phase after phase, it stands");
		Check(uses == static_cast<int>(std::ceil(6.0 / 2.0) + std::ceil(12.0 / 2.0) + std::ceil(10.0 / 2.0)), "each Build gives WorkPerUse seconds of work");
		const Entity* roof = entities.Find(entities.FindByName("Shelter 1 roof"));
		Check(roof && roof->Active && roof->Transform.Scale.y == 1.0f, "and its roof is shown at full size");
		Check(system.Report().find("Shelter 1 (shelter): done") != std::string::npos, "the report says so");
	}

	// ---- construction failure modes: missing materials must not make progress, and a site created after the
	// gameplay binding must still be connected to the BuildingSystem and persisted back to the entity
	{
		const std::map<std::string, BuildingDef> defs = DefaultBuildings();
		const BuildingDef& def = defs.at("shelter");
		EntityWorld entities;
		GameplayWorld gameplay;
		RegisterBuildingContent(gameplay.Library);
		BuildingSystem system;
		system.Bind(gameplay);

		const EntityId id = BuildSite(entities, def, "Shelter 1", { 0.0f, 0.0f, 0.0f });
		AbilitySystemComponent* site = gameplay.Create("Shelter 1");
		site->FromComponent(*entities.Find(id)->Gameplay);
		system.Update(0.0f, &entities); // the site was added after Bind: this must install its listener
		Entity& workerEntity = *entities.Find(entities.Create("Worker"));
		workerEntity.Gameplay.emplace();
		workerEntity.Gameplay->Abilities = { "Build" };
		AbilitySystemComponent* worker = gameplay.Create("Worker");
		worker->FromComponent(*workerEntity.Gameplay);

		Check(!worker->Activate("Build", site), "a site without materials cannot start construction");
		for (int frame = 0; frame < 120; ++frame)
		{
			gameplay.Update(1.0f / 60.0f);
			system.Update(1.0f / 60.0f, &entities);
		}
		SiteState blocked = ReadSite(*site, def);
		Check(blocked.Phase == 0 && blocked.Work == 0.0f && !blocked.Ready && !blocked.Done,
			"missing materials leave the site blocked without accumulating work");

		Check(DeliverToSite(*site, def, "stone", 3) == 3 && site->HasTag("Building.Ready"),
			"delivering the missing materials makes the phase ready");
		for (int frame = 0; frame < 60 * 60 && !site->HasTag("Building.Done"); ++frame)
		{
			if (!worker->AnyActive() && site->HasTag("Building.Ready"))
				Check(worker->Activate("Build", site), "the worker can activate Build after delivery");
			gameplay.Update(1.0f / 60.0f);
			system.Update(1.0f / 60.0f, &entities);
			const SiteState now = ReadSite(*site, def);
			if (!now.Ready && !now.Done)
				for (const auto& [item, n] : now.Missing) DeliverToSite(*site, def, item, n);
		}
		Check(site->HasTag("Building.Done"), "a site added after the initial bind can finish all phases");
		const int storedCount = gameplay.Store(entities);
		Check(storedCount == 2, "the gameplay state stores the worker and building back to the scene");
		const Entity* stored = entities.Find(id);
		Check(stored && stored->Gameplay && std::find(stored->Gameplay->Tags.begin(), stored->Gameplay->Tags.end(), "Building.Done") != stored->Gameplay->Tags.end(),
			"the completed building remains finished in the entity component");
		GameplayWorld loaded;
		RegisterBuildingContent(loaded.Library);
		Check(loaded.Load(entities) == 2, "the stored gameplay state loads back for the worker and building");
		const AbilitySystemComponent* loadedSite = loaded.Find("Shelter 1");
		Check(loadedSite && loadedSite->HasTag("Building.Done") && ReadSite(*loadedSite, def).Done,
			"the loaded building remains finished after gameplay synchronization");
	}

	// ---- the place of the camp on a synthetic land
	{
		const ResourceGround land = Land();
		SiteWeights w;
		w.Want = { { "berry_bush", 1.0f }, { "tree", 1.0f } };
		std::vector<SiteResource> res;
		for (int i = 0; i < 6; ++i) res.push_back({ "berry_bush", { 25.0f + 3.0f * static_cast<float>(i), -25.0f } });
		for (int i = 0; i < 6; ++i) res.push_back({ "tree", { 25.0f + 3.0f * static_cast<float>(i), -32.0f } });
		for (int i = 0; i < 8; ++i) res.push_back({ "rock", { 80.0f, -80.0f + 3.0f * static_cast<float>(i) } });
		Check(!ScoreSite(land, res, w, { 60.0f, 0.0f }).Ok && ScoreSite(land, res, w, { 60.0f, 0.0f }).Why == "in the water", "a place in the lake is refused");
		Check(!ScoreSite(land, res, w, { 60.0f, 17.0f }).Ok, "and one on its bank");
		Check(!ScoreSite(land, res, w, { -60.0f, 0.0f }).Ok && ScoreSite(land, res, w, { -60.0f, 0.0f }).Why.find("steep") != std::string::npos, "and one on the hill");
		Check(!ScoreSite(land, res, w, { 0.0f, 80.0f }).Ok, "and one in the snow (or by the edge)");
		const SiteRating best = BestSite(land, res, w);
		std::fprintf(stderr, "  synthetic: %s\n", best.Text().c_str());
		const float water = land.WaterDistance(best.At.x, best.At.y);
		Check(best.Ok && water >= w.WaterNear && water <= 45.0f, "the best place is near the lake but not on its bank");
		Check(best.Near.count("berry_bush") && best.Near.count("tree"), "and near the berries and the trees the clan wants");
		const std::vector<SiteRating> ranked = RankSites(land, res, w);
		bool sorted = true;
		for (size_t i = 1; i < ranked.size(); ++i) sorted = sorted && ranked[i - 1].Total >= ranked[i].Total;
		Check(sorted && ranked.front().At == best.At, "the ranking goes best first");
		w.Want = { { "rock", 1.0f } };
		const SiteRating rocky = BestSite(land, res, w);
		Check(rocky.Ok && rocky.Near.count("rock"), "a clan that wants stone settles by the rocks");
	}

	// ---- the place of the camp on generated worlds (CPU): deterministic and sensible
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> defs;
		RegisterEraOneItems(items, recipes, defs);
		Tribe probe;
		EntityWorld entities;
		GameplayWorld gameplay;
		probe.Bind(entities, gameplay, items, recipes, defs);
		int worlds = 0, good = 0;
		for (const WorldType type : { WorldType::Temperate, WorldType::Alpine })
			for (uint32_t seed : { 3u, 5u, 11u })
			{
				WorldDesc d;
				d.Type = type;
				d.Seed = seed;
				d.Size = 192.0f;
				d.Resolution = 161;
				d.TreeBudget = 150;
				d.RockBudget = 40;
				const auto t0 = std::chrono::steady_clock::now();
				const GeneratedWorld world = WorldGenerator::GenerateData(d);
				const ResourceGround ground = ResourceGroundOf(world);
				const std::vector<SiteResource> res = SpotResources(PlanWorldResources(ground, defs, seed, 1.0f));
				const SiteWeights w = ClanSiteWeights(probe, { "forager", "woodcutter", "miner", "forager" });
				const SiteRating a = BestSite(ground, res, w), b = BestSite(ground, res, w);
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
				std::fprintf(stderr, "  %s %u: %s (%.0f ms with the world)\n", type == WorldType::Alpine ? "alpine" : "temperate", seed, a.Text().c_str(), ms);
				++worlds;
				Check(a.At == b.At && a.Total == b.Total, "the same world gives the same camp");
				if (!a.Ok) continue;
				const float water = world.WaterDistanceAt(a.At.x, a.At.y);
				bool flat = true;
				for (int k = 0; k < 8; ++k)
				{
					const glm::vec2 p = a.At + glm::vec2(std::cos(0.785f * static_cast<float>(k)), std::sin(0.785f * static_cast<float>(k))) * w.FlatRadius;
					flat = flat && ground.SlopeDegrees(p.x, p.y) <= w.MaxSlope;
				}
				int near = 0;
				for (const auto& [kind, n] : a.Near) near += n;
				if (water >= w.WaterNear && flat && near > 0 && world.BiomeAt(a.At.x, a.At.y) != Biome::Water) ++good;
				else std::fprintf(stderr, "FAIL: the camp of seed %u is not sensible: water %.1f m, flat %d, %d nodes near\n", seed, static_cast<double>(water), flat ? 1 : 0, near), ++failures;
			}
		Check(good >= worlds - 1, "on generated worlds the camp is by the water, on flat ground and near resources (a world may have no place)");
	}

	// ---- a tribe builds its shelter by itself (and nothing changes for a tribe without buildings)
	{
		Settlement v;
		const float dt = 1.0f / 30.0f;
		double doneAt = -1.0;
		bool siteAt = false, hauled = false, worked = false, started = false;
		for (int frame = 0; frame < static_cast<int>(3600.0f / dt) && doneAt < 0.0; ++frame)
		{
			v.Step(dt);
			siteAt = siteAt || v.Shelter() != nullptr;
			for (const TribeMember& m : v.Tribe_.Members())
			{
				hauled = hauled || m.Job == "deliver";
				worked = worked || m.Job == "build";
			}
			if (v.ShelterDone()) doneAt = v.Tribe_.Time();
			if (frame % 30 == 0 && !started)
				for (const std::string& l : v.Tribe_.Log()) started = started || l.find("the clan starts Shelter 1") != std::string::npos;
		}
		const std::vector<std::string> logAtDone = v.Tribe_.Log();
		const int framesToDone = static_cast<int>(std::lround(doneAt / dt));
		for (size_t i = v.Tribe_.Log().size() > 60 ? v.Tribe_.Log().size() - 60 : 0; i < v.Tribe_.Log().size(); ++i) std::fprintf(stderr, "  %s\n", v.Tribe_.Log()[i].c_str());
		std::fprintf(stderr, "%s", v.Tribe_.Report().c_str());
		Check(siteAt, "the clan starts a shelter once its tools and campfire are done");
		Check(hauled && worked, "members carry the materials to the site and build it");
		Check(doneAt > 0.0, "the shelter stands within an hour of play");
		std::fprintf(stderr, "  the shelter stood at %.0f s\n", doneAt);
		Check(started, "the log tells when it starts");
		const EntityId shelter = v.Entities.FindByName("Shelter 1");
		const glm::vec4 at = shelter == NullEntity ? glm::vec4(0.0f) : v.Entities.WorldMatrix(shelter)[3];
		Check(glm::distance(glm::vec2(at.x, at.z), v.Tribe_.Config.Economy.Camp) > 4.0f && glm::distance(glm::vec2(at.x, at.z), v.Tribe_.Config.Economy.Camp) < 25.0f, "it stands near the camp, off its centre");
		Check(v.Tribe_.SettlementReport().find("Shelter 1 (shelter) stands: 3 beds") != std::string::npos, "tribe status shows it");
		// the night after, the first three sleep in it (their beds are inside its footprint) and the fourth at the camp
		int inside = 0;
		bool stands = false;
		for (int frame = 0; frame < static_cast<int>(1200.0f / dt); ++frame)
		{
			v.Step(dt);
			if (frame % 30 != 0) continue;
			int now = 0;
			for (const TribeMember& m : v.Tribe_.Members())
				if (m.Job == "sleep" && glm::distance(m.Body->Position(), glm::vec2(at.x, at.z)) < 2.0f) ++now;
			inside = std::max(inside, now);
			if (!stands) for (const std::string& l : v.Tribe_.Log()) stands = stands || l.find("Shelter 1 stands") != std::string::npos || l.find("finishes Shelter 1") != std::string::npos;
		}
		std::fprintf(stderr, "  at most %d asleep in the shelter at once\n", inside);
		Check(stands, "and when it stands");
		Check(inside >= 1 && inside <= 3, "they sleep in it, three at most (its beds)");

		// a clan that moves its camp (H12) leaves its buildings behind and builds again where it settles
		{
			const glm::vec2 far = v.Tribe_.Config.Economy.Camp + glm::vec2(80.0f, 0.0f);
			Check(v.Tribe_.MoveCamp(far, "a test"), "the camp moves");
			Check(v.ShelterDone() && v.Tribe_.SettlementReport().find("Shelter 1") == std::string::npos, "the shelter stays where it was, and is no longer the clan's");
			const std::vector<BuildingNeed> needs = v.Tribe_.BuildingNeeds();
			Check(!needs.empty() && needs.front().Building == "shelter" && needs.front().Why == "beds for 0 of 4", "so the clan wants a shelter again at its new camp");
		}
		{
			Tribe lone;
			EntityWorld e;
			GameplayWorld g;
			lone.Bind(e, g, v.Items, v.Recipes, v.Defs);
			CampSite site;
			Check(!SettlementSiteChooser(lone, Land())(CampSiteRequest{}, site), "H8's chooser for a migration finds nothing for a clan that knows nothing");
		}

		// two runs end the same
		Settlement w;
		for (int frame = 0; frame < framesToDone; ++frame) w.Step(dt);
		Check(w.ShelterDone() && w.Tribe_.Log() == logAtDone, "a second run builds it at the same moment, with the same log");

		Settlement plain(false);
		for (int frame = 0; frame < static_cast<int>(900.0f / dt); ++frame) plain.Step(dt);
		bool any = false;
		for (const std::string& n : plain.Gameplay.Names()) any = any || plain.Gameplay.Find(n)->HasTag("Building");
		Check(!any && plain.Tribe_.StationAvailable("workbench") && !plain.Tribe_.StationAvailable("drying_rack") && !plain.Tribe_.StationAt("drying_rack"),
			"without buildings the tribe builds nothing and the camp is its only workbench");
	}

	// ---- the job board posts the construction jobs (H7), and a site saved in the middle of its work comes back as it was
	{
		Settlement v;
		const float dt = 1.0f / 30.0f;
		bool haulJob = false, buildJob = false;
		int frame = 0;
		for (; frame < static_cast<int>(3600.0f / dt); ++frame)
		{
			v.Step(dt);
			for (const auto& [id, job] : v.Tribe_.Board().Jobs())
			{
				haulJob = haulJob || job.Kind == "deliver";
				buildJob = buildJob || job.Kind == "build";
			}
			const AbilitySystemComponent* s = v.Shelter();
			if (s && ReadSite(*s, v.Builds.Defs.at("shelter")).Phase >= 1) break;
		}
		Check(haulJob && buildJob, "the job board posts hauling to the site and working its phase");
		const AbilitySystemComponent* site = v.Shelter();
		Check(site != nullptr, "there is a site in the middle of its work");
		if (site)
		{
			const SiteState before = ReadSite(*site, v.Builds.Defs.at("shelter"));
			v.Gameplay.Store(v.Entities);
			const std::string scene = SaveSceneToString(v.Entities);
			const std::string tribe = v.Tribe_.ToJson();

			Settlement loaded;
			Check(LoadSceneFromString(loaded.Entities, scene).Ok, "the scene with the site loads");
			loaded.Gameplay.Load(loaded.Entities);
			loaded.Resources.Bind(loaded.Gameplay, loaded.Defs, [](const std::string&, const std::string&, int) {});
			loaded.Builds.Bind(loaded.Gameplay);
			Check(loaded.Tribe_.FromJson(tribe, true), "the tribe loads");
			const AbilitySystemComponent* again = loaded.Shelter();
			const SiteState after = again ? ReadSite(*again, loaded.Builds.Defs.at("shelter")) : SiteState{};
			Check(again && after.Phase == before.Phase && after.Work == before.Work && after.Missing == before.Missing && after.Ready == before.Ready,
				"its phase, its work and what it lacks come back as they were");
			// an older save of a tribe (no "plan") loads with the planner at its start
			std::string old = tribe;
			if (const size_t at = old.find("\"plan\":"); at != std::string::npos) old.erase(at, old.find(',', at) - at + 1);
			Check(loaded.Tribe_.FromJson(old, false), "a tribe saved before the buildings loads");
		}
	}

	// ---- the provisions of the Stone-Age buildings (docs/PROGRESION.md): the store's cap and spoilage, defense against raids, healing, company,
	// and several sites for a big clan
	{
		// raises a building at once: every phase gets its materials and its work
		auto raise = [](Settlement& s, const std::string& id, const glm::vec2& at)
		{
			std::string name;
			if (!s.Tribe_.StartSite(id, at, &name).empty()) return std::string();
			AbilitySystemComponent* a = s.Gameplay.Find(name);
			const BuildingDef& def = s.Builds.Defs.at(id);
			for (int guard = 0; guard < 20 && !a->HasTag("Building.Done"); ++guard)
			{
				for (const auto& [item, n] : ReadSite(*a, def).Missing) DeliverToSite(*a, def, item, n);
				WorkOnSite(*a, def, 1000.0f);
			}
			return a->HasTag("Building.Done") ? name : std::string();
		};
		Settlement v(true, true);
		const glm::vec2 camp = v.Tribe_.Config.Economy.Camp;
		const int cap = v.Tribe_.StoreCap();
		Check(v.Tribe_.Defense() == 0.0f && v.Tribe_.StoreBonus() == 0 && v.Tribe_.StoreKeep() == 1.0f && v.Tribe_.HealingAt(camp) == 1.0f, "a camp without them: no defense, no bonus, no healer");
		Check(!raise(v, "cache_pit", camp + glm::vec2(-10.0f, 0.0f)).empty() && !raise(v, "granary", camp + glm::vec2(-10.0f, 8.0f)).empty(), "a cache pit and a granary stand");
		Check(v.Tribe_.StoreCap() == cap + 30 && v.Tribe_.StoreKeep() == 0.5f, "they add to the main store's cap (+10, +20) and the best of them halves its spoilage");
		// the berries of the store keep twice as long: the same old stack goes off in the plain camp and not in this one
		{
			Settlement plain(true, true);
			for (Settlement* s : { &v, &plain })
			{
				s->Tribe_.Store().Remove("berry", 1000);
				s->Tribe_.Store().Add(s->Items, "berry", 30);
				for (ItemSlot& slot : const_cast<std::vector<ItemSlot>&>(s->Tribe_.Store().Stacks())) if (slot.Id == "berry") slot.Age = 1500.0f;
			}
			const float dt = 1.0f / 30.0f;
			for (int frame = 0; frame < static_cast<int>(400.0f / dt); ++frame) { v.Step(dt); plain.Step(dt); }
			const int lostHere = v.Tribe_.Economy().Spoiled.count("berry") ? v.Tribe_.Economy().Spoiled.at("berry") : 0;
			const int lostThere = plain.Tribe_.Economy().Spoiled.count("berry") ? plain.Tribe_.Economy().Spoiled.at("berry") : 0;
			std::fprintf(stderr, "  berries lost in 400 s: %d with a granary, %d without\n", lostHere, lostThere);
			Check(lostThere > lostHere, "food keeps longer in a camp with a granary");
		}
		// defense: a palisade puts raiders off and makes the defenders bolder at the camp
		Check(!raise(v, "palisade", camp + glm::vec2(0.0f, 14.0f)).empty() && v.Tribe_.Defense() == 1.0f, "a palisade gives the camp a defense of 1");
		const CombatRules rules = DefaultCombatRules();
		const RaidMotive open = WeighRaid(rules.Raids, 0.6f, 0.6f, 0.4f, 0.5f), walled = WeighRaid(rules.Raids, 0.6f, 0.6f, 0.4f, 0.5f, v.Tribe_.Defense());
		Check(walled.Score < open.Score && open.Score >= rules.Raids.Want && walled.Score < rules.Raids.Want, "a raid worth it on an open camp is not worth it on a walled one");
		Check(walled.Why().find("walls") != std::string::npos, "and the motive says why");
		ThreatView view;
		view.Camp = true;
		const float bare = FightOrFlight(rules.Threats, rules.Morale, view).Fight;
		view.Defense = v.Tribe_.Defense();
		Check(FightOrFlight(rules.Threats, rules.Morale, view).Fight > bare, "defenders behind the palisade are readier to fight at the camp");
		HostilityTable table;
		Inventory store(4, 0.0f);
		table.Set("Raiders", "Clan", 1.0f);
		table.Settled("Clan", camp, &store, v.Tribe_.Defense());
		Check(!table.RaidTargets("Raiders").empty() && table.RaidTargets("Raiders").front().Defense == 1.0f, "the clan tells the raiders' side its defense with its camp");
		// healing: the wounded heal twice as fast by the healer's hut, and rest there
		const std::string hut = raise(v, "healer_hut", camp + glm::vec2(10.0f, -2.0f));
		Check(!hut.empty(), "a healer's hut stands");
		const glm::vec2 spot = v.Tribe_.RecoverSpot(v.Tribe_.Members().front());
		Check(v.Tribe_.HealingAt(spot) == 2.0f && v.Tribe_.HealingAt(camp + glm::vec2(-30.0f, 0.0f)) == 1.0f && glm::distance(spot, camp + glm::vec2(10.0f, -2.0f)) < 4.0f,
			"a wounded member rests before it, where wounds heal twice as fast (not far from it)");
		{
			CombatSystem cs;
			cs.Bind(v.Entities, v.Gameplay);
			cs.Join("Cleo", "Clan");
			cs.Join("Dan", "Clan");
			cs.Hurt("Cleo", 5.0f, "cut", 0.5f);
			cs.Hurt("Dan", 5.0f, "cut", 0.5f);
			for (int i = 0; i < 300; ++i)
			{
				cs.Find("Dan")->Heal = v.Tribe_.HealingAt(spot);
				cs.Update(1.0f);
			}
			const float cleo = cs.Find("Cleo")->Severity(), dan = cs.Find("Dan")->Severity();
			std::fprintf(stderr, "  wound severity after 300 s: %.3f at the camp, %.3f by the healer\n", static_cast<double>(cleo), static_cast<double>(dan));
			Check(dan < cleo, "the same wound heals faster by the healer's hut");
		}
		// company: a totem by the camp keeps the clan in better company than a camp without one
		{
			Settlement with(true, true), without(true, true);
			Check(!raise(with, "totem", with.Tribe_.Config.Economy.Camp + glm::vec2(9.0f, 0.0f)).empty(), "a totem stands by the camp");
			const float dt = 1.0f / 30.0f;
			double sumWith = 0.0, sumWithout = 0.0;
			for (int frame = 0; frame < static_cast<int>(600.0f / dt); ++frame)
			{
				with.Step(dt);
				without.Step(dt);
				if (frame % 30 != 0) continue;
				for (const TribeMember& m : with.Tribe_.Members()) sumWith += with.Tribe_.NeedLevels(m)[NeedName::Company];
				for (const TribeMember& m : without.Tribe_.Members()) sumWithout += without.Tribe_.NeedLevels(m)[NeedName::Company];
			}
			std::fprintf(stderr, "  mean company: %.1f with a totem, %.1f without\n", sumWith / 600.0 / 4.0, sumWithout / 600.0 / 4.0);
			Check(sumWith > sumWithout, "the clan keeps better company with a meeting place");
		}
		Check(ConstructionRules().SitesFor(4) == 1 && ConstructionRules().SitesFor(8) == 2 && ConstructionRules().SitesFor(40) == 3 && ConstructionRules{ 2, 0 }.SitesFor(1) == 2,
			"one site per four members, three at most (four members: one at a time, as before)");
		Check(v.Tribe_.SettlementReport().find("defense 1") != std::string::npos && v.Tribe_.SettlementReport().find("wounds heal x2") != std::string::npos, "the report says what each gives");
	}

	// ---- the Stone-Age progression plays (docs/PROGRESION.md): a clan of eight on a generated world, with the technologies and the buildings of
	// era 1, finds new technologies by itself in a few days and raises buildings of several kinds, some of them behind what it found
	{
		const std::map<std::string, BuildingDef> defs = DefaultBuildings();
		const TechTree techs = DefaultTechs();
		WorldDesc d;
		d.Seed = 5;
		d.Size = 192.0f;
		d.Resolution = 129;
		d.TreeBudget = 120;
		d.RockBudget = 30;
		const GeneratedWorld world = WorldGenerator::GenerateData(d);
		auto play = [&](std::set<std::string>& found, std::set<std::string>& kinds, int& mostOpen, std::string& log)
		{
			SimSetup setup;
			setup.World = &world;
			setup.Members = 8;
			setup.Buildings = &defs;
			setup.Techs = &techs;
			setup.Step = 0.2f;
			setup.CheckEvery = 10;
			SimRun run(setup);
			if (!run.Ok()) return false;
			for (int part = 0; part < 6 * 24; ++part)
			{
				run.Run(50.0);
				int open = 0;
				for (const Tribe::SettlementSite& s : run.Brain().SettlementSites()) if (!s.State.Done) ++open;
				mostOpen = std::max(mostOpen, open);
			}
			run.Finish();
			for (const std::string& t : run.Brain().Knowledge().Clan()) if (!techs.Techs.at(t).Known) found.insert(t);
			for (const Tribe::SettlementSite& s : run.Brain().SettlementSites())
				if (s.State.Done)
					for (const auto& [id, def] : defs) if (def.Name == s.Building) kinds.insert(id);
			log = run.Brain().SettlementReport() + run.Brain().Knowledge().ClanStatus();
			if (run.Monitor().Violations()) log += "\n" + run.Monitor().Describe();
			const auto counts = run.Monitor().CountByCheck();
			return counts.count("buildings") && counts.at("buildings") == 0 && (!counts.count("knowledge") || counts.at("knowledge") == 0);
		};
		std::set<std::string> found, kinds, found2, kinds2;
		int mostOpen = 0, mostOpen2 = 0;
		std::string log, log2;
		const bool clean = play(found, kinds, mostOpen, log);
		std::string f, k;
		for (const std::string& t : found) f += " " + t;
		for (const std::string& b : kinds) k += " " + b;
		std::fprintf(stderr, "  progression in 6 days: found%s; built%s; at most %d sites at once\n%s\n", f.c_str(), k.c_str(), mostOpen, log.c_str());
		Check(clean, "six days of the progression break neither the buildings invariant nor the knowledge one");
		Check(found.size() >= 3, "the clan finds at least three technologies of the tree by itself in six days");
		Check(kinds.size() >= 3, "and raises at least three different kinds of building");
		bool gated = false;
		for (const std::string& b : kinds)
			for (const auto& [id, t] : techs.Techs)
				if (!t.Known && std::find(t.Buildings.begin(), t.Buildings.end(), b) != t.Buildings.end()) gated = true;
		Check(gated, "at least one of them behind a technology it found");
		Check(mostOpen >= 2, "a clan of eight keeps two sites open at once");
		play(found2, kinds2, mostOpen2, log2);
		Check(found2 == found && kinds2 == kinds && log2 == log, "a second run finds and builds the same (deterministic)");
	}

	// ---- a whole clan with buildings on generated worlds (the QA harness of H20): the invariants hold, the "buildings" one included, and it builds
	{
		const std::map<std::string, BuildingDef> defs = DefaultBuildings();
		int sites = 0;
		for (uint32_t seed : { 5u, 11u })
		{
			WorldDesc d;
			d.Seed = seed;
			d.Size = 192.0f;
			d.Resolution = 129;
			d.TreeBudget = 120;
			d.RockBudget = 30;
			const GeneratedWorld world = WorldGenerator::GenerateData(d);
			SimSetup setup;
			setup.World = &world;
			setup.Members = 4;
			setup.Buildings = &defs;
			SimRun run(setup);
			Check(run.Ok(), "the harness starts with buildings");
			if (!run.Ok()) continue;
			run.Run(2.0 * 1200.0);
			run.Finish();
			const SimMonitor& mon = run.Monitor();
			std::fprintf(stderr, "  sim temperate %u: %d violations; %s", seed, mon.Violations(), run.Brain().SettlementReport().c_str());
			if (mon.Violations()) std::fprintf(stderr, "%s\n", mon.Describe().c_str());
			Check(mon.CountByCheck().count("buildings") && mon.CountByCheck().at("buildings") == 0, "the buildings invariant holds on a generated world");
			Check(mon.Violations() == 0, "and every other one");
			for (const std::string& n : run.View().Gameplay->Names()) if (run.View().Gameplay->Find(n)->HasTag("Building")) ++sites;
		}
		Check(sites > 0, "in two days the clans of two worlds start building");
	}

	if (failures == 0) std::printf("settlement: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
