// Entities -> scene objects and lights, and a rendered frame. SKIPs (77) without an OpenGL 4.5 context.
#include "GB/Visual/EntityRender.h"
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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

	// A group of `n` lights along x, at height `y`, with the ids a fresh world gives (the group first).
	void PopulateLights(GB::EntityWorld& w, int n, float y)
	{
		const GB::EntityId group = w.Create("Lamps");
		for (int i = 0; i < n; ++i)
		{
			const GB::EntityId id = w.Create("Lamp " + std::to_string(i + 1), group);
			w.Find(id)->Transform.Position = { 3.0f * static_cast<float>(i), y, 10.0f };
			w.Find(id)->Light.emplace();
		}
	}

	// T53: EntityWorld::Clear starts the ids again at 1, so a world emptied and filled again with as many entities has the same ids and the same
	// NextId (what T31 keyed the lists on); the entities behind the ids are other ones, and the new world must be drawn on the very first Build.
	// The entities change list (lights before, props after, then lights again, higher): the new nodes may take the freed addresses, and only lists
	// made again (EntityWorld::Structure changes in Clear) put them in the right one. No GPU: props go through the prop builder, lights are data.
	void SameIdsAfterClear()
	{
		GB::EntityWorld w;
		PopulateLights(w, 20, 1.0f);
		GB::EntityRenderer er;
		std::vector<glm::vec3> propsAt;
		er.SetPropBuilder([&propsAt](const GB::PropComponent&, const glm::mat4& m, GB::Scene&) { propsAt.push_back(glm::vec3(m[3])); return true; });
		er.SetPropCull({ 0.0f, 0.0f, 0.0f }, 420.0f);   // (the editor's path: the props by cells)
		GB::Scene scene;
		er.Build(w, scene);
		Check(scene.PointLights.size() == 20 && propsAt.empty(), "the lights of the first world are drawn");

		const GB::EntityId next = w.NextId();
		w.Clear();
		PopulateProps(w, 20);
		Check(w.NextId() == next && w.FindByName("Tree 1") == 2, "the new world takes the same ids and the same NextId");
		scene = GB::Scene{};
		er.Build(w, scene);
		Check(propsAt.size() == 20, "the props of the new world (same ids) are built on the first frame");
		Check(scene.PointLights.empty(), "no light of the old world is drawn");
		Check(!propsAt.empty() && std::abs(propsAt.back().y) < 1e-4f && std::abs(propsAt.back().x - 57.0f) < 1e-4f, "the props stand where the new world puts them");

		w.Clear();
		PopulateLights(w, 20, 5.0f);
		Check(w.NextId() == next, "the third world takes the same ids again");
		propsAt.clear();
		scene = GB::Scene{};
		er.Build(w, scene);
		Check(propsAt.empty() && scene.PointLights.size() == 20, "the lights of the third world are drawn on the first frame, and no prop");
		bool newHeight = !scene.PointLights.empty();
		for (const GB::PointLight& l : scene.PointLights) newHeight = newHeight && std::abs(l.Position.y - 5.0f) < 1e-4f;
		Check(newHeight, "the lights stand where the new world puts them (the new transform)");
	}

	// T66: the renderer caches a world, the world is moved away and another takes its place in the same object (SceneEditor::ReplaceWorld): the next
	// Build draws the new content. The moved-from object changes its stamp, so even a world moved into and out of the same address is listed again.
	void WorldMovedIn()
	{
		GB::EntityWorld live;
		PopulateLights(live, 10, 1.0f);
		GB::EntityRenderer er;
		size_t built = 0;
		er.SetPropBuilder([&built](const GB::PropComponent&, const glm::mat4&, GB::Scene&) { ++built; return true; });
		GB::Scene scene;
		er.Build(live, scene);
		Check(scene.PointLights.size() == 10, "the lights of the live world are drawn");

		GB::EntityWorld loaded;
		PopulateProps(loaded, 10);
		GB::EntityWorld old = std::move(live);
		live = std::move(loaded);
		scene = GB::Scene{};
		er.Build(live, scene);
		Check(built == 10 && scene.PointLights.empty(), "a world moved in is drawn on the first frame");
	}
}

int GBTest_entity_render(int, char**)
{
	GB::Log::Init();
	PropsAfterRegenerate();   // (needs no window)
	SameIdsAfterClear();
	WorldMovedIn();
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

	// T53 with meshes: the same ids after a Clear, another shape and another place, drawn on the first Build
	const GB::Mesh* sphereMesh = scene.Objects.empty() ? nullptr : scene.Objects[0].MeshPtr;
	const GB::EntityId next = w.NextId();
	w.Clear();
	const GB::EntityId box = w.Create("Ball");
	w.Find(box)->Mesh.emplace().Shape = GB::MeshShape::Cube;
	w.Find(box)->Transform.Position = { 4, 2, 0 };
	w.Create("Lamp");   // (no light: an empty entity at the old light's id)
	w.Create("Hidden");
	w.Create("Missing");
	Check(box == ball && w.NextId() == next, "the new world takes the same ids");
	scene = GB::Scene{};
	er.Build(w, scene);
	Check(scene.Objects.size() == 1 && scene.PointLights.empty(), "the new world: one object and no light");
	Check(!scene.Objects.empty() && scene.Objects[0].MeshPtr != sphereMesh && std::abs(scene.Objects[0].Transform[3].x - 4.0f) < 1e-4f,
		"the new world's mesh and transform on the first Build");

	if (failures) return 1;
	std::puts("entity render tests passed");
	return 0;
}
