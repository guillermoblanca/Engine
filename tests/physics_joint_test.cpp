// Joints in the physics world: a pendulum on a world anchor, a hinge, a chain, cone and twist limits, collision groups, removal, and a
// joined group that sleeps together. CPU only.
#include "GB/World/Physics.h"
#include <cmath>
#include <cstdio>
#include <glm/gtc/quaternion.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	const float kDt = 1.0f / 60.0f;
	GB::BodyDesc Ball(float r = 0.15f, float density = 800.0f)
	{
		GB::BodyDesc d;
		d.Shape = GB::ShapeType::Sphere;
		d.Radius = r;
		d.Density = density;
		d.LinearDamping = 0.0f;
		d.AngularDamping = 0.0f;
		return d;
	}
}

int main()
{
	using namespace GB;

	// ---- a pendulum: a ball on a rod of 2 m keeps its distance from the anchor and swings
	{
		PhysicsWorld w;
		const int b = w.AddBody(Ball(), { 2.0f, 5.0f, 0.0f });
		JointDesc j = w.MakeJoint(b, -1, { 0.0f, 5.0f, 0.0f }, { 0, 0, 1 }, { 1, 0, 0 });
		const int id = w.AddJoint(j);
		Check(id >= 0 && w.IsJointAlive(id) && w.GetJointCount() == 1, "a joint is added");
		float worstError = 0.0f, lowest = 1e9f, highest = -1e9f;
		bool crossed = false;
		for (int i = 0; i < 60 * 6; ++i)
		{
			w.Step(kDt);
			worstError = std::max(worstError, w.GetJointError(id));
			lowest = std::min(lowest, w.GetBody(b).Position.y);
			highest = std::max(highest, w.GetBody(b).Position.y);
			crossed = crossed || w.GetBody(b).Position.x < -0.5f;
		}
		Check(worstError < 0.03f, "the anchor stays put (within 3 cm)");
		Check(lowest < 3.2f && lowest > 2.9f, "the ball swings down to the length of the rod");
		Check(highest < 5.1f, "it never rises above where it was let go (energy is not created)");
		Check(crossed, "and it swings through to the other side");
		w.RemoveBody(b);
		Check(!w.IsJointAlive(id), "removing a body removes its joints");
	}

	// ---- a hinge: the ball can only swing in the plane perpendicular to its axis
	{
		PhysicsWorld w;
		const int b = w.AddBody(Ball(), { 1.5f, 5.0f, 0.0f });
		JointDesc j = w.MakeJoint(b, -1, { 0.0f, 5.0f, 0.0f }, { 0, 0, 1 }, { 1, 0, 0 });
		j.Hinge = true;
		w.AddJoint(j);
		w.GetBody(b).AddImpulse({ 0.0f, 0.0f, 3.0f }, w.GetBody(b).Position);   // pushed out of the plane
		float zDrift = 0.0f;
		for (int i = 0; i < 60 * 3; ++i) { w.Step(kDt); zDrift = std::max(zDrift, std::fabs(w.GetBody(b).Position.z)); }
		Check(zDrift < 0.05f, "a hinge keeps the ball in its plane (the push out of it is taken by the joint)");
	}

	// ---- a chain of balls hangs and stays together
	{
		PhysicsWorld w;
		std::vector<int> bodies;
		for (int i = 0; i < 6; ++i) bodies.push_back(w.AddBody(Ball(0.12f), { 0.5f * static_cast<float>(i + 1), 6.0f, 0.0f }));
		std::vector<int> joints;
		joints.push_back(w.AddJoint(w.MakeJoint(bodies[0], -1, { 0.0f, 6.0f, 0.0f }, { 0, 0, 1 }, { 1, 0, 0 })));
		for (int i = 1; i < 6; ++i)
			joints.push_back(w.AddJoint(w.MakeJoint(bodies[static_cast<size_t>(i)], bodies[static_cast<size_t>(i - 1)], { 0.5f * static_cast<float>(i) + 0.25f, 6.0f, 0.0f }, { 0, 0, 1 }, { 1, 0, 0 })));
		float worst = 0.0f, lowest = 1e9f;
		bool finite = true;
		for (int i = 0; i < 60 * 8; ++i)
		{
			w.Step(kDt);
			lowest = std::min(lowest, w.GetBody(bodies.back()).Position.y);
			for (int id : joints) worst = std::max(worst, w.GetJointError(id));
			for (int b : bodies) finite = finite && std::isfinite(w.GetBody(b).Position.y);
		}
		Check(finite && worst < 0.08f, "a chain of six stays joined (within 8 cm) and stays finite");
		Check(lowest < 4.0f && lowest > 2.2f, "and it hangs down to about its length");
	}

	// ---- a cone limit: the swing is capped
	{
		PhysicsWorld w;
		const int b = w.AddBody(Ball(), { 0.0f, 4.0f, 0.0f });
		JointDesc j = w.MakeJoint(b, -1, { 0.0f, 5.0f, 0.0f }, { 0, -1, 0 }, { 1, 0, 0 });   // the axis points down: swing = the angle from straight down
		j.SwingLimit = glm::radians(50.0f);
		const int id = w.AddJoint(j);
		w.GetBody(b).AddImpulse({ 40.0f, 0.0f, 0.0f }, w.GetBody(b).Position);   // a hard sideways shove
		float worstSwing = 0.0f;
		for (int i = 0; i < 60 * 3; ++i) { w.Step(kDt); worstSwing = std::max(worstSwing, w.GetJointSwing(id)); }
		Check(worstSwing < glm::radians(56.0f) && worstSwing > glm::radians(35.0f), "a hard shove swings it up to the limit and no further");
	}

	// ---- a twist limit
	{
		PhysicsWorld w;
		BodyDesc d; d.Shape = ShapeType::Box; d.HalfExtents = { 0.1f, 0.4f, 0.1f }; d.LinearDamping = 0.0f; d.AngularDamping = 0.0f;
		const int b = w.AddBody(d, { 0.0f, 5.0f, 0.0f });
		w.Gravity = glm::vec3(0.0f);
		JointDesc j = w.MakeJoint(b, -1, { 0.0f, 5.0f, 0.0f }, { 0, 1, 0 }, { 1, 0, 0 });
		j.TwistMin = glm::radians(-30.0f);
		j.TwistMax = glm::radians(30.0f);
		const int id = w.AddJoint(j);
		w.GetBody(b).AngularVelocity = { 0.0f, 6.0f, 0.0f };   // spinning about its axis
		float worst = 0.0f;
		for (int i = 0; i < 60 * 2; ++i) { w.Step(kDt); worst = std::max(worst, std::fabs(w.GetJointTwist(id))); }
		Check(worst < glm::radians(36.0f) && worst > glm::radians(20.0f), "the twist stops at its limit");
		w.GetBody(b).AngularVelocity = { 0.0f, -9.0f, 0.0f };
		float worstNeg = 0.0f;
		for (int i = 0; i < 60 * 2; ++i) { w.Step(kDt); worstNeg = std::max(worstNeg, std::fabs(w.GetJointTwist(id))); }
		Check(worstNeg < glm::radians(36.0f), "also the other way");
	}

	// ---- collision groups: the same group does not collide
	{
		auto fall = [](int g1, int g2)
		{
			PhysicsWorld w;
			w.SetGround([](float, float) { return 0.0f; }, nullptr);
			w.Gravity = glm::vec3(0.0f);
			BodyDesc a; a.Shape = ShapeType::Box; a.HalfExtents = glm::vec3(0.5f); a.Group = g1;
			BodyDesc b = a; b.Group = g2;
			const int ia = w.AddBody(a, { 0.0f, 2.0f, 0.0f });
			const int ib = w.AddBody(b, { 0.4f, 2.0f, 0.0f });   // overlapping
			for (int i = 0; i < 60; ++i) w.Step(kDt);
			return glm::distance(w.GetBody(ia).Position, w.GetBody(ib).Position);
		};
		Check(fall(7, 7) < 0.45f, "bodies of the same group pass through each other");
		Check(fall(7, 8) > 0.8f && fall(0, 0) > 0.8f, "other bodies push each other apart");
	}

	// ---- a joined pair resting on the ground sleeps together
	{
		PhysicsWorld w;
		w.SetGround([](float, float) { return 0.0f; }, nullptr);
		BodyDesc d; d.Shape = ShapeType::Box; d.HalfExtents = { 0.4f, 0.1f, 0.1f }; d.Group = 3;
		const int a = w.AddBody(d, { 0.0f, 1.0f, 0.0f });
		const int b = w.AddBody(d, { 0.8f, 1.0f, 0.0f });
		w.AddJoint(w.MakeJoint(b, a, { 0.4f, 1.0f, 0.0f }, { 0, 0, 1 }, { 1, 0, 0 }));
		for (int i = 0; i < 60 * 8; ++i) w.Step(kDt);
		Check(w.GetBody(a).Sleeping && w.GetBody(b).Sleeping, "a joined pair that came to rest sleeps");
		Check(w.GetBody(a).Position.y < 0.2f && w.GetBody(b).Position.y < 0.2f, "lying on the ground");
		Check(glm::distance(w.GetBody(a).Position, w.GetBody(b).Position) < 1.0f, "still joined");
	}

	// ---- an explosion carries a joined pair as one
	{
		PhysicsWorld w;
		w.SetGround([](float, float) { return 0.0f; }, nullptr);
		BodyDesc d; d.Shape = ShapeType::Box; d.HalfExtents = { 0.3f, 0.3f, 0.3f }; d.Group = 5;
		const int a = w.AddBody(d, { 0.0f, 0.31f, 0.0f });
		const int b = w.AddBody(d, { 0.6f, 0.31f, 0.0f });
		const int j = w.AddJoint(w.MakeJoint(b, a, { 0.3f, 0.31f, 0.0f }, { 0, 0, 1 }, { 1, 0, 0 }));
		for (int i = 0; i < 120; ++i) w.Step(kDt);
		w.Explode({ -2.0f, 0.0f, 0.0f }, 6.0f, 12.0f);
		for (int i = 0; i < 90; ++i) w.Step(kDt);
		Check(w.GetBody(a).Position.x > 1.0f && w.GetBody(b).Position.x > 1.5f, "both are thrown away from the blast");
		Check(w.GetJointError(j) < 0.1f, "without coming apart");
	}

	if (failures) return 1;
	std::puts("physics joint tests passed");
	return 0;
}
