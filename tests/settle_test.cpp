// Settlement sites, trails and their levelling (GB/World/Settle.h and the nodes settlement_sites, route_network and path_grade), headless and
// on GB_WorldGen alone. Synthetic maps where the answer is known: a flat terrace by a lake among rolling hills is the first site; two sites
// across a river that can be waded at one place only are joined through it; the trail over a corrugated slope keeps its grade after levelling,
// with light earthwork and no step steeper than its talus; and the levelled ground has no pit. The choice is the same every time, through the
// graph and its cache, and an unknown Revision is refused.
#include "GB/World/Drainage.h"
#include "GB/World/FlowAccumulation.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/TerrainGraphCache.h"
#include "GB/World/Graph/TgHash.h"
#include "GB/World/Settle.h"
#include "GB/World/Water/WaterRaster.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	float Smooth01(float a, float b, float x)
	{
		const float t = std::fmin(1.0f, std::fmax(0.0f, (x - a) / (b - a)));
		return t * t * (3.0f - 2.0f * t);
	}

	// 600 m, 2 m cells. Rolling hills 8 m high every ~125 m (too steep and rough for a village), a lake of 60 m at (0, -200), and a flat
	// terrace of 45 m at (0, -90): 50 m from the shore, 5 m above the water.
	TgField SiteMap()
	{
		TgField f;
		f.Resize(301, 600.0f);
		for (uint32_t j = 0; j < f.N; ++j)
			for (uint32_t i = 0; i < f.N; ++i)
			{
				const glm::vec2 p = f.SamplePosition(i, j);
				float h = 9.0f + 8.0f * std::sin(p.x / 20.0f) * std::cos(p.y / 17.0f);
				const float terrace = 1.0f - Smooth01(45.0f, 70.0f, glm::distance(p, glm::vec2(0.0f, -90.0f)));
				h = h + (5.0f - h) * terrace;
				const float lake = glm::distance(p, glm::vec2(0.0f, -200.0f));
				if (lake < 90.0f) h = std::fmin(h, 1.0f + 4.0f * Smooth01(60.0f, 90.0f, lake));
				if (lake < 60.0f) h = -3.0f;
				f.At(i, j) = h;
			}
		return f;
	}

	WaterField SiteWater()
	{
		WaterField w;
		LakeBody lake;
		lake.Present = true;
		lake.Center = { 0.0f, -200.0f };
		lake.Radius = 60.0f;
		lake.Level = 0.0f;
		w.SetLake(lake);
		return w;
	}

	// 400 m, 2 m cells. A river along z = 0 (level 0.5, half-width 4) 1.6 m deep except a shallow reach (0.5 m) at x = 60; banks rising 12 % to
	// both sides, corrugated across x (1.5 m every 44 m: a 21 % grade straight across) away from the river.
	constexpr float kFordX = 60.0f;
	TgField RiverMap()
	{
		TgField f;
		f.Resize(201, 400.0f);
		for (uint32_t j = 0; j < f.N; ++j)
			for (uint32_t i = 0; i < f.N; ++i)
			{
				const glm::vec2 p = f.SamplePosition(i, j);
				const float az = std::fabs(p.y);
				float h = 1.0f + 0.12f * az + 1.5f * std::sin(p.x / 7.0f) * Smooth01(10.0f, 30.0f, az);
				if (az <= 6.0f) h = 0.5f - (std::fabs(p.x - kFordX) < 10.0f ? 0.5f : 1.6f);
				f.At(i, j) = h;
			}
		return f;
	}

	WaterField RiverWater()
	{
		RiverData r;
		for (int x = -200; x <= 200; x += 10) { r.Points.push_back({ static_cast<float>(x), 0.5f, 0.0f }); r.Speed.push_back(0.5f); }
		r.HalfWidth = 4.0f;
		r.Depth = 1.6f;
		WaterField w;
		w.SetRiver(r);
		return w;
	}

	bool SameSites(const std::vector<SettlementSite>& a, const std::vector<SettlementSite>& b)
	{
		if (a.size() != b.size()) return false;
		for (size_t k = 0; k < a.size(); ++k) if (a[k].Position != b[k].Position || a[k].Score != b[k].Score) return false;
		return true;
	}
}

