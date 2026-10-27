// The character motor, its controllers (player, AI, script), the follow camera and the animator that reads only the
// motion state. Pure CPU: terrain is a function, obstacles are static colliders.
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Assets/Model.h"
#include "GB/Game/CharacterControllers.h"
#include "GB/Game/FollowCamera.h"
#include "GB/Log.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) < eps; }

	GB::PhysicsWorld Flat(float y = 0.0f)
	{
		GB::PhysicsWorld w;
		w.SetGround([y](float, float) { return y; }, nullptr);
		return w;
	}
	// Ground rising along +z with the given slope (rise over run).
	GB::PhysicsWorld Ramp(float k)
	{
		GB::PhysicsWorld w;
		w.SetGround([k](float, float z) { return k * z; }, [k](float, float) { return glm::normalize(glm::vec3(0, 1, -k)); });
		return w;
	}
	GB::StaticCollider Box(glm::vec3 center, glm::vec3 half)
	{
		GB::StaticCollider s;
		s.Shape = GB::ShapeType::Box;
		s.Center = center;
		s.HalfExtents = half;
		return s;
	}
	void Run(GB::CharacterMotor& m, const GB::MoveInput& in, float seconds, const GB::PhysicsWorld& w, float dt = 1.0f / 60.0f)
	{
		for (float t = 0; t < seconds - 1e-6f; t += dt) m.Step(in, dt, w);
	}
	GB::MoveInput Walk(float x, float z) { GB::MoveInput in; in.Velocity = { x, z }; return in; }
}

