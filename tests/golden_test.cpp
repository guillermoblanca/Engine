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
#include <cstdio>
#include <cstdlib>
#include <string>
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
		if (std::getenv("GB_GLASS_BENCH"))   // the cost of the translucent pass at 1920x1080, 4x MSAA: glass with transmission vs plain Blend
		{
			GB::Renderer big(1920, 1080, 4, 2048);
			GB::Framebuffer bigFb(1920, 1080);
			for (int pass = 0; pass < 2; ++pass)
			{
				GB::Scene s = sc;
				if (pass == 1) for (GB::RenderObject& o : s.Objects) o.Mat.Transmission = 0.0f;
				double ms = 0.0;
				for (int i = 0; i < 60; ++i)
				{
					big.Render(s, cam, &bigFb);
					if (i >= 20) ms += big.GetStats().GpuTranslucentMs;
				}
				std::fprintf(stderr, "bench: %s, translucent pass %.3f ms (mean of 40 frames), copies %u\n", pass == 0 ? "transmission" : "plain blend",
					ms / 40.0, big.GetStats().TransmissionCopies);
			}
		}
	}
	return failures;
}