int GBTest_settle(int, char**)
{
	// ---------------------------------------------------------------- sites
	{
		const TgField map = SiteMap();
		const WaterField water = SiteWater();
		SiteParams p;
		p.Separation = 120.0f;
		p.Edge = 30.0f;
		p.Count = 4;
		const SiteResult a = FindSettlementSites(map, &water, nullptr, p), b = FindSettlementSites(map, &water, nullptr, p);
		Check(!a.Sites.empty(), "the terrace is a site");
		if (!a.Sites.empty())
		{
			const SettlementSite& s = a.Sites.front();
			Check(glm::distance(s.Position, glm::vec2(0.0f, -90.0f)) <= 15.0f, "the terrace comes first (at " + std::to_string(s.Position.x) + ", " + std::to_string(s.Position.y) + ")");
			Check(s.WaterDistance >= p.WaterMin && s.WaterDistance <= p.WaterMax, "S1: the water at " + std::to_string(s.WaterDistance) + " m");
			Check(s.CampSlopeMax <= p.CampMaxSlope && s.SteepShare <= p.SteepShareMax, "S2 and X3 on the first site");
			Check(s.AboveWater >= p.FloodHeight, "X4: above the water");
			Check(s.Rank == 0, "the first is rank 0");
		}
		for (size_t i = 0; i < a.Sites.size(); ++i)
			for (size_t j = i + 1; j < a.Sites.size(); ++j)
				Check(glm::distance(a.Sites[i].Position, a.Sites[j].Position) >= p.Separation, "the sites are Separation apart");
		for (size_t i = 1; i < a.Sites.size(); ++i) Check(a.Sites[i].Score <= a.Sites[i - 1].Score && a.Sites[i].Rank == static_cast<int>(i), "best first");
		Check(SameSites(a.Sites, b.Sites), "the same sites twice");
		// the codec gives them back as they were
		std::vector<SettlementSite> back;
		Check(DecodeSites(EncodeSites(a.Sites), back) && SameSites(a.Sites, back), "sites survive their encoding");
		for (const TgPath& path : EncodeSites(a.Sites).Items) Check(path.Points.size() == path.Values.size(), "one value per point in a site's record");
		// on the rolling hills alone there is no site
		SiteParams strict = p;
		strict.WaterMin = 60.0f;
		const SiteResult far = FindSettlementSites(map, &water, nullptr, strict);
		Check(std::all_of(far.Sites.begin(), far.Sites.end(), [](const SettlementSite& s) { return s.WaterDistance >= 60.0f; }), "WaterMin is kept");
	}

	// ---------------------------------------------------------------- the trail goes by the ford
	const TgField river = RiverMap();
	const WaterField riverWater = RiverWater();
	std::vector<SettlementSite> two(2);
	two[0].Position = { -100.0f, 80.0f };
	two[1].Position = { -100.0f, -80.0f };
	for (size_t k = 0; k < 2; ++k) { two[k].Radius = 20.0f; two[k].Rank = static_cast<int>(k); two[k].Ground = two[k].PlatformLevel = river.Sample(two[k].Position.x, two[k].Position.y); }
	RouteParams rp;
	rp.Cell = 4.0f;
	rp.ShapeFords = 0;
	rp.WaterSpurMax = 0.0f;
	rp.MaxCols = 0;
	const RouteResult routes = PlanRoutes(river, &riverWater, two, rp);
	Check(routes.Components == 1, "C1: the two sites are joined");
	Check(routes.Fords.size() == 1 && !routes.Fords.empty() && std::fabs(routes.Fords[0].Position.x - kFordX) <= 12.0f && routes.Fords[0].Depth <= 1.0f,
		"one natural ford, at the shallow reach");
	const Trail* link = nullptr;
	for (const Trail& t : routes.Trails) if (t.Kind == TrailKind::Link) link = &t;
	Check(link != nullptr, "a link between the sites");
	if (link)
	{
		bool crosses = false, wrong = false;
		for (const glm::vec3& q : link->Points)
			if (std::fabs(q.z) < 6.0f) { crosses = true; if (std::fabs(q.x - kFordX) > 15.0f) wrong = true; }
		Check(crosses && !wrong, "the trail crosses the river at the ford and nowhere else");
		Check(link->Fords == 1, "it counts the ford it crosses");
		Check(link->Length / std::max(link->Straight, 1.0f) < 3.0f, "the detour to the ford, and no more (" + std::to_string(link->Length / std::max(link->Straight, 1.0f)) + "; by the ford, 2.24)");
	}
	{
		std::vector<Trail> back;
		TgPaths g, info;
		EncodeTrails(routes.Trails, g, info);
		Check(DecodeTrails(g, info, back) && back.size() == routes.Trails.size(), "trails survive their encoding");
	}
	// with the shallow reach as deep as the rest, no natural ford: ShapeFords 0 leaves the sites apart, 1 builds one
	{
		TgField deep = river;
		for (uint32_t j = 0; j < deep.N; ++j)
			for (uint32_t i = 0; i < deep.N; ++i)
				if (std::fabs(deep.SamplePosition(i, j).y) <= 6.0f) deep.At(i, j) = 0.5f - 1.6f;
		Check(PlanRoutes(deep, &riverWater, two, rp).Components == 2, "no ford: the river parts them");
		RouteParams shape = rp;
		shape.ShapeFords = 1;
		const RouteResult built = PlanRoutes(deep, &riverWater, two, shape);
		Check(built.Components == 1 && !built.Fords.empty() && built.Fords[0].Built, "ShapeFords: a ford to build, and they are joined");
		const GradeResult graded = GradePaths(deep, &riverWater, two, built.Trails, built.Fords, GradeParams{});
		bool raised = false;
		for (const Ford& f : graded.Fords) raised = raised || f.Built;
		Check(raised, "path_grade builds the ford it uses");
		const TgField& bed = deep;
		float shallowest = 1e9f;
		const WaterRaster wet = RasterizeWater(riverWater, bed.N, bed.Min(), bed.Spacing());
		for (size_t c = 0; c < bed.Values.size(); ++c)
			if (wet.Kinds[c] == WaterRaster::River && graded.Path[c] > 0.5f) shallowest = std::fmin(shallowest, wet.Level[c] - graded.Height[c]);
		Check(shallowest <= GradeParams{}.FordDepth + 1e-3f, "the built ford is wadeable on the tread");
	}

	// ---------------------------------------------------------------- levelling
	{
		GradeParams gp;
		const GradeResult g = GradePaths(river, &riverWater, two, routes.Trails, routes.Fords, gp);
		const uint32_t n = river.N;
		const float sp = river.Spacing();
		const WaterRaster wet = RasterizeWater(riverWater, n, river.Min(), sp);
		for (const Trail& t : g.Trails)
		{
			if (t.Kind != TrailKind::Link) continue;
			Check(t.WithinGrade >= 0.95f, "the grade is held on 95 % of the trail (" + std::to_string(t.WithinGrade) + ")");
			Check(t.Cut + t.Fill > 0.0f, "the corrugations were levelled");
			Check(t.Cut + t.Fill < t.Length * (gp.Width + 2.0f * gp.MaxCut / gp.Talus) * gp.MaxCut, "the earthwork is light (" + std::to_string(t.Cut + t.Fill) + " m3)");
			float before = 0.0f, ignored = 0.0f;
			MeasureTrailGrade(river, t, gp.MaxGrade, 9.0f, before, ignored);
			Check(t.WithinGrade > before || before >= 0.99f, "levelling made it better than the ground was (" + std::to_string(before) + " -> " + std::to_string(t.WithinGrade) + ")");
		}
		for (const SettlementSite& s : g.Sites) Check(s.Cut + s.Fill > 0.0f && s.Cut + s.Fill < 3.14159f * s.Radius * s.Radius * 2.0f, "a platform with light earthwork");
		// no cell steeper than the talus where the ground was changed (outside the tread and the platform), and no change in the water
		size_t changed = 0, steep = 0, wetChanged = 0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const size_t c = static_cast<size_t>(j) * n + i;
				if (wet.Wet(c)) { wetChanged += g.Height[c] != river.Values[c]; continue; }
				if (std::fabs(g.Height[c] - river.Values[c]) < 0.05f || g.Path[c] > 0.5f || g.Platform[c] > 0.5f) continue;
				++changed;
				const float gx = (g.Height[c + 1] - g.Height[c - 1]) / (2.0f * sp), gz = (g.Height[c + n] - g.Height[c - n]) / (2.0f * sp);
				steep += std::atan(std::sqrt(gx * gx + gz * gz)) * 57.2958f > 30.0f;
			}
		Check(changed > 0 && steep * 100 <= changed, "the talus is 30 degrees or less (" + std::to_string(steep) + " of " + std::to_string(changed) + ")");
		Check(wetChanged == 0, "the water's cells are not touched (no ford to build here)");
		// no pit: every dry cell inside the border has a lower neighbour (what the probe counts as draining into a pit); the map had none
		const std::vector<int> down0 = FlowAccumulation::Directions(river.Values.data(), n);
		size_t pits0 = 0;
		for (size_t c = 0; c < down0.size(); ++c) { const uint32_t i = static_cast<uint32_t>(c % n), j = static_cast<uint32_t>(c / n); if (i && j && i + 1 < n && j + 1 < n && !wet.Wet(c) && down0[c] < 0) ++pits0; }
		Check(pits0 == 0, "the river map has no pit of its own (" + std::to_string(pits0) + ")");
		const std::vector<int> down = FlowAccumulation::Directions(g.Height.data(), n);
		size_t pits = 0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const size_t c = static_cast<size_t>(j) * n + i;
				if (!wet.Wet(c) && down[c] < 0) ++pits;
			}
		Check(pits == 0, "no pit after levelling (" + std::to_string(pits) + ")");
		Check(g.DrainRaise < 0.5f, "the final fill is a trim, not a dam (" + std::to_string(g.DrainRaise) + " m at " + std::to_string(g.DrainRaiseAt.x) + ", " + std::to_string(g.DrainRaiseAt.y) + ")");
	}

	// ---------------------------------------------------------------- the nodes, through a graph
	{
		TerrainGraph g;
		g.Id = "settle";
		auto node = [&](const char* id, const char* type, std::map<std::string, double> params = {}, const char* source = "")
		{
			TgNode nd; nd.Id = id; nd.Type = type; nd.Label = id; nd.Source = source; nd.Params = std::move(params);
			g.Nodes.push_back(nd);
		};
		node("ground", "height_input", {}, "height");
		node("sites", "settlement_sites", { { "WaterMin", 0.0 }, { "WaterMax", 2e9 }, { "Edge", 30.0 }, { "Separation", 120.0 }, { "Count", 3.0 } });
		node("routes", "route_network", { { "Cell", 4.0 } });
		node("grade", "path_grade");
		node("out_h", "output", {}, "height");
		node("out_s", "output", {}, "settlements");
		node("out_t", "output", {}, "trails");
		node("out_i", "output", {}, "trail_info");
		node("out_p", "output", {}, "path");
		g.Edges = { { "ground", "sites", "height", "out" }, { "ground", "routes", "height", "out" }, { "sites", "routes", "sites", "sites" },
			{ "ground", "grade", "height", "out" }, { "sites", "grade", "sites", "sites" }, { "routes", "grade", "trails", "trails" },
			{ "routes", "grade", "trail_info", "trail_info" }, { "routes", "grade", "fords", "fords" },
			{ "grade", "out_h", "in", "height" }, { "grade", "out_s", "in", "sites" }, { "grade", "out_t", "in", "trails" },
			{ "grade", "out_i", "in", "trail_info" }, { "grade", "out_p", "in", "path" } };
		const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
		Check(!HasTgErrors(ValidateTerrainGraph(g, registry)), "settlement_sites -> route_network -> path_grade is a valid graph");
		const TgField map = SiteMap();
		std::map<std::string, TgField> inputs{ { "height", map } };
		TgContext ctx;
		ctx.Resolution = map.N; ctx.Size = map.Size; ctx.Inputs = &inputs;
		TgResult r1, r2, r3, warm;
		std::string error;
		Check(EvaluateTerrainGraph(g, registry, ctx, r1, error), "it evaluates (" + error + ")");
		Check(EvaluateTerrainGraph(g, registry, ctx, r2, error) && TgHashResult(r1) == TgHashResult(r2), "the same twice");
		TerrainGraphCache cache;
		EvaluateTerrainGraph(g, registry, ctx, warm, error, &cache);
		Check(EvaluateTerrainGraph(g, registry, ctx, r3, error, &cache) && TgHashResult(r3) == TgHashResult(r1), "the same through the cache");
		std::vector<SettlementSite> sites;
		std::vector<Trail> trails;
		Check(r1.PathOutputs.count("settlements") && DecodeSites(r1.PathOutputs["settlements"], sites) && sites.size() >= 2, "the graph publishes the sites");
		Check(r1.PathOutputs.count("trails") && DecodeTrails(r1.PathOutputs["trails"], r1.PathOutputs["trail_info"], trails) && !trails.empty(), "and the trails");
		Check(r1.Outputs.count("path") && r1.Outputs["path"].N == map.N, "and the path mask");
		for (const char* id : { "sites", "routes", "grade" })
		{
			TerrainGraph bad = g;
			for (TgNode& nd : bad.Nodes) if (nd.Id == std::string(id)) nd.Params["Revision"] = 2;
			TgResult r;
			Check(!EvaluateTerrainGraph(bad, registry, ctx, r, error) && error.find("Revision") != std::string::npos, std::string("an unknown Revision of ") + id + " is refused");
		}
	}

	if (failures) std::fprintf(stderr, "settle: %d failures\n", failures);
	else std::printf("settle: ok\n");
	return failures ? 1 : 0;
}
