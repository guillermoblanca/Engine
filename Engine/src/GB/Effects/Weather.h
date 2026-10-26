#pragma once
#include "GB/Effects/Lightning.h"
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/Particles.h"
#include "GB/Renderer/Renderer.h"
#include <glm/glm.hpp>
#include <memory>

namespace GB
{
	enum class WeatherPreset { Clear, LightRain, Rain, Thunderstorm, Snowfall, Blizzard, Dust, Sandstorm, Tornado };

	// Drives all weather effects around the camera: rain (with splashes and mist), snow, ambient dust,
	// sandstorms, thunderstorms (lightning + thunder events) and a tornado. Each effect has a 0..1 level
	// that eases toward its target, so changes blend smoothly. Overcast sky, fog and wet surfaces are
	// applied to the scene's Environment.
	class Weather
	{
	public:
		explicit Weather(uint32_t seed = 1);

		// Ground height used for rain/snow impacts and where the tornado touches down.
		float GroundY = 0.0f;
		float TransitionSeconds = 4.0f;   // time to go from 0 to 1 (or back)

		void SetPreset(WeatherPreset preset);
		void SetRain(float level) { m_Target.Rain = glm::clamp(level, 0.0f, 1.0f); }
		void SetSnow(float level) { m_Target.Snow = glm::clamp(level, 0.0f, 1.0f); }
		void SetDust(float level) { m_Target.Dust = glm::clamp(level, 0.0f, 1.0f); }
		void SetSandstorm(float level) { m_Target.Sandstorm = glm::clamp(level, 0.0f, 1.0f); }
		void SetStorm(float level) { m_Target.Storm = glm::clamp(level, 0.0f, 1.0f); }   // dark sky + lightning
		void SetTornado(float level) { m_Target.Tornado = glm::clamp(level, 0.0f, 1.0f); }
		void SetWind(const glm::vec3& velocity) { m_Wind = velocity; }                     // horizontal m/s
		const glm::vec3& GetWind() const { return m_Wind; }
		void SetTornadoPosition(const glm::vec3& position) { m_TornadoPosition = position; }
		const glm::vec3& GetTornadoPosition() const { return m_TornadoPosition; }

		// Jumps to the targets without easing (screenshots, tests).
		void Snap();
		float GetRain() const { return m_Level.Rain; }
		float GetSnow() const { return m_Level.Snow; }
		float GetDust() const { return m_Level.Dust; }
		float GetSandstorm() const { return m_Level.Sandstorm; }
		float GetStorm() const { return m_Level.Storm; }
		float GetTornado() const { return m_Level.Tornado; }
		float GetWetness() const { return m_Wetness; }
		float GetSnowCover() const { return m_SnowCover; }

		// Captures the scene's current environment, sun and the number of particle systems / lights /
		// batches it already has. Call once after building the scene, before the first Apply.
		void SetBase(const Scene& scene);

		void Update(float dt, const Camera& camera);
		// Writes the weather into `scene`: environment (sky, fog, wetness, flash), sun, lightning light
		// and the particle systems. Idempotent: call every frame on the same scene.
		void Apply(Scene& scene) const;

		Lightning& GetLightning() { return m_Lightning; }
		const Lightning& GetLightning() const { return m_Lightning; }
		ParticleSystem& GetRainSystem() { return *m_Rain; }
		ParticleSystem& GetSnowSystem() { return *m_SnowFlakes; }
		ParticleSystem& GetTornadoFunnel() { return *m_Funnel; }
		size_t GetLiveParticleCount() const;

	private:
		struct Levels { float Rain = 0, Snow = 0, Dust = 0, Sandstorm = 0, Storm = 0, Tornado = 0; };

		std::unique_ptr<ParticleSystem> Make(const ParticleEmitterDesc& desc);
		void Retune(const Camera& camera);

		Levels m_Level, m_Target;
		glm::vec3 m_Wind{ 0.0f };
		glm::vec3 m_TornadoPosition{ 0.0f, 0.0f, -30.0f };
		float m_Wetness = 0.0f;
		float m_SnowCover = 0.0f;
		float m_Time = 0.0f;
		uint32_t m_Seed;

		Lightning m_Lightning;
		std::unique_ptr<ParticleSystem> m_Rain, m_Splash, m_Mist, m_SnowFlakes, m_Motes, m_Haze,
			m_SandStreaks, m_SandClouds, m_Funnel, m_FunnelBase, m_Debris, m_StormCloud;

		// Base scene state captured by SetBase().
		Environment m_BaseEnv;
		DirectionalLight m_BaseSun;
		size_t m_BaseParticles = 0, m_BaseBatches = 0, m_BaseLights = 0;
		bool m_HasBase = false;
	};
}