int main()
{
	using namespace GB;
	Log::Init();

	// ---- walking on flat ground
	{
		const PhysicsWorld w = Flat();
		CharacterMotor m;
		m.SetPosition({ 0, 0, 0 });
		Run(m, Walk(0, 3), 2.0f, w);
		const MotionState& s = m.State();
		Check(s.Grounded && Near(s.Position.y, 0.0f, 1e-4f), "stands on the ground");
		Check(Near(s.Speed, 3.0f, 0.01f) && s.Position.z > 5.0f && s.Position.z < 6.0f, "reaches the wanted speed and travels");
		Check(Near(s.LocalVelocity.y, 3.0f, 0.05f) && std::fabs(s.LocalVelocity.x) < 0.05f && Near(s.Yaw, 0.0f, 0.01f), "forward speed in the character's own axes");
		Run(m, Walk(0, 0), 0.5f, w);
		Check(s.Speed < 0.01f, "stops when told to");
		Check(m.State().LocalAcceleration == m.State().LocalAcceleration, "acceleration is a number");

		// facing follows the movement, at a limited turn rate
		CharacterMotor turn;
		turn.SetPosition({ 0, 0, 0 });
		turn.Step(Walk(4, 0), 1.0f / 60.0f, w);
		Check(turn.State().Yaw > 0.0f && turn.State().Yaw < glm::radians(10.0f), "turning is rate limited");
		Run(turn, Walk(4, 0), 1.0f, w);
		Check(Near(turn.State().Yaw, 1.5708f, 0.02f), "ends up facing where it moves (+x)");

		// strafing: facing fixed, movement sideways = left/right in local axes
		CharacterMotor strafe;
		strafe.SetPosition({ 0, 0, 0 }, 0.0f);
		MoveInput in = Walk(2, 0);
		in.HasYaw = true; in.DesiredYaw = 0.0f;
		Run(strafe, in, 1.0f, w);
		Check(Near(strafe.State().LocalVelocity.x, 2.0f, 0.05f) && Near(strafe.State().LocalVelocity.y, 0.0f, 0.05f), "+x is the character's left when it faces +z");
	}

	// ---- jumping and falling
	{
		const PhysicsWorld w = Flat();
		CharacterMotor m;
		m.SetPosition({ 0, 0, 0 });
		MoveInput jump; jump.Jump = true;
		m.Step(jump, 1.0f / 60.0f, w);
		Check(m.State().Jumped && !m.State().Grounded, "a jump leaves the ground");
		float apex = 0.0f, airtime = 0.0f;
		int landings = 0;
		float landingSpeed = 0.0f;
		for (int i = 0; i < 200; ++i)
		{
			m.Step(MoveInput(), 1.0f / 120.0f, w);
			apex = std::max(apex, m.State().Position.y);
			if (!m.State().Grounded) airtime += 1.0f / 120.0f;
			if (m.State().Landed) { ++landings; landingSpeed = m.State().LandingSpeed; }
		}
		Check(Near(apex, 1.1f, 0.08f), "the jump reaches the configured height");
		Check(landings == 1 && Near(landingSpeed, std::sqrt(2.0f * 22.0f * 1.1f), 0.5f), "one landing, at the speed it fell");
		Check(Near(airtime, 2.0f * std::sqrt(2.0f * 1.1f / 22.0f), 0.08f), "airtime follows from height and gravity");
		Check(m.State().Grounded && Near(m.State().Position.y, 0.0f, 1e-4f), "back on the ground");

		CharacterMotor fall;
		fall.SetPosition({ 0, 5, 0 });
		fall.Step(MoveInput(), 1.0f / 60.0f, w);   // settles: the ground is 5 m below, so it starts falling
		Check(fall.State().Grounded == false || fall.State().Position.y <= 5.0f, "starts falling");
		bool landed = false; float speed = 0.0f;
		for (int i = 0; i < 300 && !landed; ++i) { fall.Step(MoveInput(), 1.0f / 60.0f, w); landed = fall.State().Landed; speed = fall.State().LandingSpeed; }
		Check(landed && Near(speed, std::sqrt(2.0f * 22.0f * 5.0f), 1.0f), "a fall lands at the right speed");
	}

	// ---- slopes
	{
		const PhysicsWorld gentle = Ramp(std::tan(glm::radians(25.0f)));
		CharacterMotor m;
		m.SetPosition({ 0, 0, 0 });
		Run(m, Walk(0, 3), 2.0f, gentle);
		Check(m.State().Grounded && m.State().Position.z > 4.0f && Near(m.State().Position.y, std::tan(glm::radians(25.0f)) * m.State().Position.z, 0.15f), "climbs a gentle slope, staying on it");
		Check(Near(m.State().SlopeDegrees, 25.0f, 1.0f), "reports the slope");

		const PhysicsWorld wall = Ramp(std::tan(glm::radians(65.0f)));
		CharacterMotor steep;
		steep.SetPosition({ 0, 0, 0 });
		Run(steep, Walk(0, 3), 2.0f, wall);
		Check(steep.State().Position.z < 0.6f, "cannot walk up a slope steeper than the limit");

		CharacterMotor slide;
		slide.SetPosition({ 0, std::tan(glm::radians(65.0f)) * 5.0f, 5 });
		Run(slide, Walk(0, 0), 0.0f, wall);
		slide.Step(MoveInput(), 1.0f / 60.0f, wall);
		Run(slide, MoveInput(), 1.0f, wall);
		Check(slide.State().Position.z < 4.5f, "slides down a slope too steep to stand on");
	}

	// ---- steps, walls and ledges
	{
		PhysicsWorld w = Flat();
		w.AddStatic(Box({ 0, 0.15f, 3 }, { 2, 0.15f, 0.5f }));       // a 0.3 m step
		CharacterMotor m;
		m.SetPosition({ 0, 0, 0 });
		Run(m, Walk(0, 2), 3.0f, w);
		Check(m.State().Position.z > 3.8f && m.State().Grounded, "walks over a low step");
		CharacterMotor onTop;
		onTop.SetPosition({ 0, 0, 0 });
		Run(onTop, Walk(0, 2), 1.7f, w);
		Check(Near(onTop.State().Position.y, 0.3f, 0.02f) && onTop.State().Grounded, "stands on top of the step");

		PhysicsWorld wallWorld = Flat();
		wallWorld.AddStatic(Box({ 0, 1, 3 }, { 20, 1, 0.5f }));      // a long 2 m wall
		CharacterMotor blocked;
		blocked.SetPosition({ 0, 0, 0 });
		Run(blocked, Walk(0, 3), 2.0f, wallWorld);
		Check(blocked.State().Position.z < 2.2f + 0.02f && blocked.State().Position.z > 2.0f, "stops at a wall, a radius away");
		CharacterMotor sliding;
		sliding.SetPosition({ 0, 0, 0 });
		Run(sliding, Walk(2, 2), 1.5f, wallWorld);
		Check(sliding.State().Position.x > 1.0f && sliding.State().Position.z < 2.25f, "slides along a wall instead of sticking to it");

		PhysicsWorld ledge;
		ledge.SetGround([](float, float z) { return z > 5.0f ? -3.0f : 0.0f; }, nullptr);
		CharacterMotor off;
		off.SetPosition({ 0, 0, 0 });
		bool landed = false;
		for (int i = 0; i < 400; ++i) { off.Step(Walk(0, 2), 1.0f / 60.0f, ledge); landed = landed || off.State().Landed; }
		Check(landed && Near(off.State().Position.y, -3.0f, 0.01f), "walks off a ledge, falls and lands below");

		// coyote time: a jump just after leaving the edge still works, a late one does not
		auto jumpAfter = [&](float seconds)
		{
			CharacterMotor c;
			c.SetPosition({ 0, 0, 4.9f });
			float t = 0.0f;
			while (c.State().Grounded || t < 0.001f) { c.Step(Walk(0, 2), 1.0f / 120.0f, ledge); t += 1.0f / 120.0f; if (t > 2) break; }
			for (float e = 0; e < seconds; e += 1.0f / 120.0f) c.Step(Walk(0, 2), 1.0f / 120.0f, ledge);
			MoveInput j = Walk(0, 2); j.Jump = true;
			c.Step(j, 1.0f / 120.0f, ledge);
			return c.State().Jumped;
		};
		Check(jumpAfter(0.03f), "a jump right after leaving a ledge works (coyote time)");
		Check(!jumpAfter(0.3f), "a jump long after leaving it does not");
	}

	// ---- water
	{
		PhysicsWorld w = Flat();
		w.SetWater([](float, float) { return 1.0f; });
		CharacterMotor m;
		m.SetPosition({ 0, 0, 0 });
		Run(m, Walk(0, 3), 2.0f, w);
		Check(m.State().InWater && Near(m.State().WaterDepth, 1.0f, 0.02f), "knows when it is wading");
		Check(Near(m.State().Speed, 3.0f * 0.55f, 0.1f), "wading is slower");
	}

	// ---- determinism and frame rate
	{
		const PhysicsWorld w = Ramp(0.2f);
		auto run = [&](float dt)
		{
			CharacterMotor m;
			m.SetPosition({ 0, 0, 0 });
			Run(m, Walk(1, 3), 2.0f, w, dt);
			return m.State();
		};
		const MotionState a = run(1.0f / 60.0f), b = run(1.0f / 60.0f), slow = run(1.0f / 30.0f), fast = run(1.0f / 120.0f);
		Check(std::memcmp(&a.Position, &b.Position, sizeof a.Position) == 0, "the same inputs give the same motion, bit for bit");
		Check(glm::distance(slow.Position, fast.Position) < 0.15f, "the result hardly depends on the frame rate");
	}

	// ---- controllers
	{
		PlayerController p;
		p.SetInput({ { 0, 1 }, false, false });
		p.SetCameraYaw(0.0f);
		MotionState dummy;
		MoveInput out = p.Update(dummy, 0.016f);
		Check(Near(out.Velocity.x, 0.0f, 1e-4f) && Near(out.Velocity.y, 1.7f, 1e-4f), "forward with the camera looking +z goes +z at walking speed");
		p.SetCameraYaw(glm::radians(90.0f));
		out = p.Update(dummy, 0.016f);
		Check(Near(out.Velocity.x, 1.7f, 1e-3f) && Near(out.Velocity.y, 0.0f, 1e-3f), "forward follows the camera");
		p.SetCameraYaw(0.0f);
		p.SetInput({ { 1, 0 }, true, false });
		out = p.Update(dummy, 0.016f);
		Check(Near(out.Velocity.x, -4.4f, 1e-3f), "right is -x for a camera facing +z, and running is faster");
		p.SetInput({ { 0, 0.5f }, true, false });
		Check(Near(p.Update(dummy, 0.016f).Velocity.y, 2.2f, 1e-3f), "a half-pushed stick gives half speed");
		p.SetInput({ { 1, 1 }, false, false });
		out = p.Update(dummy, 0.016f);
		Check(Near(glm::length(out.Velocity), 1.7f, 1e-3f), "diagonals are not faster");
		p.Strafe = true;
		Check(p.Update(dummy, 0.016f).HasYaw, "strafe mode faces the camera");
		p.Strafe = false;
		p.SetInput({ { 0, 0 }, false, true });
		const bool first = p.Update(dummy, 0.016f).Jump, second = p.Update(dummy, 0.016f).Jump;
		Check(first && !second, "holding jump jumps once");

		const PhysicsWorld w = Flat();
		AIController ai;
		CharacterMotor m;
		m.SetPosition({ 0, 0, 0 });
		ai.MoveTo({ 6, 0, 0 });
		float maxSpeed = 0.0f;
		for (int i = 0; i < 600 && !ai.Arrived(); ++i) { m.Step(ai.Update(m.State(), 1.0f / 60.0f), 1.0f / 60.0f, w); maxSpeed = std::max(maxSpeed, m.State().Speed); }
		Check(ai.Arrived() && glm::distance(glm::vec2(m.State().Position.x, m.State().Position.z), glm::vec2(6, 0)) < 0.4f, "the AI walks to its target");
		Check(maxSpeed < 1.75f, "and never exceeds the walking speed");
		Run(m, MoveInput(), 0.5f, w);
		Check(m.State().Speed < 0.05f, "it ends standing still");

		AIController path;
		CharacterMotor m2;
		m2.SetPosition({ 0, 0, 0 });
		path.Run = true;
		path.SetPath({ { 4, 0, 0 }, { 4, 0, 4 }, { 0, 0, 4 } });
		for (int i = 0; i < 900 && !path.Arrived(); ++i) m2.Step(path.Update(m2.State(), 1.0f / 60.0f), 1.0f / 60.0f, w);
		Check(path.Arrived() && glm::distance(glm::vec2(m2.State().Position.x, m2.State().Position.z), glm::vec2(0, 4)) < 0.4f, "the AI follows a path through its waypoints");

		PhysicsWorld blockedWorld = Flat();
		blockedWorld.AddStatic(Box({ 3, 1, 0 }, { 0.5f, 1, 5 }));
		AIController stuck;
		CharacterMotor m3;
		m3.SetPosition({ 0, 0, 0 });
		stuck.MoveTo({ 8, 0, 0 });
		for (int i = 0; i < 600 && !stuck.Stuck() && !stuck.Arrived(); ++i) m3.Step(stuck.Update(m3.State(), 1.0f / 60.0f), 1.0f / 60.0f, blockedWorld);
		Check(stuck.Stuck() && !stuck.Arrived(), "the AI gives up when a wall blocks it");

		ScriptedController script;
		std::string error;
		Check(script.Load("move 3 0; wait 0.2; jump; move 3 3; face 180", &error), "a script loads");
		Check(!script.Load("dance 1", &error) && error.find("dance") != std::string::npos, "an unknown command is reported");
		script.Load("move 3 0; wait 0.2; jump; move 3 3; face 180");
		CharacterMotor m4;
		m4.SetPosition({ 0, 0, 0 });
		bool jumped = false;
		for (int i = 0; i < 2400 && !script.Finished(); ++i) { m4.Step(script.Update(m4.State(), 1.0f / 60.0f), 1.0f / 60.0f, w); jumped = jumped || m4.State().Jumped; }
		Check(script.Finished() && jumped, "the script runs to the end, jump included");
		Check(glm::distance(glm::vec2(m4.State().Position.x, m4.State().Position.z), glm::vec2(3, 3)) < 0.5f && std::fabs(std::fabs(m4.State().Yaw) - 3.1416f) < 0.1f, "and leaves the character where and how it said");
	}

	// ---- the camera
	{
		FollowCamera cam;
		cam.Yaw = 0.0f; cam.Pitch = glm::radians(10.0f);
		const PhysicsWorld open = Flat();
		cam.Update({ 0, 0, 0 }, 0.016f, &open);
		Check(cam.GetCamera().Position.z < -4.0f && Near(glm::length(cam.GetCamera().Position - cam.GetCamera().Target), 4.5f, 0.01f), "the camera sits behind the character at its distance");
		Check(Near(cam.GetCamera().Target.y, 1.5f, 1e-4f), "and looks at the pivot above the feet");

		PhysicsWorld walled = Flat();
		walled.AddStatic(Box({ 0, 2, -2 }, { 3, 2, 0.2f }));
		cam.Update({ 0, 0, 0 }, 0.016f, &walled);
		Check(cam.CurrentDistance() < 2.0f && cam.GetCamera().Position.z > -2.0f, "a wall behind the character pulls the camera in front of it");
		float before = cam.CurrentDistance();
		for (int i = 0; i < 20; ++i) cam.Update({ 0, 0, 0 }, 0.016f, &open);
		Check(cam.CurrentDistance() > before && cam.CurrentDistance() < 4.5f, "it backs out smoothly when the wall is gone");
		for (int i = 0; i < 300; ++i) cam.Update({ 0, 0, 0 }, 0.016f, &open);
		Check(Near(cam.CurrentDistance(), 4.5f, 0.05f), "and settles at its distance");

		cam.Rotate(0.0f, 10.0f);
		Check(Near(cam.Pitch, glm::radians(70.0f), 1e-4f), "pitch is limited");
		cam.Zoom(100.0f);
		cam.Update({ 0, 0, 0 }, 1.0f, &open);
		Check(cam.CurrentDistance() <= 12.0f + 1e-3f, "zoom is limited");

		FollowCamera low;
		low.Pitch = glm::radians(-10.0f);   // looking up from below
		const PhysicsWorld hill = Ramp(0.5f);
		low.Update({ 0, 0, 0 }, 0.016f, &hill);
		Check(low.GetCamera().Position.y >= hill.GroundHeight(low.GetCamera().Position.x, low.GetCamera().Position.z) + 0.29f, "the camera stays above the terrain");

		FollowCamera eased;
		eased.Update({ 0, 0, 0 }, 0.016f);
		eased.Update({ 10, 0, 0 }, 0.016f);
		Check(eased.GetCamera().Target.x > 0.0f && eased.GetCamera().Target.x < 10.0f, "the camera follows with a little delay");
	}

	// ---- the animator reads only the motion state, whoever produced it
	{
		const std::string dir = GB_TEST_DATA_DIR;
		auto model = Model::Load(dir + "/test_rig.fbx");
		if (!model) { std::fprintf(stderr, "FAIL: test rig\n"); return 1; }
		model->AppendAnimations(dir + "/test_clip.fbx");
		model->AppendAnimations(dir + "/test_idle.fbx");

		// two characters walk the same course: one led by an AI, the other replaying the AI's inputs
		const PhysicsWorld w = Flat();
		AIController ai;
		CharacterMotor a, b;
		a.SetPosition({ 0, 0, 0 }); b.SetPosition({ 0, 0, 0 });
		ai.MoveTo({ 5, 0, 0 });
		ModelInstance ia(*model), ib(*model);
		LocomotionOptions opt;
		CharacterAnimator anA(*model, ia, opt), anB(*model, ib, opt);
		for (const char* n : { "test_idle", "test_rig", "test_clip" }) { anA.Locomotion().AddClip(n); anB.Locomotion().AddClip(n); }
		AudioConfig cfg; cfg.Device = false;
		AudioEngine audio(cfg);
		FootstepSounds stepsA(audio), stepsB(audio);
		anA.SetFootsteps(&stepsA); anB.SetFootsteps(&stepsB);
		for (int i = 0; i < 420; ++i)
		{
			const MoveInput in = ai.Update(a.State(), 1.0f / 60.0f);
			a.Step(in, 1.0f / 60.0f, w);
			b.Step(in, 1.0f / 60.0f, w);   // the same inputs from "elsewhere" (a replay, the network...)
			const glm::mat4 place = glm::translate(glm::mat4(1.0f), a.State().Position);
			anA.Update(a.State(), 1.0f / 60.0f, place);
			anB.Update(b.State(), 1.0f / 60.0f, place);
		}
		Check(ai.Arrived(), "the AI-led character arrived");
		bool same = ia.GetPose().size() == ib.GetPose().size();
		for (size_t i = 0; same && i < ia.GetPose().size(); ++i) same = glm::all(glm::equal(ia.GetPose()[i].T, ib.GetPose()[i].T)) && glm::all(glm::equal(glm::vec4(ia.GetPose()[i].R.x, ia.GetPose()[i].R.y, ia.GetPose()[i].R.z, ia.GetPose()[i].R.w), glm::vec4(ib.GetPose()[i].R.x, ib.GetPose()[i].R.y, ib.GetPose()[i].R.z, ib.GetPose()[i].R.w)));
		Check(same, "the same motion gives the same pose, whoever moved the character");
		Check(stepsA.Count() > 3 && stepsA.Count() == stepsB.Count(), "walking plays footsteps");

		// in the air there are no footsteps; a landing is a step
		CharacterMotor air;
		air.SetPosition({ 0, 4, 0 });
		ModelInstance ic(*model);
		CharacterAnimator anC(*model, ic, opt);
		anC.Locomotion().AddClip("test_idle"); anC.Locomotion().AddClip("test_rig");
		FootstepSounds stepsC(audio);
		anC.SetFootsteps(&stepsC);
		int steps = -1, afterFall = 0;
		for (int i = 0; i < 200; ++i)
		{
			air.Step(MoveInput(), 1.0f / 60.0f, w);
			anC.Update(air.State(), 1.0f / 60.0f, glm::translate(glm::mat4(1.0f), air.State().Position));
			if (air.State().Landed) { steps = stepsC.Count(); afterFall = steps; }
		}
		Check(afterFall == 1, "a fall ends with one landing step");
	}

	if (failures) return 1;
	std::puts("character tests passed");
	return 0;
}
