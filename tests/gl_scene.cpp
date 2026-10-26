// Renders a small PBR scene offscreen through the full pipeline (shadows, MSAA HDR, bloom,
// tonemap) and sanity-checks the result. SKIPs (77) without an OpenGL 4.5 context.
// Set GB_TEST_OUTPUT_DIR to also keep the rendered PNG for visual inspection.
#include "GB/Log.h"
#include "GB/Window.h"
#include "GB/Renderer/Renderer.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
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

	constexpr uint32_t W = 320, H = 180;
	GB::Renderer renderer(W, H, 4, 1024);
	Check(renderer.IsValid(), "renderer initializes (all shaders compile)");
	if (!renderer.IsValid())
		return 1;

	GB::Mesh sphere = GB::Mesh::CreateSphere(0.5f, 32, 16);
	GB::Mesh plane = GB::Mesh::CreatePlane(20.0f);

	GB::Scene scene;
	GB::RenderObject floor;
	floor.MeshPtr = &plane;
	floor.Mat.Albedo = glm::vec3(0.5f);
	floor.Mat.Roughness = 0.7f;
	scene.Objects.push_back(floor);

	GB::RenderObject ball;
	ball.MeshPtr = &sphere;
	ball.Mat.Albedo = { 1.0f, 0.1f, 0.1f };
	ball.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 0));
	scene.Objects.push_back(ball);
	scene.Env.ShadowRadius = 6.0f;

	// Textured + displaced grid: a red albedo map must tint the grid and displacement must not break rendering.
	GB::Mesh grid = GB::Mesh::CreateGrid(2.0f, 32);
	GB::Texture2D redMap(GB::Image::Solid(8, 8, 255, 0, 0), GB::TextureEncoding::SRGB);
	GB::Texture2D flatNormal(GB::Image::Solid(8, 8, 128, 128, 255), GB::TextureEncoding::Linear);
	GB::Texture2D midHeight(GB::Image::Solid(8, 8, 128, 128, 128), GB::TextureEncoding::Linear);
	Check(redMap.IsValid() && flatNormal.IsValid() && midHeight.IsValid(), "textures upload");
	GB::RenderObject quad;
	quad.MeshPtr = &grid;
	quad.Mat.AlbedoMap = &redMap;
	quad.Mat.NormalMap = &flatNormal;
	quad.Mat.HeightMap = &midHeight;
	quad.Mat.HeightScale = 0.2f;
	quad.Mat.UVScale = 2.0f;
	quad.Transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.0f)) *
		glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
	scene.Objects.push_back(quad);

	GB::Camera camera;
	camera.Position = { 0, 1.5f, 4.0f };
	camera.Target = { 0, 0.5f, 0 };

	GB::Framebuffer target(W, H);
	renderer.Render(scene, camera, &target);
	const auto px = target.ReadPixels();
	auto at = [&](uint32_t x, uint32_t y) { return &px[(static_cast<size_t>(y) * W + x) * 4]; };

	const uint8_t* sky = at(W / 2, 4);                  // top of the frame: sky
	Check(sky[2] > sky[0], "sky is bluish at the zenith");
	const uint8_t* ballPx = at(W / 2, H / 2 + 6);       // around the sphere
	Check(ballPx[0] > ballPx[1] + 20 && ballPx[0] > ballPx[2] + 20, "sphere is red");
	const uint8_t* floorPx = at(W / 2, H - 4);          // bottom: floor
	Check(floorPx[0] > 10 && floorPx[1] > 10, "floor is lit (not black)");

	const uint8_t* quadPx = at(W / 2 + 80, H / 2 - 30);   // inside the textured quad, right of the sphere
	Check(quadPx[0] > quadPx[1] + 30 && quadPx[0] > quadPx[2] + 30, "albedo map tints the displaced quad red");

	// Particles: an additive white puff in front of the camera must brighten the center but not the corners.
	{
		GB::ParticleEmitterDesc d;
		d.Shape = GB::ParticleShape::Point;
		d.ConeAngleDegrees = 0.0f;
		d.Rate = 0.0f;
		d.Lifetime = { 50.0f, 50.0f };
		d.Speed = { 0.0f, 0.0f };
		d.StartSize = { 1.2f, 1.2f };
		d.StartColor = d.EndColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		d.Intensity = 2.0f;
		d.FadeInFraction = 0.0f;
		GB::ParticleSystem puff(d);
		puff.SetPosition({ 0.0f, 1.0f, 2.5f });
		puff.Burst(1);
		puff.Update(0.01f);

		GB::Scene withParticle = scene;
		withParticle.Particles.push_back(&puff);
		GB::Framebuffer target2(W, H);
		renderer.Render(withParticle, camera, &target2);
		const auto after = target2.ReadPixels();
		auto lum = [&](const std::vector<uint8_t>& v, uint32_t x, uint32_t y) { const uint8_t* p = &v[(static_cast<size_t>(y) * W + x) * 4]; return p[0] + p[1] + p[2]; };
		// Camera looks at (0,0.5,0) from (0,1.5,4); the puff at (0,1,2.5) projects near the middle of the frame.
		Check(lum(after, W / 2, H / 2 - 10) > lum(px, W / 2, H / 2 - 10) + 150, "additive particle brightens where it is drawn");
		const int cornerChange = static_cast<int>(lum(after, 3, 3)) - static_cast<int>(lum(px, 3, 3));
		std::fprintf(stderr, "info: particle bloom changes the corner by %d (sum of RGB)\n", cornerChange);
		// Only the bloom halo (wide, faint) reaches the corner, far less than the puff itself.
		Check(cornerChange >= 0 && cornerChange < 40, "particles barely affect pixels far from them");
	}

	// Every pixel must have been written: the sky pass covers the whole frame.
	size_t black = 0;
	for (size_t i = 0; i < px.size(); i += 4)
		if (px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0)
			++black;
	Check(black == 0, "no unrendered (pure black) pixels");

	if (const char* dir = std::getenv("GB_TEST_OUTPUT_DIR"))
		target.SavePNG((std::filesystem::path(dir) / "gb_scene.png").string().c_str());
	return failures;
}
