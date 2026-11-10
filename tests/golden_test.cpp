// Golden-image tests: canonical scenes rendered headless and compared with the reference pictures in
// tests/golden/images. Update after an intended visual change: GB_UPDATE_GOLDEN=1 ctest -R golden_images
// (then look at the pictures and commit them). Failures leave expected | actual | difference in ./qa_artifacts.
#include "GB/Effects/Sky.h"
#include "GB/Log.h"
#include "GB/Qa/Qa.h"
#include "GB/Renderer/GrassGpu.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/RocksGpu.h"
#include "GB/Renderer/TerrainGpu.h"
#include "GB/Renderer/TreesGpu.h"
#include "GB/Platform/Window.h"
#include "GB/World/Grass.h"
#include "GB/World/Rocks.h"
#include "GB/World/Terrain.h"
#include "GB/World/Trees.h"
#include "GB/World/Water.h"
#include "GB/Renderer/Texture.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	constexpr uint32_t W = 192, H = 108;

	void Report(const GB::Qa::Result& r)
	{
		if (!r.Passed) { std::fprintf(stderr, "FAIL: %s\n", r.Message.c_str()); ++failures; }
		else std::fprintf(stderr, "ok: %s\n", r.Message.c_str());
	}

	// ---- T101: the maps of the ice and the wet glass, made here (ProcTexture's interior and wet-glass maps are T102's) ----
	float Rand(int x, int y, int k)
	{
		uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(k) * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
	}

	// Tileable Voronoi of `cells` x `cells` cells over [0, 1)^2: the distances to the nearest and second nearest points and the nearest cell.
	struct Cell { float F1, F2; int X, Y; };
	Cell Voronoi(float u, float v, int cells, int seed)
	{
		const float x = u * static_cast<float>(cells), y = v * static_cast<float>(cells);
		const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
		Cell c{ 9.0f, 9.0f, 0, 0 };
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int gx = cx + dx, gy = cy + dy;
				const int wx = (gx % cells + cells) % cells, wy = (gy % cells + cells) % cells;
				const float px = static_cast<float>(gx) + 0.1f + 0.8f * Rand(wx, wy, seed), py = static_cast<float>(gy) + 0.1f + 0.8f * Rand(wx, wy, seed + 1);
				const float d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y)) / static_cast<float>(cells);
				if (d < c.F1) { c.F2 = c.F1; c.F1 = d; c.X = wx; c.Y = wy; }
				else if (d < c.F2) c.F2 = d;
			}
		return c;
	}

	// The cost of the translucent pass at 1920x1080, 4x MSAA (GB_GLASS_BENCH=1): the scene as it is, its glass as plain Blend (no transmission)
	// and without any translucent object (T101: that one has to stay at zero).
	void GlassBench(const char* name, const GB::Scene& scene, const GB::Camera& cam)
	{
		GB::Renderer big(1920, 1080, 4, 2048);
		GB::Framebuffer bigFb(1920, 1080);
		const char* kinds[] = { "transmission", "plain blend", "no translucent" };
		for (int pass = 0; pass < 3; ++pass)
		{
			GB::Scene s = scene;
			if (pass == 1) for (GB::RenderObject& o : s.Objects) o.Mat.Transmission = 0.0f;
			if (pass == 2) std::erase_if(s.Objects, [](const GB::RenderObject& o) { return o.Mat.Alpha == GB::AlphaMode::Blend; });
			double ms = 0.0;
			for (int i = 0; i < 60; ++i)
			{
				big.Render(s, cam, &bigFb);
				if (i >= 20) ms += big.GetStats().GpuTranslucentMs;
			}
			std::fprintf(stderr, "bench: %s, %s, translucent pass %.3f ms (mean of 40 frames), copies %u, mips %u\n", name, kinds[pass], ms / 40.0,
				big.GetStats().TransmissionCopies, big.GetStats().TransmissionMips);
		}
	}

	GB::Image MakeImage(uint32_t size) { GB::Image img; img.Width = img.Height = size; img.Pixels.assign(static_cast<size_t>(size) * size * 4, 0); return img; }
	uint8_t Byte(float x) { return static_cast<uint8_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); }

	// The inside of the ice: rgb what is there, a its density. White cracks (the edges of two sizes of cells), bubbles and a faint cloud.
	GB::Image IceInterior(uint32_t size)
	{
		GB::Image img = MakeImage(size);
		for (uint32_t j = 0; j < size; ++j)
			for (uint32_t i = 0; i < size; ++i)
			{
				const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(j) + 0.5f) / static_cast<float>(size);
				const Cell big = Voronoi(u, v, 4, 11), fine = Voronoi(u, v, 9, 23);
				const float crack = 1.0f - std::clamp((big.F2 - big.F1) / 0.012f, 0.0f, 1.0f);
				const float hair = (1.0f - std::clamp((fine.F2 - fine.F1) / 0.006f, 0.0f, 1.0f)) * (Rand(fine.X, fine.Y, 5) > 0.45f ? 0.7f : 0.0f);
				const float cloud = 0.02f + 0.06f * Rand(big.X, big.Y, 7);
				float bubble = 0.0f;
				for (int b = 0; b < 60; ++b)
				{
					const float bx = Rand(b, 1, 31), by = Rand(b, 2, 31), r = (1.5f + 4.0f * Rand(b, 3, 31) * Rand(b, 4, 31)) / static_cast<float>(size);
					float dx = std::abs(u - bx), dy = std::abs(v - by);
					dx = std::min(dx, 1.0f - dx); dy = std::min(dy, 1.0f - dy);
					const float d = std::sqrt(dx * dx + dy * dy) / r;
					if (d < 1.0f) bubble = std::max(bubble, 0.45f + 0.5f * d * d);   // a bright rim
				}
				const float a = std::max({ crack * 0.95f, hair, bubble * 0.8f, cloud });
				uint8_t* p = &img.Pixels[(static_cast<size_t>(j) * size + i) * 4];
				p[0] = Byte(0.92f); p[1] = Byte(0.97f); p[2] = Byte(1.0f); p[3] = Byte(a);
			}
		return img;
	}

	// Faceted, chipped ice: a flat facet per cell, tilted at random, a groove along the edges; and the facets' heights (the thickness).
	void IceFacets(uint32_t size, GB::Image& normal, GB::Image& height)
	{
		normal = MakeImage(size);
		height = MakeImage(size);
		for (uint32_t j = 0; j < size; ++j)
			for (uint32_t i = 0; i < size; ++i)
			{
				const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(j) + 0.5f) / static_cast<float>(size);
				const Cell c = Voronoi(u, v, 6, 41);
				glm::vec3 n(Rand(c.X, c.Y, 1) * 2.0f - 1.0f, Rand(c.X, c.Y, 2) * 2.0f - 1.0f, 1.6f);
				const float groove = 1.0f - std::clamp((c.F2 - c.F1) / 0.02f, 0.0f, 1.0f);
				n = glm::normalize(glm::vec3(n.x, n.y, n.z * (1.0f - 0.6f * groove)));
				uint8_t* p = &normal.Pixels[(static_cast<size_t>(j) * size + i) * 4];
				p[0] = Byte(n.x * 0.5f + 0.5f); p[1] = Byte(n.y * 0.5f + 0.5f); p[2] = Byte(n.z * 0.5f + 0.5f); p[3] = 255;
				uint8_t* h = &height.Pixels[(static_cast<size_t>(j) * size + i) * 4];
				h[0] = h[1] = h[2] = Byte((0.25f + 0.75f * Rand(c.X, c.Y, 3)) * (1.0f - 0.5f * groove)); h[3] = 255;
			}
	}

	// Misted glass with drops and runs: a height field of domes (drops) and vertical capsules (runs); the normal from its slope, and the
	// metal-roughness map's g low on the water (sharp refraction) and 1 on the mist (the material's roughness: blurred); the albedo white on the
	// mist (its diffuse part: milky) and black on the water (clear).
	void WetGlass(uint32_t size, GB::Image& normal, GB::Image& rough, GB::Image& albedo)
	{
		const int n = static_cast<int>(size);
		std::vector<float> h(static_cast<size_t>(n) * n, 0.0f);
		auto at = [&](int x, int y) -> float& { return h[static_cast<size_t>((y % n + n) % n) * n + static_cast<size_t>((x % n + n) % n)]; };
		auto drop = [&](float cx, float cy, float rx, float ry)
		{
			for (int y = static_cast<int>(cy - ry) - 1; y <= static_cast<int>(cy + ry) + 1; ++y)
				for (int x = static_cast<int>(cx - rx) - 1; x <= static_cast<int>(cx + rx) + 1; ++x)
				{
					const float dx = (static_cast<float>(x) - cx) / rx, dy = (static_cast<float>(y) - cy) / ry, d2 = dx * dx + dy * dy;
					if (d2 < 1.0f) at(x, y) = std::max(at(x, y), std::sqrt(1.0f - d2) * std::min(rx, ry));
				}
		};
		for (int k = 0; k < 90; ++k)
		{
			const float r = 2.0f + 11.0f * Rand(k, 1, 51) * Rand(k, 2, 51);
			drop(Rand(k, 3, 51) * static_cast<float>(n), Rand(k, 4, 51) * static_cast<float>(n), r, r * (1.0f + 0.25f * Rand(k, 5, 51)));
		}
		for (int k = 0; k < 9; ++k)   // runs: a narrow trail with a drop at its foot
		{
			const float x = Rand(k, 6, 52) * static_cast<float>(n), top = Rand(k, 7, 52) * static_cast<float>(n);
			const float length = 30.0f + 80.0f * Rand(k, 8, 52), w = 1.5f + 1.5f * Rand(k, 9, 52);
			for (float y = top; y < top + length; y += 1.0f)
				drop(x + 2.0f * std::sin(y * 0.07f + static_cast<float>(k)), y, w, w * 1.5f);
			drop(x, top + length, w * 2.2f, w * 2.6f);
		}
		normal = MakeImage(size);
		rough = MakeImage(size);
		albedo = MakeImage(size);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x)
			{
				const float gx = (at(x + 1, y) - at(x - 1, y)) * 0.5f, gy = (at(x, y + 1) - at(x, y - 1)) * 0.5f;
				const glm::vec3 nn = glm::normalize(glm::vec3(-gx * 1.4f, -gy * 1.4f, 1.0f));   // row y is v (the GL order)
				uint8_t* p = &normal.Pixels[(static_cast<size_t>(y) * n + static_cast<size_t>(x)) * 4];
				p[0] = Byte(nn.x * 0.5f + 0.5f); p[1] = Byte(nn.y * 0.5f + 0.5f); p[2] = Byte(nn.z * 0.5f + 0.5f); p[3] = 255;
				const bool wet = at(x, y) > 0.05f;
				uint8_t* r = &rough.Pixels[(static_cast<size_t>(y) * n + static_cast<size_t>(x)) * 4];
				r[0] = 0; r[1] = Byte(wet ? 0.07f : 1.0f); r[2] = 255; r[3] = 255;
				uint8_t* c = &albedo.Pixels[(static_cast<size_t>(y) * n + static_cast<size_t>(x)) * 4];   // the mist is white, the water clear
				c[0] = c[1] = c[2] = Byte(wet ? 0.0f : 1.0f); c[3] = 255;
			}
	}
}

