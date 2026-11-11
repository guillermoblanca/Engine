// TERRENO_V5 E3: the deposit (GB/World/Deposit.h) and the node deposit (Nodes/DepositNode.h), headless and on GB_WorldGen alone.
//   - a synthetic sierra front: parallel V gullies with walls at 31 degrees falling 8.5 degrees to the south, then a plain of 1.7 degrees with
//     a closed hollow (a lake to be) in it; the west gullies hard rock. The fill raises the soft gullies' floors and eases their walls, does
//     not touch the hard ones nor the crests; a fan lies at the mouth of every gully, at the fan's angle, holding about the volume asked;
//   - no new hollows (the level of the water over the ground grows nowhere) and no cell without a lower neighbour outside the hollow;
//   - the hollow, the cells beside it and the border do not move, and the lake's level is the same;
//   - the same twice, bit for bit; with the fill, the fans and the easing off, the input as it was;
//   - the easing (on) only cuts, and only where the slope is long;
//   - the node: evaluates after a talus, the same twice and through the cache, and an unknown Revision is refused.
#include "GB/World/Deposit.h"
#include "GB/World/Drainage.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/TerrainGraphCache.h"
#include "GB/World/Graph/TgHash.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	constexpr uint32_t kN = 257;
	constexpr float kSp = 2.0f;
	constexpr uint32_t kFront = 160;   // the mountain is j < kFront, the plain after it

	size_t At(uint32_t i, uint32_t j) { return static_cast<size_t>(j) * kN + i; }

	float SlopeDeg(const std::vector<float>& h, uint32_t i, uint32_t j)
	{
		const float gx = (h[At(i + 1, j)] - h[At(i - 1, j)]) / (2.0f * kSp), gz = (h[At(i, j + 1)] - h[At(i, j - 1)]) / (2.0f * kSp);
		return std::atan(std::sqrt(gx * gx + gz * gz)) * 57.2957795f;
	}
}

