#include "GB/Animation/BlendSpace.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GB
{
	GaitInfo MakeGaitInfo(const Model& model, int clip, const ClipAnalysisConfig& config)
	{
		GaitInfo info;
		const ClipStats s = AnalyzeClip(model, clip, config);
		if (!s.Valid) return info;
		info.Clip = clip;
		info.Duration = s.Duration;
		info.Velocity = glm::vec2(s.RootDelta.x, s.RootDelta.z) / s.Duration;
		info.RootStart = s.RootStart;
		info.RootDelta = s.RootDelta;
		if (!s.LeftPlants.empty() && !s.RightPlants.empty())
		{
			info.LeftPlant = s.LeftPlants.front();
			info.RightPlant = s.RightPlants.front();
		}
		return info;
	}

	size_t BlendSpace::Add(const GaitInfo& info, glm::vec2 position)
	{
		m_Entries.push_back({ info, position });
		m_TrianglesReady = false;
		return m_Entries.size() - 1;
	}

	void BlendSpace::Replace(size_t i, const GaitInfo& info, glm::vec2 position)
	{
		if (i >= m_Entries.size()) return;
		m_Entries[i] = { info, position };
		m_TrianglesReady = false;
	}

	namespace
	{
		// Barycentric coordinates of the point of triangle (a, b, c) closest to p (Ericson, Real-Time Collision Detection).
		glm::vec3 ClosestBarycentric(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c)
		{
			const glm::vec2 ab = b - a, ac = c - a, ap = p - a;
			const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
			if (d1 <= 0.0f && d2 <= 0.0f) return { 1, 0, 0 };
			const glm::vec2 bp = p - b;
			const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
			if (d3 >= 0.0f && d4 <= d3) return { 0, 1, 0 };
			const float vc = d1 * d4 - d3 * d2;
			if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) { const float v = d1 / (d1 - d3); return { 1 - v, v, 0 }; }
			const glm::vec2 cp = p - c;
			const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
			if (d6 >= 0.0f && d5 <= d6) return { 0, 0, 1 };
			const float vb = d5 * d2 - d1 * d6;
			if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) { const float w = d2 / (d2 - d6); return { 1 - w, 0, w }; }
			const float va = d3 * d6 - d5 * d4;
			if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) { const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return { 0, 1 - w, w }; }
			const float denom = 1.0f / (va + vb + vc);
			return { 1.0f - vb * denom - vc * denom, vb * denom, vc * denom };
		}

		float Area(glm::vec2 a, glm::vec2 b, glm::vec2 c) { return 0.5f * std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)); }
	}

	// Delaunay: a triangle is kept when no other position lies inside its circumcircle. The sizes here are a handful of
	// clips, so the brute force is instant. Flat triangles (nearly collinear positions) are dropped.
	void BlendSpace::BuildTriangles() const
	{
		m_Triangles.clear();
		const size_t n = m_Entries.size();
		for (size_t i = 0; i < n; ++i)
			for (size_t j = i + 1; j < n; ++j)
				for (size_t k = j + 1; k < n; ++k)
				{
					const glm::vec2 a = m_Entries[i].Position, b = m_Entries[j].Position, c = m_Entries[k].Position;
					const float area = Area(a, b, c);
					const float longest = std::max({ glm::dot(b - a, b - a), glm::dot(c - b, c - b), glm::dot(a - c, a - c) });
					if (area < 1e-4f || area / std::max(longest, 1e-6f) < 0.02f) continue;
					bool empty = true;
					for (size_t m = 0; m < n && empty; ++m)
					{
						if (m == i || m == j || m == k) continue;
						const glm::vec2 p = m_Entries[m].Position;
						// incircle determinant, sign corrected for the triangle's orientation
						const float ax = a.x - p.x, ay = a.y - p.y, bx = b.x - p.x, by = b.y - p.y, cx = c.x - p.x, cy = c.y - p.y;
						float det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) + (cx * cx + cy * cy) * (ax * by - bx * ay);
						if ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) < 0.0f) det = -det;
						if (det > 1e-4f) empty = false;
					}
					if (empty) m_Triangles.push_back({ i, j, k });
				}
		m_TrianglesReady = true;
	}

	std::vector<BlendSpace::Weight> BlendSpace::Evaluate(glm::vec2 q) const
	{
		const size_t n = m_Entries.size();
		if (n == 0) return {};
		if (n == 1) return { { 0, 1.0f } };

		// all on one line? then it is a 1D tree: blend the two neighbours along the line
		size_t farthest = 0;
		float farDist = -1.0f;
		for (size_t i = 1; i < n; ++i)
		{
			const float d = glm::distance(m_Entries[0].Position, m_Entries[i].Position);
			if (d > farDist) { farDist = d; farthest = i; }
		}
		bool collinear = true;
		for (size_t i = 0; i < n && collinear; ++i)
			if (Area(m_Entries[0].Position, m_Entries[farthest].Position, m_Entries[i].Position) > 1e-4f) collinear = false;
		if (collinear)
		{
			const glm::vec2 origin = m_Entries[0].Position, dir = farDist > 1e-6f ? (m_Entries[farthest].Position - origin) / farDist : glm::vec2(1, 0);
			std::vector<std::pair<float, size_t>> along;
			for (size_t i = 0; i < n; ++i) along.push_back({ glm::dot(m_Entries[i].Position - origin, dir), i });
			std::sort(along.begin(), along.end());
			const float s = glm::dot(q - origin, dir);
			if (s <= along.front().first) return { { along.front().second, 1.0f } };
			if (s >= along.back().first) return { { along.back().second, 1.0f } };
			for (size_t k = 0; k + 1 < along.size(); ++k)
				if (s <= along[k + 1].first)
				{
					const float span = along[k + 1].first - along[k].first;
					const float t = span > 1e-6f ? (s - along[k].first) / span : 0.0f;
					return { { along[k].second, 1.0f - t }, { along[k + 1].second, t } };
				}
			return { { along.back().second, 1.0f } };
		}

		// the triangle whose closest point to q is nearest (0 when q is inside); the smallest one on a tie
		if (!m_TrianglesReady) BuildTriangles();
		float bestDist = std::numeric_limits<float>::max(), bestArea = 0.0f;
		size_t bi = 0, bj = 0, bk = 0;
		glm::vec3 bestBary(1, 0, 0);
		bool found = false;
		for (const auto& t : m_Triangles)
		{
			const glm::vec2 a = m_Entries[t[0]].Position, b = m_Entries[t[1]].Position, c = m_Entries[t[2]].Position;
			const float area = Area(a, b, c);
			const glm::vec3 w = ClosestBarycentric(q, a, b, c);
			const float dist = glm::distance(q, a * w.x + b * w.y + c * w.z);
			if (!found || dist < bestDist - 1e-4f || (dist < bestDist + 1e-4f && area < bestArea))
			{
				found = true; bestDist = dist; bestArea = area; bi = t[0]; bj = t[1]; bk = t[2]; bestBary = w;
			}
		}
		if (!found) return { { 0, 1.0f } };
		std::vector<Weight> out;
		const size_t idx[3] = { bi, bj, bk };
		for (int c = 0; c < 3; ++c)
			if (bestBary[c] > 1e-3f) out.push_back({ idx[c], bestBary[c] });
		float sum = 0.0f;
		for (const Weight& w : out) sum += w.Value;
		for (Weight& w : out) w.Value /= sum;
		return out;
	}
}
