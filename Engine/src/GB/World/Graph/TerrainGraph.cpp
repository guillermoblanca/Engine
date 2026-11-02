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
		std::vector<std::vector<int>> adj(graph.Nodes.size());
		for (const TgEdge& e : graph.Edges)
		{
			const int from = NodeIndex(graph, e.From), to = NodeIndex(graph, e.To);
			if (from < 0) { Error(e.To, "edge from unknown node '" + e.From + "'"); continue; }
			if (to < 0) { Error(e.From, "edge to unknown node '" + e.To + "'"); continue; }
			adj[from].push_back(to);
		}

		// cycles (only once the graph is otherwise well formed: a dangling edge already failed above)
		if (!HasTgErrors(issues))
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
		out.In.assign(graph.Nodes.size(), {});
		std::vector<int> inDegree(graph.Nodes.size(), 0);
		std::vector<std::vector<int>> outAdj(graph.Nodes.size());
		for (const TgEdge& e : graph.Edges)
		{
			const int from = NodeIndex(graph, e.From), to = NodeIndex(graph, e.To);
			out.In[to].push_back(from);   // in edge order, as declared
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
		std::vector<TgField> fields(graph.Nodes.size());
		std::vector<bool> ready(graph.Nodes.size(), false);

		for (int index : graph.Order)
		{
			const TgNode& node = graph.Nodes[index];
			const ITerrainNode* impl = registry.Find(node.Type);
			if (!impl) { error = "node '" + node.Id + "' has unregistered type '" + node.Type + "'"; return false; }

			std::vector<const TgField*> inputs;
			inputs.reserve(graph.In[index].size());
			for (int src : graph.In[index]) inputs.push_back(ready[src] ? &fields[src] : nullptr);

			if (!impl->Evaluate(node, ctx, inputs, fields[index], error))
			{
				if (error.empty()) error = "node '" + node.Id + "' failed";
				return false;
			}
			ready[index] = true;

			if (const IScatterNode* scatter = impl->AsScatterNode())
			{
				TgPlacements placements;
				if (!scatter->Scatter(node, ctx, inputs, placements, error))
				{
					if (error.empty()) error = "node '" + node.Id + "' failed to scatter";
					return false;
				}
				out.Placements[node.Id] = std::move(placements);
			}

			if (const IPathNode* tracer = impl->AsPathNode())
			{
				TgPaths paths;
				if (!tracer->Trace(node, ctx, inputs, paths, error))
				{
					if (error.empty()) error = "node '" + node.Id + "' failed to trace";
					return false;
				}
				out.Paths[node.Id] = std::move(paths);
			}
		}

		for (int index : graph.Order)
		{
			const TgNode& node = graph.Nodes[index];
			if (node.Type == "output" && !node.Source.empty()) out.Outputs[node.Source] = fields[index];
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
