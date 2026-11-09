// The reference scene of the render plan (T40, documentation/02_tecnico/arquitectura/PLAN_RENDER.md E1): one fixed village (buildings, rocks and
// trees of the visuals data with their glTF pieces and material layers, grass, a lake and people) at 7 h, 12 h, 18:30 and 22 h, in rain and in snow.
// Each stage of the plan (the RHI with no change of picture, physical light, CSM, GTAO/TAA...) is compared against these six pictures, and against
// what this scene costs per pass.
//
// Deterministic: fixed seeds, hours and camera, the weather advanced by a fixed number of fixed steps, the people posed at a fixed time of their
// clip; nothing reads the clock. One sample per pixel like golden_test.cpp (identical across runs on a driver).
// Update after an intended visual change: GB_UPDATE_GOLDEN=1 ctest -R "^reference_scene$" (then look at the pictures and commit them).
//
// GB_REFERENCE_BENCH=1: instead of checking the pictures, measures the scene at 1920x1080 with the editor's `medium` quality (MSAA 4, a 2048 shadow
// map, SSAO 0.4): the GPU milliseconds of every pass (RenderStats) and the CPU time of building the scene and sending it, mean and deviation of
// GB_REFERENCE_SAMPLES (30) frames per shot, after a warm-up. Not part of ctest.
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Effects/Sky.h"
#include "GB/Effects/Weather.h"
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Qa/Qa.h"
#include "GB/Renderer/GrassGpu.h"
#include "GB/Renderer/ModelGpu.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/TerrainGpu.h"
#include "GB/Visual/SceneVisuals.h"
#include "GB/Visual/VisualDefs.h"
#include "GB/World/Grass.h"
#include "GB/World/Terrain.h"
#include "GB/World/Water.h"
#include <glad/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	constexpr uint32_t W = 480, H = 270;   // big enough to read the rocks' moss and the people, small enough to keep in the repository

	void Report(const GB::Qa::Result& r)
	{
		if (!r.Passed) { std::fprintf(stderr, "FAIL: %s\n", r.Message.c_str()); ++failures; }
		else std::fprintf(stderr, "ok: %s\n", r.Message.c_str());
	}

	// One shot of the reference: the hour and the weather.
	struct Shot
	{
		const char* Name;
		float Hours;
		enum class Sky { Clear, Rain, Snow } Weather = Sky::Clear;
	};
	const Shot kShots[] = {
		{ "reference_07h", 7.0f }, { "reference_12h", 12.0f }, { "reference_1830", 18.5f }, { "reference_22h", 22.0f },
		{ "reference_rain", 12.0f, Shot::Sky::Rain }, { "reference_snow", 12.0f, Shot::Sky::Snow },
	};

	struct Stat
	{
		std::vector<double> V;
		void Add(double x) { V.push_back(x); }
		double Mean() const { double s = 0.0; for (double x : V) s += x; return V.empty() ? 0.0 : s / static_cast<double>(V.size()); }
		double Dev() const
		{
			if (V.size() < 2) return 0.0;
			const double m = Mean();
			double s = 0.0;
			for (double x : V) s += (x - m) * (x - m);
			return std::sqrt(s / static_cast<double>(V.size() - 1));
		}
	};
}

