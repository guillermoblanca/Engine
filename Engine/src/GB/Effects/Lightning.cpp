#include "Lightning.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	Lightning::Lightning(const LightningDesc& desc) : Desc(desc)
	{
		m_Rng = 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(Desc.Seed) * 0xBF58476D1CE4E5B9ull);
		Random(); Random();
		ScheduleNext();
	}

	float Lightning::Random()
	{
		m_Rng ^= m_Rng >> 12;
		m_Rng ^= m_Rng << 25;
		m_Rng ^= m_Rng >> 27;
		return static_cast<float>(((m_Rng * 0x2545F4914F6CDD1Dull) >> 40) & 0xFFFFFF) / 16777216.0f;
	}

	void Lightning::ScheduleNext()
	{
		m_Timer = Random(Desc.Interval.x, std::max(Desc.Interval.x, Desc.Interval.y));
	}

	glm::vec3 Lightning::RandomPerpendicular(const glm::vec3& dir)
	{
		const glm::vec3 helper = std::fabs(dir.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
		const glm::vec3 u = glm::normalize(glm::cross(helper, dir));
		const glm::vec3 v = glm::cross(dir, u);
		const float phi = Random(0.0f, 6.2831853f);
		return u * std::cos(phi) + v * std::sin(phi);
	}

	void Lightning::Subdivide(const glm::vec3& a, const glm::vec3& b, int depth, float width, bool allowBranches)
	{
		if (depth <= 0)
		{
			m_Segments.push_back({ a, b, width });
			return;
		}
		const glm::vec3 d = b - a;
		const float len = glm::length(d);
		if (len < 1e-4f)
			return;
		const glm::vec3 dir = d / len;
		const glm::vec3 mid = (a + b) * 0.5f + RandomPerpendicular(dir) * len * Desc.Jaggedness * Random(0.2f, 1.0f);

		// A side branch leaves from the middle point, heading generally down and away.
		if (allowBranches && depth >= 2 && m_BranchesLeft > 0 && Random() < Desc.BranchChance)
		{
			--m_BranchesLeft;
			glm::vec3 branchDir = glm::normalize(dir + RandomPerpendicular(dir) * Random(0.6f, 1.4f));
			branchDir.y = std::min(branchDir.y, -0.15f);   // lightning does not climb
			branchDir = glm::normalize(branchDir);
			Subdivide(mid, mid + branchDir * len * Random(1.5f, 3.2f), std::max(depth - 2, 1), width * 0.45f, false);
		}
		Subdivide(a, mid, depth - 1, width, allowBranches);
		Subdivide(mid, b, depth - 1, width, allowBranches);
	}

	void Lightning::Strike(const glm::vec3* target)
	{
		if (target)
			m_Target = *target;
		else
		{
			const float angle = Random(0.0f, 6.2831853f);
			const float r = Desc.AreaRadius * std::sqrt(Random());
			m_Target = Desc.Center + glm::vec3(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
		}

		const glm::vec3 start = m_Target + glm::vec3(Random(-14.0f, 14.0f), Desc.CloudHeight, Random(-14.0f, 14.0f));
		m_Segments.clear();
		m_BranchesLeft = Desc.MaxBranches;
		Subdivide(start, m_Target, std::max(Desc.Levels, 1), 1.0f, true);
		// Branches and jitter must not dig into the ground: cut segments where they reach it.
		const float ground = m_Target.y;
		std::vector<LightningSegment> kept;
		kept.reserve(m_Segments.size());
		for (LightningSegment seg : m_Segments)
		{
			if (seg.A.y < ground && seg.B.y < ground)
				continue;
			if (seg.A.y < ground)
				seg.A = glm::mix(seg.A, seg.B, (ground - seg.A.y) / (seg.B.y - seg.A.y));
			else if (seg.B.y < ground)
				seg.B = glm::mix(seg.A, seg.B, (seg.A.y - ground) / (seg.A.y - seg.B.y));
			kept.push_back(seg);
		}
		m_Segments.swap(kept);

		m_Pulse[0] = 0.0f;
		m_Pulse[1] = Random(0.07f, 0.13f);
		m_Pulse[2] = m_Pulse[1] + Random(0.08f, 0.15f);
		m_PulseAmp[0] = 1.0f;
		m_PulseAmp[1] = Random(0.5f, 0.9f);
		m_PulseAmp[2] = Random(0.3f, 0.6f);
		m_Age = 0.0f;
		m_Active = true;

		const float distance = glm::length(m_Target - m_Listener);
		ThunderEvent thunder;
		thunder.Position = m_Target;
		thunder.Distance = distance;
		thunder.Loudness = 1.0f / (1.0f + std::pow(distance / 120.0f, 1.5f));
		m_Pending.push_back({ distance / std::max(Desc.SpeedOfSound, 1.0f), thunder });
		Rebuild();
	}

	void Lightning::Update(float dt)
	{
		if (dt <= 0.0f)
			return;
		if (Desc.Automatic)
		{
			m_Timer -= dt;
			if (m_Timer <= 0.0f)
			{
				Strike();
				ScheduleNext();
			}
		}
		if (m_Active)
		{
			m_Age += dt;
			if (m_Age >= Desc.Duration)
			{
				m_Active = false;
				m_Flash = 0.0f;
				m_Segments.clear();
				m_Glow.clear();
				m_Core.clear();
			}
			else
				Rebuild();
		}
		for (size_t i = 0; i < m_Pending.size();)
		{
			m_Pending[i].Delay -= dt;
			if (m_Pending[i].Delay <= 0.0f)
			{
				m_Ready.push_back(m_Pending[i].Event);
				m_Pending.erase(m_Pending.begin() + static_cast<long>(i));
			}
			else
				++i;
		}
	}

	std::vector<ThunderEvent> Lightning::TakeThunder()
	{
		std::vector<ThunderEvent> out;
		out.swap(m_Ready);
		return out;
	}

	void Lightning::Rebuild()
	{
		// Flash envelope: sharp attack and quick decay for each of up to three pulses (the "flicker"),
		// all fading over the bolt's duration.
		float flash = 0.0f;
		for (int i = 0; i < 3; ++i)
			if (m_Age >= m_Pulse[i])
				flash += m_PulseAmp[i] * std::exp(-(m_Age - m_Pulse[i]) / 0.045f);
		flash *= std::max(0.0f, 1.0f - m_Age / std::max(Desc.Duration, 1e-3f));
		m_Flash = std::min(flash, 1.0f);

		m_Glow.clear();
		m_Core.clear();
		if (m_Flash <= 0.002f)
			return;
		m_Glow.reserve(m_Segments.size());
		m_Core.reserve(m_Segments.size());
		for (const LightningSegment& s : m_Segments)
		{
			const glm::vec3 d = s.B - s.A;
			const float len = glm::length(d);
			if (len < 1e-4f)
				continue;
			const glm::vec3 dir = d / len;
			const glm::vec3 mid = (s.A + s.B) * 0.5f;
			const float half = len * 0.5f;

			// The stretched-billboard shader draws half-length = size * (1 + speed), so speed = half / size.
			const float glowSize = Desc.GlowWidth * s.Width, coreSize = Desc.CoreWidth * s.Width;
			ParticleInstance glow;
			glow.PositionSize = glm::vec4(mid, glowSize);
			glow.Color = glm::vec4(Desc.Color * Desc.Brightness * 0.08f, 0.55f * m_Flash);
			glow.VelocityRotation = glm::vec4(dir * (half / glowSize), 0.0f);
			glow.Frame = glm::vec4(0.0f);
			m_Glow.push_back(glow);

			ParticleInstance core;
			core.PositionSize = glm::vec4(mid, coreSize);
			core.Color = glm::vec4(glm::mix(Desc.Color, glm::vec3(1.0f), 0.6f) * Desc.Brightness, m_Flash);
			core.VelocityRotation = glm::vec4(dir * (half / coreSize), 0.0f);
			core.Frame = glm::vec4(0.0f);
			m_Core.push_back(core);
		}
	}

	void Lightning::AddToScene(Scene& scene) const
	{
		if (!m_Active || m_Flash <= 0.002f)
			return;
		ParticleBatch batch;
		batch.Blend = ParticleBlend::Additive;
		batch.Sprite = ParticleSprite::Streak;
		batch.Stretch = 1.0f;
		batch.Instances = &m_Glow;
		scene.ParticleBatches.push_back(batch);
		batch.Instances = &m_Core;
		scene.ParticleBatches.push_back(batch);

		PointLight light;
		light.Position = m_Target + glm::vec3(0.0f, Desc.CloudHeight * 0.35f, 0.0f);
		light.Color = Desc.Color;
		light.Intensity = Desc.LightIntensity * m_Flash;
		scene.PointLights.push_back(light);
	}
}
