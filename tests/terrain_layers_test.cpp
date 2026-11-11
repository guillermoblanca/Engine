// T115: a partition terrain (Terrain::SurfaceLayersArePartition) is drawn by its material layers, every other terrain as before.
//   - TerrainLayerTextures: the 13 kinds' maps are made at the asked size, deterministic, each albedo's mean the kind's mean colour;
//   - a small terrain whose layers are a partition of grass, cliff and path: TerrainGpu makes the layer path (splat arrays of the
//     heightmap's size, 4 layers; 13 layer maps), the material points at it and no longer draws the grain map;
//   - a terrain with only the six layers of before (rock and snow), and one without layers: no layer path, the material as before;
//   - the Renderer compiles its GB_TERRAIN_LAYERS program and draws the partition terrain: the frame is not empty, and the layered ground
//     differs from the same ground drawn the old way.
// T132: GB_TERRAIN_BENCH=<rounds> measures the layers' GPU cost on the editor's showcase world (see TerrainBench); GB_TERRAIN_BENCH_INFO=1|2
// prints what lies under the lakes and where the snow is partial (2: only that), GB_TERRAIN_BENCH_PROBE=x0,z0,x1,z1 the weights along a line.
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/TerrainGpu.h"
#include "GB/Renderer/TerrainLayerTextures.h"
#include "GB/Renderer/RocksGpu.h"
#include "GB/Renderer/TreesGpu.h"
#include "GB/World/Rocks.h"
#include "GB/World/Trees.h"
#include "GB/World/Terrain.h"
#include "GB/World/Graph/TerrainGraphLibrary.h"
#include "GB/World/WorldGen.h"
#include <glad/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	constexpr uint32_t kN = 65;   // 1 m per sample: 64 m, centred on the origin

	// A gentle slope with a 6 m wall across it at x > 12.
	GB::Heightmap Ground()
	{
		GB::Heightmap hm(kN, 64.0f);
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i)
			{
				const glm::vec2 p = hm.SamplePosition(i, j);
				hm.At(i, j) = 0.05f * p.y + (p.x > 12.0f ? std::min((p.x - 12.0f) * 2.5f, 6.0f) : 0.0f);
			}
		return hm;
	}

	std::unique_ptr<GB::Terrain> MakeTerrain(bool partition, bool layers)
	{
		auto terrain = GB::Terrain::CreateFromHeightmap(Ground(), GB::HillParams{}, GB::TerrainStyle::Meadow);
		if (!layers) return terrain;
		const GB::Heightmap& hm = terrain->GetHeightmap();
		const size_t count = static_cast<size_t>(kN) * kN;
		std::vector<GB::SurfaceLayer> list;
		if (partition)
		{
			// grass, a cliff on the wall and a path along z = 0 (sum 1 everywhere)
			GB::SurfaceLayer grass{ GB::SurfaceLayerKind::Grass, std::vector<float>(count) }, cliff{ GB::SurfaceLayerKind::Cliff, std::vector<float>(count) },
				path{ GB::SurfaceLayerKind::Path, std::vector<float>(count) };
			for (uint32_t j = 0; j < kN; ++j)
				for (uint32_t i = 0; i < kN; ++i)
				{
					const glm::vec2 p = hm.SamplePosition(i, j);
					const size_t c = static_cast<size_t>(j) * kN + i;
					cliff.Weights[c] = p.x > 12.0f && p.x < 15.0f ? 1.0f : 0.0f;
					path.Weights[c] = (1.0f - cliff.Weights[c]) * std::max(0.0f, 1.0f - std::fabs(p.y) / 2.0f);
					grass.Weights[c] = 1.0f - cliff.Weights[c] - path.Weights[c];
				}
			list = { grass, cliff, path };
		}
		else
		{
			GB::SurfaceLayer rock{ GB::SurfaceLayerKind::Rock, std::vector<float>(count, 0.0f) }, snow{ GB::SurfaceLayerKind::Snow, std::vector<float>(count, 0.0f) };
			for (size_t c = 0; c < count; ++c) { rock.Weights[c] = c % kN > kN / 2 ? 1.0f : 0.0f; snow.Weights[c] = c / kN > kN / 2 ? 0.5f : 0.0f; }
			list = { rock, snow };
		}
		terrain->SetSurfaceLayers(std::move(list));
		return terrain;
	}

	// T132: GB_TERRAIN_BENCH=<rounds>: the GPU cost of the layered terrain against the same terrain drawn without its layers (as before T115),
	// in one process (the same clocks for both). The world is the editor's showcase (highland_valley_showcase, temperate, seed 5, 3 km at
	// 1025, mesh detail 0.3; GB_TERRAIN_BENCH_GRAPH: another graph file), without trees or rocks, so the objects pass is the terrain and the
	// sky; the editor's eye-height shots, 1920x1080, MSAA 4. The variants take turns, `rounds` times, 40 frames each (10 of warm-up); reported:
	// the median of the per-round means and the minimum frame of the objects pass (GpuSceneMs). GB_TERRAIN_BENCH_OUT=<dir>: a picture of
	// each view, drawn with the layers.
	int TerrainBench(int rounds)
	{
		std::string graphPath;
		if (const char* g = std::getenv("GB_TERRAIN_BENCH_GRAPH")) graphPath = g;
		else graphPath = (std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "data" / "terrain_graphs" / "highland_valley_showcase.json").string();
		GB::TerrainGraphLibrary library;
		std::string id;
		const std::string error = library.LoadFile(graphPath, &id);
		if (!error.empty() || !library.Find(id)) { std::fprintf(stderr, "FAIL: terrain bench: cannot load %s (%s)\n", graphPath.c_str(), error.c_str()); return 1; }
		GB::WorldDesc desc;
		desc.Type = GB::WorldType::Temperate;
		desc.Seed = 5;
		desc.MeshDetail = 0.3f;
		desc.Size = 3000.0f;
		desc.Resolution = 1025;
		desc.TreeBudget = 40000;
		desc.RockBudget = 900;
		desc.TerrainGraphDefinition = std::make_shared<const GB::TerrainGraph>(*library.Find(id));
		const auto genStart = std::chrono::steady_clock::now();
		GB::GeneratedWorld world = GB::WorldGenerator::GenerateData(desc);
		if (!world.TerrainPtr) { std::fprintf(stderr, "FAIL: terrain bench: no terrain\n"); return 1; }
		GB::Terrain& terrain = *world.TerrainPtr;
		if (std::getenv("GB_TERRAIN_BENCH_INFO"))
		{
			// what lies under the lakes, and where the snow is partial (for the shots)
			const GB::Heightmap& hm = terrain.GetHeightmap();
			const int n = static_cast<int>(hm.GetResolution());
			const std::vector<GB::SurfaceLayer>& layers = terrain.GetSurfaceLayers();
			auto dominant = [&](size_t c) { int best = -1; float bw = -1.0f; for (const GB::SurfaceLayer& l : layers) if (l.Weights[c] > bw) { bw = l.Weights[c]; best = static_cast<int>(l.Kind); } return best; };
			for (const GB::LakeBody* lake : terrain.Water().Lakes())
			{
				double mean[16] = {};
				int dom[16] = {}, count = 0, isolated = 0;
				for (int j = 1; j < n - 1; ++j)
					for (int i = 1; i < n - 1; ++i)
					{
						const glm::vec2 p = hm.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
						if (glm::length(p - lake->Center) > lake->Radius * 1.6f || hm.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)) > lake->Level - 0.3f) continue;
						if (terrain.Water().LakeLevelAt(p.x, p.y) < lake->Level - 0.01f) continue;
						const size_t c = static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i);
						for (const GB::SurfaceLayer& l : layers) mean[static_cast<int>(l.Kind)] += l.Weights[c];
						const int d = dominant(c);
						++dom[d]; ++count;
						const int around[4] = { dominant(c - 1), dominant(c + 1), dominant(c - static_cast<size_t>(n)), dominant(c + static_cast<size_t>(n)) };
						if (around[0] != d && around[1] != d && around[2] != d && around[3] != d) ++isolated;
					}
				std::printf("lake at %.0f %.0f r %.0f level %.2f: %d samples under water, %d isolated\n", static_cast<double>(lake->Center.x), static_cast<double>(lake->Center.y),
					static_cast<double>(lake->Radius), static_cast<double>(lake->Level), count, isolated);
				for (int k = 0; k < 13; ++k)
					if (mean[k] > 0.0 || dom[k] > 0)
						std::printf("   %-10s mean weight %.3f, dominant in %.1f%%\n", GB::TerrainLayerTextures::Looks()[static_cast<size_t>(k)].Name, mean[k] / std::max(count, 1), 100.0 * dom[k] / std::max(count, 1));
			}
			const GB::SurfaceLayer* snow = nullptr;
			for (const GB::SurfaceLayer& l : layers) if (l.Kind == GB::SurfaceLayerKind::Snow) snow = &l;
			if (snow)
			{
				const int block = std::max(1, static_cast<int>(100.0f / hm.GetSpacing()));
				struct Block { int Partial; glm::vec3 At; };
				std::vector<Block> blocks;
				for (int bj = 0; bj + block <= n; bj += block)
					for (int bi = 0; bi + block <= n; bi += block)
					{
						int partial = 0;
						glm::vec3 sum(0.0f);
						for (int j = bj; j < bj + block; ++j)
							for (int i = bi; i < bi + block; ++i)
							{
								const float w = snow->Weights[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)];
								if (w < 0.15f || w > 0.85f) continue;
								++partial;
								const glm::vec2 p = hm.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
								sum += glm::vec3(p.x, hm.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)), p.y);
							}
						if (partial > 0) blocks.push_back({ partial, sum / static_cast<float>(partial) });
					}
				std::sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) { return a.Partial > b.Partial; });
				for (size_t b = 0; b < std::min<size_t>(6, blocks.size()); ++b)
					std::printf("partial snow: %d samples around %.0f %.1f %.0f\n", blocks[b].Partial, static_cast<double>(blocks[b].At.x), static_cast<double>(blocks[b].At.y), static_cast<double>(blocks[b].At.z));
			}
			// where the gravel is (the 280-340 m fade between its maps and the macro map)
			for (const GB::SurfaceLayer& l : layers)
			{
				if (l.Kind != GB::SurfaceLayerKind::Gravel) continue;
				const int block = std::max(1, static_cast<int>(100.0f / hm.GetSpacing()));
				int best = 0;
				glm::vec3 at(0.0f);
				for (int bj = 0; bj + block <= n; bj += block)
					for (int bi = 0; bi + block <= n; bi += block)
					{
						int count = 0;
						glm::vec3 sum(0.0f);
						for (int j = bj; j < bj + block; ++j)
							for (int i = bi; i < bi + block; ++i)
								if (l.Weights[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)] > 0.6f)
								{
									++count;
									const glm::vec2 p = hm.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
									sum += glm::vec3(p.x, hm.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)), p.y);
								}
						if (count > best) { best = count; at = sum / static_cast<float>(count); }
					}
				std::printf("gravel: %d samples over 0.6 around %.0f %.1f %.0f\n", best, static_cast<double>(at.x), static_cast<double>(at.y), static_cast<double>(at.z));
			}
			// the layers' weights along a line of points (GB_TERRAIN_BENCH_INFO=2 also stops here)
			if (const char* probe = std::getenv("GB_TERRAIN_BENCH_PROBE"))
			{
				float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
				if (std::sscanf(probe, "%f,%f,%f,%f", &x0, &z0, &x1, &z1) == 4)
					for (int s = 0; s <= 8; ++s)
					{
						const float x = x0 + (x1 - x0) * static_cast<float>(s) / 8.0f, z = z0 + (z1 - z0) * static_cast<float>(s) / 8.0f;
						std::printf("probe %.0f %.0f (h %.1f):", static_cast<double>(x), static_cast<double>(z), static_cast<double>(terrain.GetHeight(x, z)));
						for (const GB::SurfaceLayer& l : layers)
						{
							const float w = terrain.SurfaceLayerWeight(l.Kind, x, z);
							if (w > 0.02f) std::printf(" %s %.2f", GB::TerrainLayerTextures::Looks()[static_cast<size_t>(l.Kind)].Name, static_cast<double>(w));
						}
						std::printf("\n");
					}
			}
			if (std::atoi(std::getenv("GB_TERRAIN_BENCH_INFO")) == 2) return 0;
		}
		GB::TerrainGpu gpu(terrain);
		if (!gpu.HasLayerMaterials()) { std::fprintf(stderr, "FAIL: terrain bench: the world is not drawn by layers\n"); return 1; }
		std::printf("terrain bench: %s built in %.1f s\n", id.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - genStart).count());

		constexpr uint32_t BW = 1920, BH = 1080;
		GB::Renderer renderer(BW, BH, 4, 2048);
		if (!renderer.IsValid()) { std::fprintf(stderr, "FAIL: terrain bench: the renderer does not compile\n"); return 1; }
		GB::Framebuffer fb(BW, BH);
		// the editor's orbit shots: yaw, pitch (degrees), distance, target; y below -1e8: the ground there plus 2 m
		struct View { const char* Name; float Yaw, Pitch, Distance; glm::vec3 Target; };
		const View views[] = {
			{ "path_eye", 94.8f, 3.2f, 35.3f, { -254.9f, -24.57f, 196.3f } },
			{ "shore_eye", 270.0f, 4.0f, 90.0f, { -234.0f, -1e9f, -200.0f } },
			{ "cliff_scree_eye", 101.8f, -28.3f, 95.2f, { 497.9f, 151.82f, 1222.0f } },
			{ "cliff_scree", 101.8f, 12.0f, 220.0f, { 520.0f, -1e9f, 1218.0f } },
		};
		auto cameraOf = [&](const View& v)
		{
			GB::Camera cam;
			cam.FarPlane = 1500.0f;
			glm::vec3 target = v.Target;
			if (target.y < -1e8f) target.y = terrain.GetHeight(target.x, target.z) + 2.0f;
			float pitch = glm::radians(v.Pitch);
			const float yaw = glm::radians(v.Yaw);
			auto at = [&](float p) { return target + v.Distance * glm::vec3(std::sin(yaw) * std::cos(p), std::sin(p), std::cos(yaw) * std::cos(p)); };
			for (int i = 0; i < 3; ++i)   // (the editor's orbit keeps the camera above the ground)
			{
				const glm::vec3 p = at(pitch);
				const float floor = terrain.GetHeight(p.x, p.z) + 0.5f;
				if (p.y >= floor) break;
				pitch = std::max(pitch, std::asin(std::clamp((floor - target.y) / v.Distance, -1.0f, 1.0f)));
			}
			cam.Position = at(pitch);
			cam.Target = target;
			return cam;
		};
		struct Variant { const char* Name; std::function<GB::Material()> Make; };
		// the layers with some of their flags cleared (uLayerParams.y: 2 broken tiles, 4 no surface map), to see what each costs
		auto withoutFlag = [&](int flag)
		{
			auto maps = std::make_shared<GB::TerrainLayerMaps>(*gpu.GetMaterial().TerrainLayers);
			for (glm::vec4& p : maps->Params) p.y = static_cast<float>(static_cast<int>(p.y + 0.5f) & ~flag);
			return maps;
		};
		const std::shared_ptr<GB::TerrainLayerMaps> noBroken = withoutFlag(2), noFlat = withoutFlag(4);
		auto layersWith = [&](const GB::TerrainLayerMaps* maps) { GB::Material m = gpu.GetMaterial(); m.TerrainLayers = maps; return m; };
		std::vector<Variant> variants = {
			{ "plain (before T115)", [&] { return gpu.GetPlainMaterial(); } },
			{ "layers", [&] { return gpu.GetMaterial(); } },
			{ "layers, tiles not broken", [&] { return layersWith(noBroken.get()); } },
			{ "layers, all surface maps", [&] { return layersWith(noFlat.get()); } },
		};
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
		auto sceneOf = [&](const GB::Material& m, const GB::Camera& cam, bool props)
		{
			GB::Scene sc;
			gpu.AddToScene(sc);
			for (GB::RenderObject& o : sc.Objects) o.Mat = m;
			if (props)
			{
				for (const GB::TreePlacement& t : world.Trees)
				{
					const float d = glm::distance(t.Position, cam.Position);
					if (d > 340.0f || t.Variant < 0 || static_cast<size_t>(t.Variant) >= trees.size()) continue;
					(d > 70.0f ? treesFar : trees)[static_cast<size_t>(t.Variant)]->AddToScene(sc, t.Position, t.Yaw, t.Scale, 340.0f);
				}
				for (const GB::RockPlacement& r : world.Rocks)
					if (glm::distance(r.Position, cam.Position) < 340.0f && r.Variant >= 0 && static_cast<size_t>(r.Variant) < rocks.size())
						rocks[static_cast<size_t>(r.Variant)]->AddToScene(sc, r.Position, r.Yaw, r.Scale, 0.12f * r.Scale);
			}
			sc.Sun.Direction = glm::normalize(glm::vec3(-0.35f, -0.8f, -0.45f));
			sc.Env.ShadowCenter = cam.Target;
			sc.Env.ShadowRadius = 80.0f;
			return sc;
		};
		const char* outDir = std::getenv("GB_TERRAIN_BENCH_OUT");
		std::printf("terrain bench: %ux%u MSAA 4, %d rounds of 30 frames (objects pass, ms); 'scene': with the trees and rocks\n", BW, BH, rounds);
		for (const View& view : views)
		{
			const GB::Camera cam = cameraOf(view);
			const size_t kinds = variants.size();
			std::vector<std::vector<double>> means(2 * kinds), mins(2 * kinds);
			for (int r = 0; r < rounds; ++r)
				for (size_t v = 0; v < 2 * kinds; ++v)
				{
					const GB::Scene sc = sceneOf(variants[v % kinds].Make(), cam, v >= kinds);
					double sum = 0.0, lo = 1e9;
					int n = 0;
					for (int i = 0; i < 40; ++i)
					{
						renderer.Render(sc, cam, &fb);
						glFinish();
						if (i < 10) continue;
						const double ms = renderer.GetStats().GpuSceneMs;
						sum += ms; lo = std::min(lo, ms); ++n;
					}
					means[v].push_back(sum / n);
					mins[v].push_back(lo);
				}
			auto median = [](std::vector<double> x) { std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
			std::printf("  view %s (camera %.1f %.1f %.1f)\n", view.Name, static_cast<double>(cam.Position.x), static_cast<double>(cam.Position.y), static_cast<double>(cam.Position.z));
			for (size_t v = 0; v < 2 * kinds; ++v)
			{
				const size_t b = v < kinds ? 0 : kinds;
				const double base = median(means[b]), baseMin = *std::min_element(mins[b].begin(), mins[b].end());
				const double m = median(means[v]), lo = *std::min_element(mins[v].begin(), mins[v].end());
				std::printf("    %-6s %-24s median of means %.3f (%+.1f%%), min %.3f (%+.1f%%)\n", v < kinds ? "ground" : "scene", variants[v % kinds].Name, m, 100.0 * (m / base - 1.0), lo, 100.0 * (lo / baseMin - 1.0));
			}
			if (outDir)
			{
				for (size_t v = 0; v < kinds; ++v)
				{
					renderer.Render(sceneOf(variants[v].Make(), cam, true), cam, &fb);
					const std::vector<uint8_t> px = fb.ReadPixels();
					const std::string path = std::string(outDir) + "/bench_" + view.Name + "_" + std::to_string(v) + ".png";
					GB::SavePNG(path.c_str(), BW, BH, px.data());
				}
			}
		}
		return 0;
	}
}

