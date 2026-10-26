// Ground marks: the interaction map (stamps, recovery, scrolling window, torus seam), the foot-plant
// detector and how a material shows the marks. The GL part skips without an OpenGL 4.5 context.
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include "GB/World/Footsteps.h"
#include "GB/World/GroundInteraction.h"
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
	bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
	constexpr float kPi = 3.14159265358979f;
}

int main()
{
	GB::Log::Init();

	// ---------------------------------------------------------------- FootstepTracker (CPU)
	{
		GB::FootstepTracker tracker(2);
		tracker.PlantHeight = 0.05f;
		tracker.LiftHeight = 0.10f;
		const auto flat = [](float, float) { return 0.0f; };
		int plants[2] = { 0, 0 };
		glm::vec3 lastPos(0.0f);
		// Two feet stepping in antiphase while moving forward: each foot lands once per 1 s cycle.
		const float dt = 1.0f / 60.0f;
		for (int frame = 0; frame < 60 * 5; ++frame)
		{
			const float t = static_cast<float>(frame) * dt;
			std::vector<glm::vec3> feet(2);
			for (int f = 0; f < 2; ++f)
			{
				const float phase = t + 0.5f * static_cast<float>(f);
				const float lift = std::max(0.0f, std::sin(2.0f * kPi * phase)) * 0.2f;   // up for half the cycle, down for half
				feet[f] = { 1.2f * t, lift, 0.1f * (f == 0 ? 1.0f : -1.0f) };
			}
			for (const GB::FootPlant& p : tracker.Update(feet, flat, { 1, 0, 0 }, dt))
			{
				++plants[p.Foot];
				lastPos = p.Position;
				Check(Near(p.Position.y, 0.0f, 1e-5f) && p.Heading.x == 1.0f && p.Speed > 0.5f, "a plant is on the ground, with the heading and the foot's speed");
			}
		}
		Check(plants[0] >= 4 && plants[0] <= 5 && plants[1] >= 4 && plants[1] <= 5, "each foot lands once per cycle (5 s -> 4-5 times, none at the very first frame)");
		Check(lastPos.x > 3.0f, "plants happen where the foot is");

		// Jitter around the threshold must not produce a stream of plants (hysteresis).
		GB::FootstepTracker jitter(1);
		jitter.PlantHeight = 0.05f;
		jitter.LiftHeight = 0.10f;
		int count = 0;
		for (int i = 0; i < 200; ++i)
		{
			const float h = 0.03f + ((i % 2) ? 0.03f : 0.0f);   // alternates 0.03 / 0.06: never above LiftHeight
			count += static_cast<int>(jitter.Update({ { 0, h, 0 } }, flat, { 0, 0, 1 }, dt).size());
		}
		Check(count == 0, "no plants without lifting above LiftHeight first");

		// Terrain height matters: a foot at y = 2 on ground at y = 2 is down.
		GB::FootstepTracker onHill(1);
		onHill.PlantHeight = 0.05f;
		onHill.LiftHeight = 0.1f;
		const auto hill = [](float x, float) { return x; };
		int hillPlants = 0;
		for (int i = 0; i < 120; ++i)
		{
			const float x = static_cast<float>(i) * 0.05f;
			const float lift = (i / 30) % 2 == 0 ? 0.3f : 0.0f;
			hillPlants += static_cast<int>(onHill.Update({ { x, x + lift, 0.0f } }, hill, { 1, 0, 0 }, dt).size());
		}
		Check(hillPlants == 1 || hillPlants == 2, "plants are measured against the ground height, not y = 0");

		GB::FootstepTracker fast(1);
		fast.PlantHeight = 0.05f; fast.LiftHeight = 0.1f; fast.MinLandingSpeed = 5.0f;
		int slow = 0;
		for (int i = 0; i < 120; ++i)
		{
			const float lift = (i / 20) % 2 == 0 ? 0.3f : 0.0f;
			slow += static_cast<int>(fast.Update({ { 0.01f * static_cast<float>(i), lift, 0.0f } }, flat, { 1, 0, 0 }, dt).size());
		}
		Check(slow == 0, "slow landings are ignored when MinLandingSpeed is set");
	}

	// ---------------------------------------------------------------- GL
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP (GL part): no OpenGL 4.5 context\n");
		return failures ? failures : 77;
	}

	GB::GroundInteractionDesc desc;
	desc.Size = 64.0f;
	desc.Resolution = 512;      // 12.5 cm texels
	desc.RecoveryPerSecond = 0.0f;
	GB::GroundInteraction ground(desc);
	Check(ground.IsValid(), "interaction map initializes");

	// --- circle
	ground.StampCircle({ 3.0f, 0.0f, 4.0f }, 1.0f, 1.0f);
	Check(ground.GetPendingStampCount() == 1, "stamps are queued until Update");
	Check(Near(ground.ReadDepth(3.0f, 4.0f), 0.0f, 1e-6f), "nothing is drawn before Update");
	ground.Update(0.0f, { 0.0f, 0.0f, 0.0f });
	Check(ground.GetPendingStampCount() == 0, "Update draws and clears the queue");
	Check(ground.ReadDepth(3.0f, 4.0f) > 0.95f, "full depth at the center of a circle");
	Check(ground.ReadDepth(3.5f, 4.0f) > 0.2f && ground.ReadDepth(3.5f, 4.0f) < 0.95f, "soft falloff toward the rim");
	Check(Near(ground.ReadDepth(5.0f, 4.0f), 0.0f, 1e-6f) && Near(ground.ReadDepth(-10.0f, 20.0f), 0.0f, 1e-6f), "nothing outside");

	// --- footprint orientation: long axis along the heading
	ground.Clear();
	ground.StampFootprint({ -6.0f, 0.0f, -6.0f }, { 1, 0, 0 }, 1.0f, 0.3f, 1.0f);
	ground.StampFootprint({ 6.0f, 0.0f, 6.0f }, { 0, 0, 1 }, 1.0f, 0.3f, 1.0f);
	ground.Update(0.0f, { 0, 0, 0 });
	Check(ground.ReadDepth(-6.0f, -6.0f) > 0.8f, "footprint center");
	Check(ground.ReadDepth(-6.35f, -6.0f) > 0.3f && ground.ReadDepth(-5.65f, -6.0f) > 0.3f, "heading +X: the print extends along X");
	Check(Near(ground.ReadDepth(-6.0f, -5.65f), 0.0f, 1e-3f) && Near(ground.ReadDepth(-6.0f, -6.35f), 0.0f, 1e-3f), "heading +X: and is narrow along Z");
	Check(ground.ReadDepth(6.0f, 6.35f) > 0.3f && Near(ground.ReadDepth(6.35f, 6.0f), 0.0f, 1e-3f), "heading +Z: the print is turned 90 degrees");
	// narrower at the heel (-X) than at the toe (+X)
	const float heel = ground.ReadDepth(-6.3f, -5.9f), toe = ground.ReadDepth(-5.7f, -5.9f);
	Check(toe > heel, "the toe (front) is wider than the heel");

	// --- overlapping marks do not dig deeper than the deepest one
	ground.Clear();
	ground.Stamp({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f, 0.5f, GB::StampShape::Circle);
	ground.Stamp({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f, 0.8f, GB::StampShape::Circle);
	ground.Stamp({ 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f, 0.3f, GB::StampShape::Circle);
	ground.Update(0.0f, { 0, 0, 0 });
	Check(Near(ground.ReadDepth(0.0f, 0.0f), 0.8f, 0.03f), "overlapping stamps take the maximum, not the sum");

	// --- line / capsule
	ground.Clear();
	ground.StampLine({ 0.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f }, 0.4f, 1.0f);
	ground.Update(0.0f, { 0, 0, 0 });
	Check(ground.ReadDepth(2.0f, 0.0f) > 0.9f && ground.ReadDepth(1.0f, 0.1f) > 0.6f, "a dragged line is filled along its length");
	Check(ground.ReadDepth(-0.1f, 0.0f) > 0.4f, "round caps");
	Check(Near(ground.ReadDepth(2.0f, 0.5f), 0.0f, 1e-3f) && Near(ground.ReadDepth(-0.6f, 0.0f), 0.0f, 1e-3f), "...and nothing beyond them");

	// --- recovery
	{
		GB::GroundInteractionDesc d = desc;
		d.RecoveryPerSecond = 0.1f;
		GB::GroundInteraction recovering(d);
		recovering.StampCircle({ 0, 0, 0 }, 1.0f, 1.0f);
		recovering.Update(0.0f, { 0, 0, 0 });
		const float before = recovering.ReadDepth(0.0f, 0.0f);
		recovering.Update(10.0f, { 0, 0, 0 });
		const float after = recovering.ReadDepth(0.0f, 0.0f);
		Check(Near(after / before, std::exp(-1.0f), 0.02f), "marks fade as exp(-rate * dt)");
		for (int i = 0; i < 30; ++i) recovering.Update(10.0f, { 0, 0, 0 });
		Check(recovering.ReadDepth(0.0f, 0.0f) < 0.01f, "...until the ground is smooth again");
	}

	// --- scrolling window: marks inside survive a move, marks that fall out are forgotten
	ground.Clear();
	ground.StampCircle({ 2.0f, 0.0f, 0.0f }, 0.6f, 1.0f);
	ground.Update(0.0f, { 0, 0, 0 });
	Check(ground.ReadDepth(2.0f, 0.0f) > 0.9f, "mark placed");
	ground.Update(0.0f, { 10.0f, 0.0f, 5.0f });   // moves the window but the mark is still inside it
	Check(ground.ReadDepth(2.0f, 0.0f) > 0.9f, "a mark that stays inside the window survives scrolling");
	ground.Update(0.0f, { 45.0f, 0.0f, 0.0f });  // now 43 m behind the center: outside the 64 m window
	Check(Near(ground.ReadDepth(2.0f, 0.0f), 0.0f, 1e-6f), "a mark that leaves the window is cleared");
	Check(Near(ground.ReadDepth(2.0f + 64.0f, 0.0f), 0.0f, 1e-6f), "...so it cannot reappear on the far side of the torus");
	// New marks in the new window work.
	ground.StampCircle({ 47.0f, 0.0f, 1.0f }, 0.6f, 1.0f);
	ground.Update(0.0f, { 45.0f, 0.0f, 0.0f });
	Check(ground.ReadDepth(47.0f, 1.0f) > 0.9f, "marks work in the new window");
	// A jump far beyond the window clears everything.
	ground.Update(0.0f, { 5000.0f, 0.0f, 5000.0f });
	Check(Near(ground.ReadDepth(47.0f, 1.0f), 0.0f, 1e-6f), "a teleport clears the map");

	// --- torus seam: a mark across the wrap boundary shows on both sides
	ground.Clear();
	ground.Update(0.0f, { 60.0f, 0.0f, 10.0f });   // window x in [28, 92]; the seam of the texture is at x = 64
	ground.StampCircle({ 64.0f, 0.0f, 10.0f }, 1.0f, 1.0f);
	ground.Update(0.0f, { 60.0f, 0.0f, 10.0f });
	Check(ground.ReadDepth(63.6f, 10.0f) > 0.2f && ground.ReadDepth(64.4f, 10.0f) > 0.2f && ground.ReadDepth(64.0f, 10.0f) > 0.9f, "a mark on the texture seam is continuous across it");

	// --- a material shows the marks: depressed, darker ground
	{
		GB::Renderer renderer(160, 90, 1, 1024);
		Check(renderer.IsValid(), "renderer initializes");
		GB::Mesh plane = GB::Mesh::CreateGrid(20.0f, 64);
		GB::Scene scene;
		GB::RenderObject floor;
		floor.MeshPtr = &plane;
		floor.Mat.Albedo = glm::vec3(0.6f);
		floor.Mat.Roughness = 0.9f;
		floor.Mat.TrailDepth = 0.1f;
		floor.Mat.TrailTint = { 0.35f, 0.35f, 0.35f };
		scene.Objects.push_back(floor);
		scene.Sun.CastShadows = false;
		scene.Env.ShadowRadius = 12.0f;

		GB::GroundInteraction marks(desc);
		marks.StampCircle({ 0.0f, 0.0f, 0.0f }, 1.5f, 1.0f);
		marks.Update(0.0f, { 0, 0, 0 });

		GB::Camera cam;
		cam.Position = { 0.0f, 6.0f, 0.5f };
		cam.Target = { 0.0f, 0.0f, 0.0f };
		auto lumAt = [&](const GB::Scene& sc, float wx, float wz)
		{
			GB::Framebuffer t(160, 90);
			renderer.Render(sc, cam, &t);
			const auto v = t.ReadPixels();
			const glm::vec4 clip = cam.GetProjection(160.0f / 90.0f) * cam.GetView() * glm::vec4(wx, 0.0f, wz, 1.0f);
			const int x = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * 160.0f);
			const int y = static_cast<int>((1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * 90.0f);
			const uint8_t* p = &v[(static_cast<size_t>(y) * 160 + x) * 4];
			return p[0] + p[1] + p[2];
		};
		const int without = lumAt(scene, 0.0f, 0.0f);   // no Ground in the scene: marks are ignored
		const int withoutAway = lumAt(scene, 5.0f, 0.0f);
		scene.Ground = &marks;
		const int marked = lumAt(scene, 0.0f, 0.0f);
		const int untouched = lumAt(scene, 5.0f, 0.0f);
		std::fprintf(stderr, "info: ground luminance without map %d, in a mark %d, away from marks %d\n", without, marked, untouched);
		Check(marked < without - 60, "the marked ground is darker (tint)");
		Check(std::abs(untouched - withoutAway) < 6, "ground away from the marks is unchanged");
	}
	return failures;
}
