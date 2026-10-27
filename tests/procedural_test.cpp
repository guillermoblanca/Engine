// The procedural layers: feet on uneven ground (FootIK: a step up, a drop, a slope, a placed and scaled character, weights) and the head
// turning toward a point (LookAt: reaching, limits, clearing). The generated humanoid; CPU only.
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/FootIK.h"
#include "GB/Animation/LookAt.h"
#include "GB/Assets/Model.h"
#include "GB/Log.h"
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
	bool Near(float a, float b, float eps) { return std::fabs(a - b) < eps; }
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;
	auto model = Model::Load(dir + "/test_humanoid.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test humanoid\n"); return 1; }
	const HumanoidRig rig = FindHumanoidRig(*model);
	const Pose rest = RestPose(*model);
	const float dt = 1.0f / 60.0f;
	const glm::mat4 identity(1.0f);

	std::vector<glm::mat4> w;
	auto worldOf = [&](const Pose& p) { PoseToWorld(*model, p, w); return w; };
	auto footY = [&](const Pose& p, bool left) { return worldOf(p)[static_cast<size_t>(left ? rig.LeftLeg.Foot : rig.RightLeg.Foot)][3].y; };
	auto hipsY = [&](const Pose& p) { return worldOf(p)[static_cast<size_t>(rig.Hips)][3].y; };
	auto settle = [&](FootIK& feet, Pose& pose, const glm::mat4& placement, float weight = 1.0f)
	{
		for (int i = 0; i < 90; ++i) { pose = rest; feet.Update(pose, placement, weight, dt); }
	};
	const float restLeft = footY(rest, true), restRight = footY(rest, false), restHips = hipsY(rest);

	// ---- feet
	{
		FootIK feet(*model, rig);
		Pose pose = rest;
		feet.Update(pose, identity, 1.0f, dt);
		Check(Near(footY(pose, true), restLeft, 1e-6f) && Near(hipsY(pose), restHips, 1e-6f), "without a ground function nothing changes");
		feet.SetGround([](float, float) { return 0.0f; });
		settle(feet, pose, identity);
		Check(Near(footY(pose, true), restLeft, 1e-4f) && Near(footY(pose, false), restRight, 1e-4f) && Near(hipsY(pose), restHips, 1e-4f), "flat ground leaves the clip alone");
	}
	{
		FootIK feet(*model, rig);
		feet.SetGround([](float x, float) { return x > 0.05f ? 0.2f : 0.0f; });   // the left foot (+X) is on a 20 cm step
		Pose pose = rest;
		settle(feet, pose, identity);
		Check(Near(footY(pose, true), restLeft + 0.2f, 2e-3f), "the foot on the step is lifted onto it");
		Check(Near(footY(pose, false), restRight, 2e-3f) && Near(hipsY(pose), restHips, 2e-3f), "the other foot and the pelvis stay");
	}
	{
		FootIK feet(*model, rig);
		feet.SetGround([](float x, float) { return x > 0.05f ? -0.2f : 0.0f; });   // the left foot is over a 20 cm drop
		Pose pose = rest;
		settle(feet, pose, identity);
		Check(Near(hipsY(pose), restHips - 0.2f, 2e-3f), "the pelvis drops to the lowest foot");
		Check(Near(footY(pose, true), restLeft - 0.2f, 2e-3f) && Near(footY(pose, false), restRight, 2e-3f), "the low foot reaches down, the other stays on its ground");
		Check(Near(feet.PelvisOffset(), -0.2f, 1e-3f) && Near(feet.FootOffset(true), -0.2f, 1e-3f), "the offsets are reported");
	}
	{
		// a slope: the soles tilt with it
		FootIK feet(*model, rig);
		feet.SetGround([](float, float z) { return 0.4f * z; });
		Pose pose = rest;
		settle(feet, pose, identity);
		const glm::mat4 foot = worldOf(pose)[static_cast<size_t>(rig.LeftLeg.Foot)];
		const glm::vec3 normal = glm::normalize(glm::vec3(0.0f, 1.0f, -0.4f));
		Check(glm::dot(glm::normalize(glm::vec3(foot[1])), normal) > 0.998f, "the sole follows the slope");
	}
	{
		// a placed, turned and scaled character on high ground: offsets are relative to the ground under the character
		const glm::mat4 placement = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 1.0f, -3.0f)) * glm::rotate(glm::mat4(1.0f), 0.9f, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(1.2f));
		FootIK feet(*model, rig);
		feet.SetGround([](float, float) { return 1.3f; });   // 30 cm above the character's own ground
		Pose pose = rest;
		settle(feet, pose, placement);
		const glm::vec3 foot = glm::vec3(placement * worldOf(pose)[static_cast<size_t>(rig.LeftLeg.Foot)][3]);
		Check(Near(foot.y, 1.0f + restLeft * 1.2f + 0.3f, 3e-3f), "in the world: the foot is 30 cm above where the clip had it");
		pose = rest;
		FootIK off(*model, rig);
		off.SetGround([](float, float) { return 1.3f; });
		off.Update(pose, placement, 0.0f, dt);
		Check(Near(footY(pose, true), restLeft, 1e-6f), "weight 0 leaves the pose alone");
		Pose half = rest;
		FootIK halfFeet(*model, rig);
		halfFeet.SetGround([](float, float) { return 1.3f; });
		for (int i = 0; i < 90; ++i) { half = rest; halfFeet.Update(half, placement, 0.5f, dt); }
		const float y = footY(half, true);
		Check(y > restLeft + 0.02f && y < footY(pose, true) + 0.3f / 1.2f - 0.02f, "weight 0.5 is half way");
	}
	{
		// a step over an edge is a settle, not a pop
		FootIK feet(*model, rig);
		float ground = 0.0f;
		feet.SetGround([&](float, float) { return ground; });
		Pose pose = rest;
		settle(feet, pose, identity);
		ground = 0.3f;
		float last = footY(pose, true), worst = 0.0f;
		for (int i = 0; i < 90; ++i)
		{
			pose = rest;
			feet.Update(pose, identity, 1.0f, dt);
			worst = std::max(worst, std::fabs(footY(pose, true) - last));
			last = footY(pose, true);
		}
		Check(worst < 0.08f && Near(last, restLeft + 0.3f, 3e-3f), "a sudden 30 cm edge is followed smoothly");
	}

	// ---- look at
	auto forwardOf = [&](const Pose& p)
	{
		std::vector<glm::mat4> rw, wr;
		PoseToWorld(*model, rest, rw);
		PoseToWorld(*model, p, wr);
		const auto rot = [](const glm::mat4& m) { return glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])))); };
		return glm::normalize(rot(wr[static_cast<size_t>(rig.Head)]) * glm::inverse(rot(rw[static_cast<size_t>(rig.Head)])) * glm::vec3(0.0f, 0.0f, 1.0f));
	};
	auto headPos = [&](const Pose& p) { return glm::vec3(worldOf(p)[static_cast<size_t>(rig.Head)][3]); };
	{
		LookAt look(*model, rig);
		Pose pose = rest;
		for (int i = 0; i < 30; ++i) { pose = rest; look.Update(pose, identity, 1.0f, dt); }
		Check(glm::distance(forwardOf(pose), glm::vec3(0, 0, 1)) < 1e-4f && !look.HasTarget(), "without a target the head is left alone");

		look.SetTarget(headPos(rest) + glm::vec3(2.0f, 0.0f, 2.0f));   // 45 degrees to the left, level
		for (int i = 0; i < 120; ++i) { pose = rest; look.Update(pose, identity, 1.0f, dt); }
		Check(Near(look.Yaw(), 0.7854f, 0.01f) && Near(look.Pitch(), 0.0f, 0.01f), "the gaze reaches the target's direction");
		Check(glm::dot(forwardOf(pose), glm::normalize(glm::vec3(1, 0, 1))) > 0.9995f, "the head looks at it (45 degrees to the left)");
		Check(forwardOf(pose).x > 0.0f, "left is +X");

		look.SetTarget(headPos(rest) + glm::vec3(0.0f, 1.5f, 3.0f));   // above: pitch
		for (int i = 0; i < 120; ++i) { pose = rest; look.Update(pose, identity, 1.0f, dt); }
		Check(Near(look.Pitch(), std::atan2(1.5f, 3.0f), 0.01f) && forwardOf(pose).y > 0.3f, "it looks up at a target above");

		look.SetTarget(headPos(rest) + glm::vec3(0.0f, 0.0f, -4.0f));   // directly behind: clamped
		for (int i = 0; i < 200; ++i) { pose = rest; look.Update(pose, identity, 1.0f, dt); }
		Check(std::fabs(look.Yaw()) <= look.Config.MaxYaw + 1e-3f && std::fabs(look.Yaw()) > look.Config.MaxYaw - 0.05f, "a target behind turns the head only to the limit");

		look.ClearTarget();
		for (int i = 0; i < 240; ++i) { pose = rest; look.Update(pose, identity, 1.0f, dt); }
		Check(glm::distance(forwardOf(pose), glm::vec3(0, 0, 1)) < 1e-2f, "clearing the target brings the head back");

		look.SetTarget(headPos(rest) + glm::vec3(2.0f, 0.0f, 2.0f));
		for (int i = 0; i < 120; ++i) { pose = rest; look.Update(pose, identity, 0.0f, dt); }
		Check(glm::distance(forwardOf(pose), glm::vec3(0, 0, 1)) < 1e-4f, "weight 0 leaves the head alone");

		// the turn is shared by chest, neck and head
		look.SetTarget(headPos(rest) + glm::vec3(2.0f, 0.0f, 2.0f));
		for (int i = 0; i < 120; ++i) { pose = rest; look.Update(pose, identity, 1.0f, dt); }
		const float chest = 2.0f * std::acos(glm::clamp(std::fabs(pose[static_cast<size_t>(rig.Spine2)].R.w), 0.0f, 1.0f));
		const float headTurn = 2.0f * std::acos(glm::clamp(std::fabs(pose[static_cast<size_t>(rig.Head)].R.w), 0.0f, 1.0f));
		Check(chest > 0.05f && headTurn > chest, "the chest turns a little and the head most");
	}

	// ---- inside an AnimInstance
	{
		ModelInstance inst(*model);
		AnimInstance anim(*model, inst);
		anim.Default().AddClip("idle");
		MotionState still;
		for (int i = 0; i < 10; ++i) anim.Update(still, dt, identity);
		const float before = inst.GetNodeWorld(rig.LeftLeg.Foot)[3].y;
		anim.Feet().SetGround([](float, float) { return 0.15f; });
		for (int i = 0; i < 90; ++i) anim.Update(still, dt, identity);
		Check(Near(inst.GetNodeWorld(rig.LeftLeg.Foot)[3].y, before + 0.15f, 5e-3f), "the animation instance puts the feet on the ground it is given");
		anim.Look().SetTarget(glm::vec3(0.0f, 1.6f, 3.0f));
		for (int i = 0; i < 90; ++i) anim.Update(still, dt, identity);
		Check(anim.Look().Strength() > 0.99f, "and turns the head to its target");
	}

	if (failures) return 1;
	std::puts("procedural tests passed");
	return 0;
}
