#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// A priority graph: nodes that read inputs (needs, hour, distance, stock...), shape them with curves and combiners, and end in outputs (one per kind of
	// action option) that say what the option is worth. Pure data and pure evaluation: no Tribe, no clock, no randomness (docs/GRAFO_PRIORIDADES.md).

	inline constexpr int kPriorityGraphVersion = 1;   // the schema of the JSON

	struct PgNode
	{
		std::string Id, Type, Label;     // Type: input, const, curve, combine, clamp, output, subgraph, param, return
		float X = 0.0f, Y = 0.0f;        // where the editor draws it
		std::string Source;              // input: the input name; output: the kind of option ("*" = any); subgraph: the id of the graph included
		std::string Fn;                  // curve: linear ramp sigmoid inverse threshold urgency falloff; combine: mul sum min max weighted; output: replace multiply add
		std::map<std::string, double> Params;
	};

	struct PgEdge
	{
		std::string From, To;
		std::string Port = "in";         // into a subgraph node: the param node of the included graph it feeds
		double Weight = 1.0;
	};

	struct PriorityGraph
	{
		std::string Id, Name, Description, Author;
		std::vector<std::string> Tags;
		bool Template = false;           // a plantilla in the library (Instantiate makes a graph from it)
		int Version = kPriorityGraphVersion;
		int Revision = 1;
		std::vector<PgNode> Nodes;
		std::vector<PgEdge> Edges;
		const PgNode* FindNode(const std::string& id) const;
		PgNode* FindNode(const std::string& id);
		bool operator==(const PriorityGraph&) const;
	};

	struct PgIssue
	{
		bool Error = true;               // false: a warning
		std::string Node;                // the node it is about ("" = the graph)
		std::string Message;
	};

	using PgResolver = std::function<const PriorityGraph*(const std::string&)>;   // graph id -> graph (null when unknown)

	// ---- what a graph may read and write
	struct PgInputInfo { std::string Name, Group, Help; float Min = 0.0f, Max = 1.0f; };
	const std::vector<PgInputInfo>& PriorityInputCatalog();            // fixed names; factor.*, store.*, skill.*, trait.* are open families
	bool IsKnownPriorityInput(const std::string& name);
	const std::vector<std::string>& PriorityKnownKinds();              // the kinds of option the tribe produces
	bool IsKnownPriorityKind(const std::string& kind);                 // "*", a known kind, or "clan_..."
	const std::vector<std::string>& PriorityNodeTypes();
	const std::vector<std::string>& PriorityFunctions(const std::string& type);   // for curve, combine and output

	// ---- JSON (versioned). On a mistake they return why and change nothing.
	std::string PriorityGraphToJson(const PriorityGraph& graph);
	std::string PriorityGraphFromJson(const std::string& json, PriorityGraph& out);

	// ---- validation: errors (the graph cannot run) and warnings, with clear messages
	std::vector<PgIssue> ValidatePriorityGraph(const PriorityGraph& graph, const PgResolver& resolve = {});
	bool HasPgErrors(const std::vector<PgIssue>& issues);
	std::string PgIssuesText(const std::vector<PgIssue>& issues);

	// ---- compile and evaluate
	struct CompiledPriorityGraph
	{
		std::string Id;
		std::vector<PgNode> Nodes;                         // subgraphs expanded; their nodes are named "<subgraph node>/<node>"
		std::vector<std::vector<std::pair<int, float>>> In;   // per node: (source node, edge weight) in list order
		std::vector<int> Order;                            // topological order
		std::vector<int> Outputs;                          // indices of the output nodes
		bool Identity = false;                             // it only hands the score on: a single "*" replace output fed by option.score with weight 1
	};
	// Fills `out`; false (and `out` empty) when the graph has errors (they are in `issues`).
	bool CompilePriorityGraph(const PriorityGraph& graph, const PgResolver& resolve, CompiledPriorityGraph& out, std::vector<PgIssue>* issues = nullptr);

	struct PgInputs
	{
		std::map<std::string, float> Values;
		std::function<bool(const std::string&, float&)> Resolver;   // names not in Values (store.wood, factor.x...)
		bool Get(const std::string& name, float& out) const;
	};

	struct PgOutput { std::string Kind, Mode, Node; float Value = 0.0f; };
	struct PgResult
	{
		std::vector<PgOutput> Outputs;
		std::vector<float> Values;                         // per node of the compiled graph
		const PgOutput* For(const std::string& kind) const;   // the output of that kind, else the "*" one, else null
	};
	PgResult EvaluatePriorityGraph(const CompiledPriorityGraph& graph, const PgInputs& inputs);
	// Convenience: compiles every time (null result outputs when the graph is invalid).
	PgResult EvaluatePriorityGraph(const PriorityGraph& graph, const PgInputs& inputs, const PgResolver& resolve = {});
	// What an output does to a score: replace -> value; multiply -> score * value; add -> score + value.
	float ApplyPgMode(const std::string& mode, float score, float value);

	// The graph that hands the score on (the default; exact by construction), and the templates that rebuild the formulas of the tribe.
	PriorityGraph MakeBuiltinPriorityGraph();
	PriorityGraph MakeFormulasPriorityGraph();   // "builtin_formulas": eat, drink, regroup, gather from the recorded factors
	PriorityGraph MakeRawPriorityGraph();        // "builtin_raw": eat, drink, sleep, warm, regroup from the needs and the distance
	PriorityGraph MakeNeedUrgencyFragment();     // "fragment_need_urgency": param level -> urgency
	PriorityGraph MakeCautiousPriorityGraph();   // "cautious": an example that includes the fragment
}
