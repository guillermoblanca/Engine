// TERRENO_V5 E2: the stream-power erosion (GB/World/StreamPower.h) and the node fluvial_erosion (Nodes/FluvialErosionNode.h), headless and
// on GB_WorldGen alone.
//   - a cone with uplift: one iteration keeps the mass by the formula (the ground rose by U dt and went down by K dt sqrt(A) S, with S the
//     slope to the receiver after the step, cell by cell, and the volumes add up); forty iterations cut radial valleys into it and make no pit;
//   - a closed bowl: its cells (a lake for the drainage) only rise with the uplift, they are never incised;
//   - the cascade: the same twice, bit for bit; its coarse stage on a field that nests over the coarse grid is exactly the erosion run on the
//     decimated field (what the preview at the coarse resolution computes); hard rock erodes less;
//   - E2b, the diffusion near a critical slope (Roering): a steep wall wears back faster than with the linear diffusion, a gentle slope almost
//     the same, and the volume stays; with it off (the default), the cascade and the node give the bytes they gave before it existed;
//   - the node: evaluates, the same twice and through the cache, and an unknown Revision is refused.
#include "GB/World/Drainage.h"
#include "GB/World/FlowAccumulation.h"
#include "GB/World/StreamPower.h"
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

	float Hash01(uint32_t i, uint32_t j, uint32_t seed)
	{
		uint32_t h = i * 0x8da6b343u ^ j * 0xd8163841u ^ seed * 0xcb1ab31fu;
		h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
		return static_cast<float>(h & 0xffffu) / 65535.0f;
	}

	// A cone 200 m high and 600 m in radius on a 1280 m map, with half a metre of noise so the water picks its own valleys.
	std::vector<float> Cone(uint32_t n, float size)
	{
		std::vector<float> h(static_cast<size_t>(n) * n);
		const float sp = size / static_cast<float>(n - 1);
		for (uint32_t j = 0; j < n; ++j)
			for (uint32_t i = 0; i < n; ++i)
			{
				const float x = static_cast<float>(i) * sp - 0.5f * size, z = static_cast<float>(j) * sp - 0.5f * size;
				const float r = std::sqrt(x * x + z * z);
				h[static_cast<size_t>(j) * n + i] = std::max(0.0f, 200.0f * (1.0f - r / 600.0f)) + 0.5f * Hash01(i, j, 7);
			}
		return h;
	}

	size_t Pits(const std::vector<float>& h, uint32_t n)
	{
		const std::vector<int> d8 = FlowAccumulation::Directions(h.data(), n);
		size_t pits = 0;
		for (size_t c = 0; c < h.size(); ++c)
		{
			const size_t i = c % n, j = c / n;
			if (i == 0 || j == 0 || i == n - 1 || j == n - 1) continue;
			pits += d8[c] < 0;
		}
		return pits;
	}

	double Volume(const std::vector<float>& h, float cell)
	{
		double v = 0.0;
		for (const float x : h) v += x;
		return v * cell;
	}
}

