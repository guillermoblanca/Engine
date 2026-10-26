// CPU-only tests for cameras: projection, blending and the orbit / fly / follow controllers.
#include "GB/Input.h"
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/CameraController.h"
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

int main()
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

	// ---- Polling with no window must be harmless
	orbit.Update(0.016f);
	fly.Update(0.016f);
	Check(GB::Input::GetScrollDelta() == 0.0f && GB::Input::GetMouseDelta().first == 0.0, "input: no window means no mouse or scroll");
	return failures;
}
