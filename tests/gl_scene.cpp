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
