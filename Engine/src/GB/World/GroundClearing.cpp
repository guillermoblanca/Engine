#include "GB/World/GroundClearing.h"
#include "GB/World/TilledGround.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace GB
{
	bool GroundClearing::Patch::operator<(const Patch& o) const
	{
		if (std::tie(Centre.x, Centre.y, Half.x, Half.y, Yaw, ClipMargin) != std::tie(o.Centre.x, o.Centre.y, o.Half.x, o.Half.y, o.Yaw, o.ClipMargin))
			return std::tie(Centre.x, Centre.y, Half.x, Half.y, Yaw, ClipMargin) < std::tie(o.Centre.x, o.Centre.y, o.Half.x, o.Half.y, o.Yaw, o.ClipMargin);
		return std::lexicographical_compare(Clip.begin(), Clip.end(), o.Clip.begin(), o.Clip.end(),
			[](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
	}

	glm::vec4 GroundClearing::Bounds(const Patch& p, float feather)
	{
		const float r = glm::radians(p.Yaw), c = std::abs(std::cos(r)), s = std::abs(std::sin(r));
		const glm::vec2 e(p.Half.x * c + p.Half.y * s + feather, p.Half.x * s + p.Half.y * c + feather);
		return { p.Centre.x - e.x, p.Centre.y - e.y, p.Centre.x + e.x, p.Centre.y + e.y };
	}

	void GroundClearing::Set(std::vector<Patch> patches, float feather)
	{
		std::sort(patches.begin(), patches.end());
		m_Patches = std::move(patches);
		m_Feather = std::max(0.0f, feather);
		m_Axis.clear();
		m_Buckets.clear();
		for (size_t i = 0; i < m_Patches.size(); ++i)
		{
			const float r = glm::radians(m_Patches[i].Yaw);
			m_Axis.emplace_back(std::cos(r), std::sin(r));
			const glm::vec4 b = Bounds(m_Patches[i], m_Feather);
			for (int x = static_cast<int>(std::floor(b.x / kBucket)); x <= static_cast<int>(std::floor(b.z / kBucket)); ++x)
				for (int z = static_cast<int>(std::floor(b.y / kBucket)); z <= static_cast<int>(std::floor(b.w / kBucket)); ++z)
					m_Buckets[{ x, z }].push_back(static_cast<int>(i));
		}
	}

	float GroundClearing::At(float x, float z) const
	{
		if (m_Patches.empty()) return 0.0f;
		const auto it = m_Buckets.find({ static_cast<int>(std::floor(x / kBucket)), static_cast<int>(std::floor(z / kBucket)) });
		if (it == m_Buckets.end()) return 0.0f;
		float best = 0.0f;
		for (int i : it->second)
		{
			const Patch& p = m_Patches[static_cast<size_t>(i)];
			const glm::vec2 d(x - p.Centre.x, z - p.Centre.y);
			// into the patch's own axes (a yaw about +y turns x towards -z: Transform.Rotation.y, as GroundRect)
			const glm::vec2 a = m_Axis[static_cast<size_t>(i)];
			const glm::vec2 local(d.x * a.x - d.y * a.y, d.x * a.y + d.y * a.x);
			const glm::vec2 out = glm::max(glm::abs(local) - p.Half, glm::vec2(0.0f));
			float dist = glm::length(out);
			if (p.Clip.size() >= 3) dist = std::max(dist, -(OutlineDistance(p.Clip, { x, z }) + p.ClipMargin));
			if (dist <= 0.0f) return 1.0f;
			if (m_Feather <= 0.0f || dist >= m_Feather) continue;
			const float t = 1.0f - dist / m_Feather;
			best = std::max(best, t * t * (3.0f - 2.0f * t));
		}
		return best;
	}

	std::vector<glm::vec4> GroundClearing::Changed(const std::vector<Patch>& before, float featherBefore, const std::vector<Patch>& after, float featherAfter)
	{
		std::vector<glm::vec4> out;
		if (featherBefore != featherAfter)
		{
			for (const Patch& p : before) out.push_back(Bounds(p, featherBefore));
			for (const Patch& p : after) out.push_back(Bounds(p, featherAfter));
			return out;
		}
		std::vector<Patch> gone, added;
		std::set_difference(before.begin(), before.end(), after.begin(), after.end(), std::back_inserter(gone));
		std::set_difference(after.begin(), after.end(), before.begin(), before.end(), std::back_inserter(added));
		for (const Patch& p : gone) out.push_back(Bounds(p, featherBefore));
		for (const Patch& p : added) out.push_back(Bounds(p, featherAfter));
		return out;
	}
}
