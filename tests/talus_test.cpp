// TERRENO_V5 E4: the talus with hardness (GB/World/Talus.h) and the node talus (Nodes/TalusNode.h), headless and on GB_WorldGen alone.
//   - a synthetic step: a plateau 40 m over a gently tilted plain, its north half hard rock and its south half soft. The hard wall keeps a
//     cliff (it is only cut to its angle, 70 degrees), the soft wall is cut back to its angle (36), what falls rests at the foot as scree at
//     the scree's angle, and the volume is kept: the ground loses exactly what was carried away, and nothing here reaches a channel or the
//     border. Ground under its angle (the plain far from the wall, the plateau's top far from its edge) does not move by a single bit;
//   - the same twice, bit for bit;
//   - a lake (a closed hollow in the plain, with a steep shore) does not move, and its shore is held at its angle over the water;
//   - no new hollows: the level of the water over the ground grows nowhere;
//   - the node: evaluates, the same twice and through the cache, its own hardness when none is given, and an unknown Revision is refused.
#include "GB/World/Drainage.h"
#include "GB/World/Talus.h"
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
	constexpr float kSp = 1.0f;

	size_t At(uint32_t i, uint32_t j) { return static_cast<size_t>(j) * kN + i; }

	// The steepest slope (degrees, central differences) over the cells i0..i1, j0..j1.
	float Steepest(const std::vector<float>& h, uint32_t i0, uint32_t i1, uint32_t j0, uint32_t j1)
	{
		float most = 0.0f;
		for (uint32_t j = j0; j <= j1; ++j)
			for (uint32_t i = i0; i <= i1; ++i)
			{
				const float gx = (h[At(i + 1, j)] - h[At(i - 1, j)]) / (2.0f * kSp), gz = (h[At(i, j + 1)] - h[At(i, j - 1)]) / (2.0f * kSp);
				most = std::max(most, std::atan(std::sqrt(gx * gx + gz * gz)) * 57.2957795f);
			}
		return most;
	}

	double Volume(const std::vector<float>& h)
	{
		double v = 0.0;
		for (const float x : h) v += x;
		return v * kSp * kSp;
	}
}

