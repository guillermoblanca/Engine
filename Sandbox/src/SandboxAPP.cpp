#include "GB.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

// PBR showcase: a roughness sweep of metal and dielectric spheres under sun, sky and point lights.
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
		m_Sphere = std::make_unique<GB::Mesh>(GB::Mesh::CreateSphere(0.5f));
		m_Plane = std::make_unique<GB::Mesh>(GB::Mesh::CreatePlane(400.0f));
		m_Cube = std::make_unique<GB::Mesh>(GB::Mesh::CreateCube(1.0f));
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
		m_Distance = std::clamp(m_Distance + zoom * 6.0f * dt, 3.0f, 30.0f);

		if (GB::Input::IsKeyDown(Key::Escape) || GB::Input::IsGamepadButtonDown(GB::GamepadButton::Back))
			Close();
	}

	void OnRender() override
	{
		GB::Camera camera;
		camera.Target = { 0.0f, 1.3f, 0.0f };
		camera.Position = camera.Target + m_Distance * glm::vec3(
			std::sin(m_Yaw) * std::cos(m_Pitch), std::sin(m_Pitch), std::cos(m_Yaw) * std::cos(m_Pitch));
		m_Renderer->Resize(GetWindow()->GetWidth(), GetWindow()->GetHeight());
		m_Renderer->Render(m_Scene, camera);
	}

private:
	static GB::ApplicationConfig MakeConfig()
	{
		GB::ApplicationConfig config;
		config.Title = "GB Sandbox - PBR";
		config.Width = 1600;
		config.Height = 900;
		config.CreateWindow = true;
		return config;
	}

	void BuildScene()
	{
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
	GB::Scene m_Scene;
	float m_Yaw = 0.0f, m_Pitch = 0.22f, m_Distance = 13.0f;
};

GB::Application* GB::CreateApplication()
{
	return new Sandbox();
}
