// Blasts and breakage: explosions push bodies, hard hits and blasts break destructible bodies into fragments that
// inherit motion and vanish later; a brick wall stands until a heavy ball hits it. CPU only.
#include "GB/World/Destruction.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	GB::PhysicsWorld Flat()
	{
		GB::PhysicsWorld w;
		w.SetGround([](float, float) { return 0.0f; }, nullptr);
		return w;
	}
	void Run(GB::PhysicsWorld& w, GB::DestructionSystem* d, float seconds)
	{
		for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f)
		{
			w.Step(1.0f / 60.0f);
			if (d) d->Update(1.0f / 60.0f, w.TakeContacts()); else w.TakeContacts();
		}
	}
}

int main()
{
	// ---- an explosion pushes bodies away, weaker with distance and for heavy bodies
	{
		auto w = Flat();
		GB::BodyDesc d; d.HalfExtents = glm::vec3(0.4f);
		const int near1 = w.AddBody(d, { 2, 0.4f, 0 }), far1 = w.AddBody(d, { 9, 0.4f, 0 }), out = w.AddBody(d, { 30, 0.4f, 0 });
		GB::BodyDesc heavy = d; heavy.Density = 3000.0f;
		const int stone = w.AddBody(heavy, { 0, 0.4f, 2 });
		const auto hits = w.Explode({ 0, 0.4f, 0 }, 12.0f, 14.0f);
		Check(hits.size() == 3, "only bodies in range are affected");
		Check(w.GetBody(near1).Velocity.x > 3.0f && w.GetBody(near1).Velocity.y > 0.5f, "a near body is thrown away and up");
		Check(w.GetBody(far1).Velocity.x > 0.0f && w.GetBody(far1).GetSpeed() < w.GetBody(near1).GetSpeed() * 0.5f, "a far body is pushed much less");
		Check(w.GetBody(out).GetSpeed() == 0.0f, "bodies out of range are untouched");
		Check(w.GetBody(stone).GetSpeed() < w.GetBody(near1).GetSpeed(), "heavy bodies move less");
	}
	// ---- a crate breaks into fragments when the blast is strong enough, and not when it is weak
	{
		auto w = Flat();
		GB::DestructionSystem ds(w);
		GB::BodyDesc d; d.HalfExtents = glm::vec3(0.5f);
		GB::DestructibleDesc dd; dd.Toughness = 600.0f; dd.Split = { 2, 2, 2 }; dd.DebrisLifetime = 3.0f;
		const int weak = w.AddBody(d, { 20, 0.5f, 0 }), strong = w.AddBody(d, { 2, 0.5f, 0 });
		ds.Add(weak, dd); ds.Add(strong, dd);
		const float mass = w.GetBody(strong).Mass;
		int spawned = 0;
		ds.OnFragment = [&](int, int from, const glm::vec3& size) { if (from == strong && size.x > 0.4f && size.x < 0.6f) ++spawned; };
		ds.Explode({ 0, 0.5f, 0 }, 25.0f, 16.0f);
		ds.Update(1.0f / 60.0f, {});
		Check(!w.IsAlive(strong) && w.IsAlive(weak), "the crate near the blast breaks, the far one only gets shaken");
		Check(spawned == 8, "a 2x2x2 split gives 8 fragments of half the size");
		const auto breaks = ds.TakeBreaks();
		Check(breaks.size() == 1 && breaks[0].Fragments == 8 && breaks[0].Position.x > 1.5f, "the break is reported with its position and fragment count");
		float total = 0.0f; int n = 0; float maxSpeed = 0.0f;
		for (size_t i = 0; i < w.GetBodyCount(); ++i)
			if (w.IsAlive(static_cast<int>(i)) && ds.IsDebris(static_cast<int>(i))) { total += w.GetBody(static_cast<int>(i)).Mass; ++n; maxSpeed = std::max(maxSpeed, w.GetBody(static_cast<int>(i)).GetSpeed()); }
		Check(n == 8 && total > mass * 0.85f && total < mass * 1.0f, "the fragments weigh about as much as the crate (a hair less for the gaps)");
		Check(maxSpeed > 3.0f, "and fly apart");
		Run(w, &ds, 1.0f);
		std::fprintf(stderr, "info: debris %zu broken %zu\n", ds.GetDebrisCount(), ds.GetBrokenCount());
		Check(ds.GetDebrisCount() == 8, "the debris is still there after a second");
		Run(w, &ds, 4.0f);
		Check(ds.GetDebrisCount() == 0 && w.GetBodyCount() >= 1, "and vanishes after its lifetime");
		bool onlyWeakLeft = true;
		for (size_t i = 0; i < w.GetBodyCount(); ++i)
			if (w.IsAlive(static_cast<int>(i)) && static_cast<int>(i) != weak) onlyWeakLeft = false;
		Check(onlyWeakLeft, "only the untouched crate remains");
	}
	// ---- a hard hit breaks it, soft taps accumulate damage, a sphere breaks into four
	{
		auto w = Flat();
		GB::DestructionSystem ds(w);
		GB::BodyDesc d; d.HalfExtents = glm::vec3(0.5f);
		GB::DestructibleDesc dd; dd.Toughness = 100.0f;
		const int crate = w.AddBody(d, { 0, 0.5f, 0 });
		ds.Add(crate, dd);
		ds.Damage(crate, 30.0f, { 0, 0, 0 }); ds.Update(0.0f, {});
		Check(w.IsAlive(crate), "a light hit does not break it");
		ds.Damage(crate, 40.0f, { 0, 0, 0 }); ds.Damage(crate, 40.0f, { 0, 0, 0 }); ds.Update(0.0f, {});
		Check(!w.IsAlive(crate), "hits accumulate and eventually break it");
		GB::BodyDesc sp; sp.Shape = GB::ShapeType::Sphere; sp.Radius = 0.6f;
		const int ball = w.AddBody(sp, { 5, 0.6f, 0 });
		ds.Add(ball, dd);
		ds.Damage(ball, 500.0f, { 5, 0, 0 }); ds.Update(0.0f, {});
		int pieces = 0;
		for (size_t i = 0; i < w.GetBodyCount(); ++i)
			if (w.IsAlive(static_cast<int>(i)) && w.GetBody(static_cast<int>(i)).Desc.Shape == GB::ShapeType::Sphere) ++pieces;
		Check(!w.IsAlive(ball) && pieces == 4, "a sphere breaks into four smaller spheres");
		// a body dropped from a height breaks on impact (contact events feed the system)
		auto w2 = Flat();
		GB::DestructionSystem ds2(w2);
		GB::DestructibleDesc fragile; fragile.Toughness = 40.0f;
		const int dropped = w2.AddBody(d, { 0, 8.0f, 0 });
		const int sturdy = w2.AddBody(d, { 5, 8.0f, 0 });
		ds2.Add(dropped, fragile);
		GB::DestructibleDesc strongDesc; strongDesc.Toughness = 1e6f;
		ds2.Add(sturdy, strongDesc);
		Run(w2, &ds2, 2.0f);
		Check(ds2.GetBrokenCount() == 1 && w2.IsAlive(sturdy), "a fragile crate breaks when it lands from 8 m, a sturdy one does not");
		// fragments that may break again
		auto w3 = Flat();
		GB::DestructionSystem ds3(w3);
		GB::DestructibleDesc again; again.Toughness = 30.0f; again.FragmentsBreakAgain = true; again.FragmentToughness = 30.0f; again.DebrisLifetime = 20.0f;
		const int big = w3.AddBody(d, { 0, 12.0f, 0 });
		ds3.Add(big, again);
		Run(w3, &ds3, 3.0f);
		Check(ds3.GetBrokenCount() > 1, "fragments marked as breakable break again when they land hard");
	}
	// ---- a brick wall stands, and falls apart when a heavy ball hits it
	{
		auto w = Flat();
		GB::DestructionSystem ds(w);
		GB::BodyDesc brick; brick.HalfExtents = { 0.4f, 0.15f, 0.2f }; brick.Density = 1800.0f; brick.Friction = 0.7f;
		GB::DestructibleDesc dd; dd.Toughness = 1e9f;   // bricks only move, they do not break
		std::vector<int> bricks;
		GB::BodyDesc half = brick; half.HalfExtents.x = 0.2f;
		auto addBrick = [&](const GB::BodyDesc& d, float x, int row)
		{
			bricks.push_back(w.AddBody(d, { x, 0.15f + 0.3002f * static_cast<float>(row), 10.0f }));
			ds.Add(bricks.back(), dd);
		};
		for (int row = 0; row < 8; ++row)
		{
			if (row % 2 == 0)
				for (int col = 0; col < 7; ++col) addBrick(brick, (static_cast<float>(col) - 3.0f) * 0.82f, row);
			else
			{
				for (int col = 0; col < 6; ++col) addBrick(brick, (static_cast<float>(col) - 2.5f) * 0.82f, row);
				addBrick(half, -2.65f, row); addBrick(half, 2.65f, row);   // half bricks close the ends
			}
		}
		Run(w, &ds, 4.0f);
		float drift = 0.0f; int sleeping = 0;
		for (int b : bricks) { drift = std::max(drift, std::fabs(w.GetBody(b).Position.z - 10.0f)); if (w.GetBody(b).Sleeping) ++sleeping; }
		std::fprintf(stderr, "info: wall drift %.3f, sleeping %d of %zu\n", drift, sleeping, bricks.size());
		Check(drift < 0.12f && sleeping > static_cast<int>(bricks.size()) * 9 / 10, "the wall stands still and its bricks fall asleep");
		GB::BodyDesc ballDesc; ballDesc.Shape = GB::ShapeType::Sphere; ballDesc.Radius = 0.45f; ballDesc.Density = 5000.0f;
		const int ball = w.AddBody(ballDesc, { 0, 1.2f, 0 });
		w.GetBody(ball).Velocity = { 0, 2.0f, 22.0f };
		Run(w, &ds, 3.0f);
		int moved = 0;
		for (int b : bricks) if (std::fabs(w.GetBody(b).Position.z - 10.0f) > 0.5f || w.GetBody(b).Position.y < 0.1f) ++moved;
		Check(moved > 6, "a heavy ball knocks bricks out of the wall");
		float highest = 0.0f;
		for (int b : bricks) highest = std::max(highest, w.GetBody(b).Position.y);
		Check(highest < 2.4f, "and the wall comes down (nothing stays at the original height)");
	}
	// ---- a blast on a wall throws its bricks
	{
		auto w = Flat();
		GB::DestructionSystem ds(w);
		GB::BodyDesc brick; brick.HalfExtents = { 0.4f, 0.15f, 0.2f }; brick.Density = 1800.0f;
		std::vector<int> bricks;
		for (int row = 0; row < 6; ++row)
			for (int col = 0; col < 5; ++col)
				bricks.push_back(w.AddBody(brick, { (static_cast<float>(col) - 2.0f) * 0.82f + (row % 2 ? 0.0f : 0.0f), 0.15f + 0.3002f * static_cast<float>(row), 0.0f }));   // stacked columns: stable on their own
		Run(w, nullptr, 3.0f);
		ds.Explode({ 0, 0.8f, -1.5f }, 6.0f, 20.0f);
		Run(w, &ds, 1.5f);
		int flown = 0;
		for (int b : bricks) if (w.GetBody(b).Position.z > 1.5f) ++flown;
		Check(flown >= 10, "a blast behind the wall throws many bricks out the other side");
	}
	return failures;
}
