#include "GB/World/Water/WaterField.h"
#include "GB/World/PathGeometry.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	RiverSample ProbeRiverSegment(const RiverData& river, size_t k, float x, float z)
	{
		const auto& P = river.Points;
		const glm::vec2 a(P[k].x, P[k].z), ab = glm::vec2(P[k + 1].x, P[k + 1].z) - a;
		const float tt = SegmentParameter(glm::vec2(x, z), a, ab);
		RiverSample s;
		s.Distance = glm::length(glm::vec2(x, z) - (a + ab * tt));
		s.Level = glm::mix(P[k].y, P[k + 1].y, tt);
		s.Segment = k;
		s.T = tt;
		s.HalfWidth = river.HalfWidthAt(k, tt);
		s.Direction = ab;
		s.Found = true;
		return s;
	}

	RiverSample NearestRiverPoint(const RiverData& river, float x, float z)
	{
		RiverSample best;
		for (size_t k = 0; k + 1 < river.Points.size(); ++k)
		{
			const RiverSample s = ProbeRiverSegment(river, k, x, z);
			if (s.Distance < best.Distance) best = s;
		}
		return best;
	}

	void WaterField::BuildRiverIndex()
	{
		m_IndexCells.clear();
		m_IndexW = m_IndexH = 0;
		const auto& P = m_River.Points;
		if (P.size() < 2) return;
		float widest = m_River.HalfWidth;
		for (const float w : m_River.HalfWidths) widest = std::max(widest, w);
		m_IndexReach = 1.2f * widest;
		glm::vec2 lo(P[0].x, P[0].z), hi = lo;
		for (const glm::vec3& p : P) { lo = glm::min(lo, glm::vec2(p.x, p.z)); hi = glm::max(hi, glm::vec2(p.x, p.z)); }
		m_IndexMin = lo - glm::vec2(m_IndexReach + 1.0f);
		const glm::vec2 size = hi - lo + glm::vec2(2.0f * (m_IndexReach + 1.0f));
		m_IndexW = static_cast<int>(std::ceil(size.x / m_IndexCell)) + 1;
		m_IndexH = static_cast<int>(std::ceil(size.y / m_IndexCell)) + 1;
		m_IndexCells.assign(static_cast<size_t>(m_IndexW) * static_cast<size_t>(m_IndexH), {});
		for (size_t k = 0; k + 1 < P.size(); ++k)
		{
			// the cells the segment's box, grown by the reach, touches (a cell is listed whole: every point of it is covered)
			const glm::vec2 a(P[k].x, P[k].z), b(P[k + 1].x, P[k + 1].z);
			const glm::vec2 smin = glm::min(a, b) - glm::vec2(m_IndexReach) - m_IndexMin, smax = glm::max(a, b) + glm::vec2(m_IndexReach) - m_IndexMin;
			const int i0 = std::max(0, static_cast<int>(std::floor(smin.x / m_IndexCell))), i1 = std::min(m_IndexW - 1, static_cast<int>(std::floor(smax.x / m_IndexCell)));
			const int j0 = std::max(0, static_cast<int>(std::floor(smin.y / m_IndexCell))), j1 = std::min(m_IndexH - 1, static_cast<int>(std::floor(smax.y / m_IndexCell)));
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i) m_IndexCells[static_cast<size_t>(j) * static_cast<size_t>(m_IndexW) + static_cast<size_t>(i)].push_back(static_cast<unsigned>(k));
		}
	}

	RiverSample WaterField::NearestRiverInBand(float x, float z) const
	{
		// Exact where it matters: a segment within the reach of the point is filed in its cell, so if the nearest of the cell's segments is
		// within the reach it is the nearest of all (and the first of equals, as NearestRiverPoint, since the cell lists them in order).
		if (m_IndexCells.empty()) return {};
		const int i = static_cast<int>(std::floor((x - m_IndexMin.x) / m_IndexCell)), j = static_cast<int>(std::floor((z - m_IndexMin.y) / m_IndexCell));
		if (i < 0 || j < 0 || i >= m_IndexW || j >= m_IndexH) return {};
		RiverSample best;
		for (const unsigned k : m_IndexCells[static_cast<size_t>(j) * static_cast<size_t>(m_IndexW) + static_cast<size_t>(i)])
		{
			const RiverSample s = ProbeRiverSegment(m_River, k, x, z);
			if (s.Distance < best.Distance) best = s;
		}
		return best.Found && best.Distance <= m_IndexReach ? best : RiverSample{};
	}

	glm::vec2 WaterField::RiverFlow(float x, float z) const
	{
		if (m_River.Points.empty()) return glm::vec2(0.0f);
		const RiverSample s = NearestRiver(x, z);
		if (!s.Found || s.Distance > s.HalfWidth * 1.1f || glm::length(s.Direction) < 1e-6f) return glm::vec2(0.0f);
		const float speed = glm::mix(m_River.Speed[s.Segment], m_River.Speed[s.Segment + 1], s.T);
		const float edge = glm::clamp(s.Distance / s.HalfWidth, 0.0f, 1.0f);
		return glm::normalize(s.Direction) * speed * (1.0f - 0.65f * edge * edge);
	}

	float WaterField::LevelNear(float x, float z, float lakeReach, float riverReach) const
	{
		float level = kNoWaterLevel;
		if (m_Lake.Present && glm::distance(glm::vec2(x, z), m_Lake.Center) < m_Lake.Radius * lakeReach) level = m_Lake.Level;
		if (River())
		{
			const RiverSample s = NearestRiver(x, z);
			if (s.Distance < s.HalfWidth * riverReach) level = std::max(level, s.Level);
		}
		return level;
	}
}
