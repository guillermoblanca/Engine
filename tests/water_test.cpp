// Water: the wave set (CPU mirror of the shader), the ripple simulation (drops spread, decay and follow the
// scrolling window) and how the surface renders over a sunken floor. The GL part skips without OpenGL 4.5.
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Platform/Window.h"
#include "GB/World/Water.h"
#include "GB/Renderer/WaterRipples.h"
#include "GB/Renderer/TerrainGpu.h"
#include "GB/Renderer/RocksGpu.h"
#include "GB/Renderer/TreesGpu.h"
#include "GB/World/Rocks.h"
#include "GB/World/Trees.h"
#include "GB/World/Terrain.h"
#include "GB/World/Water/LakeMesh.h"
#include "GB/World/Water/WaterPresets.h"
#include "GB/World/Graph/TerrainGraphLibrary.h"
#include "GB/World/WorldGen.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	// T135: GB_WATER_BENCH=<rounds>: the GPU cost of the water pass (GpuWaterMs) and of the whole frame (GpuTotalMs) on the editor's
	// showcase world (highland_valley_showcase, temperate, seed 5, 3 km at 1025, mesh detail 0.3), terrain and water only, 1920x1080
	// MSAA 4, in the views of the water shots: the main lake seen obliquely from ~260 m, and the river at eye height on its fastest
	// stretch. Each view is drawn `rounds` times 40 frames (10 of warm-up); reported: the median of the per-round means and the minimum
	// frame. The time runs (the shader's animation is part of the cost). GB_WATER_BENCH_INFO=1 also prints the river (points, speeds,
	// mouth) and the lakes, with an editor `camera` command for the river shot. Two builds are compared by running each one's GBGLTests
	// in turn: the shaders are compiled into the executable. GB_WATER_BENCH_OUT=<dir>: a picture of each view.
	// T138: the lakes are drawn from their mesh (BuildLakeMesh, Scene::Lakes), as the editor does once it hands it over;
	// GB_WATER_BENCH_DISCS=1 draws them as discs instead (the path of before, for comparing in one build). GB_WATER_BENCH_GRAPH=<id>
	// another graph of assets/data/terrain_graphs (e.g. highland_valley_e1); GB_WATER_BENCH_VIEWS="name,yaw,pitch,dist,x,y,z;..." more
	// views with the editor's `camera` numbers (y absolute).
	int WaterBench(int rounds)
	{
		const char* graphEnv = std::getenv("GB_WATER_BENCH_GRAPH");
		const std::string graphName = std::string(graphEnv && *graphEnv ? graphEnv : "highland_valley_showcase") + ".json";
		const std::string graphPath = (std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "data" / "terrain_graphs" / graphName).string();
		GB::TerrainGraphLibrary library;
		std::string id;
		const std::string error = library.LoadFile(graphPath, &id);
		if (!error.empty() || !library.Find(id)) { std::fprintf(stderr, "FAIL: water bench: cannot load %s (%s)\n", graphPath.c_str(), error.c_str()); return 1; }
		GB::WorldDesc desc;
		desc.Type = GB::WorldType::Temperate;
		desc.Seed = 5;
		desc.MeshDetail = 0.3f;
		desc.Size = 3000.0f;
		desc.Resolution = 1025;
		desc.TreeBudget = 40000;
		desc.RockBudget = 900;
		desc.TerrainGraphDefinition = std::make_shared<const GB::TerrainGraph>(*library.Find(id));
		GB::GeneratedWorld world = GB::WorldGenerator::GenerateData(desc);
		if (!world.TerrainPtr) { std::fprintf(stderr, "FAIL: water bench: no terrain\n"); return 1; }
		GB::Terrain& terrain = *world.TerrainPtr;
		const GB::WaterField& field = terrain.Water();
		const GB::RiverData* river = field.River();
		if (!river || river->Points.size() < 12) { std::fprintf(stderr, "FAIL: water bench: the world has no river\n"); return 1; }

		// the editor's orbit camera: yaw, pitch (degrees), distance, target
		auto orbit = [](float yawDeg, float pitchDeg, float distance, glm::vec3 target)
		{
			GB::Camera cam;
			cam.FarPlane = 4500.0f;
			const float yaw = glm::radians(yawDeg), pitch = glm::radians(pitchDeg);
			cam.Position = target + distance * glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
			cam.Target = target;
			return cam;
		};
		// the river's fastest stretch in the lowland (its last third, before the mouth), looked at from upstream at eye height over the water
		const size_t first = river->Points.size() * 2 / 3;
		size_t fast = first;
		for (size_t i = first; i + 40 < river->Points.size(); ++i)
			if (river->Speed[i] > river->Speed[fast] + 0.01f) fast = i;
		const glm::vec3 at = river->Points[fast], ahead = river->Points[std::min(fast + 3, river->Points.size() - 1)];
		glm::vec2 flow(ahead.x - at.x, ahead.z - at.z);
		flow = glm::length(flow) > 1e-4f ? glm::normalize(flow) : glm::vec2(1.0f, 0.0f);
		const glm::vec3 eyeTarget(at.x + flow.x * 6.0f, at.y + 0.3f, at.z + flow.y * 6.0f);
		const float eyeYaw = glm::degrees(std::atan2(-flow.x, -flow.y)), eyeDist = 14.0f;
		const float eyePitch = glm::degrees(std::asin(std::clamp(1.6f / eyeDist, -1.0f, 1.0f)));

		if (std::getenv("GB_WATER_BENCH_INFO"))
		{
			std::printf("river: %zu points, half width %.1f, mouth %.1f m\n", river->Points.size(), static_cast<double>(river->HalfWidth), static_cast<double>(river->MouthLength));
			const size_t stride = std::max<size_t>(1, river->Points.size() / 24);
			for (size_t i = 0; i < river->Points.size(); i += stride)
			{
				const glm::vec3& p = river->Points[i];
				std::printf("  point %zu: %.1f %.2f %.1f speed %.2f half %.1f\n", i, static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z),
					static_cast<double>(river->Speed[i]), static_cast<double>(river->HalfWidthAt(std::min(i, river->Points.size() - 2), 0.0f)));
			}
			const glm::vec3& last = river->Points.back();
			std::printf("  last point: %.1f %.2f %.1f\n", static_cast<double>(last.x), static_cast<double>(last.y), static_cast<double>(last.z));
			std::printf("fastest at point %zu (%.2f m/s): camera %.1f %.1f %.1f %.1f %.2f %.1f\n", fast, static_cast<double>(river->Speed[fast]),
				static_cast<double>(eyeYaw), static_cast<double>(eyePitch), static_cast<double>(eyeDist), static_cast<double>(eyeTarget.x), static_cast<double>(eyeTarget.y), static_cast<double>(eyeTarget.z));
			for (const GB::LakeBody* lake : field.Lakes())
				std::printf("lake at %.0f %.0f r %.0f level %.2f\n", static_cast<double>(lake->Center.x), static_cast<double>(lake->Center.y), static_cast<double>(lake->Radius), static_cast<double>(lake->Level));
		}

		std::vector<std::unique_ptr<GB::WaterSurface>> surfaces;
		if (auto lake = GB::MakeLakeSurface(field)) surfaces.push_back(std::move(lake));
		for (auto& lake : GB::MakeMoreLakeSurfaces(field)) surfaces.push_back(std::move(lake));
		if (auto r = GB::MakeRiverSurface(field)) surfaces.push_back(std::move(r));
		GB::TerrainGpu gpu(terrain);
		std::shared_ptr<const GB::LakeMesh> lakeMesh;
		if (!std::getenv("GB_WATER_BENCH_DISCS") && !field.Lakes().empty())
			lakeMesh = std::make_shared<const GB::LakeMesh>(GB::BuildLakeMesh(field, terrain.GetHeightmap()));

		constexpr uint32_t BW = 1920, BH = 1080;
		GB::Renderer renderer(BW, BH, 4, 2048);
		if (!renderer.IsValid()) { std::fprintf(stderr, "FAIL: water bench: the renderer does not compile\n"); return 1; }
		GB::Framebuffer fb(BW, BH);
		struct View { std::string Name; GB::Camera Cam; };
		std::vector<View> views = {
			{ "lake_oblique", orbit(35.0f, 30.0f, 260.0f, { -234.0f, -30.1f, 0.0f }) },
			{ "river_eye", orbit(eyeYaw, eyePitch, eyeDist, eyeTarget) },
		};
		if (const char* more = std::getenv("GB_WATER_BENCH_VIEWS"))
		{
			std::string list = more;
			size_t start = 0;
			while (start < list.size())
			{
				const size_t end = std::min(list.find(';', start), list.size());
				const std::string item = list.substr(start, end - start);
				start = end + 1;
				char name[64] = {};
				float yaw = 0.0f, pitch = 0.0f, dist = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f;
				if (std::sscanf(item.c_str(), "%63[^,],%f,%f,%f,%f,%f,%f", name, &yaw, &pitch, &dist, &x, &y, &z) == 7)
					views.push_back({ name, orbit(yaw, pitch, dist, { x, y, z }) });
			}
		}
		// the world's trees and rocks as the editor draws them (WorldInstance: the far trees beyond 70 m, none beyond 340 m)
		GB::TreesGpu treesGpu;
		std::vector<std::unique_ptr<GB::TreeGpu>> trees, treesFar;
		for (const GB::TreeParams& tp : world.TreeRecipes)
		{
			trees.push_back(treesGpu.Create(tp));
			GB::TreeParams far = tp;
			far.Detail = 0;
			treesFar.push_back(treesGpu.Create(far));
		}
		std::vector<std::unique_ptr<GB::RockGpu>> rocks;
		for (const GB::RockParams& rp : world.RockRecipes) rocks.push_back(GB::RocksGpu::Create(rp));
		std::printf("water bench: %ux%u MSAA 4, %zu surfaces, lakes %s, terrain, trees and rocks, %d rounds of 30 frames (ms)\n", BW, BH, surfaces.size(),
			lakeMesh ? ("mesh (" + std::to_string(lakeMesh->TriangleCount()) + " triangles)").c_str() : "as discs", rounds);
		float time = 0.0f;
		for (const View& view : views)
		{
			GB::Scene sc;
			gpu.AddToScene(sc);
			for (const GB::TreePlacement& tr : world.Trees)
			{
				const float d = glm::distance(tr.Position, view.Cam.Position);
				if (d > 340.0f || tr.Variant < 0 || static_cast<size_t>(tr.Variant) >= trees.size()) continue;
				(d > 70.0f ? treesFar : trees)[static_cast<size_t>(tr.Variant)]->AddToScene(sc, tr.Position, tr.Yaw, tr.Scale, 340.0f);
			}
			for (const GB::RockPlacement& rk : world.Rocks)
				if (glm::distance(rk.Position, view.Cam.Position) < 340.0f && rk.Variant >= 0 && static_cast<size_t>(rk.Variant) < rocks.size())
					rocks[static_cast<size_t>(rk.Variant)]->AddToScene(sc, rk.Position, rk.Yaw, rk.Scale, 0.12f * rk.Scale);
			for (const auto& s : surfaces) sc.Water.push_back(s.get());
			sc.Lakes = lakeMesh;
			sc.Sun.Direction = glm::normalize(glm::vec3(-0.35f, -0.8f, -0.45f));
			sc.Env.ShadowRadius = 80.0f;
			sc.Env.ShadowCenter = view.Cam.Target;
			std::vector<double> water, total, waterMin, totalMin;
			for (int r = 0; r < rounds; ++r)
			{
				double sw = 0.0, st = 0.0, lw = 1e9, lt = 1e9;
				int n = 0;
				for (int i = 0; i < 40; ++i)
				{
					time += 1.0f / 60.0f;
					sc.Env.Time = time;
					renderer.Render(sc, view.Cam, &fb);
					glFinish();
					if (i < 10) continue;
					const GB::RenderStats& s = renderer.GetStats();
					sw += s.GpuWaterMs; st += s.GpuTotalMs;
					lw = std::min(lw, static_cast<double>(s.GpuWaterMs)); lt = std::min(lt, static_cast<double>(s.GpuTotalMs));
					++n;
				}
				water.push_back(sw / n); total.push_back(st / n); waterMin.push_back(lw); totalMin.push_back(lt);
			}
			auto median = [](std::vector<double> x) { std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
			std::printf("  view %-12s water median %.3f min %.3f | total median %.3f min %.3f\n", view.Name.c_str(), median(water), *std::min_element(waterMin.begin(), waterMin.end()),
				median(total), *std::min_element(totalMin.begin(), totalMin.end()));
			if (const char* outDir = std::getenv("GB_WATER_BENCH_OUT"))
				fb.SavePNG((std::string(outDir) + "/water_bench_" + view.Name + ".png").c_str());
		}
		return 0;
	}
}

