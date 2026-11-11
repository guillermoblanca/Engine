// T115: a partition terrain (Terrain::SurfaceLayersArePartition) is drawn by its material layers, every other terrain as before.
//   - TerrainLayerTextures: the 13 kinds' maps are made at the asked size, deterministic, each albedo's mean the kind's mean colour;
//   - a small terrain whose layers are a partition of grass, cliff and path: TerrainGpu makes the layer path (splat arrays of the
//     heightmap's size, 4 layers; 13 layer maps), the material points at it and no longer draws the grain map;
//   - a terrain with only the six layers of before (rock and snow), and one without layers: no layer path, the material as before;
//   - the Renderer compiles its GB_TERRAIN_LAYERS program and draws the partition terrain: the frame is not empty, and the layered ground
//     differs from the same ground drawn the old way.
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/TerrainGpu.h"
#include "GB/Renderer/TerrainLayerTextures.h"
#include "GB/World/Terrain.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
