// CPU-only tests for the new particle forces, lightning and the weather controller.
#include "GB/Effects/Lightning.h"
#include "GB/Effects/Weather.h"
#include "GB/Renderer/Particles.h"
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
	constexpr float kDt = 1.0f / 60.0f;

	template <class T> void Run(T& s, float seconds)
	{
		const int steps = static_cast<int>(std::lround(seconds / kDt));
		for (int i = 0; i < steps; ++i)
			s.Update(kDt);
	}

	GB::ParticleEmitterDesc Quiet()
	{
		GB::ParticleEmitterDesc d;
		d.Shape = GB::ParticleShape::Point;
		d.ConeAngleDegrees = 0.0f;
		d.Rate = 0.0f;
		d.Lifetime = { 100.0f, 100.0f };
		d.Speed = { 0.0f, 0.0f };
		d.MaxParticles = 100000;
		return d;
	}
}

int main()
{
	// ---------------------------------------------------------------- particle forces
	{
		// Turbulence moves particles, deterministically, and differently at different places.
		GB::ParticleEmitterDesc d = Quiet();
		d.Shape = GB::ParticleShape::Box;
		d.BoxSize = { 40.0f, 40.0f, 40.0f };
		d.TurbulenceStrength = 4.0f;
		GB::ParticleSystem a(d), b(d), start(d);
		a.Burst(200); b.Burst(200); start.Burst(200);
		Run(a, 1.0f); Run(b, 1.0f);
		glm::vec3 meanDisp(0.0f);
		float spread = 0.0f;
		for (size_t i = 0; i < a.GetAliveCount(); ++i)
		{
			const glm::vec3 disp = a.GetParticles()[i].Position - start.GetParticles()[i].Position;
			meanDisp += disp;
			spread += glm::length(disp);
			if (a.GetParticles()[i].Position != b.GetParticles()[i].Position) { Check(false, "turbulence is deterministic"); break; }
		}
		meanDisp /= 200.0f;
		float variance = 0.0f;
		for (size_t i = 0; i < a.GetAliveCount(); ++i)
			variance += glm::dot(a.GetParticles()[i].Position - start.GetParticles()[i].Position - meanDisp, a.GetParticles()[i].Position - start.GetParticles()[i].Position - meanDisp);
		Check(spread / 200.0f > 0.5f, "turbulence displaces particles");
		Check(variance / 200.0f > 0.2f, "turbulence differs from place to place");
		d.TurbulenceStrength = 0.0f;
		GB::ParticleSystem still(d);
		still.Burst(10);
		const glm::vec3 before = still.GetParticles()[0].Position;
		Run(still, 1.0f);
		Check(glm::length(still.GetParticles()[0].Position - before) < 1e-5f, "without forces particles stay put");
	}
	{
		// Vortex: a particle on +X of the axis is pushed tangentially (counter-clockwise seen from +Y => -Z) and pulled in.
		GB::ParticleEmitterDesc d = Quiet();
		d.Shape = GB::ParticleShape::Point;
		d.Vortex = true;
		d.VortexSwirl = 10.0f;
		d.VortexInward = 0.0f;
		d.VortexInwardTop = 0.0f;
		d.VortexRadius = 50.0f;
		d.VortexLift = 0.0f;
		GB::ParticleSystem v(d);
		v.EmitAt({ 5.0f, 0.0f, 0.0f }, 1);
		Run(v, 0.5f);
		const glm::vec3 vel = v.GetParticles()[0].Velocity;
		Check(vel.z < -2.0f && std::fabs(vel.x) < 0.5f, "vortex swirl accelerates tangentially (cross(up, +X) = -Z)");

		GB::ParticleEmitterDesc pull = d;
		pull.VortexSwirl = 0.0f;
		pull.VortexInward = 8.0f;
		pull.VortexInwardTop = 8.0f;
		GB::ParticleSystem in(pull);
		in.EmitAt({ 5.0f, 0.0f, 0.0f }, 1);
		Run(in, 0.5f);
		Check(in.GetParticles()[0].Velocity.x < -2.0f, "vortex inward pull drags particles toward the axis");

		GB::ParticleEmitterDesc lift = d;
		lift.VortexSwirl = 0.0f;
		lift.VortexLift = 6.0f;
		GB::ParticleSystem up(lift);
		up.EmitAt({ 2.0f, 0.0f, 0.0f }, 1);
		Run(up, 0.5f);
		Check(up.GetParticles()[0].Velocity.y > 2.0f, "vortex lift accelerates along the axis");

		GB::ParticleEmitterDesc far = d;
		GB::ParticleSystem outside(far);
		outside.EmitAt({ 80.0f, 0.0f, 0.0f }, 1);   // beyond VortexRadius
		Run(outside, 0.5f);
		Check(glm::length(outside.GetParticles()[0].Velocity) < 1e-4f, "no vortex force outside its radius");

		GB::ParticleSystem moved(d);
		moved.SetPosition({ 100.0f, 0.0f, 0.0f });
		moved.EmitAt({ 105.0f, 0.0f, 0.0f }, 1);
		Run(moved, 0.5f);
		Check(moved.GetParticles()[0].Velocity.z < -2.0f, "the vortex axis follows the emitter position");
	}
	{
		// Ground collision: kill, bounce, and hit reports feeding a sub-emitter.
		GB::ParticleEmitterDesc d = Quiet();
		d.Gravity = { 0, -10, 0 };
		d.GravityScale = 1.0f;
		d.CollideGround = true;
		d.GroundY = 0.0f;
		GB::ParticleSystem drop(d);
		drop.EmitAt({ 1.0f, 2.0f, 3.0f }, 1);
		size_t hits = 0;
		for (int i = 0; i < 120; ++i)
		{
			drop.Update(kDt);
			hits += drop.GetHits().size();
		}
		Check(hits == 1 && drop.GetAliveCount() == 0, "a falling particle hits the ground once and dies");

		GB::ParticleEmitterDesc bounceDesc = d;
		bounceDesc.Bounce = 0.5f;
		GB::ParticleSystem ball(bounceDesc);
		ball.EmitAt({ 0.0f, 5.0f, 0.0f }, 1);
		bool bounced = false;
		float maxAfter = 0.0f;
		bool hit = false;
		for (int i = 0; i < 240; ++i)
		{
			ball.Update(kDt);
			if (!ball.GetHits().empty()) hit = true;
			if (hit && ball.GetAliveCount() > 0)
			{
				maxAfter = std::max(maxAfter, ball.GetParticles()[0].Position.y);
				bounced = true;
			}
			if (ball.GetAliveCount() > 0)
				Check(ball.GetParticles()[0].Position.y >= -1e-3f, "bouncing particles never go under the ground");
		}
		Check(bounced && maxAfter > 0.3f && maxAfter < 3.0f, "a bounce rises to a lower height than the drop");

		GB::ParticleEmitterDesc rain = d;
		rain.Rate = 600.0f;
		rain.Shape = GB::ParticleShape::Box;
		rain.BoxSize = { 4, 0, 4 };
		GB::ParticleSystem sky(rain);
		sky.SetPosition({ 0, 4, 0 });
		GB::ParticleEmitterDesc splashDesc = Quiet();
		splashDesc.Lifetime = { 0.5f, 0.5f };
		GB::ParticleSystem splash(splashDesc);
		sky.SetHitEmitter(&splash, 2, 1.0f);
		size_t emitted = 0;
		for (int i = 0; i < 180; ++i)
		{
			sky.Update(kDt);
			splash.Update(kDt);
			emitted += sky.GetHits().size();
		}
		Check(emitted > 100 && splash.GetAliveCount() > 20, "ground hits spawn particles in the hit emitter");
		for (const GB::Particle& p : splash.GetParticles())
			if (std::fabs(p.Position.y) > 0.1f && p.Age < 0.01f) { Check(false, "splashes appear at the ground"); break; }
	}
	{
		GB::ParticleEmitterDesc d = Quiet();
		d.Shape = GB::ParticleShape::Cone;
		d.ConeAngleDegrees = 20.0f;
		d.Speed = { 1.0f, 1.0f };
		GB::ParticleSystem s(d);
		s.EmitAt({ 7.0f, 1.0f, -2.0f }, 50);
		bool at = true;
		for (const GB::Particle& p : s.GetParticles())
			at = at && glm::length(p.Position - glm::vec3(7, 1, -2)) < 1e-4f;
		Check(s.GetAliveCount() == 50 && at, "EmitAt spawns at the given world position");
	}

	// ---------------------------------------------------------------- lightning
	{
		GB::LightningDesc d;
		d.Automatic = false;
		d.Seed = 5;
		GB::Lightning l(d);
		Check(!l.IsActive() && l.GetFlash() == 0.0f, "starts idle");
		const glm::vec3 target(10.0f, 0.0f, -20.0f);
		l.SetListener({ 0.0f, 0.0f, 0.0f });
		l.Strike(&target);
		Check(l.IsActive() && l.GetFlash() > 0.9f, "a strike lights up immediately");
		const auto& seg = l.GetSegments();
		Check(seg.size() >= (1u << d.Levels), "the bolt has at least 2^Levels segments");
		std::vector<GB::LightningSegment> main;
		for (const auto& sg : seg) if (sg.Width == 1.0f) main.push_back(sg);
		bool connected = main.size() == (1u << d.Levels);
		for (size_t i = 0; i + 1 < main.size(); ++i)
			connected = connected && glm::length(main[i].B - main[i + 1].A) < 1e-3f;
		Check(connected, "main bolt segments chain end to end (2^Levels of them)");
		Check(glm::length(main.back().B - target) < 1e-3f, "the main bolt ends at the strike target");
		float lowest = 1e9f, highest = -1e9f;
		for (const auto& s : seg) { lowest = std::min(lowest, std::min(s.A.y, s.B.y)); highest = std::max(highest, std::max(s.A.y, s.B.y)); }
		Check(lowest >= -1e-4f && Near(lowest, 0.0f, 1e-3f) && Near(highest, d.CloudHeight, 4.0f), "bolt spans from the cloud to the ground and never goes under it");
		bool anyBranch = false;
		for (const auto& s : seg) anyBranch = anyBranch || s.Width < 1.0f;
		Check(anyBranch, "with these settings the bolt has branches");

		GB::Scene scene;
		l.AddToScene(scene);
		Check(scene.ParticleBatches.size() == 2 && scene.PointLights.size() == 1 && scene.PointLights[0].Intensity > 1000.0f, "an active bolt adds glow+core batches and a flash light");
		Check(scene.ParticleBatches[0].Instances->size() == seg.size(), "one instance per segment");

		// Flicker: brightness is not monotonic (several pulses) and ends at zero.
		float previous = l.GetFlash();
		int rises = 0;
		for (int i = 0; i < 40; ++i)
		{
			l.Update(0.01f);
			if (l.GetFlash() > previous + 1e-3f) ++rises;
			previous = l.GetFlash();
		}
		Check(rises >= 1, "the flash flickers (re-brightens after the first pulse)");
		l.Update(1.0f);
		Check(!l.IsActive() && l.GetFlash() == 0.0f, "the bolt fades out after its duration");
		GB::Scene idle;
		l.AddToScene(idle);
		Check(idle.ParticleBatches.empty() && idle.PointLights.empty(), "an idle lightning adds nothing");

		// Thunder arrives after distance / speed of sound (fresh object: earlier updates already delivered it).
		const float distance = glm::length(target);
		GB::Lightning t(d);
		t.SetListener({ 0.0f, 0.0f, 0.0f });
		t.Strike(&target);
		bool early = false;
		for (float time = 0.0f; time < distance / 343.0f - 0.03f; time += 0.01f)
		{
			t.Update(0.01f);
			early = early || !t.TakeThunder().empty();
		}
		Check(!early, "no thunder before the sound has travelled");
		for (int i = 0; i < 10; ++i) t.Update(0.01f);
		const auto thunder = t.TakeThunder();
		Check(thunder.size() == 1 && Near(thunder[0].Distance, distance, 1e-3f) && thunder[0].Loudness > 0.0f && thunder[0].Loudness <= 1.0f, "thunder event after the delay, with distance and loudness");
		Check(t.TakeThunder().empty(), "thunder is delivered once");

		// Farther strikes are quieter and later.
		GB::Lightning far(d);
		const glm::vec3 farTarget(0.0f, 0.0f, -400.0f);
		far.SetListener({ 0, 0, 0 });
		far.Strike(&farTarget);
		for (int i = 0; i < 150; ++i) far.Update(0.01f);
		const auto farThunder = far.TakeThunder();
		Check(farThunder.size() == 1 && farThunder[0].Loudness < thunder[0].Loudness, "distant thunder is quieter");

		// Determinism.
		GB::Lightning x(d), y(d);
		x.Strike(&target); y.Strike(&target);
		bool same = x.GetSegments().size() == y.GetSegments().size();
		for (size_t i = 0; same && i < x.GetSegments().size(); ++i) same = x.GetSegments()[i].B == y.GetSegments()[i].B;
		Check(same, "same seed gives the same bolt");

		// Automatic strikes.
		GB::LightningDesc autoDesc;
		autoDesc.Interval = { 1.0f, 1.0f };
		autoDesc.Seed = 9;
		GB::Lightning automatic(autoDesc);
		int strikes = 0;
		bool wasActive = false;
		for (int i = 0; i < 700; ++i)
		{
			automatic.Update(0.01f);
			if (automatic.IsActive() && !wasActive) ++strikes;
			wasActive = automatic.IsActive();
		}
		Check(strikes >= 5 && strikes <= 7, "automatic mode strikes about once per interval");
	}

	// ---------------------------------------------------------------- weather
	{
		GB::Camera cam;
		cam.Position = { 0.0f, 2.0f, 0.0f };
		GB::Scene scene;
		scene.Env.SkyTop = { 0.2f, 0.4f, 0.8f };
		scene.Sun.Intensity = 4.0f;
		scene.Particles.push_back(nullptr);   // pre-existing entries must survive Apply
		GB::Weather w(1);
		w.SetBase(scene);
		w.Update(0.016f, cam);
		w.Apply(scene);
		Check(scene.Env.FogDensity == 0.0f && scene.Env.Wetness == 0.0f && scene.Sun.Intensity == 4.0f, "clear weather leaves the scene untouched");
		Check(scene.Particles.size() == 1 + 12, "Apply appends the weather systems after the scene's own");
		w.Apply(scene);
		Check(scene.Particles.size() == 1 + 12, "Apply is idempotent");

		auto step = [&](GB::Weather& wx, float seconds)
		{
			const int n = static_cast<int>(std::lround(seconds / kDt));
			for (int i = 0; i < n; ++i)
				wx.Update(kDt, cam);
		};

		// --- Rain: eases in, darkens the sky, adds fog, wets the ground, splashes.
		w.SetPreset(GB::WeatherPreset::Rain);
		w.TransitionSeconds = 4.0f;
		step(w, 1.0f);
		Check(w.GetRain() > 0.2f && w.GetRain() < 0.3f, "rain level eases in gradually (0.25 per second here)");
		step(w, 5.0f);
		Check(Near(w.GetRain(), 0.75f, 1e-3f), "rain reaches its target");
		w.Apply(scene);
		Check(scene.Env.FogDensity > 0.005f, "rain adds fog");
		Check(scene.Env.Wetness > 0.3f, "rain wets the surfaces");
		Check(scene.Sun.Intensity < 3.0f, "rain dims the sun");
		Check(scene.Env.SkyTop.b < 0.8f && (scene.Env.SkyTop.b - scene.Env.SkyTop.r) < 0.6f * 0.8f, "rain darkens and desaturates the sky");
		Check(w.GetRainSystem().GetAliveCount() > 1500, "a few thousand raindrops are alive");
		Check(Near(w.GetRainSystem().GetTransform()[3].x, cam.Position.x, 2.0f), "the rain volume follows the camera");
		cam.Position = { 50.0f, 2.0f, 30.0f };
		step(w, 0.1f);
		Check(Near(w.GetRainSystem().GetTransform()[3].x, 50.0f, 2.0f) && Near(w.GetRainSystem().GetTransform()[3].z, 30.0f, 2.0f), "...even after the camera moves");
		cam.Position = { 0.0f, 2.0f, 0.0f };

		// Drops reach the ground and are removed there.
		bool belowGround = false;
		for (const GB::Particle& p : w.GetRainSystem().GetParticles())
			belowGround = belowGround || p.Position.y < -0.01f;
		Check(!belowGround, "no raindrop falls through the ground");

		// --- Drying is slow.
		const float wet = w.GetWetness();
		w.SetPreset(GB::WeatherPreset::Clear);
		step(w, 5.0f);
		Check(w.GetWetness() < wet && w.GetWetness() > 0.5f * wet, "surfaces dry out slowly after the rain stops");
		Check(w.GetRain() < 0.75f, "rain fades out");

		// --- Snow accumulates.
		GB::Weather snow(2);
		snow.SetBase(scene);
		snow.SetPreset(GB::WeatherPreset::Snowfall);
		snow.Snap();
		step(snow, 10.0f);
		GB::Scene snowScene;
		snow.SetBase(snowScene);
		snow.Apply(snowScene);
		Check(snow.GetSnowCover() > 0.1f && snowScene.Env.SnowCover > 0.1f, "snow cover builds up while it snows");
		Check(snow.GetSnowSystem().GetAliveCount() > 3000, "thousands of flakes");
		// Switching to clear weather melts the snow cover in well under half a minute.
		snow.SetPreset(GB::WeatherPreset::Clear);
		step(snow, 30.0f);
		Check(snow.GetSnowCover() < 0.05f, "snow cover melts away within 30 s of clear weather");

		// --- Sandstorm: tan fog, dense.
		GB::Weather sand(3);
		GB::Scene sandScene;
		sand.SetBase(sandScene);
		sand.SetPreset(GB::WeatherPreset::Sandstorm);
		sand.Snap();
		step(sand, 2.0f);
		sand.Apply(sandScene);
		Check(sandScene.Env.FogDensity >= 0.015f && sandScene.Env.FogColor.r > sandScene.Env.FogColor.b + 0.15f, "sandstorm: thick tan fog");

		// --- Tornado: the funnel spins counter-clockwise (seen from above) and widens with height.
		GB::Weather tw(4);
		GB::Scene tornadoScene;
		tw.SetBase(tornadoScene);
		tw.SetTornadoPosition({ 0.0f, 0.0f, -40.0f });
		tw.SetPreset(GB::WeatherPreset::Tornado);
		tw.Snap();
		step(tw, 12.0f);
		const glm::vec3 axis(0.0f, 0.0f, -40.0f);
		double momentum = 0.0, lowR = 0.0, highR = 0.0;
		int lowN = 0, highN = 0;
		for (const GB::Particle& p : tw.GetTornadoFunnel().GetParticles())
		{
			const glm::vec3 r(p.Position.x - axis.x, 0.0f, p.Position.z - axis.z);
			momentum += r.z * p.Velocity.x - r.x * p.Velocity.z;
			if (p.Position.y < 6.0f) { lowR += glm::length(r); ++lowN; }
			else if (p.Position.y > 18.0f) { highR += glm::length(r); ++highN; }
		}
		Check(tw.GetTornadoFunnel().GetAliveCount() > 1000, "the funnel is populated");
		Check(momentum > 0.0, "the funnel rotates counter-clockwise seen from above");
		Check(lowN > 20 && highN > 20 && highR / highN > 1.3 * lowR / lowN, "the funnel is wider at the top than at the ground");

		// --- Thunderstorm: lightning strikes by itself and thunder follows.
		GB::Weather storm(6);
		GB::Scene stormScene;
		storm.SetBase(stormScene);
		storm.SetPreset(GB::WeatherPreset::Thunderstorm);
		storm.Snap();
		int thunderEvents = 0;
		bool flashSeen = false;
		for (int i = 0; i < 60 * 40; ++i)
		{
			storm.Update(kDt, cam);
			flashSeen = flashSeen || storm.GetLightning().GetFlash() > 0.5f;
			thunderEvents += static_cast<int>(storm.GetLightning().TakeThunder().size());
		}
		Check(flashSeen && thunderEvents >= 2, "a thunderstorm produces lightning and thunder on its own");
		storm.Apply(stormScene);
		Check(stormScene.Env.FogDensity > 0.008f && stormScene.Sun.Intensity < 1.0f, "a thunderstorm is dark and foggy");

		// --- Snap jumps straight to the targets.
		GB::Weather quick(7);
		quick.SetPreset(GB::WeatherPreset::Blizzard);
		quick.Snap();
		Check(Near(quick.GetSnow(), 1.0f, 1e-6f), "Snap skips the transition");
	}
	return failures;
}
