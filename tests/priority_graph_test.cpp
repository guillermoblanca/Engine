// The priority graph (docs/GRAFO_PRIORIDADES.md), headless: JSON (versioned, round trip, the files that ship equal the code), validation messages, curves and
// combiners, the templates that rebuild the tribe's formulas (exactly), subgraphs, determinism, the library (duplicate, templates, import/export, assignments,
// files) and the tribe itself: with the builtin graph a simulation ends exactly as without graphs, and a graph that changes scores changes it.
//   GBPriorityGraphTests --write <dir>   writes the graphs that ship to <dir> (assets/data/priority_graphs)
#include "GB/AI/PriorityGraph.h"
#include "GB/AI/PriorityLibrary.h"
#include "GB/AI/Utility.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/TribePriorities.h"
#include "GB/World/WorldGen.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}
	bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
}

using namespace GB;

namespace
{
	PgNode N(const char* id, const char* type, const char* source = "", const char* fn = "", std::map<std::string, double> params = {})
	{
		PgNode n;
		n.Id = id; n.Type = type; n.Source = source; n.Fn = fn; n.Params = std::move(params);
		return n;
	}

	// input(x) -> curve -> output "eat" (replace)
	float Curve(const char* fn, std::map<std::string, double> params, float x)
	{
		PriorityGraph g;
		g.Id = "c";
		g.Nodes = { N("x", "input", "time.hour"), N("c", "curve", "", fn, std::move(params)), N("o", "output", "eat", "replace") };
		g.Edges = { { "x", "c", "in", 1.0 }, { "c", "o", "in", 1.0 } };
		PgInputs in;
		in.Values["time.hour"] = x;
		const PgResult r = EvaluatePriorityGraph(g, in);
		return r.Outputs.empty() ? -999.0f : r.Outputs[0].Value;
	}

	float Combine(const char* fn, std::vector<std::pair<double, double>> valuesAndWeights)
	{
		PriorityGraph g;
		g.Id = "c";
		g.Nodes.push_back(N("m", "combine", "", fn));
		g.Nodes.push_back(N("o", "output", "eat", "replace"));
		g.Edges.push_back({ "m", "o", "in", 1.0 });
		int i = 0;
		for (const auto& [v, w] : valuesAndWeights)
		{
			const std::string id = "k" + std::to_string(i++);
			g.Nodes.push_back(N(id.c_str(), "const", "", "", { { "value", v } }));
			g.Edges.push_back({ id, "m", "in", w });
		}
		const PgResult r = EvaluatePriorityGraph(g, PgInputs());
		return r.Outputs.empty() ? -999.0f : r.Outputs[0].Value;
	}

	bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

	std::string ReadAll(const std::filesystem::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		std::string text = ss.str(), out;   // (a checkout may turn LF into CRLF)
		for (char c : text) if (c != '') out += c;
		return out;
	}

	// An option with the factors the tribe records, built the way Decide builds them.
	UtilityOption Option(const char* kind, float score, std::vector<ScoreFactor> factors)
	{
		UtilityOption o;
		o.Kind = kind;
		o.Score = score;
		o.Factors = std::move(factors);
		return o;
	}

	float Output(const CompiledPriorityGraph& c, const char* kind, const UtilityOption& o, PgInputs in)
	{
		in.Values["option.score"] = o.Score;
		for (const ScoreFactor& f : o.Factors) in.Values["factor." + f.Name] = f.Value;
		const PgResult r = EvaluatePriorityGraph(c, in);
		const PgOutput* out = r.For(kind);
		return out ? out->Value : -999.0f;
	}

	constexpr double kDay = 1200.0;

	std::string RunFingerprint(const GeneratedWorld& world, const std::shared_ptr<PriorityLibrary>& library, double days, std::string* log = nullptr)
	{
		SimSetup setup;
		setup.World = &world;
		setup.Members = 5;
		SimRun run(setup);
		if (!run.Ok()) return "ERROR " + run.Error();
		run.Brain().Priorities = library;
		run.Run(days * kDay);
		run.Finish();
		if (log) for (const std::string& l : run.Brain().Log()) *log += l + "\n";
		return run.Fingerprint();
	}
}

