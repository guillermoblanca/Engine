#include "GB.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

// Six scenes, picked with --scene=pbr (default), --scene=textures, --scene=model, --scene=particles, --scene=weather
// or --scene=desert:
//   desert   procedural dune field with wind ripples, sand blowing off the crests and a dust devil. Weather applies
//            on top: --weather=clear|sandstorm|storm|snow|... .  Options: --terrain=dunes|hills --style=desert|snow|meadow
//            --seed=N --wind-angle=DEG --wavelength=M --height=M --nospray --simulate=S.
//            --walker=cesium|fox walks a circle and leaves footprints (--walker-speed=M/S --walker-yaw=DEG).
//   weather  a small town square under changing weather. --weather=clear|lightrain|rain|storm|snow|blizzard|
//            dust|sandstorm|tornado (default storm), --simulate=SECONDS warm-up (default 8), --strike (a
//            lightning bolt at the start), --nosnap (ease in instead of starting at full intensity).
//            Keys 1-9 pick the same presets in that order, L = lightning strike.
//   particles night campfire: fire, smoke, sparks, flickering light, snow, periodic explosion.
//            --simulate=SECONDS (warm-up before the first frame, default 3), --explosion-every=SECONDS
//            (default 4, 0 = off), --nosnow.  Key B: explosion.
//   model    loads a glTF/GLB (e.g. exported from Blender): --model=path.glb (default: DamagedHelmet),
//            --nofloor, --yaw=DEG initial camera yaw, --anim=NAME|INDEX, --time=SECONDS (freeze there),
//            --speed=X, --noanim, --playlist=A,B,C --switch=SECONDS (cycle clips with cross-fade),
//            --orbit=DEG_PER_SEC (camera turns by itself).
//   Cameras (all scenes): C cycles orbit / free fly / the glTF file's cameras with a smooth transition;
//            --camera=orbit|fly|N (N = glTF camera index), --size=WxH.
//            Orbit: mouse left-drag rotates, right-drag pans, wheel zooms, arrows/WASD, Q/E, right stick.
//            Fly: WASD + Q/E, Shift = fast, right mouse look, wheel = speed, sticks.  Keys: 1-9 switch animation (cross-fade), Space pause/resume
//   pbr      roughness sweep of metal and dielectric spheres under sun, sky and point lights.
//   textures 4x3 material gallery on subdivided quads with normal mapping + height displacement.
//            Extra options: --tile=N (0-11, single large tile) --grid=N (subdivisions)
//            --height=H (displacement) --flipy / --noflipx (normal map axes) --nodisplace
class Sandbox : public GB::Application
{
public:
	Sandbox() : GB::Application(MakeConfig())
	{
		if (!IsValid())
			return;

		m_Renderer = std::make_unique<GB::Renderer>(GetWindow()->GetWidth(), GetWindow()->GetHeight());
		if (!m_Renderer->IsValid())
		{
			GB_CLIENT_ERROR("Renderer could not be created");
			Close();
			return;
		}
		const GB::LaunchOptions& options = GB::GetLaunchOptions();
		// Defaults for the PBR showcase; the other scenes override them.
		m_Orbit.Target = { 0.0f, 1.3f, 0.0f };
		m_Orbit.Pitch = 0.22f;
		m_Orbit.Distance = 13.0f;
		m_Orbit.MinDistance = 3.0f;
		m_Orbit.MaxDistance = 30.0f;
		const std::string sceneName = options.GetValue("scene", "pbr");
		if (sceneName == "textures")
			BuildTextureScene(options);
		else if (sceneName == "model")
			BuildModelScene(options);
		else if (sceneName == "particles")
			BuildParticleScene(options);
		else if (sceneName == "weather")
			BuildWeatherScene(options);
		else if (sceneName == "desert")
			BuildDesertScene(options);
		else
			BuildScene();

		m_LastCamera = BuildCamera(0);
		const std::string cameraArg = options.GetValue("camera", "orbit");
		if (cameraArg == "fly")
			SwitchCamera(1, false);
		else if (cameraArg != "orbit")
		{
			const int mode = 2 + std::atoi(cameraArg.c_str());
			if (mode < CameraModeCount())
				SwitchCamera(mode, false);
			else
				GB_CLIENT_WARN("Unknown camera '{}'", cameraArg);
		}
	}

protected:
	void OnUpdate(float dt) override
	{
		using GB::Key;
		const bool cameraKey = GB::Input::IsKeyDown(Key::C) || GB::Input::IsGamepadButtonDown(GB::GamepadButton::Y);
		if (cameraKey && !m_CameraKeyWasDown)
			SwitchCamera((m_CameraMode + 1) % CameraModeCount(), true);
		m_CameraKeyWasDown = cameraKey;

		if (m_Weather)
		{
			static const char* names[9] = { "clear", "lightrain", "rain", "storm", "snow", "blizzard", "dust", "sandstorm", "tornado" };
			for (int i = 0; i < 9; ++i)
			{
				const bool down = GB::Input::IsKeyDown(static_cast<Key>(static_cast<int>(Key::D1) + i));
				if (down && !m_WeatherKeyWasDown[i])
					ApplyWeatherPreset(names[i], false);
				m_WeatherKeyWasDown[i] = down;
			}
			const bool strike = GB::Input::IsKeyDown(Key::L);
			if (strike && !m_StrikeKeyWasDown)
				m_Weather->GetLightning().Strike();
			m_StrikeKeyWasDown = strike;
			if (m_Terrain)
				UpdateDesert(dt);
			else
				UpdateWeather(dt);
		}
		if (!m_Particles.empty() && !m_Terrain)   // (the desert scene updates its own particle systems)
		{
			UpdateParticles(dt);
			const bool boom = GB::Input::IsKeyDown(Key::B);
			if (boom && !m_BoomKeyWasDown && m_Explosion)
				m_Explosion->Burst(110);
			m_BoomKeyWasDown = boom;
		}

		if (m_CameraMode == 0)
			m_Orbit.Update(dt);
		else if (m_CameraMode == 1)
			m_Fly.Update(dt);
		if (m_BlendT < 1.0f)
			m_BlendT = std::min(1.0f, m_BlendT + dt / 0.6f);

		if (m_Instance)
		{
			const bool space = GB::Input::IsKeyDown(Key::Space) || GB::Input::IsGamepadButtonDown(GB::GamepadButton::A);
			if (space && !m_SpaceWasDown)
				m_Instance->SetPaused(!m_Instance->IsPaused());
			m_SpaceWasDown = space;
			for (int i = 0; i < 9; ++i)
				if (GB::Input::IsKeyDown(static_cast<Key>(static_cast<int>(Key::D1) + i)))
					m_Instance->Play(i, true, 0.3f);
			if (m_Playlist.size() > 1 && !m_Instance->IsPaused())
			{
				m_PlaylistTimer += dt;
				if (m_PlaylistTimer >= m_SwitchSeconds)
				{
					m_PlaylistTimer = 0.0f;
					m_PlaylistIndex = (m_PlaylistIndex + 1) % m_Playlist.size();
					m_Instance->Play(m_Playlist[m_PlaylistIndex], true, 0.3f);
				}
			}
			m_Instance->Update(dt);
		}
		if (m_CameraMode == 0)
			m_Orbit.Yaw += m_OrbitSpeed * dt;

		if (GB::Input::IsKeyDown(Key::Escape) || GB::Input::IsGamepadButtonDown(GB::GamepadButton::Back))
			Close();
	}

