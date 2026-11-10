#pragma once
#include "GB/World/Graph/TerrainGraph.h"
#include <string>

namespace GB
{
	// A terrain graph as text (docs/GRAFO_TERRENO.md, section 10.5), in the shape of PriorityGraphToJson/FromJson (GB/AI/PriorityGraph.h):
	//
	//   { "kind": "terrain_graph", "version": 2, "id": "...", "name": "...",
	//     "nodes": [ { "id", "type", "label", "source", "x", "y", "params": { name: number } } ],
	//     "edges": [ { "from", "to", "port", "fromPort" } ] }
	//
	// Lossless: the parameters are written as full doubles and the nodes and edges keep their order (a version 1 edge binds by edge order and the
	// evaluation order breaks ties by node order), so a graph read back evaluates bit for bit like the one written.
	std::string TerrainGraphToJson(const TerrainGraph& graph);
	// "" when it was read; otherwise why not, and `out` is left untouched. A graph newer than kTerrainGraphVersion is refused; an older one keeps
	// the version it was written with (version 1 edges are still understood by CompileTerrainGraph).
	std::string TerrainGraphFromJson(const std::string& json, TerrainGraph& out);
}
