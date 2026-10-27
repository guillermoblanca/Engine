// Golden-image tests: canonical scenes rendered headless and compared with the reference pictures in
// tests/golden/images. Update after an intended visual change: GB_UPDATE_GOLDEN=1 ctest -R golden_images
// (then look at the pictures and commit them). Failures leave expected | actual | difference in ./qa_artifacts.
#include "GB/Effects/Sky.h"
#include "GB/Log.h"
#include "GB/Qa/Qa.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include "GB/World/Grass.h"
#include "GB/World/Rocks.h"
#include "GB/World/Terrain.h"
#include "GB/World/Trees.h"
#include "GB/World/Water.h"
#include <cstdio>
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
	GB::Renderer renderer(W, H, 1, 1024);   // one sample: identical across drivers
	GB::Framebuffer fb(W, H);
	auto shoot = [&](const char* name, const GB::Scene& scene, const GB::Camera& cam, GB::Qa::ImageTolerance tol = {})
	{
		renderer.Render(scene, cam, &fb);
		GB::Image img;
		img.Width = W; img.Height = H; img.Pixels = fb.ReadPixels();
		Report(GB::Qa::CheckGoldenImage(GB_GOLDEN_DIR "/images", name, img, tol));
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
	GB::TreeFactory trees;
	auto oak = trees.Create([] { GB::TreeParams t; t.Species = GB::TreeSpecies::Broadleaf; t.Height = 8.0f; t.Seed = 3; return t; }());
	auto pine = trees.Create([] { GB::TreeParams t; t.Species = GB::TreeSpecies::Conifer; t.Height = 11.0f; t.Seed = 4; return t; }());
	GB::GrassParams gp;
	gp.Seed = 5; gp.Size = 40.0f; gp.Density = 30.0f;
	gp.HeightFn = terrain->MakeGroundFunction();
	const GB::Heightmap* hm = &terrain->GetHeightmap();
	gp.NormalFn = [hm](float x, float z) { return hm->GetNormal(x, z); };
	GB::GrassField grass(gp);
	auto hillScene = [&]()
	{
		GB::Scene sc;
		terrain->AddToScene(sc);
		sc.Grass.push_back(&grass);
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
		auto r1 = GB::RockFactory::Create(a), r2 = GB::RockFactory::Create(b), r3 = GB::RockFactory::Create(c);
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
	return failures;
}
