// Rigid bodies: terrain, static colliders, stacking, buoyancy, vortices, raycasts and determinism. CPU only.
#include "GB/World/Physics.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	void Run(GB::PhysicsWorld& w, float seconds)
	{
		for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) w.Step(1.0f / 60.0f);
	}
	GB::PhysicsWorld Flat(float y = 0.0f)
	{
		GB::PhysicsWorld w;
		w.SetGround([y](float, float) { return y; }, nullptr);
		return w;
	}
}

int main()
{
	// ---- a sphere dropped on flat ground bounces and settles on it
	{
		auto w = Flat();
		GB::BodyDesc d; d.Shape = GB::ShapeType::Sphere; d.Radius = 0.5f; d.Restitution = 0.6f;
		const int s = w.AddBody(d, { 0, 4.0f, 0 });
		float lowest = 1e9f, apex = 0.0f; bool bounced = false;
		for (float t = 0.0f; t < 6.0f; t += 1.0f / 60.0f)
		{
			w.Step(1.0f / 60.0f);
			const float y = w.GetBody(s).Position.y;
			lowest = std::min(lowest, y);
			if (t > 0.9f && t < 1.2f) apex = std::max(apex, y);   // after the first hit (~0.9 s)
			if (w.GetBody(s).Velocity.y > 2.0f) bounced = true;
		}
		Check(bounced, "a bouncy sphere rebounds");
		Check(lowest > 0.4f, "the sphere never sinks into the ground");
		Check(std::fabs(w.GetBody(s).Position.y - 0.5f) < 0.02f && w.GetBody(s).Sleeping, "it comes to rest on the ground and sleeps");
		(void)apex;
		const auto contacts = w.TakeContacts();
		Check(!contacts.empty() && contacts[0].Impulse > 2.0f, "the first impact is reported with its impulse");
	}
	// ---- a box lands flat, tilted boxes fall over onto a face
	{
		auto w = Flat();
		GB::BodyDesc d; d.HalfExtents = { 0.6f, 0.4f, 0.5f };
		const int flat = w.AddBody(d, { 0, 2.0f, 0 });
		const int tilted = w.AddBody(d, { 5, 2.0f, 0 }, glm::angleAxis(0.7f, glm::normalize(glm::vec3(1, 0.3f, 0.5f))));
		Run(w, 6.0f);
		Check(std::fabs(w.GetBody(flat).Position.y - 0.4f) < 0.03f, "a box rests on its face (center at half height)");
		const float y = w.GetBody(tilted).Position.y;
		Check(y > 0.38f && y < 0.62f && w.GetBody(tilted).Sleeping, "a tilted box falls onto a face and rests");
		Check(w.GetBody(flat).GetSpeed() < 0.01f, "and is still");
	}
	// ---- a slope: spheres roll, boxes with friction stay or slide; friction is respected
	{
		GB::PhysicsWorld w;
		w.SetGround([](float x, float) { return -0.4f * x; }, [](float, float) { return glm::normalize(glm::vec3(0.4f, 1.0f, 0.0f)); });
		GB::BodyDesc d; d.Shape = GB::ShapeType::Sphere; d.Radius = 0.4f;
		const int ball = w.AddBody(d, { 0, 1.0f, 0 });
		GB::BodyDesc box; box.HalfExtents = glm::vec3(0.4f); box.Friction = 0.9f;
		const int stay = w.AddBody(box, { 0, 1.0f, 6 });
		GB::BodyDesc ice = box; ice.Friction = 0.05f;
		const int slide = w.AddBody(ice, { 0, 1.0f, -6 });
		Run(w, 4.0f);
		Check(w.GetBody(ball).Position.x > 4.0f, "a sphere rolls down the slope");
		Check(std::fabs(w.GetBody(ball).AngularVelocity.z) > 2.0f, "and spins as it rolls");
		Check(w.GetBody(stay).Position.x < 2.5f && w.GetBody(stay).Sleeping, "a high-friction box comes to rest on the slope");
		Check(w.GetBody(slide).Position.x > w.GetBody(stay).Position.x + 3.0f, "a low-friction box slides much further down");
	}
	// ---- body vs body: momentum is conserved in a head-on sphere collision; boxes can be stacked
	{
		GB::PhysicsWorld w; w.Gravity = glm::vec3(0.0f);
		GB::BodyDesc d; d.Shape = GB::ShapeType::Sphere; d.Radius = 0.5f; d.Restitution = 1.0f; d.LinearDamping = 0.0f;
		const int a = w.AddBody(d, { -3, 0, 0 }), b = w.AddBody(d, { 3, 0, 0 });
		w.GetBody(a).Velocity = { 4, 0, 0 };
		w.GetBody(b).Velocity = { -2, 0, 0 };
		const float p0 = w.GetBody(a).Mass * 4.0f - w.GetBody(b).Mass * 2.0f;
		Run(w, 3.0f);
		const float p1 = w.GetBody(a).Mass * w.GetBody(a).Velocity.x + w.GetBody(b).Mass * w.GetBody(b).Velocity.x;
		Check(std::fabs(p1 - p0) < 0.05f * std::fabs(p0), "momentum is conserved");
		Check(w.GetBody(a).Velocity.x < 0.0f && w.GetBody(b).Velocity.x > 0.0f, "the spheres bounce apart");

		auto g = Flat();
		GB::BodyDesc bx; bx.HalfExtents = glm::vec3(0.5f);
		int ids[3];
		for (int i = 0; i < 3; ++i) ids[i] = g.AddBody(bx, { 0, 0.55f + 1.02f * static_cast<float>(i), 0 });
		Run(g, 5.0f);
		Check(std::fabs(g.GetBody(ids[2]).Position.y - 2.5f) < 0.15f && std::fabs(g.GetBody(ids[2]).Position.x) < 0.3f, "three boxes stay stacked");
	}
	// ---- static colliders: a capsule (tree trunk), a box (wall) and a sphere (boulder) stop moving bodies
	{
		auto w = Flat();
		GB::StaticCollider trunk; trunk.Shape = GB::ShapeType::Capsule; trunk.Center = { 5, 2, 0 }; trunk.Radius = 0.4f; trunk.HalfHeight = 2.0f;
		w.AddStatic(trunk);
		GB::StaticCollider wall; wall.Shape = GB::ShapeType::Box; wall.Center = { 0, 1, 8 }; wall.HalfExtents = { 5, 1, 0.3f };
		w.AddStatic(wall);
		GB::BodyDesc d; d.Shape = GB::ShapeType::Sphere; d.Radius = 0.3f; d.LinearDamping = 0.0f;
		const int a = w.AddBody(d, { 0, 0.4f, 0 }), b = w.AddBody(d, { 0, 0.4f, 3 });
		w.GetBody(a).Velocity = { 8, 0, 0 };
		w.GetBody(b).Velocity = { 0, 0, 8 };
		GB::BodyDesc crate; crate.HalfExtents = glm::vec3(0.4f);
		const int c = w.AddBody(crate, { 0, 0.5f, -3 });
		w.GetBody(c).Velocity = { 6, 0, 0 };
		GB::StaticCollider rock; rock.Shape = GB::ShapeType::Sphere; rock.Center = { 6, 0.6f, -3 }; rock.Radius = 1.0f;
		w.AddStatic(rock);
		Run(w, 2.0f);
		Check(w.GetBody(a).Position.x < 4.9f, "a ball is stopped by a tree trunk");
		Check(w.GetBody(b).Position.z < 7.5f, "a ball is stopped by a wall");
		Check(w.GetBody(c).Position.x < 5.0f, "a crate is stopped by a boulder");
	}
	// ---- water: wood floats, stone sinks, entering produces a splash
	{
		GB::PhysicsWorld w;
		w.SetGround([](float, float) { return -3.0f; }, nullptr);
		w.SetWater([](float, float) { return 0.0f; });
		GB::BodyDesc wood; wood.Shape = GB::ShapeType::Sphere; wood.Radius = 0.5f; wood.Density = 500.0f;
		GB::BodyDesc stone = wood; stone.Density = 2500.0f;
		const int a = w.AddBody(wood, { 0, 2.0f, 0 }), b = w.AddBody(stone, { 4, 2.0f, 0 });
		bool splashed = false;
		for (float t = 0.0f; t < 8.0f; t += 1.0f / 60.0f)
		{
			w.Step(1.0f / 60.0f);
			for (const auto& s : w.TakeSplashes()) if (s.Speed > 2.0f) splashed = true;
		}
		const float wy = w.GetBody(a).Position.y;
		Check(wy > -0.3f && wy < 0.45f && std::fabs(w.GetBody(a).Velocity.y) < 0.3f, "wood floats with part of it above the surface");
		Check(w.GetBody(a).Submersion > 0.3f && w.GetBody(a).Submersion < 0.85f, "and is partly submerged");
		Check(w.GetBody(b).Position.y < -2.3f, "stone sinks to the bottom");
		Check(splashed, "entering the water reports a splash");
	}
	// ---- vortex: lifts and flings a light crate, a heavy boulder resists
	{
		auto w = Flat();
		GB::Vortex v; v.Position = { 0, 0, 0 }; v.Radius = 8.0f;
		GB::BodyDesc light; light.HalfExtents = glm::vec3(0.4f); light.Density = 250.0f;   // a hollow crate
		GB::BodyDesc heavy; heavy.Shape = GB::ShapeType::Sphere; heavy.Radius = 0.8f; heavy.Density = 2600.0f;
		const int c = w.AddBody(light, { 6, 0.5f, 0 }), r = w.AddBody(heavy, { 6, 0.9f, 3 });
		float maxY = 0.0f, maxSpeed = 0.0f;
		for (float t = 0.0f; t < 8.0f; t += 1.0f / 60.0f)
		{
			w.Vortices = { v };
			w.Step(1.0f / 60.0f);
			maxY = std::max(maxY, w.GetBody(c).Position.y);
			maxSpeed = std::max(maxSpeed, w.GetBody(c).GetSpeed());
		}
		Check(maxY > 6.0f, "the vortex lifts a light crate high");
		Check(maxSpeed > 8.0f, "and flings it");
		Check(w.GetBody(r).Position.y < 3.0f, "a heavy boulder mostly resists");
		w.Vortices.clear();
		Run(w, 25.0f);
		Check(std::fabs(w.GetBody(c).Position.y - 0.4f) < 0.3f, "when the vortex ends the crate falls back to the ground");
	}
	// ---- raycasts
	{
		auto w = Flat();
		GB::StaticCollider wall; wall.Shape = GB::ShapeType::Box; wall.Center = { 10, 1, 0 }; wall.HalfExtents = { 0.5f, 1, 3 };
		w.AddStatic(wall);
		GB::BodyDesc d; d.Shape = GB::ShapeType::Sphere; d.Radius = 1.0f;
		const int s = w.AddBody(d, { 0, 1.0f, 8 });
		w.GetBody(s).Sleeping = true;
		auto hit = w.Raycast({ 0, 1, 0 }, { 1, 0, 0 }, 50.0f);
		Check(hit.Hit && std::fabs(hit.Distance - 9.5f) < 0.01f && hit.Normal.x < -0.9f && hit.Body == -1, "a ray hits a wall face");
		hit = w.Raycast({ 0, 1, 0 }, { 0, 0, 1 }, 50.0f);
		Check(hit.Hit && std::fabs(hit.Distance - 7.0f) < 0.01f && hit.Body == s, "a ray hits a body and reports it");
		hit = w.Raycast({ 0, 5, 0 }, { 0, -1, 0 }, 50.0f);
		Check(hit.Hit && std::fabs(hit.Distance - 5.0f) < 0.02f && hit.Normal.y > 0.99f, "a ray hits the ground");
		hit = w.Raycast({ 0, 5, 0 }, { 0, 1, 0 }, 50.0f);
		Check(!hit.Hit, "a ray into the sky hits nothing");
	}
	// ---- determinism: the same setup gives bit-identical results
	{
		auto sim = []()
		{
			auto w = Flat();
			GB::BodyDesc d; d.HalfExtents = { 0.5f, 0.3f, 0.4f };
			for (int i = 0; i < 12; ++i) w.AddBody(d, { 0.3f * static_cast<float>(i % 3), 1.0f + 1.1f * static_cast<float>(i), 0.2f * static_cast<float>(i % 2) }, glm::angleAxis(0.3f * static_cast<float>(i), glm::vec3(0, 1, 0)));
			Run(w, 4.0f);
			return w.GetBody(11).Position;
		};
		const glm::vec3 p = sim(), q = sim();
		Check(p == q, "the simulation is deterministic");
	}
	return failures;
}
