// CPU-only tests for cameras: projection, blending and the orbit / fly / follow controllers.
#include "GB/Input/InputSource.h"
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/CameraController.h"
#include "GB/Renderer/ShadowView.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
	bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
	{
		return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
	}
	constexpr float kPi = 3.14159265358979f;
}

int GBTest_camera(int, char**)
{
	// ---- Camera: orthographic projection maps the visible box to NDC [-1,1]
	GB::Camera ortho;
	ortho.Orthographic = true;
	ortho.OrthoHalfHeight = 3.0f;
	ortho.NearPlane = 1.0f;
	ortho.FarPlane = 11.0f;
	const glm::vec4 corner = ortho.GetProjection(2.0f) * glm::vec4(6.0f, 3.0f, -1.0f, 1.0f);
	Check(Near(corner.x, 1.0f) && Near(corner.y, 1.0f) && Near(corner.z, -1.0f), "ortho: right/top/near corner -> NDC (1,1,-1)");

	// ---- Camera::Blend
	GB::Camera a, b;
	a.Position = { 0, 0, 0 }; a.Target = { 0, 0, -1 }; a.FovYDegrees = 30.0f;
	b.Position = { 10, 0, 0 }; b.Target = { 10, 0, -1 }; b.FovYDegrees = 60.0f;
	const GB::Camera mid = GB::Camera::Blend(a, b, 0.5f);
	Check(Near(mid.Position, { 5, 0, 0 }) && Near(mid.GetForward(), { 0, 0, -1 }) && Near(mid.FovYDegrees, 45.0f), "blend: halfway position, direction and fov");
	Check(Near(GB::Camera::Blend(a, b, 0.0f).Position, a.Position) && Near(GB::Camera::Blend(a, b, 1.0f).Position, b.Position), "blend: endpoints");
	GB::Camera lookRight = a;
	lookRight.Target = { 1, 0, 0 };
	Check(Near(GB::Camera::Blend(a, lookRight, 0.5f).GetForward(), glm::normalize(glm::vec3(0.5f, 0, -0.5f))), "blend: view direction is interpolated");

	// ---- OrbitController
	GB::OrbitController orbit;
	orbit.Target = { 0, 0, 0 };
	orbit.Yaw = 0.0f; orbit.Pitch = 0.0f; orbit.Distance = 10.0f;
	Check(Near(orbit.GetPosition(), { 0, 0, 10 }), "orbit: yaw 0 puts the camera on +Z");
	orbit.Rotate(kPi / 2, 0.0f);
	Check(Near(orbit.GetPosition(), { 10, 0, 0 }, 1e-3f), "orbit: yaw 90 deg moves the camera to +X");
	orbit.Rotate(0.0f, 10.0f);
	Check(Near(orbit.Pitch, 89.0f * kPi / 180.0f, 1e-3f), "orbit: pitch is clamped below the pole");
	orbit.Pitch = 0.0f; orbit.Yaw = 0.0f; orbit.Distance = 10.0f;
	orbit.Zoom(std::log(2.0f));
	Check(Near(orbit.Distance, 20.0f), "orbit: zoom is multiplicative");
	orbit.MaxDistance = 25.0f; orbit.Zoom(10.0f);
	Check(Near(orbit.Distance, 25.0f), "orbit: zoom respects MaxDistance");
	orbit.Distance = 10.0f;
	orbit.Pan(0.1f, 0.0f);
	Check(Near(orbit.Target, { 1, 0, 0 }, 1e-3f), "orbit: pan moves the target along the camera's right axis");
	GB::Camera cam;
	orbit.Apply(cam);
	Check(Near(cam.Target, orbit.Target) && Near(cam.Position, orbit.GetPosition()), "orbit: Apply fills the camera");

	// ---- FlyController
	GB::FlyController fly;
	fly.Position = { 0, 0, 0 }; fly.Yaw = 0.0f; fly.Pitch = 0.0f; fly.Speed = 5.0f;
	fly.Move(1, 0, 0, 1.0f);
	Check(Near(fly.Position, { 0, 0, -5 }), "fly: forward at yaw 0 goes toward -Z");
	fly.Position = { 0, 0, 0 };
	fly.Move(1, 1, 0, 1.0f);
	Check(Near(glm::length(fly.Position), 5.0f), "fly: diagonal movement is not faster");
	fly.Position = { 0, 0, 0 };
	fly.Yaw = kPi / 2;
	fly.Move(1, 0, 0, 1.0f);
	Check(Near(fly.Position, { -5, 0, 0 }, 1e-3f), "fly: yaw 90 deg turns left, forward goes toward -X");
	fly.Position = { 0, 0, 0 }; fly.Yaw = 0.0f;
	fly.Move(0, 0, 1, 1.0f, 2.0f);
	Check(Near(fly.Position, { 0, 10, 0 }), "fly: up with speed multiplier");
	fly.Position = { 0, 0, 0 };
	fly.LookAt({ 10, 0, 0 });
	Check(Near(fly.GetForward(), { 1, 0, 0 }, 1e-3f), "fly: LookAt points the view at the target");
	fly.Look(0.0f, 10.0f);
	Check(Near(fly.Pitch, 89.0f * kPi / 180.0f, 1e-3f), "fly: pitch is clamped");

	// ---- FollowController
	GB::FollowController follow;
	follow.Offset = { 0, 2, 5 };
	follow.LookOffset = { 0, 1, 0 };
	follow.SetTarget({ 10, 0, 0 }, 0.0f);
	follow.Snap();
	follow.Apply(cam);
	Check(Near(cam.Position, { 10, 2, 5 }) && Near(cam.Target, { 10, 1, 0 }), "follow: snap puts the camera behind and above the target");

	follow.SetTarget({ 10, 0, 0 }, kPi / 2);   // target turned left: "behind" is now +X
	follow.Snap();
	follow.Apply(cam);
	Check(Near(cam.Position, { 15, 2, 0 }, 1e-3f), "follow: the offset rotates with the target's yaw");

	// Frame-rate independence: one 0.1 s step == two 0.05 s steps.
	GB::FollowController f1, f2;
	f1.SetTarget({ 0, 0, 0 }, 0.0f); f2.SetTarget({ 0, 0, 0 }, 0.0f);
	f1.Snap(); f2.Snap();
	f1.SetTarget({ 100, 0, 0 }, 0.0f); f2.SetTarget({ 100, 0, 0 }, 0.0f);
	f1.Update(0.1f);
	f2.Update(0.05f); f2.Update(0.05f);
	GB::Camera c1, c2;
	f1.Apply(c1); f2.Apply(c2);
	Check(Near(c1.Position, c2.Position, 1e-3f), "follow: smoothing does not depend on the frame rate");
	Check(c1.Position.x > 10.0f && c1.Position.x < 100.0f, "follow: lags behind a fast target");
	for (int i = 0; i < 200; ++i)
		f1.Update(0.05f);
	f1.Apply(c1);
	Check(Near(c1.Position.x, 100.0f, 1e-2f), "follow: converges to the target");

	// ---- Orbit camera, walking scheme (the editor)
	{
		GB::OrbitController o;
		o.Keys = GB::OrbitController::KeyScheme::Walk;
		o.Target = { 0, 0, 0 }; o.Yaw = 0.0f; o.Pitch = 0.3f; o.Distance = 10.0f;
		o.Walk(0.0f, 1.0f, 1.0f);
		Check(Near(o.Target, { 0, 0, -7 }, 1e-3f), "walk: forward goes where the camera looks, over the ground (yaw 0 = toward -Z), at 0.7 of the distance per second");
		o.Target = { 0, 0, 0 };
		o.Walk(1.0f, 0.0f, 1.0f);
		Check(Near(o.Target, { 7, 0, 0 }, 1e-3f), "walk: right is the camera's right hand");
		o.Target = { 0, 0, 0 }; o.Yaw = kPi / 2;
		o.Walk(0.0f, 1.0f, 1.0f);
		Check(Near(o.Target, { -7, 0, 0 }, 1e-3f), "walk: it follows the yaw (yaw 90 deg looks toward -X)");
		o.Target = { 0, 0, 0 }; o.Yaw = 0.0f; o.Distance = 1.0f;
		o.Walk(0.0f, 1.0f, 1.0f);
		Check(Near(o.Target, { 0, 0, -4 }, 1e-3f), "walk: close to the target it still moves at least MinWalk metres per second");
		o.Target = { 0, 0, 0 }; o.Distance = 10.0f;
		o.Walk(1.0f, 1.0f, 1.0f);
		Check(std::abs(glm::length(o.Target) - 7.0f) < 1e-3f, "walk: diagonals are not faster");
		o.Target = { 0, 0, 0 };
		o.Walk(0.0f, 1.0f, 1.0f, 3.0f);
		Check(Near(o.Target, { 0, 0, -21 }, 1e-3f), "walk: the speed multiplier (Shift) scales the step");

		o.Target = { 0, 0, 0 }; o.Distance = 10.0f; o.Yaw = 0.0f;
		o.PanGround(0.1f, 0.1f);
		Check(Near(o.Target, { 1, 0, -1 }, 1e-3f), "pan on the ground: right and forward, scaled by the distance, never up");

		// Zoom towards a point keeps it where it is on screen: the target slides to it as the distance shrinks.
		o.Target = { 0, 0, 0 }; o.Distance = 10.0f;
		o.ZoomToward(std::log(0.5f), { 10, 0, 0 });
		Check(Near(o.Distance, 5.0f, 1e-3f) && Near(o.Target, { 5, 0, 0 }, 1e-3f), "zoom toward: halving the distance halves the target's distance to the point");
		o.MinDistance = 1.0f;
		for (int i = 0; i < 40; ++i) o.ZoomToward(-0.3f, { 10, 0, 0 });
		Check(Near(o.Distance, 1.0f, 1e-3f) && Near(glm::distance(o.Target, glm::vec3(10, 0, 0)), 1.0f, 1e-2f), "zoom toward: the point's offset from the target shrinks with the distance (it stays put on screen) so a far point can be reached");

		// The ground: the target stays above it and the camera does not go under it.
		GB::OrbitController g;
		g.Ground = [](float, float z) { return z < -5.0f ? 30.0f : 2.0f; };
		g.TargetHeight = 1.0f;
		g.Target = { 0, 0, 0 }; g.Yaw = 0.0f; g.Pitch = 0.0f; g.Distance = 10.0f;
		g.KeepAboveGround();
		Check(Near(g.Target.y, 3.0f, 1e-4f), "ground: the target is TargetHeight above the terrain");
		g.Target = { 0, 0, -20 };
		g.KeepAboveGround();
		Check(Near(g.Target.y, 31.0f, 1e-4f), "ground: walking onto a hill lifts the target with it");
		g.Target = { 0, 0, 0 }; g.Pitch = -0.5f;
		g.KeepAboveGround();
		Check(g.GetPosition().y >= 2.0f + g.CameraClearance - 1e-3f, "ground: the camera is not left under the terrain (the pitch rises)");

		// Following: eases to the goal, the same however the time is cut up.
		GB::OrbitController f1, f2;
		f1.Target = f2.Target = { 0, 0, 0 };
		f1.FollowTo({ 100, 0, 0 }, 0.1f);
		f2.FollowTo({ 100, 0, 0 }, 0.05f); f2.FollowTo({ 100, 0, 0 }, 0.05f);
		Check(Near(f1.Target, f2.Target, 1e-2f), "orbit follow: easing does not depend on the frame rate");
		Check(f1.Target.x > 10.0f && f1.Target.x < 100.0f, "orbit follow: lags behind a fast goal");
		for (int i = 0; i < 100; ++i) f1.FollowTo({ 100, 0, 0 }, 0.05f);
		Check(Near(f1.Target, { 100, 0, 0 }, 1e-2f), "orbit follow: converges to the goal");
		f1.Target = { 100, 0, 0 };
		f1.Keys = GB::OrbitController::KeyScheme::Walk;
		f1.Update(0.016f, GB::NoInput());
		Check(Near(f1.Target, { 100, 0, 0 }, 1e-4f), "orbit follow: with no input the target stays");

		// Top-down: straight down, the mouse cannot tilt it, and the old pitch comes back.
		GB::OrbitController t;
		t.Pitch = 0.3f;
		t.SetTopDown(true);
		Check(Near(t.Pitch, 89.0f * kPi / 180.0f, 1e-3f), "top-down: looks straight down (89 deg)");
		t.Rotate(0.5f, -0.5f);
		Check(Near(t.Pitch, 89.0f * kPi / 180.0f, 1e-3f) && Near(t.Yaw, 0.5f, 1e-4f), "top-down: yaw turns, pitch is locked");
		t.SetTopDown(false);
		Check(Near(t.Pitch, 0.3f, 1e-4f), "top-down: leaving restores the pitch");
	}

	// ---- Polling with no window must be harmless
	const GB::NoInput none;
	orbit.Update(0.016f, none);
	fly.Update(0.016f, none);
	Check(none.GetScrollDelta() == 0.0f && none.GetMouseDelta().first == 0.0, "input: no window means no mouse or scroll");

	// ---- The controllers read whatever IInputSource they are given (there is no global input)
	{
		GB::FlyController flier;
		const glm::vec3 start = flier.Position;
		GB::ScriptedInput keys;
		flier.Update(0.5f, keys);
		Check(Near(flier.Position, start, 1e-6f), "scripted input: nothing pressed, the fly camera stays");
		keys.SetKey(GB::Key::W, true);
		flier.Update(0.5f, keys);
		Check(!Near(flier.Position, start, 1e-3f), "scripted input: a held W moves the fly camera");

		GB::FlyController other;   // a second controller on its own source is unaffected by the first one's keys
		GB::ScriptedInput idle;
		other.Update(0.5f, idle);
		Check(Near(other.Position, start, 1e-6f), "scripted input: two sources are independent");
	}

	// ---- Shadow map snapped to texels: a fixed world point keeps its spot inside its texel while the centre moves by fractions of a texel
	{
		const unsigned size = 2048;
		const float radius = 30.0f, texel = 2.0f * radius / static_cast<float>(size);
		const glm::vec3 sun = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.3f));
		const glm::vec3 point(3.21f, 1.5f, -7.65f);
		auto spot = [&](const glm::vec3& center)
		{
			const glm::vec4 c = GB::ShadowViewProj(center, sun, radius, size) * glm::vec4(point, 1.0f);
			const glm::vec2 t = glm::vec2(c) * (0.5f * static_cast<float>(size));
			return t - glm::floor(t);   // the point's position inside its texel
		};
		const glm::vec2 first = spot({ 0.0f, 0.0f, 0.0f });
		bool steady = true, insideBox = true;
		for (int i = 1; i <= 12; ++i)
		{
			const glm::vec3 center = glm::vec3(0.37f, 0.11f, -0.23f) * (texel * 0.29f * static_cast<float>(i));   // 0.29 texel steps
			const glm::vec2 s = spot(center), d = glm::abs(s - first);
			steady = steady && glm::all(glm::lessThan(glm::min(d, 1.0f - d), glm::vec2(2e-3f)));
			const glm::vec4 c = GB::ShadowViewProj(center, sun, radius, size) * glm::vec4(center, 1.0f);
			insideBox = insideBox && std::fabs(c.x) < 2.0f / static_cast<float>(size) + 1e-4f && std::fabs(c.y) < 2.0f / static_cast<float>(size) + 1e-4f;
		}
		Check(steady, "shadow snap: a world point stays on the same spot of its texel as the centre moves by fractions of a texel");
		Check(insideBox, "shadow snap: the centre stays within a texel of the middle of the map");
	}
	return failures;
}
