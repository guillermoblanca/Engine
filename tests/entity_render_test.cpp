// Entities -> scene objects and lights, and a rendered frame. SKIPs (77) without an OpenGL 4.5 context.
#include "GB/Game/EntityRender.h"
#include "GB/Log.h"
#include "GB/Window.h"
#include <cmath>
#include <cstdio>

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

	GB::EntityWorld w;
	const GB::EntityId ball = w.Create("Ball");
	w.Find(ball)->Mesh.emplace().Shape = GB::MeshShape::Sphere;
	w.Find(ball)->Transform.Position = { 0, 1, 0 };
	const GB::EntityId lamp = w.Create("Lamp", ball);
	w.Find(lamp)->Light.emplace().Intensity = 30.0f;
	w.Find(lamp)->Transform.Position = { 0, 2, 0 };
	const GB::EntityId hidden = w.Create("Hidden");
	w.Find(hidden)->Mesh.emplace();
	w.Find(hidden)->Active = false;
	const GB::EntityId missing = w.Create("Missing");
	w.Find(missing)->Mesh.emplace().Model = "does/not/exist.glb";

	GB::EntityRenderer er;
	GB::Scene scene;
	const size_t drawn = er.Build(w, scene);
	Check(drawn == 2, "only active entities with something to draw");
	Check(scene.Objects.size() == 1 && scene.PointLights.size() == 1, "one object and one light");
	Check(std::abs(scene.PointLights[0].Position.y - 3.0f) < 1e-4f, "light uses the world position");

	GB::Renderer renderer(160, 90, 1, 512);
	Check(renderer.IsValid(), "renderer initializes");
	GB::Camera cam;
	cam.Position = { 0, 2, 5 };
	renderer.Render(scene, cam);
	Check(renderer.GetStats().ObjectsDrawn > 0, "the scene draws");

	if (failures) return 1;
	std::puts("entity render tests passed");
	return 0;
}
