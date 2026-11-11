#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace GB
{
	// The segments of a polyline filed by square cells, so that "which segment is nearest to this point" stops being a walk over the whole line
	// for every sample of a map (T125: the biome classification and the river's carving did that a million times over thousands of segments).
	// Segment k (from point k to point k + 1) is filed in every cell its box, grown by `reach`, touches, and each cell lists its segments in
	// ascending order. So every segment within `reach` of a point is among Candidates() of that point, in the order a walk over the whole line
	// meets them: a caller that keeps the first strictly nearest of them gets the walk's answer -- the same segment, the first of equals --
	// whenever that answer lies within `reach`. Past it the caller must treat the point as having no segment near; give `reach` some slack
	// (a metre) over the distance that matters, so float rounding at a cell's edge never drops a segment that counts.
	class SegmentGrid
	{
	public:
		SegmentGrid() = default;
		SegmentGrid(const std::vector<glm::vec2>& points, float reach, float cell = 16.0f) { Build(points, reach, cell); }

		void Build(const std::vector<glm::vec2>& points, float reach, float cell = 16.0f)
		{
			m_W = m_H = 0;
			m_Start.clear();
			m_Items.clear();
			if (points.size() < 2 || !(cell > 0.0f)) return;
			m_Cell = cell;
			reach = std::max(reach, 0.0f);
			glm::vec2 lo = points[0], hi = points[0];
			for (const glm::vec2& p : points) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
			m_Min = lo - glm::vec2(reach + cell);
			const glm::vec2 size = hi - lo + glm::vec2(2.0f * (reach + cell));
			m_W = static_cast<int>(std::ceil(size.x / cell)) + 1;
			m_H = static_cast<int>(std::ceil(size.y / cell)) + 1;
			// two passes (count, then fill) into one flat list: cell c's segments are m_Items[m_Start[c] .. m_Start[c + 1])
			m_Start.assign(static_cast<size_t>(m_W) * static_cast<size_t>(m_H) + 1, 0u);
			auto cells = [&](size_t k, auto&& visit)
			{
				const glm::vec2 a = points[k], b = points[k + 1];
				const glm::vec2 smin = glm::min(a, b) - glm::vec2(reach) - m_Min, smax = glm::max(a, b) + glm::vec2(reach) - m_Min;
				const int i0 = std::max(0, static_cast<int>(std::floor(smin.x / cell))), i1 = std::min(m_W - 1, static_cast<int>(std::floor(smax.x / cell)));
				const int j0 = std::max(0, static_cast<int>(std::floor(smin.y / cell))), j1 = std::min(m_H - 1, static_cast<int>(std::floor(smax.y / cell)));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i) visit(static_cast<size_t>(j) * static_cast<size_t>(m_W) + static_cast<size_t>(i));
			};
			for (size_t k = 0; k + 1 < points.size(); ++k) cells(k, [&](size_t c) { ++m_Start[c + 1]; });
			for (size_t c = 1; c < m_Start.size(); ++c) m_Start[c] += m_Start[c - 1];
			m_Items.resize(m_Start.back());
			std::vector<uint32_t> fill(m_Start.begin(), m_Start.end() - 1);
			for (size_t k = 0; k + 1 < points.size(); ++k) cells(k, [&](size_t c) { m_Items[fill[c]++] = static_cast<uint32_t>(k); });
		}

		bool Empty() const { return m_Items.empty(); }
		// The segments filed in the cell of (x, z), in ascending order; none outside the grid (no segment is within `reach` there).
		std::span<const uint32_t> Candidates(float x, float z) const
		{
			if (m_W == 0) return {};
			const int i = static_cast<int>(std::floor((x - m_Min.x) / m_Cell)), j = static_cast<int>(std::floor((z - m_Min.y) / m_Cell));
			if (i < 0 || j < 0 || i >= m_W || j >= m_H) return {};
			const size_t c = static_cast<size_t>(j) * static_cast<size_t>(m_W) + static_cast<size_t>(i);
			return { m_Items.data() + m_Start[c], m_Items.data() + m_Start[c + 1] };
		}

	private:
		float m_Cell = 16.0f;
		glm::vec2 m_Min{ 0.0f };
		int m_W = 0, m_H = 0;
		std::vector<uint32_t> m_Start;   // W*H + 1 offsets into m_Items
		std::vector<uint32_t> m_Items;   // segment indices, cell by cell, ascending within a cell
	};
}
