#include "GB/World/Graph/TerrainGraph.h"

namespace GB
{
	const TgNode* TerrainGraph::FindNode(const std::string& id) const
	{
		for (const TgNode& n : Nodes) if (n.Id == id) return &n;
		return nullptr;
	}
	TgNode* TerrainGraph::FindNode(const std::string& id)
	{
		for (TgNode& n : Nodes) if (n.Id == id) return &n;
		return nullptr;
	}

	bool HasTgErrors(const std::vector<TgIssue>& issues)
	{
		for (const TgIssue& i : issues) if (i.Error) return true;
		return false;
	}
	std::string TgIssuesText(const std::vector<TgIssue>& issues)
	{
		std::string text;
		for (const TgIssue& i : issues)
		{
			if (!text.empty()) text += "\n";
			text += (i.Error ? "error" : "warning");
			if (!i.Node.empty()) { text += " ["; text += i.Node; text += "]"; }
			text += ": ";
			text += i.Message;
		}
		return text;
	}

	namespace
	{
		int NodeIndex(const TerrainGraph& g, const std::string& id)
		{
			for (size_t i = 0; i < g.Nodes.size(); ++i) if (g.Nodes[i].Id == id) return static_cast<int>(i);
			return -1;
		}

		// A coloured DFS (white/grey/black) that names the cycle it finds, same approach as ValidatePriorityGraph.
		bool FindCycle(const TerrainGraph& g, const std::vector<std::vector<int>>& adj, std::string& pathText)
		{
			std::vector<int> color(g.Nodes.size(), 0);   // 0 white, 1 grey, 2 black
			std::vector<int> stack;
			std::function<bool(int)> visit = [&](int node) -> bool
			{
				color[node] = 1;
				stack.push_back(node);
				for (int next : adj[node])
				{
					if (color[next] == 1)
					{
						std::string path;
						for (size_t i = 0; i < stack.size(); ++i)
						{
							if (!path.empty()) path += " -> ";
							path += g.Nodes[stack[i]].Id;
						}
						path += " -> " + g.Nodes[next].Id;
						pathText = path;
						return true;
					}
					if (color[next] == 0 && visit(next)) return true;
				}
				stack.pop_back();
				color[node] = 2;
				return false;
			};
			for (size_t i = 0; i < g.Nodes.size(); ++i)
				if (color[i] == 0 && visit(static_cast<int>(i))) return true;
			return false;
		}
	}

