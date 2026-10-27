#include "TestScenarios.h"
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

		std::string Pbr()
		{
			std::string s = kReset;
			s += "set sun.direction -0.55 -0.62 -0.45\nset sun.intensity 3.5\nset exposure 0.9\nset sky.intensity 0.7\n";
			s += "set bloom.threshold 1.2\nset bloom.intensity 0.45\nset shadow.radius 10\n";
			s += "add plane 0 0 0\nscale 30 1 30\ncolor 0.22 0.23 0.25 0 0.55\n";
			constexpr int columns = 7;
			for (int i = 0; i < columns; ++i)
			{
				const double rough = 0.08 + 0.92 * i / (columns - 1);
				const double x = (i - (columns - 1) * 0.5) * 1.25;
				s += Fmt("add sphere %.3f 2.15 0\nscale 1.1\ncolor 1 0.78 0.34 1 %.3f\n", x, rough);
				s += Fmt("add sphere %.3f 0.6 0\nscale 1.1\ncolor 0.75 0.06 0.05 0 %.3f\n", x, rough);
			}
			s += "add cube -3.2 0.5 2.4\ncolor 0.85 0.9 1 1 0.12\n";
			s += "add sphere 3.2 0.45 2.4\nscale 0.9\ncolor 0.1 0.1 0.1 0 0.4\nemissive 2 6 14\n";
			s += "add light 3.2 1.1 2.4\nlight 0.25 0.5 1 14\n";
			s += "add light -3.6 2.8 2.8\nlight 1 0.7 0.4 12\n";
			s += "select none\ncamera 0 12.5 13 0 1.3 0\n";
			return s;
		}

		std::string Textures()
		{
			// 4x3 gallery of material swatches: the textured quads of the Sandbox need texture maps on entities.
			std::string s = kReset;
			s += "add plane 0 0 0\nscale 30 1 30\ncolor 0.18 0.18 0.2 0 0.8\n";
			static const double swatch[12][5] = {
				{ 0.55, 0.55, 0.55, 0, 0.9 }, { 0.45, 0.30, 0.18, 0, 0.7 }, { 0.30, 0.45, 0.20, 0, 0.8 }, { 0.70, 0.62, 0.45, 0, 0.9 },
				{ 0.80, 0.80, 0.82, 1, 0.3 }, { 0.95, 0.64, 0.54, 1, 0.4 }, { 1.00, 0.78, 0.34, 1, 0.2 }, { 0.30, 0.30, 0.32, 1, 0.6 },
				{ 0.70, 0.10, 0.10, 0, 0.3 }, { 0.10, 0.25, 0.70, 0, 0.3 }, { 0.10, 0.55, 0.20, 0, 0.5 }, { 0.90, 0.90, 0.90, 0, 0.1 } };
			for (int i = 0; i < 12; ++i)
			{
				const double x = (i % 4 - 1.5) * 2.2, z = (i / 4 - 1.0) * 2.2;
				s += Fmt("add cube %.2f 0.5 %.2f\n", x, z);
				s += Fmt("color %.2f %.2f %.2f %.2f %.2f\n", swatch[i][0], swatch[i][1], swatch[i][2], swatch[i][3], swatch[i][4]);
			}
			s += "select none\ncamera 0 40 12 0 0 0\n";
			return s;
		}
	}

	const std::vector<TestScenario>& TestScenarios()
	{
		static const std::vector<TestScenario> all = {
			{ "pbr", "PBR spheres", "Roughness sweep of gold and plastic spheres, chrome cube and a glowing orb under sun and point lights.", Pbr(), false },
			{ "textures", "Materials gallery", "4x3 gallery of material swatches (the Sandbox's textured, displaced quads need texture maps on entities).", Textures(), true },
			{ "model", "glTF model", "The DamagedHelmet glTF on a floor, as a regular model entity.",
				std::string(kReset) + "add plane 0 0 0\nscale 30 1 30\ncolor 0.22 0.23 0.25 0 0.55\nadd models/DamagedHelmet.glb 0 1.3 0\nselect none\ncamera 35 12 4 0 1.3 0\n", false },
			{ "particles", "Night campfire", "Night with snow, a flickering-orange light and an emissive ember (the Sandbox's fire/smoke/spark systems are not entities yet).",
				std::string(kReset) + "time 23\nweather snow snap\nadd plane 0 0 0\nscale 40 1 40\ncolor 0.35 0.35 0.38 0 0.9\n"
				"add sphere 0 0.25 0\nscale 0.5\ncolor 0.2 0.05 0.02 0 0.8\nemissive 6 2 0.4\nadd light 0 0.9 0\nlight 1 0.55 0.2 25\nselect none\ncamera 30 18 7 0 0.8 0\n", true },
			{ "weather", "Weather", "Temperate world under a storm (switch presets with `weather <name>`).",
				"weather storm snap\ngenerate temperate 3\nweather storm snap\nquality medium\n", true },
			{ "desert", "Desert", "Dune world with clear sky (the Sandbox's wind ripples and dust devil are not ported).",
				"weather clear snap\ngenerate desert 1\nweather clear snap\nquality medium\n", true },
			{ "showcase", "Showcase", "Temperate world at golden hour with the ultra quality preset.",
				"weather clear snap\ngenerate temperate 5\ntime 18.5\nquality ultra\n", true },
		};
		return all;
	}

	const TestScenario* FindTestScenario(const std::string& name)
	{
		for (const TestScenario& s : TestScenarios())
			if (s.Name == name) return &s;
		return nullptr;
	}
}