int main()
{
	GB::Log::Init();
	const char* benchEnv = std::getenv("GB_REFERENCE_BENCH");
	const bool bench = benchEnv && benchEnv[0] == '1';
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return 77;
	}

	// ------------------------------------------------------------------ the world: gentle hills with a lake, grass
	GB::HillParams hp;
	hp.Seed = 7; hp.Height = 3.0f; hp.Scale = 90.0f;
	hp.LakeCenter = { -16.0f, -30.0f }; hp.LakeRadius = 11.0f; hp.LakeDepth = 2.0f; hp.WaterLevel = 0.0f; hp.LakeIrregularity = 0.15f;
	auto terrain = GB::Terrain::CreateHills(hp, 128.0f, 129, GB::TerrainStyle::Meadow);
	GB::TerrainGpu terrainGpu(*terrain);
	auto ground = [&](float x, float z) { return terrain->GetHeight(x, z); };
	GB::GrassParams gp;
	gp.Seed = 7; gp.Size = 56.0f; gp.Density = 24.0f;
	gp.HeightFn = terrain->MakeGroundFunction();
	const GB::Heightmap* hm = &terrain->GetHeightmap();
	gp.NormalFn = [hm](float x, float z) { return hm->GetNormal(x, z); };
	GB::GrassField grass(gp);
	GB::GrassGpu grassGpu(grass);
	GB::WaterSurface lake(40.0f, 96);
	lake.Position = { hp.LakeCenter.x, hp.WaterLevel, hp.LakeCenter.y };
	lake.Radius = hp.LakeRadius + 2.0f;

	// ------------------------------------------------------------------ the visuals data: buildings, rocks, trees (glTF pieces, material layers)
	GB::VisualLibrary library;
	const std::string visualsError = GB::LoadVisualsFile(GB_ASSETS_DIR "/data/visuals.json", library);
	if (!visualsError.empty())
	{
		std::fprintf(stderr, "FAIL: visuals.json: %s\n", visualsError.c_str());
		return 1;
	}
	GB::VisualCache visuals(library);
	visuals.AssetDir = GB_ASSETS_DIR;
	auto at = [&](float x, float z, float yawDegrees, float scale = 1.0f)
	{
		return glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), { x, ground(x, z), z }), glm::radians(yawDegrees), glm::vec3(0, 1, 0)), glm::vec3(scale));
	};
	auto lastPhase = [&](const std::string& id) { const auto it = library.Buildings.find(id); return it == library.Buildings.end() ? 0 : static_cast<int>(it->second.Phases.size()) - 1; };

	// ------------------------------------------------------------------ the people: X Bot posed at a fixed time of its clips, with clan colours
	std::unique_ptr<GB::Model> person = GB::Model::Load(GB_PACKS_DIR "/X Bot.fbx");
	struct Person { std::unique_ptr<GB::ModelInstance> Instance; glm::mat4 Root; std::vector<glm::vec3> Tints; };
	std::vector<Person> people;
	if (person)
	{
		for (const char* clip : { "idle", "dig and plant seeds", "kneeling idle", "Standing Walk Forward" })
			person->AppendAnimations(std::string(GB_PACKS_DIR "/") + clip + ".fbx");
		const float height = person->GetBoundsMax().y - person->GetBoundsMin().y;
		const float scale = height > 0.0f ? 1.72f / height : 1.0f;
		struct Spot { float X, Z, Yaw; const char* Clip; float Time; glm::vec3 Clan; };
		const Spot spots[] = {
			{ 1.6f, -9.2f, 200.0f, "idle", 0.7f, { 0.75f, 0.25f, 0.18f } },
			{ -1.2f, -9.6f, 140.0f, "kneeling idle", 1.3f, { 0.75f, 0.25f, 0.18f } },
			{ 3.6f, -5.5f, -30.0f, "Standing Walk Forward", 0.35f, { 0.2f, 0.4f, 0.75f } },
			{ -4.5f, -3.5f, 70.0f, "dig and plant seeds", 2.1f, { 0.75f, 0.25f, 0.18f } },
		};
		const std::vector<GB::ModelMaterial>& mats = person->GetMaterials();
		size_t brightest = 0;
		auto light = [](const glm::vec3& c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; };
		for (size_t i = 1; i < mats.size(); ++i) if (light(mats[i].Albedo) > light(mats[brightest].Albedo)) brightest = i;
		for (const Spot& s : spots)
		{
			Person p;
			p.Instance = std::make_unique<GB::ModelInstance>(*person);
			const int clip = person->FindClip(s.Clip);
			if (clip >= 0) { p.Instance->Play(clip, true, 0.0f); p.Instance->SetTime(s.Time); }
			else std::fprintf(stderr, "note: no clip '%s' (rest pose)\n", s.Clip);
			p.Root = at(s.X, s.Z, s.Yaw, scale);
			p.Tints.assign(mats.size(), glm::vec3(1.0f));   // the clan's colour on the brightest material (TribeVisuals' rule)
			const float l = std::max(light(mats[brightest].Albedo), 0.05f);
			if (!mats.empty()) p.Tints[brightest] = glm::mix(glm::vec3(1.0f), s.Clan * (std::min(1.0f, l * 1.4f) / l), 0.85f);
			people.push_back(std::move(p));
		}
	}
	else
		std::fprintf(stderr, "note: no X Bot in " GB_PACKS_DIR ": the reference has no people\n");

	const glm::vec3 hearth{ 0.0f, ground(0.0f, -7.5f), -7.5f };
	auto addStatic = [&](GB::Scene& sc)
	{
		terrainGpu.AddToScene(sc);
		sc.Grass.push_back(&grassGpu);
		sc.Water.push_back(&lake);
		auto building = [&](const char* id, float x, float z, float yaw)
		{
			if (!visuals.AddBuilding(id, lastPhase(id), 1.0f, at(x, z, yaw), sc)) std::fprintf(stderr, "note: no building '%s'\n", id);
		};
		building("longhouse", 9.0f, -16.0f, -20.0f);
		building("hut", -7.5f, -13.0f, 25.0f);
		building("hut", -12.0f, -5.5f, 60.0f);
		building("storage", 10.5f, -6.5f, -70.0f);
		building("granary", 4.0f, -20.0f, 0.0f);
		building("drying_rack", -3.0f, -16.5f, 10.0f);
		building("hearth", hearth.x, hearth.z, 0.0f);
		building("totem", 5.0f, -10.5f, 200.0f);
		auto model = [&](const char* id, float x, float z, float yaw, int variant, float scale = 1.0f, float fraction = 1.0f)
		{
			if (!visuals.AddModel(id, fraction, variant, at(x, z, yaw, scale), sc)) std::fprintf(stderr, "note: no model '%s'\n", id);
		};
		model("boulder", -4.2f, 1.0f, 30.0f, 0, 1.1f);
		model("rock", 2.8f, 2.6f, 110.0f, 1);
		model("rock", 5.6f, 0.2f, 250.0f, 2, 0.9f);
		model("rock_small", 0.6f, 4.4f, 0.0f, 0);
		model("granite_rock", -9.0f, -3.5f, 45.0f, 0);
		model("old_tree", -20.0f, -12.0f, 0.0f, 0);
		model("tree", 18.0f, -24.0f, 80.0f, 1);
		model("tree", -4.0f, -32.0f, 160.0f, 2);
		model("tree", 22.0f, -10.0f, 200.0f, 0);
		model("young_tree", 13.0f, 3.0f, 40.0f, 1);
		for (int i = 0; i < 9; ++i)   // a wood line behind the village (it hides the end of the ground)
			model(i % 3 == 0 ? "old_tree" : "tree", -30.0f + 7.5f * static_cast<float>(i), -46.0f + 3.0f * static_cast<float>(i % 2), 40.0f * static_cast<float>(i), i % 3, 1.3f);
		model("berry_bush", -9.0f, -1.0f, 0.0f, 0);
		model("berry_bush", 8.5f, -1.5f, 90.0f, 1);
		model("reed_bed", -9.5f, -24.0f, 0.0f, 0);
		for (const Person& p : people) GB::AddToScene(*p.Instance, sc, p.Root, &p.Tints);
	};

	GB::Camera cam;
	cam.Position = { 1.0f, ground(1.0f, 9.5f) + 3.6f, 9.5f };
	cam.Target = { 0.0f, ground(0.0f, -10.0f) + 1.0f, -10.0f };

	// One shot's scene: the sky of the hour on a fresh base (Sky::Compute compounds when applied twice), the weather on top of it advanced by a
	// fixed number of fixed steps, the hearth's fire at night.
	auto makeScene = [&](const Shot& shot, GB::Scene& sc, GB::Weather& weather)
	{
		sc = GB::Scene();
		GB::Environment base;
		base.ShadowCenter = { 1.0f, ground(1.0f, -8.0f), -8.0f };
		base.ShadowRadius = 30.0f;
		GB::SkyDesc d;
		d.Hours = shot.Hours;
		const GB::SkyState st = GB::Sky::Compute(d, base, GB::DirectionalLight());
		sc.Env = st.Env;
		sc.Sun = st.Sun;
		sc.Post.SSAO = 0.4f;   // the editor's `medium`
		addStatic(sc);
		if (st.NightFactor > 0.05f)
		{
			GB::PointLight fire;
			fire.Position = hearth + glm::vec3(0.0f, 0.6f, 0.0f);
			fire.Color = { 1.0f, 0.6f, 0.3f };
			fire.Intensity = 18.0f * st.NightFactor;
			fire.Radius = 14.0f;
			sc.PointLights.push_back(fire);
		}
		weather.Reseed(41);
		weather.SetGroundFunction(ground);
		weather.SetPreset(GB::WeatherPreset::Clear);
		if (shot.Weather == Shot::Sky::Rain) { weather.SetRain(0.8f); weather.SetWind({ 3.0f, 0.0f, 1.0f }); }   // (no storm: no lightning)
		if (shot.Weather == Shot::Sky::Snow) weather.SetPreset(GB::WeatherPreset::Snowfall);
		weather.SetBase(sc);
		weather.Snap();
		for (int i = 0; i < 150; ++i) weather.Update(1.0f / 60.0f, cam);   // 2.5 s: the rain and the snow fill the view
		weather.Apply(sc);
	};

	if (!bench)
	{
		const GB::Qa::Config qa = GB::Qa::Config::FromEnvironment();
		GB::Renderer renderer(W, H, 1, 2048);   // one sample: identical across runs
		GB::Framebuffer fb(W, H);
		for (const Shot& shot : kShots)
		{
			GB::Scene sc;
			GB::Weather weather;
			makeScene(shot, sc, weather);
			renderer.Render(sc, cam, &fb);
			GB::Image img;
			img.Width = W; img.Height = H; img.Pixels = fb.ReadPixels();
			Report(GB::Qa::CheckGoldenImage(qa, GB_GOLDEN_DIR "/images/reference", shot.Name, img, { 1.0, 0.002, 16 }));
		}
		return failures;
	}

	// ------------------------------------------------------------------ bench: the cost per pass at 1080p, `medium`
	int samples = 30;
	if (const char* n = std::getenv("GB_REFERENCE_SAMPLES")) samples = std::max(2, std::atoi(n));
	constexpr uint32_t BW = 1920, BH = 1080;
	GB::Renderer renderer(BW, BH, 4, 2048);
	GB::Framebuffer fb(BW, BH);
	std::printf("reference scene %ux%u, MSAA 4, shadow map 2048, SSAO 0.4 (medium); %d samples per shot (mean +- sd, ms)\n", BW, BH, samples);
	std::printf("shot | GPU total | shadows | objects (sky) | grass | water | translucent | particles | resolve | bloom | ssao | composite | CPU build | CPU send | draw calls | triangles\n");
	for (const Shot& shot : kShots)
	{
		GB::Scene sc;
		GB::Weather weather;
		makeScene(shot, sc, weather);
		Stat total, shadow, scene, sky, grassMs, water, trans, parts, resolve, bloom, ao, comp, build, send;
		for (int i = 0; i < samples + 10; ++i)
		{
			// the scene built again every frame, as the editor does (the static part, the weather's particles stepped)
			const auto t0 = std::chrono::steady_clock::now();
			GB::Scene frame;
			frame.Env = sc.Env; frame.Sun = sc.Sun; frame.Post = sc.Post; frame.PointLights = sc.PointLights;
			addStatic(frame);
			weather.Update(1.0f / 60.0f, cam);
			weather.SetBase(frame);
			weather.Apply(frame);
			frame.Env = sc.Env;   // (the base was already weathered: keep the shot's environment)
			frame.Sun = sc.Sun;
			const auto t1 = std::chrono::steady_clock::now();
			renderer.Render(frame, cam, &fb);
			const auto t2 = std::chrono::steady_clock::now();
			glFinish();   // one frame at a time: every sample is a timer set of its own, read on the next frame
			if (i < 10) continue;   // warm-up (the caches, the driver's shader compiles)
			const GB::RenderStats& s = renderer.GetStats();
			total.Add(s.GpuTotalMs); shadow.Add(s.GpuShadowMs); scene.Add(s.GpuSceneMs); sky.Add(s.GpuSkyMs); grassMs.Add(s.GpuGrassMs); water.Add(s.GpuWaterMs);
			trans.Add(s.GpuTranslucentMs); parts.Add(s.GpuParticlesMs); resolve.Add(s.GpuResolveMs); bloom.Add(s.GpuBloomMs); ao.Add(s.GpuSsaoMs); comp.Add(s.GpuCompositeMs);
			build.Add(std::chrono::duration<double, std::milli>(t1 - t0).count());
			send.Add(std::chrono::duration<double, std::milli>(t2 - t1).count());
		}
		auto f = [](const Stat& st) { static char b[8][32]; static int k = 0; k = (k + 1) % 8; std::snprintf(b[k], sizeof b[k], "%.2f+-%.2f", st.Mean(), st.Dev()); return b[k]; };
		std::printf("%s | %s | %s | %s", shot.Name, f(total), f(shadow), f(scene));
		std::printf(" (%s) | %s | %s", f(sky), f(grassMs), f(water));
		std::printf(" | %s | %s | %s", f(trans), f(parts), f(resolve));
		std::printf(" | %s | %s | %s", f(bloom), f(ao), f(comp));
		std::printf(" | %s | %s | %u | %.2f M\n", f(build), f(send), renderer.GetStats().DrawCalls, static_cast<double>(renderer.GetStats().Triangles) * 1e-6);
	}
	return 0;
}
