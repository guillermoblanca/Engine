#include "SandSpray.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	SandSpray::SandSpray(const Terrain& terrain, uint32_t seed) : m_Terrain(terrain)
	{
		m_Rng = 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(seed) * 0xBF58476D1CE4E5B9ull);
		Random(); Random();

		ParticleEmitterDesc grains;
		grains.Shape = ParticleShape::Point;
		grains.ConeAngleDegrees = 28.0f;
		grains.Rate = 0.0f;
		grains.MaxParticles = 9000;
		grains.Lifetime = { 1.2f, 2.6f };
		grains.Speed = { 4.0f, 10.0f };
		grains.Gravity = { 0.0f, -9.81f, 0.0f };
		grains.GravityScale = 0.35f;
		grains.Drag = 0.35f;
		grains.TurbulenceStrength = 2.5f;
		grains.TurbulenceFrequency = 0.4f;
		grains.TurbulenceSpeed = 1.0f;
		grains.StartSize = { 0.03f, 0.065f };
		grains.StartRotationDegrees = { 0.0f, 0.0f };
		grains.StartColor = { 0.66f, 0.48f, 0.28f, 0.50f };
		grains.EndColor = { 0.66f, 0.48f, 0.28f, 0.0f };
		grains.FadeInFraction = 0.05f;
		grains.Stretch = 0.22f;
		grains.Blend = ParticleBlend::Alpha;
		grains.Sprite = ParticleSprite::Streak;
		grains.CollideGround = true;
		grains.GroundHeightFn = m_Terrain.MakeGroundFunction();
		grains.Seed = seed * 3 + 1;
		m_Grains = std::make_unique<ParticleSystem>(grains);

		ParticleEmitterDesc puffs = grains;
		puffs.MaxParticles = 600;
		puffs.Lifetime = { 2.5f, 4.5f };
		puffs.Speed = { 2.0f, 5.0f };
		puffs.GravityScale = -0.05f;
		puffs.Drag = 0.5f;
		puffs.StartSize = { 1.5f, 3.0f };
		puffs.EndSizeScale = 2.4f;
		puffs.AngularVelocityDegrees = { -10.0f, 10.0f };
		puffs.StartColor = { 0.86f, 0.70f, 0.48f, 0.10f };
		puffs.EndColor = { 0.86f, 0.70f, 0.48f, 0.0f };
		puffs.FadeInFraction = 0.3f;
		puffs.Stretch = 0.0f;
		puffs.Sprite = ParticleSprite::Smoke;
		puffs.SoftDistance = 1.2f;
		puffs.StartRotationDegrees = { 0.0f, 360.0f };
		puffs.Seed = seed * 5 + 2;
		m_Puffs = std::make_unique<ParticleSystem>(puffs);
	}

	float SandSpray::Random()
	{
		m_Rng ^= m_Rng >> 12;
		m_Rng ^= m_Rng << 25;
		m_Rng ^= m_Rng >> 27;
		return static_cast<float>(((m_Rng * 0x2545F4914F6CDD1Dull) >> 40) & 0xFFFFFF) / 16777216.0f;
	}

	void SandSpray::Update(float dt, const Camera& camera)
	{
		if (dt <= 0.0f)
			return;
		m_Time += dt;

		const glm::vec3 horizontal(m_Wind.x, 0.0f, m_Wind.z);
		const float speed = glm::length(horizontal);
		const glm::vec3 dir = speed > 1e-3f ? horizontal / speed : glm::vec3(1, 0, 0);
		for (ParticleSystem* system : { m_Grains.get(), m_Puffs.get() })
		{
			system->Desc.Direction = glm::normalize(dir + glm::vec3(0.0f, 0.28f, 0.0f));
			system->Desc.Wind = horizontal * 0.35f;
		}
		m_Grains->Desc.Speed = { 0.5f * speed + 2.0f, 1.1f * speed + 3.0f };

		// Pick crest points near the camera; gusts make the sand come in waves.
		const auto& crests = m_Terrain.GetCrestPoints();
		if (!crests.empty())
		{
			const float gust = 0.65f + 0.35f * std::sin(m_Time * 0.9f) * std::sin(m_Time * 0.37f + 1.0f);
			m_Carry += Rate * Intensity * gust * dt;
			m_PuffCarry += 14.0f * Intensity * gust * dt;
			const float r2 = Radius * Radius;
			auto pick = [&](glm::vec3& out)
			{
				for (int attempt = 0; attempt < 24; ++attempt)
				{
					const glm::vec3& c = crests[static_cast<size_t>(Random() * static_cast<float>(crests.size())) % crests.size()];
					const glm::vec2 d(c.x - camera.Position.x, c.z - camera.Position.z);
					if (glm::dot(d, d) <= r2)
					{
						out = c + glm::vec3(Random() - 0.5f, 0.05f, Random() - 0.5f) * 2.0f;
						return true;
					}
				}
				return false;
			};
			glm::vec3 p;
			for (; m_Carry >= 1.0f; m_Carry -= 1.0f)
				if (pick(p))
					m_Grains->EmitAt(p, 1);
			for (; m_PuffCarry >= 1.0f; m_PuffCarry -= 1.0f)
				if (pick(p))
					m_Puffs->EmitAt(p, 1);
		}
		else
			m_Carry = m_PuffCarry = 0.0f;

		m_Grains->Update(dt);
		m_Puffs->Update(dt);
	}

	void SandSpray::AddToScene(Scene& scene) const
	{
		scene.Particles.push_back(m_Puffs.get());
		scene.Particles.push_back(m_Grains.get());
	}
}