int GBTest_water(int, char**)
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
	if (const char* benchEnv = std::getenv("GB_WATER_BENCH"))
		return failures + WaterBench(std::max(1, std::atoi(benchEnv)));

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
		scene.Post.Mapper = GB::Tonemap::ACES;   // colour thresholds tuned with ACES: this checks the feature, not the tonemap (Neutral is the default)
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

		// Screen-space reflections: a red block standing on the shore is mirrored in the water.
		{
			GB::WaterSurface calm(40.0f, 64);
			calm.Params.WaveHeight = 0.0f;
			calm.Params.Choppiness = 0.0f;
			calm.Params.Foam = 0.0f;
			GB::Mesh blockMesh = GB::Mesh::CreateCube(1.0f);
			GB::Scene sc2;
			sc2.Post.Mapper = GB::Tonemap::ACES;   // colour thresholds tuned with ACES: this checks the feature, not the tonemap (Neutral is the default)
			sc2.Sun.CastShadows = false;
			GB::RenderObject ground = scene.Objects[0];
			ground.Transform = glm::translate(glm::mat4(1.0f), { 0.0f, -3.0f, 0.0f });
			sc2.Objects.push_back(ground);
			GB::RenderObject block;
			block.MeshPtr = &blockMesh;
			block.Mat.Albedo = { 0.9f, 0.05f, 0.05f };
			block.Mat.Emissive = { 4.0f, 0.0f, 0.0f };
			block.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { 0.0f, 1.5f, -7.0f }), { 3.0f, 3.0f, 3.0f });
			sc2.Objects.push_back(block);
			sc2.Water.push_back(&calm);
			GB::Camera low;
			low.Position = { 0.0f, 2.5f, 8.0f };
			low.Target = { 0.0f, 0.0f, -4.0f };
			auto redAtMirror = [&]()
			{
				GB::Framebuffer t(160, 90);
				renderer.Render(sc2, low, &t);
				if (std::getenv("GB_TEST_DUMP")) t.SavePNG(sc2.ScreenSpaceReflections ? "/tmp/ssr_on.png" : "/tmp/ssr_off.png");
				const auto v = t.ReadPixels();
				const glm::vec4 clip = low.GetProjection(160.0f / 90.0f) * low.GetView() * glm::vec4(0.0f, -0.4f, -7.0f, 1.0f);   // the image of the block just below its base
				const int x = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * 160.0f), y = static_cast<int>((1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * 90.0f);
				const uint8_t* p = &v[(static_cast<size_t>(y) * 160 + x) * 4];
				return glm::ivec3(p[0], p[1], p[2]);
			};
			sc2.ScreenSpaceReflections = true;
			const glm::ivec3 withSSR = redAtMirror();
			sc2.ScreenSpaceReflections = false;
			const glm::ivec3 without = redAtMirror();
			std::fprintf(stderr, "info: mirror of the block: SSR %d %d %d, sky only %d %d %d\n", withSSR.x, withSSR.y, withSSR.z, without.x, without.y, without.z);
			Check((withSSR.x - withSSR.y) > (without.x - without.y) + 15, "screen-space reflections show the red block in the water");
		}

		// Ripples and the radius mask render without breaking anything.
		GB::WaterRipples ripples;
		ripples.Update(0.0f, { 0, 0, 0 });
		ripples.Impulse({ 0.0f, 0.0f, 0.0f }, 0.3f, 0.05f);
		ripples.Update(0.1f, { 0, 0, 0 });
		scene.Ripples = &ripples;
		water.Radius = 8.0f;
		const glm::ivec3 rippled = center(scene);
		Check(rippled.x + rippled.y + rippled.z > 0, "a rippled, masked surface renders");
		scene.Ripples = nullptr;
		water.Radius = 0.0f;

		// T138: a lake drawn from its mesh (Scene::Lakes). A square of the grid at the surface's level over the floor 4 m down: it tints
		// the floor as the grid did, and the surface of Water that stands for the same lake (same centre and level) is not drawn on top.
		{
			scene.Objects[0].Transform = glm::translate(glm::mat4(1.0f), { 0.0f, -4.0f, 0.0f });
			auto lakes = std::make_shared<GB::LakeMesh>();
			const int n = 8;
			const float spacing = 2.5f, half = spacing * static_cast<float>(n) * 0.5f;
			for (int j = 0; j <= n; ++j)
				for (int i = 0; i <= n; ++i)
				{
					GB::LakeVertex v;
					v.Position = { -half + spacing * static_cast<float>(i), 0.0f, -half + spacing * static_cast<float>(j) };
					v.Depth = 4.0f;
					const int ring = std::min(std::min(i, j), std::min(n - i, n - j));
					v.ShoreDistance = static_cast<float>(ring) * spacing;
					lakes->Vertices.push_back(v);
				}
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
				{
					const uint32_t a = static_cast<uint32_t>(j * (n + 1) + i), b = a + 1, c = a + static_cast<uint32_t>(n + 1), d = c + 1;
					lakes->Indices.insert(lakes->Indices.end(), { a, c, b, b, c, d });
				}
			GB::WaterBodyParams body;
			body.Level = 0.0f;
			body.Look = water.Params;
			body.Radius = half;
			lakes->Bodies.push_back(body);
			lakes->Ranges.push_back({ 0, static_cast<uint32_t>(lakes->Vertices.size()), 0, static_cast<uint32_t>(lakes->Indices.size()) });

			scene.Water.clear();
			const glm::ivec3 floorOnly = center(scene);
			scene.Lakes = lakes;
			const glm::ivec3 mesh = center(scene);
			scene.Water.push_back(&water);   // the disc of the same lake: skipped
			const glm::ivec3 both = center(scene);
			std::fprintf(stderr, "info: lake mesh over 4 m of water %d %d %d (floor %d %d %d, with its disc %d %d %d)\n", mesh.x, mesh.y, mesh.z,
				floorOnly.x, floorOnly.y, floorOnly.z, both.x, both.y, both.z);
			Check(std::abs(mesh.x - floorOnly.x) + std::abs(mesh.y - floorOnly.y) + std::abs(mesh.z - floorOnly.z) > 40, "the lake mesh hides and tints the floor");
			Check(mesh.z + mesh.y > mesh.x * 2, "the lake mesh's deep water is blue-green");
			Check(both == mesh, "the disc of a lake the mesh draws is skipped");
			scene.Lakes.reset();
		}
	}
	return failures;
}
