#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/TerrainGraphCache.h"
#include <chrono>
#include <set>

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

	TerrainGraph TerrainGraphUpstreamOf(const TerrainGraph& graph, const std::vector<std::string>& nodeIds)
	{
		std::set<std::string> keep;
		std::vector<std::string> pending;
		for (const std::string& id : nodeIds)
			if (graph.FindNode(id) && keep.insert(id).second) pending.push_back(id);
		while (!pending.empty())
		{
			const std::string id = pending.back();
			pending.pop_back();
			for (const TgEdge& e : graph.Edges)
				if (e.To == id && graph.FindNode(e.From) && keep.insert(e.From).second) pending.push_back(e.From);
		}
		TerrainGraph out;
		out.Id = graph.Id;
		out.Name = graph.Name;
		out.Version = graph.Version;
		for (const TgNode& n : graph.Nodes)
			if (keep.count(n.Id)) out.Nodes.push_back(n);
		for (const TgEdge& e : graph.Edges)   // every edge into a kept node comes from a kept node: the wiring (and a version 1 edge order) is the same
			if (keep.count(e.To)) out.Edges.push_back(e);
		return out;
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

	namespace
	{
		// What decides the result of every node besides its own definition and inputs: the seed, the footprint and the fields brought in.
		uint64_t ContextKey(const TgContext& ctx)
		{
			uint64_t h = TgHashBytes(&ctx.Seed, sizeof ctx.Seed);
			h = TgHashBytes(&ctx.Resolution, sizeof ctx.Resolution, h);
			h = TgHashBytes(&ctx.Size, sizeof ctx.Size, h);
			h = TgHashBytes(&ctx.Center, sizeof ctx.Center, h);
			if (ctx.Inputs)
				for (const auto& [name, field] : *ctx.Inputs) h = TgHashField(field, TgHashString(name, h));
			return h;
		}

		// A node's cache key: its type, its Source, the exact bits of its parameters, then, port by port, the key of the node feeding it and the
		// output it is fed from (a change upstream changes every key below it), and the context. Not its label or position.
		uint64_t NodeKey(const TgNode& node, const std::vector<TgSource>& in, const std::vector<uint64_t>& keys, uint64_t context)
		{
			uint64_t h = TgHashString(node.Type, context);
			h = TgHashString(node.Source, h);
			for (const auto& [name, value] : node.Params) h = TgHashBytes(&value, sizeof value, TgHashString(name, h));
			for (const TgSource& src : in)
			{
				const uint64_t upstream = src.Node >= 0 ? keys[static_cast<size_t>(src.Node)] : 0;
				h = TgHashString(src.Port, TgHashBytes(&upstream, sizeof upstream, h));
			}
			return h;
		}
	}

	bool EvaluateTerrainGraph(const CompiledTerrainGraph& graph, const TerrainNodeRegistry& registry, const TgContext& ctx, TgResult& out, std::string& error,
		TerrainGraphCache* cache)
	{
		out = TgResult();
		std::vector<std::map<std::string, TgValue>> values(graph.Nodes.size());
		std::vector<bool> ready(graph.Nodes.size(), false);
		std::unique_lock<std::mutex> lock;
		std::vector<uint64_t> keys;
		uint64_t context = 0;
		using Clock = std::chrono::steady_clock;
		const Clock::time_point started = Clock::now();
		if (cache)
		{
			lock = std::unique_lock<std::mutex>(cache->m_Mutex);
			cache->m_Stats = {};
			cache->m_HasFootprint = true;
			cache->m_Size = ctx.Size;
			cache->m_Center = ctx.Center;
			keys.assign(graph.Nodes.size(), 0);
			context = ContextKey(ctx);
			// what is not a node of this graph any more is dropped (an id reused by the next graph would only miss: its key differs)
			std::set<std::string> ids;
			for (const TgNode& node : graph.Nodes) ids.insert(node.Id);
			for (auto it = cache->m_Entries.begin(); it != cache->m_Entries.end();) it = ids.count(it->first) ? std::next(it) : cache->m_Entries.erase(it);
		}

		for (int index : graph.Order)
		{
			const TgNode& node = graph.Nodes[index];
			const ITerrainNode* impl = registry.Find(node.Type);
			if (!impl) { error = "node '" + node.Id + "' has unregistered type '" + node.Type + "'"; return false; }

			if (cache)
			{
				keys[static_cast<size_t>(index)] = NodeKey(node, graph.In[index], keys, context);
				const auto it = cache->m_Entries.find(node.Id);
				if (it != cache->m_Entries.end() && it->second.Key == keys[static_cast<size_t>(index)])
				{
					values[index] = it->second.Outputs;
					ready[index] = true;
					++cache->m_Stats.Reused;
					continue;
				}
			}

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

			const Clock::time_point nodeStarted = Clock::now();
			if (!impl->Run(node, ctx, inputs, values[index], error))
			{
				if (error.empty()) error = "node '" + node.Id + "' failed";
				if (cache) { cache->m_Entries.erase(node.Id); cache->Tally(); ++cache->m_Generation; }
				return false;
			}
			ready[index] = true;
			if (cache)
			{
				const double ms = std::chrono::duration<double, std::milli>(Clock::now() - nodeStarted).count();
				TerrainGraphCache::Entry& entry = cache->m_Entries[node.Id];
				entry.Key = keys[static_cast<size_t>(index)];
				entry.Outputs = values[index];
				entry.Ms = ms;
				entry.Bytes = 0;
				for (const auto& [port, value] : entry.Outputs) entry.Bytes += TerrainGraphCache::BytesOf(value);
				cache->m_Stats.RunMs[node.Id] = ms;
				++cache->m_Stats.Ran;
			}
		}
		if (cache)
		{
			cache->FitBudget();   // (after the walk: the budget weighs what every node cost this time)
			cache->Tally();
			cache->m_Stats.Ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
			++cache->m_Generation;
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

	bool EvaluateTerrainGraph(const TerrainGraph& graph, const TerrainNodeRegistry& registry, const TgContext& ctx, TgResult& out, std::string& error,
		TerrainGraphCache* cache)
	{
		CompiledTerrainGraph compiled;
		std::vector<TgIssue> issues;
		if (!CompileTerrainGraph(graph, registry, compiled, &issues)) { error = TgIssuesText(issues); return false; }
		return EvaluateTerrainGraph(compiled, registry, ctx, out, error, cache);
	}
}