int GBTest_fluvial_erosion(int, char**)
{
	constexpr uint32_t kN = 129;
	constexpr float kSize = 1280.0f;
	const float sp = kSize / static_cast<float>(kN - 1), cell = sp * sp;
	const size_t count = static_cast<size_t>(kN) * kN;

	// ---------------------------------------------------------------- one iteration: the mass by the formula
	{
		const std::vector<float> h0 = Cone(kN, kSize);
		std::vector<float> h1 = h0;
		StreamPowerOptions o;
		o.Erodibility = 4e-5f;
		o.Uplift = 1e-3f;
		o.TimeStep = 1000.0f;
		o.Iterations = 1;
		o.MultipleFlow = false;   // (the drainage below is the one the iteration used: its area along the receivers)
		StreamPowerStats st;
		StreamPowerErode(h1, kN, sp, o, &st);
		const DrainageResult d = ComputeDrainage(h0.data(), kN, sp);   // the drainage that iteration used
		double worst = 0.0, incised = 0.0, allLost = 0.0;
		size_t incisedCells = 0;
		bool lakesRise = true, borderStays = true;
		for (size_t c = 0; c < count; ++c)
		{
			const int32_t r = d.Receiver[c];
			if (r < 0)
			{
				const size_t i = c % kN, j = c / kN;
				const size_t inner = i == 0 || i == kN - 1 ? std::clamp<size_t>(j, 1, kN - 2) * kN + (i == 0 ? 1 : kN - 2) : (j == 0 ? kN : (kN - 2) * kN) + std::clamp<size_t>(i, 1, kN - 2);
				borderStays = borderStays && (h1[c] == h0[c] || h1[c] == h1[inner]) && h1[c] <= h0[c];
				continue;
			}
			const float up = h0[c] + o.Uplift * o.TimeStep;
			if (d.Level[c] > h0[c]) { lakesRise = lakesRise && h1[c] == up; continue; }
			if (up - h1[c] <= 0.0f) continue;
			allLost += static_cast<double>(up) - h1[c];
			// the stream-power law with the slope after the step: h0 + U dt - h1 = K dt sqrt(A) (h1 - h1[r]) / L (a receiver in a lake: the
			// base is the lake's surface, checked by the bowl below)
			const size_t rc = static_cast<size_t>(r);
			if (d.Level[rc] > h0[rc]) continue;
			const bool diag = rc % kN != c % kN && rc / kN != c / kN;
			const double L = diag ? sp * 1.41421356 : sp;
			const double law = static_cast<double>(o.Erodibility) * o.TimeStep * std::sqrt(static_cast<double>(d.Area[c])) * (static_cast<double>(h1[c]) - h1[rc]) / L;
			const double lost = static_cast<double>(up) - h1[c];
			worst = std::max(worst, std::fabs(law - lost) / (lost + 1e-3));
			incised += lost;
			++incisedCells;
		}
		Check(incisedCells > 1000, "the cone is incised (" + std::to_string(incisedCells) + " cells)");
		Check(worst < 1e-3, "every incised cell lost K dt sqrt(A) S, S the slope to its receiver after the step (worst relative error " + std::to_string(worst) + ")");
		Check(borderStays, "the border is base level: it does not rise, it only follows the ground inside it down");
		Check(lakesRise, "a cell in a hollow is not incised: it rises by U dt and nothing else");
		const double dv = Volume(h1, cell) - Volume(h0, cell);
		const double expected = st.UpliftVolume - st.IncisionVolume - st.BorderVolume;
		Check(std::fabs(dv - expected) < 1e-4 * std::max(1.0, std::fabs(st.UpliftVolume)), "the volume changed by the uplift minus the incision and the border's lowering (" + std::to_string(dv) + " vs " + std::to_string(expected) + " m3)");
		Check(std::fabs(st.IncisionVolume - allLost * cell) < 1e-3 * std::max(1.0, st.IncisionVolume), "the incision volume is the sum of the formula over the cells");
		std::printf("fluvial_erosion: one step on the cone: +%.0f m3 uplift, -%.0f m3 incision over %zu cells\n", st.UpliftVolume, st.IncisionVolume, incisedCells);
	}

	// ---------------------------------------------------------------- forty iterations: radial valleys, no pit
	{
		const std::vector<float> h0 = Cone(kN, kSize);
		std::vector<float> h = h0;
		StreamPowerOptions o;
		o.Erodibility = 4e-5f;
		o.Uplift = 1e-3f;
		o.TimeStep = 1000.0f;
		o.Iterations = 40;
		StreamPowerErode(h, kN, sp, o);
		TgField f;
		f.Resize(kN, kSize);
		f.Values = h;
		// around the circle at half the radius: valleys (local minima) between ridges, metres deep; the cone had half a metre of noise there
		constexpr int kAround = 180;
		float lo = 1e9f, hi = -1e9f;
		std::vector<float> ring(kAround);
		for (int k = 0; k < kAround; ++k)
		{
			const float a = 6.2831853f * static_cast<float>(k) / kAround;
			ring[static_cast<size_t>(k)] = f.Sample(300.0f * std::cos(a), 300.0f * std::sin(a));
			lo = std::min(lo, ring[static_cast<size_t>(k)]); hi = std::max(hi, ring[static_cast<size_t>(k)]);
		}
		int valleys = 0;
		for (int k = 0; k < kAround; ++k)
		{
			const float a = ring[static_cast<size_t>((k + kAround - 1) % kAround)], b = ring[static_cast<size_t>(k)], c = ring[static_cast<size_t>((k + 1) % kAround)];
			valleys += b < a && b <= c;
		}
		std::printf("fluvial_erosion: cone after 40 steps: %.1f m of relief around the half radius, %d valleys; pits %zu -> %zu\n", hi - lo, valleys, Pits(h0, kN), Pits(h, kN));
		Check(hi - lo > 5.0f, "valleys metres deep around the cone (" + std::to_string(hi - lo) + " m)");
		Check(valleys >= 6, "several radial valleys (" + std::to_string(valleys) + ")");
		Check(Pits(h, kN) <= Pits(h0, kN), "the incision makes no new pit");
	}

	// ---------------------------------------------------------------- a closed bowl is a lake: never incised
	{
		std::vector<float> h(count);
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i)
			{
				const float x = static_cast<float>(i) - 64.0f, z = static_cast<float>(j) - 64.0f;
				const float r = std::sqrt(x * x + z * z);
				// a slope down to the border outside a wide rim at 60 m around a bowl whose floor is 20 m with a little noise
				float v = r < 30.0f ? 20.0f + 0.3f * Hash01(i, j, 3) + 0.01f * r * r : r < 40.0f ? 60.0f : std::max(0.0f, 60.0f - 2.0f * (r - 40.0f));
				h[static_cast<size_t>(j) * kN + i] = v;
			}
		const DrainageResult d = ComputeDrainage(h.data(), kN, sp);
		std::vector<float> after = h;
		StreamPowerOptions o;
		o.Erodibility = 4e-5f;
		o.Uplift = 0.0f;
		o.TimeStep = 1000.0f;
		o.Iterations = 10;
		StreamPowerErode(after, kN, sp, o);
		size_t lake = 0, moved = 0, outside = 0;
		for (size_t c = 0; c < count; ++c)
		{
			if (d.Level[c] > h[c]) { ++lake; moved += after[c] != h[c]; }
			else outside += after[c] < h[c];
		}
		std::printf("fluvial_erosion: bowl: %zu lake cells, %zu moved; %zu cells incised outside it\n", lake, moved, outside);
		Check(lake > 1000 && moved == 0, "the bowl's cells keep their ground (" + std::to_string(moved) + " of " + std::to_string(lake) + " moved)");
		Check(outside > 100, "the slopes outside it are incised");
	}

	// ---------------------------------------------------------------- E2b: the diffusion near a critical slope (Roering)
	// A mesa in the middle of a flat map (no incision, no uplift: only the diffusion), once with a 45-degree wall and once with a 6-degree one,
	// linear and with the critical slope at 30 degrees. The steep wall wears back faster with the critical slope, under 30 degrees; the gentle one
	// almost as with the linear diffusion; the ground's volume stays (nothing reaches the border, which is flat).
	{
		constexpr uint32_t n = 65;
		const float spacing = 4.0f;
		auto mesa = [&](float top, float inner, float outer)
		{
			std::vector<float> h(static_cast<size_t>(n) * n);
			for (uint32_t j = 0; j < n; ++j)
				for (uint32_t i = 0; i < n; ++i)
				{
					const float x = (static_cast<float>(i) - 32.0f) * spacing, z = (static_cast<float>(j) - 32.0f) * spacing;
					const float r = std::sqrt(x * x + z * z);
					h[static_cast<size_t>(j) * n + i] = top * std::clamp((outer - r) / (outer - inner), 0.0f, 1.0f);
				}
			return h;
		};
		auto run = [&](const std::vector<float>& h0, float sc, double& moved, double& dv, float& steepest)
		{
			std::vector<float> h = h0;
			StreamPowerOptions o;
			o.Erodibility = 0.0f;
			o.Diffusion = 0.004f;
			o.CriticalSlope = sc;
			o.TimeStep = 1000.0f;
			o.Iterations = 10;
			StreamPowerErode(h, n, spacing, o);
			moved = 0.0;
			steepest = 0.0f;
			for (size_t c = 0; c < h.size(); ++c) moved = std::max(moved, static_cast<double>(std::fabs(h[c] - h0[c])));
			for (uint32_t j = 1; j + 1 < n; ++j)
				for (uint32_t i = 1; i + 1 < n; ++i)
				{
					const size_t c = static_cast<size_t>(j) * n + i;
					steepest = std::max({ steepest, std::fabs(h[c + 1] - h[c]) / spacing, std::fabs(h[c + n] - h[c]) / spacing });
				}
			dv = Volume(h, spacing * spacing) - Volume(h0, spacing * spacing);
		};
		const float sc = std::tan(30.0f * 3.14159265f / 180.0f);
		const std::vector<float> steep = mesa(30.0f, 50.0f, 80.0f), gentle = mesa(6.0f, 40.0f, 100.0f);
		double moveLin = 0, moveCrit = 0, gentleLin = 0, gentleCrit = 0, dvLin = 0, dvCrit = 0, dvGentle = 0;
		float slopeLin = 0, slopeCrit = 0, slopeGentle = 0;
		run(steep, 0.0f, moveLin, dvLin, slopeLin);
		run(steep, sc, moveCrit, dvCrit, slopeCrit);
		run(gentle, 0.0f, gentleLin, dvGentle, slopeGentle);
		run(gentle, sc, gentleCrit, dvGentle, slopeGentle);
		std::printf("fluvial_erosion: critical slope 30 deg: the 45-degree wall moves %.2f m (linear %.2f m) and ends at %.2f (linear %.2f); the 6-degree one %.3f m (linear %.3f m)\n",
			moveCrit, moveLin, slopeCrit, slopeLin, gentleCrit, gentleLin);
		Check(moveCrit > 2.0 * moveLin && slopeCrit < sc && slopeLin > sc,
			"with the critical slope a steep wall wears back faster than with the linear diffusion, down to under the critical slope (the linear one stays above it)");
		Check(gentleCrit < 1.15 * gentleLin && gentleCrit >= gentleLin, "a gentle slope diffuses almost as with the linear diffusion");
		const double volume = Volume(steep, spacing * spacing);
		Check(std::fabs(dvCrit) < 1e-5 * volume && std::fabs(dvGentle) < 1e-5 * volume, "the critical-slope diffusion moves ground, it does not make or lose it (" + std::to_string(dvCrit) + " m3)");

		// no checkerboard on a steep, rough wall: a 45-degree cone with cell-scale noise, diffused with the critical slope. The sub-steps are
		// sized by the flux's derivative; sized by its ratio q/u (what E2b had first) the walls grew an odd-even mode, the diamonds of the snow
		constexpr uint32_t cn = 129;
		std::vector<float> cone(static_cast<size_t>(cn) * cn);
		for (uint32_t j = 0; j < cn; ++j)
			for (uint32_t i = 0; i < cn; ++i)
			{
				const float x = (static_cast<float>(i) - 64.0f) * spacing, z = (static_cast<float>(j) - 64.0f) * spacing;
				cone[static_cast<size_t>(j) * cn + i] = std::max(0.0f, 200.0f - std::sqrt(x * x + z * z)) + 0.5f * Hash01(i, j, 3);
			}
		StreamPowerOptions co;
		co.Erodibility = 0.0f;
		co.Diffusion = 0.01f;
		co.CriticalSlope = sc;
		co.CriticalSlopeCap = 0.8f;
		co.TimeStep = 1000.0f;
		co.Iterations = 20;
		StreamPowerErode(cone, cn, spacing, co);
		double board = 0.0, rough = 0.0;
		size_t cells = 0;
		for (uint32_t j = 2; j + 2 < cn; ++j)
			for (uint32_t i = 2; i + 2 < cn; ++i)
			{
				const size_t c = static_cast<size_t>(j) * cn + i;
				if (cone[c] < 5.0f) continue;
				const double lap = static_cast<double>(cone[c - 1] + cone[c + 1] + cone[c - cn] + cone[c + cn] - 4.0f * cone[c]);
				board += (i + j) % 2 ? lap : -lap;
				rough += std::fabs(lap);
				++cells;
			}
		board = std::fabs(board) / static_cast<double>(std::max<size_t>(1, cells));
		rough /= static_cast<double>(std::max<size_t>(1, cells));
		std::printf("fluvial_erosion: critical slope on a rough 45-degree cone: checkerboard %.4f m, mean |laplacian| %.4f m over %zu cells\n", board, rough, cells);
		Check(cells > 1000 && board < 0.01, "the critical-slope diffusion grows no checkerboard on a steep wall");
	}

	// ---------------------------------------------------------------- E2b: the channel head
	// With ChannelArea the incision only acts where the drainage area passes it: in one step without diffusion every cell below the ramp
	// rises by exactly U dt, and those well above it are incised. Over 40 steps the same twice and no new pit.
	{
		const std::vector<float> h0 = Cone(kN, kSize);
		StreamPowerOptions o;
		o.Erodibility = 4e-5f;
		o.Uplift = 1e-3f;
		o.TimeStep = 1000.0f;
		o.Iterations = 1;
		o.MultipleFlow = false;   // (the drainage below is the one the step used)
		o.ChannelArea = 5000.0f;
		o.ChannelSpread = 2.0f;
		std::vector<float> h1 = h0;
		StreamPowerErode(h1, kN, sp, o);
		const DrainageResult d = ComputeDrainage(h0.data(), kN, sp);
		const float udt = o.Uplift * o.TimeStep;
		size_t hill = 0, hillWrong = 0, channel = 0, channelCut = 0;
		for (size_t c = 0; c < count; ++c)
		{
			const size_t i = c % kN, j = c / kN;
			if (i == 0 || j == 0 || i == kN - 1 || j == kN - 1 || d.Receiver[c] < 0) continue;
			if (d.Area[c] < 2500.0f) { ++hill; hillWrong += h1[c] != h0[c] + udt; }
			else if (d.Area[c] > 10000.0f && !(d.Level[c] > h0[c])) { ++channel; channelCut += h1[c] < h0[c] + udt; }
		}
		std::printf("fluvial_erosion: channel head 5000 m2: %zu hillslope cells (%zu not just uplifted), %zu channel cells (%zu incised)\n", hill, hillWrong, channel, channelCut);
		Check(hill > 1000 && hillWrong == 0, "below the channel head the ground is not incised: it rises by U dt exactly");
		Check(channel > 20 && channelCut * 10 >= channel * 9, "well above the channel head the cells are incised");
		o.Iterations = 40;
		o.MultipleFlow = true;
		o.Diffusion = 0.01f;
		o.CriticalSlope = std::tan(35.0f * 3.14159265f / 180.0f);
		std::vector<float> a = h0, b = h0;
		StreamPowerErode(a, kN, sp, o);
		StreamPowerErode(b, kN, sp, o);
		Check(std::memcmp(a.data(), b.data(), count * sizeof(float)) == 0, "with the channel head and the critical slope the same twice, bit for bit");
		Check(Pits(a, kN) <= Pits(h0, kN), "the channel head makes no new pit (" + std::to_string(Pits(a, kN)) + " pits, " + std::to_string(Pits(h0, kN)) + " before)");
		std::vector<float> plain = h0;
		o.ChannelArea = 0.0f;
		StreamPowerErode(plain, kN, sp, o);
		Check(std::memcmp(a.data(), plain.data(), count * sizeof(float)) != 0, "the channel head changes the result");
	}

	// ---------------------------------------------------------------- the cascade
	{
		const std::vector<float> h = Cone(kN, kSize);
		std::vector<float> hard(count);
		for (size_t c = 0; c < count; ++c) hard[c] = (c % kN) < kN / 2 ? 1.0f : 0.0f;   // the west half hard rock
		StreamPowerOptions o;
		o.Erodibility = 4e-5f;
		o.Uplift = 5e-4f;
		o.Diffusion = 0.05f;
		o.TimeStep = 1000.0f;
		o.Hardness = hard.data();
		o.HardErodibility = 0.2f;
		StreamPowerCascade cascade;
		cascade.CoarseN = 33;
		cascade.CoarseIterations = 30;
		cascade.MidIterations = 5;
		cascade.FineIterations = 2;
		const StreamPowerCascadeResult a = StreamPowerErodeCascade(h.data(), kN, sp, o, cascade);
		const StreamPowerCascadeResult b = StreamPowerErodeCascade(h.data(), kN, sp, o, cascade);
		// (E2b) with the critical slope off this is what it was before CriticalSlope existed: the hash recorded on master 02b8307
		const uint64_t cascadeHash = TgHashBytes(a.Heights.data(), a.Heights.size() * sizeof(float));
		Check(cascadeHash == 0x8bd9076c17d138b8ull, "linear diffusion (CriticalSlope 0) gives the cascade of before E2b, bit for bit (hash " + std::to_string(cascadeHash) + ")");
		Check(a.Stages == std::vector<uint32_t>{ 33, 65, 129 }, "the stages are 33, 65 and 129");
		Check(a.Heights.size() == count && std::memcmp(a.Heights.data(), b.Heights.data(), count * sizeof(float)) == 0, "the cascade gives the same twice, bit for bit");
		// the preview: the same graph at 33 samples sees the decimated input and runs the coarse stage alone
		const std::vector<float> small = ResampleGrid(h.data(), kN, 33), smallHard = ResampleGrid(hard.data(), kN, 33);
		bool decimated = true;
		for (uint32_t j = 0; j < 33; ++j)
			for (uint32_t i = 0; i < 33; ++i) decimated = decimated && small[j * 33 + i] == h[static_cast<size_t>(j) * 4 * kN + i * 4];
		Check(decimated, "resampling 129 onto 33 takes every 4th sample exactly");
		StreamPowerOptions so = o;
		so.Hardness = smallHard.data();
		const StreamPowerCascadeResult preview = StreamPowerErodeCascade(small.data(), 33, kSize / 32.0f, so, cascade);
		Check(preview.Stages == std::vector<uint32_t>{ 33 } && a.CoarseN == 33 && preview.Heights.size() == a.Coarse.size() &&
			std::memcmp(preview.Heights.data(), a.Coarse.data(), a.Coarse.size() * sizeof(float)) == 0,
			"the coarse stage of the 129 field is the 33 field eroded, bit for bit (the preview predicts the world)");
		// and the finer stages keep the coarse valleys: the result decimated is close to the coarse result
		const std::vector<float> back = ResampleGrid(a.Heights.data(), kN, 33);
		double diff = 0.0;
		for (size_t c = 0; c < back.size(); ++c) diff = std::max(diff, static_cast<double>(std::fabs(back[c] - a.Coarse[c])));
		double relief = 0.0;
		for (size_t c = 0; c < a.Coarse.size(); ++c) relief = std::max(relief, static_cast<double>(small[c] - a.Coarse[c]));
		std::printf("fluvial_erosion: cascade 33/65/129: the finer stages move the coarse samples by %.2f m at most (the coarse stage cut %.1f m)\n", diff, relief);
		Check(diff < 0.5 * relief, "the finer stages add detail, they do not redo the valleys");
		// hard rock erodes less: the incision of the west half against the east
		double west = 0.0, east = 0.0;
		for (size_t c = 0; c < count; ++c) (hard[c] > 0.5f ? west : east) += std::max(0.0f, h[c] - a.Heights[c]);
		std::printf("fluvial_erosion: incision west (hard) %.0f, east (soft) %.0f m.cells\n", west, east);
		Check(west < 0.6 * east, "hard rock erodes less");
	}

	// ---------------------------------------------------------------- the node
	{
		TerrainGraph g;
		auto node = [&](const char* id, const char* type, std::map<std::string, double> params = {}, const char* source = "")
		{
			TgNode n; n.Id = id; n.Type = type; n.Label = id; n.Source = source; n.Params = std::move(params);
			g.Nodes.push_back(std::move(n));
		};
		node("ground", "heightfield_noise", { { "Frequency", 0.006 }, { "Octaves", 5 }, { "Amplitude", 60 } });
		node("hard", "heightfield_noise", { { "Frequency", 0.01 }, { "Octaves", 2 }, { "Amplitude", 1 }, { "Seed", 3 } });
		node("fluvial", "fluvial_erosion", { { "Erodibility", 4 }, { "TimeStep", 1000 }, { "CoarseResolution", 33 }, { "CoarseIterations", 20 },
			{ "MidIterations", 4 }, { "FineIterations", 2 } });
		node("out_h", "output", {}, "height");
		g.Edges = { { "ground", "fluvial", "height", "height" }, { "hard", "fluvial", "hardness", "height" }, { "fluvial", "out_h", "in", "height" } };
		const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
		Check(!HasTgErrors(ValidateTerrainGraph(g, registry)), "noise -> fluvial_erosion is a valid graph");
		TgContext ctx;
		ctx.Seed = 5; ctx.Resolution = kN; ctx.Size = kSize;
		TgResult r1, r2, r3, warm;
		std::string error;
		Check(EvaluateTerrainGraph(g, registry, ctx, r1, error), "it evaluates (" + error + ")");
		Check(EvaluateTerrainGraph(g, registry, ctx, r2, error) && TgHashResult(r1) == TgHashResult(r2), "the same twice");
		TerrainGraphCache cache;
		EvaluateTerrainGraph(g, registry, ctx, warm, error, &cache);
		Check(EvaluateTerrainGraph(g, registry, ctx, r3, error, &cache) && TgHashResult(r3) == TgHashResult(r1), "the same through the cache");
		if (r1.Nodes.count("fluvial") && r1.Nodes.count("ground"))
		{
			const auto& out = r1.Nodes.at("fluvial");
			const TgField& in = r1.Nodes.at("ground").at("height").Field;
			const TgField& h = out.at("height").Field;
			const TgField& eroded = out.at("eroded").Field;
			bool sum = true;
			double cut = 0.0;
			for (size_t c = 0; c < count; ++c) { sum = sum && eroded.Values[c] == in.Values[c] - h.Values[c]; cut = std::max(cut, static_cast<double>(eroded.Values[c])); }
			Check(sum && cut > 1.0, "eroded is the input minus the output, and the ground went down (" + std::to_string(cut) + " m at most)");
			Check(out.at("area").Field.Values.size() == count && out.at("coarse").Field.Values.size() == count, "area and coarse are fields of the same size");
		}
		// E2b: the node with the critical slope off (its default) gives what it gave before the parameter existed, bit for bit: the hash of
		// this graph's height recorded on master 02b8307, before the change (with diffusion and uplift, so the linear diffusion runs)
		{
			TerrainGraph lin = g;
			for (TgNode& n : lin.Nodes) if (n.Id == "fluvial") { n.Params["Diffusion"] = 0.05; n.Params["Uplift"] = 0.5; }
			TgResult rl;
			const bool ok = EvaluateTerrainGraph(lin, registry, ctx, rl, error);
			const uint64_t hash = ok && rl.Outputs.count("height") ? TgHashField(rl.Outputs.at("height")) : 0;
			Check(hash == 0x64bcc8da27be3374ull, "the node with CriticalSlope 0 (its default) gives what it gave before E2b, bit for bit");
			TerrainGraph rg = lin;
			for (TgNode& n : rg.Nodes) if (n.Id == "fluvial") n.Params["CriticalSlope"] = 30;
			TgResult ra, rb;
			const bool okA = EvaluateTerrainGraph(rg, registry, ctx, ra, error), okB = EvaluateTerrainGraph(rg, registry, ctx, rb, error);
			Check(okA && okB && TgHashResult(ra) == TgHashResult(rb), "with CriticalSlope 30 the node gives the same twice");
			Check(okA && ra.Outputs.count("height") && TgHashField(ra.Outputs.at("height")) != hash, "and CriticalSlope changes the result");
		}
		TerrainGraph bad = g;
		for (TgNode& n : bad.Nodes) if (n.Id == "fluvial") n.Params["Revision"] = 2;
		TgResult r;
		Check(!EvaluateTerrainGraph(bad, registry, ctx, r, error) && error.find("Revision") != std::string::npos, "an unknown Revision is refused, not guessed");
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("fluvial_erosion: all checks passed\n");
	return failures ? 1 : 0;
}
