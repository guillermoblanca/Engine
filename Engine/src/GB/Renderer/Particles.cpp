#include "Particles.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		float Smooth(float edge0, float edge1, float x)
		{
			const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		float Hash3(int x, int y, int z)
		{
			uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ static_cast<uint32_t>(z) * 83492791u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
		}

		// Smooth value noise in [0,1].
		float Noise3(const glm::vec3& p)
		{
			const glm::vec3 f = glm::floor(p);
			const glm::vec3 t = p - f;
			const glm::vec3 u = t * t * (3.0f - 2.0f * t);
			const int x = static_cast<int>(f.x), y = static_cast<int>(f.y), z = static_cast<int>(f.z);
			auto lerp = [](float a, float b, float k) { return a + (b - a) * k; };
			const float c00 = lerp(Hash3(x, y, z), Hash3(x + 1, y, z), u.x), c10 = lerp(Hash3(x, y + 1, z), Hash3(x + 1, y + 1, z), u.x);
			const float c01 = lerp(Hash3(x, y, z + 1), Hash3(x + 1, y, z + 1), u.x), c11 = lerp(Hash3(x, y + 1, z + 1), Hash3(x + 1, y + 1, z + 1), u.x);
			return lerp(lerp(c00, c10, u.y), lerp(c01, c11, u.y), u.z);
		}

		// Orthonormal basis (right, up) perpendicular to `axis`.
		void Basis(const glm::vec3& axis, glm::vec3& right, glm::vec3& up)
		{
			const glm::vec3 helper = std::abs(axis.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
			right = glm::normalize(glm::cross(helper, axis));
			up = glm::cross(axis, right);
		}
	}

	ParticleSystem::ParticleSystem(const ParticleEmitterDesc& desc) : Desc(desc)
	{
		Reset();
	}

	void ParticleSystem::Reset()
	{
		m_Particles.clear();
		m_Time = 0.0f;
		m_Accumulator = 0.0f;
		m_RngState = 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(Desc.Seed) * 0xBF58476D1CE4E5B9ull);
		Random(); Random();   // warm up
	}

	float ParticleSystem::Random()
	{
		// xorshift64*: small, fast and identical on every platform.
		m_RngState ^= m_RngState >> 12;
		m_RngState ^= m_RngState << 25;
		m_RngState ^= m_RngState >> 27;
		const uint64_t r = m_RngState * 0x2545F4914F6CDD1Dull;
		return static_cast<float>((r >> 40) & 0xFFFFFF) / 16777216.0f;
	}

	bool ParticleSystem::IsFinished() const
	{
		return !Desc.Looping && m_Time >= Desc.Duration && m_Particles.empty();
	}

	void ParticleSystem::Emit(uint32_t count, float maxAge, const glm::vec3* worldOrigin)
	{
		const glm::mat3 rotation(m_Transform);
		for (uint32_t i = 0; i < count && m_Particles.size() < Desc.MaxParticles; ++i)
		{
			glm::vec3 position(0.0f);
			const glm::vec3 axis = glm::length(Desc.Direction) > 0.0f ? glm::normalize(Desc.Direction) : glm::vec3(0, 1, 0);
			glm::vec3 direction = axis;

			auto coneDirection = [&]()
			{
				glm::vec3 right, up;
				Basis(axis, right, up);
				const float cosMax = std::cos(glm::radians(Desc.ConeAngleDegrees));
				const float cosTheta = 1.0f - Random() * (1.0f - cosMax);    // uniform on the cap
				const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
				const float phi = 2.0f * kPi * Random();
				return axis * cosTheta + (right * std::cos(phi) + up * std::sin(phi)) * sinTheta;
			};

			switch (Desc.Shape)
			{
			case ParticleShape::Point:
				direction = coneDirection();
				break;
			case ParticleShape::Sphere:
			{
				glm::vec3 p;
				do { p = glm::vec3(Random(), Random(), Random()) * 2.0f - 1.0f; } while (glm::dot(p, p) > 1.0f || glm::dot(p, p) < 1e-6f);
				position = p * Desc.Radius;
				direction = glm::normalize(p);
				break;
			}
			case ParticleShape::Cone:
			{
				glm::vec3 right, up;
				Basis(axis, right, up);
				const float phi = 2.0f * kPi * Random();
				const float r = Desc.Radius * std::sqrt(Random());
				position = (right * std::cos(phi) + up * std::sin(phi)) * r;
				direction = coneDirection();
				break;
			}
			case ParticleShape::Box:
				position = (glm::vec3(Random(), Random(), Random()) - 0.5f) * Desc.BoxSize;
				direction = coneDirection();
				break;
			}

			Particle p;
			p.Lifetime = std::max(Random(Desc.Lifetime), 0.01f);
			p.Age = Random() * maxAge;   // spread over the frame to avoid visible banding
			p.StartSize = Random(Desc.StartSize);
			p.Rotation = glm::radians(Random(Desc.StartRotationDegrees));
			p.AngularVelocity = glm::radians(Random(Desc.AngularVelocityDegrees));
			glm::vec3 velocity = direction * Random(Desc.Speed);
			if (!Desc.LocalSpace)
			{
				p.Position = worldOrigin ? *worldOrigin : glm::vec3(m_Transform * glm::vec4(position, 1.0f));
				velocity = rotation * velocity;
			}
			else
				p.Position = worldOrigin ? glm::vec3(glm::inverse(m_Transform) * glm::vec4(*worldOrigin, 1.0f)) : position;
			p.Velocity = velocity;
			m_Particles.push_back(p);
		}
	}

	void ParticleSystem::Burst(uint32_t count) { Emit(count, 0.0f); }

	void ParticleSystem::EmitAt(const glm::vec3& worldPosition, uint32_t count) { Emit(count, 0.0f, &worldPosition); }

	void ParticleSystem::EmitWindow(float from, float to)
	{
		for (const ParticleBurst& b : Desc.Bursts)
			if (b.Time >= from && b.Time < to)
				Emit(b.Count, 0.0f);
	}

	void ParticleSystem::Update(float dt)
	{
		if (dt <= 0.0f)
			return;

		// 1. Move and age the existing particles (before emission so new ones start at their spawn point).
		m_Hits.clear();
		m_TotalTime += dt;
		const glm::vec3 baseAcceleration = Desc.Gravity * Desc.GravityScale + Desc.Wind;
		const float damping = std::exp(-Desc.Drag * dt);
		const glm::vec3 vortexCenter = Desc.LocalSpace ? glm::vec3(0.0f) : glm::vec3(m_Transform[3]);
		const glm::vec3 vortexAxis = glm::length(Desc.VortexAxis) > 0.0f ? glm::normalize(Desc.VortexAxis) : glm::vec3(0, 1, 0);
		const bool collide = Desc.CollideGround && !Desc.LocalSpace;
		for (size_t i = 0; i < m_Particles.size();)
		{
			Particle& p = m_Particles[i];
			p.Age += dt;
			if (p.Age >= p.Lifetime)
			{
				p = m_Particles.back();   // swap-and-pop (order does not matter)
				m_Particles.pop_back();
				continue;
			}

			glm::vec3 acceleration = baseAcceleration;
			if (Desc.TurbulenceStrength != 0.0f)
			{
				const glm::vec3 q = p.Position * Desc.TurbulenceFrequency + glm::vec3(0.0f, m_TotalTime * Desc.TurbulenceSpeed, 0.0f);
				const glm::vec3 n(Noise3(q), Noise3(q + glm::vec3(31.4f, 17.2f, 5.9f)), Noise3(q + glm::vec3(-12.1f, 47.3f, 23.8f)));
				acceleration += (n * 2.0f - 1.0f) * Desc.TurbulenceStrength;
			}
			if (Desc.Vortex)
			{
				const glm::vec3 d = p.Position - vortexCenter;
				const float height = glm::dot(d, vortexAxis);
				const glm::vec3 radial = d - vortexAxis * height;
				const float r = glm::length(radial);
				if (r > 1e-4f)
				{
					const float falloff = 1.0f - Smooth(0.0f, std::max(Desc.VortexRadius, 1e-3f), r);
					const float k = glm::clamp(height / std::max(Desc.VortexHeight, 1e-3f), 0.0f, 1.0f);
					const float inward = glm::mix(Desc.VortexInward, Desc.VortexInwardTop, k);
					const glm::vec3 outward = radial / r;
					const glm::vec3 tangent = glm::cross(vortexAxis, outward);
					acceleration += (tangent * Desc.VortexSwirl - outward * inward + vortexAxis * Desc.VortexLift) * falloff;
				}
			}

			p.Velocity = (p.Velocity + acceleration * dt) * damping;
			p.Position += p.Velocity * dt;
			p.Rotation += p.AngularVelocity * dt;

			const float groundY = collide && Desc.GroundHeightFn ? Desc.GroundHeightFn(p.Position.x, p.Position.z) : Desc.GroundY;
			if (collide && p.Position.y < groundY)
			{
				if (m_Hits.size() < 512)
					m_Hits.push_back({ glm::vec3(p.Position.x, groundY, p.Position.z), glm::length(p.Velocity) });
				if (Desc.Bounce > 0.0f && std::fabs(p.Velocity.y) > 0.3f)
				{
					p.Position.y = groundY;
					p.Velocity.y = -p.Velocity.y * Desc.Bounce;
					p.Velocity.x *= 1.0f - Desc.BounceFriction;
					p.Velocity.z *= 1.0f - Desc.BounceFriction;
				}
				else
				{
					p = m_Particles.back();
					m_Particles.pop_back();
					continue;
				}
			}
			++i;
		}

		if (m_HitTarget)
		{
			for (const ParticleHit& hit : m_Hits)
				if (m_HitProbability >= 1.0f || Random() < m_HitProbability)
					m_HitTarget->EmitAt(hit.Position, m_HitCount);
		}

		// 2. Emit.
		const float previous = m_Time;
		m_Time += dt;
		if (m_Playing)
		{
			const float duration = std::max(Desc.Duration, 1e-4f);
			float emitDt = dt;
			if (Desc.Looping)
			{
				if (m_Time >= duration)
				{
					EmitWindow(previous, duration);
					m_Time = std::fmod(m_Time, duration);
					EmitWindow(0.0f, m_Time);
				}
				else
					EmitWindow(previous, m_Time);
			}
			else
			{
				// Only the part of the frame that lies inside [0, duration) emits.
				const float from = std::min(previous, duration), to = std::min(m_Time, duration);
				EmitWindow(from, to);
				emitDt = to - from;
			}

			m_Accumulator += Desc.Rate * emitDt;
			const uint32_t count = static_cast<uint32_t>(m_Accumulator);
			m_Accumulator -= static_cast<float>(count);
			Emit(count, dt);
		}
	}

	ParticleBatch ParticleSystem::MakeBatch(const std::vector<ParticleInstance>& instances) const
	{
		ParticleBatch b;
		b.Instances = &instances;
		b.Blend = Desc.Blend;
		b.Sprite = Desc.Sprite;
		b.CustomTexture = Desc.CustomTexture;
		b.Stretch = Desc.Stretch;
		b.Sheet = { static_cast<float>(std::max(1u, Desc.SheetColumns)), static_cast<float>(std::max(1u, Desc.SheetRows)) };
		b.SoftDistance = Desc.SoftDistance;
		return b;
	}

	void ParticleSystem::BuildInstances(const glm::vec3& cameraPosition, std::vector<ParticleInstance>& out) const
	{
		out.clear();
		out.reserve(m_Particles.size());
		const bool local = Desc.LocalSpace;
		const glm::mat3 rotation(m_Transform);
		const float frames = static_cast<float>(std::max(1u, Desc.SheetColumns * Desc.SheetRows));

		for (const Particle& p : m_Particles)
		{
			const float t = glm::clamp(p.Age / p.Lifetime, 0.0f, 1.0f);
			glm::vec4 color = glm::mix(Desc.StartColor, Desc.EndColor, t);
			color.a *= Smooth(0.0f, std::max(Desc.FadeInFraction, 1e-4f), t);
			color.r *= Desc.Intensity;
			color.g *= Desc.Intensity;
			color.b *= Desc.Intensity;

			const float size = p.StartSize * glm::mix(1.0f, Desc.EndSizeScale, t);
			const glm::vec3 position = local ? glm::vec3(m_Transform * glm::vec4(p.Position, 1.0f)) : p.Position;
			const glm::vec3 velocity = local ? rotation * p.Velocity : p.Velocity;

			ParticleInstance inst;
			inst.PositionSize = glm::vec4(position, size);
			inst.Color = color;
			inst.VelocityRotation = glm::vec4(velocity, p.Rotation);
			inst.Frame = glm::vec4(std::min(std::floor(t * frames), frames - 1.0f), 0.0f, 0.0f, 0.0f);
			out.push_back(inst);
		}

		if (Desc.Blend == ParticleBlend::Alpha)
		{
			std::sort(out.begin(), out.end(), [&](const ParticleInstance& a, const ParticleInstance& b)
			{
				const glm::vec3 da = glm::vec3(a.PositionSize) - cameraPosition, db = glm::vec3(b.PositionSize) - cameraPosition;
				return glm::dot(da, da) > glm::dot(db, db);   // far to near
			});
		}
	}

	// ------------------------------------------------------------------ presets
	namespace ParticlePresets
	{
		ParticleEmitterDesc Fire()
		{
			ParticleEmitterDesc d;
			d.Shape = ParticleShape::Cone;
			d.Radius = 0.22f;
			d.ConeAngleDegrees = 10.0f;
			d.Rate = 110.0f;
			d.MaxParticles = 600;
			d.Lifetime = { 0.5f, 1.0f };
			d.Speed = { 0.8f, 1.8f };
			d.Gravity = { 0.0f, -1.5f, 0.0f };
			d.GravityScale = -1.0f;             // negative scale = buoyancy (hot gas rises)
			d.Drag = 0.4f;
			d.StartSize = { 0.28f, 0.5f };
			d.EndSizeScale = 0.12f;
			d.AngularVelocityDegrees = { -40.0f, 40.0f };
			d.StartColor = { 1.0f, 0.62f, 0.15f, 0.85f };
			d.EndColor = { 0.85f, 0.07f, 0.0f, 0.0f };
			d.Intensity = 1.5f;
			d.Blend = ParticleBlend::Additive;
			d.Sprite = ParticleSprite::SoftCircle;
			d.SoftDistance = 0.35f;
			d.Seed = 11;
			return d;
		}

		ParticleEmitterDesc Smoke()
		{
			ParticleEmitterDesc d;
			d.Shape = ParticleShape::Cone;
			d.Radius = 0.2f;
			d.ConeAngleDegrees = 14.0f;
			d.Rate = 12.0f;
			d.MaxParticles = 200;
			d.Lifetime = { 2.5f, 4.0f };
			d.Speed = { 0.5f, 1.0f };
			d.Gravity = { 0.0f, -1.0f, 0.0f };
			d.GravityScale = -0.4f;
			d.Wind = { 0.25f, 0.0f, 0.0f };
			d.Drag = 0.3f;
			d.StartSize = { 0.5f, 0.8f };
			d.EndSizeScale = 3.5f;
			d.AngularVelocityDegrees = { -25.0f, 25.0f };
			d.StartColor = { 0.42f, 0.30f, 0.22f, 0.26f };   // warmed by the fire at the base...
			d.EndColor = { 0.09f, 0.09f, 0.11f, 0.0f };      // ...cool and dark as it rises
			d.FadeInFraction = 0.15f;
			d.Blend = ParticleBlend::Alpha;
			d.Sprite = ParticleSprite::Smoke;
			d.SoftDistance = 1.0f;
			d.Seed = 23;
			return d;
		}

		ParticleEmitterDesc Sparks()
		{
			ParticleEmitterDesc d;
			d.Shape = ParticleShape::Cone;
			d.Radius = 0.15f;
			d.ConeAngleDegrees = 38.0f;
			d.Rate = 28.0f;
			d.MaxParticles = 300;
			d.Lifetime = { 0.8f, 1.7f };
			d.Speed = { 2.0f, 5.0f };
			d.GravityScale = 0.6f;
			d.Drag = 0.2f;
			d.StartSize = { 0.025f, 0.045f };
			d.EndSizeScale = 0.4f;
			d.StartColor = { 1.0f, 0.72f, 0.28f, 1.0f };
			d.EndColor = { 1.0f, 0.25f, 0.05f, 0.0f };
			d.Intensity = 9.0f;
			d.Stretch = 1.0f;
			d.FadeInFraction = 0.01f;
			d.Blend = ParticleBlend::Additive;
			d.Sprite = ParticleSprite::Spark;
			d.StartRotationDegrees = { 0.0f, 0.0f };
			d.Seed = 37;
			return d;
		}

		ParticleEmitterDesc Explosion()
		{
			ParticleEmitterDesc d;
			d.Shape = ParticleShape::Sphere;
			d.Radius = 0.12f;
			d.Rate = 0.0f;
			d.Bursts = { { 0.0f, 110 } };
			d.Looping = false;
			d.Duration = 0.1f;
			d.MaxParticles = 400;
			d.Lifetime = { 0.5f, 1.4f };
			d.Speed = { 2.0f, 7.0f };
			d.Gravity = { 0.0f, -1.0f, 0.0f };
			d.GravityScale = -0.8f;             // the fireball rises as it cools
			d.Drag = 2.6f;
			d.StartSize = { 0.22f, 0.5f };
			d.EndSizeScale = 1.7f;
			d.AngularVelocityDegrees = { -90.0f, 90.0f };
			d.StartColor = { 1.0f, 0.5f, 0.14f, 0.55f };
			d.EndColor = { 0.25f, 0.05f, 0.02f, 0.0f };
			d.Intensity = 1.2f;
			d.FadeInFraction = 0.02f;
			d.Blend = ParticleBlend::Additive;
			d.Sprite = ParticleSprite::SoftCircle;
			d.SoftDistance = 0.7f;
			d.Seed = 5;
			return d;
		}

		ParticleEmitterDesc Snow()
		{
			ParticleEmitterDesc d;
			d.Shape = ParticleShape::Box;
			d.BoxSize = { 12.0f, 0.2f, 12.0f };
			d.Direction = { 0.0f, -1.0f, 0.0f };
			d.ConeAngleDegrees = 10.0f;
			d.Rate = 160.0f;
			d.MaxParticles = 2000;
			d.Lifetime = { 6.0f, 9.0f };
			d.Speed = { 0.15f, 0.5f };
			d.GravityScale = 0.0f;
			d.Gravity = { 0.0f, -0.3f, 0.0f };
			d.Wind = { 0.12f, -0.35f, 0.0f };
			d.Drag = 0.2f;
			d.StartSize = { 0.025f, 0.055f };
			d.StartColor = { 0.9f, 0.95f, 1.0f, 0.6f };
			d.EndColor = { 0.9f, 0.95f, 1.0f, 0.6f };
			d.FadeInFraction = 0.05f;
			d.Blend = ParticleBlend::Alpha;
			d.Sprite = ParticleSprite::SoftCircle;
			d.Seed = 41;
			return d;
		}

		ParticleEmitterDesc Magic()
		{
			ParticleEmitterDesc d;
			d.Shape = ParticleShape::Sphere;
			d.Radius = 0.5f;
			d.Rate = 60.0f;
			d.MaxParticles = 500;
			d.Lifetime = { 1.2f, 2.5f };
			d.Speed = { 0.1f, 0.4f };
			d.Gravity = { 0.0f, -1.0f, 0.0f };
			d.GravityScale = -0.3f;
			d.Drag = 0.8f;
			d.StartSize = { 0.05f, 0.12f };
			d.EndSizeScale = 0.2f;
			d.StartColor = { 0.3f, 0.9f, 1.0f, 1.0f };
			d.EndColor = { 0.7f, 0.2f, 1.0f, 0.0f };
			d.Intensity = 6.0f;
			d.Blend = ParticleBlend::Additive;
			d.Sprite = ParticleSprite::SoftCircle;
			d.Seed = 53;
			return d;
		}

		ParticleEmitterDesc DustDevil()
		{
			ParticleEmitterDesc d;
			d.Shape = ParticleShape::Cone;
			d.Radius = 0.6f;
			d.ConeAngleDegrees = 12.0f;
			d.Rate = 220.0f;
			d.MaxParticles = 1500;
			d.Lifetime = { 2.5f, 4.5f };
			d.Speed = { 2.0f, 4.0f };
			d.Drag = 1.1f;
			d.Vortex = true;
			d.VortexSwirl = 12.0f;
			d.VortexInward = 20.0f;
			d.VortexInwardTop = -0.5f;
			d.VortexHeight = 14.0f;
			d.VortexLift = 8.0f;
			d.VortexRadius = 14.0f;
			d.StartSize = { 0.5f, 1.0f };
			d.EndSizeScale = 3.0f;
			d.AngularVelocityDegrees = { -40.0f, 40.0f };
			d.StartColor = { 0.72f, 0.55f, 0.36f, 0.26f };
			d.EndColor = { 0.68f, 0.52f, 0.34f, 0.0f };
			d.FadeInFraction = 0.1f;
			d.Blend = ParticleBlend::Alpha;
			d.Sprite = ParticleSprite::Smoke;
			d.SoftDistance = 1.0f;
			d.Seed = 61;
			return d;
		}
	}
}
