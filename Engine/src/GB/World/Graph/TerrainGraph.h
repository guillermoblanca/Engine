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

	inline constexpr int kTerrainGraphVersion = 1;   // the schema below

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
		std::string Port = "in";       // a node with several inputs tells them apart by port name
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
	// TerrainNodeRegistry. Does not run any node.
	std::vector<TgIssue> ValidateTerrainGraph(const TerrainGraph& graph);
	bool HasTgErrors(const std::vector<TgIssue>& issues);
	std::string TgIssuesText(const std::vector<TgIssue>& issues);

	// A graph reduced to an evaluation order: a topological sort (Kahn's algorithm, as in CompilePriorityGraph)
	// plus, per node, which compiled index feeds each of its incoming ports in edge order.
	struct CompiledTerrainGraph
	{
		std::vector<TgNode> Nodes;
		std::vector<std::vector<int>> In;   // per node: indices of the nodes that feed it, in edge order
		std::vector<int> Order;             // topological order (indices into Nodes/In)
	};

	// Fills `out` from a graph with no structural errors; false (and `out` left empty) when it has any -- they
	// are in `issues`, same as ValidatePriorityGraph/CompilePriorityGraph.
	bool CompileTerrainGraph(const TerrainGraph& graph, CompiledTerrainGraph& out, std::vector<TgIssue>* issues = nullptr);

	struct TgResult
	{
		std::map<std::string, TgField> Outputs;         // keyed by the Source of each "output" node that ran
		std::map<std::string, TgPlacements> Placements; // keyed by the Id of each node that is also an IScatterNode
	};

	// Pure evaluation: runs every node in topological order through TerrainNodeRegistry, with no side effect on
	// the engine (no Heightmap, no GPU, no file). A node that fails stops the walk; `error` says which and why.
	bool EvaluateTerrainGraph(const CompiledTerrainGraph& graph, const TgContext& ctx, TgResult& out, std::string& error);
	// Convenience that compiles first (as EvaluatePriorityGraph does for a raw PriorityGraph).
	bool EvaluateTerrainGraph(const TerrainGraph& graph, const TgContext& ctx, TgResult& out, std::string& error);
}
