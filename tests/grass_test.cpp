// Grass: CPU generation (determinism, density, slope and mask rules, chunks) and GPU behaviour
// (blades render, bend away under ground marks). The GL part skips without OpenGL 4.5.
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include "GB/World/Grass.h"
#include "GB/World/GroundInteraction.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
}

int main()
{
	GB::Log::Init();

	// ---------------------------------------------------------------- generation (CPU)
	GB::GrassParams p;
	p.Seed = 5;
	p.Size = 48.0f;
	p.Density = 10.0f;
	p.ChunkSize = 12.0f;
	GB::GrassField field(p);
	const float expected = p.Size * p.Size * p.Density;
	Check(Near(static_cast<float>(field.GetBladeCount()), expected, expected * 0.02f), "blade count = area * density");
	Check(field.GetChunks().size() == 16, "4 x 4 chunks of 12 m");

	GB::GrassField again(p);
	bool same = again.GetBladeCount() == field.GetBladeCount();
	for (size_t i = 0; same && i < field.GetBladeCount(); i += 37)
		same = field.GetBlades()[i].PositionYaw == again.GetBlades()[i].PositionYaw && field.GetBlades()[i].Shape == again.GetBlades()[i].Shape;
	Check(same, "generation is deterministic");
	GB::GrassParams other = p;
	other.Seed = 6;
	GB::GrassField different(other);
	Check(different.GetBlades()[0].PositionYaw != field.GetBlades()[0].PositionYaw, "another seed, another meadow");

	bool inside = true, ranges = true;
	for (const GB::GrassBlade& b : field.GetBlades())
	{
		inside = inside && b.PositionYaw.x >= -24.0f && b.PositionYaw.x <= 24.0f && b.PositionYaw.z >= -24.0f && b.PositionYaw.z <= 24.0f;
		ranges = ranges && b.Shape.x >= p.Height.x && b.Shape.x <= p.Height.y && b.Shape.z >= 0.0f && b.Shape.z <= 1.0f;
	}
	Check(inside, "blades stay inside the field");
	Check(ranges, "heights are within the configured range");

	// Ground height and masks.
	GB::GrassParams shaped = p;
	shaped.HeightFn = [](float x, float z) { return 0.5f * x + 2.0f; };
	shaped.DensityFn = [](float x, float) { return x < 0.0f ? 0.0f : 1.0f; };     // bare on the left half
	shaped.NormalFn = [](float, float z) { return z > 12.0f ? glm::vec3(0.9f, 0.4f, 0.0f) : glm::vec3(0, 1, 0); };   // too steep for z > 12
	GB::GrassField masked(shaped);
	bool grounded = true, rightOnly = true, notSteep = true;
	for (const GB::GrassBlade& b : masked.GetBlades())
	{
		grounded = grounded && Near(b.PositionYaw.y, 0.5f * b.PositionYaw.x + 2.0f, 1e-4f);
		rightOnly = rightOnly && b.PositionYaw.x >= 0.0f;
		notSteep = notSteep && b.PositionYaw.z <= 12.0f;
	}
	Check(grounded, "blades are rooted on the ground height function");
	Check(rightOnly, "the density mask removes grass");
	Check(notSteep, "slopes beyond MaxSlopeDegrees get no grass");
	const float kept = static_cast<float>(masked.GetBladeCount());
	Check(kept > 0.2f * expected && kept < 0.4f * expected, "roughly a quarter of the field remains (half the width, three quarters of the depth)");

	// Culling by chunk.
	Check(field.CountVisibleBlades({ 0.0f, 1.0f, 0.0f }) < field.GetBladeCount() + 1 && field.CountVisibleBlades({ 0.0f, 1.0f, 0.0f }) > 0, "chunks near the camera are counted");
	GB::GrassParams near = p;
	near.DrawDistance = 8.0f;
	GB::GrassField short1(near);
	Check(short1.CountVisibleBlades({ 0.0f, 1.0f, 0.0f }) < short1.GetBladeCount() / 2, "a short draw distance keeps the far chunks out");
	Check(short1.CountVisibleBlades({ 500.0f, 1.0f, 500.0f }) == 0, "far from the field nothing is drawn");

	// ---------------------------------------------------------------- GPU
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP (GL part): no OpenGL 4.5 context\n");
		return failures ? failures : 77;
	}

	GB::Renderer renderer(160, 90, 4, 1024);
	Check(renderer.IsValid(), "renderer (with the grass shader) initializes");

	GB::GrassParams gp;
	gp.Seed = 9;
	gp.Size = 6.0f;           // a small patch, so everything seen below can be under the stamped marks
	gp.ChunkSize = 6.0f;
	gp.Density = 160.0f;
	gp.Height = { 0.45f, 0.5f };
	gp.DryAmount = 0.0f;
	gp.DrawDistance = 40.0f;
	GB::GrassField meadow(gp);
	Check(meadow.GetBuffer() != 0, "blades upload to the GPU");

	GB::Scene scene;
	scene.Grass.push_back(&meadow);
	scene.Sun.CastShadows = false;
	scene.Env.ShadowRadius = 10.0f;
	scene.Env.Wind = { 0.0f, 0.0f, 0.0f };

	// Side-on view along the ground: tall grass fills the band above the soil, flattened grass does not.
	GB::Camera cam;
	cam.Position = { 0.0f, 0.25f, 7.0f };
	cam.Target = { 0.0f, 0.25f, 0.0f };
	cam.FovYDegrees = 40.0f;
	auto greenInBand = [&](const GB::Scene& sc)
	{
		GB::Framebuffer t(160, 90);
		renderer.Render(sc, cam, &t);
		const auto px = t.ReadPixels();
		int green = 0;
		for (int y = 0; y < 90; ++y)
			for (int x = 60; x < 100; ++x)   // the middle columns
			{
				const uint8_t* p = &px[(static_cast<size_t>(y) * 160 + x) * 4];
				if (p[1] > p[0] + 20 && p[1] > p[2] + 20)
					++green;
			}
		return green;
	};
	const int standing = greenInBand(scene);
	std::fprintf(stderr, "info: green pixels standing=%d\n", standing);
	Check(standing > 300, "a meadow of tall grass is visible");

	GB::GroundInteractionDesc gd;
	gd.Size = 32.0f;
	gd.Resolution = 512;
	gd.RecoveryPerSecond = 0.0f;
	GB::GroundInteraction marks(gd);
	marks.StampCircle({ 0.0f, 0.0f, 0.0f }, 6.0f, 1.0f);   // trampled patch covering the whole field
	marks.Update(0.0f, { 0, 0, 0 });
	scene.Ground = &marks;
	const int trampled = greenInBand(scene);
	std::fprintf(stderr, "info: green pixels trampled=%d\n", trampled);
	Check(trampled < standing * 0.7, "grass flattens where the ground map has marks");

	scene.Ground = nullptr;
	scene.Env.SnowCover = 1.0f;
	const int snowy = greenInBand(scene);
	std::fprintf(stderr, "info: green pixels snowy=%d\n", snowy);
	Check(snowy < standing * 0.6, "snow buries the grass (shorter and whiter)");
	return failures;
}
