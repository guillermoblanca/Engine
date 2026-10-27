#include "TestScenarios.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace GB::Editor
{
	namespace
	{
		std::string Fmt(const char* format, double a = 0, double b = 0, double c = 0, double d = 0, double e = 0)
		{
			char buf[256];
			std::snprintf(buf, sizeof buf, format, a, b, c, d, e);
			return buf;
		}

		// Clean slate shared by the scenes that do not generate a world.
		const char* const kReset = "new_scene\nweather clear snap\nquality medium\n";

		// Commands that act on the entity `add` just created (the selection).
		std::string Material(double r, double g, double b, double metallic, double roughness)
		{
			return Fmt("entity set sel mesh.albedo %.3f %.3f %.3f\nentity set sel mesh.metallic %.3f\nentity set sel mesh.roughness %.3f\n", r, g, b, metallic, roughness);
		}
		std::string Floor(double size, double r, double g, double b, double roughness)
		{
			return Fmt("add plane 0 0 0\nentity set sel mesh.size %.1f\n", size) + Material(r, g, b, 0, roughness);
		}
		std::string Scale(double s) { return Fmt("entity set sel scale %.3f %.3f %.3f\n", s, s, s); }
		std::string Emissive(double r, double g, double b) { return Fmt("entity set sel mesh.emissive %.3f %.3f %.3f\n", r, g, b); }
		std::string Light(double r, double g, double b, double intensity, double radius)
		{
			return Fmt("entity set sel light.color %.3f %.3f %.3f\nentity set sel light.intensity %.3f\nentity set sel light.radius %.3f\n", r, g, b, intensity, radius);
		}

		std::string Pbr()
		{
			std::string s = kReset;
			s += "set sun.direction -0.55 -0.62 -0.45\nset sun.intensity 3.5\nset exposure 0.9\nset sky.intensity 0.7\n";
			s += "set bloom.threshold 1.2\nset bloom.intensity 0.45\nset shadow.radius 10\n";
			s += Floor(60, 0.22, 0.23, 0.25, 0.55);
			constexpr int columns = 7;
			for (int i = 0; i < columns; ++i)
			{
				const double rough = 0.08 + 0.92 * i / (columns - 1);
				const double x = (i - (columns - 1) * 0.5) * 1.25;
				s += Fmt("add sphere %.3f 2.15 0\n", x) + Scale(1.1) + Material(1, 0.78, 0.34, 1, rough);
				s += Fmt("add sphere %.3f 0.6 0\n", x) + Scale(1.1) + Material(0.75, 0.06, 0.05, 0, rough);
			}
			s += "add cube -3.2 0.5 2.4\n" + Material(0.85, 0.9, 1, 1, 0.12);
			s += "add sphere 3.2 0.45 2.4\n" + Scale(0.9) + Material(0.1, 0.1, 0.1, 0, 0.4) + Emissive(2, 6, 14);
			s += "add light 3.2 1.1 2.4\n" + Light(0.25, 0.5, 1, 14, 20);
			s += "add light -3.6 2.8 2.8\n" + Light(1, 0.7, 0.4, 12, 20);
			s += "select none\ncamera 0 12.5 13 0 1.3 0\n";
			return s;
		}

		std::string Textures()
		{
			// 4x3 gallery of material swatches: the textured quads of the Sandbox need texture maps on entities.
			std::string s = kReset;
			s += Floor(60, 0.18, 0.18, 0.2, 0.8);
			static const double swatch[12][5] = {
				{ 0.55, 0.55, 0.55, 0, 0.9 }, { 0.45, 0.30, 0.18, 0, 0.7 }, { 0.30, 0.45, 0.20, 0, 0.8 }, { 0.70, 0.62, 0.45, 0, 0.9 },
				{ 0.80, 0.80, 0.82, 1, 0.3 }, { 0.95, 0.64, 0.54, 1, 0.4 }, { 1.00, 0.78, 0.34, 1, 0.2 }, { 0.30, 0.30, 0.32, 1, 0.6 },
				{ 0.70, 0.10, 0.10, 0, 0.3 }, { 0.10, 0.25, 0.70, 0, 0.3 }, { 0.10, 0.55, 0.20, 0, 0.5 }, { 0.90, 0.90, 0.90, 0, 0.1 } };
			for (int i = 0; i < 12; ++i)
			{
				const double x = (i % 4 - 1.5) * 2.2, z = (i / 4 - 1.0) * 2.2;
				s += Fmt("add cube %.2f 0.5 %.2f\n", x, z) + Material(swatch[i][0], swatch[i][1], swatch[i][2], swatch[i][3], swatch[i][4]);
			}
			s += "select none\ncamera 0 40 12 0 0 0\n";
			return s;
		}

		std::string Model()
		{
			return std::string(kReset) + Floor(60, 0.22, 0.23, 0.25, 0.55) + "add models/DamagedHelmet.glb 0 1.3 0\nselect none\ncamera 35 12 4 0 1.3 0\n";
		}

		std::string Campfire()
		{
			return std::string(kReset) + "time 23\nweather snow snap\n" + Floor(80, 0.35, 0.35, 0.38, 0.9) +
				"add sphere 0 0.25 0\n" + Scale(0.5) + Material(0.2, 0.05, 0.02, 0, 0.8) + Emissive(6, 2, 0.4) +
				"add light 0 0.9 0\n" + Light(1, 0.55, 0.2, 25, 20) + "select none\ncamera 30 18 7 0 0.8 0\n";
		}

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
	}

	const std::vector<TestScenario>& TestScenarios()
	{
		static const std::vector<TestScenario> all = {
			{ "pbr", "PBR spheres", "Roughness sweep of gold and plastic spheres, chrome cube and a glowing orb under sun and point lights.", Pbr(), false },
			{ "textures", "Materials gallery", "4x3 gallery of material swatches (the Sandbox's textured, displaced quads need texture maps on entities).", Textures(), true },
			{ "model", "glTF model", "The DamagedHelmet glTF on a floor, as a regular model entity.", Model(), false },
			{ "particles", "Night campfire", "Night with snow, a flickering-orange light and an emissive ember (the Sandbox's fire/smoke/spark systems are not entities yet).", Campfire(), true },
			{ "weather", "Weather", "Temperate world under a storm (switch presets with `weather <name>`).",
				"set tod.enabled false\ngenerate temperate 3\nweather storm snap\nquality medium\n", true },
			{ "desert", "Desert", "Dune world with clear sky (the Sandbox's wind ripples and dust devil are not ported).",
				"set tod.enabled false\ngenerate desert 1\nweather clear snap\nquality medium\n", true },
			{ "showcase", "Showcase", "Temperate world at golden hour with the ultra quality preset.",
				"set tod.enabled false\ngenerate temperate 5\nweather clear snap\ntime 18.5\nquality ultra\n", true },
		};
		return all;
	}

	const TestScenario* FindTestScenario(const std::string& name)
	{
		for (const TestScenario& s : TestScenarios())
			if (s.Name == name) return &s;
		return nullptr;
	}

	void RegisterTestCommands(CommandRunner& commands, std::function<void(const std::string&)> runScript, std::function<CommandResult(const std::string&)> runExternal)
	{
		commands.Register("tests", "tests: lists the Sandbox test scenes you can run with `test <name>`", [](const std::vector<std::string>&) -> CommandResult
		{
			std::string out = "test scenes:";
			for (const TestScenario& s : TestScenarios())
				out += "\n  " + s.Name + (s.Approximate ? " (approximation)" : "") + ": " + s.Description;
			out += "\n`test <name>` builds it in the viewport; `test <name> sandbox` opens the original in its own window";
			return { true, out };
		});
		commands.Register("test", "test <name> [sandbox]: builds a Sandbox test scene in the viewport (sandbox = run the original in its own window)",
			[runScript = std::move(runScript), runExternal = std::move(runExternal)](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: test <name> [sandbox]  (see tests)" };
			const TestScenario* s = FindTestScenario(Lower(a[0]));
			if (!s) return { false, "unknown test scene '" + a[0] + "' (see tests)" };
			if (a.size() > 1 && Lower(a[1]) == "sandbox")
				return runExternal ? runExternal(s->Name) : CommandResult{ false, "the Sandbox executable is not available" };
			if (!runScript) return { false, "cannot run scripts here" };
			runScript(s->Script);
			return { true, "test scene '" + s->Name + "': " + s->Title };
		});
	}
}
