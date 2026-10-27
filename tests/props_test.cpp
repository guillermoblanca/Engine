// Sockets, props with hand grips and two-bone IK: the humanoid rig is found, the standard sockets sit where a hand, the back and the
// carrying point are and follow their bones, the IK solver reaches (and keeps the bone lengths, stretches toward unreachable targets),
// and a box attached to the carrying socket pulls both hands onto its grips. The generated humanoid always; CPU only.
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/IK.h"
#include "GB/Animation/Rig.h"
#include "GB/Animation/Socket.h"
#include "GB/Assets/Model.h"
#include "GB/Log.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	glm::vec3 Pos(const glm::mat4& m) { return glm::vec3(m[3]); }
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;
	auto model = Model::Load(dir + "/test_humanoid.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test humanoid\n"); return 1; }

	// ---- the rig
	const HumanoidRig rig = FindHumanoidRig(*model);
	Check(rig.HasArms() && rig.HasLegs() && rig.Hips >= 0 && rig.Head >= 0 && rig.Spine2 >= 0 && rig.Left.Middle >= 0, "the humanoid rig is found by name");
	{
		auto bare = Model::Load(dir + "/test_rig.fbx");
		const HumanoidRig partial = FindHumanoidRig(*bare);
		Check(!partial.HasArms() && partial.Hips >= 0 && partial.Head >= 0, "a skeleton without arms gives a partial rig");
		Check(StandardSockets(*bare, partial).Find("hand_l") == nullptr && StandardSockets(*bare, partial).Find("head") != nullptr, "sockets are made only where the bones exist");
	}

	// ---- sockets
	const SocketSet sockets = StandardSockets(*model, rig);
	for (const char* n : { "hand_l", "hand_r", "head", "back", "carry", "hip_l", "hip_r", "foot_l", "foot_r" })
		Check(sockets.Find(n) != nullptr, n);
	std::vector<glm::mat4> world;
	PoseToWorld(*model, RestPose(*model), world);
	const glm::vec3 wrist = Pos(world[static_cast<size_t>(rig.Left.Hand)]), finger = Pos(world[static_cast<size_t>(rig.Left.Middle)]);
	const glm::vec3 palm = Pos(sockets.Matrix(world, "hand_l"));
	Check(glm::distance(palm, wrist + (finger - wrist) * 0.6f) < 1e-4f, "the hand socket is the middle of the palm");
	Check(Pos(sockets.Matrix(world, "hand_l")).x > 0.0f && Pos(sockets.Matrix(world, "hand_r")).x < 0.0f, "left is +X, right is -X");
	Check(Pos(sockets.Matrix(world, "carry")).z > Pos(sockets.Matrix(world, "back")).z + 0.3f, "carry is in front, back behind");
	{
		// the socket follows its bone: bend the left arm and the socket moves with the hand, keeping its place in the bone's frame
		Pose pose = RestPose(*model);
		pose[static_cast<size_t>(rig.Left.UpperArm)].R = glm::angleAxis(1.2f, glm::vec3(1, 0, 0)) * pose[static_cast<size_t>(rig.Left.UpperArm)].R;
		std::vector<glm::mat4> moved;
		PoseToWorld(*model, pose, moved);
		const float before = glm::distance(Pos(sockets.Matrix(world, "hand_l")), Pos(world[static_cast<size_t>(rig.Left.Hand)]));
		const float after = glm::distance(Pos(sockets.Matrix(moved, "hand_l")), Pos(moved[static_cast<size_t>(rig.Left.Hand)]));
		Check(glm::distance(Pos(moved[static_cast<size_t>(rig.Left.Hand)]), wrist) > 0.2f && std::fabs(before - after) < 1e-4f, "a socket moves with its bone, at the same distance");
	}

	// ---- two-bone IK
	const TwoBoneChain arm{ rig.Left.UpperArm, rig.Left.ForeArm, rig.Left.Hand };
	auto boneLength = [&](const Pose& p, int a, int b)
	{
		std::vector<glm::mat4> w;
		PoseToWorld(*model, p, w);
		return glm::distance(Pos(w[static_cast<size_t>(a)]), Pos(w[static_cast<size_t>(b)]));
	};
	auto handOf = [&](const Pose& p)
	{
		std::vector<glm::mat4> w;
		PoseToWorld(*model, p, w);
		return Pos(w[static_cast<size_t>(rig.Left.Hand)]);
	};
	{
		const Pose rest = RestPose(*model);
		const glm::vec3 hand = handOf(rest);
		const glm::vec3 target = hand + glm::vec3(-0.12f, 0.15f, 0.2f);
		Pose pose = rest;
		const IKResult r = SolveTwoBoneIK(*model, pose, arm, target, hand + glm::vec3(0, 0, -1));
		Check(r.Solved && r.Reached && r.Error < 1e-3f && glm::distance(handOf(pose), target) < 1e-3f, "the hand reaches a target within reach");
		Check(std::fabs(boneLength(pose, arm.Root, arm.Mid) - boneLength(rest, arm.Root, arm.Mid)) < 1e-4f
			&& std::fabs(boneLength(pose, arm.Mid, arm.End) - boneLength(rest, arm.Mid, arm.End)) < 1e-4f, "the bone lengths are kept");
		// the elbow bends toward the pole
		std::vector<glm::mat4> w;
		PoseToWorld(*model, pose, w);
		Pose behind = rest;
		SolveTwoBoneIK(*model, behind, arm, target, hand + glm::vec3(0, 0, -1));
		Pose front = rest;
		SolveTwoBoneIK(*model, front, arm, target, hand + glm::vec3(0, 0, 1));
		std::vector<glm::mat4> wb, wf;
		PoseToWorld(*model, behind, wb);
		PoseToWorld(*model, front, wf);
		Check(Pos(wf[static_cast<size_t>(arm.Mid)]).z > Pos(wb[static_cast<size_t>(arm.Mid)]).z + 0.05f, "the elbow bends toward the pole");

		Pose far = rest;
		const glm::vec3 shoulder = Pos(world[static_cast<size_t>(arm.Root)]);
		const glm::vec3 faraway = shoulder + glm::normalize(glm::vec3(0.3f, 0.2f, 1.0f)) * 3.0f;
		const IKResult f = SolveTwoBoneIK(*model, far, arm, faraway, faraway);
		const float reach = boneLength(rest, arm.Root, arm.Mid) + boneLength(rest, arm.Mid, arm.End);
		Check(f.Solved && !f.Reached && std::fabs(glm::distance(handOf(far), shoulder) - reach) < 2e-3f, "an unreachable target gives a fully stretched arm");
		Check(glm::dot(glm::normalize(handOf(far) - shoulder), glm::normalize(faraway - shoulder)) > 0.999f, "... aimed at the target");

		Pose none = rest;
		SolveTwoBoneIK(*model, none, arm, target, hand, 0.0f);
		Check(glm::distance(handOf(none), hand) < 1e-6f, "weight 0 leaves the pose alone");
		Pose half = rest;
		SolveTwoBoneIK(*model, half, arm, target, hand + glm::vec3(0, 0, -1), 0.5f);
		const float d = glm::distance(handOf(half), hand);
		Check(d > 0.02f && glm::distance(handOf(half), target) > 0.02f, "weight 0.5 is in between");

		Pose rot = rest;
		const glm::quat wanted = glm::angleAxis(0.9f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.3f)));
		SolveTwoBoneIK(*model, rot, arm, target, hand + glm::vec3(0, 0, -1), 1.0f, &wanted);
		std::vector<glm::mat4> wr;
		PoseToWorld(*model, rot, wr);
		const glm::quat got = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(wr[static_cast<size_t>(arm.End)][0])), glm::normalize(glm::vec3(wr[static_cast<size_t>(arm.End)][1])), glm::normalize(glm::vec3(wr[static_cast<size_t>(arm.End)][2]))));
		Check(std::fabs(glm::dot(got, wanted)) > 0.9999f, "the hand takes the wanted orientation");

		const glm::quat q = RotationBetween({ 1, 0, 0 }, { 0, 1, 0 });
		Check(glm::distance(q * glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)) < 1e-5f && glm::distance(RotationBetween({ 0, 0, 1 }, { 0, 0, -1 }) * glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)) < 1e-5f, "the shortest rotation, also for opposite directions");
	}

	// ---- a box carried in both hands
	ModelInstance inst(*model);
	AnimInstance anim(*model, inst);
	anim.Default().AddClip("test_humanoid");
	const glm::mat4 place = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, -2.0f)) * glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0, 1, 0));
	MotionState still;
	const float dt = 1.0f / 60.0f;
	for (int i = 0; i < 10; ++i) anim.Update(still, dt, place);

	Prop box;
	box.Name = "box";
	box.Socket = "carry";
	box.Grips = { { "hand_l", glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, 0.0f, 0.0f)) }, { "hand_r", glm::translate(glm::mat4(1.0f), glm::vec3(-0.2f, 0.0f, 0.0f)) } };
	Prop bad = box; bad.Name = "bad"; bad.Socket = "nowhere";
	Check(!anim.Attach(bad) && anim.Attach(box) && anim.HasProp("box") && !anim.HasProp("bad"), "a prop needs a known socket");
	Check(anim.PropWeight("box") == 0.0f, "the hands start away from the prop");
	const glm::vec3 handBefore = Pos(inst.GetNodeWorld(rig.Left.Hand));
	float handError = 1e9f, rightError = 1e9f;
	for (int i = 0; i < 40; ++i) anim.Update(still, dt, place);
	glm::mat4 boxWorld;
	Check(anim.PropMatrix("box", boxWorld) && anim.PropWeight("box") > 0.99f, "the prop is attached and the hands are on it");
	{
		const glm::mat4 invPlace = glm::inverse(place);
		const glm::mat4 modelSpace = invPlace * boxWorld;
		const glm::vec3 leftWanted = Pos(modelSpace * glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, 0.0f, 0.0f)));
		const glm::vec3 rightWanted = Pos(modelSpace * glm::translate(glm::mat4(1.0f), glm::vec3(-0.2f, 0.0f, 0.0f)));
		handError = glm::distance(Pos(sockets.Matrix(inst, "hand_l")), leftWanted);
		rightError = glm::distance(Pos(sockets.Matrix(inst, "hand_r")), rightWanted);
		Check(handError < 0.01f && rightError < 0.01f, "both palms are on the grips (within 1 cm)");
		Check(glm::distance(Pos(boxWorld), Pos(place * sockets.Matrix(inst, "carry"))) < 1e-3f, "the prop is at its socket in the world");
		Check(glm::distance(handBefore, Pos(inst.GetNodeWorld(rig.Left.Hand))) > 0.02f, "the hand moved to reach it");
	}
	{
		// a moving character: the prop goes with it
		MotionState run = still;
		glm::mat4 moved = place;
		for (int i = 0; i < 30; ++i)
		{
			moved = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.05f)) * moved;
			anim.Update(still, dt, moved);
		}
		glm::mat4 w2;
		anim.PropMatrix("box", w2);
		Check(glm::distance(Pos(w2), Pos(boxWorld)) > 1.0f, "the prop follows the character through the world");
	}
	{
		// SnapGrips: the hands stay exactly where the clips put them
		ModelInstance inst2(*model);
		AnimInstance snap(*model, inst2);
		snap.Default().AddClip("test_humanoid");
		for (int i = 0; i < 10; ++i) snap.Update(still, dt, place);
		Prop held;
		held.Name = "held";
		held.Socket = "carry";
		snap.SnapGrips(held);
		Check(held.Grips.size() == 2, "snapping gives a grip for each hand");
		// relative to the carry socket (the clip breathes: the chest moves, the hands go with it)
		const auto inCarry = [&](const char* hand) { return Pos(glm::inverse(snap.Sockets().Matrix(inst2, "carry")) * snap.Sockets().Matrix(inst2, hand)); };
		const glm::vec3 leftBefore = inCarry("hand_l"), rightBefore = inCarry("hand_r");
		snap.Attach(held);
		for (int i = 0; i < 40; ++i) snap.Update(still, dt, place);
		Check(glm::distance(inCarry("hand_l"), leftBefore) < 1e-3f && glm::distance(inCarry("hand_r"), rightBefore) < 1e-3f, "grips taken from the pose leave the hands where they are");
	}
	anim.Detach("box");
	for (int i = 0; i < 40; ++i) anim.Update(still, dt, place);
	Check(!anim.HasProp("box") && !anim.PropMatrix("box", boxWorld), "letting go removes the prop after the fade");
	Check(glm::distance(Pos(inst.GetNodeWorld(rig.Left.Hand)), handBefore) < 0.02f, "the hands go back to the clip");

	// ---- the real packs: the box carried with the Mixamo "box" clips (optional)
	const char* mixamo = std::getenv("GB_MIXAMO_DIR");
	const char* farming = std::getenv("GB_FARMING_DIR");
	if (mixamo && farming)
	{
		auto bot = Model::Load(std::string(mixamo) + "/idle.fbx");
		if (bot)
		{
			for (const std::string& folder : { std::string(mixamo), std::string(farming) })
				for (const auto& e : std::filesystem::directory_iterator(folder))
					if (e.path().extension() == ".fbx" && e.path().filename() != "X Bot.fbx" && e.path().filename() != "idle.fbx") bot->AppendAnimations(e.path().generic_string());
			ModelInstance realInst(*bot);
			AnimInstance real(*bot, realInst);
			real.Default().AddClip("idle");
			real.Default().AddClip("walking");
			Check(real.AddSet({ "box", { "box idle", "box walk arc" } }), "the box set exists");
			const HumanoidRig botRig = real.Rig();
			Check(botRig.HasArms() && botRig.HasLegs() && real.Sockets().Find("carry") && real.Sockets().Find("hand_l"), "the Mixamo rig and its sockets are found");
			real.SetOverlay("box", 0.01f);
			MotionState walk;
			walk.LocalVelocity = { 0.0f, 1.0f };
			walk.Velocity = { 0.0f, 0.0f, 1.0f };
			walk.Speed = 1.0f;
			for (int i = 0; i < 60; ++i) real.Update(walk, dt, glm::mat4(1.0f));
			// where the clip puts the hands, in the carry socket's frame; grips a little off that must still be reached
			const glm::mat4 carry = real.Sockets().Matrix(realInst, "carry");
			const glm::vec3 lp = Pos(glm::inverse(carry) * real.Sockets().Matrix(realInst, "hand_l")), rp = Pos(glm::inverse(carry) * real.Sockets().Matrix(realInst, "hand_r"));
			Prop crate;
			crate.Name = "crate";
			crate.Socket = "carry";
			crate.Grips = { { "hand_l", glm::translate(glm::mat4(1.0f), lp + glm::vec3(0.0f, 0.04f, 0.03f)) }, { "hand_r", glm::translate(glm::mat4(1.0f), rp + glm::vec3(0.0f, 0.04f, 0.03f)) } };
			Check(real.Attach(crate), "the crate is attached to the carry socket");
			// a control that moves the same way without the prop: what the clips themselves do between frames is not the IK's doing
			ModelInstance ctrlInst(*bot);
			AnimInstance ctrl(*bot, ctrlInst);
			ctrl.Default().AddClip("idle");
			ctrl.Default().AddClip("walking");
			ctrl.AddSet({ "box", { "box idle", "box walk arc" } });
			ctrl.SetOverlay("box", 0.01f);
			for (int i = 0; i < 60; ++i) ctrl.Update(walk, dt, glm::mat4(1.0f));
			auto snapshot = [&](const ModelInstance& in)
			{
				std::vector<glm::vec3> v;
				for (size_t n = 0; n < bot->GetNodes().size(); ++n) v.push_back(Pos(in.GetNodeWorld(static_cast<int>(n))));
				return v;
			};
			std::vector<glm::vec3> last = snapshot(realInst), lastCtrl = snapshot(ctrlInst);
			float pop = 0.0f;
			for (int i = 0; i < 90; ++i)
			{
				real.Update(walk, dt, glm::mat4(1.0f));
				ctrl.Update(walk, dt, glm::mat4(1.0f));
				const std::vector<glm::vec3> now = snapshot(realInst), nowCtrl = snapshot(ctrlInst);
				float clipJump = 0.0f;   // the box walk clip has a pop of its own once per loop: judge only the frames where the clip is smooth
				for (size_t n = 0; n < now.size(); ++n) clipJump = std::max(clipJump, glm::distance(nowCtrl[n], lastCtrl[n]));
				if (clipJump < 0.1f)
					for (size_t n = 0; n < now.size(); ++n)
						pop = std::max(pop, glm::distance(now[n], last[n]) - glm::distance(nowCtrl[n], lastCtrl[n]));
				last = now;
				lastCtrl = nowCtrl;
			}
			const glm::mat4 c2 = real.Sockets().Matrix(realInst, "carry");
			const float errL = glm::distance(Pos(real.Sockets().Matrix(realInst, "hand_l")), Pos(c2 * glm::translate(glm::mat4(1.0f), lp + glm::vec3(0.0f, 0.04f, 0.03f))));
			const float errR = glm::distance(Pos(real.Sockets().Matrix(realInst, "hand_r")), Pos(c2 * glm::translate(glm::mat4(1.0f), rp + glm::vec3(0.0f, 0.04f, 0.03f))));
			Check(errL < 0.015f && errR < 0.015f, "walking with the box clips, both palms stay on the grips");
			Check(pop < 0.03f, "reaching for the crate moves no bone more than the clips do (no pop)");
		}
	}

	if (failures) return 1;
	std::puts("props tests passed");
	return 0;
}