	void OnRender() override
	{
		GB::Camera camera = BuildCamera(m_CameraMode);
		if (m_BlendT < 1.0f)
		{
			const float t = m_BlendT * m_BlendT * (3.0f - 2.0f * m_BlendT);   // smoothstep
			camera = GB::Camera::Blend(m_BlendFrom, camera, t);
		}
		m_LastCamera = camera;
		m_Renderer->Resize(GetWindow()->GetWidth(), GetWindow()->GetHeight());
		if (m_Instance)
		{
			m_Scene.Objects.resize(m_StaticObjects);
			m_Instance->AddToScene(m_Scene);
		}
		else if (m_Walker)
		{
			m_Scene.Objects.resize(m_StaticObjects);
			m_Walker->Instance->AddToScene(m_Scene, m_Walker->Root);
		}
		m_Renderer->Render(m_Scene, camera);
	}

private:
	// Camera modes: 0 = orbit, 1 = free fly, 2.. = cameras defined in the glTF file (animated with it).
	int CameraModeCount() const { return 2 + (m_Instance ? static_cast<int>(m_Model->GetCameras().size()) : 0); }

	GB::Camera BuildCamera(int mode) const
	{
		GB::Camera camera;
		if (mode >= 2 && m_Instance && m_Instance->GetCamera(mode - 2, camera))
			return camera;   // the file's own lens and clip planes
		if (mode == 1)
			m_Fly.Apply(camera);
		else
			m_Orbit.Apply(camera);
		// Scale the clip planes with the viewing distance so tiny and huge models both fit.
		camera.NearPlane = m_Orbit.Distance * 0.01f;
		camera.FarPlane = m_Orbit.Distance * 500.0f;
		return camera;
	}

	void SwitchCamera(int mode, bool blend)
	{
		if (mode == m_CameraMode)
			return;
		if (blend)
		{
			m_BlendFrom = m_LastCamera;
			m_BlendT = 0.0f;
		}
		if (mode == 1)   // continue from wherever the view currently is
		{
			m_Fly.Position = m_LastCamera.Position;
			m_Fly.LookAt(m_LastCamera.Target);
			m_Fly.Speed = std::max(m_Orbit.Distance * 0.5f, 0.1f);
		}
		m_CameraMode = mode;
		GB_CLIENT_INFO("Camera: {}", mode == 0 ? "orbit" : mode == 1 ? "free fly"
			: "glTF '" + m_Model->GetCameras()[mode - 2].Name + "'");
	}

	static GB::ApplicationConfig MakeConfig()
	{
		GB::ApplicationConfig config;
		config.Title = "GB Sandbox - PBR";
		config.Width = 1600;
		config.Height = 900;
		unsigned w = 0, h = 0;
		if (std::sscanf(GB::GetLaunchOptions().GetValue("size").c_str(), "%ux%u", &w, &h) == 2 && w > 0 && h > 0)
		{
			config.Width = w;
			config.Height = h;
		}
		config.CreateWindow = true;
		return config;
	}

	static GB::Image LoadAtlas(const char* name)
	{
		return GB::Image::Load(std::string(GB_ASSET_DIR) + "/textures/" + name, true);
	}

