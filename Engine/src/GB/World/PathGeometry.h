#pragma once
#include <algorithm>
#include <glm/glm.hpp>

namespace GB
{
	// Small pieces of plane geometry that the river, the lake and the valleys of the world were each writing out on their own: one place, so
	// every caller gets the same bits (the generator's output depends on them: a change here is a change of GeneratorVersion).

	// A point of the uniform Catmull-Rom spline through p0..p3 at t (0..1 between p1 and p2).
	inline glm::vec2 CatmullRom(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, float t)
	{
		const float t2 = t * t, t3 = t2 * t;
		return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
	}

	// Where the point of the segment from `a` to `a + ab` nearest to `p` is, as a fraction 0..1 of the way (0 for a segment of no length).
	inline float SegmentParameter(const glm::vec2& p, const glm::vec2& a, const glm::vec2& ab)
	{
		return glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
	}
}
