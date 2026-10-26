#pragma once
#include "Texture.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	enum class ParticleShape { Point, Sphere, Cone, Box };
	enum class ParticleBlend { Additive, Alpha };
	enum class ParticleSprite { SoftCircle, Smoke, Spark, Custom };

	struct ParticleBurst
	{
		float Time = 0.0f;       // seconds from the start of each loop
		uint32_t Count = 10;
	};

	// Everything that defines how a particle system emits, moves and looks. Ranges are {min, max}.
	struct ParticleEmitterDesc
	{
		// --- Emission
		ParticleShape Shape = ParticleShape::Cone;
		float Radius = 0.1f;                       // Sphere: volume radius. Cone: base radius
		glm::vec3 BoxSize{ 1.0f };                 // Box: full extents
		glm::vec3 Direction{ 0.0f, 1.0f, 0.0f };   // emission axis in emitter space (Cone/Box/Point)
		float ConeAngleDegrees = 25.0f;            // half angle around Direction
		float Rate = 50.0f;                        // particles per second
		std::vector<ParticleBurst> Bursts;
		float Duration = 5.0f;
		bool Looping = true;
		uint32_t MaxParticles = 2000;
		uint32_t Seed = 1;                         // same seed + same updates = identical particles

		// --- Motion
		glm::vec2 Lifetime{ 1.0f, 2.0f };
		glm::vec2 Speed{ 1.0f, 3.0f };
		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
		float GravityScale = 0.0f;                 // 1 = real gravity, negative = buoyancy
		glm::vec3 Wind{ 0.0f };
		float Drag = 0.0f;                         // velocity decay per second
		bool LocalSpace = false;                   // true: particles follow the emitter when it moves

		// --- Look
		glm::vec2 StartSize{ 0.1f, 0.2f };
		float EndSizeScale = 1.0f;                 // size at death relative to its starting size
		glm::vec2 StartRotationDegrees{ 0.0f, 360.0f };
		glm::vec2 AngularVelocityDegrees{ 0.0f, 0.0f };
		glm::vec4 StartColor{ 1.0f };              // RGBA, linear
		glm::vec4 EndColor{ 1.0f, 1.0f, 1.0f, 0.0f };
		float Intensity = 1.0f;                    // multiplies RGB; > 1 feeds the bloom
		float FadeInFraction = 0.05f;              // fraction of the lifetime spent fading in
		float Stretch = 0.0f;                      // length multiplier per unit of speed (sparks, rain)
		ParticleBlend Blend = ParticleBlend::Additive;
		ParticleSprite Sprite = ParticleSprite::SoftCircle;
		const Texture2D* CustomTexture = nullptr;  // used when Sprite == Custom (not owned)
		uint32_t SheetColumns = 1, SheetRows = 1;  // flipbook; plays once over the particle's lifetime
	};

	struct Particle
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Velocity{ 0.0f };
		float Age = 0.0f;
		float Lifetime = 1.0f;
		float StartSize = 0.1f;
		float Rotation = 0.0f;           // radians
		float AngularVelocity = 0.0f;    // radians per second
	};

	// Per-particle data as rendered (16 floats, one vertex-attribute stream with divisor 1).
	struct ParticleInstance
	{
		glm::vec4 PositionSize;      // xyz world position, w billboard size (world units)
		glm::vec4 Color;             // RGBA, rgb already multiplied by Intensity
		glm::vec4 VelocityRotation;  // xyz world velocity, w rotation (radians)
		glm::vec4 Frame;             // x = flipbook frame
	};

	// One emitter plus its live particles. CPU simulation, deterministic for a given seed.
	class ParticleSystem
	{
	public:
		explicit ParticleSystem(const ParticleEmitterDesc& desc = {});

		ParticleEmitterDesc Desc;   // may be edited at any time

		void SetTransform(const glm::mat4& transform) { m_Transform = transform; }
		void SetPosition(const glm::vec3& position) { m_Transform[3] = glm::vec4(position, 1.0f); }
		const glm::mat4& GetTransform() const { return m_Transform; }

		void Update(float dt);
		void Play() { m_Playing = true; }
		void Stop() { m_Playing = false; }          // stops emitting; live particles finish their life
		void Reset();                               // kills everything and rewinds (re-seeds the generator)
		void Burst(uint32_t count);                 // emits immediately, regardless of Rate/Playing

		bool IsPlaying() const { return m_Playing; }
		// Non-looping system that finished emitting and has no particles left.
		bool IsFinished() const;
		size_t GetAliveCount() const { return m_Particles.size(); }
		const std::vector<Particle>& GetParticles() const { return m_Particles; }

		// Fills `out` with the render data. Alpha-blended systems are sorted far to near.
		void BuildInstances(const glm::vec3& cameraPosition, std::vector<ParticleInstance>& out) const;

	private:
		float Random();                       // [0,1)
		float Random(const glm::vec2& range) { return range.x + (range.y - range.x) * Random(); }
		void Emit(uint32_t count, float maxAge);
		void EmitWindow(float from, float to);

		glm::mat4 m_Transform{ 1.0f };
		std::vector<Particle> m_Particles;
		uint64_t m_RngState = 1;
		float m_Time = 0.0f;
		float m_Accumulator = 0.0f;
		bool m_Playing = true;
	};

	// Ready-made emitters (tweak the returned description as needed).
	namespace ParticlePresets
	{
		ParticleEmitterDesc Fire();       // flames, additive, buoyant
		ParticleEmitterDesc Smoke();      // slow rising puffs, alpha blended
		ParticleEmitterDesc Sparks();     // fast stretched embers with gravity
		ParticleEmitterDesc Explosion();  // one burst; call system.Burst() or Reset() to fire again
		ParticleEmitterDesc Snow();       // drifting flakes from a box above the scene
		ParticleEmitterDesc Magic();      // glowing motes drifting upward
	}
}
