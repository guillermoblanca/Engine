// Entities -> scene objects and lights, and a rendered frame. SKIPs (77) without an OpenGL 4.5 context.
#include "GB/Visual/EntityRender.h"
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	// A group of `n` props (trees) along x, as SceneEditor::PopulateEnvironment makes the scenery of a generated world.
	void PopulateProps(GB::EntityWorld& w, int n)
	{
		const GB::EntityId group = w.Create("Trees");
		for (int i = 0; i < n; ++i)
		{
			const GB::EntityId id = w.Create("Tree " + std::to_string(i + 1), group);
			w.Find(id)->Transform.Position = { 3.0f * static_cast<float>(i), 0.0f, 10.0f };
			w.Find(id)->Prop = GB::PropComponent{ GB::PropKind::Tree, 0 };
		}
	}

	// T31: a regenerated world destroys the old trees and creates as many new ones (new ids, same count); its props must be drawn on the
	// very next Build, not after the periodic refresh of the lists. No GPU: props only go through the prop builder.
	void PropsAfterRegenerate()
	{
		GB::EntityWorld w;
		PopulateProps(w, 50);
		GB::EntityRenderer er;
		size_t built = 0;
		er.SetPropBuilder([&built](const GB::PropComponent&, const glm::mat4&, GB::Scene&) { ++built; return true; });
		er.SetPropCull({ 0.0f, 0.0f, 0.0f }, 420.0f);   // (the editor's path: the props by cells)
		GB::Scene scene;
		er.Build(w, scene);
		Check(built == 50, "the props of the first world are built");

		const size_t count = w.Count();
		Check(w.Destroy(w.FindByName("Trees")), "the old scenery goes (the group takes its trees along)");
		PopulateProps(w, 50);
		Check(w.Count() == count, "the new world has as many entities as the old one");
		built = 0;
		scene = GB::Scene{};
		er.Build(w, scene);
		Check(built == 50, "the props of a regenerated world with the same count are built on the next frame");
	}
}

int GBTest_entity_render(int, char**)
{
	GB::Log::Init();
	PropsAfterRegenerate();   // (needs no window)
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return failures ? 1 : 77;   // (the checks without a window still count)
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
