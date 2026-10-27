// Editor logic without a window: property registry, command runner and the log buffer. CPU only.
#include "Commands.h"
#include "LogBuffer.h"
#include "SceneEditor.h"
#include "Properties.h"
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	enum class Mode : int { A, B, C };
}

int main()
{
	using namespace GB;
	using namespace GB::Editor;

	float f = 0.5f; int i = 3; bool b = false; glm::vec3 v(1.0f), c(0.0f); Mode m = Mode::A;
	int changes = 0;
	PropertyRegistry reg;
	reg.Float("a.f", f, 0.0f, 1.0f, "A").OnChange = [&] { ++changes; };
	reg.Int("a.i", i, 0, 10, "A");
	reg.Bool("a.b", b, "A");
	reg.Vec3("a.v", v, -5.0f, 5.0f, "A");
	reg.Color("a.c", c, "A");
	reg.Enum("a.m", m, { "A", "B", "C" }, "A");

	Check(reg.Set("a.f", "0.25").Ok && f == 0.25f, "float set");
	Check(changes == 1, "onchange fired");
	Check(reg.Set("a.f", "7").Ok && f == 1.0f, "float clamped");
	Check(!reg.Set("a.f", "abc").Ok, "bad float rejected");
	Check(!reg.Set("nope", "1").Ok, "unknown path rejected");
	Check(reg.Set("a.i", "99").Ok && i == 10, "int clamped");
	Check(reg.Set("a.b", "true").Ok && b, "bool set");
	Check(reg.Set("a.v", "1 2 3").Ok && v == glm::vec3(1, 2, 3), "vec3 set");
	Check(reg.Set("a.c", "#ff0000").Ok && c.r > 0.99f && c.g < 0.01f, "color hex");
	Check(reg.Set("a.m", "C").Ok && m == Mode::C, "enum by name");
	Check(reg.Set("a.m", "1").Ok && m == Mode::B, "enum by index");
	Check(!reg.Set("a.m", "Z").Ok, "bad enum rejected");
	Check(reg.List("a.").size() == 6, "list by prefix");
	Check(!reg.Get("a.i").empty() && reg.Get("zzz").empty(), "get");
	reg.RemovePrefix("a.");
	Check(reg.All().empty(), "remove prefix");

	Check(CommandRunner::Tokenize("set \"a b\" 3 # note").size() == 3, "tokenize quotes and comments");
	Check(CommandRunner::SplitScript("a; b\nc # x\n\n").size() == 3, "split script");

	CommandRunner cmd;
	int sum = 0;
	cmd.Register("add", "adds", [&](const std::vector<std::string>& a) { sum += std::stoi(a.at(0)); return CommandResult{}; });
	Check(cmd.Execute("add 4").Ok && sum == 4, "command runs");
	Check(!cmd.Execute("bogus").Ok, "unknown command");
	Check(cmd.Complete("a").size() == 1, "completion");

	LogBuffer buf;
	buf.Add(spdlog::level::info, "hola");
	Check(buf.Snapshot().size() == 1, "log buffer");
	buf.Clear();
	Check(buf.Snapshot().empty(), "log clear");

	// Scene editing: commands, selection, undo/redo, picking.
	{
		SceneEditor ed;
		PropertyRegistry props;
		CommandRunner runner;
		ed.Bind(props);
		ed.Register(runner);
		ed.SetSpawnPoint({ 0.0f, 0.0f, 0.0f });

		Check(runner.Execute("entity create cube Crate").Ok && ed.Selected() != NullEntity, "create selects the entity");
		Check(props.Find("sel.position") && props.Find("sel.mesh.albedo") && !props.Find("sel.light.color"), "selection properties follow the components");
		Check(props.Set("sel.position", "0 2 -5").Ok, "set through the selection property");
		ed.Tick(false);
		Check(ed.World().Find(ed.Selected())->Transform.Position == glm::vec3(0, 2, -5), "property writes the entity");
		Check(runner.Execute("entity create light Lamp Crate").Ok && ed.World().Find(ed.Resolve("Lamp"))->Parent == ed.Resolve("Crate"), "child entity");
		Check(runner.Execute("entity set Crate position 1 2 3").Ok && ed.World().Find(ed.Resolve("Crate"))->Transform.Position == glm::vec3(1, 2, 3), "entity set by name");
		Check(!runner.Execute("entity set Crate nonsense 1").Ok, "bad property rejected");
		Check(runner.Execute("entity add body").Ok && props.Find("sel.body.mass"), "add component rebinds");
		Check(!runner.Execute("entity parent Crate Lamp").Ok, "cycle rejected through the command");
		Check(runner.Execute("entity duplicate Crate").Ok && ed.World().Count() == 4, "duplicate copies the subtree");
		Check(runner.Execute("entity delete").Ok && ed.World().Count() == 2, "delete the selection");

		// undo / redo walk back through the steps
		Check(runner.Execute("undo").Ok && ed.World().Count() == 4, "undo delete");
		Check(runner.Execute("redo").Ok && ed.World().Count() == 2, "redo delete");
		while (ed.UndoDepth() > 0) ed.Undo();
		Check(ed.World().Count() == 0 && ed.Selected() == NullEntity, "undo all leaves an empty scene");
		Check(!runner.Execute("undo").Ok, "nothing left to undo");

		// scene files
		runner.Execute("entity create sphere Ball");
		const std::string path = (std::filesystem::temp_directory_path() / "gb_editor_scene_test.json").string();
		Check(runner.Execute("scene save \"" + path + "\"").Ok, "scene save");
		runner.Execute("scene new");
		Check(ed.World().Count() == 0, "scene new");
		Check(runner.Execute("scene load \"" + path + "\"").Ok && ed.World().FindByName("Ball") != NullEntity, "scene load");
		Check(!runner.Execute("scene load /no/such/file.json").Ok, "missing file reported");
		std::filesystem::remove(path);

		// picking: nearest of two overlapping boxes along the ray, misses elsewhere
		runner.Execute("scene new");
		runner.Execute("entity create cube Near");
		props.Set("sel.position", "0 0 -2");
		ed.Tick(false);
		runner.Execute("entity create cube Far");
		props.Set("sel.position", "0 0 -6");
		ed.Tick(false);
		Ray ray{ { 0, 0, 5 }, { 0, 0, -1 } };
		Check(ed.Pick(ray) == ed.Resolve("Near"), "pick the nearest");
		Ray off{ { 5, 0, 5 }, { 0, 0, -1 } };
		Check(ed.Pick(off) == NullEntity, "pick misses");
		Camera cam;
		cam.Position = { 0, 0, 5 };
		cam.Target = { 0, 0, 0 };
		const Ray center = ScreenRay(cam, { 0.5f, 0.5f }, 16.0f / 9.0f);
		Check(ed.Pick(center) == ed.Resolve("Near"), "screen ray through the center");
		const glm::vec3 axis(1, 0, 0);
		Check(std::fabs(RayAxisParam({ { 3, 4, 0 }, { 0, -1, 0 } }, { 0, 0, 0 }, axis) - 3.0f) < 1e-4f, "axis parameter of a ray");
	}

	if (failures) return 1;
	std::puts("editor tests passed");
	return 0;
}
