// CPU-only tests for the third-person character: acceleration, turning, camera-relative input, obstacles,
// water, slopes, frame-rate independence and the follow camera's ground collision.
#include "GB/Game/CharacterController.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	constexpr float kPi = 3.14159265358979f;

	GB::CharacterInput Forward(bool run = false, float cameraYaw = 0.0f)
	{
		GB::CharacterInput in;
		in.Move = { 0.0f, 1.0f };
		in.Run = run;
		in.CameraYaw = cameraYaw;
		return in;
	}

	void Run(GB::CharacterController& c, const GB::CharacterWorld& w, const GB::CharacterInput& in, float seconds, float dt)
	{
		for (float t = 0.0f; t < seconds - 1e-6f; t += dt)
			c.Step(in, w, dt);
	}
}

int main()
{
	GB::CharacterWorld flat;   // y = 0 everywhere

	// ---- Camera yaw 0: the camera sits on +Z looking toward -Z, so "forward" is -Z; D is +X.
	{
		GB::CharacterController c;
		c.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		Run(c, flat, Forward(), 1.0f, 1.0f / 60.0f);
		Check(c.GetPosition().z < -0.3f && std::fabs(c.GetPosition().x) < 1e-3f, "forward goes away from the camera");
		GB::CharacterInput right;
		right.Move = { 1.0f, 0.0f };
		GB::CharacterController d;
		d.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		Run(d, flat, right, 1.0f, 1.0f / 60.0f);
		Check(d.GetPosition().x > 0.3f && std::fabs(d.GetPosition().z) < 1e-3f, "D goes to the camera's right");
		// With the camera on +X (yaw 90), forward is -X.
		GB::CharacterController e;
		e.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		Run(e, flat, Forward(false, kPi * 0.5f), 1.0f, 1.0f / 60.0f);
		Check(e.GetPosition().x < -0.3f, "movement follows the camera yaw");
	}

	// ---- Speed ramps up smoothly, stays under the caps, and decays after release.
	{
		GB::CharacterController c;
		c.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		float previous = 0.0f, biggestJump = 0.0f, top = 0.0f;
		for (int i = 0; i < 180; ++i)
		{
			c.Step(Forward(true), flat, 1.0f / 60.0f);
			biggestJump = std::fmax(biggestJump, c.GetSpeed() - previous);
			previous = c.GetSpeed();
			top = std::fmax(top, previous);
		}
		Check(biggestJump < 0.25f, "acceleration is gradual (no speed pop)");
		Check(top <= c.Config.RunSpeed + 1e-3f, "never faster than the run speed");
		Check(top > c.Config.RunSpeed * 0.95f, "reaches the run speed");
		GB::CharacterInput none;
		Run(c, flat, none, 0.15f, 1.0f / 60.0f);
		Check(c.GetSpeed() > 0.0f && c.GetSpeed() < c.Config.RunSpeed * 0.5f, "slows down gradually after release");
		Run(c, flat, none, 1.5f, 1.0f / 60.0f);
		Check(c.GetSpeed() == 0.0f, "comes to a full stop");

		GB::CharacterController w;
		w.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		Run(w, flat, Forward(false), 3.0f, 1.0f / 60.0f);
		Check(w.GetSpeed() <= w.Config.WalkSpeed + 1e-3f && w.GetSpeed() > w.Config.WalkSpeed * 0.95f, "walk speed is the cap without Shift");
	}

	// ---- Diagonals are not faster; a half-tilted stick is slower.
	{
		GB::CharacterController straight, diagonal, half;
		for (GB::CharacterController* c : { &straight, &diagonal, &half })
			c->SetPosition(flat, 0.0f, 0.0f, 0.0f);
		GB::CharacterInput d = Forward(true);
		d.Move = { 1.0f, 1.0f };
		GB::CharacterInput h = Forward(true);
		h.Move = { 0.0f, 0.5f };
		Run(straight, flat, Forward(true), 2.0f, 1.0f / 60.0f);
		Run(diagonal, flat, d, 2.0f, 1.0f / 60.0f);
		Run(half, flat, h, 2.0f, 1.0f / 60.0f);
		Check(std::fabs(diagonal.GetSpeed() - straight.GetSpeed()) < 1e-3f, "diagonal speed equals straight speed");
		Check(half.GetSpeed() < straight.GetSpeed() * 0.6f, "a half-tilted stick is slower");
	}

	// ---- Heading turns smoothly toward the direction of travel, also across the +-pi seam.
	{
		GB::CharacterController c;
		c.SetPosition(flat, 0.0f, 0.0f, kPi - 0.2f);   // facing nearly +Z... pi is -Z? yaw = atan2(x, z): pi = -Z
		GB::CharacterInput in;
		in.Move = { 0.0f, -1.0f };                     // camera yaw 0: backward = +Z, i.e. yaw 0
		float previous = c.GetYaw(), biggest = 0.0f;
		for (int i = 0; i < 90; ++i)
		{
			c.Step(in, flat, 1.0f / 60.0f);
			float d = c.GetYaw() - previous;
			if (d > kPi) d -= 2.0f * kPi;
			if (d < -kPi) d += 2.0f * kPi;
			biggest = std::fmax(biggest, std::fabs(d));
			previous = c.GetYaw();
		}
		Check(biggest < c.Config.MaxTurnRate / 60.0f + 1e-4f, "turning respects the rate cap (no snap at the seam)");
		Check(std::fabs(c.GetYaw()) < 0.05f, "ends up facing the direction of travel");
		// Shortest way: from -170 degrees to +170 degrees is a 20 degree turn, not 340.
		GB::CharacterController s;
		s.SetPosition(flat, 0.0f, 0.0f, -kPi + 0.17f);
		GB::CharacterInput back;
		back.Move = { 0.0f, 1.0f };
		back.CameraYaw = 0.0f;   // forward = -Z = yaw pi
		s.Step(back, flat, 1.0f / 60.0f);
		Check(s.GetYaw() < -kPi + 0.17f + 0.001f || s.GetYaw() > 0.0f, "turns the short way around");
	}

	// ---- Frame-rate independence.
	{
		GB::CharacterController a, b;
		a.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		b.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		GB::CharacterInput in = Forward(true);
		in.Move = { 0.4f, 1.0f };
		Run(a, flat, in, 3.0f, 1.0f / 30.0f);
		Run(b, flat, in, 3.0f, 1.0f / 120.0f);
		Check(glm::distance(a.GetPosition(), b.GetPosition()) < 0.25f, "same trajectory at 30 and 120 fps");
		Check(std::fabs(a.GetYaw() - b.GetYaw()) < 0.05f, "same heading at 30 and 120 fps");
	}

	// ---- Obstacles: never inside, and the character slides along them.
	{
		GB::CharacterWorld w;
		w.Obstacles.push_back({ { 0.0f, -4.0f }, 1.0f });
		GB::CharacterController c;
		c.SetPosition(w, 0.3f, 0.0f, 0.0f);
		float minDistance = 1e9f;
		for (int i = 0; i < 600; ++i)
		{
			c.Step(Forward(true), w, 1.0f / 60.0f);
			minDistance = std::fmin(minDistance, glm::distance(glm::vec2(c.GetPosition().x, c.GetPosition().z), w.Obstacles[0].Center));
		}
		Check(minDistance >= 1.0f + c.Config.Radius - 1e-3f, "never enters a tree");
		Check(c.GetPosition().z < -4.0f, "slides around the obstacle and keeps going");
	}

	// ---- Water: shallow is walkable (and slower), deep is a wall.
	{
		GB::CharacterWorld w;
		w.GroundHeight = [](float, float z) { return z < -5.0f ? -3.0f : (z < -2.0f ? -0.4f : 0.0f); };   // shallow 0.4 m, then deep 3 m
		w.WaterLevel = [](float, float z) { return z < -2.0f ? 0.0f : -1e9f; };
		GB::CharacterController c;
		c.SetPosition(w, 0.0f, 0.0f, 0.0f);
		float minZ = 0.0f;
		bool waded = false;
		for (int i = 0; i < 900; ++i)
		{
			c.Step(Forward(true), w, 1.0f / 60.0f);
			minZ = std::fmin(minZ, c.GetPosition().z);
			waded = waded || c.GetWadeDepth() > 0.2f;
		}
		Check(waded, "walks into the shallows");
		Check(minZ > -5.1f, "stops at the deep water");
		Check(c.GetPosition().y > -0.5f, "stays on the shallow bed");
		GB::CharacterController slow, fast;
		slow.SetPosition(w, 0.0f, -3.0f, 0.0f);
		fast.SetPosition(w, 0.0f, 3.0f, 0.0f);
		Run(slow, w, Forward(true), 0.4f, 1.0f / 60.0f);
		Run(fast, w, Forward(true), 0.4f, 1.0f / 60.0f);
		Check(slow.GetSpeed() < fast.GetSpeed(), "wading is slower than dry land");
	}

	// ---- Terrain: follows a slope, refuses a cliff.
	{
		GB::CharacterWorld w;
		w.GroundHeight = [](float, float z) { return z < -6.0f ? 20.0f : -0.3f * z; };   // gentle ramp up toward -Z (rise over run 0.3), then a wall
		GB::CharacterController c;
		c.SetPosition(w, 0.0f, 0.0f, 0.0f);
		float worst = 0.0f;
		for (int i = 0; i < 400; ++i)
		{
			c.Step(Forward(true), w, 1.0f / 60.0f);
			const float g = w.GroundHeight(c.GetPosition().x, c.GetPosition().z);
			worst = std::fmax(worst, std::fabs(c.GetPosition().y - g));
			Check(c.GetPosition().y >= g - 1e-4f, "never sinks into the ground");
		}
		Check(worst < 0.1f, "sticks to the terrain");
		Check(c.GetPosition().z > -6.0f, "does not climb the cliff");
	}

	// ---- Map bounds.
	{
		GB::CharacterWorld w;
		w.BoundsMin = { -5.0f, -5.0f };
		w.BoundsMax = { 5.0f, 5.0f };
		GB::CharacterController c;
		c.SetPosition(w, 0.0f, 0.0f, 0.0f);
		Run(c, w, Forward(true), 6.0f, 1.0f / 60.0f);
		Check(c.GetPosition().z >= -5.0f - 1e-4f, "stays inside the map");
	}

	// ---- Locomotion state has hysteresis.
	{
		using GB::Locomotion;
		Check(GB::NextLocomotion(Locomotion::Idle, 0.2f) == Locomotion::Idle, "idle below the walk threshold");
		Check(GB::NextLocomotion(Locomotion::Idle, 1.0f) == Locomotion::Walk, "walk");
		Check(GB::NextLocomotion(Locomotion::Walk, 0.2f) == Locomotion::Walk, "walk keeps going between the thresholds");
		Check(GB::NextLocomotion(Locomotion::Walk, 0.05f) == Locomotion::Idle, "back to idle");
		Check(GB::NextLocomotion(Locomotion::Walk, 3.5f) == Locomotion::Run, "run");
		Check(GB::NextLocomotion(Locomotion::Run, 2.8f) == Locomotion::Run, "run keeps going between the thresholds");
		Check(GB::NextLocomotion(Locomotion::Run, 2.0f) == Locomotion::Walk, "run drops to walk");
	}

	// ---- Scripted input and spawn search.
	{
		const auto script = GB::ParseCharacterScript("WD:1.5,RW:2,:0.5,RWQ:bad");
		Check(script.size() == 3, "three valid script steps");
		Check(script[0].Move.x == 1.0f && script[0].Move.y == 1.0f && !script[0].Run, "WD moves diagonally");
		Check(script[1].Run && script[1].Move.y == 1.0f, "RW runs forward");
		Check(GB::SampleCharacterScript(script, 1.0f) == &script[0], "t = 1 is in the first step");
		Check(GB::SampleCharacterScript(script, 2.0f) == &script[1], "t = 2 is in the second step");
		Check(GB::SampleCharacterScript(script, 3.9f) == &script[2], "t = 3.9 is in the idle step");
		Check(GB::SampleCharacterScript(script, 4.1f) == nullptr, "nothing after the end");

		GB::CharacterWorld w;
		w.WaterLevel = [](float x, float) { return x > 0.0f ? 0.0f : -1e9f; };
		w.GroundHeight = [](float x, float) { return x > 0.0f ? -2.0f : 0.0f; };
		w.Obstacles.push_back({ { -3.0f, 0.0f }, 1.0f });
		const glm::vec2 spot = GB::FindFreeSpot(w, { 1.0f, 0.0f }, 2.0f);
		Check(spot.x <= 0.0f && glm::distance(spot, glm::vec2(-3.0f, 0.0f)) >= 3.0f, "the spawn search leaves water and trees behind");
	}

	// ---- Third-person camera.
	{
		GB::ThirdPersonCamera cam;
		const auto hill = [](float x, float z) { return 6.0f * std::exp(-((x - 3.0f) * (x - 3.0f) + (z - 3.0f) * (z - 3.0f)) / 8.0f); };
		cam.GroundHeight = hill;
		cam.Yaw = 0.0f;
		cam.Pitch = 0.15f;
		cam.Distance = 8.0f;
		const glm::vec3 character(3.0f, hill(3.0f, 3.0f), -4.0f);
		cam.Snap(character);
		// Behind the hill the camera is pulled in and kept above the surface at every step.
		float lowestClearance = 1e9f;
		for (int i = 0; i < 120; ++i)
		{
			cam.Update(1.0f / 60.0f, character);
			const glm::vec3 p = cam.GetPosition();
			lowestClearance = std::fmin(lowestClearance, p.y - hill(p.x, p.z));
		}
		Check(lowestClearance >= cam.GroundMargin - 1e-3f, "the camera stays above the terrain");
		// Open ground: back at the full distance.
		cam.GroundHeight = [](float, float) { return 0.0f; };
		const glm::vec3 open(0.0f, 0.0f, 0.0f);
		cam.Snap(open);
		for (int i = 0; i < 600; ++i)
			cam.Update(1.0f / 60.0f, open);
		Check(std::fabs(cam.GetEffectiveDistance() - 8.0f) < 0.05f, "full distance on open ground");

		// Following is smooth: a character that jumps 10 m moves the camera target gradually.
		cam.Update(1.0f / 60.0f, { 10.0f, 0.0f, 0.0f });
		Check(cam.GetTarget().x > 0.0f && cam.GetTarget().x < 3.0f, "the target eases toward the character");

		// Pitch is clamped; yaw wraps; zoom stays in range.
		cam.Rotate(0.0f, 10.0f);
		Check(cam.Pitch <= cam.MaxPitch + 1e-6f, "pitch is clamped");
		cam.Rotate(7.0f, 0.0f);
		Check(std::fabs(cam.Yaw) <= kPi + 1e-5f, "yaw wraps");
		cam.Zoom(100.0f);
		Check(cam.Distance <= cam.MaxDistance + 1e-4f, "zoom is clamped");
	}

	if (failures == 0)
		std::printf("character tests passed\n");
	// ---- Turning on the spot (opt-in): asked to go the opposite way from a standstill, the character turns first.
	{
		GB::CharacterController plain;
		plain.SetPosition(flat, 0.0f, 0.0f, 0.0f);   // faces +Z; "forward" with camera yaw 0 is -Z: straight behind it
		Run(plain, flat, Forward(), 0.4f, 1.0f / 60.0f);
		Check(plain.GetSpeed() > 1.0f, "by default the character sets off at once while it turns");

		GB::CharacterController turner;
		turner.Config.TurnInPlaceAngle = 70.0f;
		turner.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		Run(turner, flat, Forward(), 0.4f, 1.0f / 60.0f);
		Check(turner.GetSpeed() < 0.05f, "with turn-in-place the character stays where it is while it turns");
		Check(std::fabs(turner.GetYaw()) > 0.8f && std::fabs(turner.GetYaw()) < 2.5f, "and its heading swings round at the turn rate");
		Run(turner, flat, Forward(), 1.6f, 1.0f / 60.0f);
		Check(turner.GetSpeed() > 1.0f && turner.GetPosition().z < -0.3f, "then it walks off the right way");

		GB::CharacterController small;   // a small change of direction does not stop the character
		small.Config.TurnInPlaceAngle = 70.0f;
		small.SetPosition(flat, 0.0f, 0.0f, 3.14159265f);   // already facing -Z
		Run(small, flat, Forward(), 0.4f, 1.0f / 60.0f);
		Check(small.GetSpeed() > 1.0f, "no turn first when the character already faces the way it goes");
	}

	return failures == 0 ? 0 : 1;
}
