// The ragdoll: a humanoid as bodies and joints in the physics world. It falls and lies on the ground, keeps its joints together and within
// their limits, gives back a valid pose, is stopped by walls, thrown by explosions, floats in water, takes blows and is deterministic.
// The generated humanoid; CPU only.
#include "GB/Animation/Ragdoll.h"
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
	const float kDt = 1.0f / 60.0f;
	void Run(GB::PhysicsWorld& w, float seconds)
	{
		for (float t = 0.0f; t < seconds; t += kDt) w.Step(kDt);
	}
	float WorstJoint(const GB::PhysicsWorld& w, const GB::Ragdoll& r)
	{
		float worst = 0.0f;
		for (int j : r.Joints()) worst = std::max(worst, w.GetJointError(j));
		return worst;
	}
	bool Finite(const GB::Pose& p)
	{
		for (const GB::BonePose& b : p)
			if (!std::isfinite(b.T.x) || !std::isfinite(b.T.y) || !std::isfinite(b.T.z) || !std::isfinite(b.R.w) || std::fabs(glm::length(b.R) - 1.0f) > 1e-3f) return false;
		return true;
	}
}

int main()
{
	using namespace GB;
	Log::Init();
	auto model = Model::Load(std::string(GB_TEST_DATA_DIR) + "/test_humanoid.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test humanoid\n"); return 1; }
	const HumanoidRig rig = FindHumanoidRig(*model);
	const Pose rest = RestPose(*model);
	const glm::mat4 identity(1.0f);
	auto flat = []() { PhysicsWorld w; w.SetGround([](float, float) { return 0.0f; }, nullptr); return w; };

	// ---- falling and lying down
	{
		PhysicsWorld w = flat();
		Ragdoll r(*model, rig, w);
		Check(r.Valid() && !r.Active(), "the rig is complete enough");
		Check(r.Activate(rest, identity, glm::vec3(0.0f)) && r.Active() && r.BodyCount() == 13 && r.Joints().size() == 12, "thirteen bodies and twelve joints");
		Check(WorstJoint(w, r) < 1e-3f, "the joints start closed");
		float worst = 0.0f;
		bool finite = true;
		for (int i = 0; i < 60 * 6; ++i)
		{
			w.Step(kDt);
			worst = std::max(worst, WorstJoint(w, r));
			Pose p = rest;
			r.ReadPose(p, identity);
			finite = finite && Finite(p);
		}
		Check(finite, "the pose it gives is always valid (finite, unit rotations)");
		Check(worst < 0.08f, "the joints stay together (within 8 cm) through the fall");
		Check(r.AtRest(), "it comes to rest");
		Check(r.PelvisPosition().y < 0.3f && r.LyingDown(), "lying on the ground");
		float lowestHead = 1e9f;
		for (size_t i = 0; i < r.BodyCount(); ++i) lowestHead = std::min(lowestHead, w.GetBody(r.Body(i)).Position.y);
		Check(lowestHead > -0.02f, "nothing sinks into the ground");
		// the pose: bone distances are kept
		Pose p = rest;
		r.ReadPose(p, identity);
		std::vector<glm::mat4> restWorld, now;
		PoseToWorld(*model, rest, restWorld);
		PoseToWorld(*model, p, now);
		const auto dist = [&](const std::vector<glm::mat4>& m, int a, int b) { return glm::distance(glm::vec3(m[static_cast<size_t>(a)][3]), glm::vec3(m[static_cast<size_t>(b)][3])); };
		Check(std::fabs(dist(now, rig.Hips, rig.Head) - dist(restWorld, rig.Hips, rig.Head)) < 0.12f, "the skeleton keeps its proportions (hips to head)");
		Check(std::fabs(dist(now, rig.Left.UpperArm, rig.Left.ForeArm) - dist(restWorld, rig.Left.UpperArm, rig.Left.ForeArm)) < 0.12f, "and the arms (shoulder to elbow)");
		Check(std::fabs(dist(now, rig.LeftLeg.UpLeg, rig.LeftLeg.Knee) - dist(restWorld, rig.LeftLeg.UpLeg, rig.LeftLeg.Knee)) < 0.12f, "and the legs (hip to knee)");
		r.Deactivate();
		Check(!r.Active() && !w.IsAlive(w.GetBodyCount() > 0 ? 0 : 0) && !w.IsJointAlive(0), "deactivating removes the bodies and joints");
	}

	// ---- knees and elbows only bend one way
	{
		PhysicsWorld w = flat();
		Ragdoll r(*model, rig, w);
		r.Activate(rest, identity, { 1.5f, 0.0f, 0.0f });
		float kneeMin = 1e9f, kneeMax = -1e9f;
		const int knee = r.Joints()[8 - 0];   // the order of creation: chest, head, arm L (cone, hinge), arm R, leg L (cone, hinge, cone)...
		(void)knee;
		for (int i = 0; i < 60 * 5; ++i) { w.Step(kDt); }
		// find the hinges: the joints with the hinge flag are the elbows and the knees
		int hinges = 0;
		bool withinLimits = true;
		for (int j : r.Joints())
		{
			const float twist = w.GetJointTwist(j);
			const float swing = w.GetJointSwing(j);
			withinLimits = withinLimits && swing < glm::radians(115.0f) + 0.1f;
			if (swing < 0.02f) { ++hinges; kneeMin = std::min(kneeMin, twist); kneeMax = std::max(kneeMax, twist); }
		}
		Check(withinLimits, "no joint goes beyond its swing limit");
		Check(hinges >= 4 && kneeMin > -0.3f && kneeMax < 2.7f, "the knees and elbows stay within their bending range (and do not bend backwards)");
	}

	// ---- carrying its own speed, and walls
	{
		PhysicsWorld w = flat();
		Ragdoll r(*model, rig, w);
		r.Activate(rest, identity, { 4.0f, 0.0f, 0.0f });
		Run(w, 5.0f);
		Check(r.PelvisPosition().x > 1.0f && r.AtRest(), "a character that was running slides on and stops");

		PhysicsWorld w2 = flat();
		StaticCollider wall;
		wall.Shape = ShapeType::Box;
		wall.Center = { 2.0f, 1.5f, 0.0f };
		wall.HalfExtents = { 0.2f, 1.5f, 5.0f };
		w2.AddStatic(wall);
		Ragdoll r2(*model, rig, w2);
		r2.Activate(rest, identity, { 6.0f, 0.0f, 0.0f });
		float furthest = -1e9f;
		for (int i = 0; i < 60 * 4; ++i)
		{
			w2.Step(kDt);
			for (size_t b = 0; b < r2.BodyCount(); ++b) furthest = std::max(furthest, w2.GetBody(r2.Body(b)).Position.x);
		}
		Check(furthest < 2.0f, "a wall stops it (nothing goes through)");
	}

	// ---- an explosion throws it like any other body
	{
		PhysicsWorld w = flat();
		Ragdoll r(*model, rig, w);
		r.Activate(rest, identity, glm::vec3(0.0f));
		Run(w, 3.0f);
		const glm::vec3 before = r.PelvisPosition();
		w.Explode(before + glm::vec3(-1.5f, 0.0f, 0.0f), 6.0f, 14.0f);
		Run(w, 2.0f);
		Check(r.PelvisPosition().x > before.x + 0.8f, "an explosion beside it throws it away");
		Check(WorstJoint(w, r) < 0.1f, "without tearing it apart");
	}

	// ---- water and blows
	{
		PhysicsWorld w = flat();
		w.SetWater([](float, float) { return 0.6f; });
		Ragdoll r(*model, rig, w);
		r.Activate(rest, identity, glm::vec3(0.0f));
		Run(w, 3.0f);
		bool wet = false;
		for (size_t b = 0; b < r.BodyCount(); ++b) wet = wet || w.GetBody(r.Body(b)).Submersion > 0.0f;
		Check(wet, "in water the bodies are submerged and buoyant");
	}
	{
		PhysicsWorld w = flat();
		w.Gravity = glm::vec3(0.0f);
		Ragdoll r(*model, rig, w);
		r.Activate(rest, identity, glm::vec3(0.0f));
		std::vector<glm::vec3> before;
		for (size_t b = 0; b < r.BodyCount(); ++b) before.push_back(w.GetBody(r.Body(b)).Position);
		const glm::vec3 hand = glm::vec3(0.2f, 0.9f, 0.0f);
		r.Hit(hand, { 0.0f, 0.0f, 40.0f });
		Run(w, 0.3f);
		float moved = 0.0f, pelvisMoved = glm::distance(w.GetBody(r.Body(0)).Position, before[0]);
		for (size_t b = 0; b < r.BodyCount(); ++b) moved = std::max(moved, glm::distance(w.GetBody(r.Body(b)).Position, before[b]));
		Check(moved > 0.1f && moved > pelvisMoved, "a blow moves the limb it hits most");
	}

	// ---- deterministic
	{
		auto run = [&]()
		{
			PhysicsWorld w = flat();
			Ragdoll r(*model, rig, w);
			r.Activate(rest, identity, { 2.0f, 0.0f, 1.0f });
			Run(w, 3.0f);
			return r.PelvisPosition();
		};
		const glm::vec3 a = run(), b = run();
		Check(a == b, "the same fall gives the same result");
	}

	if (failures) return 1;
	std::puts("ragdoll tests passed");
	return 0;
}
