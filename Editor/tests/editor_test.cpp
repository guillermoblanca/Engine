// Editor logic without a window: property registry, command runner and the log buffer. CPU only.
#include "Commands.h"
#include "LogBuffer.h"
#include "Properties.h"
#include "SceneEditor.h"
#include "TestScenarios.h"
#include <cstdio>

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

	// ---- scene editing: add, move, delete, new scene (all CPU)
	{
		SceneEditor ed(GB_ASSET_DIR);
		CommandRunner sc;
		int worldCleared = 0, replaced = 0;
		SceneHooks hooks;
		hooks.ClearWorld = [&] { ++worldCleared; };
		hooks.SceneReplaced = [&] { ++replaced; };
		hooks.SpawnPoint = [] { return glm::vec3(1, 2, 3); };
		hooks.Ground = [](float, float) { return 5.0f; };
		RegisterSceneCommands(sc, ed, hooks);

		Check(sc.Execute("add cube").Ok && ed.World().Count() == 1, "add cube");
		Check(ed.World().Find(ed.Selected())->Transform.Position == glm::vec3(1, 2, 3), "new objects appear at the spawn point");
		Check(sc.Execute("add sphere 4 0 -2").Ok && ed.World().Find(ed.Selected())->Transform.Position == glm::vec3(4, 0, -2), "add at a position");
		Check(!sc.Execute("add sphere 4 0").Ok, "incomplete position rejected");
		Check(!sc.Execute("add nothing").Ok, "unknown thing rejected");
		Check(sc.Execute("add models/DamagedHelmet.glb 0 1 0").Ok, "add a glTF by asset path");
		Check(ed.World().Find(ed.Selected())->Mesh->Model == "models/DamagedHelmet.glb", "model path stored relative");
		Check(sc.Execute(std::string("add \"") + GB_ASSET_DIR + "/models/Fox.glb\"").Ok && ed.World().Find(ed.Selected())->Mesh->Model == "models/Fox.glb", "absolute model path becomes relative");
		Check(!sc.Execute("add models/missing.glb").Ok && !sc.Execute("add readme.txt").Ok, "missing / non-model files rejected");

		Check(sc.Execute("select Cube").Ok && ed.Selected() == ed.World().FindByName("Cube"), "select by name");
		Check(sc.Execute("move 7 8 9").Ok && ed.World().Find(ed.Selected())->Transform.Position == glm::vec3(7, 8, 9), "move the selection");
		Check(sc.Execute("rotate 0 90 0 Sphere").Ok && ed.World().Find(ed.World().FindByName("Sphere"))->Transform.Rotation.y == 90.0f, "rotate a named target");
		Check(sc.Execute("scale 2").Ok && ed.World().Find(ed.Selected())->Transform.Scale == glm::vec3(2.0f), "uniform scale");
		Check(sc.Execute("scale 1 2 3 Cube").Ok && ed.World().Find(ed.Selected())->Transform.Scale == glm::vec3(1, 2, 3), "per-axis scale");
		Check(!sc.Execute("move 1 2").Ok, "move needs three numbers");
		Check(sc.Execute("drop").Ok && ed.World().Find(ed.Selected())->Transform.Position.y == 5.0f, "drop to the ground");
		Check(sc.Execute("color 1 0 0 1 0.2").Ok && ed.World().Find(ed.Selected())->Mesh->Metallic == 1.0f, "color");
		Check(sc.Execute("duplicate").Ok && ed.World().Count() == 5, "duplicate");
		const size_t before = ed.World().Count();
		Check(sc.Execute("delete").Ok && ed.World().Count() == before - 1 && ed.Selected() == NullEntity, "delete the selection");
		Check(!sc.Execute("delete").Ok, "nothing selected");
		Check(sc.Execute("objects Sphere").Message.find("1 entities") == 0, "objects filters");

		Check(sc.Execute("scene_save editor_test_scene.json").Ok, "scene saved");
		const size_t saved = ed.World().Count();
		sc.Execute("new_scene");
		Check(ed.World().Count() == 0 && worldCleared == 1 && replaced == 1, "new_scene empties the entities and the generated world");
		Check(sc.Execute("scene_load editor_test_scene.json").Ok && ed.World().Count() == saved, "scene loaded");
		std::remove("editor_test_scene.json");

		// the generated environment: trees and rocks are ordinary entities
		std::vector<TreePlacement> trees(3);
		std::vector<RockPlacement> rocks(2);
		for (size_t i = 0; i < trees.size(); ++i) { trees[i].Position = { static_cast<float>(i) * 10.0f, 0, 0 }; trees[i].Variant = static_cast<int>(i); trees[i].Yaw = 1.0f; trees[i].Scale = 1.5f; }
		rocks[1].Position = { 0, 0, 20 };
		ed.Clear();
		sc.Execute("add cube 50 0 50");
		Check(ed.PopulateEnvironment(trees, rocks) == 5 && ed.World().FindByTag("world").size() == 2, "environment entities and groups");
		const EntityId tree2 = ed.World().FindByName("Tree 2");
		Check(tree2 != NullEntity && ed.World().Find(tree2)->Prop && ed.World().Find(tree2)->Prop->Variant == 1, "tree entity with its variant");
		Check(std::abs(ed.World().Find(tree2)->Transform.Scale.x - 1.5f) < 1e-5f, "tree scale");
		SceneEditor::Sphere bounds;
		Check(ed.WorldBounds(tree2, bounds) && bounds.Radius > 1.0f, "props have pick bounds");
		Check(ed.Pick({ 10, 0.2f, 30 }, { 0, 0, -1 }) == tree2, "a ray picks a tree");
		Check(ed.Pick({ 50, 5, 50 }, { 0, -1, 0 }) == ed.World().FindByName("Cube"), "a ray picks a user object");
		Check(ed.Pick({ 100, 100, 100 }, { 0, 1, 0 }) == NullEntity, "a ray into the sky picks nothing");
		Check(sc.Execute("delete Tree 2").Ok && ed.World().FindByName("Tree 2") == NullEntity, "a tree can be deleted");
		sc.Execute("move 3 0 3 Rock 1");
		Check(ed.World().Find(ed.World().FindByName("Rock 1"))->Transform.Position == glm::vec3(3, 0, 3), "a rock can be moved");
		ed.PopulateEnvironment(trees, rocks);   // regenerating replaces the environment but keeps the user's cube
		Check(ed.World().FindByName("Cube") != NullEntity && ed.World().FindByName("Tree 2") != NullEntity && ed.World().Count() == 1 + 2 + 5, "regenerate keeps user objects");
	}

	// ---- test scenarios: every Sandbox scene is a script of editor commands
	{
		for (const char* name : { "pbr", "textures", "model", "particles", "weather", "desert", "showcase" })
			Check(FindTestScenario(name) != nullptr, name);
		Check(FindTestScenario("nope") == nullptr, "unknown scenario");

		SceneEditor ed(GB_ASSET_DIR);
		CommandRunner sc;
		SceneHooks hooks;
		std::vector<std::string> queued;
		hooks.RunScript = [&](const std::string& text) { for (auto& l : CommandRunner::SplitScript(text)) queued.push_back(l); };
		RegisterSceneCommands(sc, ed, hooks);
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
		sc.Execute("new_scene");
		for (const std::string& line : CommandRunner::SplitScript(FindTestScenario("model")->Script)) sc.Execute(line);
		Check(ed.World().FindByName("DamagedHelmet") != NullEntity, "model scene has the helmet");
	}

	if (failures) return 1;
	std::puts("editor tests passed");
	return 0;
}
