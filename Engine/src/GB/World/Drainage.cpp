#include "GB/World/Drainage.h"
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace GB
{
	namespace
	{
		constexpr int kDi[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
		constexpr int kDj[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
		constexpr float kDist[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f };

		// At least `epsilon` above `base`, and strictly above it even where a float cannot hold the epsilon any more.
		float Above(float base, float epsilon)
		{
			const float v = base + epsilon;
			return v > base ? v : std::nextafter(base, std::numeric_limits<float>::infinity());
		}
	}

	void AccumulateDrainageArea(DrainageResult& d, const float* precipitation)
	{
		const size_t count = static_cast<size_t>(d.N) * d.N;
		const float cell = d.Spacing * d.Spacing;
		d.Area.assign(count, 0.0f);
		if (d.Order.size() != count || d.Receiver.size() != count) return;
		for (size_t k = count; k-- > 0;)
		{
			const uint32_t c = d.Order[k];
			d.Area[c] += cell * (precipitation ? precipitation[c] : 1.0f);
			if (d.Receiver[c] >= 0) d.Area[static_cast<size_t>(d.Receiver[c])] += d.Area[c];
		}
	}

	DrainageResult ComputeDrainage(const float* h, uint32_t n, float spacing, const DrainageOptions& options)
	{
		DrainageResult d;
		if (n < 2 || !h) return d;
		d.N = n;
		d.Spacing = spacing;
		const size_t count = static_cast<size_t>(n) * n;
		d.Level.assign(count, 0.0f);
		d.Filled.assign(count, 0.0f);
		d.Receiver.assign(count, -1);
		d.Order.clear();
		d.Order.reserve(count);
		const float epsilon = options.Epsilon;

		std::vector<uint8_t> closed(count, 0);
		using Item = std::pair<float, uint32_t>;   // (level, flat index): ties go to the lower index
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
		std::deque<uint32_t> pit;
		auto seed = [&](size_t c)
		{
			if (closed[c]) return;
			closed[c] = 1;
			d.Level[c] = d.Filled[c] = h[c];
			open.push({ h[c], static_cast<uint32_t>(c) });
		};
		if (options.BorderOutlets)
			for (uint32_t i = 0; i < n; ++i)
			{
				seed(i); seed(static_cast<size_t>(n - 1) * n + i); seed(static_cast<size_t>(i) * n); seed(static_cast<size_t>(i) * n + n - 1);
			}
		if (options.Outlets)
			for (size_t c = 0; c < count; ++c)
				if (options.Outlets[c]) seed(c);
		if (open.empty()) seed(0);   // (no outlet at all: the first cell drains the map, so that every cell still has somewhere to go)

		std::vector<uint32_t> parent(count, 0);
		while (!open.empty() || !pit.empty())
		{
			uint32_t c;
			if (!pit.empty()) { c = pit.front(); pit.pop_front(); }
			else { c = open.top().second; open.pop(); }
			d.Order.push_back(c);
			const int ci = static_cast<int>(c % n), cj = static_cast<int>(c / n);
			for (int k = 0; k < 8; ++k)
			{
				const int ni = ci + kDi[k], nj = cj + kDj[k];
				if (ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
				const uint32_t m = static_cast<uint32_t>(nj) * n + static_cast<uint32_t>(ni);
				if (closed[m]) continue;
				closed[m] = 1;
				parent[m] = c;
				d.Receiver[m] = static_cast<int32_t>(c);
				d.Filled[m] = h[m] > d.Filled[c] ? h[m] : Above(d.Filled[c], epsilon);
				if (h[m] <= d.Level[c]) { d.Level[m] = d.Level[c]; pit.push_back(m); }   // a pit or a flat: flooded from here, breadth first
				else { d.Level[m] = h[m]; open.push({ h[m], m }); }
			}
		}

		if (options.ReceiverMode == 1)
		{
			std::vector<uint32_t> rank(count, 0);
			for (size_t k = 0; k < count; ++k) rank[d.Order[k]] = static_cast<uint32_t>(k);
			for (size_t c = 0; c < count; ++c)
			{
				if (d.Receiver[c] < 0) continue;
				const int ci = static_cast<int>(c % n), cj = static_cast<int>(c / n);
				const uint32_t p = parent[c];
				const int pi = static_cast<int>(p % n), pj = static_cast<int>(p / n);
				int best = static_cast<int>(p);
				float bestSlope = (d.Filled[c] - d.Filled[p]) / ((pi != ci && pj != cj) ? 1.41421356f : 1.0f);
				for (int k = 0; k < 8; ++k)
				{
					const int ni = ci + kDi[k], nj = cj + kDj[k];
					if (ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
					const uint32_t m = static_cast<uint32_t>(nj) * n + static_cast<uint32_t>(ni);
					if (rank[m] >= rank[c] || !(d.Filled[m] < d.Filled[c])) continue;
					const float slope = (d.Filled[c] - d.Filled[m]) / kDist[k];
					if (slope > bestSlope) { bestSlope = slope; best = static_cast<int>(m); }
				}
				d.Receiver[c] = best;
			}
		}
		AccumulateDrainageArea(d, options.Precipitation);
		return d;
	}
}
