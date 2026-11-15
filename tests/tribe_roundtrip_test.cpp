// K0 of the refactor of Tribe (documentation/02_tecnico/arquitectura/TRIBE_REFACTOR.md section 4, step 5): the golden save of a clan. A clan of
// six lives half a day on a generated world (a SimRun, the harness of GBSim, with its built-in content: no assets, so a change of data does not
// move it; section 4 asks for two days, but one day of this clan costs ~9 s and rapido has ~10 s), is saved, and the save must be the text of tests/data/saves/tribe_k0.json, written at the base commit of the refactor. Then the
// save is loaded into a second run and saved again (the same text), and the fixture itself is loaded into a third and saved again (the same
// text). A cut of the refactor that changes what a tribe does or what it saves fails here. Only Tribe::ToJson is covered: what lives outside
// the tribe's save (the buildings of the settlement, the world's nodes) is the GBSim A/B's (tools/gbsim_ab.py). CPU only.
//
// The text is compared pretty-printed (nlohmann::ordered_json, key order kept), so the fixture diffs line by line; the comparison is exact
// on every value. Carriage returns are dropped from the fixture when read (git may check it out with CRLF).
//
// Regenerate the fixture only after a change that is meant to change the save (never in a cut of the refactor), from the build directory:
//   GB_UPDATE_GOLDEN=1 ctest -C Release -R "^tribe_roundtrip$"          (or: GBSimTests tribe_roundtrip --update)
// then look at the diff of tests/data/saves/tribe_k0.json and commit it with the change that moved it.
#include "GB/Game/SimQA.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/World/WorldGen.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	constexpr double kDay = 1200.0;   // as GBSim (Sim/src/main.cpp)
	// Half a day: the clan has gathered, built, hunted and talked, and the test stays in the budget of rapido (one day of this clan is ~9 s).
	constexpr double kDays = 0.5;

	// The save as the fixture holds it: one value per line, the keys in the order the tribe wrote them. Empty if it is not JSON.
	std::string Pretty(const std::string& json)
	{
		const nlohmann::ordered_json j = nlohmann::ordered_json::parse(json, nullptr, false);
		return j.is_discarded() ? std::string() : j.dump(1, '\t') + "\n";
	}

	std::string ReadText(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		std::string out;
		out.reserve(text.size());
		for (char c : text) if (c != '\r') out += c;
		return out;
	}

	// The first line where two texts part, for the message.
	std::string FirstDifference(const std::string& a, const std::string& b)
	{
		size_t i = 0, line = 1;
		while (i < a.size() && i < b.size() && a[i] == b[i]) { if (a[i] == '\n') ++line; ++i; }
		auto lineAt = [](const std::string& s, size_t at)
		{
			const size_t from = s.rfind('\n', at == 0 ? 0 : at - 1);
			const size_t start = from == std::string::npos ? 0 : from + 1;
			return s.substr(start, s.find('\n', start) - start);
		};
		return "line " + std::to_string(line) + ": got \"" + lineAt(a, i) + "\", expected \"" + lineAt(b, i) + "\"";
	}

	struct Content
	{
		std::map<std::string, BuildingDef> Buildings = DefaultBuildings();
		std::map<std::string, SpeciesDef> Species = DefaultSpecies();
	};

	SimSetup Setup(const GeneratedWorld& world, const Content& content, const std::vector<std::pair<std::string, std::string>>& roster)
	{
		SimSetup s;
		s.World = &world;
		s.Members = 6;
		s.Roster = roster;
		s.Buildings = &content.Buildings;
		s.Species = &content.Species;
		s.Combat = true;
		s.GameSeeds = true;   // as GBSim: every system seeded from the world's seed
		return s;
	}
}

int GBTest_tribe_roundtrip(int argc, char** argv)
{
	const char* env = std::getenv("GB_UPDATE_GOLDEN");
	const bool update = (env && *env && std::strcmp(env, "0") != 0) || (argc > 1 && std::strcmp(argv[1], "--update") == 0);
	const std::string fixture = std::string(GB_TEST_DATA_DIR) + "/saves/tribe_k0.json";

	WorldDesc d;
	d.Type = WorldType::Temperate;
	d.Seed = 5;
	d.Resolution = 129;
	const auto t0 = std::chrono::steady_clock::now();
	auto since = [&t0] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
	const GeneratedWorld world = WorldGenerator::GenerateData(d);
	const Content content;
	std::printf("world %.1f s\n", since());

	// 1. kDays of a clan, saved: the golden text
	SimRun first(Setup(world, content, {}));
	Check(first.Ok(), "a clan of six" + (first.Ok() ? std::string() : ": " + first.Error()));
	if (!first.Ok()) return 1;
	std::printf("clan set up %.1f s\n", since());
	first.Run(kDays * kDay);
	const std::string saved = first.Brain().ToJson();
	const std::string pretty = Pretty(saved);
	Check(!pretty.empty(), "the save is JSON");
	std::printf("after %.1f days (%.1f s): %zu members, %zu bytes saved\n", kDays, since(), first.Brain().Members().size(), saved.size());

	if (update)
	{
		std::ofstream out(fixture, std::ios::binary | std::ios::trunc);
		out << pretty;
		Check(static_cast<bool>(out), "the fixture is written: " + fixture);
		std::printf("wrote %s\n", fixture.c_str());
	}
	const std::string golden = ReadText(fixture);
	Check(!golden.empty(), "the fixture reads: " + fixture + " (regenerate: GB_UPDATE_GOLDEN=1)");
	if (!golden.empty()) Check(pretty == golden, "the save is the fixture's text; " + FirstDifference(pretty, golden));

	// 2. the save loaded into a second run (its members recreated first, as the editor does) saves the same text
	{
		SimRun second(Setup(world, content, Tribe::SavedMembers(saved)));
		Check(second.Ok() && second.Brain().FromJson(saved, true), "the save loads into another run");
		const std::string again = second.Brain().ToJson();
		Check(again == saved, "and saves again the same text; " + FirstDifference(Pretty(again), pretty));
	}

	// 3. the fixture itself loaded and saved again gives the fixture
	if (!golden.empty())
	{
		SimRun third(Setup(world, content, Tribe::SavedMembers(golden)));
		Check(third.Ok() && third.Brain().FromJson(golden, true), "the fixture loads");
		const std::string back = Pretty(third.Brain().ToJson());
		Check(back == golden, "and saves again the fixture's text; " + FirstDifference(back, golden));
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("tribe_roundtrip: ok\n");
	return failures ? 1 : 0;
}