int GBTest_deposit(int, char**)
{
	// ---------------------------------------------------------------- the sierra front
	// gullies every 20 cells (40 m), their axes at i = 10, 30, ..., the crests at i = 0, 20, ...: walls of 0.6 (31 degrees), 12 m high; the
	// mountain falls 0.15 per metre to the south (8.5 degrees), the plain 0.03 (1.7 degrees), and the gullies fade over the first 10 cells
	// of the plain. A bowl 3 m deep and 12 cells across in the plain is a closed hollow. Hard rock: i < 60.
	const size_t count = static_cast<size_t>(kN) * kN;
	std::vector<float> h(count), hard(count, 0.0f);
	for (uint32_t j = 0; j < kN; ++j)
		for (uint32_t i = 0; i < kN; ++i)
		{
			const float z = static_cast<float>(j) * kSp;
			const float front = static_cast<float>(kFront) * kSp;
			float base = z < front ? 120.0f - 0.15f * z : 120.0f - 0.15f * front - 0.03f * (z - front);
			const float axis = std::fabs(static_cast<float>(i % 20) - 10.0f) * kSp;   // metres from the crest... to 20 at the axis
			const float wall = 0.6f * (20.0f - axis);                                   // 0 at the crest, 12 at the axis
			const float fade = std::clamp((front + 20.0f - z) / 20.0f, 0.0f, 1.0f);
			base += 12.0f * fade - wall * fade;                                         // the crests 12 m over the gully floor
			const float di = static_cast<float>(i) - 200.0f, dj = static_cast<float>(j) - 220.0f, r2 = di * di + dj * dj;
			if (r2 < 144.0f) base -= 3.0f * (1.0f - r2 / 144.0f);
			h[At(i, j)] = base;
			hard[At(i, j)] = i < 60 ? 1.0f : 0.0f;
		}
	DepositOptions o;
	o.Hardness = hard.data();
	o.Fill = 0.5f;
	o.FillRadius = 30.0f;
	o.FanArea = 8000.0f;
	o.FanAngle = 5.0f;
	o.FanApexAngle = 7.0f;
	o.FanReach = 30.0f;
	o.FanDepth = 1.0f;
	const DepositResult a = DepositSediment(h.data(), kN, kSp, o);
	const DepositStats& s = a.Stats;
	std::printf("deposit: fill %.0f m3 on %zu cells, %zu apexes asking %.0f m3, fans %.0f m3 on %zu cells (factor %.2f, %d passes), drainage took %.0f m3, %zu cells held\n",
		s.FillVolume, s.FillCells, s.Apexes, s.FanTarget, s.FanVolume, s.FanCells, s.ApexFactor, s.FitPasses, s.DrainVolume, s.HeldCells);

	// the fill: the soft gullies' floors up, their walls gentler, the hard ones and the crests as they were
	{
		float softRaise = 0.0f, hardMoved = 0.0f, crestMoved = 0.0f, wallBefore = 0.0f, wallAfter = 0.0f;
		for (uint32_t j = 40; j < 120; ++j)
		{
			for (uint32_t i = 70; i < 250; i += 20) softRaise = std::max(softRaise, a.Heights[At(i, j)] - h[At(i, j)]);
			for (uint32_t i = 1; i < 60; ++i) hardMoved = std::max(hardMoved, std::fabs(a.Heights[At(i, j)] - h[At(i, j)]));
			for (uint32_t i = 80; i < 250; i += 20) crestMoved = std::max(crestMoved, std::fabs(a.Heights[At(i, j)] - h[At(i, j)]));
			for (uint32_t i = 72; i < 78; ++i) { wallBefore = std::max(wallBefore, SlopeDeg(h, i, j)); wallAfter = std::max(wallAfter, SlopeDeg(a.Heights, i, j)); }
		}
		std::printf("deposit: the soft gullies' floors up %.2f m, their lower walls %.1f -> %.1f degrees; hard rock moved %.4f m, the crests %.4f m\n", softRaise, wallBefore, wallAfter, hardMoved, crestMoved);
		Check(softRaise > 1.0f, "the fill raises the soft gullies' floors");
		Check(wallAfter < wallBefore - 5.0f, "and eases their lower walls");
		Check(hardMoved < 1e-3f, "hard rock is not filled");
		Check(crestMoved < 1e-3f, "the crests do not move");
	}
	// the fans: one at every mouth, on the plain, at the fan's angle, holding about the volume asked
	{
		size_t fanCells = 0, gentle = 0, plain = 0;
		for (uint32_t j = 1; j + 1 < kN; ++j)
			for (uint32_t i = 1; i + 1 < kN; ++i)
			{
				if (a.Fan[At(i, j)] < 0.2f) continue;
				++fanCells;
				plain += j >= kFront - 10;
				const float deg = SlopeDeg(a.Heights, i, j);
				gentle += deg >= 2.0f && deg <= 8.0f;
			}
		std::printf("deposit: %zu fan cells, %.0f %% of them on the plain, %.0f %% at 2-8 degrees\n", fanCells, 100.0 * static_cast<double>(plain) / static_cast<double>(std::max<size_t>(fanCells, 1)),
			100.0 * static_cast<double>(gentle) / static_cast<double>(std::max<size_t>(fanCells, 1)));
		Check(s.Apexes >= 9, "an apex at the mouth of the gullies (" + std::to_string(s.Apexes) + ")");
		Check(fanCells > 0 && plain * 10 >= fanCells * 9, "the fans lie at the front, on the plain");
		Check(gentle * 10 >= fanCells * 7, "most of a fan is at its angle (2-8 degrees)");
		Check(s.FanVolume > 0.7 * s.FanTarget && s.FanVolume < 1.2 * s.FanTarget, "the fans hold about the volume asked");
	}
	// no new hollows, the hollow and its shore still, the border still
	{
		const DrainageResult before = ComputeDrainage(h.data(), kN, kSp), after = ComputeDrainage(a.Heights.data(), kN, kSp);
		size_t grew = 0, minima = 0, lakeMoved = 0, borderMoved = 0;
		float levelBefore = 0.0f, levelAfter = 0.0f;
		for (size_t c = 0; c < count; ++c)
		{
			grew += (after.Level[c] - a.Heights[c]) > (before.Level[c] - h[c]) + 1e-4f;
			const uint32_t i = static_cast<uint32_t>(c % kN), j = static_cast<uint32_t>(c / kN);
			const bool border = i == 0 || j == 0 || i + 1 == kN || j + 1 == kN;
			borderMoved += border && a.Heights[c] != h[c];
			if (before.Level[c] > h[c] + 1e-4f)
			{
				lakeMoved += a.Heights[c] != h[c];
				levelBefore = std::max(levelBefore, before.Level[c]); levelAfter = std::max(levelAfter, after.Level[c]);
				continue;
			}
			if (border) continue;
			bool lower = false;
			for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) if ((di || dj) && a.Heights[At(i + di, j + dj)] < a.Heights[c]) lower = true;
			minima += !lower;
		}
		Check(grew == 0, "no new hollows (" + std::to_string(grew) + " cells hold more water)");
		Check(minima == 0, "every cell outside the hollow has a lower neighbour (" + std::to_string(minima) + " do not)");
		Check(lakeMoved == 0 && levelBefore == levelAfter && levelBefore > 0.0f, "the hollow does not move and its level is the same");
		Check(borderMoved == 0, "the border does not move");
	}
	// the same twice; off, the input as it was
	{
		const DepositResult b = DepositSediment(h.data(), kN, kSp, o);
		Check(std::memcmp(a.Heights.data(), b.Heights.data(), count * sizeof(float)) == 0 && std::memcmp(a.Alluvium.data(), b.Alluvium.data(), count * sizeof(float)) == 0,
			"the same twice, bit for bit");
		DepositOptions off = o;
		off.Fill = 0.0f; off.FanArea = 0.0f; off.EaseAngle = 0.0f;
		const DepositResult c = DepositSediment(h.data(), kN, kSp, off);
		Check(std::memcmp(c.Heights.data(), h.data(), count * sizeof(float)) == 0, "with the fill, the fans and the easing off, the input as it was");
	}
	// the easing only cuts, and only on long slopes: the short gully walls stay
	{
		DepositOptions oe;
		oe.Fill = 0.0f; oe.FanArea = 0.0f;
		oe.EaseAngle = 20.0f; oe.ReposeAngle = 36.0f; oe.EaseStart = 60.0f; oe.EaseFull = 120.0f; oe.ChannelArea = 4000.0f;
		const DepositResult e = DepositSediment(h.data(), kN, kSp, oe);
		bool down = true;
		for (size_t c = 0; c < count; ++c) down = down && e.Heights[c] <= h[c];
		Check(down, "the easing only cuts");
		DepositOptions all = oe;
		all.EaseStart = 0.0f; all.EaseFull = 0.0f;   // every slope long: the walls eased too
		const DepositResult ea = DepositSediment(h.data(), kN, kSp, all);
		std::printf("deposit: easing to 20 degrees cuts %.0f m3 where slopes are long, %.0f m3 everywhere\n", e.Stats.EaseVolume, ea.Stats.EaseVolume);
		Check(ea.Stats.EaseVolume > 1000.0 && e.Stats.EaseVolume < 0.05 * ea.Stats.EaseVolume, "the short gully walls (20 m) are hardly eased, unlike every slope");
	}

	// ---------------------------------------------------------------- the node
	{
		TerrainGraph g;
		auto node = [&](const char* id, const char* type, std::map<std::string, double> params = {}, const char* source = "")
		{
			TgNode n; n.Id = id; n.Type = type; n.Label = id; n.Source = source; n.Params = std::move(params);
			g.Nodes.push_back(std::move(n));
		};
		node("ground", "heightfield_noise", { { "Frequency", 0.01 }, { "Octaves", 5 }, { "Amplitude", 120 }, { "Mode", 1 } });
		node("talus", "talus", { { "HardCover", 0.1 }, { "NoiseScale", 60 } });
		node("deposit", "deposit", { { "FanArea", 2000 }, { "FillRadius", 20 } });
		node("out_h", "output", {}, "height");
		g.Edges = { { "ground", "talus", "height", "height" }, { "talus", "deposit", "height", "height" }, { "talus", "deposit", "hardness", "hardness" },
			{ "deposit", "out_h", "in", "height" } };
		const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
		Check(!HasTgErrors(ValidateTerrainGraph(g, registry)), "noise -> talus -> deposit is a valid graph");
		TgContext ctx;
		ctx.Seed = 5; ctx.Resolution = 129; ctx.Size = 400.0f;
		TgResult r1, r2, r3, warm;
		std::string error;
		Check(EvaluateTerrainGraph(g, registry, ctx, r1, error), "it evaluates (" + error + ")");
		Check(EvaluateTerrainGraph(g, registry, ctx, r2, error) && TgHashResult(r1) == TgHashResult(r2), "the same twice");
		TerrainGraphCache cache;
		EvaluateTerrainGraph(g, registry, ctx, warm, error, &cache);
		Check(EvaluateTerrainGraph(g, registry, ctx, r3, error, &cache) && TgHashResult(r3) == TgHashResult(r1), "the same through the cache");
		if (r1.Nodes.count("deposit") && r1.Nodes.count("talus"))
		{
			const auto& out = r1.Nodes.at("deposit");
			const std::vector<float>& in = r1.Nodes.at("talus").at("height").Field.Values;
			const std::vector<float>& hh = out.at("height").Field.Values;
			size_t raised = 0;
			for (size_t c = 0; c < hh.size(); ++c) raised += hh[c] > in[c];
			std::printf("deposit node: %.1f %% of the cells raised\n", 100.0 * static_cast<double>(raised) / static_cast<double>(hh.size()));
			Check(raised > 0, "it lays something down");
			for (const char* port : { "alluvium", "fan", "eased" }) Check(out.count(port) && out.at(port).Field.Values.size() == hh.size(), std::string("output ") + port);
		}
		TerrainGraph bad = g;
		for (TgNode& n : bad.Nodes) if (n.Id == "deposit") n.Params["Revision"] = 2;
		TgResult r;
		Check(!EvaluateTerrainGraph(bad, registry, ctx, r, error) && error.find("Revision") != std::string::npos, "an unknown Revision is refused, not guessed");
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("deposit: all checks passed\n");
	return failures ? 1 : 0;
}
