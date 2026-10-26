#include "GB.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

// Three scenes, picked with --scene=pbr (default), --scene=textures or --scene=model:
//   model    loads a glTF/GLB (e.g. exported from Blender): --model=path.glb (default: DamagedHelmet),
//            --nofloor, --yaw=DEG initial camera yaw, --anim=NAME|INDEX, --time=SECONDS (freeze there),
//            --speed=X, --noanim, --playlist=A,B,C --switch=SECONDS (cycle clips with cross-fade),
//            --orbit=DEG_PER_SEC (camera turns by itself).  All scenes: --size=WxH.  Keys: 1-9 switch animation (cross-fade), Space pause/resume
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
		const std::string sceneName = options.GetValue("scene", "pbr");
		if (sceneName == "textures")
			BuildTextureScene(options);
		else if (sceneName == "model")
			BuildModelScene(options);
		else
			BuildScene();
	}

protected:
	void OnUpdate(float dt) override
	{
		using GB::Key;
		float yaw = 0.0f, pitch = 0.0f, zoom = 0.0f;
		if (GB::Input::IsKeyDown(Key::Left) || GB::Input::IsKeyDown(Key::A))  yaw -= 1.0f;
		if (GB::Input::IsKeyDown(Key::Right) || GB::Input::IsKeyDown(Key::D)) yaw += 1.0f;
		if (GB::Input::IsKeyDown(Key::Up) || GB::Input::IsKeyDown(Key::W))    pitch += 1.0f;
		if (GB::Input::IsKeyDown(Key::Down) || GB::Input::IsKeyDown(Key::S))  pitch -= 1.0f;
		if (GB::Input::IsKeyDown(Key::Q)) zoom -= 1.0f;
		if (GB::Input::IsKeyDown(Key::E)) zoom += 1.0f;

		yaw += GB::Input::GetGamepadAxis(GB::GamepadAxis::RightX);
		pitch -= GB::Input::GetGamepadAxis(GB::GamepadAxis::RightY);

		m_Yaw += yaw * 1.5f * dt;
		m_Pitch = std::clamp(m_Pitch + pitch * 1.0f * dt, 0.05f, 1.4f);
		m_Distance = std::clamp(m_Distance + zoom * 0.5f * m_Distance * dt, m_ZoomMin, m_ZoomMax);

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
		m_Yaw += m_OrbitSpeed * dt;

		if (GB::Input::IsKeyDown(Key::Escape) || GB::Input::IsGamepadButtonDown(GB::GamepadButton::Back))
			Close();
	}

	void OnRender() override
	{
		GB::Camera camera;
		camera.Target = m_CameraTarget;
		camera.Position = camera.Target + m_Distance * glm::vec3(
			std::sin(m_Yaw) * std::cos(m_Pitch), std::sin(m_Pitch), std::cos(m_Yaw) * std::cos(m_Pitch));
		// Scale the clip planes with the viewing distance so tiny and huge models both fit.
		camera.NearPlane = m_Distance * 0.01f;
		camera.FarPlane = m_Distance * 500.0f;
		m_Renderer->Resize(GetWindow()->GetWidth(), GetWindow()->GetHeight());
		if (m_Instance)
		{
			m_Scene.Objects.resize(m_StaticObjects);
			m_Instance->AddToScene(m_Scene);
		}
		m_Renderer->Render(m_Scene, camera);
	}

private:
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
		m_CameraTarget = { 0.0f, 0.0f, 0.0f };
		m_Pitch = 0.0f;
		m_Distance = single >= 0 ? 6.0f : 10.5f;
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
		m_CameraTarget = center;
		m_Pitch = 0.18f;
		m_Yaw = glm::radians(static_cast<float>(std::atof(options.GetValue("yaw", "0").c_str())));
		m_Distance = radius / std::sin(glm::radians(22.5f)) * 1.15f;
		m_ZoomMin = radius * 0.5f;
		m_ZoomMax = radius * 20.0f;
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
	glm::vec3 m_CameraTarget{ 0.0f, 1.3f, 0.0f };
	GB::Scene m_Scene;
	float m_Yaw = 0.0f, m_Pitch = 0.22f, m_Distance = 13.0f;
	float m_ZoomMin = 3.0f, m_ZoomMax = 30.0f;
	std::unique_ptr<GB::Model> m_Model;
	std::unique_ptr<GB::ModelInstance> m_Instance;
	size_t m_StaticObjects = 0;
	std::vector<int> m_Playlist;
	size_t m_PlaylistIndex = 0;
	float m_PlaylistTimer = 0.0f, m_SwitchSeconds = 1.5f, m_OrbitSpeed = 0.0f;
	bool m_SpaceWasDown = false;
};

GB::Application* GB::CreateApplication()
{
	return new Sandbox();
}
