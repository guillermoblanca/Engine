// Editor logic without a window: property registry, command runner and the log buffer. CPU only.
#include "Commands.h"
#include "LogBuffer.h"
#include "Properties.h"
#include "SceneEditor.h"
#include "TestScenarios.h"
#include "GB/Game/SceneIO.h"
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
		Check(runner.Execute("entity interact Crate pick work 0 0 -1.5 180 bucket fruit").Ok && ed.World().Find(ed.Resolve("Crate"))->Interact
			&& ed.World().Find(ed.Resolve("Crate"))->Interact->Points.size() == 1 && ed.World().Find(ed.Resolve("Crate"))->Interact->Points[0].Gives == "fruit", "an interaction point is added to an entity");
		Check(runner.Execute("entity interact Crate pick - 0 0 -2 90").Ok && ed.World().Find(ed.Resolve("Crate"))->Interact->Points.size() == 1 && ed.World().Find(ed.Resolve("Crate"))->Interact->Points[0].Montage.empty(), "the same name updates the point");
		Check(!runner.Execute("entity interact Crate pick").Ok, "an incomplete interaction command is refused");
		Check(runner.Execute("undo").Ok && ed.World().Find(ed.Resolve("Crate"))->Interact->Points[0].Gives == "fruit", "undo restores the previous point");
		Check(runner.Execute("entity interact Crate clear").Ok && !ed.World().Find(ed.Resolve("Crate"))->Interact, "the points can be cleared");
		Check(runner.Execute("undo").Ok, "undo brings the points back");
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

	// Adding objects, the generated environment as entities, New Scene (all CPU).
	{
		SceneEditor ed;
		ed.SetAssetRoot(GB_ASSET_DIR);
		PropertyRegistry props;
		CommandRunner sc;
		int worldCleared = 0;
		ed.Bind(props);
		ed.Register(sc);
		ed.OnNewScene = [&] { ++worldCleared; };
		ed.SetSpawnPoint({ 1, 2, 3 });

		Check(sc.Execute("add cube").Ok && ed.World().Count() == 1, "add cube");
		Check(ed.World().Find(ed.Selected())->Transform.Position == glm::vec3(1, 2, 3), "new objects appear at the spawn point");
		Check(sc.Execute("add sphere 4 0 -2").Ok && ed.World().Find(ed.Selected())->Transform.Position == glm::vec3(4, 0, -2), "add at a position");
		Check(!sc.Execute("add sphere 4 0").Ok && !sc.Execute("add sphere a b c").Ok, "bad positions rejected");
		Check(!sc.Execute("add nothing").Ok, "unknown thing rejected");
		Check(sc.Execute("add light 0 3 0").Ok && ed.World().Find(ed.Selected())->Light, "add light");
		Check(sc.Execute("add models/DamagedHelmet.glb 0 1 0").Ok, "add a glTF by asset path");
		Check(ed.World().Find(ed.Selected())->Mesh->Model == "models/DamagedHelmet.glb", "model path stored relative");
		Check(sc.Execute(std::string("add \"") + GB_ASSET_DIR + "/models/Fox.glb\"").Ok && ed.World().Find(ed.Selected())->Mesh->Model == "models/Fox.glb", "absolute model path becomes relative");
		Check(!sc.Execute("add models/missing.glb").Ok && !sc.Execute("add readme.txt").Ok, "missing / non-model files rejected");
		Check(ed.UndoDepth() >= 5 && sc.Execute("undo").Ok && ed.World().FindByName("Fox") == NullEntity, "adding is undoable");

		Check(sc.Execute("new_scene").Ok && ed.World().Count() == 0 && worldCleared == 1, "new_scene empties the entities and the generated world");
		Check(sc.Execute("scene new").Ok && worldCleared == 2 && ed.UndoDepth() == 0, "scene new does the same and starts a fresh history");

		// the generated environment: trees and rocks are ordinary entities
		std::vector<TreePlacement> trees(3);
		std::vector<RockPlacement> rocks(2);
		for (size_t i = 0; i < trees.size(); ++i) { trees[i].Position = { static_cast<float>(i) * 10.0f, 0, 0 }; trees[i].Variant = static_cast<int>(i); trees[i].Yaw = 1.0f; trees[i].Scale = 1.5f; }
		rocks[1].Position = { 0, 0, 20 };
		ed.SetPropBounds([](const PropComponent&, glm::vec3& center, float& radius) { center = { 0, 2, 0 }; radius = 2.0f; return true; });
		sc.Execute("add cube 50 0 50");
		Check(ed.PopulateEnvironment(trees, rocks) == 5 && ed.World().FindByTag("world").size() == 2 && ed.UndoDepth() == 0, "environment entities, grouped, history restarted");
		const EntityId tree2 = ed.World().FindByName("Tree 2");
		Check(tree2 != NullEntity && ed.World().Find(tree2)->Prop && ed.World().Find(tree2)->Prop->Variant == 1, "tree entity with its variant");
		Check(std::abs(ed.World().Find(tree2)->Transform.Scale.x - 1.5f) < 1e-5f, "tree scale");
		Check(ed.Pick(Ray{ { 10, 3, 30 }, { 0, 0, -1 } }) == tree2, "a ray picks a tree");
		Check(ed.Pick(Ray{ { 50, 5, 50 }, { 0, -1, 0 } }) == ed.World().FindByName("Cube"), "a ray picks a user object");
		Check(ed.Pick(Ray{ { 100, 100, 100 }, { 0, 1, 0 } }) == NullEntity, "a ray into the sky picks nothing");
		Check(sc.Execute("entity set \"Tree 2\" position 11 0 1").Ok && ed.World().Find(tree2)->Transform.Position == glm::vec3(11, 0, 1), "a tree can be moved");
		Check(sc.Execute("entity delete \"Tree 2\"").Ok && ed.World().FindByName("Tree 2") == NullEntity, "a tree can be deleted");
		Check(sc.Execute("undo").Ok && ed.World().FindByName("Tree 2") != NullEntity, "and the deletion undone");
		ed.PopulateEnvironment(trees, rocks);   // regenerating replaces the environment but keeps the user's cube
		Check(ed.World().FindByName("Cube") != NullEntity && ed.World().FindByName("Tree 2") != NullEntity && ed.World().Count() == 1 + 2 + 5, "regenerate keeps user objects");
		std::string saved = SaveSceneToString(ed.World());
		EntityWorld again;
		Check(LoadSceneFromString(again, saved).Ok && again.Count() == ed.World().Count(), "the environment survives a save and load");
	}

	// ---- test scenarios: every Sandbox scene is a script of editor commands
	{
		for (const char* name : { "pbr", "textures", "model", "particles", "weather", "desert", "showcase" })
			Check(FindTestScenario(name) != nullptr, name);
		Check(FindTestScenario("nope") == nullptr, "unknown scenario");

		SceneEditor ed;
		ed.SetAssetRoot(GB_ASSET_DIR);
		PropertyRegistry props;
		CommandRunner sc;
		ed.Bind(props);
		ed.Register(sc);
		std::vector<std::string> queued;
		RegisterTestCommands(sc, [&](const std::string& text) { for (auto& l : CommandRunner::SplitScript(text)) queued.push_back(l); }, nullptr);
		for (const char* app : { "set", "weather", "quality", "camera", "time", "generate" })   // provided by the application
			sc.Register(app, "", [](const std::vector<std::string>&) { return CommandResult{}; });

		for (const TestScenario& s : TestScenarios())
			for (const std::string& line : CommandRunner::SplitScript(s.Script))
			{
				const CommandResult r = sc.Execute(line);
				if (!r.Ok) std::fprintf(stderr, "scenario %s: '%s' -> %s\n", s.Name.c_str(), line.c_str(), r.Message.c_str());
				Check(r.Ok, "scenario line runs");
			}
		Check(sc.Execute("test pbr").Ok && !queued.empty() && queued.front() == "new_scene", "`test` queues the scenario script");
		Check(!sc.Execute("test bogus").Ok && !sc.Execute("test pbr sandbox").Ok, "unknown scene / no sandbox hook");
		Check(sc.Execute("tests").Message.find("showcase") != std::string::npos, "`tests` lists them");

		sc.Execute("new_scene");
		for (const std::string& line : CommandRunner::SplitScript(FindTestScenario("pbr")->Script)) sc.Execute(line);
		Check(ed.World().Count() == 19, "pbr: floor, 14 spheres, cube, orb and two lights");
		const EntityId plane = ed.World().FindByName("Plane");
		Check(plane != NullEntity && ed.World().Find(plane)->Mesh->Size == 60.0f, "pbr floor is configured through entity set");
		sc.Execute("new_scene");
		for (const std::string& line : CommandRunner::SplitScript(FindTestScenario("model")->Script)) sc.Execute(line);
		Check(ed.World().FindByName("DamagedHelmet") != NullEntity, "model scene has the helmet");
	}

	if (failures) return 1;
	std::puts("editor tests passed");
	return 0;
}
