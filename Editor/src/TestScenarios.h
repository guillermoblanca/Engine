#pragma once
#include <string>
#include <vector>

namespace GB::Editor
{
	// The test scenes of the Sandbox (`--scene=pbr`, `textures`, `model`, `particles`, `weather`, `desert`, `showcase`)
	// as editor scripts: running one rebuilds the scene in the viewport with the editor's own commands, so what is
	// shown can be selected, moved and saved. `Approximate` marks scenes that use effects the editor does not have
	// as entities yet (particle systems, textured materials); the exact original runs in the Sandbox window.
	struct TestScenario
	{
		std::string Name;          // the Sandbox's --scene value
		std::string Title;
		std::string Description;
		std::string Script;        // editor commands, one per line
		bool Approximate = false;
	};

	const std::vector<TestScenario>& TestScenarios();
	const TestScenario* FindTestScenario(const std::string& name);
}
