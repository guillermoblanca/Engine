#include "GB/World/StreamPower.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace GB
{
	namespace
	{
		double MsSince(std::chrono::steady_clock::time_point t0)
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		}

		// The source position of target sample k of m over n samples, as an integer cell and a fraction: exact (no rounding) when the grids nest.
		void Locate(uint32_t k, uint32_t n, uint32_t m, uint32_t& i0, float& t)
		{
			const uint64_t num = static_cast<uint64_t>(k) * (n - 1);
			i0 = static_cast<uint32_t>(num / (m - 1));
			t = static_cast<float>(num % (m - 1)) / static_cast<float>(m - 1);
			if (i0 >= n - 1) { i0 = n - 1; t = 0.0f; }
		}

		std::vector<uint8_t> ResampleMask(const uint8_t* mask, uint32_t n, uint32_t m)
		{
			std::vector<uint8_t> out(static_cast<size_t>(m) * m, 0);
			for (uint32_t j = 0; j < m; ++j)
				for (uint32_t i = 0; i < m; ++i)
				{
					uint32_t i0, j0;
					float ti, tj;
					Locate(i, n, m, i0, ti);
					Locate(j, n, m, j0, tj);
					const uint32_t si = std::min(n - 1, i0 + (ti >= 0.5f ? 1u : 0u)), sj = std::min(n - 1, j0 + (tj >= 0.5f ? 1u : 0u));
					out[static_cast<size_t>(j) * m + i] = mask[static_cast<size_t>(sj) * n + si];
				}
			return out;
		}

		// One stage's copy of the optional fields, at its resolution.
		struct StageFields
		{
			std::vector<float> Uplift, Hardness, Precipitation;
			std::vector<uint8_t> Outlets;
			void Make(const StreamPowerOptions& o, uint32_t n, uint32_t m)
			{
				if (o.UpliftMap) Uplift = ResampleGrid(o.UpliftMap, n, m);
				if (o.Hardness) Hardness = ResampleGrid(o.Hardness, n, m);
				if (o.Precipitation) Precipitation = ResampleGrid(o.Precipitation, n, m);
				if (o.Outlets) Outlets = ResampleMask(o.Outlets, n, m);
			}
			StreamPowerOptions Apply(StreamPowerOptions o) const
			{
				if (o.UpliftMap) o.UpliftMap = Uplift.data();
				if (o.Hardness) o.Hardness = Hardness.data();
				if (o.Precipitation) o.Precipitation = Precipitation.data();
				if (o.Outlets) o.Outlets = Outlets.data();
				return o;
			}
		};

	}

	void AccumulateMultipleFlowArea(DrainageResult& d, const float* precipitation)
	{
		const uint32_t n = d.N;
		const size_t count = static_cast<size_t>(n) * n;
		const float cell = d.Spacing * d.Spacing;
		d.Area.assign(count, 0.0f);
		if (d.Order.size() != count || d.Receiver.size() != count) return;
		std::vector<uint32_t> rank(count);
		for (size_t k = 0; k < count; ++k) rank[d.Order[k]] = static_cast<uint32_t>(k);
		constexpr int kDi[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
		constexpr int kDj[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
		// Quinn's weights: the drop over the distance times the contour length the flow crosses (half a cell straight, 0.354 diagonally)
		constexpr float kWeight[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.25f, 0.25f, 0.25f, 0.25f };   // 0.354 / sqrt 2: the slope's distance folded in
		for (size_t k = count; k-- > 0;)
		{
			const uint32_t c = d.Order[k];
			d.Area[c] += cell * (precipitation ? precipitation[c] : 1.0f);
			if (d.Receiver[c] < 0) continue;
			const int ci = static_cast<int>(c % n), cj = static_cast<int>(c / n);
			float w[8];
			float total = 0.0f;
			for (int q = 0; q < 8; ++q)
			{
				w[q] = 0.0f;
				const int ni = ci + kDi[q], nj = cj + kDj[q];
				if (ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
				const uint32_t m = static_cast<uint32_t>(nj) * n + static_cast<uint32_t>(ni);
				if (rank[m] >= rank[c] || !(d.Filled[m] < d.Filled[c])) continue;
				w[q] = (d.Filled[c] - d.Filled[m]) * kWeight[q];
				total += w[q];
			}
			if (!(total > 0.0f)) { d.Area[static_cast<size_t>(d.Receiver[c])] += d.Area[c]; continue; }
			for (int q = 0; q < 8; ++q)
				if (w[q] > 0.0f)
					d.Area[static_cast<size_t>(cj + kDj[q]) * n + static_cast<size_t>(ci + kDi[q])] += d.Area[c] * (w[q] / total);
		}
	}

	namespace
	{
		void Add(StreamPowerStats& to, const StreamPowerStats& s)
		{
			to.Iterations += s.Iterations; to.Drainages += s.Drainages; to.DiffusionSteps += s.DiffusionSteps;
			to.DrainageMs += s.DrainageMs; to.SolveMs += s.SolveMs; to.DiffusionMs += s.DiffusionMs;
			to.UpliftVolume += s.UpliftVolume; to.IncisionVolume += s.IncisionVolume; to.DiffusionVolume += s.DiffusionVolume;
			to.BorderVolume += s.BorderVolume;
		}
	}

	std::vector<float> ResampleGrid(const float* values, uint32_t n, uint32_t m)
	{
		std::vector<float> out(static_cast<size_t>(m) * m, 0.0f);
		if (n == 0 || m == 0 || !values) return out;
		if (n == 1 || m == 1) { std::fill(out.begin(), out.end(), values[0]); return out; }
		std::vector<uint32_t> ci(m);
		std::vector<float> ct(m);
		for (uint32_t k = 0; k < m; ++k) Locate(k, n, m, ci[k], ct[k]);
		for (uint32_t j = 0; j < m; ++j)
		{
			const uint32_t j0 = ci[j], j1 = std::min(n - 1, j0 + 1);
			const float tj = ct[j];
			for (uint32_t i = 0; i < m; ++i)
			{
				const uint32_t i0 = ci[i], i1 = std::min(n - 1, i0 + 1);
				const float ti = ct[i];
				const float* r0 = values + static_cast<size_t>(j0) * n;
				const float* r1 = values + static_cast<size_t>(j1) * n;
				// (a zero fraction takes the sample itself: decimation and the coinciding samples of an upsampling are exact)
				const float a = ti == 0.0f ? r0[i0] : r0[i0] + (r0[i1] - r0[i0]) * ti;
				const float b = tj == 0.0f ? a : (ti == 0.0f ? r1[i0] : r1[i0] + (r1[i1] - r1[i0]) * ti);
				out[static_cast<size_t>(j) * m + i] = tj == 0.0f ? a : a + (b - a) * tj;
			}
		}
		return out;
	}

	void StreamPowerErode(std::vector<float>& h, uint32_t n, float spacing, const StreamPowerOptions& o, StreamPowerStats* statsOut, DrainageResult* drainageOut)
	{
		StreamPowerStats stats;
		const size_t count = static_cast<size_t>(n) * n;
		if (n < 3 || h.size() != count)
		{
			if (statsOut) *statsOut = stats;
			return;
		}
		const float cell = spacing * spacing;
		const float dt = o.TimeStep;
		const float diagonal = spacing * 1.41421356f;
		DrainageOptions dopt;
		dopt.BorderOutlets = o.BorderOutlets;
		dopt.Outlets = o.Outlets;
		dopt.Precipitation = o.Precipitation;
		dopt.ReceiverMode = 1;

		// K dt per cell (the hardness lowers it) and U dt per cell: fixed for the whole run
		std::vector<float> kdt(count), udt(count);
		for (size_t c = 0; c < count; ++c)
		{
			const float hard = o.Hardness ? std::clamp(o.Hardness[c], 0.0f, 1.0f) : 0.0f;
			kdt[c] = o.Erodibility * (1.0f + (o.HardErodibility - 1.0f) * hard) * dt;
			udt[c] = o.Uplift * (o.UpliftMap ? o.UpliftMap[c] : 1.0f) * dt;
		}
		std::vector<uint8_t> fixed(count, 0);
		for (size_t c = 0; c < count; ++c)
		{
			const uint32_t i = static_cast<uint32_t>(c % n), j = static_cast<uint32_t>(c / n);
			if (o.BorderOutlets && (i == 0 || j == 0 || i == n - 1 || j == n - 1)) fixed[c] = 1;
			if (o.Outlets && o.Outlets[c]) fixed[c] = 1;
		}

		// the diffusion's sub-steps: Kd dt / dx2 per step at most 0.2 (the explicit 4-neighbour scheme is stable up to 0.25)
		const double alphaTotal = static_cast<double>(o.Diffusion) * dt / (static_cast<double>(spacing) * spacing);
		const int diffusionSteps = alphaTotal > 0.0 ? static_cast<int>(std::ceil(alphaTotal / 0.2)) : 0;
		const float alpha = diffusionSteps > 0 ? static_cast<float>(alphaTotal / diffusionSteps) : 0.0f;

		DrainageResult d;
		std::vector<float> next(count), surface(count);
		const int every = std::max(1, o.DrainageEvery);
		for (int it = 0; it < o.Iterations; ++it)
		{
			auto t0 = std::chrono::steady_clock::now();
			if (it % every == 0 || d.N == 0)
			{
				d = ComputeDrainage(h.data(), n, spacing, dopt);
				if (o.MultipleFlow) AccumulateMultipleFlowArea(d, o.Precipitation);
				++stats.Drainages;
			}
			stats.DrainageMs += MsSince(t0);

			// the incision, along the drainage order: every receiver's new height (and the surface of the water it holds) is known first
			t0 = std::chrono::steady_clock::now();
			double uplifted = 0.0, incised = 0.0;
			for (size_t k = 0; k < count; ++k)
			{
				const uint32_t c = d.Order[k];
				const int32_t r = d.Receiver[c];
				if (r < 0 || fixed[c]) { next[c] = h[c]; surface[c] = h[c]; continue; }
				const float up = h[c] + udt[c];
				uplifted += static_cast<double>(udt[c]);
				const float base = surface[static_cast<size_t>(r)];
				if (d.Level[c] > h[c] || up <= base)   // in a lake, or no slope toward the receiver: no incision
				{
					next[c] = up;
					surface[c] = std::max(up, base);
					continue;
				}
				const uint32_t ri = static_cast<uint32_t>(r) % n, rj = static_cast<uint32_t>(r) / n;
				const bool diag = ri != c % n && rj != c / n;
				const float f = kdt[c] * std::sqrt(d.Area[c]) / (diag ? diagonal : spacing);
				const float v = (up + f * base) / (1.0f + f);
				next[c] = v;
				surface[c] = v;
				incised += static_cast<double>(up - v);
			}
			stats.UpliftVolume += uplifted * cell;
			stats.IncisionVolume += incised * cell;
			h.swap(next);
			// the map's border follows the ground inside it down: a border cell never stands above its inner neighbour, so the edge of the map
			// does not become a wall where the land beside it drained elsewhere and went down (it never rises: it is still base level)
			if (o.BorderOutlets)
			{
				double lowered = 0.0;
				for (uint32_t k = 0; k < n; ++k)
				{
					const uint32_t inner = std::clamp(k, 1u, n - 2);
					const size_t cells[4][2] = { { k, static_cast<size_t>(n) + inner }, { static_cast<size_t>(n - 1) * n + k, static_cast<size_t>(n - 2) * n + inner },
						{ static_cast<size_t>(k) * n, static_cast<size_t>(inner) * n + 1 }, { static_cast<size_t>(k) * n + n - 1, static_cast<size_t>(inner) * n + n - 2 } };
					for (const auto& pair : cells)
					{
						const size_t c = pair[0], in = pair[1];
						if ((o.Outlets && o.Outlets[c]) || !(h[in] < h[c])) continue;
						lowered += static_cast<double>(h[c] - h[in]);
						h[c] = h[in];
					}
				}
				stats.BorderVolume += lowered * cell;
			}
			stats.SolveMs += MsSince(t0);

			// the hillslope diffusion: each sub-step from a copy (per cell, independent of the visiting order); the base level stays
			t0 = std::chrono::steady_clock::now();
			double before = 0.0;
			if (diffusionSteps > 0) for (size_t c = 0; c < count; ++c) before += h[c];
			for (int s = 0; s < diffusionSteps; ++s)
			{
				next = h;
				for (uint32_t j = 1; j + 1 < n; ++j)
				{
					const size_t row = static_cast<size_t>(j) * n;
					for (uint32_t i = 1; i + 1 < n; ++i)
					{
						const size_t c = row + i;
						if (fixed[c]) continue;
						const float lap = h[c - 1] + h[c + 1] + h[c - n] + h[c + n] - 4.0f * h[c];
						next[c] = h[c] + alpha * lap;
					}
				}
				h.swap(next);
				++stats.DiffusionSteps;
			}
			if (diffusionSteps > 0)
			{
				double after = 0.0;
				for (size_t c = 0; c < count; ++c) after += h[c];
				stats.DiffusionVolume += (after - before) * cell;
			}
			stats.DiffusionMs += MsSince(t0);
			++stats.Iterations;
		}
		if (drainageOut)
		{
			if (d.N == 0)   // (no iteration ran)
			{
				d = ComputeDrainage(h.data(), n, spacing, dopt);
				if (o.MultipleFlow) AccumulateMultipleFlowArea(d, o.Precipitation);
			}
			*drainageOut = std::move(d);
		}
		if (statsOut) *statsOut = stats;
	}

	StreamPowerCascadeResult StreamPowerErodeCascade(const float* heights, uint32_t n, float spacing, const StreamPowerOptions& options, const StreamPowerCascade& cascade)
	{
		StreamPowerCascadeResult result;
		const size_t count = static_cast<size_t>(n) * n;
		if (n < 3 || !heights) { result.Heights.assign(heights, heights ? heights + count : heights); return result; }
		const float extent = spacing * static_cast<float>(n - 1);

		// the stages: CoarseN, then twice as fine (2s - 1) while below N, then N
		std::vector<uint32_t> stages;
		const uint32_t coarse = std::max<uint32_t>(3, cascade.CoarseN);
		if (n <= coarse) stages.push_back(n);
		else
		{
			for (uint32_t s = coarse; s < n; s = 2 * s - 1) stages.push_back(s);
			stages.push_back(n);
		}
		result.Stages = stages;

		std::vector<float> h, inputPrev;
		uint32_t prev = 0;
		for (size_t k = 0; k < stages.size(); ++k)
		{
			const uint32_t s = stages[k];
			const bool last = k + 1 == stages.size();
			std::vector<float> input = s == n ? std::vector<float>(heights, heights + count) : ResampleGrid(heights, n, s);
			if (k == 0) h = input;
			else
			{
				// the coarser result interpolated, plus the detail this resolution has that the coarser one did not
				std::vector<float> up = ResampleGrid(h.data(), prev, s);
				const std::vector<float> upInput = ResampleGrid(inputPrev.data(), prev, s);
				for (size_t c = 0; c < up.size(); ++c) up[c] += input[c] - upInput[c];
				h.swap(up);
			}
			StageFields fields;
			if (s != n) fields.Make(options, n, s);
			StreamPowerOptions o = s != n ? fields.Apply(options) : options;
			o.Iterations = k == 0 ? cascade.CoarseIterations : last ? cascade.FineIterations : cascade.MidIterations;
			o.DrainageEvery = k == 0 ? 1 : cascade.FineDrainageEvery;
			StreamPowerStats stats;
			StreamPowerErode(h, s, extent / static_cast<float>(s - 1), o, &stats, last ? &result.Drainage : nullptr);
			Add(result.Stats, stats);
			if (k == 0) { result.Coarse = h; result.CoarseN = s; }
			inputPrev.swap(input);
			prev = s;
		}
		result.Heights.swap(h);
		return result;
	}
}
