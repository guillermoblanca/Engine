#include "Weather.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	namespace
	{
		float Approach(float current, float target, float maxDelta)
		{
			if (current < target) return std::min(current + maxDelta, target);
			return std::max(current - maxDelta, target);
		}
		glm::vec3 Horizontal(const glm::vec3& v) { return { v.x, 0.0f, v.z }; }
	}

	Weather::Weather(uint32_t seed) : m_Seed(seed), m_Lightning([&] { LightningDesc d; d.Seed = seed * 7 + 1; d.Automatic = false; return d; }())
	{
		// ---- Rain: streaks falling from a box above the camera, splashes where they land, ground mist.
		ParticleEmitterDesc rain;
		rain.Shape = ParticleShape::Box;
		rain.BoxSize = { 40.0f, 0.5f, 40.0f };
		rain.Direction = { 0.0f, -1.0f, 0.0f };
		rain.ConeAngleDegrees = 1.5f;
		rain.Rate = 0.0f;
		rain.MaxParticles = 12000;
		rain.Lifetime = { 1.0f, 1.3f };
		rain.Speed = { 17.0f, 22.0f };
		rain.StartSize = { 0.010f, 0.016f };
		rain.StartRotationDegrees = { 0.0f, 0.0f };
		rain.StartColor = rain.EndColor = { 0.78f, 0.84f, 0.95f, 0.32f };
		rain.FadeInFraction = 0.0f;
		rain.Stretch = 0.55f;
		rain.Blend = ParticleBlend::Alpha;
		rain.Sprite = ParticleSprite::Streak;
		rain.CollideGround = true;
		rain.Seed = seed * 11 + 1;
		m_Rain = Make(rain);

		ParticleEmitterDesc splash;
		splash.Shape = ParticleShape::Point;
		splash.ConeAngleDegrees = 62.0f;
		splash.Rate = 0.0f;
		splash.MaxParticles = 3000;
		splash.Lifetime = { 0.18f, 0.35f };
		splash.Speed = { 0.7f, 1.9f };
		splash.GravityScale = 1.0f;
		splash.StartSize = { 0.012f, 0.022f };
		splash.EndSizeScale = 0.4f;
		splash.StartColor = { 0.85f, 0.9f, 1.0f, 0.65f };
		splash.EndColor = { 0.85f, 0.9f, 1.0f, 0.0f };
		splash.FadeInFraction = 0.0f;
		splash.Blend = ParticleBlend::Alpha;
		splash.Sprite = ParticleSprite::SoftCircle;
		splash.Seed = seed * 13 + 1;
		m_Splash = Make(splash);
		m_Rain->SetHitEmitter(m_Splash.get(), 1, 0.12f);

		ParticleEmitterDesc mist;
		mist.Shape = ParticleShape::Box;
		mist.BoxSize = { 34.0f, 0.3f, 34.0f };
		mist.Rate = 0.0f;
		mist.MaxParticles = 300;
		mist.Lifetime = { 4.0f, 6.0f };
		mist.Speed = { 0.1f, 0.4f };
		mist.ConeAngleDegrees = 80.0f;
		mist.StartSize = { 2.0f, 3.5f };
		mist.EndSizeScale = 1.6f;
		mist.StartColor = { 0.68f, 0.73f, 0.80f, 0.07f };
		mist.EndColor = { 0.68f, 0.73f, 0.80f, 0.0f };
		mist.FadeInFraction = 0.3f;
		mist.AngularVelocityDegrees = { -8.0f, 8.0f };
		mist.Blend = ParticleBlend::Alpha;
		mist.Sprite = ParticleSprite::Smoke;
		mist.SoftDistance = 1.0f;
		mist.Seed = seed * 17 + 1;
		m_Mist = Make(mist);

		// ---- Snow: flakes tumbling in turbulence.
		ParticleEmitterDesc snow = ParticlePresets::Snow();
		snow.BoxSize = { 34.0f, 0.5f, 34.0f };
		snow.Rate = 0.0f;
		snow.MaxParticles = 26000;
		snow.Lifetime = { 7.0f, 10.0f };
		snow.Speed = { 0.4f, 1.1f };
		snow.Wind = { 0.0f, 0.0f, 0.0f };
		snow.Gravity = { 0.0f, -0.4f, 0.0f };
		snow.GravityScale = 1.0f;
		snow.Drag = 0.6f;
		snow.TurbulenceStrength = 1.6f;
		snow.TurbulenceFrequency = 0.35f;
		snow.StartSize = { 0.045f, 0.11f };
		snow.StartColor = snow.EndColor = { 0.95f, 0.97f, 1.0f, 0.85f };
		snow.CollideGround = true;
		snow.Seed = seed * 19 + 1;
		m_SnowFlakes = Make(snow);

		// ---- Dust: drifting motes and a faint haze.
		ParticleEmitterDesc motes;
		motes.Shape = ParticleShape::Box;
		motes.BoxSize = { 28.0f, 5.0f, 28.0f };
		motes.Rate = 0.0f;
		motes.MaxParticles = 2500;
		motes.Lifetime = { 4.0f, 7.0f };
		motes.Speed = { 0.05f, 0.3f };
		motes.ConeAngleDegrees = 90.0f;
		motes.Drag = 0.5f;
		motes.TurbulenceStrength = 0.9f;
		motes.TurbulenceFrequency = 0.3f;
		motes.StartSize = { 0.04f, 0.11f };
		motes.StartColor = { 0.80f, 0.64f, 0.42f, 0.65f };
		motes.EndColor = { 0.80f, 0.64f, 0.42f, 0.0f };
		motes.FadeInFraction = 0.25f;
		motes.Blend = ParticleBlend::Alpha;
		motes.Seed = seed * 23 + 1;
		m_Motes = Make(motes);

		ParticleEmitterDesc haze = motes;
		haze.BoxSize = { 30.0f, 3.0f, 30.0f };
		haze.MaxParticles = 200;
		haze.Lifetime = { 6.0f, 9.0f };
		haze.StartSize = { 3.0f, 6.0f };
		haze.EndSizeScale = 1.5f;
		haze.StartColor = { 0.80f, 0.64f, 0.42f, 0.10f };
		haze.EndColor = { 0.80f, 0.64f, 0.42f, 0.0f };
		haze.Sprite = ParticleSprite::Smoke;
		haze.AngularVelocityDegrees = { -6.0f, 6.0f };
		haze.SoftDistance = 1.5f;
		haze.Seed = seed * 29 + 1;
		m_Haze = Make(haze);

		// ---- Sandstorm: wind-driven grit streaks plus rolling dust clouds.
		ParticleEmitterDesc grit;
		grit.Shape = ParticleShape::Box;
		grit.BoxSize = { 6.0f, 9.0f, 44.0f };    // re-aimed at the wind every frame
		grit.Direction = { 1.0f, 0.0f, 0.0f };
		grit.ConeAngleDegrees = 6.0f;
		grit.Rate = 0.0f;
		grit.MaxParticles = 9000;
		grit.Lifetime = { 1.2f, 2.2f };
		grit.Speed = { 14.0f, 22.0f };
		grit.TurbulenceStrength = 6.0f;
		grit.TurbulenceFrequency = 0.25f;
		grit.TurbulenceSpeed = 1.2f;
		grit.StartSize = { 0.02f, 0.045f };
		grit.StartRotationDegrees = { 0.0f, 0.0f };
		grit.StartColor = { 0.60f, 0.42f, 0.24f, 0.55f };
		grit.EndColor = { 0.60f, 0.42f, 0.24f, 0.0f };
		grit.FadeInFraction = 0.1f;
		grit.Stretch = 0.3f;
		grit.Blend = ParticleBlend::Alpha;
		grit.Sprite = ParticleSprite::Streak;
		grit.Seed = seed * 31 + 1;
		m_SandStreaks = Make(grit);

		ParticleEmitterDesc sandCloud = haze;
		sandCloud.BoxSize = { 6.0f, 7.0f, 40.0f };
		sandCloud.Direction = { 1.0f, 0.0f, 0.0f };
		sandCloud.ConeAngleDegrees = 12.0f;
		sandCloud.Speed = { 7.0f, 12.0f };
		sandCloud.MaxParticles = 500;
		sandCloud.Lifetime = { 3.0f, 5.0f };
		sandCloud.StartSize = { 4.0f, 8.0f };
		sandCloud.EndSizeScale = 1.8f;
		sandCloud.StartColor = { 0.62f, 0.40f, 0.22f, 0.13f };
		sandCloud.EndColor = { 0.62f, 0.40f, 0.22f, 0.0f };
		sandCloud.Drag = 0.0f;
		sandCloud.TurbulenceStrength = 2.0f;
		sandCloud.SoftDistance = 2.0f;
		sandCloud.Seed = seed * 37 + 1;
		m_SandClouds = Make(sandCloud);

		// ---- Tornado: swirling funnel, ground dust ring, debris and the wall cloud above.
		ParticleEmitterDesc funnel;
		funnel.Shape = ParticleShape::Cone;
		funnel.Radius = 1.5f;
		funnel.ConeAngleDegrees = 8.0f;
		funnel.Rate = 0.0f;
		funnel.MaxParticles = 4200;
		funnel.Lifetime = { 5.0f, 8.0f };
		funnel.Speed = { 3.0f, 7.0f };
		funnel.Drag = 1.2f;
		funnel.Vortex = true;
		funnel.VortexSwirl = 15.0f;         // terminal spin ~ swirl / drag = 12 m/s...
		funnel.VortexInward = 32.0f;        // ...held on a ~4 m radius by this pull at the base
		funnel.VortexInwardTop = 1.5f;      // weak at the top: the funnel flares out into the wall cloud
		funnel.VortexHeight = 40.0f;
		funnel.VortexLift = 9.0f;
		funnel.VortexRadius = 40.0f;
		funnel.StartSize = { 1.4f, 2.6f };
		funnel.EndSizeScale = 3.2f;
		funnel.AngularVelocityDegrees = { -25.0f, 25.0f };
		funnel.StartColor = { 0.25f, 0.23f, 0.22f, 0.42f };
		funnel.EndColor = { 0.12f, 0.12f, 0.13f, 0.0f };
		funnel.FadeInFraction = 0.12f;
		funnel.Blend = ParticleBlend::Alpha;
		funnel.Sprite = ParticleSprite::Smoke;
		funnel.SoftDistance = 2.0f;
		funnel.Seed = seed * 41 + 1;
		m_Funnel = Make(funnel);

		ParticleEmitterDesc ring = funnel;
		ring.Radius = 5.0f;
		ring.ConeAngleDegrees = 70.0f;
		ring.Speed = { 3.0f, 8.0f };
		ring.MaxParticles = 900;
		ring.Lifetime = { 2.0f, 3.5f };
		ring.VortexSwirl = 18.0f;
		ring.VortexInward = 9.0f;
		ring.VortexInwardTop = 3.0f;
		ring.VortexHeight = 10.0f;
		ring.VortexLift = 4.0f;
		ring.StartSize = { 2.0f, 4.0f };
		ring.EndSizeScale = 2.0f;
		ring.StartColor = { 0.62f, 0.47f, 0.32f, 0.30f };
		ring.EndColor = { 0.5f, 0.38f, 0.27f, 0.0f };
		ring.Seed = seed * 43 + 1;
		m_FunnelBase = Make(ring);

		ParticleEmitterDesc debris = funnel;
		debris.Radius = 3.0f;
		debris.ConeAngleDegrees = 40.0f;
		debris.MaxParticles = 700;
		debris.Lifetime = { 3.0f, 5.0f };
		debris.Speed = { 3.0f, 10.0f };
		debris.VortexSwirl = 26.0f;
		debris.VortexInward = 34.0f;
		debris.VortexInwardTop = 4.0f;
		debris.VortexLift = 14.0f;
		debris.StartSize = { 0.08f, 0.22f };
		debris.EndSizeScale = 1.0f;
		debris.AngularVelocityDegrees = { -300.0f, 300.0f };
		debris.StartColor = { 0.10f, 0.08f, 0.06f, 0.95f };
		debris.EndColor = { 0.10f, 0.08f, 0.06f, 0.0f };
		debris.FadeInFraction = 0.02f;
		debris.Sprite = ParticleSprite::SoftCircle;
		debris.SoftDistance = 0.0f;
		debris.Seed = seed * 47 + 1;
		m_Debris = Make(debris);

		ParticleEmitterDesc cloud = funnel;
		cloud.Shape = ParticleShape::Sphere;
		cloud.Radius = 14.0f;
		cloud.MaxParticles = 500;
		cloud.Lifetime = { 9.0f, 13.0f };
		cloud.Speed = { 0.0f, 0.5f };
		cloud.Drag = 0.2f;
		cloud.VortexSwirl = 7.0f;
		cloud.VortexInward = 0.0f;
		cloud.VortexInwardTop = 0.0f;
		cloud.VortexLift = 0.0f;
		cloud.VortexRadius = 40.0f;
		cloud.StartSize = { 10.0f, 18.0f };
		cloud.EndSizeScale = 1.3f;
		cloud.StartColor = { 0.10f, 0.11f, 0.13f, 0.30f };
		cloud.EndColor = { 0.10f, 0.11f, 0.13f, 0.0f };
		cloud.FadeInFraction = 0.3f;
		cloud.SoftDistance = 0.0f;
		cloud.Seed = seed * 53 + 1;
		m_StormCloud = Make(cloud);
	}

	std::unique_ptr<ParticleSystem> Weather::Make(const ParticleEmitterDesc& desc)
	{
		return std::make_unique<ParticleSystem>(desc);
	}

	void Weather::SetPreset(WeatherPreset preset)
	{
		m_Target = {};
		switch (preset)
		{
		case WeatherPreset::Clear: m_Wind = { 1.0f, 0.0f, 0.3f }; break;
		case WeatherPreset::LightRain: m_Target.Rain = 0.3f; m_Wind = { 1.5f, 0.0f, 0.5f }; break;
		case WeatherPreset::Rain: m_Target.Rain = 0.75f; m_Target.Storm = 0.35f; m_Wind = { 3.0f, 0.0f, 1.0f }; break;
		case WeatherPreset::Thunderstorm: m_Target.Rain = 1.0f; m_Target.Storm = 1.0f; m_Wind = { 7.0f, 0.0f, 2.5f }; break;
		case WeatherPreset::Snowfall: m_Target.Snow = 0.6f; m_Wind = { 1.2f, 0.0f, 0.4f }; break;
		case WeatherPreset::Blizzard: m_Target.Snow = 1.0f; m_Target.Storm = 0.4f; m_Wind = { 12.0f, 0.0f, 3.0f }; break;
		case WeatherPreset::Dust: m_Target.Dust = 0.8f; m_Wind = { 2.5f, 0.0f, 0.8f }; break;
		case WeatherPreset::Sandstorm: m_Target.Sandstorm = 1.0f; m_Target.Dust = 0.6f; m_Wind = { 16.0f, 0.0f, 3.0f }; break;
		case WeatherPreset::Tornado: m_Target.Tornado = 1.0f; m_Target.Storm = 0.9f; m_Target.Dust = 0.3f; m_Wind = { 5.0f, 0.0f, 1.5f }; break;
		}
	}

	void Weather::Snap()
	{
		m_Level = m_Target;
		m_Wetness = std::max(m_Level.Rain, m_Level.Snow * 0.3f);
		m_SnowCover = m_Level.Snow > 0.05f ? 0.9f : 0.0f;
	}

	void Weather::SetGroundFunction(std::function<float(float, float)> fn)
	{
		m_GroundFn = std::move(fn);
		for (auto* s : { m_Rain.get(), m_SnowFlakes.get() })
			s->Desc.GroundHeightFn = m_GroundFn;
	}

	void Weather::SetBase(const Scene& scene)
	{
		m_BaseEnv = scene.Env;
		m_BaseSun = scene.Sun;
		m_BaseParticles = scene.Particles.size();
		m_BaseBatches = scene.ParticleBatches.size();
		m_BaseLights = scene.PointLights.size();
		m_HasBase = true;
	}

	size_t Weather::GetLiveParticleCount() const
	{
		size_t n = 0;
		for (const auto* s : { m_Rain.get(), m_Splash.get(), m_Mist.get(), m_SnowFlakes.get(), m_Motes.get(), m_Haze.get(),
			m_SandStreaks.get(), m_SandClouds.get(), m_Funnel.get(), m_FunnelBase.get(), m_Debris.get(), m_StormCloud.get() })
			n += s->GetAliveCount();
		return n;
	}

	void Weather::Retune(const Camera& camera)
	{
		const glm::vec3 cam = camera.Position;
		const glm::vec3 wind = Horizontal(m_Wind);
		const float windSpeed = glm::length(wind);
		const glm::vec3 windDir = windSpeed > 1e-3f ? wind / windSpeed : glm::vec3(1, 0, 0);
		const float ground = GroundAt(cam.x, cam.z);   // terrain height under the camera
		const float top = std::max(cam.y, ground) + 15.0f;
		const float snowTop = std::max(cam.y, ground) + 9.0f;

		// Rain falls at an angle set by the wind and lands on GroundY.
		{
			ParticleEmitterDesc& d = m_Rain->Desc;
			d.Direction = glm::normalize(glm::vec3(wind.x * 0.06f, -1.0f, wind.z * 0.06f));
			d.Rate = 7000.0f * m_Level.Rain;
			d.GroundY = GroundY;
			m_Rain->SetPosition({ cam.x - wind.x * 0.5f, top, cam.z - wind.z * 0.5f });
			m_Rain->Desc.Lifetime = { (top - ground + 6.0f) / 21.0f, (top - ground + 6.0f) / 16.0f };
		}
		m_Splash->SetPosition({ cam.x, ground, cam.z });
		m_Mist->Desc.Rate = 7.0f * m_Level.Rain;
		m_Mist->Desc.Wind = wind * 0.1f;
		m_Mist->SetPosition({ cam.x, ground + 0.4f, cam.z });

		{
			ParticleEmitterDesc& d = m_SnowFlakes->Desc;
			d.Rate = 2600.0f * m_Level.Snow;
			d.Wind = wind * 0.55f;
			d.GroundY = GroundY;
			d.Lifetime = { (snowTop - ground + 6.0f) / 1.1f, (snowTop - ground + 6.0f) / 0.6f };
			m_SnowFlakes->SetPosition({ cam.x - wind.x, snowTop, cam.z - wind.z });
		}

		m_Motes->Desc.Rate = 140.0f * m_Level.Dust;
		m_Motes->Desc.Wind = wind * 0.3f;
		m_Motes->SetPosition({ cam.x, ground + 2.5f, cam.z });
		m_Haze->Desc.Rate = 6.0f * m_Level.Dust;
		m_Haze->Desc.Wind = wind * 0.2f;
		m_Haze->SetPosition({ cam.x, ground + 1.5f, cam.z });

		// Sandstorm: boxes are upwind of the camera and shoot along the wind.
		const float yawRad = std::atan2(windDir.z, windDir.x);
		const glm::mat4 toWind = glm::rotate(glm::mat4(1.0f), -yawRad, glm::vec3(0, 1, 0));
		for (auto* system : { m_SandStreaks.get(), m_SandClouds.get() })
		{
			system->Desc.Direction = windDir;
			glm::mat4 t = glm::translate(glm::mat4(1.0f), { cam.x - windDir.x * 20.0f, ground + 3.5f, cam.z - windDir.z * 20.0f }) * toWind;
			system->SetTransform(t);
			system->Desc.Direction = { 1.0f, 0.0f, 0.0f };   // emitter-local; the rotation above aims it
		}
		m_SandStreaks->Desc.Rate = 7500.0f * m_Level.Sandstorm;
		m_SandStreaks->Desc.Speed = { windSpeed * 0.9f + 4.0f, windSpeed * 1.3f + 6.0f };
		m_SandClouds->Desc.Rate = 28.0f * m_Level.Sandstorm;

		// Tornado touches down at its position.
		const glm::vec3 base = { m_TornadoPosition.x, GroundAt(m_TornadoPosition.x, m_TornadoPosition.z), m_TornadoPosition.z };
		m_Funnel->SetPosition(base);
		m_Funnel->Desc.Rate = 420.0f * m_Level.Tornado;
		m_FunnelBase->SetPosition(base);
		m_FunnelBase->Desc.Rate = 150.0f * m_Level.Tornado;
		m_Debris->SetPosition(base + glm::vec3(0.0f, 0.3f, 0.0f));
		m_Debris->Desc.Rate = 90.0f * m_Level.Tornado;
		m_StormCloud->SetPosition(base + glm::vec3(0.0f, 46.0f, 0.0f));
		m_StormCloud->Desc.Rate = 26.0f * std::max(m_Level.Tornado, m_Level.Storm * 0.4f);
	}

	void Weather::Update(float dt, const Camera& camera)
	{
		if (dt <= 0.0f)
			return;
		m_Time += dt;

		const float step = dt / std::max(TransitionSeconds, 0.01f);
		m_Level.Rain = Approach(m_Level.Rain, m_Target.Rain, step);
		m_Level.Snow = Approach(m_Level.Snow, m_Target.Snow, step);
		m_Level.Dust = Approach(m_Level.Dust, m_Target.Dust, step);
		m_Level.Sandstorm = Approach(m_Level.Sandstorm, m_Target.Sandstorm, step);
		m_Level.Storm = Approach(m_Level.Storm, m_Target.Storm, step);
		m_Level.Tornado = Approach(m_Level.Tornado, m_Target.Tornado, step);

		// Surfaces get wet quickly and dry slowly.
		const float wetTarget = std::max(m_Level.Rain, m_Level.Snow * 0.3f);
		m_Wetness = Approach(m_Wetness, wetTarget, dt * (wetTarget > m_Wetness ? 0.25f : 0.03f));

		// Snow piles up while it snows and melts in about fifteen seconds once it stops (it used to take minutes, which looked like a bug).
		m_SnowCover = Approach(m_SnowCover, m_Level.Snow > 0.05f ? 1.0f : 0.0f, dt * (m_Level.Snow > 0.05f ? 0.04f * m_Level.Snow : 0.07f));

		Retune(camera);
		for (auto* s : { m_Rain.get(), m_Splash.get(), m_Mist.get(), m_SnowFlakes.get(), m_Motes.get(), m_Haze.get(),
			m_SandStreaks.get(), m_SandClouds.get(), m_Funnel.get(), m_FunnelBase.get(), m_Debris.get(), m_StormCloud.get() })
			s->Update(dt);

		// Lightning follows the camera and gets more frequent as the storm builds.
		// Strikes land in the part of the sky the camera is looking at (so they are seen), not behind it.
		glm::vec3 forward = camera.Target - camera.Position;
		forward.y = 0.0f;
		forward = glm::length(forward) > 1e-3f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
		m_Lightning.Desc.AreaRadius = 38.0f;
		m_Lightning.Desc.Center = { camera.Position.x + forward.x * 52.0f, GroundAt(camera.Position.x, camera.Position.z), camera.Position.z + forward.z * 52.0f };
		m_Lightning.Desc.Automatic = m_Level.Storm > 0.55f;
		m_Lightning.Desc.Interval = { glm::mix(9.0f, 3.0f, m_Level.Storm), glm::mix(18.0f, 7.0f, m_Level.Storm) };
		m_Lightning.SetListener(camera.Position);
		m_Lightning.Update(dt);
	}

	void Weather::Apply(Scene& scene) const
	{
		if (m_HasBase)
		{
			scene.Particles.resize(m_BaseParticles);
			scene.ParticleBatches.resize(m_BaseBatches);
			scene.PointLights.resize(m_BaseLights);
		}
		Environment env = m_HasBase ? m_BaseEnv : scene.Env;
		DirectionalLight sun = m_HasBase ? m_BaseSun : scene.Sun;

		// Overcast: how much each condition darkens and greys the sky.
		const float overcast = glm::clamp(std::max({ m_Level.Storm, m_Level.Rain * 0.8f, m_Level.Snow * 0.55f,
			m_Level.Sandstorm * 0.9f, m_Level.Tornado, m_Level.Dust * 0.35f }), 0.0f, 1.0f);
		const glm::vec3 stormTop(0.09f, 0.10f, 0.13f), stormHorizon(0.23f, 0.25f, 0.29f);
		const glm::vec3 sandTop(0.30f, 0.19f, 0.10f), sandHorizon(0.50f, 0.34f, 0.19f);
		const glm::vec3 snowTop(0.42f, 0.45f, 0.50f), snowHorizon(0.70f, 0.72f, 0.75f);

		glm::vec3 top = glm::mix(env.SkyTop, stormTop, overcast);
		glm::vec3 horizon = glm::mix(env.SkyHorizon, stormHorizon, overcast);
		top = glm::mix(top, snowTop, m_Level.Snow * 0.8f);
		horizon = glm::mix(horizon, snowHorizon, m_Level.Snow * 0.8f);
		top = glm::mix(top, sandTop, m_Level.Sandstorm);
		horizon = glm::mix(horizon, sandHorizon, m_Level.Sandstorm);
		env.SkyTop = top;
		env.SkyHorizon = horizon;
		env.SkyGround = glm::mix(env.SkyGround, horizon * 0.5f, overcast * 0.6f);
		env.Intensity *= 1.0f - 0.45f * overcast;
		sun.Intensity *= 1.0f - 0.8f * overcast;
		sun.CastShadows = sun.CastShadows && overcast < 0.85f;

		// Fog: rain and snow veil the distance, sand and dust choke it.
		const float fogDensity = std::max({ env.FogDensity, 0.010f * m_Level.Rain + 0.006f * m_Level.Storm,
			0.020f * m_Level.Snow, 0.020f * m_Level.Sandstorm, 0.014f * m_Level.Dust, 0.006f * m_Level.Tornado });
		const float weights = m_Level.Rain + m_Level.Snow + m_Level.Sandstorm + m_Level.Dust + 1e-4f;
		glm::vec3 fogColor = (glm::vec3(0.38f, 0.42f, 0.48f) * m_Level.Rain + glm::vec3(0.74f, 0.76f, 0.80f) * m_Level.Snow
			+ glm::vec3(0.60f, 0.40f, 0.22f) * m_Level.Sandstorm + glm::vec3(0.70f, 0.58f, 0.42f) * m_Level.Dust) / weights;
		if (fogDensity > env.FogDensity + 1e-5f)
			env.FogColor = glm::mix(env.FogColor, glm::mix(fogColor, horizon, 0.25f), glm::clamp(weights, 0.0f, 1.0f));
		env.FogDensity = fogDensity;
		env.Wetness = std::max(env.Wetness, m_Wetness * (1.0f - m_SnowCover));
		env.SnowCover = std::max(env.SnowCover, m_SnowCover);
		env.CloudCover = std::max(env.CloudCover, overcast);
		env.Stars *= 1.0f - 0.9f * overcast;
		env.Time = m_Time;
		env.Wind = Horizontal(m_Wind);   // grass and trees sway with the weather wind

		// Lightning: brighten the sky for the flash.
		env.Intensity += m_Lightning.GetFlash() * m_Lightning.Desc.SkyBoost;
		scene.Env = env;
		scene.Sun = sun;

		m_Lightning.AddToScene(scene);
		for (const auto* s : { m_Rain.get(), m_Splash.get(), m_Mist.get(), m_SnowFlakes.get(), m_Motes.get(), m_Haze.get(),
			m_SandStreaks.get(), m_SandClouds.get(), m_StormCloud.get(), m_Funnel.get(), m_FunnelBase.get(), m_Debris.get() })
			scene.Particles.push_back(s);
	}
}