int GBTest_terrain_layers(int argc, char** argv)
{
	GB::Log::Init();

	// `GBGLTests terrain_layers --out <dir>`: a contact sheet of the 13 kinds' albedo (each tile shown 2 x 2, to see it tile) and their
	// shaded relief, to look at, then the checks.
	if (argc >= 3 && std::string(argv[1]) == "--out")
	{
		constexpr uint32_t S = 256, cols = 5, rows = 3;
		const GB::TerrainLayerTextures::Maps maps = GB::TerrainLayerTextures::Generate(S);
		for (int sheet = 0; sheet < 2; ++sheet)
		{
			const uint32_t W = cols * 2 * S, H = rows * 2 * S;
			std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 40);
			for (size_t k = 0; k < maps.Albedo.size(); ++k)
			{
				const uint32_t ox = static_cast<uint32_t>(k % cols) * 2 * S, oy = static_cast<uint32_t>(k / cols) * 2 * S;
				for (uint32_t y = 0; y < 2 * S; ++y)
					for (uint32_t x = 0; x < 2 * S; ++x)
					{
						const size_t src = (static_cast<size_t>(y % S) * S + x % S) * 4, dst = (static_cast<size_t>(oy + y) * W + ox + x) * 4;
						if (sheet == 0)
							for (int c = 0; c < 3; ++c) px[dst + static_cast<size_t>(c)] = maps.Albedo[k].Pixels[src + static_cast<size_t>(c)];
						else
						{
							// the normal lit from the upper left, times the occlusion
							const float nx = maps.Surface[k].Pixels[src] / 127.5f - 1.0f, ny = maps.Surface[k].Pixels[src + 1] / 127.5f - 1.0f;
							const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
							const float lit = std::clamp(0.25f + 0.75f * (-0.5f * nx - 0.5f * ny + 0.7f * nz), 0.0f, 1.0f) * maps.Surface[k].Pixels[src + 3] / 255.0f;
							for (int c = 0; c < 3; ++c) px[dst + static_cast<size_t>(c)] = static_cast<uint8_t>(lit * 255.0f);
						}
						px[dst + 3] = 255;
					}
			}
			const std::string path = std::string(argv[2]) + (sheet == 0 ? "/terrain_layers_albedo.png" : "/terrain_layers_relief.png");
			GB::SavePNG(path.c_str(), W, H, px.data());
			std::printf("wrote %s\n", path.c_str());
		}
		return 0;
	}

	// ---------------------------------------------------------------- the layers' maps (CPU)
	{
		const GB::TerrainLayerTextures::Maps a = GB::TerrainLayerTextures::Generate(64), b = GB::TerrainLayerTextures::Generate(64);
		Check(a.Albedo.size() == 13 && a.Surface.size() == 13, "a map of each of the 13 kinds");
		bool valid = true, same = true;
		for (size_t k = 0; k < a.Albedo.size(); ++k)
		{
			valid = valid && a.Albedo[k].IsValid() && a.Albedo[k].Width == 64 && a.Surface[k].IsValid() && a.Surface[k].Width == 64;
			same = same && a.Albedo[k].Pixels == b.Albedo[k].Pixels && a.Surface[k].Pixels == b.Surface[k].Pixels;
		}
		Check(valid, "every map is a valid 64 x 64 image");
		Check(same, "the maps are deterministic");
		bool distinct = true;
		for (size_t k = 1; k < a.Albedo.size(); ++k) distinct = distinct && a.Albedo[k].Pixels != a.Albedo[k - 1].Pixels;
		Check(distinct, "each kind has its own map");
		// the mean colour (linear) near the table's, so the far ground keeps the macro map's colours
		auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
		bool means = true;
		for (size_t k = 0; k < a.Albedo.size(); ++k)
		{
			const glm::vec3 target = GB::TerrainLayerTextures::Looks()[k].MeanColor;
			for (int ch = 0; ch < 3; ++ch)
				means = means && std::fabs(a.MeanLinear[k][ch] - lin(target[ch])) < 0.03f;
		}
		Check(means, "each map's mean colour is its kind's");
	}

	// ---------------------------------------------------------------- the layer path (GL)
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP (GL part): no OpenGL 4.5 context\n");
		return failures ? failures : 77;
	}
	if (const char* benchEnv = std::getenv("GB_TERRAIN_BENCH"))
		return failures + TerrainBench(std::max(1, std::atoi(benchEnv)));

	auto layered = MakeTerrain(true, true);
	Check(layered->SurfaceLayersArePartition(), "grass, cliff and path are a partition");
	GB::TerrainGpu layeredGpu(*layered);
	Check(layeredGpu.HasLayerMaterials(), "a partition terrain is drawn by its material layers");
	const GB::TerrainLayerMaps* maps = layeredGpu.GetMaterial().TerrainLayers;
	Check(maps && maps->Splat && maps->Splat->IsValid() && maps->Splat->GetLayers() == 4 && maps->Splat->GetWidth() == kN && maps->Splat->GetHeight() == kN,
		"the splat weights: 4 array layers at the heightmap's resolution");
	Check(maps && maps->Albedo && maps->Albedo->GetLayers() == 13 && maps->Surface && maps->Surface->GetLayers() == 13 && maps->Albedo->GetWidth() == 512,
		"the layers' maps: 13 kinds at 512");
	Check(maps && std::fabs(maps->SplatRect.z - 1.0f) < 1e-4f && std::fabs(maps->SplatRect.w - static_cast<float>(kN)) < 1e-4f, "the splat rect: 1 m spacing, 65 samples");
	Check(maps && maps->Params[static_cast<int>(GB::SurfaceLayerKind::Cliff)].y > 0.5f && maps->Params[static_cast<int>(GB::SurfaceLayerKind::Path)].y < 0.5f,
		"the cliff is projected along the axes, the path from above");
	Check(layeredGpu.GetMaterial().AlbedoMap == nullptr && layeredGpu.GetMaterial().MacroMap != nullptr, "no grain map under the layers; the macro map stays");

	auto oldLayers = MakeTerrain(false, true);
	Check(!oldLayers->SurfaceLayersArePartition(), "rock and snow alone are not a partition");
	GB::TerrainGpu oldGpu(*oldLayers);
	Check(!oldGpu.HasLayerMaterials() && oldGpu.GetMaterial().TerrainLayers == nullptr && oldGpu.GetMaterial().AlbedoMap != nullptr,
		"a terrain with the six layers of before is drawn as before");

	auto plain = MakeTerrain(false, false);
	GB::TerrainGpu plainGpu(*plain);
	Check(!plainGpu.HasLayerMaterials() && plainGpu.GetMaterial().TerrainLayers == nullptr, "a terrain without layers is drawn as before");

	// ---------------------------------------------------------------- drawn
	constexpr uint32_t W = 160, H = 120;
	GB::Renderer renderer(W, H, 1, 1024);
	Check(renderer.IsValid(), "the renderer (and its GB_TERRAIN_LAYERS program) compiles");
	GB::Framebuffer fb(W, H);
	GB::Camera cam;
	cam.Position = { -6.0f, 9.0f, 18.0f };
	cam.Target = { 6.0f, 0.0f, -4.0f };
	auto shoot = [&](const GB::TerrainGpu& gpu)
	{
		GB::Scene scene;
		scene.Sun.Direction = { -0.4f, -0.7f, -0.5f };
		gpu.AddToScene(scene);
		renderer.Render(scene, cam, &fb);
		return fb.ReadPixels();
	};
	const std::vector<uint8_t> withLayers = shoot(layeredGpu);
	// the same ground and layers drawn the old way: a terrain built from the same data, its layer path not made (rock and snow only)
	const std::vector<uint8_t> before = shoot(oldGpu);
	Check(withLayers.size() == static_cast<size_t>(W) * H * 4, "the frame was read back");
	double sum = 0.0, diff = 0.0;
	for (size_t i = 0; i + 3 < withLayers.size(); i += 4)
		for (int c = 0; c < 3; ++c)
		{
			sum += withLayers[i + static_cast<size_t>(c)];
			diff += std::abs(static_cast<int>(withLayers[i + static_cast<size_t>(c)]) - static_cast<int>(before[i + static_cast<size_t>(c)]));
		}
	const double pixels = static_cast<double>(W) * H * 3.0;
	Check(sum / pixels > 10.0, "the layered terrain is drawn (the frame is not black)");
	Check(diff / pixels > 2.0, "the layered ground is not the old one");
	std::printf("terrain_layers: mean %.1f, mean difference from the old look %.1f (of 255)\n", sum / pixels, diff / pixels);

	if (failures == 0) std::printf("terrain_layers: all checks passed\n");
	return failures;
}
