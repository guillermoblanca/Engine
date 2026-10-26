// CPU-only tests of the particle simulation and the data handed to the renderer.
#include "GB/Renderer/Particles.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
	constexpr float kDt = 1.0f / 60.0f;

	void Run(GB::ParticleSystem& s, float seconds)
	{
		const int steps = static_cast<int>(std::lround(seconds / kDt));
		for (int i = 0; i < steps; ++i)
			s.Update(kDt);
	}

	// A quiet emitter: long-lived, motionless, no emission unless the test asks for it.
	GB::ParticleEmitterDesc Base()
	{
		GB::ParticleEmitterDesc d;
		d.Shape = GB::ParticleShape::Point;
		d.ConeAngleDegrees = 0.0f;
		d.Rate = 0.0f;
		d.Lifetime = { 100.0f, 100.0f };
		d.Speed = { 0.0f, 0.0f };
		d.GravityScale = 0.0f;
		d.Drag = 0.0f;
		d.MaxParticles = 100000;
		return d;
	}
}

int main()
{
	// ---- determinism
	{
		GB::ParticleEmitterDesc d = GB::ParticlePresets::Fire();
		GB::ParticleSystem a(d), b(d);
		Run(a, 2.0f); Run(b, 2.0f);
		bool same = a.GetAliveCount() == b.GetAliveCount() && a.GetAliveCount() > 0;
		for (size_t i = 0; same && i < a.GetAliveCount(); ++i)
			same = a.GetParticles()[i].Position == b.GetParticles()[i].Position && a.GetParticles()[i].Age == b.GetParticles()[i].Age;
		Check(same, "same seed and same updates give identical particles");

		d.Seed = 99;
		GB::ParticleSystem c(d);
		Run(c, 2.0f);
		Check(a.GetParticles()[0].Position != c.GetParticles()[0].Position, "a different seed gives different particles");

		a.Reset();
		Check(a.GetAliveCount() == 0, "Reset clears the particles");
		Run(a, 2.0f);
		bool again = a.GetAliveCount() == b.GetAliveCount();
		for (size_t i = 0; again && i < a.GetAliveCount(); ++i)
			again = a.GetParticles()[i].Position == b.GetParticles()[i].Position;
		Check(again, "Reset re-seeds: the run repeats exactly");
	}

	// ---- emission rate and lifetime
	{
		GB::ParticleEmitterDesc d = Base();
		d.Rate = 100.0f;
		GB::ParticleSystem s(d);
		Run(s, 1.0f);
		Check(std::abs(static_cast<int>(s.GetAliveCount()) - 100) <= 2, "100/s for 1 s -> about 100 particles");

		d.Lifetime = { 0.5f, 0.5f };
		d.Rate = 10.0f;
		GB::ParticleSystem mortal(d);
		Run(mortal, 3.0f);
		Check(std::abs(static_cast<int>(mortal.GetAliveCount()) - 5) <= 1, "10/s with 0.5 s lifetime -> about 5 alive");

		d.Lifetime = { 100.0f, 100.0f };
		d.Rate = 1000.0f;
		d.MaxParticles = 50;
		GB::ParticleSystem capped(d);
		Run(capped, 1.0f);
		Check(capped.GetAliveCount() == 50, "MaxParticles caps the population");
	}

	// ---- bursts, looping, finishing
	{
		GB::ParticleEmitterDesc d = Base();
		d.Duration = 2.0f;
		d.Bursts = { { 0.0f, 50 }, { 1.0f, 20 } };
		GB::ParticleSystem s(d);
		s.Update(kDt);
		Check(s.GetAliveCount() == 50, "burst at t=0 fires on the first update");
		Run(s, 0.5f);
		Check(s.GetAliveCount() == 50, "no burst before t=1");
		Run(s, 0.6f);
		Check(s.GetAliveCount() == 70, "burst at t=1");
		Run(s, 1.0f);   // total ~2.1 s: the loop restarted and fired the t=0 burst again
		Check(s.GetAliveCount() == 120, "looping repeats the bursts");

		d.Looping = false;
		GB::ParticleSystem once(d);
		Run(once, 5.0f);
		Check(once.GetAliveCount() == 70, "a non-looping system emits its bursts once");
		Check(!once.IsFinished(), "not finished while particles are alive");

		GB::ParticleEmitterDesc e = Base();
		e.Looping = false;
		e.Duration = 1.0f;
		e.Rate = 100.0f;
		e.Lifetime = { 0.4f, 0.4f };
		GB::ParticleSystem finite(e);
		Run(finite, 0.9f);
		const size_t during = finite.GetAliveCount();
		Run(finite, 3.0f);
		Check(during > 10 && finite.GetAliveCount() == 0 && finite.IsFinished(), "non-looping emitter stops and finishes");

		GB::ParticleSystem stopped(Base());
		GB::ParticleEmitterDesc r = Base(); r.Rate = 100.0f;
		GB::ParticleSystem sys(r);
		Run(sys, 0.5f);
		const size_t n = sys.GetAliveCount();
		sys.Stop();
		Run(sys, 0.5f);
		Check(sys.GetAliveCount() == n, "Stop() ends emission but keeps live particles");
		sys.Play();
		Run(sys, 0.5f);
		Check(sys.GetAliveCount() > n, "Play() resumes emission");

		GB::ParticleSystem manual(Base());
		manual.Burst(7);
		Check(manual.GetAliveCount() == 7, "Burst() emits immediately");
	}

	// ---- physics
	{
		GB::ParticleEmitterDesc d = Base();
		d.Gravity = { 0, -10, 0 };
		d.GravityScale = 1.0f;
		GB::ParticleSystem s(d);
		s.Burst(1);
		Run(s, 1.0f);
		Check(Near(s.GetParticles()[0].Position.y, -5.0f, 0.2f), "free fall: y = -g t^2 / 2");

		GB::ParticleEmitterDesc buoy = d;
		buoy.GravityScale = -0.5f;
		GB::ParticleSystem up(buoy);
		up.Burst(1);
		Run(up, 1.0f);
		Check(up.GetParticles()[0].Position.y > 2.0f, "negative gravity scale rises");

		GB::ParticleEmitterDesc drag = Base();
		drag.Direction = { 1, 0, 0 };
		drag.Speed = { 10.0f, 10.0f };
		drag.Drag = 1.0f;
		GB::ParticleSystem dr(drag);
		dr.Burst(1);
		Run(dr, 1.0f);
		Check(Near(dr.GetParticles()[0].Velocity.x, 10.0f * std::exp(-1.0f), 0.1f), "drag decays velocity exponentially");

		GB::ParticleEmitterDesc wind = Base();
		wind.Wind = { 2, 0, 0 };
		GB::ParticleSystem w(wind);
		w.Burst(1);
		Run(w, 1.0f);
		Check(Near(w.GetParticles()[0].Velocity.x, 2.0f, 0.05f), "wind accelerates particles");
	}

	// ---- shapes
	{
		GB::ParticleEmitterDesc d = Base();
		d.Shape = GB::ParticleShape::Cone;
		d.ConeAngleDegrees = 20.0f;
		d.Radius = 0.0f;
		d.Speed = { 1.0f, 1.0f };
		GB::ParticleSystem cone(d);
		cone.Burst(500);
		bool inside = true;
		float widest = 0.0f;
		for (const GB::Particle& p : cone.GetParticles())
		{
			const float angle = glm::degrees(std::acos(glm::clamp(glm::dot(glm::normalize(p.Velocity), glm::vec3(0, 1, 0)), -1.0f, 1.0f)));
			widest = std::max(widest, angle);
			inside = inside && angle <= 20.01f;
		}
		Check(inside && widest > 15.0f, "cone: directions stay inside the half angle and fill it");

		GB::ParticleEmitterDesc sp = Base();
		sp.Shape = GB::ParticleShape::Sphere;
		sp.Radius = 2.0f;
		sp.Speed = { 1.0f, 1.0f };
		GB::ParticleSystem sphere(sp);
		sphere.Burst(300);
		bool ok = true;
		for (const GB::Particle& p : sphere.GetParticles())
			ok = ok && glm::length(p.Position) <= 2.001f && glm::dot(glm::normalize(p.Velocity), glm::normalize(p.Position)) > 0.999f;
		Check(ok, "sphere: spawns inside the radius and moves outward");

		GB::ParticleEmitterDesc bx = Base();
		bx.Shape = GB::ParticleShape::Box;
		bx.BoxSize = { 4, 2, 6 };
		GB::ParticleSystem box(bx);
		box.Burst(300);
		bool inBox = true;
		for (const GB::Particle& p : box.GetParticles())
			inBox = inBox && std::fabs(p.Position.x) <= 2.0f && std::fabs(p.Position.y) <= 1.0f && std::fabs(p.Position.z) <= 3.0f;
		Check(inBox, "box: spawns inside the extents");
	}

	// ---- world space vs local space
	{
		GB::ParticleEmitterDesc d = Base();
		GB::ParticleSystem world(d);
		world.SetPosition({ 5, 0, 0 });
		world.Burst(1);
		d.LocalSpace = true;
		GB::ParticleSystem local(d);
		local.SetPosition({ 5, 0, 0 });
		local.Burst(1);
		std::vector<GB::ParticleInstance> iw, il;
		world.BuildInstances({ 0, 0, 10 }, iw);
		local.BuildInstances({ 0, 0, 10 }, il);
		Check(Near(iw[0].PositionSize.x, 5.0f, 1e-4f) && Near(il[0].PositionSize.x, 5.0f, 1e-4f), "both spawn at the emitter");
		world.SetPosition({ 10, 0, 0 });
		local.SetPosition({ 10, 0, 0 });
		world.BuildInstances({ 0, 0, 10 }, iw);
		local.BuildInstances({ 0, 0, 10 }, il);
		Check(Near(iw[0].PositionSize.x, 5.0f, 1e-4f), "world space: particles stay where they were emitted");
		Check(Near(il[0].PositionSize.x, 10.0f, 1e-4f), "local space: particles follow the emitter");

		GB::ParticleEmitterDesc rot = Base();
		rot.Direction = { 0, 1, 0 };
		rot.Speed = { 3.0f, 3.0f };
		GB::ParticleSystem turned(rot);
		turned.SetTransform(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)));   // +Y becomes -X
		turned.Burst(1);
		const glm::vec3 v = turned.GetParticles()[0].Velocity;
		Check(Near(v.x, -3.0f, 1e-3f) && Near(v.y, 0.0f, 1e-3f), "emission direction follows the emitter's rotation");
	}

	// ---- render data: color / size over life, flipbook, sorting
	{
		GB::ParticleEmitterDesc d = Base();
		d.Lifetime = { 1.0f, 1.0f };
		d.StartSize = { 1.0f, 1.0f };
		d.EndSizeScale = 3.0f;
		d.StartColor = { 1, 0, 0, 1 };
		d.EndColor = { 0, 0, 1, 0 };
		d.Intensity = 2.0f;
		d.FadeInFraction = 0.0f;
		d.SheetColumns = 4; d.SheetRows = 2;
		GB::ParticleSystem s(d);
		s.Burst(1);
		s.Update(0.5f);
		std::vector<GB::ParticleInstance> inst;
		s.BuildInstances({ 0, 0, 10 }, inst);
		Check(inst.size() == 1, "one instance per particle");
		Check(Near(inst[0].PositionSize.w, 2.0f, 1e-3f), "size interpolates to EndSizeScale");
		Check(Near(inst[0].Color.r, 1.0f, 1e-3f) && Near(inst[0].Color.b, 1.0f, 1e-3f) && Near(inst[0].Color.a, 0.5f, 1e-3f), "color/alpha interpolate and Intensity scales rgb only");
		Check(Near(inst[0].Frame.x, 4.0f, 1e-3f), "flipbook plays over the lifetime (frame 4 of 8 at half life)");

		d.FadeInFraction = 0.5f;
		GB::ParticleSystem fade(d);
		fade.Burst(1);
		fade.Update(0.25f);
		fade.BuildInstances({ 0, 0, 10 }, inst);
		const float expectedFade = 0.5f * 0.5f * (3.0f - 2.0f * 0.5f) * 0.75f;   // smoothstep(0,.5,.25) * (1 - 0.25)
		Check(Near(inst[0].Color.a, expectedFade, 1e-3f), "fade-in ramps the alpha up");

		GB::ParticleEmitterDesc sorted = Base();
		sorted.Shape = GB::ParticleShape::Box;
		sorted.BoxSize = { 0, 0, 20 };
		sorted.Blend = GB::ParticleBlend::Alpha;
		GB::ParticleSystem sys(sorted);
		sys.Burst(60);
		sys.BuildInstances({ 0, 0, 30 }, inst);
		bool farToNear = true;
		for (size_t i = 1; i < inst.size(); ++i)
			farToNear = farToNear && std::fabs(inst[i - 1].PositionSize.z - 30.0f) >= std::fabs(inst[i].PositionSize.z - 30.0f);
		Check(farToNear, "alpha-blended particles are sorted far to near");
	}

	// ---- presets
	{
		struct Preset { const char* name; GB::ParticleEmitterDesc desc; };
		const Preset presets[] = {
			{ "Fire", GB::ParticlePresets::Fire() }, { "Smoke", GB::ParticlePresets::Smoke() },
			{ "Sparks", GB::ParticlePresets::Sparks() }, { "Explosion", GB::ParticlePresets::Explosion() },
			{ "Snow", GB::ParticlePresets::Snow() }, { "Magic", GB::ParticlePresets::Magic() } };
		for (const Preset& p : presets)
		{
			GB::ParticleSystem s(p.desc);
			Run(s, p.desc.Looping ? 2.0f : 0.4f);   // a one-shot is only alive for a moment
			std::vector<GB::ParticleInstance> inst;
			s.BuildInstances({ 0, 2, 8 }, inst);
			bool finite = !inst.empty();
			for (const auto& i : inst)
				finite = finite && std::isfinite(i.PositionSize.x + i.PositionSize.y + i.PositionSize.z + i.PositionSize.w
					+ i.Color.r + i.Color.g + i.Color.b + i.Color.a + i.VelocityRotation.w);
			if (!finite)
				std::fprintf(stderr, "preset %s produced no or non-finite particles\n", p.name);
			Check(finite, "every preset emits finite particles");
		}
		GB::ParticleSystem boom(GB::ParticlePresets::Explosion());
		Run(boom, 3.0f);
		Check(boom.IsFinished(), "the explosion preset plays once and finishes");
	}
	return failures;
}
