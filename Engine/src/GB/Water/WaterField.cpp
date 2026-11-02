#include "GB/Water/WaterField.h"
#include <algorithm>

namespace GB
{
	RiverSample ProbeRiverSegment(const RiverData& river, size_t k, float x, float z)
	{
		const auto& P = river.Points;
		const glm::vec2 a(P[k].x, P[k].z), ab = glm::vec2(P[k + 1].x, P[k + 1].z) - a;
		const float tt = glm::clamp(glm::dot(glm::vec2(x, z) - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
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