	void BuildTextureScene(const GB::LaunchOptions& options)
	{
		// The atlases have ~3 px black gutters between (and around) tiles, so each tile is cropped
		// with a margin; otherwise the gutter shows up as dark seams when the texture repeats.
		constexpr uint32_t cols = 4, rows = 3, cell = 128, margin = 4, tile = cell - 2 * margin;
		const GB::Image albedo = LoadAtlas("atlas_albedo.jpg");
		const GB::Image normal = LoadAtlas("atlas_normal.jpg");
		const GB::Image height = LoadAtlas("atlas_height.jpg");
		if (!albedo.IsValid() || !normal.IsValid() || !height.IsValid())
		{
			GB_CLIENT_ERROR("Could not load the texture atlases from {}/textures", GB_ASSET_DIR);
			Close();
			return;
		}

		// Per-tile roughness (the atlas has no roughness map): glazed tile and varnished parquet are smoother.
		const float roughness[cols * rows] = {
			0.90f, 0.30f, 0.85f, 0.70f,
			0.80f, 0.45f, 0.90f, 0.18f,
			0.90f, 0.95f, 0.85f, 0.60f };

		const int single = std::atoi(options.GetValue("tile", "-1").c_str());
		const uint32_t grid = static_cast<uint32_t>(std::max(2, std::atoi(options.GetValue("grid", single >= 0 ? "320" : "160").c_str())));
		const float heightScale = options.HasFlag("nodisplace") ? 0.0f
			: static_cast<float>(std::atof(options.GetValue("height", "0.12").c_str()));
		const float size = single >= 0 ? 5.0f : 2.0f;
		m_Plane = std::make_unique<GB::Mesh>(GB::Mesh::CreateGrid(size, grid));
		GB_CLIENT_INFO("Texture scene: {} triangles per quad, displacement {}", grid * grid * 2, heightScale);

		for (uint32_t r = 0; r < rows; ++r)
		{
			for (uint32_t c = 0; c < cols; ++c)
			{
				const uint32_t index = r * cols + c;
				if (single >= 0 && static_cast<uint32_t>(single) != index)
					continue;
				const uint32_t x = c * cell + margin;
				const uint32_t y = albedo.Height - (r + 1) * cell + margin;   // images were loaded bottom-up
				m_Textures.push_back(std::make_unique<GB::Texture2D>(albedo.Crop(x, y, tile, tile), GB::TextureEncoding::SRGB));
				m_Textures.push_back(std::make_unique<GB::Texture2D>(normal.Crop(x, y, tile, tile), GB::TextureEncoding::Linear));
				m_Textures.push_back(std::make_unique<GB::Texture2D>(height.Crop(x, y, tile, tile), GB::TextureEncoding::Linear));

				GB::RenderObject o;
				o.MeshPtr = m_Plane.get();
				o.Mat.Albedo = glm::vec3(1.0f);
				o.Mat.Roughness = roughness[index];
				o.Mat.AlbedoMap = m_Textures[m_Textures.size() - 3].get();
				o.Mat.NormalMap = m_Textures[m_Textures.size() - 2].get();
				o.Mat.HeightMap = m_Textures[m_Textures.size() - 1].get();
				o.Mat.UVScale = 2.0f;
				o.Mat.HeightScale = heightScale;
				// Measured against the atlas' height map: Y is OpenGL-style but the red channel is inverted.
				o.Mat.NormalFlipX = !options.HasFlag("noflipx");
				o.Mat.NormalFlipY = options.HasFlag("flipy");

				const glm::vec3 pos = single >= 0 ? glm::vec3(0.0f)
					: glm::vec3((static_cast<float>(c) - 1.5f) * 2.1f, (1.0f - static_cast<float>(r)) * 2.1f, 0.0f);
				// Grid lies in XZ (normal +Y); stand it up so it faces the camera (+Z).
				o.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), pos), glm::radians(90.0f), glm::vec3(1, 0, 0));
				m_Scene.Objects.push_back(o);
			}
		}

		// Grazing light from the upper left makes relief and displacement shadows readable.
		m_Scene.Sun.Direction = { 0.75f, -0.5f, -0.65f };
		m_Scene.Sun.Intensity = 4.5f;
		m_Scene.Env.Intensity = 0.55f;
		m_Scene.Env.Exposure = 1.0f;
		m_Scene.Env.BloomIntensity = 0.15f;
		m_Scene.Env.ShadowCenter = { 0.0f, 0.0f, 0.0f };
		m_Scene.Env.ShadowRadius = single >= 0 ? 4.0f : 8.0f;
		m_Orbit.Target = { 0.0f, 0.0f, 0.0f };
		m_Orbit.Pitch = 0.0f;
		m_Orbit.Distance = single >= 0 ? 6.0f : 10.5f;
	}

	void BuildParticleScene(const GB::LaunchOptions& options)
	{
		m_Sphere = std::make_unique<GB::Mesh>(GB::Mesh::CreateSphere(0.5f, 24, 12));
		m_Plane = std::make_unique<GB::Mesh>(GB::Mesh::CreatePlane(80.0f));
		m_Cube = std::make_unique<GB::Mesh>(GB::Mesh::CreateCube(1.0f));

		auto add = [&](const GB::Mesh* mesh, glm::vec3 pos, glm::vec3 scale, float yawDeg, GB::Material mat)
		{
			GB::RenderObject o;
			o.MeshPtr = mesh;
			mat.HeightScale = 0.0f;
			mat.CavityStrength = 0.0f;
			o.Mat = mat;
			o.Transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), pos), glm::radians(yawDeg), glm::vec3(0, 1, 0)), scale);
			m_Scene.Objects.push_back(o);
		};

		GB::Material ground;
		ground.Albedo = { 0.16f, 0.15f, 0.14f };
		ground.Roughness = 0.9f;
		add(m_Plane.get(), { 0, 0, 0 }, glm::vec3(1.0f), 0.0f, ground);

		GB::Material stone;
		stone.Albedo = { 0.3f, 0.3f, 0.32f };
		stone.Roughness = 0.85f;
		for (int i = 0; i < 9; ++i)   // ring of stones around the fire
		{
			const float a = glm::radians(40.0f * static_cast<float>(i));
			add(m_Sphere.get(), { 0.85f * std::cos(a), 0.12f, 0.85f * std::sin(a) }, { 0.38f, 0.26f, 0.34f }, 0.0f, stone);
		}
		GB::Material wood;
		wood.Albedo = { 0.22f, 0.12f, 0.06f };
		wood.Roughness = 0.8f;
		add(m_Cube.get(), { 0.0f, 0.1f, 0.0f }, { 0.12f, 0.12f, 0.9f }, 30.0f, wood);
		add(m_Cube.get(), { 0.0f, 0.1f, 0.0f }, { 0.12f, 0.12f, 0.9f }, -35.0f, wood);
		add(m_Cube.get(), { 0.0f, 0.22f, 0.0f }, { 0.12f, 0.12f, 0.8f }, 95.0f, wood);

		auto system = [&](GB::ParticleEmitterDesc desc, glm::vec3 position)
		{
			m_Particles.push_back(std::make_unique<GB::ParticleSystem>(desc));
			m_Particles.back()->SetPosition(position);
			m_Scene.Particles.push_back(m_Particles.back().get());
			return m_Particles.back().get();
		};
		system(GB::ParticlePresets::Smoke(), { 0.0f, 1.0f, 0.0f });
		system(GB::ParticlePresets::Fire(), { 0.0f, 0.18f, 0.0f });
		system(GB::ParticlePresets::Sparks(), { 0.0f, 0.45f, 0.0f });
		GB::ParticleEmitterDesc magic = GB::ParticlePresets::Magic();
		magic.Radius = 0.35f;
		system(magic, { 2.6f, 1.1f, -0.5f });
		if (!options.HasFlag("nosnow"))
			system(GB::ParticlePresets::Snow(), { 0.0f, 6.0f, 0.0f });
		GB::ParticleEmitterDesc boom = GB::ParticlePresets::Explosion();
		boom.Looping = true;     // restarted by us; Looping keeps it "alive" so IsFinished stays false
		boom.Bursts.clear();
		boom.Rate = 0.0f;
		m_Explosion = system(boom, { -3.2f, 0.5f, 0.8f });
		m_ExplosionEvery = static_cast<float>(std::atof(options.GetValue("explosion-every", "4").c_str()));
		m_ExplosionTimer = m_ExplosionEvery > 0.0f ? m_ExplosionEvery * 0.5f : 0.0f;
		m_FireLightBase = 9.0f;
		m_Scene.PointLights.push_back({ { 0.0f, 0.7f, 0.0f }, { 1.0f, 0.55f, 0.22f }, m_FireLightBase });

		// Night: dim cool moonlight and a dark sky; the fire is the main source and drives the bloom.
		m_Scene.Sun.Direction = { -0.4f, -0.8f, -0.35f };
		m_Scene.Sun.Color = { 0.55f, 0.65f, 1.0f };
		m_Scene.Sun.Intensity = 0.35f;
		m_Scene.Env.SkyTop = { 0.005f, 0.01f, 0.035f };
		m_Scene.Env.SkyHorizon = { 0.03f, 0.04f, 0.08f };
		m_Scene.Env.SkyGround = { 0.01f, 0.01f, 0.012f };
		m_Scene.Env.Intensity = 0.7f;
		m_Scene.Env.Exposure = 1.1f;
		m_Scene.Env.BloomThreshold = 1.0f;
		m_Scene.Env.BloomIntensity = 0.7f;
		m_Scene.Env.ShadowCenter = { 0.0f, 0.0f, 0.0f };
		m_Scene.Env.ShadowRadius = 8.0f;
		m_Orbit.Target = { 0.0f, 1.0f, 0.0f };
		m_Orbit.Pitch = 0.2f;
		m_Orbit.Yaw = 0.35f;
		m_Orbit.Distance = 6.5f;
		m_Orbit.MinDistance = 1.5f;
		m_Orbit.MaxDistance = 30.0f;

		// Warm up so the first frame (and screenshots) show a steady state.
		const float warmup = static_cast<float>(std::atof(options.GetValue("simulate", "3").c_str()));
		for (float t = 0.0f; t < warmup; t += 1.0f / 60.0f)
			UpdateParticles(1.0f / 60.0f);
	}

	void BuildWeatherScene(const GB::LaunchOptions& options)
	{
		m_Sphere = std::make_unique<GB::Mesh>(GB::Mesh::CreateSphere(0.5f, 24, 12));
		m_Plane = std::make_unique<GB::Mesh>(GB::Mesh::CreatePlane(1200.0f));
		m_Cube = std::make_unique<GB::Mesh>(GB::Mesh::CreateCube(1.0f));

		auto add = [&](const GB::Mesh* mesh, glm::vec3 pos, glm::vec3 scale, GB::Material mat)
		{
			GB::RenderObject o;
			o.MeshPtr = mesh;
			mat.HeightScale = 0.0f;
			mat.CavityStrength = 0.0f;
			o.Mat = mat;
			o.Transform = glm::scale(glm::translate(glm::mat4(1.0f), pos), scale);
			m_Scene.Objects.push_back(o);
		};

		GB::Material road;
		road.Albedo = { 0.22f, 0.22f, 0.24f };
		road.Roughness = 0.75f;
		add(m_Plane.get(), { 0, 0, 0 }, glm::vec3(1.0f), road);

		// Buildings around a square, in a loose ring, and a few lamp posts with lights.
		const glm::vec3 wall[3] = { { 0.55f, 0.42f, 0.35f }, { 0.62f, 0.60f, 0.55f }, { 0.40f, 0.44f, 0.52f } };
		for (int i = 0; i < 26; ++i)
		{
			const float a = glm::radians(360.0f * static_cast<float>(i) / 26.0f + 7.0f * static_cast<float>(i % 3));
			const float r = 34.0f + 14.0f * static_cast<float>((i * 7) % 5);
			const float h = 6.0f + 3.0f * static_cast<float>((i * 5) % 7);
			GB::Material m;
			m.Albedo = wall[i % 3];
			m.Roughness = 0.85f;
			add(m_Cube.get(), { std::cos(a) * r, h * 0.5f, std::sin(a) * r }, { 9.0f, h, 9.0f }, m);
		}
		GB::Material metal;
		metal.Albedo = { 0.12f, 0.12f, 0.13f };
		metal.Metallic = 0.8f;
		metal.Roughness = 0.4f;
		const glm::vec3 lamps[3] = { { -6.0f, 0.0f, 4.0f }, { 7.0f, 0.0f, -3.0f }, { -1.0f, 0.0f, -14.0f } };
		for (const glm::vec3& l : lamps)
		{
			add(m_Cube.get(), l + glm::vec3(0.0f, 2.6f, 0.0f), { 0.16f, 5.2f, 0.16f }, metal);
			GB::Material bulb;
			bulb.Albedo = { 0.1f, 0.1f, 0.1f };
			bulb.Emissive = { 9.0f, 6.0f, 2.8f };
			add(m_Sphere.get(), l + glm::vec3(0.0f, 5.3f, 0.0f), glm::vec3(0.45f), bulb);
			m_Scene.PointLights.push_back({ l + glm::vec3(0.0f, 5.0f, 0.0f), { 1.0f, 0.68f, 0.35f }, 45.0f });
		}
		GB::Material stone;
		stone.Albedo = { 0.5f, 0.5f, 0.52f };
		stone.Roughness = 0.9f;
		for (int i = 0; i < 12; ++i)
			add(m_Sphere.get(), { -10.0f + 2.1f * static_cast<float>(i), 0.3f, 9.0f - 0.5f * static_cast<float>(i % 3) }, glm::vec3(0.9f, 0.6f, 0.8f), stone);

		m_Scene.Sun.Direction = { -0.45f, -0.7f, -0.55f };
		m_Scene.Sun.Intensity = 3.5f;
		m_Scene.Env.Intensity = 0.9f;
		m_Scene.Env.ShadowCenter = { 0.0f, 0.0f, 0.0f };
		m_Scene.Env.ShadowRadius = 22.0f;
		m_Scene.Env.BloomThreshold = 1.3f;
		m_Scene.Env.BloomIntensity = 0.5f;

		m_Orbit.Target = { 0.0f, 2.5f, -4.0f };
		m_Orbit.Yaw = 0.35f;
		m_Orbit.Pitch = 0.12f;
		m_Orbit.Distance = 16.0f;
		m_Orbit.MinDistance = 3.0f;
		m_Orbit.MaxDistance = 90.0f;

		m_Weather = std::make_unique<GB::Weather>(3);
		m_Weather->SetBase(m_Scene);
		m_WeatherSteady = options.HasFlag("nosnap") ? false : true;
		const std::string weatherName = options.GetValue("weather", "storm");
		ApplyWeatherPreset(weatherName, m_WeatherSteady);
		if (weatherName == "tornado")   // frame the whole funnel from a distance
		{
			m_Orbit.Target = { 0.0f, 17.0f, -40.0f };
			m_Orbit.Distance = 80.0f;
			m_Orbit.Pitch = 0.06f;
			m_Orbit.Yaw = 0.0f;
			m_TornadoSwing = 5.0f;
		}

		m_LastCamera = BuildCamera(0);
		const float warmup = static_cast<float>(std::atof(options.GetValue("simulate", "8").c_str()));
		for (float t = 0.0f; t < warmup; t += 1.0f / 60.0f)
			UpdateWeather(1.0f / 60.0f);
		if (options.HasFlag("strike"))
		{
			const glm::vec3 target(-9.0f, 0.0f, -26.0f);
			m_Weather->GetLightning().Strike(&target);
			UpdateWeather(0.02f);
		}
	}

	void BuildDesertScene(const GB::LaunchOptions& options)
	{
		m_Sphere = std::make_unique<GB::Mesh>(GB::Mesh::CreateSphere(0.5f, 20, 10));
		m_Cube = std::make_unique<GB::Mesh>(GB::Mesh::CreateCube(1.0f));

		const std::string kind = options.GetValue("terrain", "dunes");
		const std::string styleName = options.GetValue("style", kind == "dunes" ? "desert" : "meadow");
		const GB::TerrainStyle style = styleName == "snow" ? GB::TerrainStyle::Snow : styleName == "meadow" ? GB::TerrainStyle::Meadow : GB::TerrainStyle::Desert;
		const uint32_t seed = static_cast<uint32_t>(std::atoi(options.GetValue("seed", "1").c_str()));
		if (kind == "hills")
		{
			GB::HillParams hp;
			hp.Seed = seed;
			hp.Height = static_cast<float>(std::atof(options.GetValue("height", "16").c_str()));
			m_Terrain = GB::Terrain::CreateHills(hp, 360.0f, 641, style);
		}
		else
		{
			GB::DuneParams dp;
			dp.Seed = seed;
			dp.WindAngleDegrees = static_cast<float>(std::atof(options.GetValue("wind-angle", "28").c_str()));
			dp.Wavelength = static_cast<float>(std::atof(options.GetValue("wavelength", "58").c_str()));
			dp.Height = static_cast<float>(std::atof(options.GetValue("height", "7.5").c_str()));
			m_Terrain = GB::Terrain::CreateDunes(dp, 360.0f, 641, style);
		}
		m_Terrain->AddToScene(m_Scene);
		GB_CLIENT_INFO("Terrain: {} ({}), height range {:.1f} .. {:.1f} m, {} crest points", kind, styleName,
			m_Terrain->GetHeightmap().GetMinHeight(), m_Terrain->GetHeightmap().GetMaxHeight(), m_Terrain->GetCrestPoints().size());

		// A few boulders and a ruin for scale.
		GB::Material rock;
		rock.Albedo = style == GB::TerrainStyle::Snow ? glm::vec3(0.42f, 0.41f, 0.40f) : glm::vec3(0.27f, 0.19f, 0.13f);
		rock.Roughness = 0.92f;
		rock.HeightScale = 0.0f;
		rock.CavityStrength = 0.0f;
		auto place = [&](const GB::Mesh* mesh, float x, float z, glm::vec3 scale, float yawDeg, float sink)
		{
			GB::RenderObject o;
			o.MeshPtr = mesh;
			o.Mat = rock;
			const float y = m_Terrain->GetHeight(x, z) - sink;
			o.Transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), { x, y, z }), glm::radians(yawDeg), glm::vec3(0, 1, 0)), scale);
			m_Scene.Objects.push_back(o);
		};
		uint32_t rng = 12345u + seed;
		auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<float>(rng >> 8) / 16777216.0f; };
		for (int i = 0; i < 26; ++i)
		{
			const float s = 0.6f + 2.0f * rnd();
			place(m_Sphere.get(), (rnd() - 0.5f) * 220.0f, (rnd() - 0.5f) * 220.0f, { s * 1.7f, s * 1.1f, s * 1.3f }, rnd() * 360.0f, s * 0.12f);
		}
		for (int i = 0; i < 3; ++i)   // a ruined obelisk group
			place(m_Cube.get(), 14.0f + 2.5f * static_cast<float>(i), -22.0f + 1.5f * static_cast<float>(i), { 1.2f, 6.0f - 1.6f * static_cast<float>(i), 1.2f }, 12.0f * static_cast<float>(i), 1.0f);

		if (kind == "hills" && style == GB::TerrainStyle::Meadow && options.GetValue("grass", "on") != "none")
		{
			GB::GrassParams gp;
			gp.Seed = seed;
			gp.Size = 190.0f;
			gp.HeightFn = m_Terrain->MakeGroundFunction();
			const GB::Heightmap* hm = &m_Terrain->GetHeightmap();
			gp.NormalFn = [hm](float x, float z) { return hm->GetNormal(x, z); };
			gp.Density = static_cast<float>(std::atof(options.GetValue("grass-density", "45").c_str()));
			m_Grass = std::make_unique<GB::GrassField>(gp);
			m_Scene.Grass.push_back(m_Grass.get());
			GB_CLIENT_INFO("Grass: {} blades in {} chunks", m_Grass->GetBladeCount(), m_Grass->GetChunks().size());
		}

		m_SandSpray = std::make_unique<GB::SandSpray>(*m_Terrain, 4);
		if (!options.HasFlag("nospray") && kind == "dunes" && style == GB::TerrainStyle::Desert)
			m_SandSpray->AddToScene(m_Scene);
		else
			m_SandSpray.reset();

		GB::ParticleEmitterDesc devil = GB::ParticlePresets::DustDevil();
		devil.CollideGround = true;
		devil.GroundHeightFn = m_Terrain->MakeGroundFunction();
		m_Particles.push_back(std::make_unique<GB::ParticleSystem>(devil));
		m_Scene.Particles.push_back(m_Particles.back().get());
		m_DustDevilIndex = 0;

		// Warm low sun, hazy sky.
		// Raking sun along the wind: windward slopes lit, lee faces in long shadows.
		const float windAngleRad = m_Terrain->GetWindAngle();
		const float elevation = glm::radians(static_cast<float>(std::atof(options.GetValue("sun", "24").c_str())));
		m_Scene.Sun.Direction = { std::cos(windAngleRad) * std::cos(elevation), -std::sin(elevation), std::sin(windAngleRad) * std::cos(elevation) };
		m_Scene.Sun.Color = { 1.0f, 0.86f, 0.66f };
		m_Scene.Sun.Intensity = 5.5f;
		m_Scene.Env.SkyTop = { 0.14f, 0.30f, 0.62f };
		m_Scene.Env.SkyHorizon = { 0.80f, 0.72f, 0.60f };
		m_Scene.Env.SkyGround = { 0.30f, 0.24f, 0.18f };
		m_Scene.Env.Intensity = 0.85f;
		m_Scene.Env.FogColor = { 0.82f, 0.72f, 0.58f };
		m_Scene.Env.FogDensity = 0.0035f;
		m_Scene.Env.FogHeightFalloff = 0.02f;
		m_Scene.Env.ShadowRadius = 70.0f;
		m_Scene.Env.BloomThreshold = 1.4f;
		m_Scene.Env.BloomIntensity = 0.35f;
		m_Scene.Env.Exposure = 1.0f;

		const float cx = 0.0f, cz = 0.0f;
		m_Orbit.Target = { cx, m_Terrain->GetHeight(cx, cz) + 2.5f, cz };
		// View across the dunes (perpendicular to the wind) from above, so the profiles are visible.
		m_Orbit.Yaw = std::atan2(-std::sin(windAngleRad), std::cos(windAngleRad));
		m_Orbit.Pitch = 0.2f;
		m_Orbit.Distance = 80.0f;
		m_Orbit.MinDistance = 3.0f;
		m_Orbit.MaxDistance = 200.0f;
		m_Scene.Env.ShadowCenter = m_Orbit.Target;

		// Trees: species depend on the style (oaks and pines in meadows, pines and bare trees in snow, palms in the desert).
		{
			const int defaultTrees = style == GB::TerrainStyle::Meadow ? 38 : style == GB::TerrainStyle::Snow ? 26 : 0;
			const int treeCount = std::atoi(options.GetValue("trees", std::to_string(defaultTrees)).c_str());
			if (treeCount > 0)
			{
				m_TreeFactory = std::make_unique<GB::TreeFactory>();
				std::vector<GB::TreeParams> recipes;
				auto add = [&](GB::TreeSpecies sp, float h, uint32_t sd, glm::vec3 tint = glm::vec3(1.0f)) { GB::TreeParams tp; tp.Species = sp; tp.Height = h; tp.Seed = sd; tp.LeafTint = tint; recipes.push_back(tp); };
				if (style == GB::TerrainStyle::Meadow)
				{
					add(GB::TreeSpecies::Broadleaf, 8.0f, 1); add(GB::TreeSpecies::Broadleaf, 10.0f, 2); add(GB::TreeSpecies::Broadleaf, 7.0f, 3, { 1.15f, 1.0f, 0.7f });
					add(GB::TreeSpecies::Conifer, 11.0f, 4); add(GB::TreeSpecies::Conifer, 9.0f, 5);
				}
				else if (style == GB::TerrainStyle::Snow)
				{
					add(GB::TreeSpecies::Conifer, 10.0f, 1); add(GB::TreeSpecies::Conifer, 12.0f, 2); add(GB::TreeSpecies::Conifer, 8.0f, 3);
					add(GB::TreeSpecies::Bare, 8.0f, 4); add(GB::TreeSpecies::Bare, 6.5f, 5);
				}
				else
				{
					add(GB::TreeSpecies::Palm, 7.5f, 1); add(GB::TreeSpecies::Palm, 9.0f, 2); add(GB::TreeSpecies::Palm, 6.0f, 3);
				}
				for (const GB::TreeParams& tp : recipes)
					m_TreeVariants.push_back(m_TreeFactory->Create(tp));
				const float snowLine = 1e9f;
				const auto placements = GB::ScatterTrees(*m_Terrain, treeCount, static_cast<int>(m_TreeVariants.size()), 7.0f, seed + 11,
					-1e9f, snowLine, 24.0f, [](float x, float z) { return std::hypot(x, z) > 16.0f; });   // keep the middle clear for the walker
				for (const GB::TreePlacement& pl : placements)
					m_TreeVariants[static_cast<size_t>(pl.Variant)]->AddToScene(m_Scene, pl.Position, pl.Yaw, pl.Scale);
				if (options.HasFlag("gallery"))   // every variant in a row, for a close look
				{
					for (size_t i = 0; i < m_TreeVariants.size(); ++i)
					{
						const float x = (static_cast<float>(i) - 0.5f * static_cast<float>(m_TreeVariants.size() - 1)) * 11.0f, z = -24.0f;
						m_TreeVariants[i]->AddToScene(m_Scene, { x, m_Terrain->GetHeight(x, z), z }, 0.0f, 1.0f);
					}
				}
				GB_CLIENT_INFO("Trees: {} placed ({} variants)", placements.size(), m_TreeVariants.size());
			}
		}

		m_StaticObjects = m_Scene.Objects.size();
		const std::string walker = options.GetValue("walker", "none");
		if (walker != "none")
			BuildWalker(walker, options, style);

		if (!options.GetValue("distance").empty())
			m_Orbit.Distance = static_cast<float>(std::atof(options.GetValue("distance").c_str()));
		if (!options.GetValue("pitch").empty())
			m_Orbit.Pitch = static_cast<float>(std::atof(options.GetValue("pitch").c_str()));
		if (!options.GetValue("yaw").empty())
			m_Orbit.Yaw = glm::radians(static_cast<float>(std::atof(options.GetValue("yaw").c_str())));

		m_Weather = std::make_unique<GB::Weather>(5);
		m_Weather->SetGroundFunction(m_Terrain->MakeGroundFunction());
		const float windAngle = m_Terrain->GetWindAngle();
		m_DesertWind = { std::cos(windAngle) * 9.0f, 0.0f, std::sin(windAngle) * 9.0f };
		m_Weather->SetBase(m_Scene);
		ApplyWeatherPreset(options.GetValue("weather", "clear"), true);
		m_Weather->SetWind(m_DesertWind * (m_Weather->GetSandstorm() > 0.5f ? 1.8f : 1.0f));
		m_Weather->SetTornadoPosition({ 60.0f, 0.0f, -70.0f });

		m_LastCamera = BuildCamera(0);
		const float warmup = static_cast<float>(std::atof(options.GetValue("simulate", "6").c_str()));
		for (float t = 0.0f; t < warmup; t += 1.0f / 60.0f)
			UpdateDesert(1.0f / 60.0f);
	}

	// An animated character that walks a circle over the terrain and stamps a footprint wherever a foot lands.
	struct Walker
	{
		std::unique_ptr<GB::Model> Model;
		std::unique_ptr<GB::ModelInstance> Instance;
		std::vector<int> FootNodes;
		std::vector<float> FootBase;           // model-space height of each foot joint when planted
		GB::FootstepTracker Tracker{ 2 };
		float Scale = 1.0f, Speed = 1.4f, Radius = 12.0f, Phase = 0.0f, YawOffset = 0.0f;
		float FootLength = 0.28f, FootWidth = 0.11f;
		glm::vec3 Position{ 0.0f }, Heading{ 0.0f, 0.0f, 1.0f };
		glm::mat4 Root{ 1.0f };
		glm::vec3 DustColor{ 0.78f, 0.62f, 0.42f };
	};

	void BuildWalker(const std::string& kind, const GB::LaunchOptions& options, GB::TerrainStyle style)
	{
		m_Walker = std::make_unique<Walker>();
		Walker& w = *m_Walker;
		std::vector<std::string> feet;
		float height = 1.75f;
		std::string clip = "";
		if (kind == "fox")
		{
			w.Model = GB::Model::Load(std::string(GB_ASSET_DIR) + "/models/Fox.glb");
			feet = { "b_LeftFoot02_018", "b_RightFoot02_022", "b_LeftHand_011", "b_RightHand_08" };
			height = 0.75f; w.FootLength = 0.10f; w.FootWidth = 0.07f; w.Speed = 1.8f; clip = "Walk";
		}
		else
		{
			w.Model = GB::Model::Load(std::string(GB_ASSET_DIR) + "/models/CesiumMan.glb");
			feet = { "leg_joint_L_5", "leg_joint_R_5" };
		}
		if (!w.Model)
		{
			GB_CLIENT_ERROR("Could not load the walker model");
			m_Walker.reset();
			return;
		}
		w.Instance = std::make_unique<GB::ModelInstance>(*w.Model);
		const int clipIndex = clip.empty() ? 0 : w.Model->FindClip(clip);
		w.Instance->Play(clipIndex < 0 ? 0 : clipIndex, true, 0.0f);
		w.Speed = static_cast<float>(std::atof(options.GetValue("walker-speed", std::to_string(w.Speed)).c_str()));
		w.YawOffset = glm::radians(static_cast<float>(std::atof(options.GetValue("walker-yaw", "0").c_str())));
		const float modelHeight = w.Model->GetBoundsMax().y - w.Model->GetBoundsMin().y;
		w.Scale = modelHeight > 1e-3f ? height / modelHeight : 1.0f;
		w.Tracker = GB::FootstepTracker(static_cast<int>(feet.size()));
		for (const std::string& name : feet)
		{
			const int node = w.Model->FindNode(name);
			if (node < 0)
				GB_CLIENT_WARN("Walker: foot node '{}' not found", name);
			else
				w.FootNodes.push_back(node);
		}
		// Height of each foot joint when planted = its lowest point over one cycle of the clip.
		w.FootBase.assign(w.FootNodes.size(), 1e9f);
		{
			GB::ModelInstance probe(*w.Model);
			probe.Play(clipIndex < 0 ? 0 : clipIndex, true, 0.0f);
			const float duration = w.Model->GetClips()[probe.GetCurrentClip()].Duration;
			for (float t = 0.0f; t < duration; t += duration / 90.0f)
			{
				probe.SetTime(t);
				for (size_t i = 0; i < w.FootNodes.size(); ++i)
					w.FootBase[i] = std::min(w.FootBase[i], probe.GetNodeWorld(w.FootNodes[i])[3].y);
			}
		}
		w.Tracker.PlantHeight = 0.045f * height / 1.75f;   // a few centimeters above the planted height, scaled to the character
		w.Tracker.LiftHeight = w.Tracker.PlantHeight * 2.0f;
		w.DustColor = style == GB::TerrainStyle::Snow ? glm::vec3(0.92f, 0.95f, 1.0f) : style == GB::TerrainStyle::Meadow ? glm::vec3(0.45f, 0.40f, 0.30f) : glm::vec3(0.78f, 0.62f, 0.42f);

		GB::GroundInteractionDesc gd;
		gd.Size = 48.0f;
		gd.Resolution = 1024;
		gd.RecoveryPerSecond = style == GB::TerrainStyle::Snow ? 0.004f : style == GB::TerrainStyle::Meadow ? 0.02f : 0.035f;
		m_Ground = std::make_unique<GB::GroundInteraction>(gd);
		m_Scene.Ground = m_Ground.get();
		m_FootDust = std::make_unique<GB::ParticleSystem>(GB::ParticlePresets::FootDust(w.DustColor));
		m_Scene.Particles.push_back(m_FootDust.get());
		m_Follow = true;
		m_Orbit.Distance = w.Scale > 1.0f || height > 1.0f ? 9.0f : 4.5f;
		m_Orbit.Pitch = 0.24f;
		m_Orbit.Yaw = 2.4f;   // looking back along the trail
		m_Orbit.MinDistance = 1.5f;
		GB_CLIENT_INFO("Walker '{}': scale {:.3f}, {} feet, speed {:.1f} m/s", kind, w.Scale, w.FootNodes.size(), w.Speed);
	}

	void UpdateWalker(float dt)
	{
		Walker& w = *m_Walker;
		w.Phase += w.Speed / w.Radius * dt;
		const glm::vec2 c(0.0f, 0.0f);
		const glm::vec2 p(c.x + w.Radius * std::cos(w.Phase), c.y + w.Radius * std::sin(w.Phase));
		const glm::vec2 h(-std::sin(w.Phase), std::cos(w.Phase));
		w.Position = { p.x, m_Terrain->GetHeight(p.x, p.y), p.y };
		w.Heading = { h.x, 0.0f, h.y };
		const float yaw = std::atan2(h.x, h.y) + w.YawOffset;
		w.Root = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), w.Position), yaw, glm::vec3(0, 1, 0)), glm::vec3(w.Scale));
		w.Instance->Update(dt);

		// Foot positions in the world; the planted height is subtracted so "on the ground" means height ~ 0.
		std::vector<glm::vec3> feet;
		for (size_t i = 0; i < w.FootNodes.size(); ++i)
		{
			glm::vec3 f = glm::vec3(w.Root * w.Instance->GetNodeWorld(w.FootNodes[i])[3]);
			f.y -= w.FootBase[i] * w.Scale;
			feet.push_back(f);
		}
		const auto ground = m_Terrain->MakeGroundFunction();
		for (const GB::FootPlant& plant : w.Tracker.Update(feet, ground, w.Heading, dt))
		{
			m_Ground->StampFootprint(plant.Position, plant.Heading, w.FootLength, w.FootWidth, 1.0f);
			m_FootDust->EmitAt(plant.Position + glm::vec3(0.0f, 0.04f, 0.0f), 3 + static_cast<uint32_t>(w.Scale > 1.0f));   // a small puff per step
		}
		m_Ground->Update(dt, w.Position);
		m_FootDust->Update(dt);
		if (m_Follow)
			m_Orbit.Target = glm::mix(m_Orbit.Target, w.Position + glm::vec3(0.0f, 0.9f, 0.0f), glm::clamp(dt * 3.0f, 0.0f, 1.0f));
	}

	void UpdateDesert(float dt)
	{
		m_Time += dt;
		if (m_Walker)
			UpdateWalker(dt);
		// The shadow map follows what the camera looks at.
		m_Weather->BaseEnvironment().ShadowCenter = m_Orbit.Target;
		if (m_SandSpray)
		{
			m_SandSpray->SetWind(m_Weather->GetWind());
			m_SandSpray->Intensity = glm::clamp(1.0f - 0.5f * m_Weather->GetRain() - 0.8f * m_Weather->GetSnow(), 0.0f, 1.0f);
			m_SandSpray->Update(dt, m_LastCamera);
		}
		// Dust devil drifts across the dunes.
		if (!m_Particles.empty())
		{
			const float x = 55.0f - 6.0f * m_Time * 0.8f, z = -48.0f + 3.0f * m_Time * 0.4f;
			const float wrapped = std::fmod(x + 200.0f, 140.0f) - 70.0f;
			m_Particles[m_DustDevilIndex]->SetPosition({ wrapped, m_Terrain->GetHeight(wrapped, z), z });
			m_Particles[m_DustDevilIndex]->Update(dt);
		}
		m_Weather->Update(dt, m_LastCamera);
		for (const GB::ThunderEvent& t : m_Weather->GetLightning().TakeThunder())
			GB_CLIENT_INFO("Thunder: {:.0f} m away, loudness {:.2f}", t.Distance, t.Loudness);
		m_Weather->Apply(m_Scene);
	}

	void ApplyWeatherPreset(const std::string& name, bool snap)
	{
		static const std::pair<const char*, GB::WeatherPreset> presets[] = {
			{ "clear", GB::WeatherPreset::Clear }, { "lightrain", GB::WeatherPreset::LightRain }, { "rain", GB::WeatherPreset::Rain },
			{ "storm", GB::WeatherPreset::Thunderstorm }, { "snow", GB::WeatherPreset::Snowfall }, { "blizzard", GB::WeatherPreset::Blizzard },
			{ "dust", GB::WeatherPreset::Dust }, { "sandstorm", GB::WeatherPreset::Sandstorm }, { "tornado", GB::WeatherPreset::Tornado } };
		for (const auto& p : presets)
		{
			if (name == p.first)
			{
				m_Weather->SetPreset(p.second);
				if (snap)
					m_Weather->Snap();
				GB_CLIENT_INFO("Weather: {}", p.first);
				return;
			}
		}
		GB_CLIENT_WARN("Unknown weather '{}'", name);
	}

	void UpdateWeather(float dt)
	{
		m_Time += dt;
		// The tornado wanders across the far field.
		m_Weather->SetTornadoPosition({ m_TornadoSwing * std::sin(m_Time * 0.08f), 0.0f, -42.0f + 8.0f * std::sin(m_Time * 0.05f) });
		m_Weather->Update(dt, m_LastCamera);
		for (const GB::ThunderEvent& t : m_Weather->GetLightning().TakeThunder())
			GB_CLIENT_INFO("Thunder: {:.0f} m away, loudness {:.2f}", t.Distance, t.Loudness);
		m_Weather->Apply(m_Scene);
	}

	void UpdateParticles(float dt)
	{
		for (auto& p : m_Particles)
			p->Update(dt);
		m_Time += dt;
		if (m_ExplosionEvery > 0.0f && m_Explosion)
		{
			m_ExplosionTimer -= dt;
			if (m_ExplosionTimer <= 0.0f)
			{
				m_ExplosionTimer = m_ExplosionEvery;
				m_Explosion->Burst(110);
			}
		}
		if (!m_Scene.PointLights.empty() && m_FireLightBase > 0.0f)   // flicker
		{
			const float f = 1.0f + 0.18f * std::sin(m_Time * 17.0f) + 0.12f * std::sin(m_Time * 31.0f + 1.3f) + 0.08f * std::sin(m_Time * 7.0f + 4.0f);
			m_Scene.PointLights[0].Intensity = m_FireLightBase * f;
		}
	}

	void BuildModelScene(const GB::LaunchOptions& options)
	{
		const std::string path = options.GetValue("model", std::string(GB_ASSET_DIR) + "/models/DamagedHelmet.glb");
		m_Model = GB::Model::Load(path);
		if (!m_Model)
		{
			GB_CLIENT_ERROR("Could not load model '{}'", path);
			Close();
			return;
		}

		const glm::vec3 center = m_Model->GetBoundsCenter();
		const float radius = std::max(m_Model->GetBoundsRadius(), 0.01f);
		if (!options.HasFlag("nofloor"))
		{
			m_Plane = std::make_unique<GB::Mesh>(GB::Mesh::CreatePlane(radius * 60.0f));
			GB::RenderObject floor;
			floor.MeshPtr = m_Plane.get();
			floor.Mat.Albedo = { 0.28f, 0.29f, 0.31f };
			floor.Mat.Roughness = 0.6f;
			floor.Mat.HeightScale = 0.0f;
			floor.Mat.CavityStrength = 0.0f;
			floor.Transform = glm::translate(glm::mat4(1.0f), { center.x, m_Model->GetBoundsMin().y - 0.001f * radius, center.z });
			m_Scene.Objects.push_back(floor);
		}

		m_StaticObjects = m_Scene.Objects.size();
		m_Instance = std::make_unique<GB::ModelInstance>(*m_Model);
		m_Instance->SetSpeed(static_cast<float>(std::atof(options.GetValue("speed", "1").c_str())));
		for (size_t i = 0; i < m_Model->GetClips().size(); ++i)
			GB_CLIENT_INFO("Animation {}: '{}' ({:.2f}s)", i, m_Model->GetClips()[i].Name, m_Model->GetClips()[i].Duration);
		m_OrbitSpeed = glm::radians(static_cast<float>(std::atof(options.GetValue("orbit", "0").c_str())));
		m_SwitchSeconds = static_cast<float>(std::atof(options.GetValue("switch", "1.5").c_str()));
		{
			std::string list = options.GetValue("playlist");
			size_t start = 0;
			while (!list.empty() && start <= list.size())
			{
				const size_t comma = list.find(',', start);
				const std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
				const int clip = m_Model->FindClip(item);
				if (clip >= 0)
					m_Playlist.push_back(clip);
				else
					GB_CLIENT_WARN("Playlist: unknown animation '{}'", item);
				if (comma == std::string::npos)
					break;
				start = comma + 1;
			}
		}
		if (!options.HasFlag("noanim") && !m_Model->GetClips().empty())
		{
			const std::string anim = options.GetValue("anim", "0");
			int clip = m_Model->FindClip(anim);
			if (clip < 0)
				clip = std::atoi(anim.c_str());
			if (!m_Playlist.empty())
				clip = m_Playlist[0];
			if (!m_Instance->Play(clip, true, 0.0f))
				GB_CLIENT_WARN("Unknown animation '{}'", anim);
			if (!options.GetValue("time").empty())
			{
				m_Instance->SetTime(static_cast<float>(std::atof(options.GetValue("time").c_str())));
				m_Instance->SetPaused(true);
			}
		}

		m_Scene.Sun.Direction = { -0.5f, -0.75f, -0.45f };
		m_Scene.Sun.Intensity = 3.5f;
		m_Scene.Env.Intensity = 0.8f;
		m_Scene.Env.BloomThreshold = 1.5f;
		m_Scene.Env.ShadowCenter = center;
		m_Scene.Env.ShadowRadius = radius * 3.0f;
		m_Orbit.Target = center;
		m_Orbit.Pitch = 0.18f;
		m_Orbit.Yaw = glm::radians(static_cast<float>(std::atof(options.GetValue("yaw", "0").c_str())));
		m_Orbit.Distance = radius / std::sin(glm::radians(22.5f)) * 1.15f;
		m_Orbit.MinDistance = radius * 0.5f;
		m_Orbit.MaxDistance = radius * 20.0f;
	}

	void BuildScene()
	{
		m_Sphere = std::make_unique<GB::Mesh>(GB::Mesh::CreateSphere(0.5f));
		m_Plane = std::make_unique<GB::Mesh>(GB::Mesh::CreatePlane(400.0f));
		m_Cube = std::make_unique<GB::Mesh>(GB::Mesh::CreateCube(1.0f));

		auto add = [&](const GB::Mesh* mesh, glm::vec3 pos, glm::vec3 scale, GB::Material mat)
		{
			GB::RenderObject o;
			o.MeshPtr = mesh;
			o.Mat = mat;
			o.Transform = glm::scale(glm::translate(glm::mat4(1.0f), pos), scale);
			m_Scene.Objects.push_back(o);
		};

		GB::Material floor;
		floor.Albedo = { 0.22f, 0.23f, 0.25f };
		floor.Roughness = 0.55f;
		add(m_Plane.get(), { 0, 0, 0 }, glm::vec3(1.0f), floor);

		// Roughness sweep: gold metal (top) and red plastic (bottom).
		constexpr int columns = 7;
		for (int i = 0; i < columns; ++i)
		{
			const float roughness = 0.08f + 0.92f * static_cast<float>(i) / (columns - 1);
			const float x = (static_cast<float>(i) - (columns - 1) * 0.5f) * 1.25f;

			GB::Material gold;
			gold.Albedo = { 1.0f, 0.78f, 0.34f };
			gold.Metallic = 1.0f;
			gold.Roughness = roughness;
			add(m_Sphere.get(), { x, 2.15f, 0.0f }, glm::vec3(1.1f), gold);

			GB::Material plastic;
			plastic.Albedo = { 0.75f, 0.06f, 0.05f };
			plastic.Metallic = 0.0f;
			plastic.Roughness = roughness;
			add(m_Sphere.get(), { x, 0.6f, 0.0f }, glm::vec3(1.1f), plastic);
		}

		// Blue-ish chrome cube and a glowing emissive orb in front (drives bloom).
		GB::Material chrome;
		chrome.Albedo = { 0.85f, 0.9f, 1.0f };
		chrome.Metallic = 1.0f;
		chrome.Roughness = 0.12f;
		add(m_Cube.get(), { -3.2f, 0.5f, 2.4f }, glm::vec3(1.0f), chrome);

		GB::Material glow;
		glow.Albedo = { 0.1f, 0.1f, 0.1f };
		glow.Roughness = 0.4f;
		glow.Emissive = { 2.0f, 6.0f, 14.0f };
		add(m_Sphere.get(), { 3.2f, 0.45f, 2.4f }, glm::vec3(0.9f), glow);

		m_Scene.Sun.Direction = { -0.55f, -0.62f, -0.45f };
		m_Scene.Sun.Intensity = 3.5f;
		m_Scene.PointLights.push_back({ { 3.2f, 1.1f, 2.4f }, { 0.25f, 0.5f, 1.0f }, 14.0f });
		m_Scene.PointLights.push_back({ { -3.6f, 2.8f, 2.8f }, { 1.0f, 0.7f, 0.4f }, 12.0f });

		m_Scene.Env.Exposure = 0.9f;
		m_Scene.Env.Intensity = 0.7f;
		m_Scene.Env.BloomThreshold = 1.2f;
		m_Scene.Env.BloomIntensity = 0.45f;
		m_Scene.Env.ShadowCenter = { 0.0f, 1.0f, 0.0f };
		m_Scene.Env.ShadowRadius = 10.0f;
	}

	std::unique_ptr<GB::Renderer> m_Renderer;
	std::unique_ptr<GB::Mesh> m_Sphere, m_Plane, m_Cube;
	std::vector<std::unique_ptr<GB::Texture2D>> m_Textures;
	GB::Scene m_Scene;
	mutable GB::OrbitController m_Orbit;
	mutable GB::FlyController m_Fly;
	int m_CameraMode = 0;
	bool m_CameraKeyWasDown = false;
	GB::Camera m_LastCamera, m_BlendFrom;
	float m_BlendT = 1.0f;
	std::unique_ptr<GB::Model> m_Model;
	std::unique_ptr<GB::ModelInstance> m_Instance;
	size_t m_StaticObjects = 0;
	std::vector<std::unique_ptr<GB::ParticleSystem>> m_Particles;
	std::unique_ptr<GB::Weather> m_Weather;
	std::unique_ptr<GB::Terrain> m_Terrain;
	std::unique_ptr<GB::SandSpray> m_SandSpray;
	std::unique_ptr<GB::GrassField> m_Grass;
	std::unique_ptr<GB::TreeFactory> m_TreeFactory;
	std::vector<std::unique_ptr<GB::Tree>> m_TreeVariants;
	std::unique_ptr<Walker> m_Walker;
	std::unique_ptr<GB::GroundInteraction> m_Ground;
	std::unique_ptr<GB::ParticleSystem> m_FootDust;
	bool m_Follow = false;
	glm::vec3 m_DesertWind{ 9.0f, 0.0f, 3.0f };
	size_t m_DustDevilIndex = 0;
	float m_TornadoSwing = 24.0f;
	bool m_WeatherSteady = true, m_StrikeKeyWasDown = false, m_WeatherKeyWasDown[9] = {};
	GB::ParticleSystem* m_Explosion = nullptr;
	float m_ExplosionEvery = 0.0f, m_ExplosionTimer = 0.0f, m_FireLightBase = 0.0f, m_Time = 0.0f;
	bool m_BoomKeyWasDown = false;
	std::vector<int> m_Playlist;
	size_t m_PlaylistIndex = 0;
	float m_PlaylistTimer = 0.0f, m_SwitchSeconds = 1.5f, m_OrbitSpeed = 0.0f;
	bool m_SpaceWasDown = false;
};

GB::Application* GB::CreateApplication()
{
	return new Sandbox();
}
