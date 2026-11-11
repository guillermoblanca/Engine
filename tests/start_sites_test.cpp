// T118: the game on a world whose terrain graph published settlement sites and trails (GeneratedWorld::Settlements, PathField; GB/Game/StartSites.h,
// docs GRAFO_TERRENO 14.6). Checked on the CPU, with no window:
//  * the camps of the starting clans take the sites IN RANK ORDER, one each, skipping a site too near a place already given;
//  * with more clans than sites the rest FALL BACK to the choice of before (ClanWorld::ChooseHome's regions), apart from the others;
//  * a camp on a site stands on its platform (CampGround = PlatformLevel), and the navigation sees a trail as trodden ground and FOLLOWS it;
//  * WITHOUT sites every camp, every ground height and every undergrowth is what it was: two runs on the same world give the same camps whether or
//    not they went through a world with sites before.
#include "GB/Game/Clans.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/StartSites.h"
#include "GB/Log.h"
#include "GB/Navigation/NavGrid.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
		else std::printf("ok: %s\n", what.c_str());
	}
	std::string At(const glm::vec2& p) { return "(" + std::to_string(static_cast<int>(p.x)) + ", " + std::to_string(static_cast<int>(p.y)) + ")"; }

	GB::SettlementSite Site(int rank, glm::vec2 at, float level)
	{
		GB::SettlementSite s;
		s.Rank = rank;
		s.Position = at;
		s.Radius = 30.0f;
		s.PlatformLevel = level;
		s.Ground = level;
		s.Score = 1.0f - 0.1f * static_cast<float>(rank);
		return s;
	}

	// ---- the order of the sites, without a world
	void Order()
	{
		using namespace GB;
		// published out of order on purpose: the Rank decides, not the place in the list
		const std::vector<SettlementSite> sites = { Site(2, { 300.0f, 0.0f }, 3.0f), Site(0, { 0.0f, 0.0f }, 1.0f), Site(1, { -300.0f, 0.0f }, 2.0f) };
		const std::vector<glm::vec2> two = GraphStartCamps(sites, 2);
		Check(two.size() == 2 && two[0] == glm::vec2(0.0f, 0.0f) && two[1] == glm::vec2(-300.0f, 0.0f), "two clans take the sites of Rank 0 and 1, in that order");
		const std::vector<glm::vec2> five = GraphStartCamps(sites, 5);
		Check(five.size() == 3 && five[2] == glm::vec2(300.0f, 0.0f), "five clans and three sites: the three sites, the rest is the host's (" + std::to_string(five.size()) + ")");
		const std::vector<glm::vec2> given = GraphStartCamps(sites, 2, { { 20.0f, 10.0f } }, 90.0f);
		Check(given.size() == 2 && given[0] == glm::vec2(-300.0f, 0.0f) && given[1] == glm::vec2(300.0f, 0.0f), "a place given by the player near site 0: the next sites are used, apart from it");
		Check(GraphStartCamps({}, 3).empty(), "no sites: no camps (the host's own choice)");
		Check(GraphSiteAt(sites, { 10.0f, 20.0f }) == &sites[1] && GraphSiteAt(sites, { 0.0f, 31.0f }) == nullptr, "the site whose platform holds a point");

		GeneratedWorld w;   // (no terrain: only the sites)
		w.Settlements = sites;
		const std::vector<CampSite> camps = GraphCampSites(w);
		Check(camps.size() == 3 && camps[0].At == sites[1].Position && camps[1].At == sites[2].Position && camps[2].At == sites[0].Position, "the clan world's list of sites is in Rank order");
		Check(!camps.empty() && camps[0].Why.find("site 0 of the terrain graph") != std::string::npos, "and says which site it is (" + (camps.empty() ? std::string() : camps[0].Why) + ")");

		ClanWorld none;
		CampSite out;
		Check(!none.ChooseHome(70.0f, nullptr, out), "a clan world with neither sites nor regions has no home to give (as before)");
		ClanWorld some;
		some.SetGraphSites(camps);
		Check(some.ChooseHome(70.0f, nullptr, out) && out.At == sites[1].Position, "with the sites, ChooseHome gives the best one");
		Check(some.ChooseHome(70.0f, [](const glm::vec2& p) { return p.x < -100.0f; }, out) && out.At == sites[2].Position, "the first one `usable` takes (a split's: near enough and free)");
		Check(!some.ChooseHome(70.0f, [](const glm::vec2&) { return false; }, out), "none usable and no regions: none");
	}

	// ---- the trails: trodden ground for the navigation
	void Trails()
	{
		using namespace GB;
		GeneratedWorld w;
		Check(TrailUndergrowth(w, 3.0f, 4.0f, 0.7f) == 0.7f, "no path field: the undergrowth given");
		// a field of 100 m with an L-shaped trail 4 m wide: from (-40, -40) north to (-40, 40), then east to (40, 40)
		w.PathField.Resize(201, 100.0f);
		for (uint32_t j = 0; j < 201; ++j)
			for (uint32_t i = 0; i < 201; ++i)
			{
				const glm::vec2 p = w.PathField.SamplePosition(i, j);
				const bool leg1 = std::fabs(p.x + 40.0f) <= 2.0f && p.y >= -42.0f && p.y <= 42.0f;
				const bool leg2 = std::fabs(p.y - 40.0f) <= 2.0f && p.x >= -42.0f && p.x <= 42.0f;
				w.PathField.At(i, j) = leg1 || leg2 ? 1.0f : 0.0f;
			}
		Check(TrailUndergrowth(w, -40.0f, 0.0f, 1.0f) == 0.0f && TrailUndergrowth(w, 0.0f, 0.0f, 1.0f) == 1.0f, "on the tread none, off it the forest's");
		CharacterWorld ground;
		ground.GroundHeight = [](float, float) { return 0.0f; };
		ground.BoundsMin = { -50.0f, -50.0f };
		ground.BoundsMax = { 50.0f, 50.0f };
		NavConfig config;
		const GeneratedWorld* world = &w;
		config.Vegetation = [world](float x, float z) { return TrailUndergrowth(*world, x, z, 1.0f); };   // a forest with a trail through it
		NavGrid grid;
		Check(grid.Build(ground, config), "a grid over the forest");
		NavPath path;
		const glm::vec2 from(-40.0f, -38.0f), to(38.0f, 40.0f);
		Check(grid.FindPath(NavProfile{}, from, to, path) && path.Found, "a path from one end of the trail to the other");
		float on = 0.0f, all = 0.0f;
		glm::vec2 last = from;
		for (const glm::vec2& p : path.Points)   // (metres of it, each segment by its middle: the waypoints are half a metre apart)
		{
			const float len = glm::distance(last, p);
			const glm::vec2 mid = 0.5f * (last + p);
			all += len;
			on += w.PathField.Sample(mid.x, mid.y) > 0.5f ? len : 0.0f;
			last = p;
		}
		char b[96];
		std::snprintf(b, sizeof b, "the walker follows the trail round the corner (%.0f%% of %.0f m on it)", 100.0 * on / std::max(all, 1.0f), static_cast<double>(all));
		Check(all > 120.0f && on >= 0.8f * all, b);
		NavGrid bare;
		NavConfig plain;
		plain.Vegetation = [](float, float) { return 1.0f; };
		NavPath straight;
		Check(bare.Build(ground, plain) && bare.FindPath(NavProfile{}, from, to, straight) && straight.Found, "without the trail the same forest");
		float length = 0.0f;
		last = from;
		for (const glm::vec2& p : straight.Points) { length += glm::distance(last, p); last = p; }
		Check(length < 0.8f * all, "goes straight across it instead (" + std::to_string(static_cast<int>(length)) + " m)");
	}

	std::vector<glm::vec2> Camps(const GB::SimRun& run)
	{
		std::vector<glm::vec2> out;
		if (run.Clans().Count() > 1) for (const auto& c : run.Clans().All()) out.push_back(c->Folk->Config.Economy.Camp);
		else out.push_back(run.Brain().Config.Economy.Camp);
		return out;
	}

	// ---- the starting clans of a run (GBSim's SimRun) on a world with and without sites
	void Runs()
	{
		using namespace GB;
		WorldDesc d;   // the editor's small world, at a coarser grid (quicker; the same rules)
		d.Type = WorldType::Temperate;
		d.Seed = 3;
		d.Resolution = 257;
		GeneratedWorld world = WorldGenerator::GenerateData(d);
		Check(world.Settlements.empty() && world.Trails.empty() && world.PathField.N == 0, "a world of the generator has no sites, trails nor path field");
		SimSetup setup;
		setup.World = &world;
		setup.Members = 9;
		setup.Clans = 3;
		std::vector<glm::vec2> before;
		std::vector<float> groundBefore;
		{
			SimRun run(setup);
			Check(run.Ok(), "three clans on the world without sites" + (run.Ok() ? std::string() : ": " + run.Error()));
			if (!run.Ok()) return;
			before = Camps(run);
			for (const auto& c : run.Clans().All()) groundBefore.push_back(c->Folk->Config.Economy.CampY);
			Check(run.Clans().GraphSites().empty(), "and its clan world has no graph sites");
		}
		Check(before.size() == 3, "three camps");
		if (before.size() != 3) return;
		bool terrain = true;
		for (size_t i = 0; i < before.size(); ++i) terrain = terrain && groundBefore[i] == world.TerrainRef->GetHeight(before[i].x, before[i].y);
		Check(terrain, "without sites each camp's ground is the terrain's height");

		// two sites, the best where the third clan was and the next where the second was (their platforms 0.5 m over the ground, so it shows)
		const float h2 = world.TerrainRef->GetHeight(before[2].x, before[2].y) + 0.5f, h1 = world.TerrainRef->GetHeight(before[1].x, before[1].y) + 0.5f;
		world.Settlements = { Site(1, before[1], h1), Site(0, before[2], h2) };
		std::vector<glm::vec2> with;
		{
			SimRun run(setup);
			Check(run.Ok(), "three clans on the world with two sites" + (run.Ok() ? std::string() : ": " + run.Error()));
			if (!run.Ok()) return;
			with = Camps(run);
			Check(with.size() == 3, "still three clans");
			if (with.size() != 3) return;
			Check(glm::distance(with[0], before[2]) < 4.0f, "the first clan settles on the site of Rank 0 " + At(before[2]) + ": " + At(with[0]));
			Check(glm::distance(with[1], before[1]) < 4.0f, "the second on the site of Rank 1 " + At(before[1]) + ": " + At(with[1]));
			const float apart = std::min(glm::distance(with[2], with[0]), glm::distance(with[2], with[1]));
			Check(!GraphSiteAt(world.Settlements, with[2]) && apart >= setup.ClanApart, "the third, with no site left, where the regions say, " + std::to_string(static_cast<int>(apart)) + " m from the others");
			const auto& all = run.Clans().All();
			Check(all.size() == 3 && all[0]->Folk->Config.Economy.CampY == h2 && all[1]->Folk->Config.Economy.CampY == h1, "a camp on a site stands on its platform's level");
			Check(all.size() == 3 && all[2]->Folk->Config.Economy.CampY == world.TerrainRef->GetHeight(with[2].x, with[2].y), "one off the sites on the terrain");
		}
		{
			setup.Clans = 1;
			setup.Members = 4;
			SimRun run(setup);
			Check(run.Ok() && glm::distance(run.Brain().Config.Economy.Camp, before[2]) < 4.0f, "a lone clan settles on the best site too");
			setup.Clans = 3;
			setup.Members = 9;
		}

		world.Settlements.clear();   // and back: the same camps as the first run, to the bit
		{
			SimRun run(setup);
			const std::vector<glm::vec2> again = Camps(run);
			bool same = run.Ok() && again.size() == before.size();
			for (size_t i = 0; same && i < again.size(); ++i) same = again[i] == before[i] && run.Clans().All()[i]->Folk->Config.Economy.CampY == groundBefore[i];
			Check(same, "without sites the camps and their ground are those of before, to the bit");
		}
	}
}

int GBTest_start_sites(int, char**)
{
	GB::Log::Init();
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	Order();
	Trails();
	Runs();
	if (failures) { std::fprintf(stderr, "start_sites: %d failures\n", failures); return 1; }
	std::puts("start_sites: ok");
	return 0;
}
