// TERRENO_V5 E1: the drainage primitive (GB/World/Drainage.h) and the nodes drainage and lake_fill (Nodes/DrainageNodes.h), headless and on
// GB_WorldGen alone. A synthetic map with known basins: two hollows that merge under one level, a second lake behind its own saddle, a pit too
// small to be a lake; the levels are the saddles' heights exactly, the receivers point down and back in the order, the area adds up, the
// filled ground has no pit outside the lakes, and it is the same every time (and through the cache). Then the same on a noise terrain.
#include "GB/World/Drainage.h"
#include "GB/World/FlowAccumulation.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/TerrainGraphCache.h"
#include "GB/World/Graph/TgHash.h"
#include "GB/World/Water/WaterPresets.h"
#include "GB/World/Water/WaterRaster.h"
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

	constexpr uint32_t kN = 49;   // 1 m per cell: Size 48

	// Low land at 1 m. Basin A: a rim at 8 m around [6, 22] x [6, 30], a saddle at 5 m at (14, 6), a floor at 3 m with two pits (0 and 1 m)
	// split by an inner ridge at 4 m (below the saddle: one lake). Basin B: a rim at 9 m around [28, 42] x [6, 30], a saddle at 7 m at
	// (35, 30), a floor at 2 m. A pit 0.5 m deep at (10, 40): too shallow for a lake.
	TgField SyntheticMap()
	{
		TgField f;
		f.Resize(kN, 48.0f);
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i)
			{
				float h = 1.0f;
				auto rim = [&](uint32_t i0, uint32_t i1, uint32_t j0, uint32_t j1) { return (i == i0 || i == i1) && j >= j0 && j <= j1 ? true : (j == j0 || j == j1) && i >= i0 && i <= i1; };
				auto inside = [&](uint32_t i0, uint32_t i1, uint32_t j0, uint32_t j1) { return i > i0 && i < i1 && j > j0 && j < j1; };
				if (rim(6, 22, 6, 30)) h = 8.0f;
				else if (inside(6, 22, 6, 30))
				{
					h = 3.0f;
					if (j == 18) h = 4.0f;                    // the inner ridge
					if (i == 14 && j == 12) h = 0.0f;         // pit 1
					if (i == 14 && j == 24) h = 1.0f;         // pit 2
				}
				if (rim(28, 42, 6, 30)) h = 9.0f;
				else if (inside(28, 42, 6, 30)) h = 2.0f;
				if (i == 14 && j == 6) h = 5.0f;              // saddle A
				if (i == 35 && j == 30) h = 7.0f;             // saddle B
				if (i == 10 && j == 40) h = 0.5f;             // the small pit
				f.At(i, j) = h;
			}
		return f;
	}
}

