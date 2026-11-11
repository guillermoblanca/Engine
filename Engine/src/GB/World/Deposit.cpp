#include "GB/World/Deposit.h"
#include "GB/World/Drainage.h"
#include "GB/World/StreamPower.h"
#include "GB/World/Talus.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace GB
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr int kDi8[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
		constexpr int kDj8[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
		constexpr float kL8[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f };
		// a cone over the 8 neighbours, each step held to tan * cos(22.5 degrees): never steeper than its angle in any direction (as the talus)
		constexpr float kOctagon = 0.92387953f;

		double Ms(std::chrono::steady_clock::time_point t0)
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		}

		// The cube root by Newton's method: only + * / (exact in IEEE), so the same bits on every platform, unlike std::cbrt.
		double Cbrt(double x)
		{
			if (x <= 0.0) return 0.0;
			double y = x > 1.0 ? x / 3.0 + 0.5 : 1.0;
			for (int k = 0; k < 200; ++k)
			{
				const double next = (2.0 * y + x / (y * y)) / 3.0;
				if (next == y || (k > 8 && std::fabs(next - y) <= 1e-15 * y)) { y = next; break; }
				y = next;
			}
			return y;
		}

		float TanDegrees(float degrees)
		{
			return static_cast<float>(std::tan(std::clamp(static_cast<double>(degrees), 0.01, 89.0) * kPi / 180.0));
		}

		// calls f(neighbour index, distance in cells) for the 8 neighbours of c inside the map
		template <class F> void Each8(size_t c, uint32_t n, F&& f)
		{
			const int i = static_cast<int>(c % n), j = static_cast<int>(c / n), nn = static_cast<int>(n);
			for (int k = 0; k < 8; ++k)
			{
				const int ni = i + kDi8[k], nj = j + kDj8[k];
				if (ni < 0 || nj < 0 || ni >= nn || nj >= nn) continue;
				f(static_cast<size_t>(nj) * n + static_cast<size_t>(ni), kL8[k]);
			}
		}

		// Dial's buckets for a shortest path whose every step changes the value by at least `width` (as in the talus): a cell taken from a
		// bucket is final, so the order inside a bucket does not change the result. Values are counted from `base` in the direction the path runs.
		struct Buckets
		{
			double Base, Width, Sign;
			std::vector<std::vector<uint32_t>> List;
			Buckets(double base, double width, double span, double sign) : Base(base), Width(width), Sign(sign)
			{
				List.resize(static_cast<size_t>(std::max(0.0, span / width)) + 2);
			}
			void Push(float v, uint32_t c)
			{
				const double t = Sign * (static_cast<double>(v) - Base) / Width;
				List[t <= 0.0 ? 0 : std::min(List.size() - 1, static_cast<size_t>(t))].push_back(c);
			}
		};

		// A flat octagon, the Minkowski sum of a square of half-side `s` and a diamond of radius `d` cells (with d about s * sqrt 2 its reach is
		// nearly the same along the axes and the diagonals): the maximum (or minimum) of `in` over it around every cell, clamped at the border.
		std::vector<float> Octagon(const std::vector<float>& in, uint32_t n, int s, int d, bool maximum)
		{
			const size_t count = in.size();
			const int nn = static_cast<int>(n);
			auto pick = [maximum](float a, float b) { return maximum ? std::max(a, b) : std::min(a, b); };
			std::vector<float> a(count), b(count);
			for (int j = 0; j < nn; ++j)   // the square: along the rows, then along the columns
				for (int i = 0; i < nn; ++i)
				{
					float v = in[static_cast<size_t>(j) * n + static_cast<size_t>(i)];
					for (int k = std::max(0, i - s); k <= std::min(nn - 1, i + s); ++k) v = pick(v, in[static_cast<size_t>(j) * n + static_cast<size_t>(k)]);
					a[static_cast<size_t>(j) * n + static_cast<size_t>(i)] = v;
				}
			for (int j = 0; j < nn; ++j)
				for (int i = 0; i < nn; ++i)
				{
					float v = a[static_cast<size_t>(j) * n + static_cast<size_t>(i)];
					for (int k = std::max(0, j - s); k <= std::min(nn - 1, j + s); ++k) v = pick(v, a[static_cast<size_t>(k) * n + static_cast<size_t>(i)]);
					b[static_cast<size_t>(j) * n + static_cast<size_t>(i)] = v;
				}
			for (int step = 0; step < d; ++step)   // the diamond: the 4 neighbours, d times, each from a copy
			{
				for (int j = 0; j < nn; ++j)
					for (int i = 0; i < nn; ++i)
					{
						const size_t c = static_cast<size_t>(j) * n + static_cast<size_t>(i);
						float v = b[c];
						if (i > 0) v = pick(v, b[c - 1]);
						if (i + 1 < nn) v = pick(v, b[c + 1]);
						if (j > 0) v = pick(v, b[c - n]);
						if (j + 1 < nn) v = pick(v, b[c + n]);
						a[c] = v;
					}
				a.swap(b);
			}
			return b;
		}
	}

	DepositResult DepositSediment(const float* heights, uint32_t n, float spacing, const DepositOptions& o)
	{
		DepositResult r;
		const size_t count = static_cast<size_t>(n) * n;
		const bool easing = o.EaseAngle > 0.0f && o.EaseAngle < o.ReposeAngle, filling = o.Fill > 0.0f && o.FillRadius > 0.0f, fans = o.FanArea > 0.0f && o.FanDepth > 0.0f;
		if (!heights || n < 3 || !(easing || filling || fans))   // nothing to do: the input as it is
		{
			if (heights) r.Heights.assign(heights, heights + count);
			r.Alluvium.assign(count, 0.0f); r.Fan.assign(count, 0.0f); r.Eased.assign(count, 0.0f);
			return r;
		}
		DepositStats& s = r.Stats;
		const float cellArea = spacing * spacing;
		auto border = [n](size_t c) { const size_t i = c % n, j = c / n; return i == 0 || j == 0 || i == n - 1 || j == n - 1; };
		auto hardAt = [&](size_t c) { return o.Hardness ? std::clamp(o.Hardness[c], 0.0f, 1.0f) : 0.0f; };

		// what never moves: the hollows of the input (its lakes to be), the fixed cells, the cells beside them (a lake's spill point among
		// them) and the border
		auto t0 = std::chrono::steady_clock::now();
		DrainageOptions dopt;
		dopt.Outlets = o.Fixed;
		DrainageResult d = ComputeDrainage(heights, n, spacing, dopt);
		std::vector<uint8_t> held(count, 0), still(count, 0);
		for (size_t c = 0; c < count; ++c) held[c] = (o.Fixed && o.Fixed[c]) || d.Level[c] > heights[c] + 1e-4f;
		for (size_t c = 0; c < count; ++c)
		{
			if (border(c)) still[c] = 1;
			if (!held[c]) continue;
			still[c] = 1;
			Each8(c, n, [&](size_t m, float) { still[m] = 1; });
		}
		for (size_t c = 0; c < count; ++c) s.HeldCells += still[c];
		s.DrainageMs = Ms(t0);

		// 1. the easing: a talus whose angle eases on long slopes (what it cuts is carried away)
		std::vector<float> cur(heights, heights + count);
		r.Eased.assign(count, 0.0f);
		if (o.EaseAngle > 0.0f && o.EaseAngle < o.ReposeAngle)
		{
			t0 = std::chrono::steady_clock::now();
			DrainageResult md = d;
			AccumulateMultipleFlowArea(md, nullptr);
			const float tanR = TanDegrees(o.ReposeAngle), tanE = TanDegrees(o.EaseAngle), tanH = std::max(TanDegrees(o.HardAngle), tanR);
			const float a0 = o.EaseStart, a1 = std::max(o.EaseFull, o.EaseStart + 1e-3f);
			// the talus takes an angle per cell as a weight between its soft and hard angle: here the eased angle and the hard one
			std::vector<float> weight(count, 0.0f);
			for (size_t c = 0; c < count; ++c)
			{
				float t = std::clamp((md.Area[c] / spacing - a0) / (a1 - a0), 0.0f, 1.0f);
				t = t * t * (3.0f - 2.0f * t);
				if (o.ChannelArea > 0.0f && md.Area[c] >= o.ChannelArea) t = 0.0f;
				const float tanSoft = tanR + (tanE - tanR) * t;
				const float tanAt = tanSoft + (tanH - tanSoft) * hardAt(c);
				weight[c] = tanH > tanE ? std::clamp((tanAt - tanE) / (tanH - tanE), 0.0f, 1.0f) : 1.0f;
			}
			TalusOptions to;
			to.SoftAngle = o.EaseAngle;
			to.HardAngle = std::max(o.HardAngle, o.ReposeAngle);
			to.Hardness = weight.data();
			to.ChannelArea = o.ChannelArea;
			to.Deposit = false;
			to.Fixed = still.data();
			TalusResult tr = TalusErode(heights, n, spacing, to);
			for (size_t c = 0; c < count; ++c)
				if (!still[c] && tr.Heights[c] < cur[c]) { r.Eased[c] = cur[c] - tr.Heights[c]; cur[c] = tr.Heights[c]; s.EaseVolume += static_cast<double>(r.Eased[c]) * cellArea; }
			d = ComputeDrainage(cur.data(), n, spacing, dopt);   // the receivers the rest keeps
			s.EaseMs = Ms(t0);
		}

		// 2. the fill: every valley narrower than about twice FillRadius raised Fill of the way up to the closing of the ground
		t0 = std::chrono::steady_clock::now();
		std::vector<float> filled = cur;
		const float fill = std::clamp(o.Fill, 0.0f, 0.95f);
		if (fill > 0.0f && o.FillRadius > 0.0f)
		{
			const float radius = o.FillRadius / spacing;
			const int sq = static_cast<int>(std::lround(radius * 0.41421356f)), dia = std::max(1, static_cast<int>(std::lround(radius * 0.58578644f)));
			const std::vector<float> closing = Octagon(Octagon(cur, n, sq, dia, true), n, sq, dia, false);
			for (size_t c = 0; c < count; ++c)
			{
				if (still[c] || closing[c] <= cur[c]) continue;
				filled[c] = cur[c] + fill * (1.0f - hardAt(c)) * (closing[c] - cur[c]);
				if (filled[c] > cur[c]) ++s.FillCells;
			}
		}
		s.FillMs = Ms(t0);

		// 3. the fans
		t0 = std::chrono::steady_clock::now();
		std::vector<float> out = filled;
		std::vector<uint32_t> apexCell;
		std::vector<double> apexVolume;
		const float tanFan = TanDegrees(o.FanAngle), tanApex = TanDegrees(o.FanApexAngle);
		if (o.FanArea > 0.0f && o.FanDepth > 0.0f)
		{
			// the stem above each cell: its donor draining the most (ties to the lower index)
			std::vector<int32_t> stem(count, -1);
			for (size_t c = 0; c < count; ++c)
			{
				const int32_t to = d.Receiver[c];
				if (to < 0) continue;
				const size_t m = static_cast<size_t>(to);
				const int32_t cur2 = stem[m];
				if (cur2 < 0 || d.Area[c] > d.Area[static_cast<size_t>(cur2)]) stem[m] = static_cast<int32_t>(c);
			}
			auto step = [&](size_t a, size_t b) { return ((a % n) != (b % n) && (a / n) != (b / n) ? 1.41421356f : 1.0f) * spacing; };
			// the slope over FanReach metres down the receivers and up the stem, on the filled ground
			auto slopeDown = [&](size_t c)
			{
				size_t k = c;
				float dist = 0.0f;
				while (dist < o.FanReach && d.Receiver[k] >= 0) { const size_t m = static_cast<size_t>(d.Receiver[k]); dist += step(k, m); k = m; }
				return dist > 0.0f ? (filled[c] - filled[k]) / dist : 0.0f;
			};
			auto slopeUp = [&](size_t c)
			{
				size_t k = c;
				float dist = 0.0f;
				while (dist < o.FanReach && stem[k] >= 0) { const size_t m = static_cast<size_t>(stem[k]); dist += step(k, m); k = m; }
				return dist > 0.0f ? (filled[k] - filled[c]) / dist : 0.0f;
			};
			std::vector<uint8_t> breakAt(count, 0);
			for (size_t c = 0; c < count; ++c)
				if (!still[c] && d.Area[c] >= o.FanArea && slopeUp(c) >= tanApex && slopeDown(c) < tanFan) breakAt[c] = 1;
			for (size_t c = 0; c < count; ++c)
			{
				if (!breakAt[c]) continue;
				const int32_t up = stem[c];
				if (up >= 0 && breakAt[static_cast<size_t>(up)]) continue;   // the first cell of a run along the stem
				apexCell.push_back(static_cast<uint32_t>(c));
				apexVolume.push_back(static_cast<double>(o.FanDepth) * d.Area[c]);
				s.FanTarget += apexVolume.back();
			}
			s.Apexes = apexCell.size();
		}

		// the drainage kept: along the receivers of the ground before the deposit, every cell strictly over the one it drains to -- a receiver
		// no lower than its donor is lowered under it (never under that ground), donors first; where that ground itself is not lower (a flat),
		// the donor is raised over it instead, receivers first. The hollows and the fixed cells are left alone (their spill points with them).
		// A channel lowered so (a tributary whose old course a fan or the fill covers) would be a slot one cell wide with walls of the deposit's
		// whole thickness: its banks are cut back to kWallTan (25 degrees, loose gravel standing) from it, never under the ground before the
		// deposit. Every cell cut stands over the one it was cut from, so no hollow is made.
		constexpr float kWallTan = 0.46630766f;   // tan 25 degrees
		std::vector<uint8_t> done(count, 0), lowered(count, 0);
		std::vector<float> top(count);
		const auto [floorIt, ceilIt] = std::minmax_element(cur.begin(), cur.end());
		const float groundLow = *floorIt, groundHigh = *ceilIt;
		auto keepDrainage = [&](std::vector<float>& f)
		{
			std::fill(lowered.begin(), lowered.end(), 0);
			for (auto it = d.Order.rbegin(); it != d.Order.rend(); ++it)
			{
				const uint32_t c = *it;
				const int32_t to = d.Receiver[c];
				if (to < 0 || held[c]) continue;
				const size_t m = static_cast<size_t>(to);
				if (held[m] || still[m] || f[m] < f[c]) continue;
				const float v = std::max(cur[m], std::nextafter(f[c], -std::numeric_limits<float>::infinity()));
				if (v < f[m]) lowered[m] = 1;
				f[m] = v;
			}
			for (const uint32_t c : d.Order)
			{
				const int32_t to = d.Receiver[c];
				if (to < 0 || held[c] || still[c]) continue;
				const size_t m = static_cast<size_t>(to);
				if (f[c] <= f[m]) f[c] = std::nextafter(f[m], std::numeric_limits<float>::infinity());
			}
			// the banks of the lowered channels (Dial's buckets up from them)
			const float wall = kWallTan * spacing;
			Buckets up(groundLow, 0.999f * wall, static_cast<double>(groundHigh) - groundLow + 2000.0, 1.0);
			std::fill(done.begin(), done.end(), 0);
			for (size_t c = 0; c < count; ++c) if (lowered[c]) up.Push(f[c], static_cast<uint32_t>(c));
			for (size_t b = 0; b < up.List.size(); ++b)
				for (size_t k = 0; k < up.List[b].size(); ++k)
				{
					const uint32_t c = up.List[b][k];
					if (done[c]) continue;
					done[c] = 1;
					const float v = f[c];
					Each8(c, n, [&](size_t m, float L)
					{
						if (done[m] || held[m] || still[m] || f[m] <= cur[m]) return;
						const float cand = v + wall * L;
						if (cand < f[m]) { f[m] = std::max(cand, cur[m]); up.Push(f[m], static_cast<uint32_t>(m)); }
					});
				}
		};
		// the cones over the filled ground, a factor over every apex's height, trimmed to the fan's angle over any neighbour
		const float tan8 = tanFan * kOctagon, stepMin = 0.999f * tan8 * spacing;
		const auto [lowIt, highIt] = std::minmax_element(filled.begin(), filled.end());
		const float low = *lowIt, high = *highIt;
		auto lay = [&](double factor, std::vector<float>& f) -> double
		{
			f = filled;
			std::fill(top.begin(), top.end(), -std::numeric_limits<float>::infinity());
			std::fill(done.begin(), done.end(), 0);
			float highest = -std::numeric_limits<float>::infinity();
			std::vector<std::pair<float, uint32_t>> apex;
			for (size_t k = 0; k < apexCell.size(); ++k)
			{
				// a cone of the fan's angle holding the volume on flat ground: V = pi y^3 / (3 tan^2)
				const double y = factor * Cbrt(3.0 * static_cast<double>(tanFan) * tanFan * apexVolume[k] / kPi);
				const float z = filled[apexCell[k]] + static_cast<float>(y);
				apex.push_back({ z, apexCell[k] });
				highest = std::max(highest, z);
			}
			if (apex.empty()) return 0.0;
			Buckets down(highest, stepMin, static_cast<double>(highest) - low, -1.0);
			for (const auto& [z, c] : apex) if (z > top[c]) { top[c] = z; down.Push(z, c); }
			for (size_t b = 0; b < down.List.size(); ++b)
				for (size_t k = 0; k < down.List[b].size(); ++k)
				{
					const uint32_t c = down.List[b][k];
					if (done[c]) continue;
					done[c] = 1;
					const float v = top[c];
					Each8(c, n, [&](size_t m, float L)
					{
						if (still[m] || done[m]) return;
						const float cand = v - tan8 * L * spacing;
						if (cand > filled[m] && cand > top[m]) { top[m] = cand; down.Push(cand, static_cast<uint32_t>(m)); }
					});
				}
			for (size_t c = 0; c < count; ++c) if (!still[c] && top[c] > filled[c]) f[c] = top[c];
			// the trim: no fan stands steeper than its angle over any neighbour, and none goes under the filled ground (Dial's buckets up)
			std::fill(done.begin(), done.end(), 0);
			Buckets up(low, stepMin, static_cast<double>(std::max(highest, high)) - low, 1.0);
			std::vector<std::pair<float, uint32_t>> first;
			for (size_t c = 0; c < count; ++c)
			{
				if (f[c] <= filled[c]) continue;
				float best = f[c];
				Each8(c, n, [&](size_t m, float L) { best = std::min(best, f[m] + tan8 * L * spacing); });
				best = std::max(best, filled[c]);
				if (best < f[c]) first.push_back({ best, static_cast<uint32_t>(c) });
			}
			for (const auto& [v, c] : first) { f[c] = v; up.Push(v, c); }
			for (size_t b = 0; b < up.List.size(); ++b)
				for (size_t k = 0; k < up.List[b].size(); ++k)
				{
					const uint32_t c = up.List[b][k];
					if (done[c]) continue;
					done[c] = 1;
					const float v = f[c];
					Each8(c, n, [&](size_t m, float L)
					{
						if (done[m] || f[m] <= filled[m]) return;
						const float cand = std::max(filled[m], v + tan8 * L * spacing);
						if (cand < f[m]) { f[m] = cand; up.Push(cand, static_cast<uint32_t>(m)); }
					});
				}
			keepDrainage(f);
			double vol = 0.0;
			for (size_t c = 0; c < count; ++c) if (f[c] > filled[c]) vol += static_cast<double>(f[c] - filled[c]) * cellArea;
			return vol;
		};
		if (!apexCell.empty())
		{
			// the factor over the apexes' heights that lays the volume asked for: the volume goes about with the cube of the height, so the
			// secant on its cube root, bracketed, at most four passes, until within 3 %
			const double target = s.FanTarget, goal = Cbrt(target);
			std::vector<float> best;
			double vol = lay(1.0, best), bestErr = std::fabs(vol - target);
			s.FitPasses = 1;
			s.ApexFactor = 1.0;
			double fa = 1.0, ga = Cbrt(vol), fb = 0.0, gb = 0.0, lo = 0.0, hi = 4.0;
			if (vol > target) hi = 1.0; else lo = 1.0;
			for (int pass = 0; pass < 4 && bestErr > 0.03 * target; ++pass)
			{
				double factor = fb > 0.0 && std::fabs(ga - gb) > 1e-12 ? fa + (fb - fa) * (goal - ga) / (gb - ga) : (ga > 0.0 ? fa * goal / ga : 2.0 * fa);
				if (!(factor > lo && factor < hi)) factor = 0.5 * (lo + hi);
				std::vector<float> f;
				const double v = lay(factor, f);
				++s.FitPasses;
				fb = fa; gb = ga; fa = factor; ga = Cbrt(v);
				if (v > target) hi = factor; else lo = factor;
				if (std::fabs(v - target) < bestErr) { bestErr = std::fabs(v - target); best = std::move(f); vol = v; s.ApexFactor = factor; }
			}
			out = std::move(best);
		}
		else keepDrainage(out);
		s.FanMs = Ms(t0);

		// the outputs, and what the drainage took back (the fill and the fans less what is left)
		t0 = std::chrono::steady_clock::now();
		r.Alluvium.assign(count, 0.0f);
		r.Fan.assign(count, 0.0f);
		for (size_t c = 0; c < count; ++c)
		{
			if (held[c]) { out[c] = heights[c]; continue; }
			const float fan = out[c] - std::max(filled[c], cur[c]);
			if (fan > 0.0f) { r.Fan[c] = fan; ++s.FanCells; s.FanVolume += static_cast<double>(fan) * cellArea; }
			if (out[c] > cur[c]) r.Alluvium[c] = out[c] - cur[c];
			s.FillVolume += static_cast<double>(std::max(0.0f, std::min(out[c], filled[c]) - cur[c])) * cellArea;
			if (out[c] < filled[c]) s.DrainVolume += static_cast<double>(filled[c] - out[c]) * cellArea;
		}
		r.Heights = std::move(out);
		s.KeepMs = Ms(t0);
		return r;
	}
}
