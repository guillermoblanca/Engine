// Water: the wave set (CPU mirror of the shader), the ripple simulation (drops spread, decay and follow the
// scrolling window) and how the surface renders over a sunken floor. The GL part skips without OpenGL 4.5.
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include "GB/World/Water.h"
#include "GB/World/WaterRipples.h"
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
}

int main()
{
	GB::Log::Init();

	// ---------------------------------------------------------------- waves (CPU)
	{
		GB::WaterSurface water(32.0f, 8);
		water.Params.WaveHeight = 0.2f;
		float lo = 1e9f, hi = -1e9f;
		for (int i = 0; i < 4000; ++i)
		{
			const float x = static_cast<float>(i) * 0.37f, z = static_cast<float>(i % 53) * 0.9f, t = static_cast<float>(i) * 0.011f;
			const float h = water.HeightAt(x, z, t);
			lo = std::min(lo, h);
			hi = std::max(hi, h);
		}
		Check(hi > 0.15f && lo < -0.15f, "waves reach about the wave height on both sides of the rest level");
		Check(hi < 0.2f * 2.01f && lo > -0.2f * 2.01f, "waves stay within the sum of the amplitudes");
		Check(std::fabs(water.HeightAt(3.0f, 4.0f, 0.0f) - water.HeightAt(3.0f, 4.0f, 2.0f)) > 1e-3f, "the surface moves over time");
		water.Params.WaveHeight = 0.0f;
		Check(water.HeightAt(3.0f, 4.0f, 1.0f) == 0.0f, "calm water is flat");
		water.Position.y = 2.5f;
		Check(std::fabs(water.SurfaceY(1.0f, 1.0f, 1.0f) - 2.5f) < 1e-6f, "SurfaceY adds the water level");
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

	{
		GB::WaterRipplesDesc desc;
		desc.Size = 32.0f;
		desc.Resolution = 256;   // 12.5 cm texels
		GB::WaterRipples ripples(desc);
		Check(ripples.IsValid(), "ripple simulation initializes");
		ripples.Update(0.0f, { 20.0f, 0.0f, 5.0f });
		ripples.Impulse({ 20.0f, 0.0f, 5.0f }, 0.3f, 0.05f);
		Check(ripples.GetPendingCount() == 1, "a drop is queued");
		ripples.Update(0.0f, { 20.0f, 0.0f, 5.0f });
		Check(ripples.GetPendingCount() == 0, "the drop is applied on update");
		const float peak = ripples.ReadHeight(20.0f, 5.0f);
		Check(peak > 0.04f && peak < 0.052f, "a drop makes a bump of about its strength");
		Check(std::fabs(ripples.ReadHeight(26.0f, 5.0f)) < 1e-4f, "far from the drop the water is still");

		// The bump spreads out as a ring and loses energy.
		float ringMax = 0.0f;
		for (int i = 0; i < 40; ++i)
		{
			ripples.Update(1.0f / 60.0f, { 20.0f, 0.0f, 5.0f });
			ringMax = std::max(ringMax, std::fabs(ripples.ReadHeight(21.5f, 5.0f)));
		}
		Check(ringMax > 0.002f, "waves travel outward from the drop");
		Check(std::fabs(ripples.ReadHeight(20.0f, 5.0f)) < peak * 0.5f, "the center settles after the ring leaves");
		for (int i = 0; i < 60 * 8; ++i)
			ripples.Update(1.0f / 60.0f, { 20.0f, 0.0f, 5.0f });
		Check(std::fabs(ripples.ReadHeight(21.5f, 5.0f)) < 0.001f && std::fabs(ripples.ReadHeight(20.0f, 5.0f)) < 0.001f, "ripples fade away");

		// Drops on the texture seam (x = 32) are continuous, and moving the window clears what scrolls in.
		ripples.Impulse({ 32.0f, 0.0f, 5.0f }, 0.4f, 0.05f);
		ripples.Update(0.0f, { 20.0f, 0.0f, 5.0f });
		Check(ripples.ReadHeight(31.8f, 5.0f) > 0.02f && ripples.ReadHeight(32.2f, 5.0f) > 0.02f, "a drop on the seam shows on both sides");
		ripples.Update(0.0f, { 60.0f, 0.0f, 5.0f });
		Check(std::fabs(ripples.ReadHeight(31.8f, 5.0f)) < 1e-4f, "moving the window away clears the old area");
	}

	// --- the surface over a sunken floor
	{
		GB::Renderer renderer(160, 90, 1, 1024);
		Check(renderer.IsValid(), "renderer initializes");
		GB::Mesh floorMesh = GB::Mesh::CreateGrid(40.0f, 32);
		GB::Scene scene;
		GB::RenderObject floor;
		floor.MeshPtr = &floorMesh;
		floor.Mat.Albedo = glm::vec3(0.55f, 0.5f, 0.35f);
		floor.Mat.Roughness = 0.9f;
		floor.Mat.HeightScale = 0.0f;
		floor.Mat.CavityStrength = 0.0f;
		floor.Transform = glm::translate(glm::mat4(1.0f), { 0.0f, -4.0f, 0.0f });
		scene.Objects.push_back(floor);
		scene.Sun.CastShadows = false;

		GB::WaterSurface water(30.0f, 64);
		water.Params.WaveHeight = 0.05f;
		water.Params.Clarity = 1.5f;
		GB::Camera cam;
		cam.Position = { 0.0f, 9.0f, 3.0f };
		cam.Target = { 0.0f, 0.0f, 0.0f };

		auto center = [&](const GB::Scene& sc)
		{
			GB::Framebuffer t(160, 90);
			renderer.Render(sc, cam, &t);
			const auto v = t.ReadPixels();
			const uint8_t* p = &v[(static_cast<size_t>(45) * 160 + 80) * 4];
			return glm::ivec3(p[0], p[1], p[2]);
		};
		const glm::ivec3 dry = center(scene);
		scene.Water.push_back(&water);
		const glm::ivec3 wet = center(scene);
		std::fprintf(stderr, "info: floor %d %d %d, under 4 m of water %d %d %d\n", dry.x, dry.y, dry.z, wet.x, wet.y, wet.z);
		Check(std::abs(wet.x - dry.x) + std::abs(wet.y - dry.y) + std::abs(wet.z - dry.z) > 40, "deep water hides and tints the floor");
		Check(wet.z + wet.y > wet.x * 2, "deep water is blue-green");

		// A shallow area: raise the floor to 0.3 m under the surface, the bottom shows through.
		scene.Objects[0].Transform = glm::translate(glm::mat4(1.0f), { 0.0f, -0.3f, 0.0f });
		const glm::ivec3 shallow = center(scene);
		std::fprintf(stderr, "info: 0.3 m of water %d %d %d\n", shallow.x, shallow.y, shallow.z);
		Check(shallow.x > wet.x + 15, "shallow water shows the bottom (warmer than deep water)");

		// Ripples and the radius mask render without breaking anything.
		GB::WaterRipples ripples;
		ripples.Update(0.0f, { 0, 0, 0 });
		ripples.Impulse({ 0.0f, 0.0f, 0.0f }, 0.3f, 0.05f);
		ripples.Update(0.1f, { 0, 0, 0 });
		scene.Ripples = &ripples;
		water.Radius = 8.0f;
		const glm::ivec3 rippled = center(scene);
		Check(rippled.x + rippled.y + rippled.z > 0, "a rippled, masked surface renders");
	}
	return failures;
}
