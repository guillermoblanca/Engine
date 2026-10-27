#include "GB/AI/Perception.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	bool InCone(const VisionParams& vision, const glm::vec2& eye, const glm::vec2& facing, const glm::vec2& target)
	{
		const glm::vec2 d = target - eye;
		const float dist = glm::length(d);
		if (dist <= vision.NearRange) return true;
		if (dist > vision.Range) return false;
		const float fl = glm::length(facing);
		if (fl < 1e-6f) return true;   // no facing yet: looks all round
		return glm::dot(d / dist, facing / fl) >= std::cos(vision.HalfFov);
	}

	bool SegmentHitsCircle(const glm::vec2& a, const glm::vec2& b, const glm::vec2& center, float radius)
	{
		const glm::vec2 ab = b - a;
		const float len2 = glm::dot(ab, ab);
		float t = 0.0f;
		if (len2 > 1e-9f) t = std::clamp(glm::dot(center - a, ab) / len2, 0.0f, 1.0f);
		return glm::distance(a + ab * t, center) < radius;
	}

	bool CanSee(const VisionParams& vision, const glm::vec2& eye, const glm::vec2& facing, const glm::vec2& target,
		const std::vector<Occluder>& occluders, const std::string& ignoreId)
	{
		if (!InCone(vision, eye, facing, target)) return false;
		for (const Occluder& o : occluders)
		{
			if (!ignoreId.empty() && o.Id == ignoreId) continue;
			if (glm::distance(o.Center, eye) < o.Radius + 0.3f) continue;       // the agent is standing against it
			if (glm::distance(o.Center, target) < o.Radius + 0.3f) continue;     // the target is what it is made of or stands on
			if (SegmentHitsCircle(eye, target, o.Center, o.Radius)) return false;
		}
		return true;
	}

	uint64_t SoundBus::Emit(const std::string& kind, const std::string& source, const std::string& subject, const glm::vec2& pos, float radius, double now)
	{
		SoundEvent e;
		e.Seq = ++m_Seq;
		e.Kind = kind;
		e.Source = source;
		e.Subject = subject;
		e.Pos = pos;
		e.Radius = radius;
		e.Time = now;
		m_Events.push_back(e);
		return e.Seq;
	}

	std::vector<SoundEvent> SoundBus::Heard(const std::string& listenerName, const glm::vec2& listener, float hearing, uint64_t since) const
	{
		std::vector<SoundEvent> out;
		for (const SoundEvent& e : m_Events)
		{
			if (e.Seq <= since || e.Source == listenerName) continue;
			if (glm::distance(e.Pos, listener) <= e.Radius * hearing) out.push_back(e);
		}
		return out;
	}

	void SoundBus::Prune(double now, double keep)
	{
		m_Events.erase(std::remove_if(m_Events.begin(), m_Events.end(), [&](const SoundEvent& e) { return now - e.Time > keep; }), m_Events.end());
	}
}
