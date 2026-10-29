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

// A tribe of four with the patch of resource nodes a `tribe build` makes, a well, the era-1 buildings and the BuildingSystem.
struct Settlement
{
	explicit Settlement(bool buildings = true)
	{
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
		Tribe_.Config.Camp = camp;
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
		Check(file.size() == 4 && file.count("shelter") && file.count("storage") && file.count("workbench") && file.count("drying_rack"), "shelter, storage, workbench and drying rack");
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
			&& keep.at("kiln").Tech == "firing" && keep.at("kiln").Station == "kiln" && keep.size() == 5, "a new building (with a tech) is a few lines of data");
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
		Check(glm::distance(glm::vec2(at.x, at.z), v.Tribe_.Config.Camp) > 4.0f && glm::distance(glm::vec2(at.x, at.z), v.Tribe_.Config.Camp) < 25.0f, "it stands near the camp, off its centre");
		Check(v.Tribe_.SettlementReport().find("Shelter 1 (shelter) stands, 3 beds") != std::string::npos, "tribe status shows it");
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
			const glm::vec2 far = v.Tribe_.Config.Camp + glm::vec2(80.0f, 0.0f);
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