int GBTest_talus(int, char**)
{
	// ---------------------------------------------------------------- the step
	// the plateau: i < 128 and 32 <= j <= 224, 40 m over the plain; the plain falls 2 % toward the east border (i = 256), so every cell of it
	// drains; a hollow in the plain at (205, 128), 4 m deep with vertical sides: a lake
	const size_t count = static_cast<size_t>(kN) * kN;
	std::vector<float> h(count), hard(count);
	for (uint32_t j = 0; j < kN; ++j)
		for (uint32_t i = 0; i < kN; ++i)
		{
			const float plain = 0.02f * static_cast<float>(kN - 1 - i);
			const bool plateau = i < 128 && j >= 32 && j <= 224;
			const float di = static_cast<float>(i) - 205.0f, dj = static_cast<float>(j) - 128.0f;
			const bool pond = di * di + dj * dj <= 36.0f;
			h[At(i, j)] = plateau ? 40.0f + plain : pond ? plain - 4.0f : plain;
			hard[At(i, j)] = j < 128 ? 1.0f : 0.0f;
		}
	TalusOptions o;
	o.SoftAngle = 36.0f;
	o.HardAngle = 70.0f;
	o.ScreeAngle = 33.0f;
	o.ChannelArea = 0.0f;   // no channels: all that falls rests as scree
	o.Hardness = hard.data();
	const TalusResult a = TalusErode(h.data(), kN, kSp, o);
	const TalusStats& s = a.Stats;
	std::printf("talus: cut %.0f m3 on %zu cells, %zu feet with %.0f m3, scree %.0f m3 on %zu cells (apex factor %.2f, %d passes); carried away: channels %.0f, lakes %.0f, border %.0f m3; %zu lake cells\n",
		s.CutVolume, s.CutCells, s.Feet, s.FootVolume, s.ScreeVolume, s.ScreeCells, s.ApexFactor, s.FitPasses, s.ChannelVolume, s.LakeVolume, s.BorderVolume, s.LakeCells);
	std::printf("talus: %.1f ms (flood %.1f, cut %.1f, fall %.1f, scree %.1f)\n", s.FloodMs + s.CutMs + s.FallMs + s.ScreeMs, s.FloodMs, s.CutMs, s.FallMs, s.ScreeMs);

	// the hard wall keeps a cliff, cut to its angle; the soft one is cut back to its angle (the scree rests at a lower one)
	const float hardMost = Steepest(a.Heights, 100, 160, 50, 110), softMost = Steepest(a.Heights, 60, 200, 150, 210);
	std::printf("talus: steepest on the hard wall %.1f deg, on the soft one %.1f deg\n", hardMost, softMost);
	Check(hardMost > 60.0f && hardMost <= 70.0f * 1.04f, "the hard wall keeps a cliff, no steeper than its angle (" + std::to_string(hardMost) + ")");
	Check(softMost <= 36.0f * 1.04f && softMost > 30.0f, "the soft wall is cut back to its angle (" + std::to_string(softMost) + ")");
	// the soft wall retreats much farther than the hard one: the plateau's top is cut back from its edge
	size_t hardBack = 0, softBack = 0;
	for (uint32_t i = 0; i < 128; ++i) { hardBack += a.Cut[At(i, 80)] > 0.0f; softBack += a.Cut[At(i, 176)] > 0.0f; }
	std::printf("talus: the top cut back %zu m on the hard side, %zu m on the soft one\n", hardBack, softBack);
	Check(hardBack >= 5 && hardBack <= 30 && softBack >= 40, "the soft wall retreats more than the hard one (" + std::to_string(hardBack) + ", " + std::to_string(softBack) + " m)");
	// scree at the foot, on both sides, and none on the plateau's top away from the edge
	Check(a.Scree[At(130, 80)] > 1.0f && a.Scree[At(135, 176)] > 1.0f, "scree at the foot of both walls");
	Check(a.Scree[At(20, 128)] == 0.0f && a.Cut[At(20, 128)] == 0.0f, "the plateau's top away from the edge is not touched");
	// the volume: what the ground lost is what was carried away (none of it here: no channel, the toe far from the border)
	const double lost = Volume(h) - Volume(a.Heights);
	const double away = s.ChannelVolume + s.LakeVolume + s.BorderVolume + (s.FootVolume - s.ScreeVolume);
	std::printf("talus: the ground lost %.1f m3 of %.0f cut; carried away %.1f m3\n", lost, s.CutVolume, away);
	Check(std::fabs(lost - away) <= 1e-3 * s.CutVolume, "the ground loses exactly what was carried away");
	Check(s.ScreeVolume >= 0.97 * s.CutVolume && s.ScreeVolume <= s.FootVolume * 1.0005, "the scree holds what fell (" + std::to_string(s.ScreeVolume / s.CutVolume) + " of the cut)");
	// ground under its angle does not move: the plain far from the wall and the pond
	bool still = true;
	for (uint32_t j = 0; j < kN; ++j) for (uint32_t i = 240; i < kN; ++i) still = still && a.Heights[At(i, j)] == h[At(i, j)];
	Check(still, "the plain far from the wall does not move by a bit");
	// the lake: its cells do not move, its shore stands within its angle over the water (not over the pond's floor)
	bool lakeStill = true, lakeMarked = true;
	for (uint32_t j = 120; j <= 136; ++j)
		for (uint32_t i = 197; i <= 213; ++i)
		{
			const float di = static_cast<float>(i) - 205.0f, dj = static_cast<float>(j) - 128.0f;
			if (di * di + dj * dj > 36.0f) continue;
			lakeStill = lakeStill && a.Heights[At(i, j)] == h[At(i, j)];
			lakeMarked = lakeMarked && a.Lake[At(i, j)] == 1;
		}
	Check(lakeStill && lakeMarked, "the lake's cells are a lake and do not move");
	// no new hollows: the level of the water over the ground grows nowhere
	const DrainageResult before = ComputeDrainage(h.data(), kN, kSp), after = ComputeDrainage(a.Heights.data(), kN, kSp);
	size_t grew = 0;
	for (size_t c = 0; c < count; ++c) grew += (after.Level[c] - a.Heights[c]) > (before.Level[c] - h[c]) + 1e-4f;
	Check(grew == 0, "no new hollows (" + std::to_string(grew) + " cells hold more water)");
	// the same twice
	const TalusResult b = TalusErode(h.data(), kN, kSp, o);
	Check(std::memcmp(a.Heights.data(), b.Heights.data(), count * sizeof(float)) == 0 && std::memcmp(a.Scree.data(), b.Scree.data(), count * sizeof(float)) == 0,
		"the same twice, bit for bit");
	// with channels, what reaches one is carried away and the channel's cells take no scree
	{
		TalusOptions oc = o;
		oc.ChannelArea = 400.0f;
		const TalusResult c = TalusErode(h.data(), kN, kSp, oc);
		Check(c.Stats.ChannelCells > 0 && c.Stats.ChannelVolume > 0.0, "with ChannelArea the channels carry some away");
		const double lostC = Volume(h) - Volume(c.Heights);
		const double awayC = c.Stats.ChannelVolume + c.Stats.LakeVolume + c.Stats.BorderVolume + (c.Stats.FootVolume - c.Stats.ScreeVolume);
		Check(std::fabs(lostC - awayC) <= 1e-3 * c.Stats.CutVolume, "and the books still balance");
	}
	// without scree, all is carried away and the ground only goes down
	{
		TalusOptions on = o;
		on.Deposit = false;
		const TalusResult c = TalusErode(h.data(), kN, kSp, on);
		bool down = true;
		for (size_t k = 0; k < count; ++k) down = down && c.Heights[k] <= h[k];
		Check(down && c.Stats.ScreeVolume == 0.0, "with Deposit off the ground only goes down");
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
		node("talus", "talus", { { "HardCover", 0.2 }, { "NoiseScale", 60 }, { "StrataSpacing", 12 } });
		node("out_h", "output", {}, "height");
		g.Edges = { { "ground", "talus", "height", "height" }, { "talus", "out_h", "in", "height" } };
		const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
		Check(!HasTgErrors(ValidateTerrainGraph(g, registry)), "noise -> talus is a valid graph");
		TgContext ctx;
		ctx.Seed = 5; ctx.Resolution = 129; ctx.Size = 400.0f;
		TgResult r1, r2, r3, warm;
		std::string error;
		Check(EvaluateTerrainGraph(g, registry, ctx, r1, error), "it evaluates (" + error + ")");
		Check(EvaluateTerrainGraph(g, registry, ctx, r2, error) && TgHashResult(r1) == TgHashResult(r2), "the same twice");
		TerrainGraphCache cache;
		EvaluateTerrainGraph(g, registry, ctx, warm, error, &cache);
		Check(EvaluateTerrainGraph(g, registry, ctx, r3, error, &cache) && TgHashResult(r3) == TgHashResult(r1), "the same through the cache");
		if (r1.Nodes.count("talus") && r1.Nodes.count("ground"))
		{
			const auto& out = r1.Nodes.at("talus");
			const std::vector<float>& in = r1.Nodes.at("ground").at("height").Field.Values;
			const std::vector<float>& hh = out.at("height").Field.Values;
			const std::vector<float>& hd = out.at("hardness").Field.Values;
			size_t hardCells = 0, moved = 0;
			for (size_t c = 0; c < hh.size(); ++c) { hardCells += hd[c] > 0.5f; moved += hh[c] != in[c]; }
			std::printf("talus node: %.1f %% hard, %.1f %% of the cells moved\n", 100.0 * static_cast<double>(hardCells) / static_cast<double>(hh.size()),
				100.0 * static_cast<double>(moved) / static_cast<double>(hh.size()));
			Check(hardCells > hh.size() / 10 && hardCells < hh.size(), "its own hardness: patches and beds");
			Check(moved > 0, "it moves the ground");
			for (const char* port : { "scree", "cliff", "cut" }) Check(out.count(port) && out.at(port).Field.Values.size() == hh.size(), std::string("output ") + port);
		}
		TerrainGraph bad = g;
		for (TgNode& n : bad.Nodes) if (n.Id == "talus") n.Params["Revision"] = 2;
		TgResult r;
		Check(!EvaluateTerrainGraph(bad, registry, ctx, r, error) && error.find("Revision") != std::string::npos, "an unknown Revision is refused, not guessed");
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("talus: all checks passed\n");
	return failures ? 1 : 0;
}
