// The layer between the character controller (tests/character_test.cpp) and the locomotion animation: the motion state
// derived from a controller, the AI driver that feeds the same controller as a player would, and the animator that reads
// only the motion state (blend + footsteps). CPU only.
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Assets/Model.h"
#include "GB/Game/AIDriver.h"
#include "GB/Game/MotionState.h"
#include "GB/Log.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) < eps; }
}

int main()
{
	using namespace GB;
	Log::Init();
	const CharacterWorld world;   // flat ground at 0, no water, no obstacles

	// ---- jump and fall
	{
		CharacterController c;
		c.SetPosition(world, 0, 0, 0.0f);
		MotionTracker tracker;
		const float dt = 1.0f / 60.0f;
		CharacterInput idle, jump;
		jump.Jump = true;
		float apex = 0.0f;
		int landings = 0, airFrames = 0;
		float landingSpeed = 0.0f;
		MotionState s;
		for (int i = 0; i < 120; ++i)
		{
			c.Step(i < 90 ? jump : idle, world, dt);   // the button is held for a long time: one jump only
			s = tracker.Update(c, dt);
			apex = std::max(apex, s.Position.y);
			airFrames += s.Grounded ? 0 : 1;
			if (s.Landed) { ++landings; landingSpeed = s.LandingSpeed; }
		}
		Check(Near(apex, 4.2f * 4.2f / 24.0f, 0.05f), "a jump rises v^2 / 2g");
		Check(landings == 1 && s.Grounded && Near(s.Position.y, 0.0f, 1e-4f), "one landing, back on the ground");
		Check(Near(landingSpeed, 4.2f, 0.4f), "it lands at about the take-off speed");
		Check(airFrames > 35 && airFrames < 50, "about 0.7 s in the air");

		// a jump is not available in mid-air; the horizontal speed barely changes with the air control
		c.SetPosition(world, 0, 0, 0.0f);
		CharacterInput run; run.Move = { 0, 1 }; run.Run = true; run.CameraYaw = 3.14159265f;
		for (int i = 0; i < 90; ++i) c.Step(run, world, dt);
		const float groundSpeed = c.GetSpeed();
		CharacterInput runJump = run; runJump.Jump = true;
		c.Step(runJump, world, dt);
		Check(c.IsAirborne(), "jumping while running");
		runJump.Jump = false; c.Step(runJump, world, dt); runJump.Jump = true;
		const float vy = c.GetVerticalSpeed();
		c.Step(runJump, world, dt);
		Check(c.GetVerticalSpeed() < vy && c.GetSpeed() > groundSpeed - 0.5f, "no second jump in the air; speed is kept");
	}
	{
		// walking off a ledge: the character falls instead of easing down
		CharacterWorld ledge;
		ledge.GroundHeight = [](float, float z) { return z < 1.0f ? 2.0f : 0.0f; };
		CharacterController c;
		c.SetPosition(ledge, 0, 0, 0.0f);
		MotionTracker tracker;
		CharacterInput walk; walk.Move = { 0, 1 }; walk.CameraYaw = 3.14159265f;
		bool fell = false, landed = false;
		for (int i = 0; i < 240; ++i)
		{
			c.Step(walk, ledge, 1.0f / 60.0f);
			const MotionState s = tracker.Update(c, 1.0f / 60.0f);
			fell = fell || !s.Grounded;
			landed = landed || (s.Landed && s.LandingSpeed > 3.0f);
		}
		Check(fell && landed && !c.IsAirborne() && Near(c.GetPosition().y, 0.0f, 1e-3f), "a 2 m drop is a fall with a hard landing");
	}

	// ---- the motion state of a controller
	{
		CharacterController c;
		c.SetPosition(world, 0, 0, 0.0f);
		MotionTracker tracker;
		MotionState s;
		CharacterInput walkForward;
		walkForward.Move = { 0, 1 };
		walkForward.CameraYaw = 3.14159265f;   // the camera looks along +z (it sits at -z)
		for (int i = 0; i < 90; ++i) { c.Step(walkForward, world, 1.0f / 60.0f); s = tracker.Update(c, 1.0f / 60.0f); }
		Check(Near(s.Speed, 1.7f, 0.02f) && Near(s.LocalVelocity.y, 1.7f, 0.02f) && Near(s.LocalVelocity.x, 0.0f, 0.02f), "walking forward: all the speed is forward");
		Check(Near(s.Yaw, 0.0f, 0.02f) && s.Position.z > 1.0f && s.Grounded, "facing +z, grounded");
		Check(Near(s.LocalAcceleration.y, 0.0f, 0.3f), "no acceleration at constant speed");

		// turning: the character swings toward +x (its left when it faces +z): a positive turn rate
		CharacterInput goLeft;
		goLeft.Move = { 0, 1 };
		goLeft.CameraYaw = 3.14159265f + 1.5708f;   // the camera looks along +x
		c.Step(goLeft, world, 1.0f / 60.0f);
		s = tracker.Update(c, 1.0f / 60.0f);
		Check(s.YawRate > 10.0f, "turning left is a positive turn rate");
		for (int i = 0; i < 90; ++i) { c.Step(goLeft, world, 1.0f / 60.0f); s = tracker.Update(c, 1.0f / 60.0f); }
		Check(Near(s.Yaw, 1.5708f, 0.03f) && Near(s.LocalVelocity.y, s.Speed, 0.05f), "ends up heading +x, all speed forward in its own axes");

		CharacterInput start;
		CharacterController rest;
		rest.SetPosition(world, 0, 0, 0.0f);
		MotionTracker t2;
		start.Move = { 0, 1 }; start.CameraYaw = 3.14159265f;
		rest.Step(start, world, 1.0f / 60.0f);
		s = t2.Update(rest, 1.0f / 60.0f);
		s = t2.Update(rest, 1.0f / 60.0f);
		rest.Step(start, world, 1.0f / 60.0f);
		s = t2.Update(rest, 1.0f / 60.0f);
		Check(s.LocalAcceleration.y > 5.0f, "starting to walk accelerates forward");
	}

	// ---- the AI feeds the controller like a player would
	{
		// every direction and speed goes where it should
		const glm::vec2 dirs[] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 }, glm::normalize(glm::vec2(1, 1)), glm::normalize(glm::vec2(-2, 1)) };
		for (const glm::vec2& d : dirs)
			for (float speed : { 0.8f, 1.7f, 3.0f, 4.6f })
			{
				CharacterController c;
				c.SetPosition(world, 0, 0, 0.0f);
				const CharacterInput in = AIDriver::InputFor(d, speed, c.Config);
				for (int i = 0; i < 240; ++i) c.Step(in, world, 1.0f / 60.0f);
				const glm::vec2 v(c.GetVelocity().x, c.GetVelocity().z);
				if (!(Near(glm::length(v), speed, 0.03f) && glm::dot(glm::normalize(v), d) > 0.999f))
				{
					std::fprintf(stderr, "FAIL: direction (%.2f, %.2f) at %.1f m/s: got (%.2f, %.2f)\n", static_cast<double>(d.x), static_cast<double>(d.y), static_cast<double>(speed), static_cast<double>(v.x), static_cast<double>(v.y));
					++failures;
				}
			}

		AIDriver ai;
		CharacterController c;
		c.SetPosition(world, 0, 0, 0.0f);
		ai.MoveTo({ 6, 0 });
		float maxSpeed = 0.0f;
		for (int i = 0; i < 900 && !ai.Arrived(); ++i) { c.Step(ai.Update(c, 1.0f / 60.0f), world, 1.0f / 60.0f); maxSpeed = std::max(maxSpeed, c.GetSpeed()); }
		Check(ai.Arrived() && glm::distance(glm::vec2(c.GetPosition().x, c.GetPosition().z), glm::vec2(6, 0)) < 0.5f, "the AI walks to its target");
		Check(maxSpeed < c.Config.WalkSpeed + 0.05f, "walking, it never runs");
		for (int i = 0; i < 90; ++i) c.Step(ai.Update(c, 1.0f / 60.0f), world, 1.0f / 60.0f);
		Check(c.GetSpeed() < 0.05f, "and ends standing still");

		AIDriver runner;
		runner.Run = true;
		CharacterController c2;
		c2.SetPosition(world, 0, 0, 0.0f);
		runner.SetPath({ { 8, 0 }, { 8, 8 }, { 0, 8 } });
		float top = 0.0f;
		for (int i = 0; i < 1200 && !runner.Arrived(); ++i) { c2.Step(runner.Update(c2, 1.0f / 60.0f), world, 1.0f / 60.0f); top = std::max(top, c2.GetSpeed()); }
		Check(runner.Arrived() && glm::distance(glm::vec2(c2.GetPosition().x, c2.GetPosition().z), glm::vec2(0, 8)) < 0.6f, "the AI follows a path through its waypoints");
		Check(top > c2.Config.WalkSpeed + 1.0f, "running, it goes faster than a walk");

		CharacterWorld walled;
		for (float z = -6.0f; z <= 6.0f; z += 0.4f) walled.Obstacles.push_back({ { 3.0f, z }, 0.5f });   // a wall of trunks
		AIDriver stuck;
		CharacterController c3;
		c3.SetPosition(walled, 0, 0, 0.0f);
		stuck.MoveTo({ 8, 0 });
		for (int i = 0; i < 900 && !stuck.Stuck() && !stuck.Arrived(); ++i) c3.Step(stuck.Update(c3, 1.0f / 60.0f), walled, 1.0f / 60.0f);
		Check(stuck.Stuck() && !stuck.Arrived(), "the AI gives up when a wall of trees blocks it");
	}

	// ---- the animator reads only the motion state, whoever produced the input
	{
		const std::string dir = GB_TEST_DATA_DIR;
		auto model = Model::Load(dir + "/test_rig.fbx");
		if (!model) { std::fprintf(stderr, "FAIL: test rig\n"); return 1; }
		model->AppendAnimations(dir + "/test_clip.fbx");
		model->AppendAnimations(dir + "/test_idle.fbx");

		// an AI-led character and one replaying the same inputs ("a player", "the network", a recording)
		AIDriver ai;
		ai.MoveTo({ 5, 0 });
		CharacterController a, b;
		a.SetPosition(world, 0, 0, 0.0f); b.SetPosition(world, 0, 0, 0.0f);
		MotionTracker ta, tb;
		ModelInstance ia(*model), ib(*model);
		CharacterAnimator anA(*model, ia), anB(*model, ib);
		for (const char* n : { "test_idle", "test_rig", "test_clip" }) { anA.Locomotion().AddClip(n); anB.Locomotion().AddClip(n); }
		AudioConfig cfg; cfg.Device = false;
		AudioEngine audio(cfg);
		FootstepSounds stepsA(audio), stepsB(audio);
		anA.SetFootsteps(&stepsA); anB.SetFootsteps(&stepsB);
		for (int i = 0; i < 600; ++i)
		{
			const CharacterInput in = ai.Update(a, 1.0f / 60.0f);
			a.Step(in, world, 1.0f / 60.0f);
			b.Step(in, world, 1.0f / 60.0f);
			const MotionState sa = ta.Update(a, 1.0f / 60.0f), sb = tb.Update(b, 1.0f / 60.0f);
			anA.Update(sa, 1.0f / 60.0f, glm::translate(glm::mat4(1.0f), sa.Position));
			anB.Update(sb, 1.0f / 60.0f, glm::translate(glm::mat4(1.0f), sb.Position));
		}
		Check(ai.Arrived(), "the AI-led character arrived");
		bool same = ia.GetPose().size() == ib.GetPose().size();
		for (size_t i = 0; same && i < ia.GetPose().size(); ++i)
			same = glm::all(glm::equal(ia.GetPose()[i].T, ib.GetPose()[i].T)) && ia.GetPose()[i].R == ib.GetPose()[i].R;
		Check(same, "the same motion gives the same pose, whoever moved the character");
		Check(stepsA.Count() >= 3 && stepsA.Count() == stepsB.Count(), "walking plays footsteps, and the same ones");

		// the blend follows the speed: standing = idle, walking = the walk clip
		anA.Update(MotionState(), 1.0f / 60.0f, glm::mat4(1.0f));
		Check(anA.Locomotion().GaitWeight() < 0.5f, "a character at rest is not walking");
	}

	if (failures) return 1;
	std::puts("character layer tests passed");
	return 0;
}
