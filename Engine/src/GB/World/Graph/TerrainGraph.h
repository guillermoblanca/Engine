#pragma once
#include "GB/World/Graph/ITerrainNode.h"
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// A terrain generation graph: nodes that build and reshape TgFields (height, moisture, biome id...) and
	// feed one another, ending in one or more "output" nodes. Pure data (SRP): nothing here knows how to
	// validate, compile or evaluate itself (docs/GRAFO_TERRENO.md). Modelled directly on GB::PgNode/PgEdge/
	// PriorityGraph (GB/AI/PriorityGraph.h), which plays the same role for the AI's utility scoring.

	// The schema below. 2: edges name the output they leave from (TgEdge::FromPort) and inputs are bound by port name; a version 1 graph (edges
	// to "in", inputs in edge order) still compiles to the same wiring.
	inline constexpr int kTerrainGraphVersion = 2;

	struct TgNode
	{
		std::string Id, Type, Label;   // Type: the key a node is registered under in TerrainNodeRegistry
		float X = 0.0f, Y = 0.0f;      // where the editor draws it
		std::string Source;            // output node: the slot this writes ("height", "biome", "moisture"...)
		std::map<std::string, double> Params;
	};

	struct TgEdge
	{
		std::string From, To;
		// The input of `To` this edge feeds, by name (TgNodeSchema::Inputs). "in", when the node has no input of that name, takes the next free
		// input in port order: how version 1 graphs were wired, by edge order.
		std::string Port = "in";
		// The output of `From` it leaves from (TgNodeSchema::Outputs). "out", when the node has no output of that name, is its first output.
		std::string FromPort = "out";
	};

	struct TerrainGraph
	{
		std::string Id, Name;
		int Version = kTerrainGraphVersion;
		std::vector<TgNode> Nodes;
		std::vector<TgEdge> Edges;

		const TgNode* FindNode(const std::string& id) const;
		TgNode* FindNode(const std::string& id);
	};

	struct TgIssue
	{
		bool Error = true;   // false: a warning, the graph still runs
		std::string Node;    // the node it is about ("" = the graph as a whole)
		std::string Message;
	};

	// Structural problems only: duplicate ids, edges to a node that does not exist, cycles (found the same way
	// as ValidatePriorityGraph: a coloured DFS that names the path), and node types unknown to
	// the `registry` it is given (the caller owns it: see MakeDefaultTerrainNodeRegistry). Does not run any node.
	std::vector<TgIssue> ValidateTerrainGraph(const TerrainGraph& graph, const TerrainNodeRegistry& registry);
	bool HasTgErrors(const std::vector<TgIssue>& issues);
	std::string TgIssuesText(const std::vector<TgIssue>& issues);

	// A graph reduced to an evaluation order: a topological sort (Kahn's algorithm, as in CompilePriorityGraph)
	// plus, per node and per input port of its schema, which node and which of its outputs feeds it.
	struct TgSource
	{
		int Node = -1;        // -1: the port is unconnected
		std::string Port;     // the output of that node
	};
	struct CompiledTerrainGraph
	{
		std::vector<TgNode> Nodes;
		std::vector<TgNodeSchema> Schemas;       // per node, from the registry it was compiled against
		std::vector<std::vector<TgSource>> In;   // per node: one entry per input port of its schema, in port order
		std::vector<int> Order;                  // topological order (indices into Nodes/In)
	};

	// Fills `out` from a graph with no structural errors; false (and `out` left empty) when it has any -- they
	// are in `issues`, same as ValidatePriorityGraph/CompilePriorityGraph.
	bool CompileTerrainGraph(const TerrainGraph& graph, const TerrainNodeRegistry& registry, CompiledTerrainGraph& out, std::vector<TgIssue>* issues = nullptr);

	struct TgResult
	{
		// What the "output" nodes publish, keyed by their Source, one map per kind of value they were given.
		std::map<std::string, TgField> Outputs;
		std::map<std::string, TgPlacements> PlacementOutputs;
		std::map<std::string, TgPaths> PathOutputs;
		std::map<std::string, WaterField> WaterOutputs;
		std::map<std::string, TgPlacements> Placements; // keyed by the Id of each node with a Placements output (its first one)
		std::map<std::string, TgPaths> Paths;           // keyed by the Id of each node with a Paths output (its first one)
		// Every output of every node, by node Id and port name: what an editor previews.
		std::map<std::string, std::map<std::string, TgValue>> Nodes;
	};

	// Pure evaluation: runs every node in topological order through the given TerrainNodeRegistry, with no side
	// effect on the engine (no Heightmap, no GPU, no file). A node that fails stops the walk; `error` says which
	// and why.
	bool EvaluateTerrainGraph(const CompiledTerrainGraph& graph, const TerrainNodeRegistry& registry, const TgContext& ctx, TgResult& out, std::string& error);
	// Convenience that compiles first (as EvaluatePriorityGraph does for a raw PriorityGraph).
	bool EvaluateTerrainGraph(const TerrainGraph& graph, const TerrainNodeRegistry& registry, const TgContext& ctx, TgResult& out, std::string& error);
}