int main()
{
	GB::Log::Init();
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return 77;
	}
	const GB::Qa::Config qa = GB::Qa::Config::FromEnvironment();
	GB::Renderer renderer(W, H, 1, 1024);   // one sample: identical across drivers
	GB::Framebuffer fb(W, H);
	auto shoot = [&](const char* name, const GB::Scene& scene, const GB::Camera& cam, GB::Qa::ImageTolerance tol = {})
	{
		renderer.Render(scene, cam, &fb);
		GB::Image img;
		img.Width = W; img.Height = H; img.Pixels = fb.ReadPixels();
		Report(GB::Qa::CheckGoldenImage(qa, GB_GOLDEN_DIR "/images", name, img, tol));
	};
	GB::Mesh sphere = GB::Mesh::CreateSphere(0.5f, 48, 24);
	GB::Mesh floorMesh = GB::Mesh::CreateGrid(60.0f, 16);

	// 1. PBR: roughness sweep of metal and dielectric spheres with sun shadows
	{
		GB::Scene sc;
		GB::RenderObject f;
		f.MeshPtr = &floorMesh; f.Mat.Albedo = glm::vec3(0.35f); f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(f);
		for (int i = 0; i < 5; ++i)
			for (int row = 0; row < 2; ++row)
			{
				GB::RenderObject o;
				o.MeshPtr = &sphere;
				o.Mat.Albedo = row ? glm::vec3(0.9f, 0.7f, 0.3f) : glm::vec3(0.8f, 0.1f, 0.1f);
				o.Mat.Metallic = row ? 1.0f : 0.0f;
				o.Mat.Roughness = 0.1f + 0.2f * static_cast<float>(i);
				o.Transform = glm::translate(glm::mat4(1.0f), { (static_cast<float>(i) - 2.0f) * 1.2f, 0.5f, static_cast<float>(row) * -1.3f });
				sc.Objects.push_back(o);
			}
		sc.Env.ShadowCenter = { 0, 0, 0 }; sc.Env.ShadowRadius = 8.0f;
		GB::Camera cam; cam.Position = { 0.0f, 3.0f, 6.5f }; cam.Target = { 0.0f, 0.4f, -0.4f };
		shoot("pbr_spheres", sc, cam);
		Report({ renderer.GetStats().TransmissionCopies == 0, "no transmissive surface, no copy of the scene (T34)" });
	}

	// 2. A night: moon, stars, clouds and two lamps
	{
		GB::SkyDesc sky; sky.Hours = 23.0f; sky.CloudCover = 0.35f;
		GB::Scene sc;
		const GB::SkyState st = GB::Sky::Compute(sky, sc.Env, sc.Sun);
		sc.Env = st.Env; sc.Sun = st.Sun;
		sc.Env.Time = 3.0f;
		GB::RenderObject f;
		f.MeshPtr = &floorMesh; f.Mat.Albedo = glm::vec3(0.3f, 0.4f, 0.2f); f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(f);
		for (int i = 0; i < 2; ++i)
		{
			GB::PointLight l; l.Position = { -3.0f + 6.0f * static_cast<float>(i), 2.5f, -4.0f }; l.Color = { 1.0f, 0.68f, 0.38f }; l.Intensity = 22.0f; l.Radius = 12.0f;
			sc.PointLights.push_back(l);
			GB::RenderObject lamp;
			lamp.MeshPtr = &sphere; lamp.Mat.Albedo = glm::vec3(1.0f, 0.75f, 0.4f); lamp.Mat.Emissive = { 9.0f, 5.2f, 2.2f };
			lamp.Transform = glm::scale(glm::translate(glm::mat4(1.0f), l.Position), glm::vec3(0.4f));
			sc.Objects.push_back(lamp);
		}
		sc.Sun.CastShadows = false;
		GB::Camera cam; cam.Position = { 0.0f, 1.6f, 6.0f }; cam.Target = { 0.0f, 3.5f, -8.0f };
		shoot("night_lamps", sc, cam);
	}

	GB::WaterSurface water(60.0f, 96);
	water.Params.WaveHeight = 0.12f;
	// 3. Water with waves, refraction and screen-space reflections over a sloping bed
	{
		GB::Scene sc;
		sc.Env.Time = 2.0f;
		GB::Mesh bed = GB::Mesh::CreateGrid(80.0f, 32);
		GB::RenderObject b;
		b.MeshPtr = &bed; b.Mat.Albedo = glm::vec3(0.5f, 0.45f, 0.3f); b.Mat.HeightScale = 0.0f; b.Mat.CavityStrength = 0.0f;
		b.Transform = glm::translate(glm::mat4(1.0f), { 0, -2.0f, 0 });
		sc.Objects.push_back(b);
		GB::Mesh block = GB::Mesh::CreateCube(1.0f);
		GB::RenderObject c;
		c.MeshPtr = &block; c.Mat.Albedo = { 0.8f, 0.2f, 0.15f };
		c.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { 2.0f, 1.0f, -6.0f }), glm::vec3(2.0f));
		sc.Objects.push_back(c);
		sc.Water.push_back(&water);
		GB::Camera cam; cam.Position = { -2.0f, 3.0f, 8.0f }; cam.Target = { 1.0f, 0.0f, -4.0f };
		shoot("water_reflections", sc, cam);
	}

	// 4. Rolling hills with grass and trees (all procedural, seeded)
	GB::HillParams hp; hp.Seed = 5; hp.Height = 10.0f;
	auto terrain = GB::Terrain::CreateHills(hp, 96.0f, 129, GB::TerrainStyle::Meadow);
	GB::TerrainGpu terrainGpu(*terrain);
	GB::TreesGpu trees;
	auto oak = trees.Create([] { GB::TreeParams t; t.Species = GB::TreeSpecies::Broadleaf; t.Height = 8.0f; t.Seed = 3; return t; }());
	auto pine = trees.Create([] { GB::TreeParams t; t.Species = GB::TreeSpecies::Conifer; t.Height = 11.0f; t.Seed = 4; return t; }());
	GB::GrassParams gp;
	gp.Seed = 5; gp.Size = 40.0f; gp.Density = 30.0f;
	gp.HeightFn = terrain->MakeGroundFunction();
	const GB::Heightmap* hm = &terrain->GetHeightmap();
	gp.NormalFn = [hm](float x, float z) { return hm->GetNormal(x, z); };
	GB::GrassField grass(gp);
	GB::GrassGpu grassGpu(grass);
	auto hillScene = [&]()
	{
		GB::Scene sc;
		terrainGpu.AddToScene(sc);
		sc.Grass.push_back(&grassGpu);
		oak->AddToScene(sc, { -6.0f, terrain->GetHeight(-6.0f, -10.0f), -10.0f }, 0.5f, 1.0f);
		pine->AddToScene(sc, { 7.0f, terrain->GetHeight(7.0f, -14.0f), -14.0f }, 1.2f, 1.0f);
		sc.Env.Time = 4.0f;
		sc.Env.ShadowCenter = { 0, 2, -8 }; sc.Env.ShadowRadius = 30.0f;
		sc.Sun.Direction = { -0.5f, -0.6f, -0.4f };
		return sc;
	};
	GB::Camera hillCam; hillCam.Position = { 0.0f, terrain->GetHeight(0.0f, 8.0f) + 2.2f, 8.0f }; hillCam.Target = { 0.0f, terrain->GetHeight(0.0f, -10.0f) + 3.0f, -10.0f };
	{
		GB::Scene sc = hillScene();
		shoot("hills_grass_trees", sc, hillCam, { 4.0, 0.02, 32 });   // foliage is the most sensitive to driver differences
	}
	{
		GB::Scene sc = hillScene();
		sc.Env.Wetness = 1.0f;
		sc.Env.FogDensity = 0.012f;
		sc.Env.SkyTop = { 0.12f, 0.14f, 0.18f }; sc.Env.SkyHorizon = { 0.3f, 0.32f, 0.36f };
		sc.Sun.Intensity = 0.8f;
		shoot("hills_rain_puddles", sc, hillCam, { 4.0, 0.02, 32 });
	}

	// 5. Rocks: moss on top, sand at the base, a wet band at the water line
	{
		GB::Scene sc;
		sc.Sun.Intensity = 2.0f;
		GB::RenderObject f;
		f.MeshPtr = &floorMesh; f.Mat.Albedo = glm::vec3(0.3f, 0.35f, 0.2f); f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(f);
		GB::RockParams a; a.Seed = 3; a.Size = 1.0f; a.Moss = 0.8f;
		GB::RockParams b; b.Seed = 8; b.Size = 1.1f; b.Sand = 0.9f; b.Color = { 0.5f, 0.4f, 0.3f };
		GB::RockParams c; c.Seed = 12; c.Size = 0.9f;
		auto r1 = GB::RocksGpu::Create(a), r2 = GB::RocksGpu::Create(b), r3 = GB::RocksGpu::Create(c);
		r1->AddToScene(sc, { -2.2f, 0, 0 }, 0.3f);
		r2->AddToScene(sc, { 0.0f, 0, 0 }, 1.0f);
		r3->AddToScene(sc, { 2.2f, 0, 0 }, 2.0f, 0.1f, 0.45f);
		sc.Env.ShadowCenter = { 0, 0, 0 }; sc.Env.ShadowRadius = 8.0f;
		GB::Camera cam; cam.Position = { 0.5f, 2.2f, 5.5f }; cam.Target = { 0.0f, 0.5f, 0.0f };
		shoot("rocks_layers", sc, cam, { 4.0, 0.02, 32 });
	}

	// 6. The post stack: SSAO, depth of field and the Neutral tone mapper
	{
		GB::Scene sc;
		GB::RenderObject f;
		f.MeshPtr = &floorMesh; f.Mat.Albedo = glm::vec3(0.5f); f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(f);
		GB::Mesh box = GB::Mesh::CreateCube(1.0f);
		for (int i = 0; i < 6; ++i)
		{
			GB::RenderObject o;
			o.MeshPtr = &box; o.Mat.Albedo = glm::vec3(0.8f, 0.5f + 0.08f * static_cast<float>(i), 0.3f);
			o.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { (static_cast<float>(i) - 2.5f) * 1.7f, 0.6f, -static_cast<float>(i) * 2.5f }), glm::vec3(1.2f));
			sc.Objects.push_back(o);
		}
		sc.Post.SSAO = 0.8f; sc.Post.DepthOfField = 3.0f; sc.Post.FocusDistance = 12.0f; sc.Post.FocusRange = 4.0f;
		sc.Post.Mapper = GB::Tonemap::Neutral; sc.Post.Saturation = 1.1f; sc.Post.Contrast = 1.05f;
		sc.Env.ShadowCenter = { 0, 0, -6 }; sc.Env.ShadowRadius = 14.0f;
		GB::Camera cam; cam.Position = { 1.0f, 3.0f, 8.0f }; cam.Target = { 0.0f, 0.5f, -5.0f };
		shoot("post_stack", sc, cam, { 4.0, 0.02, 32 });
	}

	// 7. Glass and ice (T34): a clear glass sphere and block (IOR 1.5, almost no absorption), a green glass, and a frosted block of ice (IOR 1.31,
	// a soft blue absorption, medium roughness) in front of colored bars: what is behind shows through, shifted and tinted, the edges brighter
	// (Fresnel). GB_GLASS_SHOT=<file.png> also renders it at 1280x720 with 4x MSAA into that file (the documentation's picture).
	{
		GB::Mesh box = GB::Mesh::CreateCube(1.0f);
		GB::Scene sc;
		GB::RenderObject f;
		f.MeshPtr = &floorMesh; f.Mat.Albedo = glm::vec3(0.42f, 0.4f, 0.37f); f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(f);
		const glm::vec3 colors[] = { { 0.8f, 0.1f, 0.08f }, { 0.9f, 0.75f, 0.1f }, { 0.1f, 0.3f, 0.85f }, { 0.95f, 0.95f, 0.92f }, { 0.12f, 0.6f, 0.15f },
			{ 0.85f, 0.35f, 0.05f }, { 0.08f, 0.08f, 0.08f }, { 0.6f, 0.15f, 0.7f } };
		for (int i = 0; i < 16; ++i)   // bars behind the glass: their edges show the refraction's shift
		{
			GB::RenderObject bar;
			bar.MeshPtr = &box; bar.Mat.Albedo = colors[i % 8]; bar.Mat.Roughness = 0.6f; bar.Mat.HeightScale = 0.0f; bar.Mat.CavityStrength = 0.0f;
			bar.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { (static_cast<float>(i) - 7.5f) * 0.42f, 1.0f, -2.2f }), glm::vec3(0.3f, 2.0f, 0.3f));
			sc.Objects.push_back(bar);
		}
		auto glass = [](float roughness, float ior, glm::vec3 absorption, float thickness)
		{
			GB::Material m;
			m.Albedo = glm::vec3(1.0f); m.Roughness = roughness; m.HeightScale = 0.0f; m.CavityStrength = 0.0f;
			m.Alpha = GB::AlphaMode::Blend;
			m.Transmission = 1.0f; m.Ior = ior; m.Absorption = absorption; m.Thickness = thickness;
			return m;
		};
		auto add = [&](const GB::Mesh& mesh, const GB::Material& m, glm::vec3 at, glm::vec3 scale, float turn)
		{
			GB::RenderObject o;
			o.MeshPtr = &mesh; o.Mat = m;
			o.Transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), at), glm::radians(turn), { 0, 1, 0 }), scale);
			sc.Objects.push_back(o);
		};
		add(sphere, glass(0.04f, 1.5f, glm::vec3(0.02f), 0.5f), { -2.2f, 0.62f, 0.0f }, glm::vec3(1.2f), 0.0f);
		add(box, glass(0.04f, 1.5f, glm::vec3(0.02f), 0.4f), { -0.75f, 0.45f, 0.2f }, glm::vec3(0.9f), 30.0f);
		add(sphere, glass(0.06f, 1.5f, { 2.6f, 0.25f, 2.2f }, 0.5f), { 0.75f, 0.55f, 0.1f }, glm::vec3(1.1f), 0.0f);
		add(box, glass(0.45f, 1.31f, { 0.9f, 0.25f, 0.06f }, 0.5f), { 2.2f, 0.5f, 0.0f }, glm::vec3(0.95f, 1.0f, 0.95f), -20.0f);
		sc.Env.ShadowCenter = { 0, 0, -1 }; sc.Env.ShadowRadius = 8.0f;
		sc.Sun.Direction = { -0.4f, -0.7f, -0.6f };
		GB::Camera cam; cam.Position = { 0.0f, 1.7f, 4.6f }; cam.Target = { 0.0f, 0.6f, -0.6f };
		shoot("glass_ice", sc, cam, { 4.0, 0.02, 32 });
		Report({ renderer.GetStats().TransmissionCopies == 1, "the transmissive surfaces share one copy of the scene (T34)" });
		if (const char* shot = std::getenv("GB_GLASS_SHOT"); shot && *shot)
		{
			GB::Renderer big(1280, 720, 4, 2048);
			GB::Framebuffer bigFb(1280, 720);
			big.Render(sc, cam, &bigFb);
			Report({ bigFb.SavePNG(shot), std::string("the large glass picture: ") + shot });
		}
		if (std::getenv("GB_GLASS_BENCH"))
			GlassBench("glass_ice", sc, cam);
	}

	// 8. Ice and glass v2 (T101): a faceted ice ball and block with an interior map (white cracks and bubbles seen inside, with parallax), a
	// strong cyan absorption whose thickness the facets' height changes, light through the thin edges (scatter) and a faint shadow; and a
	// misted pane with drops and runs (a normal map and a metal-roughness map: the drops are smooth and refract sharp, the mist between them
	// is rough and blurs). In front of the same colored bars. The maps are made here (ProcTexture's are T102's). GB_GLASS_V2_SHOT=<file.png>
	// renders it at 1280x720 with 4x MSAA (the documentation's picture); GB_GLASS_BENCH=1 measures it too.
	{
		GB::Mesh box = GB::Mesh::CreateCube(1.0f);
		GB::Mesh pane = GB::Mesh::CreateCube(1.0f);
		GB::Scene sc;
		GB::RenderObject f;
		f.MeshPtr = &floorMesh; f.Mat.Albedo = glm::vec3(0.42f, 0.4f, 0.37f); f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(f);
		const glm::vec3 colors[] = { { 0.8f, 0.1f, 0.08f }, { 0.9f, 0.75f, 0.1f }, { 0.1f, 0.3f, 0.85f }, { 0.95f, 0.95f, 0.92f }, { 0.12f, 0.6f, 0.15f },
			{ 0.85f, 0.35f, 0.05f }, { 0.08f, 0.08f, 0.08f }, { 0.6f, 0.15f, 0.7f } };
		for (int i = 0; i < 16; ++i)
		{
			GB::RenderObject bar;
			bar.MeshPtr = &box; bar.Mat.Albedo = colors[i % 8]; bar.Mat.Roughness = 0.6f; bar.Mat.HeightScale = 0.0f; bar.Mat.CavityStrength = 0.0f;
			bar.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { (static_cast<float>(i) - 7.5f) * 0.42f, 1.0f, -2.2f }), glm::vec3(0.3f, 2.0f, 0.3f));
			sc.Objects.push_back(bar);
		}
		GB::Image iceNormal, iceHeight, wetNormal, wetRough, wetAlbedo;
		IceFacets(256, iceNormal, iceHeight);
		WetGlass(256, wetNormal, wetRough, wetAlbedo);
		const GB::Texture2D interiorMap(IceInterior(256), GB::TextureEncoding::Linear);
		const GB::Texture2D iceNormalMap(iceNormal, GB::TextureEncoding::Linear), iceHeightMap(iceHeight, GB::TextureEncoding::Linear);
		const GB::Texture2D wetNormalMap(wetNormal, GB::TextureEncoding::Linear), wetRoughMap(wetRough, GB::TextureEncoding::Linear);
		const GB::Texture2D wetAlbedoMap(wetAlbedo, GB::TextureEncoding::SRGB);

		GB::Material ice;
		ice.Albedo = glm::vec3(0.85f, 0.95f, 1.0f); ice.Roughness = 0.3f; ice.HeightScale = 0.0f; ice.CavityStrength = 0.0f;
		ice.Alpha = GB::AlphaMode::Blend;
		ice.Transmission = 0.95f; ice.Ior = 1.31f; ice.Absorption = { 4.2f, 0.8f, 0.32f }; ice.Thickness = 0.5f;
		ice.NormalMap = &iceNormalMap; ice.NormalStrength = 0.8f; ice.HeightMap = &iceHeightMap;
		ice.UVScale = 2.0f; ice.InteriorMap = &interiorMap; ice.InteriorDepth = 0.05f; ice.InteriorStrength = 0.9f; ice.InteriorTint = glm::vec3(1.0f);
		ice.Scatter = 0.6f; ice.ShadowDensity = 0.35f; ice.ThicknessVariation = 0.8f;
		GB::Material iceBlock = ice;
		iceBlock.UVScale = 1.2f;

		GB::Material wet;
		wet.Albedo = glm::vec3(0.95f); wet.Roughness = 0.6f; wet.HeightScale = 0.0f; wet.CavityStrength = 0.0f;
		wet.Alpha = GB::AlphaMode::Blend;
		wet.Transmission = 0.8f; wet.Ior = 1.33f; wet.Absorption = glm::vec3(0.05f); wet.Thickness = 0.12f;
		wet.NormalMap = &wetNormalMap; wet.MetallicRoughnessMap = &wetRoughMap; wet.AlbedoMap = &wetAlbedoMap; wet.UVScale = 1.0f;

		auto add = [&](const GB::Mesh& mesh, const GB::Material& m, glm::vec3 at, glm::vec3 scale, float turn)
		{
			GB::RenderObject o;
			o.MeshPtr = &mesh; o.Mat = m;
			o.Transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), at), glm::radians(turn), { 0, 1, 0 }), scale);
			sc.Objects.push_back(o);
		};
		add(sphere, ice, { -1.9f, 0.68f, 0.0f }, glm::vec3(1.3f), 20.0f);
		add(box, iceBlock, { -0.35f, 0.45f, 0.35f }, glm::vec3(0.9f), 25.0f);
		add(pane, wet, { 1.55f, 0.85f, 0.1f }, glm::vec3(1.6f, 1.6f, 0.04f), -12.0f);
		GB::RenderObject post;   // in front of the ice ball: the refraction's depth test keeps it out of the ice (no leak of what is in front)
		post.MeshPtr = &box; post.Mat.Albedo = { 0.75f, 0.12f, 0.1f }; post.Mat.Roughness = 0.6f; post.Mat.HeightScale = 0.0f; post.Mat.CavityStrength = 0.0f;
		post.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { -1.45f, 0.55f, 1.05f }), glm::vec3(0.12f, 1.1f, 0.12f));
		sc.Objects.push_back(post);
		sc.Env.ShadowCenter = { 0, 0, -1 }; sc.Env.ShadowRadius = 8.0f;
		sc.Sun.Direction = { -0.4f, -0.7f, -0.6f };
		GB::Camera cam; cam.Position = { 0.0f, 1.7f, 4.6f }; cam.Target = { 0.0f, 0.6f, -0.6f };
		shoot("ice_glass_v2", sc, cam, { 4.0, 0.02, 32 });
		Report({ renderer.GetStats().TransmissionCopies == 1 && renderer.GetStats().TransmissionMips == 1,
			"one copy of the scene, with its mips: a rough transmissive surface is in view (T101)" });
		if (const char* shot = std::getenv("GB_GLASS_V2_SHOT"); shot && *shot)
		{
			GB::Renderer big(1280, 720, 4, 2048);
			GB::Framebuffer bigFb(1280, 720);
			big.Render(sc, cam, &bigFb);
			Report({ bigFb.SavePNG(shot), std::string("the large ice and glass v2 picture: ") + shot });
		}
		if (std::getenv("GB_GLASS_BENCH"))
			GlassBench("ice_glass_v2", sc, cam);
	}
	return failures;
}
