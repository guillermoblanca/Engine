#include "GB/World/Talus.h"
#include "GB/World/TerrainNoise.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace GB
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		// the 16 neighbours: the 8 around (1, sqrt 2) and the 8 a knight's move away (sqrt 5)
		constexpr int kDi16[16] = { 1, -1, 0, 0, 1, -1, 1, -1, 2, 2, -2, -2, 1, -1, 1, -1 };
		constexpr int kDj16[16] = { 0, 0, 1, -1, 1, 1, -1, -1, 1, -1, 1, -1, 2, 2, -2, -2 };
		constexpr float kL16[16] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f,
			2.23606798f, 2.23606798f, 2.23606798f, 2.23606798f, 2.23606798f, 2.23606798f, 2.23606798f, 2.23606798f };

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

		float SmoothStep(float a, float b, float x)
		{
			if (b <= a) return x >= b ? 1.0f : 0.0f;
			const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// The level water stands at in every cell: Priority-Flood from the border and the fixed cells (Barnes, Lehman & Mulla, 2014), ties to
		// the lower index, a FIFO for the cells that do not rise above the level -- the same levels GB/World/Drainage.h finds, without the
		// order, the receivers and the area (the talus only needs to know which cells are a lake). With the variant of Zhou, Sun & Fu (2016):
		// from a cell whose level is known, every neighbour at least as high has its own height as its level whatever the order (no path
		// through it can spill lower), so the slopes are climbed with a FIFO and only a cell with a lower neighbour still open goes to the
		// priority queue. The levels are exactly those of the plain Priority-Flood; only the work is less.
		std::vector<float> FloodLevel(const float* h, uint32_t n, const uint8_t* fixed)
		{
			const size_t count = static_cast<size_t>(n) * n;
			std::vector<float> level(h, h + count);
			std::vector<uint8_t> closed(count, 0);
			using Item = std::pair<float, uint32_t>;
			std::vector<Item> heap;
			heap.reserve(4 * n);
			std::deque<uint32_t> pit, trace;
			const auto later = std::greater<Item>();
			for (uint32_t j = 0; j < n; ++j)
				for (uint32_t i = 0; i < n; ++i)
				{
					const size_t c = static_cast<size_t>(j) * n + i;
					if (i == 0 || j == 0 || i == n - 1 || j == n - 1 || (fixed && fixed[c])) { closed[c] = 1; heap.push_back({ h[c], static_cast<uint32_t>(c) }); }
				}
			std::make_heap(heap.begin(), heap.end(), later);
			while (!heap.empty() || !pit.empty())
			{
				uint32_t c;
				if (!pit.empty()) { c = pit.front(); pit.pop_front(); }
				else { std::pop_heap(heap.begin(), heap.end(), later); c = heap.back().second; heap.pop_back(); }
				const uint32_t i = c % n, j = c / n;
				const float lc = level[c];
				for (int k = 0; k < 8; ++k)
				{
					const int ni = static_cast<int>(i) + kDi16[k], nj = static_cast<int>(j) + kDj16[k];
					if (ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
					const size_t m = static_cast<size_t>(nj) * n + static_cast<size_t>(ni);
					if (closed[m]) continue;
					closed[m] = 1;
					if (h[m] <= lc) { level[m] = lc; pit.push_back(static_cast<uint32_t>(m)); continue; }
					// climb the slope from m: its level is its height, and so is that of every open neighbour at least as high
					trace.push_back(static_cast<uint32_t>(m));
					while (!trace.empty())
					{
						const uint32_t x = trace.front();
						trace.pop_front();
						const uint32_t xi = x % n, xj = x / n;
						bool lower = false;
						for (int q = 0; q < 8; ++q)
						{
							const int yi = static_cast<int>(xi) + kDi16[q], yj = static_cast<int>(xj) + kDj16[q];
							if (yi < 0 || yj < 0 || yi >= static_cast<int>(n) || yj >= static_cast<int>(n)) continue;
							const size_t y = static_cast<size_t>(yj) * n + static_cast<size_t>(yi);
							if (closed[y]) continue;
							if (h[y] >= h[x]) { closed[y] = 1; trace.push_back(static_cast<uint32_t>(y)); }
							else lower = true;
						}
						if (lower) { heap.push_back({ h[x], x }); std::push_heap(heap.begin(), heap.end(), later); }
					}
				}
			}
			return level;
		}

		struct Grid
		{
			uint32_t N;
			float Sp;
			std::ptrdiff_t Offset[16];
			float Dist[16];

			Grid(uint32_t n, float sp) : N(n), Sp(sp)
			{
				for (int k = 0; k < 16; ++k) { Offset[k] = static_cast<std::ptrdiff_t>(kDj16[k]) * n + kDi16[k]; Dist[k] = kL16[k] * sp; }
			}

			// calls f(neighbour index, distance in metres) for the 8 neighbours of c inside the map
			template <class F> void Each8(size_t c, F&& f) const
			{
				const int i = static_cast<int>(c % N), j = static_cast<int>(c / N), n = static_cast<int>(N);
				if (i >= 1 && j >= 1 && i + 1 < n && j + 1 < n)
				{
					for (int k = 0; k < 8; ++k) f(static_cast<size_t>(static_cast<std::ptrdiff_t>(c) + Offset[k]), Dist[k]);
					return;
				}
				for (int k = 0; k < 8; ++k)
				{
					const int ni = i + kDi16[k], nj = j + kDj16[k];
					if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
					f(static_cast<size_t>(nj) * N + static_cast<size_t>(ni), Dist[k]);
				}
			}

			// calls f(neighbour index, distance in metres) for the 16 neighbours of c inside the map
			template <class F> void Each16(size_t c, F&& f) const
			{
				const int i = static_cast<int>(c % N), j = static_cast<int>(c / N), n = static_cast<int>(N);
				if (i >= 2 && j >= 2 && i + 2 < n && j + 2 < n)
				{
					for (int k = 0; k < 16; ++k) f(static_cast<size_t>(static_cast<std::ptrdiff_t>(c) + Offset[k]), Dist[k]);
					return;
				}
				for (int k = 0; k < 16; ++k)
				{
					const int ni = i + kDi16[k], nj = j + kDj16[k];
					if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
					f(static_cast<size_t>(nj) * N + static_cast<size_t>(ni), Dist[k]);
				}
			}
		};

		// Dial's buckets for a shortest path whose every step adds at least `width` (Dijkstra without a heap): a cell taken from a bucket is
		// final, because whatever it reaches lands in a later bucket, so the order inside a bucket does not change the result. Values are
		// counted from `base` in the direction the path runs (up for the cut and the trim, down for the cones).
		struct Buckets
		{
			double Base, Width, Sign;
			std::vector<std::vector<uint32_t>> List;
			Buckets(double base, double width, double span, double sign) : Base(base), Width(width), Sign(sign)
			{
				List.resize(static_cast<size_t>(std::max(0.0, span / width)) + 2);
			}
			size_t Index(float v) const
			{
				const double t = Sign * (static_cast<double>(v) - Base) / Width;
				return t <= 0.0 ? 0 : std::min(List.size() - 1, static_cast<size_t>(t));
			}
			void Push(float v, uint32_t c) { List[Index(v)].push_back(c); }
		};

		// The cells in descending order of `v`, ties in descending index (a radix sort, three passes of 11 bits on the float's bits made monotone).
		std::vector<uint32_t> DescendingOrder(const std::vector<float>& v)
		{
			const size_t count = v.size();
			std::vector<uint32_t> key(count), a(count), b(count);
			for (size_t c = 0; c < count; ++c)
			{
				uint32_t u;
				std::memcpy(&u, &v[c], sizeof u);
				key[c] = (u & 0x80000000u) ? ~u : (u | 0x80000000u);
				a[c] = static_cast<uint32_t>(c);
			}
			for (int pass = 0; pass < 3; ++pass)
			{
				const int shift = 11 * pass;
				std::vector<size_t> hist(2049, 0);
				for (size_t k = 0; k < count; ++k) ++hist[((key[a[k]] >> shift) & 2047u) + 1];
				for (size_t k = 1; k < hist.size(); ++k) hist[k] += hist[k - 1];
				for (size_t k = 0; k < count; ++k) b[hist[(key[a[k]] >> shift) & 2047u]++] = a[k];
				a.swap(b);
			}
			std::reverse(a.begin(), a.end());
			return a;
		}
	}

	TalusResult TalusErode(const float* heights, uint32_t n, float spacing, const TalusOptions& o)
	{
		TalusResult r;
		if (n < 3 || !heights) { if (heights) r.Heights.assign(heights, heights + static_cast<size_t>(n) * n); return r; }
		const size_t count = static_cast<size_t>(n) * n;
		const Grid g(n, spacing);
		const float cellArea = spacing * spacing;
		TalusStats& s = r.Stats;
		auto t0 = std::chrono::steady_clock::now();

		// lakes: the hollows of the input and the fixed cells; they stand at their water level for the cut and the trim
		const std::vector<float> level = FloodLevel(heights, n, o.Fixed);
		r.Lake.assign(count, 0);
		std::vector<float> cur(heights, heights + count);
		for (size_t c = 0; c < count; ++c)
			if ((o.Fixed && o.Fixed[c]) || level[c] > heights[c] + 1e-4f) { r.Lake[c] = 1; cur[c] = std::max(level[c], heights[c]); ++s.LakeCells; }
		const auto [lowIt, highIt] = std::minmax_element(cur.begin(), cur.end());
		const float low = *lowIt, high = *highIt;
		s.FloodMs = Ms(t0);

		// 2. the cut: a shortest path up from the low ground, each step costing tan(the upper cell's angle) * the distance
		t0 = std::chrono::steady_clock::now();
		const double toRad = kPi / 180.0;
		const float tanSoft = static_cast<float>(std::tan(std::clamp(static_cast<double>(o.SoftAngle), 1.0, 89.0) * toRad));
		const float tanHard = static_cast<float>(std::tan(std::clamp(static_cast<double>(o.HardAngle), 1.0, 89.0) * toRad));
		const float tanScree = static_cast<float>(std::tan(std::clamp(static_cast<double>(o.ScreeAngle), 1.0, 89.0) * toRad));
		// the cut runs over the 8 neighbours, each step held to tan * cos(22.5 degrees): the ground it leaves is an octagonal cone whose slope
		// is between 0.924 tan and tan in every direction, never steeper than the angle (with tan itself, a slope facing 22.5 degrees off
		// the axes would stand 8 % steeper). The knight's moves would round the cone, but they reach over a hard ledge to bound the soft rock
		// behind it by the ground beyond, and leave a trench there.
		constexpr float kOctagon = 0.92387953f;
		std::vector<float> tanAt(count, tanSoft * kOctagon);
		if (o.Hardness)
			for (size_t c = 0; c < count; ++c) tanAt[c] = (tanSoft + (tanHard - tanSoft) * std::clamp(o.Hardness[c], 0.0f, 1.0f)) * kOctagon;
		const float tanMin = std::min(tanSoft, tanHard) * kOctagon;
		std::vector<uint8_t> done(count, 0);
		{
			Buckets q(low, 0.999 * tanMin * spacing, static_cast<double>(high) - low, 1.0);
			std::vector<std::pair<float, uint32_t>> first;
			for (size_t c = 0; c < count; ++c)
			{
				if (r.Lake[c]) continue;
				float best = cur[c];
				const float t = tanAt[c];
				g.Each8(c, [&](size_t m, float L) { best = std::min(best, cur[m] + t * L); });
				if (best < cur[c]) first.push_back({ best, static_cast<uint32_t>(c) });
			}
			for (const auto& [v, c] : first) { cur[c] = v; q.Push(v, c); }
			for (size_t b = 0; b < q.List.size(); ++b)
				for (size_t k = 0; k < q.List[b].size(); ++k)
				{
					const uint32_t c = q.List[b][k];
					if (done[c]) continue;
					done[c] = 1;
					const float v = cur[c];
					g.Each8(c, [&](size_t m, float L)
					{
						if (r.Lake[m] || done[m]) return;
						const float cand = v + tanAt[m] * L;
						if (cand < cur[m]) { cur[m] = cand; q.Push(cand, static_cast<uint32_t>(m)); }
					});
				}
		}
		r.Cut.assign(count, 0.0f);
		for (size_t c = 0; c < count; ++c)
			if (!r.Lake[c] && cur[c] < heights[c]) { r.Cut[c] = heights[c] - cur[c]; ++s.CutCells; s.CutVolume += static_cast<double>(r.Cut[c]) * cellArea; }
		s.CutMs = Ms(t0);

		// 3. the fall, along the steepest descent of the cut ground (D8), from the highest cell down
		t0 = std::chrono::steady_clock::now();
		auto border = [n](size_t c) { const size_t i = c % n, j = c / n; return i == 0 || j == 0 || i == n - 1 || j == n - 1; };
		const std::vector<uint32_t> order = DescendingOrder(cur);
		std::vector<float> area(count, 0.0f);
		std::vector<double> load(count, 0.0);
		std::vector<double> foot(count, 0.0);
		std::vector<uint8_t> channel(count, 0);
		std::vector<int32_t> receiver(count, -1);
		std::vector<float> steepness(count, 0.0f);   // the drop to the receiver per cell of distance (m)
		for (uint32_t j = 0; j < n; ++j)
			for (uint32_t i = 0; i < n; ++i)
			{
				const size_t c = static_cast<size_t>(j) * n + i;
				if (r.Lake[c] || (o.BorderOutlets && border(c))) continue;
				int32_t to = -1;
				float steepest = 0.0f;
				for (int k = 0; k < 8; ++k)
				{
					const int ni = static_cast<int>(i) + kDi16[k], nj = static_cast<int>(j) + kDj16[k];
					if (ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
					const size_t m = static_cast<size_t>(nj) * n + static_cast<size_t>(ni);
					const float drop = (cur[c] - cur[m]) / kL16[k];
					if (drop > steepest) { steepest = drop; to = static_cast<int32_t>(m); }
				}
				receiver[c] = to;
				steepness[c] = steepest;
			}
		for (const uint32_t c : order)
		{
			area[c] += cellArea;
			load[c] += static_cast<double>(r.Cut[c]) * cellArea;
			if (r.Lake[c]) { s.LakeVolume += load[c]; continue; }
			if (o.BorderOutlets && border(c)) { s.BorderVolume += load[c]; continue; }
			const int32_t to = receiver[c];
			if (to >= 0) area[static_cast<size_t>(to)] += area[c];
			if (o.ChannelArea > 0.0f && area[c] >= o.ChannelArea) { channel[c] = 1; ++s.ChannelCells; s.ChannelVolume += load[c]; continue; }
			if (to < 0 || steepness[c] < tanScree * spacing) { foot[c] += load[c]; continue; }   // eases under the scree's angle (or a flat): a foot
			load[static_cast<size_t>(to)] += load[c];
		}
		for (size_t c = 0; c < count; ++c) if (foot[c] > 0.0) { ++s.Feet; s.FootVolume += foot[c]; }
		s.FallMs = Ms(t0);

		// 4. the scree: cones of the scree's angle over the feet, merged, trimmed to that angle over what cannot hold scree, fit to the volume
		t0 = std::chrono::steady_clock::now();
		std::vector<float> out = cur;
		std::vector<uint8_t> hold(count, 0);
		for (size_t c = 0; c < count; ++c) hold[c] = !r.Lake[c] && !channel[c] && !(o.BorderOutlets && border(c));
		for (size_t c = 0; c < count; ++c)   // nor next to a lake: its shore holds the lake's spill point
			if (r.Lake[c]) g.Each16(c, [&](size_t m, float L) { if (L < 1.5f * spacing) hold[m] = 0; });
		// the scree's cones and trim run over the 8 neighbours like the cut, each step at tan * cos(22.5 degrees): never steeper than its angle
		const float tanScree8 = tanScree * kOctagon;
		const float stepMin = 0.999f * tanScree8 * spacing;
		std::vector<float> top(count);
		// the drainage kept: along the steepest descent of the cut ground every cell stays strictly over the one it drains to, so the scree
		// dams nothing (a cell no higher than one that drains into it is lowered under it, never under its rock: the cut ground drains).
		// Returns the volume of the scree left.
		auto keepDrainage = [&](std::vector<float>& f) -> double
		{
			for (const uint32_t c : order)
			{
				const int32_t to = receiver[c];
				if (to < 0 || r.Lake[c]) continue;
				const size_t m = static_cast<size_t>(to);
				if (f[m] >= f[c]) f[m] = std::max(cur[m], std::nextafter(f[c], -std::numeric_limits<float>::infinity()));
			}
			double vol = 0.0;
			for (size_t c = 0; c < count; ++c) if (f[c] > cur[c]) vol += static_cast<double>(f[c] - cur[c]) * cellArea;
			return vol;
		};
		auto lay = [&](double factor, std::vector<float>& f) -> double
		{
			f = cur;
			// the cones: the highest surface under the apexes falling at the scree's angle, while it stays over the ground (Dial's buckets down
			// from the highest apex)
			std::fill(top.begin(), top.end(), -std::numeric_limits<float>::infinity());
			std::fill(done.begin(), done.end(), 0);
			std::vector<std::pair<float, uint32_t>> apex;
			float highest = -std::numeric_limits<float>::infinity();
			for (size_t c = 0; c < count; ++c)
			{
				if (foot[c] <= 0.0 || !hold[c]) continue;
				// a foot along the foot of a wall (two feet or more among its 8 neighbours) lays a wedge against it, one cell long:
				// V = y^2 / (2 tan) * spacing; a lone foot a half cone: V = pi y^3 / (6 tan^2)
				int beside = 0;
				const uint32_t ci = static_cast<uint32_t>(c % n), cj = static_cast<uint32_t>(c / n);
				for (int k = 0; k < 8; ++k)
				{
					const int ni = static_cast<int>(ci) + kDi16[k], nj = static_cast<int>(cj) + kDj16[k];
					if (ni >= 0 && nj >= 0 && ni < static_cast<int>(n) && nj < static_cast<int>(n) && foot[static_cast<size_t>(nj) * n + static_cast<size_t>(ni)] > 0.0) ++beside;
				}
				const double y = factor * (beside >= 2 ? std::sqrt(2.0 * static_cast<double>(tanScree) * foot[c] / spacing)
					: Cbrt(6.0 * static_cast<double>(tanScree) * tanScree * foot[c] / kPi));
				const float z = cur[c] + static_cast<float>(y);
				apex.push_back({ z, static_cast<uint32_t>(c) });
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
					g.Each8(c, [&](size_t m, float L)
					{
						if (!hold[m] || done[m]) return;
						const float cand = v - tanScree8 * L;
						if (cand > cur[m] && cand > top[m]) { top[m] = cand; down.Push(cand, static_cast<uint32_t>(m)); }
					});
				}
			for (size_t c = 0; c < count; ++c) if (hold[c] && top[c] > cur[c]) f[c] = top[c];
			// the trim: no scree stands steeper than its angle over any neighbour, and none goes under the rock (Dial's buckets up)
			std::fill(done.begin(), done.end(), 0);
			Buckets up(low, stepMin, static_cast<double>(std::max(highest, high)) - low, 1.0);
			std::vector<std::pair<float, uint32_t>> first;
			for (size_t c = 0; c < count; ++c)
			{
				if (f[c] <= cur[c]) continue;
				float best = f[c];
				g.Each8(c, [&](size_t m, float L) { best = std::min(best, f[m] + tanScree8 * L); });
				best = std::max(best, cur[c]);
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
					g.Each8(c, [&](size_t m, float L)
					{
						if (done[m] || f[m] <= cur[m]) return;
						const float cand = std::max(cur[m], v + tanScree8 * L);
						if (cand < f[m]) { f[m] = cand; up.Push(cand, static_cast<uint32_t>(m)); }
					});
				}
			return keepDrainage(f);
		};
		if (o.Deposit && s.FootVolume > 0.0)
		{
			// one pass: the scree holds at most what came down -- what a channel, a lake or the drainage trims off it is carried away, not
			// piled elsewhere -- and when the cones of a crowded foot hold more, every thickness is scaled down to the volume (the drainage
			// kept again after)
			const double target = s.FootVolume;
			std::vector<float> bestF;
			double vol = lay(1.0, bestF);
			s.FitPasses = 1;
			s.ApexFactor = 1.0;
			// more passes with lower apexes while the scree holds more than came down. The volume goes about with the cube of the apex (a
			// cone; a wedge, the square), so the factor is found by the secant on the cube root of the volume, from the two last passes; at
			// most three more passes, until the volume is within 3 % under the target. The last one over is scaled down to it below.
			if (vol > target * 1.0005)
			{
				const double goal = Cbrt(target);
				double fa = 1.0, ga = Cbrt(vol), fb = 0.0, gb = 0.0;
				double lo = 0.0, loVol = 0.0, hi = 1.0, hiVol = vol;
				std::vector<float> overF = bestF, underF;
				for (int pass = 0; pass < 3; ++pass)
				{
					double factor = fb > 0.0 && std::abs(ga - gb) > 1e-12 ? fb + (fa - fb) * (goal - gb) / (ga - gb) : fa * goal / ga;
					const double floor = lo > 0.0 ? lo : 0.0;
					if (!(factor > floor && factor < hi)) factor = 0.5 * (floor + hi);   // out of the bracket: halve it
					std::vector<float> f;
					const double v = lay(factor, f);
					++s.FitPasses;
					fb = fa; gb = ga; fa = factor; ga = Cbrt(v);
					if (v > target * 1.0005) { hi = factor; hiVol = v; overF = std::move(f); }
					else { lo = factor; loVol = v; underF = std::move(f); if (v >= target * 0.97) break; }
				}
				if (lo > 0.0 && loVol >= target * 0.97) { bestF = std::move(underF); vol = loVol; s.ApexFactor = lo; }
				else { bestF = std::move(overF); vol = hiVol; s.ApexFactor = hi; }
			}
			if (vol > target * 1.0005)
			{
				const double k = target / vol;
				for (size_t c = 0; c < count; ++c) if (bestF[c] > cur[c]) bestF[c] = cur[c] + static_cast<float>((bestF[c] - cur[c]) * k);
				vol = keepDrainage(bestF);
				s.ApexFactor *= k;
			}
			out = std::move(bestF);
		}
		r.Scree.assign(count, 0.0f);
		for (size_t c = 0; c < count; ++c)
			if (out[c] > cur[c]) { r.Scree[c] = out[c] - cur[c]; ++s.ScreeCells; s.ScreeVolume += static_cast<double>(r.Scree[c]) * cellArea; }
		for (size_t c = 0; c < count; ++c) if (r.Lake[c]) out[c] = heights[c];   // lakes do not move
		r.Heights = std::move(out);
		s.ScreeMs = Ms(t0);
		return r;
	}

	std::vector<float> TalusHardness(const float* heights, uint32_t n, float spacing, float originX, float originZ, const TalusHardnessParams& p)
	{
		const size_t count = static_cast<size_t>(n) * n;
		std::vector<float> hard(count, 0.0f);
		if (count == 0) return hard;
		const float scale = std::max(1.0f, p.NoiseScale);
		const bool strata = p.StrataSpacing > 0.0f && p.StrataCover > 0.0f;
		// the noises are smooth over `scale`: they are evaluated on a lattice of `step` samples (at most an eighth of the scale) and
		// interpolated bilinearly in between
		const uint32_t step = std::clamp(static_cast<uint32_t>(scale / (8.0f * std::max(spacing, 1e-3f))), 1u, 8u);
		const uint32_t m = (n - 1 + step - 1) / step + 1;
		auto lattice = [&](float freq, int octaves, uint32_t seed)
		{
			std::vector<float> lat(static_cast<size_t>(m) * m), out(count);
			for (uint32_t b = 0; b < m; ++b)
				for (uint32_t a = 0; a < m; ++a)
				{
					const float x = (originX + static_cast<float>(a * step) * spacing) / scale, z = (originZ + static_cast<float>(b * step) * spacing) / scale;
					lat[static_cast<size_t>(b) * m + a] = TerrainNoise::Fbm(x * freq, z * freq, octaves, seed);
				}
			for (uint32_t j = 0; j < n; ++j)
				for (uint32_t i = 0; i < n; ++i)
				{
					const uint32_t a = std::min(i / step, m - 2), b = std::min(j / step, m - 2);
					const float u = static_cast<float>(i - a * step) / static_cast<float>(step), v = static_cast<float>(j - b * step) / static_cast<float>(step);
					const size_t c00 = static_cast<size_t>(b) * m + a;
					const float top = lat[c00] + (lat[c00 + 1] - lat[c00]) * u, bottom = lat[c00 + m] + (lat[c00 + m + 1] - lat[c00 + m]) * u;
					out[static_cast<size_t>(j) * n + i] = top + (bottom - top) * v;
				}
			return out;
		};
		// the value of `values` above which `fraction` of the cells lie, and the noise's range (for the soft edge)
		auto quantile = [count](const std::vector<float>& values, float fraction, float& range)
		{
			std::vector<float> sorted = values;
			const auto [lo, hi] = std::minmax_element(sorted.begin(), sorted.end());
			range = *hi - *lo;
			const size_t k = std::min(count - 1, static_cast<size_t>(std::clamp(1.0f - fraction, 0.0f, 1.0f) * static_cast<float>(count - 1)));
			std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(k), sorted.end());
			return sorted[k];
		};
		// with `steep`, a noise is blended with the input's slope on the lattice (differences two lattice steps wide, rise over run mapped
		// to s / (1 + s)), both brought to 0..1 by their range
		std::vector<float> slopeField;
		const float steepW = std::clamp(p.Steep, 0.0f, 1.0f);
		if (steepW > 0.0f)
		{
			std::vector<float> lat(static_cast<size_t>(m) * m);
			auto hAt = [&](int a, int b)
			{
				const uint32_t i = std::min(static_cast<uint32_t>(std::clamp(a, 0, static_cast<int>(m) - 1)) * step, n - 1);
				const uint32_t j = std::min(static_cast<uint32_t>(std::clamp(b, 0, static_cast<int>(m) - 1)) * step, n - 1);
				return heights[static_cast<size_t>(j) * n + i];
			};
			const float run = 2.0f * static_cast<float>(step) * spacing;
			for (uint32_t b = 0; b < m; ++b)
				for (uint32_t a = 0; a < m; ++a)
				{
					const int ai = static_cast<int>(a), bi = static_cast<int>(b);
					const float gx = (hAt(ai + 1, bi) - hAt(ai - 1, bi)) / run, gz = (hAt(ai, bi + 1) - hAt(ai, bi - 1)) / run;
					const float sl = std::sqrt(gx * gx + gz * gz);
					lat[static_cast<size_t>(b) * m + a] = sl / (1.0f + sl);
				}
			slopeField.resize(count);
			for (uint32_t j = 0; j < n; ++j)
				for (uint32_t i = 0; i < n; ++i)
				{
					const uint32_t a = std::min(i / step, m - 2), b = std::min(j / step, m - 2);
					const float u = static_cast<float>(i - a * step) / static_cast<float>(step), v = static_cast<float>(j - b * step) / static_cast<float>(step);
					const size_t c00 = static_cast<size_t>(b) * m + a;
					const float top = lat[c00] + (lat[c00 + 1] - lat[c00]) * u, bottom = lat[c00 + m] + (lat[c00 + m + 1] - lat[c00 + m]) * u;
					slopeField[static_cast<size_t>(j) * n + i] = top + (bottom - top) * v;
				}
		}
		auto steepen = [&](std::vector<float>& values)
		{
			if (slopeField.empty()) return;
			const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
			const float nlo = *lo, nr = std::max(1e-6f, *hi - *lo);
			const auto [slo, shi] = std::minmax_element(slopeField.begin(), slopeField.end());
			const float sl0 = *slo, sr = std::max(1e-6f, *shi - *slo);
			for (size_t c = 0; c < count; ++c) values[c] = (1.0f - steepW) * (values[c] - nlo) / nr + steepW * (slopeField[c] - sl0) / sr;
		};
		if (p.Cover > 0.0f)
		{
			std::vector<float> patch = lattice(1.0f, 4, p.Seed * 7919u + 11u);
			steepen(patch);
			float range = 1.0f;
			const float thr = quantile(patch, p.Cover, range);
			const float e = 0.5f * p.Edge * range;
			for (size_t c = 0; c < count; ++c) hard[c] = p.Cover >= 1.0f ? 1.0f : SmoothStep(thr - e, thr + e, patch[c]);
		}
		if (strata)
		{
			const std::vector<float> wave = lattice(1.7f, 3, p.Seed * 7919u + 23u);
			std::vector<float> beds;
			float range = 1.0f, thr = -1.0f;
			if (p.StrataCover < 1.0f) { beds = lattice(0.6f, 3, p.Seed * 7919u + 37u); steepen(beds); thr = quantile(beds, p.StrataCover, range); }
			const float me = 0.5f * p.Edge * range;
			const float hardFrac = std::clamp(p.StrataHard, 0.0f, 1.0f), e = std::max(1e-3f, p.Edge);
			for (size_t c = 0; c < count; ++c)
			{
				const float mask = beds.empty() ? 1.0f : SmoothStep(thr - me, thr + me, beds[c]);
				if (mask <= 0.0f) continue;
				const float t = (heights[c] + p.StrataWarp * (2.0f * wave[c] - 1.0f)) / p.StrataSpacing;
				const float phase = t - std::floor(t);
				// signed distance into the hard part of the period [0, hardFrac), in fractions of a period
				const float inside = phase < hardFrac ? std::min(phase, hardFrac - phase) : -std::min(phase - hardFrac, 1.0f - phase);
				const float bed = SmoothStep(-0.5f * e, 0.5f * e, inside);
				hard[c] = std::max(hard[c], bed * mask);
			}
		}
		return hard;
	}
}