int GBTest_drainage(int, char**)
{
	const TgField map = SyntheticMap();
	const size_t count = map.Values.size();

	// ---------------------------------------------------------------- the primitive
	{
		const DrainageResult d = ComputeDrainage(map.Values.data(), kN, map.Spacing());
		Check(d.N == kN && d.Order.size() == count, "every cell is drained once");
		Check(d.Level[map.N * 12 + 14] == 5.0f && d.Level[map.N * 24 + 14] == 5.0f && d.Level[map.N * 18 + 14] == 5.0f,
			"basin A: both pits and the ridge between them stand at the saddle's 5 m, exactly");
		Check(d.Level[map.N * 18 + 35] == 7.0f, "basin B stands at its saddle's 7 m, exactly");
		Check(d.Level[map.N * 40 + 10] == 1.0f, "the small pit fills to the ground around it");
		Check(d.Level[map.N * 3 + 3] == 1.0f && d.Level[map.N * 6 + 6] == 8.0f, "where nothing pools the level is the ground");

		std::vector<uint32_t> rank(count);
		for (size_t k = 0; k < count; ++k) rank[d.Order[k]] = static_cast<uint32_t>(k);
		bool down = true, before = true, neighbour = true, borderOut = true;
		for (size_t c = 0; c < count; ++c)
		{
			const int i = static_cast<int>(c % kN), j = static_cast<int>(c / kN);
			const bool border = i == 0 || j == 0 || i == static_cast<int>(kN) - 1 || j == static_cast<int>(kN) - 1;
			if (border) { borderOut = borderOut && d.Receiver[c] == -1; continue; }
			const int32_t r = d.Receiver[c];
			if (r < 0) { neighbour = false; continue; }
			const int ri = r % static_cast<int>(kN), rj = r / static_cast<int>(kN);
			neighbour = neighbour && std::abs(ri - i) <= 1 && std::abs(rj - j) <= 1 && r != static_cast<int32_t>(c);
			down = down && d.Filled[static_cast<size_t>(r)] < d.Filled[c];
			before = before && rank[static_cast<size_t>(r)] < rank[c];
			Check(d.Filled[c] >= map.Values[c], "the filled ground is never below the ground");
		}
		Check(borderOut, "the border is the outlet");
		Check(neighbour, "every other cell drains to one of its neighbours");
		Check(down, "and to a strictly lower one (filled)");
		Check(before, "which comes before it in the order");
		double out = 0.0;
		for (size_t c = 0; c < count; ++c) if (d.Receiver[c] < 0) out += d.Area[c];
		Check(std::fabs(out - static_cast<double>(count)) < 1e-3, "the area that leaves the map is the map's (" + std::to_string(out) + " m2)");

		const DrainageResult again = ComputeDrainage(map.Values.data(), kN, map.Spacing());
		Check(again.Level == d.Level && again.Filled == d.Filled && again.Receiver == d.Receiver && again.Order == d.Order && again.Area == d.Area,
			"the same drainage twice");
		DrainageOptions parentMode;
		parentMode.ReceiverMode = 0;
		const DrainageResult parent = ComputeDrainage(map.Values.data(), kN, map.Spacing(), parentMode);
		Check(parent.Level == d.Level && parent.Order == d.Order, "the receiver rule does not change the levels or the order");
		std::vector<float> rain(count, 2.0f);
		DrainageOptions wet;
		wet.Precipitation = rain.data();
		const DrainageResult rained = ComputeDrainage(map.Values.data(), kN, map.Spacing(), wet);
		Check(rained.Area[map.N * 12 + 14] == 2.0f * d.Area[map.N * 12 + 14], "twice the rain, twice the area");
	}

	// ---------------------------------------------------------------- the nodes
	TerrainGraph g;
	g.Id = "e1";
	auto node = [&](const char* id, const char* type, std::map<std::string, double> params = {}, const char* source = "")
	{
		TgNode n; n.Id = id; n.Type = type; n.Label = id; n.Source = source; n.Params = std::move(params);
		g.Nodes.push_back(n);
	};
	node("ground", "height_input", {}, "height");
	node("drain", "drainage");
	node("lakes", "lake_fill", { { "MinDepth", 1.0 }, { "MinArea", 20.0 } });
	node("out_h", "output", {}, "height");
	node("out_w", "output", {}, "water");
	g.Edges = { { "ground", "drain", "height", "out" }, { "ground", "lakes", "height", "out" }, { "drain", "lakes", "level", "level" },
		{ "drain", "lakes", "receiver", "receiver" }, { "drain", "lakes", "order", "order" }, { "lakes", "out_h", "in", "height" },
		{ "lakes", "out_w", "in", "water" } };
	const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
	Check(!HasTgErrors(ValidateTerrainGraph(g, registry)), "drainage -> lake_fill is a valid graph");
	std::map<std::string, TgField> inputs{ { "height", map } };
	TgContext ctx;
	ctx.Resolution = kN; ctx.Size = 48.0f; ctx.Inputs = &inputs;
	TgResult r1, r2, r3;
	std::string error;
	Check(EvaluateTerrainGraph(g, registry, ctx, r1, error), "it evaluates (" + error + ")");
	Check(EvaluateTerrainGraph(g, registry, ctx, r2, error) && TgHashResult(r1) == TgHashResult(r2), "the same twice");
	TerrainGraphCache cache;
	TgResult warm;
	EvaluateTerrainGraph(g, registry, ctx, warm, error, &cache);
	Check(EvaluateTerrainGraph(g, registry, ctx, r3, error, &cache) && TgHashResult(r3) == TgHashResult(r1), "the same through the cache");
	if (r1.WaterOutputs.count("water") && r1.Outputs.count("height"))
	{
		const WaterField& w = r1.WaterOutputs.at("water");
		const TgField& h = r1.Outputs.at("height");
		Check(!w.HasLake(), "lake_fill adds lakes without making one the main lake");
		Check(w.MoreLakes().size() == 2, "two lakes: basin A (its two pits merged) and basin B, not the shallow pit (" + std::to_string(w.MoreLakes().size()) + ")");
		if (w.MoreLakes().size() == 2)
		{
			const LakeBody& a = w.MoreLakes()[0];
			const LakeBody& b = w.MoreLakes()[1];
			Check(a.Level == 5.0f && b.Level == 7.0f, "the larger is A at 5 m, then B at 7 m (" + std::to_string(a.Level) + ", " + std::to_string(b.Level) + ")");
			Check(a.Covers(map.SamplePosition(14, 12)) && a.Covers(map.SamplePosition(14, 24)) && a.Covers(map.SamplePosition(14, 18)), "A covers both pits and the ridge under it");
			Check(!a.Covers(map.SamplePosition(6, 12)) && !a.Covers(map.SamplePosition(35, 18)), "A does not cover its rim nor B");
			Check(glm::distance(a.Outlet, map.SamplePosition(14, 6)) < 0.01f && glm::distance(b.Outlet, map.SamplePosition(35, 30)) < 0.01f, "each spills over its saddle");
			Check(std::fabs(a.Area - 15.0f * 23.0f) < 0.01f && a.MaxDepth == 5.0f, "A is 15 x 23 m and 5 m deep at its deepest pit");
			Check(w.LakeLevelAt(map.SamplePosition(35, 18).x, map.SamplePosition(35, 18).y) == 7.0f && w.LakeLevelAt(1.0f - 24.0f, 1.0f - 24.0f) == kNoWaterLevel, "the level at a point is its lake's");
			Check(w.Lakes().size() == 2, "Lakes() lists the two");
			const auto surfaces = MakeMoreLakeSurfaces(w);
			Check(surfaces.size() == 2 && surfaces[1]->Position.y == 7.0f && surfaces[1]->Radius == b.Radius && !surfaces[1]->IsRiver(),
				"one flat lake surface per lake, at its level, on the circle that holds it");
		}
		Check(h.At(14, 12) == 0.0f && h.At(35, 18) == 2.0f, "a lake keeps its ground under the water");
		Check(h.At(10, 40) > 1.0f, "the small pit is filled above the ground around it");
		// no pit outside the water: D8 on the filled ground, the lakes and the border the only places water stops
		const WaterRaster wet = RasterizeWater(w, h.N, h.Min(), h.Spacing());
		const std::vector<int> d8 = FlowAccumulation::Directions(h.Values.data(), h.N);
		size_t pits = 0;
		for (size_t c = 0; c < count; ++c)
		{
			const size_t i = c % kN, j = c / kN;
			if (i == 0 || j == 0 || i == kN - 1 || j == kN - 1 || wet.Wet(c)) continue;
			pits += d8[c] < 0;
		}
		Check(pits == 0, "no dry cell of the filled ground is a pit (" + std::to_string(pits) + ")");
		const TgField& lift = r1.Nodes.at("lakes").at("raised").Field;
		// (the low land is exactly flat: it gets the epsilon slope, a millimetre a cell, back to the outlet the flood reached it from first; the
		// map is 48 cells across)
		std::printf("drainage: raised %.4f m in the pit, %.4f m on the flat at (3, 3)\n", lift.At(10, 40), lift.At(3, 3));
		Check(lift.At(10, 40) > 0.5f && lift.At(10, 40) < 0.6f && lift.At(3, 3) < 0.1f && lift.At(14, 12) == 0.0f && lift.At(6, 12) == 0.0f,
			"raised says what the filling added (0.5 m in the pit, millimetres on the flat, none in a lake or on a rim)");
	}

	// ---------------------------------------------------------------- Revision
	{
		TerrainGraph bad = g;
		for (TgNode& n : bad.Nodes) if (n.Id == "lakes") n.Params["Revision"] = 2;
		TgResult r;
		Check(!EvaluateTerrainGraph(bad, registry, ctx, r, error) && error.find("Revision") != std::string::npos, "an unknown Revision is refused, not guessed");
	}

	// ---------------------------------------------------------------- a noise terrain: lakes where it pools, no pit anywhere else
	{
		TerrainGraph noise = g;
		noise.Nodes[0] = TgNode{ "ground", "heightfield_noise", "ground", 0, 0, "", { { "Frequency", 0.02 }, { "Octaves", 5 }, { "Amplitude", 20 } } };
		for (TgEdge& e : noise.Edges) if (e.From == "ground") e.FromPort = "height";
		TgContext nctx;
		nctx.Seed = 77; nctx.Resolution = 129; nctx.Size = 256.0f;
		TgResult a, b;
		Check(EvaluateTerrainGraph(noise, registry, nctx, a, error), "noise -> drainage -> lake_fill evaluates (" + error + ")");
		Check(EvaluateTerrainGraph(noise, registry, nctx, b, error) && TgHashResult(a) == TgHashResult(b), "noise: the same twice");
		if (a.WaterOutputs.count("water") && a.Outputs.count("height"))
		{
			const WaterField& w = a.WaterOutputs.at("water");
			const TgField& h = a.Outputs.at("height");
			const WaterRaster wet = RasterizeWater(w, h.N, h.Min(), h.Spacing());
			const std::vector<int> d8 = FlowAccumulation::Directions(h.Values.data(), h.N);
			size_t pits = 0, lakeCells = 0;
			for (size_t c = 0; c < h.Values.size(); ++c)
			{
				const size_t i = c % h.N, j = c / h.N;
				lakeCells += wet.Kinds[c] == WaterRaster::Lake;
				if (i == 0 || j == 0 || i == h.N - 1 || j == h.N - 1 || wet.Wet(c)) continue;
				pits += d8[c] < 0;
			}
			Check(pits == 0, "noise: no pit outside the lakes (" + std::to_string(pits) + ")");
			std::printf("drainage: noise 129 x 129, %zu lakes over %zu cells\n", w.MoreLakes().size(), lakeCells);
			for (const LakeBody& lake : w.MoreLakes())
				Check(lake.MaxDepth >= 1.0f && lake.Area >= 20.0f, "noise: every lake is as deep and as large as asked");
		}
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("drainage: all checks passed\n");
	return failures ? 1 : 0;
}
