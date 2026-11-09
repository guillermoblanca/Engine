#pragma once
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace GB
{
	// The shape of a lake's shore. A real lake is never a circle: its radius changes with the direction, in a few broad lobes. The outline is
	// the radius as a function of the angle around the centre,
	//
	//      r(theta) = R (1 + a N(theta)),      N(theta) = (0.55 sin(2 theta + p1) + 0.35 sin(3 theta + p2) + 0.25 sin(5 theta + p3)) / 1.15,
	//
	// a sum of low harmonics, so |N| <= 1 and the radius stays within R (1 +- a), smooth (it has no corners) and different for each seed through
	// the phases. With a = 0 (the default, and what generators 1 and 2 use) the outline is the circle they always had, to the bit.
	struct LakeOutline
	{
		float Irregularity = 0.0f;   // a: the largest relative change of the radius (0 = a circle; 0.2 is a clearly lobed lake)
		float Phase1 = 0.0f, Phase2 = 0.0f, Phase3 = 0.0f;   // radians

		bool IsCircle() const { return Irregularity == 0.0f; }

		// r(theta) / R.
		float RadiusFactor(float theta) const
		{
			if (IsCircle()) return 1.0f;
			const float n = (0.55f * std::sin(2.0f * theta + Phase1) + 0.35f * std::sin(3.0f * theta + Phase2) + 0.25f * std::sin(5.0f * theta + Phase3)) / 1.15f;
			return 1.0f + Irregularity * n;
		}

		// The distance from the centre to `p`, measured in the circle of the same lake that would have the same shoreline: dividing by the radius
		// factor of the direction makes every formula written for a circle of radius R (basins, banks, the river's mouth) work on the lobed shore.
		float EffectiveDistance(const glm::vec2& p, const glm::vec2& centre) const
		{
			const glm::vec2 d = p - centre;
			const float len = glm::length(d);
			if (IsCircle() || len < 1e-6f) return len;
			return len / RadiusFactor(std::atan2(d.y, d.x));
		}
	};

	// An outline whose phases come from `seed`: the same seed, the same lake.
	inline LakeOutline MakeLakeOutline(uint32_t seed, float irregularity)
	{
		auto phase = [seed](uint32_t salt)
		{
			uint32_t h = (seed ^ (salt * 2654435761u)) * 2246822519u;
			h = (h ^ (h >> 15)) * 3266489917u;
			h ^= h >> 13;
			return static_cast<float>(h & 0xFFFFu) / 65535.0f * 6.2831853f;
		};
		LakeOutline o;
		o.Irregularity = irregularity;
		o.Phase1 = phase(1u); o.Phase2 = phase(2u); o.Phase3 = phase(3u);
		return o;
	}
}