	namespace
	{
		// Binds every edge to an input port of its target and an output port of its source (TgEdge::Port/FromPort), checks the kinds agree and
		// that every required input is fed. Explicitly named inputs are bound first; then the version 1 edges ("in", not a port of the node) take
		// the free inputs in port order, in edge order -- the wiring those graphs always had. Edges touching an unknown node or type are left out
		// (they are reported elsewhere).
		std::vector<std::vector<TgSource>> BindPorts(const TerrainGraph& graph, const TerrainNodeRegistry& registry, std::vector<TgIssue>& issues)
		{
			auto Error = [&](const std::string& node, const std::string& message) { issues.push_back({ true, node, message }); };
			std::vector<const TgNodeSchema*> schemas(graph.Nodes.size(), nullptr);
			std::vector<std::vector<TgSource>> in(graph.Nodes.size());
			for (size_t i = 0; i < graph.Nodes.size(); ++i)
			{
				schemas[i] = registry.Schema(graph.Nodes[i].Type);
				if (schemas[i]) in[i].resize(schemas[i]->Inputs.size());
			}
			auto bind = [&](const TgEdge& e, int from, int to, int slot)
			{
				const TgNodeSchema& source = *schemas[static_cast<size_t>(from)];
				int index = source.OutputIndex(e.FromPort);
				if (index < 0 && e.FromPort == "out" && !source.Outputs.empty()) index = 0;
				if (index < 0) { Error(e.From, "no output named '" + e.FromPort + "'"); return; }
				const TgPortInfo& output = source.Outputs[static_cast<size_t>(index)];
				const TgPortInfo& input = schemas[static_cast<size_t>(to)]->Inputs[static_cast<size_t>(slot)];
				if (output.Kind != input.Kind && output.Kind != TgPortKind::Any && input.Kind != TgPortKind::Any)
					Error(e.To, "input '" + input.Name + "' takes " + TgPortKindName(input.Kind) + ", but '" + e.From + "." + output.Name + "' gives " + TgPortKindName(output.Kind));
				in[static_cast<size_t>(to)][static_cast<size_t>(slot)] = { from, output.Name };
			};
			std::vector<const TgEdge*> legacy;
			for (const TgEdge& e : graph.Edges)
			{
				const int from = NodeIndex(graph, e.From), to = NodeIndex(graph, e.To);
				if (from < 0 || to < 0 || !schemas[static_cast<size_t>(from)] || !schemas[static_cast<size_t>(to)]) continue;
				const int slot = schemas[static_cast<size_t>(to)]->InputIndex(e.Port);
				if (slot < 0) { legacy.push_back(&e); continue; }
				if (in[static_cast<size_t>(to)][static_cast<size_t>(slot)].Node >= 0) { Error(e.To, "two edges feed input '" + e.Port + "'"); continue; }
				bind(e, from, to, slot);
			}
			for (const TgEdge* e : legacy)
			{
				const int from = NodeIndex(graph, e->From), to = NodeIndex(graph, e->To);
				if (e->Port != "in") { Error(e->To, "no input named '" + e->Port + "'"); continue; }
				const std::vector<TgSource>& slots = in[static_cast<size_t>(to)];
				int slot = -1;
				for (size_t i = 0; i < slots.size() && slot < 0; ++i) if (slots[i].Node < 0) slot = static_cast<int>(i);
				if (slot < 0) { Error(e->To, "no free input for the edge from '" + e->From + "'"); continue; }
				bind(*e, from, to, slot);
			}
			for (size_t i = 0; i < graph.Nodes.size(); ++i)
				if (schemas[i])
					for (size_t k = 0; k < schemas[i]->Inputs.size(); ++k)
						if (!schemas[i]->Inputs[k].Optional && in[i][k].Node < 0) Error(graph.Nodes[i].Id, "input '" + schemas[i]->Inputs[k].Name + "' is not connected");
			return in;
		}
	}

	std::vector<TgIssue> ValidateTerrainGraph(const TerrainGraph& graph, const TerrainNodeRegistry& registry)
	{
		std::vector<TgIssue> issues;
		auto Error = [&](const std::string& node, const std::string& message) { issues.push_back({ true, node, message }); };

		// duplicate ids
		for (size_t i = 0; i < graph.Nodes.size(); ++i)
			for (size_t j = i + 1; j < graph.Nodes.size(); ++j)
				if (graph.Nodes[i].Id == graph.Nodes[j].Id) Error(graph.Nodes[i].Id, "duplicate node id");

		// unknown types
		for (const TgNode& n : graph.Nodes)
			if (!registry.IsKnown(n.Type)) Error(n.Id, "unknown node type '" + n.Type + "'");

		// edges to/from nodes that do not exist
		bool dangling = false;
		std::vector<std::vector<int>> adj(graph.Nodes.size());
		for (const TgEdge& e : graph.Edges)
		{
			const int from = NodeIndex(graph, e.From), to = NodeIndex(graph, e.To);
			if (from < 0) { Error(e.To, "edge from unknown node '" + e.From + "'"); dangling = true; continue; }
			if (to < 0) { Error(e.From, "edge to unknown node '" + e.To + "'"); dangling = true; continue; }
			if (e.From == e.To) Error(e.To, "self-connection is not allowed");
			if (e.Port.empty()) Error(e.To, "edge has an empty input port");
			adj[from].push_back(to);
		}

		// ports: every edge lands on an input that exists and takes what it gives; every required input is fed
		BindPorts(graph, registry, issues);

		// cycles (once every edge joins two nodes that exist; a port problem does not hide a cycle)
		if (!dangling)
		{
			std::string path;
			if (FindCycle(graph, adj, path)) Error("", "cycle: " + path);
		}

		return issues;
	}

