#include "GB/Navigation/NavGrid.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>

namespace GB
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		double Since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

		constexpr uint8_t kObstacle = 1, kOutside = 2;
		constexpr float kDiagonal = 1.4142136f;
		const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		const int kDy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

		uint8_t Quantize(float v, float scale) { return static_cast<uint8_t>(std::min(std::max(v * scale, 0.0f), 255.0f) + 0.5f); }

		// The cost of a cell for a default walker, ignoring the walls (the chunks of the coarse graph only need an idea of it).
		float BaseCost(float slope, float depth, float veg)
		{
			float c = 1.0f + 2.0f * slope + 1.5f * veg;
			if (depth > 0.05f) c += 4.0f * (0.3f + depth);
			return c;
		}
	}

	size_t NavGrid::MemoryBytes(size_t* regions) const
	{
		const size_t graph = m_ChunkCost.capacity() * sizeof(float);
		if (regions) *regions = graph;
		return graph + m_Slope.capacity() + m_Depth.capacity() + m_Veg.capacity() + m_Flags.capacity() + m_Obstacles.capacity() * sizeof(CircleObstacle)
			+ m_Scratch.G.capacity() * sizeof(float) + m_Scratch.Parent.capacity() * sizeof(int) + (m_Scratch.Open.capacity() + m_Scratch.Closed.capacity()) * sizeof(uint32_t);
	}

	bool NavGrid::CellAt(const glm::vec2& p, int& i, int& j) const
	{
		const float fx = std::floor((p.x - m_Min.x) / m_Config.CellSize), fy = std::floor((p.y - m_Min.y) / m_Config.CellSize);
		if (!(fx >= 0.0f && fy >= 0.0f && fx < static_cast<float>(m_Width) && fy < static_cast<float>(m_Height))) return false;
		i = static_cast<int>(fx);
		j = static_cast<int>(fy);
		return true;
	}

	void NavGrid::Clear()
	{
		m_Width = m_Height = m_ChunkW = m_ChunkH = 0;
		m_Slope.clear(); m_Depth.clear(); m_Veg.clear(); m_Flags.clear(); m_ChunkCost.clear();
		m_Obstacles.clear(); m_Log.clear(); m_Traces.clear(); m_Cache.clear();
		m_Scratch = Scratch();
		m_Ground = nullptr;
		m_Water = nullptr;
	}

	bool NavGrid::Build(const CharacterWorld& world, const NavConfig& config, std::string* error)
	{
		const auto t0 = Clock::now();
		auto fail = [&](const char* why) { if (error) *error = why; Clear(); return false; };
		NavConfig c = config;
		c.CellSize = std::max(c.CellSize, 0.05f);
		c.ChunkCells = std::max(c.ChunkCells, 4);
		glm::vec2 lo = world.BoundsMin, hi = world.BoundsMax;
		if (c.AreaMax.x > c.AreaMin.x && c.AreaMax.y > c.AreaMin.y) { lo = c.AreaMin; hi = c.AreaMax; }
		if (!(hi.x > lo.x && hi.y > lo.y) || hi.x - lo.x > 1e6f || hi.y - lo.y > 1e6f) return fail("the world has no finite bounds (set NavConfig::AreaMin/AreaMax)");
		const float w = std::ceil((hi.x - lo.x) / c.CellSize), h = std::ceil((hi.y - lo.y) / c.CellSize);
		if (w * h > 8e6f) return fail("the area is too big for this cell size");

		Clear();
		m_Config = c;
		m_Min = lo;
		m_Width = static_cast<int>(w);
		m_Height = static_cast<int>(h);
		m_ChunkW = (m_Width + c.ChunkCells - 1) / c.ChunkCells;
		m_ChunkH = (m_Height + c.ChunkCells - 1) / c.ChunkCells;
		m_BoundsMin = world.BoundsMin;
		m_BoundsMax = world.BoundsMax;
		m_Ground = world.GroundHeight ? world.GroundHeight : std::function<float(float, float)>([](float, float) { return 0.0f; });
		m_Water = world.WaterLevel ? world.WaterLevel : std::function<float(float, float)>([](float, float) { return -1e9f; });
		m_Obstacles = world.Obstacles;
		const size_t n = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height);
		m_Slope.assign(n, 0);
		m_Depth.assign(n, 0);
		m_Veg.assign(n, 0);
		m_Flags.assign(n, 0);
		m_ChunkCost.assign(static_cast<size_t>(m_ChunkW) * static_cast<size_t>(m_ChunkH), 1.0f);
		for (int k = 0; k < 9; ++k)
			m_Signature[k] = m_Ground(lo.x + (hi.x - lo.x) * (0.1f + 0.4f * static_cast<float>(k % 3)), lo.y + (hi.y - lo.y) * (0.1f + 0.4f * static_cast<float>(k / 3)));

		ReadTerrain(0, 0, m_Width, m_Height);
		Rasterize(0, 0, m_Width, m_Height);
		RefreshChunks(0, 0, m_Width, m_Height);
		m_Revision = 0;
		++m_Stats.Builds;
		m_Stats.BuildSeconds += Since(t0);
		return true;
	}

	bool NavGrid::Matches(const CharacterWorld& world) const
	{
		if (!Ready()) return false;
		if (world.BoundsMin != m_BoundsMin || world.BoundsMax != m_BoundsMax) return false;
		if (!world.GroundHeight) return true;
		const glm::vec2 lo = m_Min, hi = Max();
		for (int k = 0; k < 9; ++k)
		{
			const float g = world.GroundHeight(lo.x + (hi.x - lo.x) * (0.1f + 0.4f * static_cast<float>(k % 3)), lo.y + (hi.y - lo.y) * (0.1f + 0.4f * static_cast<float>(k / 3)));
			if (std::fabs(g - m_Signature[k]) > 1e-3f) return false;
		}
		return true;
	}

	void NavGrid::ReadTerrain(int i0, int j0, int i1, int j1)
	{
		const float cs = m_Config.CellSize, half = cs * 0.5f;
		for (int j = j0; j < j1; ++j)
			for (int i = i0; i < i1; ++i)
			{
				const glm::vec2 p = CellCenter(i, j);
				const float gx = (m_Ground(p.x + half, p.y) - m_Ground(p.x - half, p.y)) / cs;
				const float gz = (m_Ground(p.x, p.y + half) - m_Ground(p.x, p.y - half)) / cs;
				const size_t k = static_cast<size_t>(Index(i, j));
				m_Slope[k] = Quantize(std::sqrt(gx * gx + gz * gz), 100.0f);
				const float surface = m_Water(p.x, p.y);
				m_Depth[k] = surface > -1e8f ? Quantize(surface - m_Ground(p.x, p.y), 100.0f) : 0;
				m_Veg[k] = m_Config.Vegetation ? Quantize(m_Config.Vegetation(p.x, p.y), 255.0f) : 0;
				const bool outside = p.x < m_BoundsMin.x || p.y < m_BoundsMin.y || p.x > m_BoundsMax.x || p.y > m_BoundsMax.y;
				m_Flags[k] = static_cast<uint8_t>((m_Flags[k] & kObstacle) | (outside ? kOutside : 0));
			}
	}

	void NavGrid::Rasterize(int i0, int j0, int i1, int j1)
	{
		for (int j = j0; j < j1; ++j)
			for (int i = i0; i < i1; ++i)
				m_Flags[static_cast<size_t>(Index(i, j))] &= static_cast<uint8_t>(~kObstacle);
		const float cs = m_Config.CellSize;
		for (const CircleObstacle& o : m_Obstacles)
		{
			const float r = o.Radius + m_Config.AgentRadius + m_Config.Margin;
			const int a = std::max(i0, static_cast<int>(std::floor((o.Center.x - r - m_Min.x) / cs)));
			const int b = std::min(i1 - 1, static_cast<int>(std::floor((o.Center.x + r - m_Min.x) / cs)));
			const int c = std::max(j0, static_cast<int>(std::floor((o.Center.y - r - m_Min.y) / cs)));
			const int d = std::min(j1 - 1, static_cast<int>(std::floor((o.Center.y + r - m_Min.y) / cs)));
			for (int j = c; j <= d; ++j)
				for (int i = a; i <= b; ++i)
				{
					const glm::vec2 q = CellCenter(i, j) - o.Center;
					if (q.x * q.x + q.y * q.y < r * r) m_Flags[static_cast<size_t>(Index(i, j))] |= kObstacle;
				}
		}
	}

	void NavGrid::RefreshChunks(int i0, int j0, int i1, int j1)
	{
		const int cc = m_Config.ChunkCells;
		for (int cj = j0 / cc; cj <= (j1 - 1) / cc; ++cj)
			for (int ci = i0 / cc; ci <= (i1 - 1) / cc; ++ci)
			{
				float sum = 0.0f;
				int open = 0;
				const int ea = std::min((ci + 1) * cc, m_Width), eb = std::min((cj + 1) * cc, m_Height);
				for (int j = cj * cc; j < eb; ++j)
					for (int i = ci * cc; i < ea; ++i)
					{
						const size_t k = static_cast<size_t>(Index(i, j));
						if (m_Flags[k]) continue;
						++open;
						sum += BaseCost(static_cast<float>(m_Slope[k]) * 0.01f, static_cast<float>(m_Depth[k]) * 0.01f, static_cast<float>(m_Veg[k]) / 255.0f);
					}
				const int total = (ea - ci * cc) * (eb - cj * cc);
				m_ChunkCost[static_cast<size_t>(cj * m_ChunkW + ci)] = open == 0 ? -1.0f : sum / static_cast<float>(open) * static_cast<float>(total) / static_cast<float>(open);
			}
	}

	void NavGrid::Log(int i0, int j0, int i1, int j1)
	{
		Change c;
		c.Revision = ++m_Revision;
		c.Min = m_Min + glm::vec2(static_cast<float>(i0), static_cast<float>(j0)) * m_Config.CellSize;
		c.Max = m_Min + glm::vec2(static_cast<float>(i1), static_cast<float>(j1)) * m_Config.CellSize;
		m_Log.push_back(c);
		if (m_Log.size() > 64) m_Log.erase(m_Log.begin());
	}

	int NavGrid::UpdateRegion(const glm::vec2& min, const glm::vec2& max)
	{
		if (!Ready()) return 0;
		const auto t0 = Clock::now();
		const float cs = m_Config.CellSize;
		const int i0 = std::max(0, static_cast<int>(std::floor((min.x - m_Min.x) / cs))), j0 = std::max(0, static_cast<int>(std::floor((min.y - m_Min.y) / cs)));
		const int i1 = std::min(m_Width, static_cast<int>(std::ceil((max.x - m_Min.x) / cs))), j1 = std::min(m_Height, static_cast<int>(std::ceil((max.y - m_Min.y) / cs)));
		if (i1 <= i0 || j1 <= j0) return 0;
		ReadTerrain(i0, j0, i1, j1);
		Rasterize(i0, j0, i1, j1);
		RefreshChunks(i0, j0, i1, j1);
		Log(i0, j0, i1, j1);
		const int cells = (i1 - i0) * (j1 - j0);
		++m_Stats.Updates;
		m_Stats.CellsUpdated += cells;
		m_Stats.UpdateSeconds += Since(t0);
		return cells;
	}

	int NavGrid::SyncObstacles(const std::vector<CircleObstacle>& now)
	{
		if (!Ready()) return 0;
		const auto t0 = Clock::now();
		auto less = [](const CircleObstacle& a, const CircleObstacle& b)
		{
			return std::tie(a.Center.x, a.Center.y, a.Radius) < std::tie(b.Center.x, b.Center.y, b.Radius);
		};
		std::vector<CircleObstacle> a = m_Obstacles, b = now;
		std::sort(a.begin(), a.end(), less);
		std::sort(b.begin(), b.end(), less);
		std::vector<CircleObstacle> changed;   // what is in one list only
		size_t x = 0, y = 0;
		while (x < a.size() || y < b.size())
		{
			if (y >= b.size() || (x < a.size() && less(a[x], b[y]))) changed.push_back(a[x++]);
			else if (x >= a.size() || less(b[y], a[x])) changed.push_back(b[y++]);
			else { ++x; ++y; }
		}
		m_Obstacles = now;
		if (changed.empty()) return 0;

		const float cs = m_Config.CellSize;
		int cells = 0;
		auto redo = [&](glm::vec2 lo, glm::vec2 hi)
		{
			const int i0 = std::max(0, static_cast<int>(std::floor((lo.x - m_Min.x) / cs))), j0 = std::max(0, static_cast<int>(std::floor((lo.y - m_Min.y) / cs)));
			const int i1 = std::min(m_Width, static_cast<int>(std::floor((hi.x - m_Min.x) / cs)) + 1), j1 = std::min(m_Height, static_cast<int>(std::floor((hi.y - m_Min.y) / cs)) + 1);
			if (i1 <= i0 || j1 <= j0) return;
			Rasterize(i0, j0, i1, j1);
			RefreshChunks(i0, j0, i1, j1);
			Log(i0, j0, i1, j1);
			cells += (i1 - i0) * (j1 - j0);
		};
		const float grow = m_Config.AgentRadius + m_Config.Margin + cs;
		if (changed.size() <= 48)
			for (const CircleObstacle& o : changed) redo(o.Center - (o.Radius + grow), o.Center + (o.Radius + grow));
		else
		{
			glm::vec2 lo(1e30f), hi(-1e30f);
			for (const CircleObstacle& o : changed) { lo = glm::min(lo, o.Center - (o.Radius + grow)); hi = glm::max(hi, o.Center + (o.Radius + grow)); }
			redo(lo, hi);
		}
		++m_Stats.Updates;
		m_Stats.CellsUpdated += cells;
		m_Stats.UpdateSeconds += Since(t0);
		return cells;
	}

	bool NavGrid::ChangedSince(uint32_t revision, const glm::vec2& a, const glm::vec2& b, float pad) const
	{
		if (revision == m_Revision) return false;
		if (m_Log.empty() || m_Log.front().Revision > revision + 1) return true;
		const glm::vec2 lo = glm::min(a, b) - pad, hi = glm::max(a, b) + pad;
		for (const Change& c : m_Log)
			if (c.Revision > revision && c.Min.x < hi.x && c.Max.x > lo.x && c.Min.y < hi.y && c.Max.y > lo.y) return true;
		return false;
	}

	float NavGrid::CostAt(int index, const NavProfile& p) const
	{
		const size_t k = static_cast<size_t>(index);
		if (m_Flags[k]) return -1.0f;
		const float slope = static_cast<float>(m_Slope[k]) * 0.01f;
		if (slope > p.MaxSlope + 0.05f) return -1.0f;
		const float depth = static_cast<float>(m_Depth[k]) * 0.01f;
		if (depth > p.MaxWadeDepth) return -1.0f;
		const float load = 1.0f + p.Load * p.LoadFactor;
		float c = 1.0f + p.SlopeWeight * slope * load + p.VegetationWeight * static_cast<float>(m_Veg[k]) * (1.0f / 255.0f);
		if (depth > 0.05f) c += p.WaterWeight * (0.3f + depth / std::max(p.MaxWadeDepth, 0.1f)) * load;
		return c;
	}

	float NavGrid::CostWith(int index, const NavProfile& p, const std::vector<CircleObstacle>* extra) const
	{
		const float c = CostAt(index, p);
		if (c < 0.0f || !extra || extra->empty()) return c;
		const glm::vec2 q = CellCenter(index % m_Width, index / m_Width);
		for (const CircleObstacle& o : *extra)
		{
			const float r = o.Radius + m_Config.AgentRadius + m_Config.Margin;
			const glm::vec2 d = q - o.Center;
			if (d.x * d.x + d.y * d.y < r * r) return -1.0f;
		}
		return c;
	}

	bool NavGrid::Walkable(const glm::vec2& p, const NavProfile& profile) const
	{
		int i, j;
		return CellAt(p, i, j) && CostAt(Index(i, j), profile) >= 0.0f;
	}

	void NavGrid::Count(long& blocked, long& costly, long& wet) const
	{
		blocked = costly = wet = 0;
		const NavProfile p;
		for (int j = 0; j < m_Height; ++j)
			for (int i = 0; i < m_Width; ++i)
			{
				const int k = Index(i, j);
				const float c = CostAt(k, p);
				if (c < 0.0f) ++blocked;
				else if (c > 1.5f) ++costly;
				if (m_Depth[static_cast<size_t>(k)] > 5) ++wet;
			}
	}

	bool NavGrid::LineClear(const NavProfile& profile, const glm::vec2& a, const glm::vec2& b, float* cost, const std::vector<CircleObstacle>* extra) const
	{
		const float len = glm::distance(a, b);
		const int steps = std::max(1, static_cast<int>(std::ceil(len / (m_Config.CellSize * 0.5f))));
		const float stepLen = len / static_cast<float>(steps);
		float total = 0.0f;
		for (int s = 0; s <= steps; ++s)
		{
			int i, j;
			if (!CellAt(a + (b - a) * (static_cast<float>(s) / static_cast<float>(steps)), i, j)) return false;
			const float c = CostWith(Index(i, j), profile, extra);
			if (c < 0.0f) return false;
			total += c * stepLen;
		}
		if (cost) *cost = total;
		return true;
	}

	int NavGrid::NearestWalkable(int ci, int cj, const NavProfile& profile, const std::vector<CircleObstacle>* extra, int rings) const
	{
		for (int r = 1; r <= rings; ++r)
		{
			int best = -1;
			int bestD = 1 << 30;
			for (int dj = -r; dj <= r; ++dj)
				for (int di = -r; di <= r; ++di)
				{
					if (std::max(std::abs(di), std::abs(dj)) != r) continue;
					const int i = ci + di, j = cj + dj;
					if (i < 0 || j < 0 || i >= m_Width || j >= m_Height) continue;
					const int d = di * di + dj * dj;
					if (d < bestD && CostWith(Index(i, j), profile, extra) >= 0.0f) { bestD = d; best = Index(i, j); }
				}
			if (best >= 0) return best;
		}
		return -1;
	}

	bool NavGrid::Corridor(int start, int goal, std::vector<uint8_t>& mask) const
	{
		const int n = m_ChunkW * m_ChunkH;
		const int s = ChunkIndex(start % m_Width, start / m_Width), g = ChunkIndex(goal % m_Width, goal / m_Width);
		if (m_ChunkCost[static_cast<size_t>(s)] < 0.0f || m_ChunkCost[static_cast<size_t>(g)] < 0.0f) return false;
		std::vector<float> dist(static_cast<size_t>(n), 1e30f);
		std::vector<int> parent(static_cast<size_t>(n), -1);
		std::vector<uint8_t> done(static_cast<size_t>(n), 0);
		using Node = std::pair<float, int>;
		std::vector<Node> heap;
		auto h = [&](int c) { return std::hypot(static_cast<float>(c % m_ChunkW - g % m_ChunkW), static_cast<float>(c / m_ChunkW - g / m_ChunkW)); };
		dist[static_cast<size_t>(s)] = 0.0f;
		heap.push_back({ h(s), s });
		while (!heap.empty())
		{
			std::pop_heap(heap.begin(), heap.end(), std::greater<Node>());
			const int cur = heap.back().second;
			heap.pop_back();
			if (done[static_cast<size_t>(cur)]) continue;
			done[static_cast<size_t>(cur)] = 1;
			if (cur == g) break;
			const int cx = cur % m_ChunkW, cy = cur / m_ChunkW;
			for (int d = 0; d < 8; ++d)
			{
				const int nx = cx + kDx[d], ny = cy + kDy[d];
				if (nx < 0 || ny < 0 || nx >= m_ChunkW || ny >= m_ChunkH) continue;
				const int nb = ny * m_ChunkW + nx;
				const float c = m_ChunkCost[static_cast<size_t>(nb)];
				if (c < 0.0f || done[static_cast<size_t>(nb)]) continue;
				const float step = (d >= 4 ? kDiagonal : 1.0f) * 0.5f * (c + std::max(m_ChunkCost[static_cast<size_t>(cur)], 1.0f));
				if (dist[static_cast<size_t>(cur)] + step < dist[static_cast<size_t>(nb)])
				{
					dist[static_cast<size_t>(nb)] = dist[static_cast<size_t>(cur)] + step;
					parent[static_cast<size_t>(nb)] = cur;
					heap.push_back({ dist[static_cast<size_t>(nb)] + h(nb), nb });
					std::push_heap(heap.begin(), heap.end(), std::greater<Node>());
				}
			}
		}
		if (!done[static_cast<size_t>(g)]) return false;
		mask.assign(static_cast<size_t>(n), 0);
		for (int c = g; c >= 0; c = parent[static_cast<size_t>(c)])   // the chunks of the route and the ring around them
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int nx = c % m_ChunkW + dx, ny = c / m_ChunkW + dy;
					if (nx >= 0 && ny >= 0 && nx < m_ChunkW && ny < m_ChunkH) mask[static_cast<size_t>(ny * m_ChunkW + nx)] = 1;
				}
		return true;
	}

	bool NavGrid::Search(const NavProfile& profile, int start, int goal, const std::vector<uint8_t>* corridor, const std::vector<CircleObstacle>* extra,
		std::vector<int>& cells, std::vector<float>& costs, int& expanded, int& nearest) const
	{
		Scratch& s = m_Scratch;
		const size_t n = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height);
		if (s.G.size() != n)
		{
			s.G.assign(n, 0.0f);
			s.Parent.assign(n, -1);
			s.Open.assign(n, 0);
			s.Closed.assign(n, 0);
			s.Gen = 0;
		}
		if (++s.Gen == 0) { std::fill(s.Open.begin(), s.Open.end(), 0u); std::fill(s.Closed.begin(), s.Closed.end(), 0u); s.Gen = 1; }
		const uint32_t gen = s.Gen;
		const float cs = m_Config.CellSize;
		const int gi = goal % m_Width, gj = goal / m_Width;
		auto h = [&](int idx)
		{
			const int dx = std::abs(idx % m_Width - gi), dy = std::abs(idx / m_Width - gj);
			return cs * (static_cast<float>(dx + dy) + (kDiagonal - 2.0f) * static_cast<float>(std::min(dx, dy)));
		};
		using Node = std::pair<float, int>;
		std::vector<Node> heap;
		s.G[static_cast<size_t>(start)] = 0.0f;
		s.Parent[static_cast<size_t>(start)] = -1;
		s.Open[static_cast<size_t>(start)] = gen;
		heap.push_back({ h(start), start });
		nearest = start;
		float nearestH = h(start);
		expanded = 0;
		bool found = false;
		while (!heap.empty())
		{
			std::pop_heap(heap.begin(), heap.end(), std::greater<Node>());
			const int cur = heap.back().second;
			heap.pop_back();
			if (s.Closed[static_cast<size_t>(cur)] == gen) continue;
			s.Closed[static_cast<size_t>(cur)] = gen;
			++expanded;
			if (cur == goal) { found = true; nearest = goal; break; }
			if (expanded >= m_Config.MaxExpansions) break;
			const float hc = h(cur);
			if (hc < nearestH) { nearestH = hc; nearest = cur; }
			const int ci = cur % m_Width, cj = cur / m_Width;
			const float cc = std::max(CostWith(cur, profile, extra), 1.0f);
			for (int d = 0; d < 8; ++d)
			{
				const int ni = ci + kDx[d], nj = cj + kDy[d];
				if (ni < 0 || nj < 0 || ni >= m_Width || nj >= m_Height) continue;
				const int nb = Index(ni, nj);
				if (s.Closed[static_cast<size_t>(nb)] == gen) continue;
				if (corridor && !(*corridor)[static_cast<size_t>(ChunkIndex(ni, nj))]) continue;
				const float c = CostWith(nb, profile, extra);
				if (c < 0.0f) continue;
				if (d >= 4 && (CostWith(Index(ci + kDx[d], cj), profile, extra) < 0.0f || CostWith(Index(ci, cj + kDy[d]), profile, extra) < 0.0f)) continue;   // no corner cutting
				const float ng = s.G[static_cast<size_t>(cur)] + (d >= 4 ? kDiagonal : 1.0f) * cs * 0.5f * (cc + c);
				if (s.Open[static_cast<size_t>(nb)] == gen && ng >= s.G[static_cast<size_t>(nb)]) continue;
				s.G[static_cast<size_t>(nb)] = ng;
				s.Parent[static_cast<size_t>(nb)] = cur;
				s.Open[static_cast<size_t>(nb)] = gen;
				heap.push_back({ ng + h(nb), nb });
				std::push_heap(heap.begin(), heap.end(), std::greater<Node>());
			}
		}
		cells.clear();
		costs.clear();
		for (int c = nearest; c >= 0; c = s.Parent[static_cast<size_t>(c)])
		{
			cells.push_back(c);
			costs.push_back(s.G[static_cast<size_t>(c)]);
		}
		std::reverse(cells.begin(), cells.end());
		std::reverse(costs.begin(), costs.end());
		return found;
	}

	void NavGrid::Smooth(const NavProfile& profile, const std::vector<int>& cells, const std::vector<float>& costs, const glm::vec2& from, const glm::vec2& to,
		bool exactGoal, const std::vector<CircleObstacle>* extra, NavPath& out) const
	{
		std::vector<glm::vec2> pts;
		std::vector<float> cum;
		pts.push_back(from);
		cum.push_back(0.0f);
		for (size_t k = 1; k < cells.size(); ++k)
		{
			pts.push_back(CellCenter(cells[k] % m_Width, cells[k] / m_Width));
			cum.push_back(costs[k]);
		}
		if (exactGoal)
		{
			pts.push_back(to);
			cum.push_back(cum.back() + glm::distance(pts[pts.size() - 2], to));
		}
		out.Cost = cum.back();
		// String pulling: from each anchor to the farthest point it can see without paying more than the route it replaces
		size_t anchor = 0;
		const size_t last = pts.size() - 1;
		while (anchor < last)
		{
			size_t best = anchor + 1;
			for (size_t j = anchor + 2; j <= last; ++j)
			{
				float c = 0.0f;
				if (LineClear(profile, pts[anchor], pts[j], &c, extra) && c <= (cum[j] - cum[anchor]) * 1.05f + 1e-3f) best = j;
				else break;
			}
			out.Points.push_back(pts[best]);
			anchor = best;
		}
	}

	bool NavGrid::FindPath(const NavProfile& profile, const glm::vec2& from, const glm::vec2& to, NavPath& out, const std::vector<CircleObstacle>* extra) const
	{
		const auto t0 = Clock::now();
		out = NavPath();
		++m_Stats.Paths;
		auto finish = [&](bool ok)
		{
			m_Stats.PathSeconds += Since(t0);
			m_Stats.Expanded += out.Expanded;
			m_ExpandedThisFrame += out.Expanded;   // (the frame's budget of cells, NavConfig::MaxExpandedPerFrame)
			if (!ok) ++m_Stats.Failed;
			if (m_Recording && !out.Points.empty())
			{
				std::vector<glm::vec2> t;
				t.push_back(from);
				t.insert(t.end(), out.Points.begin(), out.Points.end());
				m_Traces.push_back(std::move(t));
				if (m_Traces.size() > 64) m_Traces.erase(m_Traces.begin());
			}
			return ok;
		};
		int si, sj, gi, gj;
		if (!Ready() || !CellAt(from, si, sj) || !CellAt(to, gi, gj)) return finish(false);
		const bool caching = m_Config.PathCache > 0 && !extra;
		if (caching)
			if (const int hit = FromCache(profile, from, to, out); hit) return finish(hit == 1);

		// open ground: the straight line is the answer and no search is needed
		float straight = 0.0f;
		const float len = glm::distance(from, to);
		if (LineClear(profile, from, to, &straight, extra) && straight <= len * 1.08f + 1e-3f)
		{
			out.Points.push_back(to);
			out.Cost = straight;
			out.Found = true;
			return finish(true);
		}

		int start = Index(si, sj), goal = Index(gi, gj);
		// standing inside a wall of the grid (squeezed against a trunk) or on a cell that is a wall for this walker (water a little too deep at the
		// bank, a load that makes it so): start from the nearest free cell. Without the second case a walker that ended up there could never plan
		// again and failed every trip until it died (found by the H20 sweep: temperate seed 2, a clan of 20, day 4).
		if (m_Flags[static_cast<size_t>(start)] || CostAt(start, profile) < 0.0f)
		{
			start = NearestWalkable(si, sj, profile, nullptr, 10);
			if (start < 0) return finish(false);
		}
		bool exactGoal = true;
		if (CostWith(goal, profile, extra) < 0.0f)   // the goal is in a wall (an interaction point next to a trunk): go to the nearest free cell, then walk on
		{
			const int free = NearestWalkable(gi, gj, profile, extra, 4);
			if (free >= 0) goal = free;
			else exactGoal = false;   // (a whole block of wall: search for the nearest cell that can be reached, it ends there)
		}

		std::vector<int> cells;
		std::vector<float> costs;
		int expanded = 0, nearest = start;
		bool found = false;
		const int cellDist = std::max(std::abs(si - gi), std::abs(sj - gj));
		std::vector<uint8_t> mask;
		if (cellDist > m_Config.CoarseFrom && Corridor(start, goal, mask))
			found = Search(profile, start, goal, &mask, extra, cells, costs, expanded, nearest);
		int total = expanded;
		if (!found)
		{
			found = Search(profile, start, goal, nullptr, extra, cells, costs, expanded, nearest);
			total += expanded;
		}
		out.Expanded = total;
		out.Found = found;
		out.Partial = !found;
		if (!found && nearest == start) return finish(false);   // not a single step closer
		if (!found) exactGoal = false;
		Smooth(profile, cells, costs, from, to, exactGoal, extra, out);
		if (caching && ((found && exactGoal) || (!found && out.Expanded > 2000))) { ++m_Stats.CacheMisses; ToCache(profile, from, to, out); }   // (a long failing search is kept too)
		return finish(found);
	}

	// ------------------------------------------------------------------ the plan budget (H16)
	void NavGrid::NewFrame() const
	{
		if (m_FrameAware) m_LastFrame = m_ThisFrame;
		m_FrameAware = true;
		++m_FrameNumber;
		m_PlansThisFrame = 0;
		m_ExpandedThisFrame = 0;
		// The oldest of those turned away last frame have this frame's searches reserved. The rest are not kept here: they ask again, are turned away
		// again and keep their (old) number, so they come first as soon as the queue ahead of them is served. A reserved walker that does not come back
		// (it was removed, it stopped wanting to plan) only wastes its slot for one frame.
		std::sort(m_Waiting.begin(), m_Waiting.end());
		const size_t budget = static_cast<size_t>(std::max(m_Config.MaxPlansPerFrame, 0));
		m_Reserved.assign(m_Waiting.begin(), m_Waiting.begin() + static_cast<std::ptrdiff_t>(std::min(budget, m_Waiting.size())));
		m_ThisFrame = NavFrameStats();
		m_ThisFrame.Queue = static_cast<int>(m_Waiting.size());
		m_Stats.QueuePeak = std::max(m_Stats.QueuePeak, m_ThisFrame.Queue);
		m_Waiting.clear();
	}

	bool NavGrid::AllowPlan(PlanTicket* ticket) const
	{
		if (!m_FrameAware) return true;
		if (ticket && ticket->Id && ticket->Era != m_QueueEra) ticket->Id = 0;   // a ticket from before ResetQueue: a newcomer again
		auto grant = [&]()
		{
			++m_PlansThisFrame;
			++m_ThisFrame.Planned;
			++m_Stats.Granted;
			if (ticket && ticket->Id) { m_Stats.WaitPeak = std::max(m_Stats.WaitPeak, m_FrameNumber - ticket->Since); ticket->Id = 0; }
			return true;
		};
		// the cells of the frame are spent (NavConfig::MaxExpandedPerFrame): nobody else this frame, the reserved keep their old tickets for the next
		const bool spent = m_Config.MaxExpandedPerFrame > 0 && m_PlansThisFrame > 0 && m_ExpandedThisFrame >= m_Config.MaxExpandedPerFrame;
		if (ticket && ticket->Id && !spent)
		{
			const auto it = std::find(m_Reserved.begin(), m_Reserved.end(), ticket->Id);
			if (it != m_Reserved.end()) { m_Reserved.erase(it); return grant(); }
		}
		// what is left once the reserved searches are set aside goes to whoever asks first
		if (!spent && m_PlansThisFrame + static_cast<int>(m_Reserved.size()) < m_Config.MaxPlansPerFrame) return grant();
		++m_ThisFrame.Refused;
		++m_Stats.Refused;
		if (ticket)
		{
			if (!ticket->Id) { ticket->Id = ++m_NextTicket; if (!ticket->Id) ticket->Id = ++m_NextTicket; ticket->Since = m_FrameNumber; ticket->Era = m_QueueEra; }
			if (std::find(m_Waiting.begin(), m_Waiting.end(), ticket->Id) == m_Waiting.end()) m_Waiting.push_back(ticket->Id);
		}
		return false;
	}

	// ------------------------------------------------------------------ the path cache (H16)
	int64_t NavGrid::CacheKey(const glm::vec2& p) const
	{
		const float q = std::max(m_Config.CacheQuantum, m_Config.CellSize);
		const int64_t x = static_cast<int64_t>(std::floor(p.x / q)), z = static_cast<int64_t>(std::floor(p.y / q));
		return (x << 32) ^ (z & 0xffffffffll);
	}

	// A path searched before between the same squares of CacheQuantum metres, with the same profile, is good when nothing changed along it and the
	// walker can go straight to its first waypoint and from its last waypoint straight to the exact goal (both a little off the ones it was searched for).
	int NavGrid::FromCache(const NavProfile& profile, const glm::vec2& from, const glm::vec2& to, NavPath& out) const
	{
		const int64_t kf = CacheKey(from), kt = CacheKey(to);
		for (size_t k = 0; k < m_Cache.size(); ++k)
		{
			CachedPath& c = m_Cache[k];
			if (c.KeyFrom != kf || c.KeyTo != kt || !(c.Profile == profile)) continue;
			if (c.Revision != m_Revision)
			{
				if (ChangedSince(c.Revision, c.BoxMin, c.BoxMax, 1.0f)) { m_Cache.erase(m_Cache.begin() + static_cast<std::ptrdiff_t>(k)); return 0; }
				c.Revision = m_Revision;
			}
			if (c.Points.empty()) return 0;
			std::vector<glm::vec2> points = c.Points;
			if (c.Found) points.back() = to;   // (a path is kept only when it ended at its exact goal; a failure keeps where it got to)
			const glm::vec2 before = points.size() >= 2 ? points[points.size() - 2] : from;
			if (!LineClear(profile, from, points.front()) || (c.Found && !LineClear(profile, before, to))) return 0;
			out.Points = std::move(points);
			out.Cost = c.Cost;
			out.Found = c.Found;
			out.Partial = !c.Found;
			c.Used = ++m_CacheClock;
			++m_Stats.CacheHits;
			return c.Found ? 1 : 2;
		}
		return 0;
	}

	void NavGrid::ToCache(const NavProfile& profile, const glm::vec2& from, const glm::vec2& to, const NavPath& path) const
	{
		if (path.Points.empty() || path.Expanded == 0) return;   // the straight line costs less than a lookup
		const int64_t kf = CacheKey(from), kt = CacheKey(to);
		CachedPath* slot = nullptr;
		for (CachedPath& c : m_Cache)
			if (c.KeyFrom == kf && c.KeyTo == kt && c.Profile == profile) { slot = &c; break; }
		if (!slot)
		{
			if (m_Cache.size() < static_cast<size_t>(m_Config.PathCache)) slot = &m_Cache.emplace_back();
			else slot = &*std::min_element(m_Cache.begin(), m_Cache.end(), [](const CachedPath& a, const CachedPath& b) { return a.Used < b.Used; });
		}
		slot->KeyFrom = kf;
		slot->KeyTo = kt;
		slot->Profile = profile;
		slot->Points = path.Points;
		slot->Cost = path.Cost;
		slot->ExactGoal = path.Found;
		slot->Found = path.Found;
		slot->Revision = m_Revision;
		slot->Used = ++m_CacheClock;
		slot->BoxMin = slot->BoxMax = from;
		for (const glm::vec2& p : path.Points) { slot->BoxMin = glm::min(slot->BoxMin, p); slot->BoxMax = glm::max(slot->BoxMax, p); }
		if (!path.Found)
		{
			// a failure is forgotten when anything changes around the trip and what the search went through (a door opens, a tree falls)
			slot->BoxMin = glm::min(slot->BoxMin, to) - glm::vec2(30.0f);
			slot->BoxMax = glm::max(slot->BoxMax, to) + glm::vec2(30.0f);
		}
	}
}