int main(int argc, char** argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--write") == 0)
	{
		PriorityLibrary lib;
		const std::string e = lib.SaveDirectory(argv[2]);
		std::printf("%s\n", e.empty() ? "written" : e.c_str());
		return e.empty() ? 0 : 1;
	}

	// ---- the graphs that ship are valid, round-trip through JSON, and the files equal the code
	{
		PriorityLibrary lib;
		Check(lib.ValidateAll().empty(), "the graphs that ship have no errors or warnings: " + lib.ValidateAll());
		for (const std::string& id : lib.Ids())
		{
			PriorityGraph back;
			const std::string e = PriorityGraphFromJson(PriorityGraphToJson(*lib.Find(id)), back);
			Check(e.empty() && back == *lib.Find(id), "'" + id + "' survives a trip through JSON " + e);
		}
		const std::filesystem::path dir = std::filesystem::path(GB_ASSETS_DIR) / "data" / "priority_graphs";
		for (const std::string& id : lib.Ids())
			Check(ReadAll(dir / (id + ".json")) == PriorityGraphToJson(*lib.Find(id)), "assets/data/priority_graphs/" + id + ".json equals the graph in the code");
		PriorityLibrary loaded;
		const std::string e = loaded.LoadDirectory(dir.string());
		Check(e.empty(), "the folder loads: " + e);
		Check(loaded.DefaultGraph == "builtin" && loaded.Roles.empty() && loaded.Members.empty(), "the shipped assignments: builtin for everyone");
	}

	// ---- JSON: versions and mistakes
	{
		PriorityGraph g;
		Check(PriorityGraphFromJson("{\"kind\":\"priority_graph\",\"version\":99,\"id\":\"x\",\"nodes\":[]}", g).find("version 99") != std::string::npos, "a newer version is refused with a clear message");
		Check(!PriorityGraphFromJson("nope", g).empty(), "text that is not JSON is refused");
		Check(Has(PriorityGraphFromJson("{\"kind\":\"other\",\"id\":\"x\",\"nodes\":[]}", g), "not a priority graph"), "another kind of file is refused");
		Check(Has(PriorityGraphFromJson("{\"version\":1,\"nodes\":[]}", g), "no id"), "a graph with no id is refused");
		Check(Has(PriorityGraphFromJson("{\"id\":\"x\",\"nodes\":[{\"id\":\"a\",\"type\":\"const\",\"params\":{\"value\":\"x\"}}]}", g), "must be a number"), "a text param is refused");
		Check(PriorityGraphFromJson("{\"id\":\"x\",\"nodes\":[]}", g).empty() && g.Id == "x", "the minimum graph loads");
	}

	// ---- validation messages
	{
		auto text = [](const PriorityGraph& g, const PgResolver& r = {}) { return PgIssuesText(ValidatePriorityGraph(g, r)); };
		PriorityGraph g;
		g.Id = "v";
		g.Nodes = { N("a", "combine", "", "sum"), N("b", "combine", "", "sum"), N("c", "combine", "", "sum"), N("o", "output", "eat") };
		g.Edges = { { "a", "b", "in", 1 }, { "b", "c", "in", 1 }, { "c", "a", "in", 1 }, { "c", "o", "in", 1 } };
		Check(Has(text(g), "cycle: a -> b -> c -> a"), "a cycle is named: " + text(g));
		g.Edges.pop_back(); g.Edges.pop_back(); g.Edges.pop_back();
		g.Edges.push_back({ "a", "zzz", "in", 1 });
		Check(Has(text(g), "does not exist ('zzz')"), "an edge to a missing node: " + text(g));
		g.Edges = { { "c", "o", "in", 1 } };
		g.Nodes = { N("i", "input", "need.hungre"), N("c", "combine", "", "sum"), N("o", "output", "fly") };
		g.Edges = { { "i", "c", "in", 1 }, { "c", "o", "in", 1 } };
		Check(Has(text(g), "input 'need.hungre' does not exist"), "an unknown input: " + text(g));
		Check(Has(text(g), "output 'fly' is not a kind"), "an unknown output: " + text(g));
		g.Nodes = { N("i", "input", "need.hunger"), N("o1", "output", "eat"), N("o2", "output", "eat") };
		g.Edges = { { "i", "o1", "in", 1 }, { "i", "o2", "in", 1 } };
		Check(Has(text(g), "already exists in node 'o1'"), "two outputs of a kind: " + text(g));
		g.Nodes = { N("i", "input", "need.hunger"), N("c", "curve", "", "wobble"), N("o", "output", "eat") };
		g.Edges = { { "i", "c", "in", 1 }, { "c", "o", "in", 1 } };
		Check(Has(text(g), "unknown curve 'wobble'"), "an unknown curve: " + text(g));
		g.Nodes = { N("i", "input", "need.hunger"), N("i", "input", "need.rest"), N("o", "output", "eat") };
		g.Edges = { { "i", "o", "in", 1 } };
		Check(Has(text(g), "used by more than one node"), "duplicate ids: " + text(g));
		g.Nodes = { N("i", "input", "need.hunger"), N("k", "const", "", "", { { "value", 1 } }), N("o", "output", "eat") };
		g.Edges = { { "i", "k", "in", 1 }, { "k", "o", "in", 1 } };
		Check(Has(text(g), "takes no connections"), "a connection into an input: " + text(g));

		// subgraphs: missing, recursive, bad port, no return
		PriorityLibrary lib;
		PriorityGraph a, b;
		a.Id = "A"; b.Id = "B";
		a.Nodes = { N("p", "param"), N("s", "subgraph", "B"), N("r", "return") };
		a.Edges = { { "p", "s", "p", 1 }, { "s", "r", "in", 1 } };
		b.Nodes = { N("p", "param"), N("s", "subgraph", "A"), N("r", "return") };
		b.Edges = { { "p", "s", "p", 1 }, { "s", "r", "in", 1 } };
		lib.Edit("builtin");   // (touch)
		PriorityLibrary lib2;
		Check(lib2.Add(b).find("does not exist in the library") != std::string::npos, "an include of a graph that is not there is refused: " + lib2.Add(b));
		Check(lib2.Find("B") == nullptr, "and the refused graph did not get in");
		PriorityLibrary lib3;
		PriorityGraph leaf = b;
		leaf.Nodes = { N("p", "param"), N("r", "return") };
		leaf.Edges = { { "p", "r", "in", 1 } };
		Check(lib3.Add(leaf).empty(), "B alone is fine");
		a.Nodes[1].Source = "B";
		Check(lib3.Add(a).empty(), "A includes B: fine");
		b.Nodes[1].Source = "A";
		const std::string rec = lib3.Add(b, true);
		Check(Has(rec, "recursive include: B -> A -> B") || Has(rec, "recursive include"), "A -> B -> A is refused: " + rec);
		Check(lib3.Find("B")->Nodes.size() == 2, "and B stays as it was");
		PriorityGraph bad = a;
		bad.Id = "badport";
		bad.Edges[0].Port = "nope";
		Check(Has(lib3.Add(bad), "does not declare as a parameter"), "a port the fragment does not declare: " + lib3.Add(bad));
		PriorityGraph noret = leaf;
		noret.Id = "noret";
		noret.Nodes.pop_back(); noret.Edges.clear();
		PriorityGraph user;
		user.Id = "user";
		user.Nodes = { N("s", "subgraph", "noret"), N("o", "output", "eat") };
		user.Edges = { { "s", "o", "in", 1 } };
		Check(lib3.Add(noret).empty() && Has(lib3.Add(user), "no 'return' node"), "a graph with no return cannot be included");
	}

	// ---- curves and combiners
	{
		Check(Near(Curve("linear", { { "a", 2 }, { "b", 1 } }, 3), 7), "linear");
		Check(Near(Curve("ramp", { { "lo", 10 }, { "hi", 20 } }, 15), 0.5f) && Curve("ramp", { { "lo", 10 }, { "hi", 20 } }, 25) == 1.0f && Curve("ramp", { { "lo", 10 }, { "hi", 20 } }, 5) == 0.0f, "ramp");
		Check(Near(Curve("sigmoid", { { "mid", 12 }, { "k", 1 } }, 12), 0.5f) && Curve("sigmoid", { { "mid", 12 }, { "k", 1 } }, 30) > 0.99f && Curve("sigmoid", { { "mid", 12 }, { "k", 1 } }, 0) < 0.01f, "sigmoid");
		Check(Near(Curve("inverse", { { "lo", 0 }, { "hi", 24 } }, 6), 0.75f), "inverse");
		Check(Curve("threshold", { { "at", 12 } }, 12) == 1.0f && Curve("threshold", { { "at", 12 } }, 11.9f) == 0.0f && Curve("threshold", { { "at", 12 }, { "high", 3 }, { "low", 2 } }, 20) == 3.0f, "threshold");
		Check(Curve("urgency", { { "from", 60 } }, 30) == Urgency(30, 60) && Curve("falloff", { { "half", 60 } }, 60) == Falloff(60, 60) && Near(Curve("falloff", { { "half", 60 } }, 60), 0.5f), "urgency and falloff are the ones the tribe uses");
		Check(Near(Combine("mul", { { 2, 1 }, { 3, 1 }, { 4, 0.5 } }), 12) && Near(Combine("sum", { { 2, 1 }, { 3, 2 } }), 8), "mul and sum with weights");
		Check(Near(Combine("min", { { 2, 1 }, { 3, 1 } }), 2) && Near(Combine("max", { { 2, 1 }, { 3, 1 } }), 3), "min and max");
		Check(Near(Combine("weighted", { { 10, 1 }, { 20, 3 } }), 17.5f), "weighted average");
		Check(Near(Combine("mul", {}), 1.0f) && Near(Combine("sum", {}), 0.0f), "empty combiners");
	}

	// ---- the templates rebuild the formulas of the tribe, exactly
	{
		CompiledPriorityGraph formulas, raw;
		Check(CompilePriorityGraph(MakeFormulasPriorityGraph(), {}, formulas) && CompilePriorityGraph(MakeRawPriorityGraph(), {}, raw), "both templates compile");
		bool exact = true;
		for (float u = 0.0f; u <= 1.0f; u += 0.0625f)
			for (float reach : { 1.0f, 0.5f, 0.123f })
			{
				const float eat = 2.2f * u * reach, drink = 2.0f * u * reach, regroup = 1.5f * u;
				exact = exact && Output(formulas, "eat", Option("eat", 9, { { "hunger", u }, { "weight", 2.2f }, { "distance", reach } }), {}) == eat;
				exact = exact && Output(formulas, "drink", Option("drink", 9, { { "thirst", u }, { "weight", 2.0f }, { "distance", reach } }), {}) == drink;
				exact = exact && Output(formulas, "regroup", Option("regroup", 9, { { "fear", u } }), {}) == regroup;
				const float w = 0.3f + u, need = std::clamp(1.0f - u, 0.1f, 1.0f), conf = 0.9f - 0.3f * u;
				exact = exact && Output(formulas, "gather", Option("gather", 9, { { "role", w }, { "need", need }, { "distance", reach }, { "memory", conf } }), {}) == w * (0.4f + 0.7f * need) * reach * conf;
				exact = exact && Output(formulas, "talk", Option("talk", 0.77f, {}), {}) == 0.77f;   // what it does not rebuild passes on
			}
		Check(exact, "builtin_formulas gives exactly the score of eat, drink, regroup and gather, and passes the rest on");

		bool exactRaw = true;
		for (float level : { 0.0f, 7.5f, 20.0f, 33.3f, 49.0f, 60.0f, 70.0f, 95.0f, 100.0f })
			for (float d : { 0.0f, 3.0f, 45.0f, 200.0f })
				for (float night : { 0.0f, 1.0f })
				{
					PgInputs in;
					in.Values["need.hunger"] = in.Values["need.thirst"] = in.Values["need.rest"] = in.Values["need.warmth"] = in.Values["need.safety"] = level;
					in.Values["option.distance"] = d;
					in.Values["time.night"] = night;
					const UtilityOption any = Option("x", 1.0f, {});
					exactRaw = exactRaw && Output(raw, "eat", any, in) == 2.2f * Urgency(level, 60.0f) * Falloff(d, 60.0f);
					exactRaw = exactRaw && Output(raw, "drink", any, in) == 2.0f * Urgency(level, 60.0f) * Falloff(d, 80.0f);
					exactRaw = exactRaw && Output(raw, "warm", any, in) == 1.5f * Urgency(level, 50.0f) * Falloff(d, 60.0f);
					exactRaw = exactRaw && Output(raw, "regroup", any, in) == 1.5f * Urgency(level, 60.0f);
					const float tired = 1.7f * Urgency(level, 70.0f, 1.2f);
					const float dark = night != 0.0f ? 1.3f * Ramp(95.0f - level, 0.0f, 40.0f) : 0.0f;
					exactRaw = exactRaw && Output(raw, "sleep", any, in) == (tired + dark) * Falloff(d, 120.0f);
				}
		Check(exactRaw, "builtin_raw gives exactly the score of eat, drink, warm, regroup and sleep from the needs");
	}

	// ---- subgraphs: the example includes a fragment
	{
		PriorityLibrary lib;
		CompiledPriorityGraph cautious;
		std::vector<PgIssue> issues;
		Check(CompilePriorityGraph(*lib.Find("cautious"), lib.Resolver(), cautious, &issues), "the example with a subgraph compiles: " + PgIssuesText(issues));
		bool inlined = false;
		for (const PgNode& n : cautious.Nodes) inlined = inlined || n.Id == "fear/urgent";
		Check(inlined, "the included graph is expanded as 'fear/urgent'");
		PgInputs in;
		in.Values["danger"] = 0.5f;
		in.Values["need.safety"] = 30.0f;
		const PgResult r = EvaluatePriorityGraph(cautious, in);
		const PgOutput* explore = r.For("explore");
		const PgOutput* regroup = r.For("regroup");
		Check(explore && explore->Mode == "multiply" && Near(explore->Value, 1.0f - 0.7f * 0.5f), "explore is scaled down by danger");
		Check(regroup && Near(regroup->Value, 1.0f + 0.5f * Urgency(30.0f, 60.0f)), "regroup uses the urgency of the fragment");
		Check(r.For("eat") == nullptr, "no output for eat: the tribe's score stays");
		Check(ApplyPgMode("multiply", 2.0f, 0.5f) == 1.0f && ApplyPgMode("add", 2.0f, 0.5f) == 2.5f && ApplyPgMode("replace", 2.0f, 0.5f) == 0.5f, "the modes");
		// a fragment alone is not assignable
		Check(Has(lib.AssignRole("x", "fragment_need_urgency"), "fragment"), "a fragment cannot be assigned to a role");
	}

	// ---- determinism and the identity
	{
		PriorityLibrary lib;
		CompiledPriorityGraph a, b;
		CompilePriorityGraph(*lib.Find("builtin_raw"), {}, a);
		CompilePriorityGraph(*lib.Find("builtin_raw"), {}, b);
		PgInputs in;
		in.Values["need.hunger"] = 12.0f;
		in.Values["option.distance"] = 30.0f;
		const PgResult r1 = EvaluatePriorityGraph(a, in), r2 = EvaluatePriorityGraph(b, in), r3 = EvaluatePriorityGraph(a, in);
		bool same = r1.Values == r2.Values && r1.Values == r3.Values && r1.Outputs.size() == r2.Outputs.size();
		Check(same, "the same inputs give the same values, twice and from two compilations");
		CompiledPriorityGraph id;
		CompilePriorityGraph(*lib.Find("builtin"), {}, id);
		Check(id.Identity, "builtin is the identity graph");
		Check(!a.Identity, "the others are not");
	}

	// ---- the library
	{
		PriorityLibrary lib;
		Check(lib.Duplicate("cautious", "cautious2").empty() && lib.Find("cautious2") && lib.Find("cautious2")->Nodes.size() == lib.Find("cautious")->Nodes.size(), "duplicate");
		Check(Has(lib.Duplicate("cautious", "cautious2"), "already exists"), "duplicate onto an existing id is refused");
		Check(Has(lib.Duplicate("cautious", "bad id!"), "not valid"), "bad ids are refused");
		Check(lib.SaveAsTemplate("cautious2", "tpl_careful", "Careful").empty() && lib.Find("tpl_careful")->Template, "save as template");
		Check(lib.Instantiate("tpl_careful", "mine").empty() && !lib.Find("mine")->Template && lib.Find("mine")->Revision == 1, "instantiate a template");
		Check(Has(lib.Instantiate("mine", "again"), "not a template"), "only templates are instantiated");
		std::string json;
		Check(lib.ExportJson("mine", json).empty(), "export");
		PriorityLibrary other;
		std::string id;
		Check(other.ImportJson(json, false, &id).empty() && id == "mine" && *other.Find("mine") == *lib.Find("mine"), "import gives the same graph");
		Check(Has(other.ImportJson(json), "already exists") && other.ImportJson(json, true).empty(), "import over an existing graph needs overwrite");
		Check(Has(other.ImportJson("{\"id\":\"w\",\"nodes\":[{\"id\":\"o\",\"type\":\"output\",\"source\":\"nope\"}]}"), "not a kind of option"), "an import with errors is refused with the reason");
		// use and removal
		Check(lib.AssignRole("woodcutter", "mine").empty() && lib.AssignMember("Ana", "cautious2").empty(), "assign to a role and to a member");
		Check(Has(lib.Remove("mine"), "role woodcutter") && Has(lib.Remove("fragment_need_urgency"), "graph cautious"), "a graph in use is not removed");
		Check(lib.IdFor("woodcutter", "Ana") == "cautious2" && lib.IdFor("woodcutter", "Bo") == "mine" && lib.IdFor("miner", "Cy") == "builtin", "the member beats the role, the role the default");
		Check(lib.CompiledFor("woodcutter", "Bo") != nullptr && lib.CompiledFor("woodcutter", "Bo") == lib.CompiledFor("woodcutter", "Dee"), "compiled once and shared");
		Check(Has(lib.AssignRole("miner", "nothing"), "no graph"), "assign a graph that does not exist");
		Check(lib.AssignRole("woodcutter", "").empty() && lib.IdFor("woodcutter", "Bo") == "builtin", "clear a role");
		Check(lib.Rename("cautious2", "careful2").empty() && lib.Members.at("Ana") == "careful2" && lib.Find("cautious2") == nullptr, "rename updates the assignments");
		// a graph edited into an invalid one is ignored by the tribe (not applied), and says why
		PriorityGraph* edit = lib.Edit("careful2");
		edit->Nodes.push_back(N("cut", "combine", "", "sum"));
		edit->Edges.push_back({ "cut", "cut", "in", 1.0 });
		Check(lib.CompiledFor("", "Ana") == nullptr && Has(PgIssuesText(lib.Validate("careful2")), "cycle"), "an invalid graph is not applied and validation says why");
		// files
		const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gb_priority_graph_test";
		std::filesystem::remove_all(dir);
		PriorityLibrary lib4;
		lib4.Duplicate("cautious", "kept");
		lib4.AssignRole("miner", "kept");
		Check(lib4.SaveDirectory(dir.string()).empty(), "save the folder");
		PriorityLibrary fresh;
		fresh.Remove("cautious");
		const std::string e = fresh.LoadDirectory(dir.string());
		Check(e.empty() && fresh.Find("kept") && *fresh.Find("kept") == *lib4.Find("kept") && fresh.Roles.at("miner") == "kept", "load the folder: graphs and assignments come back " + e);
		{
			std::ofstream bad(dir / "broken.json");
			bad << "{ not json";
		}
		PriorityLibrary fresh2;
		Check(Has(fresh2.LoadDirectory(dir.string()), "broken.json") && fresh2.Find("kept"), "a broken file is reported and the rest loads");
		Check(Has(fresh2.LoadAssignments("{\"roles\":{\"a\":\"ghost\"}}"), "no graph 'ghost'"), "assignments to a missing graph are refused");
		std::filesystem::remove_all(dir);
	}

	// ---- the tribe: with the builtin graph a simulation ends exactly as without graphs
	{
		WorldDesc desc;
		desc.Type = WorldType::Temperate;
		desc.Seed = 7;
		desc.Size = 192.0f;
		desc.Resolution = 129;
		desc.TreeBudget = 120;
		desc.RockBudget = 30;
		const GeneratedWorld world = WorldGenerator::GenerateData(desc);
		std::string logBase, logBuiltin, logForced, logZero;
		const std::string base = RunFingerprint(world, nullptr, 0.5, &logBase);
		Check(Has(base, "{") && !Has(base, "ERROR"), "the baseline run works");
		auto builtin = std::make_shared<PriorityLibrary>();
		const std::string withBuiltin = RunFingerprint(world, builtin, 0.5, &logBuiltin);
		Check(withBuiltin == base && logBuiltin == logBase, "the builtin graph (the default) ends exactly as no graphs");
		auto forced = std::make_shared<PriorityLibrary>();
		forced->ForceEvaluate = true;
		const std::string withForced = RunFingerprint(world, forced, 0.5, &logForced);
		Check(withForced == base && logForced == logBase, "the builtin graph evaluated for every option also ends exactly the same");
		auto zero = std::make_shared<PriorityLibrary>();
		PriorityGraph flat;
		flat.Id = "flat";
		flat.Nodes = { N("c", "const", "", "", { { "value", 0.0 } }), N("o", "output", "*", "multiply") };
		flat.Edges = { { "c", "o", "in", 1.0 } };
		Check(zero->Add(flat).empty() && zero->AssignDefault("flat").empty(), "a graph that zeroes every score is accepted");
		const std::string withZero = RunFingerprint(world, zero, 0.5, &logZero);
		Check(withZero != base, "a graph that changes the scores changes the simulation (the hook is live)");
		const std::string again = RunFingerprint(world, zero, 0.5);
		Check(again == withZero, "and it does so deterministically");
		// the member and role assignments reach the decision: Decide lists the "graph" factor for the one that has a graph
		SimSetup setup;
		setup.World = &world;
		setup.Members = 3;
		SimRun run(setup);
		auto lib = std::make_shared<PriorityLibrary>();
		lib->Add(flat);
		lib->DefaultGraph = "";
		run.Brain().Priorities = lib;
		const std::string target = run.Brain().Members().front().Name;
		lib->AssignMember(target, "flat");
		run.Run(30.0);
		bool graphFactor = false, others = false;
		for (const TribeMember& m : run.Brain().Members())
		{
			bool has = false;
			for (const UtilityOption& o : m.Mind.Options) for (const ScoreFactor& f : o.Factors) if (f.Name == "graph") has = true;
			(m.Name == target ? graphFactor : others) = (m.Name == target ? graphFactor : others) || has;
		}
		Check(graphFactor && !others, "only the member with a graph gets its options rescored (they show a 'graph' factor)");
	}

	std::fprintf(stderr, failures ? "%d failure(s)\n" : "priority graph tests passed\n", failures);
	return failures ? 1 : 0;
}