	bool CompileTerrainGraph(const TerrainGraph& graph, const TerrainNodeRegistry& registry, CompiledTerrainGraph& out, std::vector<TgIssue>* issues)
	{
		out = CompiledTerrainGraph();
		std::vector<TgIssue> local = ValidateTerrainGraph(graph, registry);
		if (issues) *issues = local;
		if (HasTgErrors(local)) return false;

		out.Nodes = graph.Nodes;
		std::vector<TgIssue> ignored;
		out.In = BindPorts(graph, registry, ignored);
		for (const TgNode& n : graph.Nodes) out.Schemas.push_back(*registry.Schema(n.Type));
		std::vector<int> inDegree(graph.Nodes.size(), 0);
		std::vector<std::vector<int>> outAdj(graph.Nodes.size());
		for (const TgEdge& e : graph.Edges)
		{
			const int from = NodeIndex(graph, e.From), to = NodeIndex(graph, e.To);
			outAdj[from].push_back(to);
			++inDegree[to];
		}

		// Kahn's algorithm; nodes with the same in-degree keep file order, so the compiled order is deterministic.
		std::vector<int> ready;
		for (size_t i = 0; i < graph.Nodes.size(); ++i) if (inDegree[i] == 0) ready.push_back(static_cast<int>(i));
		size_t head = 0;
		while (head < ready.size())
		{
			const int node = ready[head++];
			out.Order.push_back(node);
			for (int next : outAdj[node]) if (--inDegree[next] == 0) ready.push_back(next);
		}
		return true;   // ValidateTerrainGraph already rejected cycles, so every node is reachable here
	}

	bool EvaluateTerrainGraph(const CompiledTerrainGraph& graph, const TerrainNodeRegistry& registry, const TgContext& ctx, TgResult& out, std::string& error)
	{
		out = TgResult();
		std::vector<std::map<std::string, TgValue>> values(graph.Nodes.size());
		std::vector<bool> ready(graph.Nodes.size(), false);

		for (int index : graph.Order)
		{
			const TgNode& node = graph.Nodes[index];
			const ITerrainNode* impl = registry.Find(node.Type);
			if (!impl) { error = "node '" + node.Id + "' has unregistered type '" + node.Type + "'"; return false; }

			std::vector<const TgValue*> inputs;
			inputs.reserve(graph.In[index].size());
			for (const TgSource& src : graph.In[index])
			{
				const TgValue* v = nullptr;
				if (src.Node >= 0 && ready[src.Node])
				{
					const auto it = values[src.Node].find(src.Port);
					if (it != values[src.Node].end()) v = &it->second;
				}
				inputs.push_back(v);
			}

			if (!impl->Run(node, ctx, inputs, values[index], error))
			{
				if (error.empty()) error = "node '" + node.Id + "' failed";
				return false;
			}
			ready[index] = true;
		}

		for (int index : graph.Order)
		{
			const TgNode& node = graph.Nodes[index];
			std::map<std::string, TgValue>& produced = values[index];
			if (node.Type == "output" && !node.Source.empty())
			{
				const auto it = produced.find("out");
				if (it != produced.end())
					switch (it->second.Kind)
					{
					case TgPortKind::Field: out.Outputs[node.Source] = it->second.Field; break;
					case TgPortKind::Placements: out.PlacementOutputs[node.Source] = it->second.Placements; break;
					case TgPortKind::Paths: out.PathOutputs[node.Source] = it->second.Paths; break;
					case TgPortKind::Water: out.WaterOutputs[node.Source] = it->second.Water; break;
					default: break;
					}
			}
			// the first Placements and the first Paths a node produced, by its id (what the callers of the scatter and path nodes read)
			for (const auto& [port, value] : produced)
			{
				if (value.Kind == TgPortKind::Placements && !out.Placements.count(node.Id)) out.Placements[node.Id] = value.Placements;
				if (value.Kind == TgPortKind::Paths && !out.Paths.count(node.Id)) out.Paths[node.Id] = value.Paths;
			}
			out.Nodes[node.Id] = std::move(produced);
		}
		return true;
	}

	bool EvaluateTerrainGraph(const TerrainGraph& graph, const TerrainNodeRegistry& registry, const TgContext& ctx, TgResult& out, std::string& error)
	{
		CompiledTerrainGraph compiled;
		std::vector<TgIssue> issues;
		if (!CompileTerrainGraph(graph, registry, compiled, &issues)) { error = TgIssuesText(issues); return false; }
		return EvaluateTerrainGraph(compiled, registry, ctx, out, error);
	}
}
