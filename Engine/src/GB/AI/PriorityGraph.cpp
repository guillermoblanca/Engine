#include "GB/AI/PriorityGraph.h"
#include "GB/AI/Utility.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

		double Param(const PgNode& n, const char* key, double fallback)
		{
			const auto it = n.Params.find(key);
			return it == n.Params.end() ? fallback : it->second;
		}
		float ParamF(const PgNode& n, const char* key, double fallback) { return static_cast<float>(Param(n, key, fallback)); }

		bool Contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

		std::string Join(const std::vector<std::string>& v, const char* sep)
		{
			std::string out;
			for (const std::string& s : v) { if (!out.empty()) out += sep; out += s; }
			return out;
		}
	}

	// ------------------------------------------------------------------ catalogs
	const std::vector<PgInputInfo>& PriorityInputCatalog()
	{
		static const std::vector<PgInputInfo> catalog = {
			{ "option.score", "option", "the score the option has now (agenda, job, society and the rest already in it)", 0.0f, 3.0f },
			{ "option.distance", "option", "metres from the character to the place of the option", 0.0f, 200.0f },
			{ "option.commit", "option", "seconds the option asks to be kept", 0.0f, 60.0f },
			{ "option.affinity", "option", "affinity (-1..1) with the person the option is about; 0 when it is about no one", -1.0f, 1.0f },
			{ "role.weight", "option", "how much the role of the character likes this kind of option", 0.0f, 3.0f },
			{ "need.hunger", "need", "hunger level: 100 full, 0 starving", 0.0f, 100.0f },
			{ "need.thirst", "need", "thirst level: 100 quenched, 0 parched", 0.0f, 100.0f },
			{ "need.rest", "need", "rest level: 100 rested, 0 exhausted", 0.0f, 100.0f },
			{ "need.warmth", "need", "warmth level: 100 warm, 0 freezing", 0.0f, 100.0f },
			{ "need.safety", "need", "safety level: 100 calm, 0 terrified", 0.0f, 100.0f },
			{ "need.company", "need", "company level: 100 content, 0 lonely", 0.0f, 100.0f },
			{ "need.health", "need", "health level", 0.0f, 100.0f },
			{ "need.vigor", "need", "vigor level", 0.0f, 100.0f },
			{ "danger", "world", "danger close by: (100 - safety) / 100", 0.0f, 1.0f },
			{ "time.hour", "world", "hour of the day, 0..24", 0.0f, 24.0f },
			{ "time.night", "world", "1 at night, else 0", 0.0f, 1.0f },
			{ "season", "world", "0 spring, 1 summer, 2 autumn, 3 winter", 0.0f, 3.0f },
			{ "agent.age", "agent", "age in years", 0.0f, 80.0f },
			{ "agent.skill", "agent", "aptitude in the trade of its role", 0.0f, 100.0f },
			{ "clan.members", "clan", "members alive in the clan", 0.0f, 50.0f },
			{ "fire.burning", "clan", "1 when the campfire burns, else 0", 0.0f, 1.0f },
			{ "factor.<name>", "option", "a factor the option carries (factor.hunger, factor.distance, factor.role...); 0 when it has none", 0.0f, 3.0f },
			{ "skill.<family>", "agent", "aptitude in a family (skill.woodcutting)", 0.0f, 100.0f },
			{ "trait.<name>", "agent", "personality trait, 0..1", 0.0f, 1.0f },
			{ "store.<item>", "clan", "units of an item in the store (store.wood)", 0.0f, 100.0f },
		};
		return catalog;
	}

	bool IsKnownPriorityInput(const std::string& name)
	{
		for (const PgInputInfo& i : PriorityInputCatalog())
			if (i.Name == name) return true;
		for (const char* prefix : { "factor.", "skill.", "trait.", "store." })
			if (StartsWith(name, prefix) && name.size() > std::string(prefix).size()) return true;
		return false;
	}

	const std::vector<std::string>& PriorityKnownKinds()
	{
		static const std::vector<std::string> kinds = { "build", "care", "deliver", "deposit", "drink", "eat", "envoy", "expedition", "experiment", "explore", "fire", "gather",
			"gear", "hunt", "migrate", "recover", "regroup", "rest", "rite", "sleep", "snare", "socialize", "stoke", "talk", "tool", "warm", "make", "farm", "haul", "craft" };
		return kinds;
	}

	bool IsKnownPriorityKind(const std::string& kind) { return kind == "*" || Contains(PriorityKnownKinds(), kind) || (StartsWith(kind, "clan_") && kind.size() > 5); }

	const std::vector<std::string>& PriorityNodeTypes()
	{
		static const std::vector<std::string> types = { "input", "const", "curve", "combine", "clamp", "output", "subgraph", "param", "return" };
		return types;
	}

	const std::vector<std::string>& PriorityFunctions(const std::string& type)
	{
		static const std::vector<std::string> none;
		static const std::vector<std::string> curves = { "linear", "ramp", "sigmoid", "inverse", "threshold", "urgency", "falloff" };
		static const std::vector<std::string> combiners = { "mul", "sum", "min", "max", "weighted" };
		static const std::vector<std::string> modes = { "replace", "multiply", "add" };
		return type == "curve" ? curves : type == "combine" ? combiners : type == "output" ? modes : none;
	}

	// ------------------------------------------------------------------ graph
	const PgNode* PriorityGraph::FindNode(const std::string& id) const
	{
		for (const PgNode& n : Nodes) if (n.Id == id) return &n;
		return nullptr;
	}
	PgNode* PriorityGraph::FindNode(const std::string& id)
	{
		for (PgNode& n : Nodes) if (n.Id == id) return &n;
		return nullptr;
	}
	bool PriorityGraph::operator==(const PriorityGraph& o) const
	{
		if (Id != o.Id || Name != o.Name || Description != o.Description || Author != o.Author || Tags != o.Tags || Template != o.Template || Version != o.Version
			|| Revision != o.Revision || Nodes.size() != o.Nodes.size() || Edges.size() != o.Edges.size()) return false;
		for (size_t i = 0; i < Nodes.size(); ++i)
		{
			const PgNode &a = Nodes[i], &b = o.Nodes[i];
			if (a.Id != b.Id || a.Type != b.Type || a.Label != b.Label || a.X != b.X || a.Y != b.Y || a.Source != b.Source || a.Fn != b.Fn || a.Params != b.Params) return false;
		}
		for (size_t i = 0; i < Edges.size(); ++i)
			if (Edges[i].From != o.Edges[i].From || Edges[i].To != o.Edges[i].To || Edges[i].Port != o.Edges[i].Port || Edges[i].Weight != o.Edges[i].Weight) return false;
		return true;
	}

	// ------------------------------------------------------------------ JSON
	namespace
	{
		double Round2(float v) { return std::round(static_cast<double>(v) * 100.0) / 100.0; }
	}

	std::string PriorityGraphToJson(const PriorityGraph& g)
	{
		Json j;
		j["kind"] = "priority_graph";
		j["version"] = g.Version;
		j["id"] = g.Id;
		j["name"] = g.Name;
		j["description"] = g.Description;
		j["author"] = g.Author;
		j["tags"] = g.Tags;
		j["template"] = g.Template;
		j["revision"] = g.Revision;
		Json nodes = Json::array();
		for (const PgNode& n : g.Nodes)
		{
			Json o;
			o["id"] = n.Id;
			o["type"] = n.Type;
			if (!n.Label.empty()) o["label"] = n.Label;
			if (!n.Source.empty()) o["source"] = n.Source;
			if (!n.Fn.empty()) o["fn"] = n.Fn;
			o["x"] = Round2(n.X);
			o["y"] = Round2(n.Y);
			if (!n.Params.empty())
			{
				Json p = Json::object();
				for (const auto& [k, v] : n.Params) p[k] = v;
				o["params"] = p;
			}
			nodes.push_back(o);
		}
		j["nodes"] = nodes;
		Json edges = Json::array();
		for (const PgEdge& e : g.Edges)
		{
			Json o;
			o["from"] = e.From;
			o["to"] = e.To;
			if (e.Port != "in") o["port"] = e.Port;
			o["weight"] = e.Weight;
			edges.push_back(o);
		}
		j["edges"] = edges;
		return j.dump(2) + "\n";
	}

	std::string PriorityGraphFromJson(const std::string& json, PriorityGraph& out)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		if (const auto k = j.find("kind"); k != j.end() && (!k->is_string() || k->get<std::string>() != "priority_graph"))
			return "not a priority graph (kind is '" + (k->is_string() ? k->get<std::string>() : std::string("?")) + "')";
		PriorityGraph g;
		if (const auto v = j.find("version"); v != j.end())
		{
			if (!v->is_number_integer()) return "version must be an integer";
			g.Version = v->get<int>();
			if (g.Version > kPriorityGraphVersion)
				return "the graph has version " + std::to_string(g.Version) + " and this engine reads up to " + std::to_string(kPriorityGraphVersion) + ": update the engine";
			if (g.Version < 1) return "version must be 1 or more";
		}
		g.Version = kPriorityGraphVersion;   // (older versions would be migrated here; there is only one so far)
		auto text = [&](const char* key, std::string& dst) -> std::string
		{
			const auto it = j.find(key);
			if (it == j.end()) return {};
			if (!it->is_string()) return std::string(key) + " must be text";
			dst = it->get<std::string>();
			return {};
		};
		for (const char* key : { "id", "name", "description", "author" })
		{
			std::string* dst = std::string(key) == "id" ? &g.Id : std::string(key) == "name" ? &g.Name : std::string(key) == "description" ? &g.Description : &g.Author;
			if (std::string e = text(key, *dst); !e.empty()) return e;
		}
		if (g.Id.empty()) return "the graph has no id";
		if (const auto t = j.find("tags"); t != j.end())
		{
			if (!t->is_array()) return "tags must be a list of text";
			for (const Json& s : *t) { if (!s.is_string()) return "tags must be a list of text"; g.Tags.push_back(s.get<std::string>()); }
		}
		if (const auto t = j.find("template"); t != j.end() && t->is_boolean()) g.Template = t->get<bool>();
		if (const auto r = j.find("revision"); r != j.end() && r->is_number_integer()) g.Revision = std::max(1, r->get<int>());
		const auto nodes = j.find("nodes");
		if (nodes == j.end() || !nodes->is_array()) return "nodes must be a list";
		for (const Json& o : *nodes)
		{
			if (!o.is_object()) return "every node must be an object";
			PgNode n;
			const auto id = o.find("id"), type = o.find("type");
			if (id == o.end() || !id->is_string()) return "a node has no id";
			n.Id = id->get<std::string>();
			if (type == o.end() || !type->is_string()) return "node '" + n.Id + "' has no type";
			n.Type = type->get<std::string>();
			if (const auto s = o.find("source"); s != o.end() && s->is_string()) n.Source = s->get<std::string>();
			if (const auto s = o.find("fn"); s != o.end() && s->is_string()) n.Fn = s->get<std::string>();
			if (const auto s = o.find("label"); s != o.end() && s->is_string()) n.Label = s->get<std::string>();
			if (const auto s = o.find("x"); s != o.end() && s->is_number()) n.X = s->get<float>();
			if (const auto s = o.find("y"); s != o.end() && s->is_number()) n.Y = s->get<float>();
			if (const auto p = o.find("params"); p != o.end())
			{
				if (!p->is_object()) return "node '" + n.Id + "': params must be an object of name -> number";
				for (const auto& [key, v] : p->items())
				{
					if (!v.is_number()) return "node '" + n.Id + "': param '" + key + "' must be a number";
					n.Params[key] = v.get<double>();
				}
			}
			g.Nodes.push_back(std::move(n));
		}
		if (const auto edges = j.find("edges"); edges != j.end())
		{
			if (!edges->is_array()) return "edges must be a list";
			for (const Json& o : *edges)
			{
				if (!o.is_object()) return "every edge must be an object";
				PgEdge e;
				const auto from = o.find("from"), to = o.find("to");
				if (from == o.end() || to == o.end() || !from->is_string() || !to->is_string()) return "an edge needs 'from' and 'to'";
				e.From = from->get<std::string>();
				e.To = to->get<std::string>();
				if (const auto s = o.find("port"); s != o.end() && s->is_string()) e.Port = s->get<std::string>();
				if (const auto s = o.find("weight"); s != o.end())
				{
					if (!s->is_number()) return "edge " + e.From + " -> " + e.To + ": weight must be a number";
					e.Weight = s->get<double>();
				}
				g.Edges.push_back(std::move(e));
			}
		}
		out = std::move(g);
		return {};
	}

	// ------------------------------------------------------------------ validation
	bool HasPgErrors(const std::vector<PgIssue>& issues)
	{
		return std::any_of(issues.begin(), issues.end(), [](const PgIssue& i) { return i.Error; });
	}

	std::string PgIssuesText(const std::vector<PgIssue>& issues)
	{
		std::string out;
		for (const PgIssue& i : issues)
		{
			out += i.Error ? "error: " : "warning: ";
			if (!i.Node.empty()) out += "node '" + i.Node + "': ";
			out += i.Message + "\n";
		}
		return out;
	}

	namespace
	{
		bool Terminal(const std::string& type) { return type == "output" || type == "return"; }

		void ValidateInto(const PriorityGraph& g, const PgResolver& resolve, std::vector<std::string>& stack, std::vector<PgIssue>& issues, const std::string& where)
		{
			auto err = [&](const std::string& node, const std::string& msg) { issues.push_back({ true, node, where + msg }); };
			auto warn = [&](const std::string& node, const std::string& msg) { issues.push_back({ false, node, where + msg }); };
			const bool fragment = std::any_of(g.Nodes.begin(), g.Nodes.end(), [](const PgNode& n) { return n.Type == "return"; });

			std::map<std::string, size_t> index;
			for (size_t i = 0; i < g.Nodes.size(); ++i)
			{
				const PgNode& n = g.Nodes[i];
				if (n.Id.empty()) { err("", "a node has no id"); continue; }
				if (!index.emplace(n.Id, i).second) err(n.Id, "the id is used by more than one node");
				if (n.Id.find('/') != std::string::npos) err(n.Id, "the id cannot contain '/'");
				if (!Contains(PriorityNodeTypes(), n.Type)) { err(n.Id, "unknown type '" + n.Type + "' (" + Join(PriorityNodeTypes(), ", ") + ")"); continue; }
				if (n.Type == "input" && !IsKnownPriorityInput(n.Source))
					err(n.Id, n.Source.empty() ? "the input has no source" : "the input '" + n.Source + "' does not exist (see the list of inputs)");
				if (n.Type == "output")
				{
					if (!IsKnownPriorityKind(n.Source))
						err(n.Id, "the output '" + n.Source + "' is not a kind of option the tribe produces (" + Join(PriorityKnownKinds(), ", ") + ", * for any)");
					if (!n.Fn.empty() && !Contains(PriorityFunctions("output"), n.Fn)) err(n.Id, "unknown output mode '" + n.Fn + "' (replace, multiply or add)");
					if (fragment) err(n.Id, "a graph that is included in another cannot have outputs");
				}
				if (n.Type == "curve" && !Contains(PriorityFunctions("curve"), n.Fn)) err(n.Id, "unknown curve '" + n.Fn + "' (" + Join(PriorityFunctions("curve"), ", ") + ")");
				if (n.Type == "combine" && !Contains(PriorityFunctions("combine"), n.Fn)) err(n.Id, "unknown combiner '" + n.Fn + "' (" + Join(PriorityFunctions("combine"), ", ") + ")");
				if ((n.Type == "param" || n.Type == "return") && !fragment && n.Type == "param")
					warn(n.Id, "a parameter outside an included graph always reads its default");
			}
			{   // one output per kind
				std::map<std::string, std::string> seen;
				for (const PgNode& n : g.Nodes)
					if (n.Type == "output" && !seen.emplace(n.Source, n.Id).second)
						err(n.Id, "the output '" + n.Source + "' already exists in node '" + seen[n.Source] + "'");
			}
			if (fragment)
			{
				int returns = 0;
				for (const PgNode& n : g.Nodes) if (n.Type == "return") ++returns;
				if (returns > 1) err("", "an included graph needs exactly one 'return' node");
			}

			std::vector<int> consumers(g.Nodes.size(), 0), feeds(g.Nodes.size(), 0);
			for (const PgEdge& e : g.Edges)
			{
				const auto from = index.find(e.From), to = index.find(e.To);
				if (from == index.end()) { err("", "the connection " + e.From + " -> " + e.To + " starts at a node that does not exist ('" + e.From + "')"); continue; }
				if (to == index.end()) { err("", "the connection " + e.From + " -> " + e.To + " ends at a node that does not exist ('" + e.To + "')"); continue; }
				++consumers[from->second];
				++feeds[to->second];
				const PgNode& a = g.Nodes[from->second];
				const PgNode& b = g.Nodes[to->second];
				if (Terminal(a.Type) && a.Type == "output") err(a.Id, "an output has nothing after it (connection to '" + b.Id + "')");
				if (a.Type == "return") err(a.Id, "'return' has nothing after it (connection to '" + b.Id + "')");
				if (b.Type == "input" || b.Type == "const") err(b.Id, "an '" + b.Type + "' node takes no connections (from '" + a.Id + "')");
				if (!std::isfinite(e.Weight)) err("", "the connection " + e.From + " -> " + e.To + " has a weight that is not a number");
				if (b.Type == "subgraph" && resolve)
				{
					const PriorityGraph* child = resolve(b.Source);
					const PgNode* param = child ? child->FindNode(e.Port) : nullptr;
					if (child && (!param || param->Type != "param"))
						err(b.Id, "the connection from '" + a.Id + "' goes to port '" + e.Port + "', which graph '" + b.Source + "' does not declare as a parameter");
				}
			}
			for (size_t i = 0; i < g.Nodes.size(); ++i)
			{
				const PgNode& n = g.Nodes[i];
				if (!Terminal(n.Type) && consumers[i] == 0) warn(n.Id, "nothing uses this node");
				if (n.Type == "output" && feeds[i] == 0) warn(n.Id, "the output has no connections: it will be 0");
			}

			// cycles: a depth-first walk that names the way round
			{
				std::vector<std::vector<size_t>> out(g.Nodes.size());
				for (const PgEdge& e : g.Edges)
				{
					const auto from = index.find(e.From), to = index.find(e.To);
					if (from != index.end() && to != index.end()) out[from->second].push_back(to->second);
				}
				std::vector<int> color(g.Nodes.size(), 0);
				std::vector<size_t> path;
				bool found = false;
				std::function<void(size_t)> visit = [&](size_t v)
				{
					if (found) return;
					color[v] = 1;
					path.push_back(v);
					for (size_t w : out[v])
					{
						if (found) break;
						if (color[w] == 1)
						{
							std::string text;
							for (size_t k = static_cast<size_t>(std::find(path.begin(), path.end(), w) - path.begin()); k < path.size(); ++k) text += g.Nodes[path[k]].Id + " -> ";
							err(g.Nodes[w].Id, "cycle: " + text + g.Nodes[w].Id);
							found = true;
						}
						else if (color[w] == 0) visit(w);
					}
					path.pop_back();
					color[v] = 2;
				};
				for (size_t i = 0; i < g.Nodes.size() && !found; ++i) if (color[i] == 0) visit(i);
			}

			// included graphs
			for (const PgNode& n : g.Nodes)
			{
				if (n.Type != "subgraph") continue;
				if (n.Source.empty()) { err(n.Id, "the subgraph names no graph"); continue; }
				if (!resolve) continue;
				const PriorityGraph* child = resolve(n.Source);
				if (!child) { err(n.Id, "the subgraph '" + n.Source + "' does not exist in the library"); continue; }
				if (std::find(stack.begin(), stack.end(), n.Source) != stack.end())
				{
					std::string chain;
					for (const std::string& s : stack) chain += s + " -> ";
					err(n.Id, "recursive include: " + chain + n.Source);
					continue;
				}
				if (!std::any_of(child->Nodes.begin(), child->Nodes.end(), [](const PgNode& c) { return c.Type == "return"; }))
				{
					err(n.Id, "the graph '" + n.Source + "' cannot be included: it has no 'return' node");
					continue;
				}
				stack.push_back(n.Source);
				std::vector<PgIssue> inner;
				ValidateInto(*child, resolve, stack, inner, where + "[" + n.Source + "] ");
				stack.pop_back();
				for (PgIssue& i : inner) if (i.Error) issues.push_back(std::move(i));
			}
		}
	}

	std::vector<PgIssue> ValidatePriorityGraph(const PriorityGraph& graph, const PgResolver& resolve)
	{
		std::vector<PgIssue> issues;
		std::vector<std::string> stack = { graph.Id };
		ValidateInto(graph, resolve, stack, issues, "");
		if (!std::any_of(graph.Nodes.begin(), graph.Nodes.end(), [](const PgNode& n) { return n.Type == "output" || n.Type == "return"; }))
			issues.push_back({ false, "", "the graph has no outputs: it changes no option" });
		return issues;
	}

	// ------------------------------------------------------------------ compile
	namespace
	{
		void Expand(const PriorityGraph& g, const PgResolver& resolve, const std::string& prefix, std::vector<PgNode>& nodes, std::vector<PgEdge>& edges)
		{
			for (const PgNode& n : g.Nodes)
			{
				PgNode c = n;
				c.Id = prefix + n.Id;
				if (n.Type == "subgraph") { c.Type = "combine"; c.Fn = "sum"; c.Source.clear(); c.Params.clear(); }
				nodes.push_back(std::move(c));
			}
			std::vector<PgEdge> inner;
			for (const PgEdge& e : g.Edges)
			{
				PgEdge c = e;
				c.From = prefix + e.From;
				c.To = prefix + e.To;
				c.Port = "in";
				const PgNode* target = g.FindNode(e.To);
				if (target && target->Type == "subgraph") c.To = prefix + e.To + "/" + e.Port;
				edges.push_back(std::move(c));
			}
			for (const PgNode& n : g.Nodes)
			{
				if (n.Type != "subgraph") continue;
				const PriorityGraph* child = resolve(n.Source);
				if (!child) continue;
				const std::string childPrefix = prefix + n.Id + "/";
				Expand(*child, resolve, childPrefix, nodes, edges);
				for (const PgNode& c : child->Nodes)
					if (c.Type == "return") edges.push_back({ childPrefix + c.Id, prefix + n.Id, "in", 1.0 });
			}
		}
	}

	bool CompilePriorityGraph(const PriorityGraph& graph, const PgResolver& resolve, CompiledPriorityGraph& out, std::vector<PgIssue>* issues)
	{
		out = CompiledPriorityGraph();
		std::vector<PgIssue> found = ValidatePriorityGraph(graph, resolve);
		if (issues) *issues = found;
		if (HasPgErrors(found)) return false;
		CompiledPriorityGraph c;
		c.Id = graph.Id;
		std::vector<PgEdge> edges;
		const PgResolver nothing = [](const std::string&) -> const PriorityGraph* { return nullptr; };
		Expand(graph, resolve ? resolve : nothing, "", c.Nodes, edges);
		std::map<std::string, int> index;
		for (size_t i = 0; i < c.Nodes.size(); ++i) index[c.Nodes[i].Id] = static_cast<int>(i);
		c.In.assign(c.Nodes.size(), {});
		std::vector<std::vector<int>> next(c.Nodes.size());
		std::vector<int> degree(c.Nodes.size(), 0);
		for (const PgEdge& e : edges)
		{
			const auto from = index.find(e.From), to = index.find(e.To);
			if (from == index.end() || to == index.end()) continue;
			c.In[static_cast<size_t>(to->second)].push_back({ from->second, static_cast<float>(e.Weight) });
			next[static_cast<size_t>(from->second)].push_back(to->second);
			++degree[static_cast<size_t>(to->second)];
		}
		std::vector<int> queue;
		for (size_t i = 0; i < c.Nodes.size(); ++i) if (degree[i] == 0) queue.push_back(static_cast<int>(i));
		for (size_t head = 0; head < queue.size(); ++head)
		{
			c.Order.push_back(queue[head]);
			for (int w : next[static_cast<size_t>(queue[head])]) if (--degree[static_cast<size_t>(w)] == 0) queue.push_back(w);
		}
		if (c.Order.size() != c.Nodes.size()) return false;   // (a cycle through an included graph that validation could not see)
		for (size_t i = 0; i < c.Nodes.size(); ++i) if (c.Nodes[i].Type == "output") c.Outputs.push_back(static_cast<int>(i));
		if (c.Nodes.size() == 2 && c.Outputs.size() == 1)
		{
			const int o = c.Outputs[0];
			const PgNode& on = c.Nodes[static_cast<size_t>(o)];
			const auto& in = c.In[static_cast<size_t>(o)];
			if (on.Source == "*" && (on.Fn.empty() || on.Fn == "replace") && in.size() == 1 && in[0].second == 1.0f)
			{
				const PgNode& s = c.Nodes[static_cast<size_t>(in[0].first)];
				c.Identity = s.Type == "input" && s.Source == "option.score" && Param(s, "scale", 1.0) == 1.0 && Param(s, "offset", 0.0) == 0.0;
			}
		}
		out = std::move(c);
		return true;
	}

	// ------------------------------------------------------------------ evaluate
	bool PgInputs::Get(const std::string& name, float& out) const
	{
		if (const auto it = Values.find(name); it != Values.end()) { out = it->second; return true; }
		return Resolver && Resolver(name, out);
	}

	const PgOutput* PgResult::For(const std::string& kind) const
	{
		const PgOutput* any = nullptr;
		for (const PgOutput& o : Outputs)
		{
			if (o.Kind == kind) return &o;
			if (o.Kind == "*") any = &o;
		}
		return any;
	}

	float ApplyPgMode(const std::string& mode, float score, float value)
	{
		if (mode == "multiply") return score * value;
		if (mode == "add") return score + value;
		return value;
	}

	PgResult EvaluatePriorityGraph(const CompiledPriorityGraph& g, const PgInputs& inputs)
	{
		PgResult r;
		r.Values.assign(g.Nodes.size(), 0.0f);
		for (int idx : g.Order)
		{
			const size_t i = static_cast<size_t>(idx);
			const PgNode& n = g.Nodes[i];
			const auto& in = g.In[i];
			float v = 0.0f;
			auto sum = [&]() { float s = 0.0f; for (const auto& [src, w] : in) s += r.Values[static_cast<size_t>(src)] * w; return s; };
			if (n.Type == "input")
			{
				float x = 0.0f;
				v = inputs.Get(n.Source, x) ? x * ParamF(n, "scale", 1.0) + ParamF(n, "offset", 0.0) : ParamF(n, "default", 0.0);
			}
			else if (n.Type == "const") v = ParamF(n, "value", 0.0);
			else if (n.Type == "curve")
			{
				const float x = in.empty() ? ParamF(n, "default", 0.0) : sum();
				if (n.Fn == "linear") v = ParamF(n, "a", 1.0) * x + ParamF(n, "b", 0.0);
				else if (n.Fn == "ramp") v = Ramp(x, ParamF(n, "lo", 0.0), ParamF(n, "hi", 1.0));
				else if (n.Fn == "sigmoid") v = 1.0f / (1.0f + std::exp(-ParamF(n, "k", 10.0) * (x - ParamF(n, "mid", 0.5))));
				else if (n.Fn == "inverse") v = 1.0f - Ramp(x, ParamF(n, "lo", 0.0), ParamF(n, "hi", 1.0));
				else if (n.Fn == "threshold") v = x >= ParamF(n, "at", 0.5) ? ParamF(n, "high", 1.0) : ParamF(n, "low", 0.0);
				else if (n.Fn == "urgency") v = Urgency(x, ParamF(n, "from", 60.0), ParamF(n, "power", 1.0));
				else if (n.Fn == "falloff") v = Falloff(x, ParamF(n, "half", 30.0));
			}
			else if (n.Type == "combine")
			{
				if (in.empty()) v = ParamF(n, "default", n.Fn == "mul" ? 1.0 : 0.0);
				else if (n.Fn == "mul") { v = 1.0f; for (const auto& [src, w] : in) v *= r.Values[static_cast<size_t>(src)] * w; }
				else if (n.Fn == "min" || n.Fn == "max")
				{
					bool first = true;
					for (const auto& [src, w] : in)
					{
						const float c = r.Values[static_cast<size_t>(src)] * w;
						v = first ? c : n.Fn == "min" ? std::min(v, c) : std::max(v, c);
						first = false;
					}
				}
				else if (n.Fn == "weighted")
				{
					float total = 0.0f;
					for (const auto& [src, w] : in) total += w;
					v = std::fabs(total) < 1e-9f ? ParamF(n, "default", 0.0) : sum() / total;
				}
				else v = sum();
			}
			else if (n.Type == "clamp") v = std::clamp(sum(), ParamF(n, "lo", 0.0), ParamF(n, "hi", 1.0));
			else if (n.Type == "output") v = in.empty() ? ParamF(n, "default", 0.0) : sum();
			else v = in.empty() ? ParamF(n, "default", 0.0) : sum();   // param, return (and subgraph nodes, which are sums after expansion)
			if (!std::isfinite(v)) v = 0.0f;
			r.Values[i] = v;
		}
		for (int o : g.Outputs)
		{
			const PgNode& n = g.Nodes[static_cast<size_t>(o)];
			r.Outputs.push_back({ n.Source, n.Fn.empty() ? "replace" : n.Fn, n.Id, r.Values[static_cast<size_t>(o)] });
		}
		return r;
	}

	PgResult EvaluatePriorityGraph(const PriorityGraph& graph, const PgInputs& inputs, const PgResolver& resolve)
	{
		CompiledPriorityGraph c;
		if (!CompilePriorityGraph(graph, resolve, c)) return {};
		return EvaluatePriorityGraph(c, inputs);
	}

	// ------------------------------------------------------------------ the graphs that ship
	namespace
	{
		struct Builder
		{
			PriorityGraph G;
			void Node(const char* id, const char* type, const char* source, const char* fn, float x, float y, std::map<std::string, double> params = {}, const char* label = "")
			{
				PgNode n;
				n.Id = id; n.Type = type; n.Source = source; n.Fn = fn; n.X = x; n.Y = y; n.Params = std::move(params); n.Label = label;
				G.Nodes.push_back(std::move(n));
			}
			void Edge(const char* from, const char* to, double weight = 1.0, const char* port = "in") { G.Edges.push_back({ from, to, port, weight }); }
		};
	}

	PriorityGraph MakeBuiltinPriorityGraph()
	{
		Builder b;
		b.G.Id = "builtin";
		b.G.Name = "Builtin";
		b.G.Description = "Hands every score on unchanged: the behaviour the tribe has without graphs (the default).";
		b.G.Author = "GB";
		b.G.Tags = { "builtin" };
		b.Node("score", "input", "option.score", "", 0.0f, 0.0f);
		b.Node("any", "output", "*", "replace", 260.0f, 0.0f);
		b.Edge("score", "any");
		return b.G;
	}

	PriorityGraph MakeFormulasPriorityGraph()
	{
		Builder b;
		b.G.Id = "builtin_formulas";
		b.G.Name = "Builtin formulas";
		b.G.Description = "Template: eat, drink, regroup and gather rebuilt from the factors the options record (weight x need x distance...). Starting point to change a formula; it leaves out boosts, agenda, job and society.";
		b.G.Author = "GB";
		b.G.Tags = { "builtin", "template" };
		b.G.Template = true;
		b.Node("f_weight", "input", "factor.weight", "", 0, 0);
		b.Node("f_hunger", "input", "factor.hunger", "", 0, 70);
		b.Node("f_thirst", "input", "factor.thirst", "", 0, 140);
		b.Node("f_distance", "input", "factor.distance", "", 0, 210);
		b.Node("f_fear", "input", "factor.fear", "", 0, 280);
		b.Node("f_role", "input", "factor.role", "", 0, 350);
		b.Node("f_need", "input", "factor.need", "", 0, 420);
		b.Node("f_memory", "input", "factor.memory", "", 0, 490);
		b.Node("c_regroup", "const", "", "", 0, 560, { { "value", 1.5 } });
		b.Node("c_base", "const", "", "", 0, 630, { { "value", 0.4 } });
		b.Node("any_score", "input", "option.score", "", 0, 700);
		b.Node("eat_mul", "combine", "", "mul", 260, 20);
		b.Node("drink_mul", "combine", "", "mul", 260, 120);
		b.Node("regroup_mul", "combine", "", "mul", 260, 280);
		b.Node("gather_sum", "combine", "", "sum", 260, 400, {}, "0.4 + 0.7 need");
		b.Node("gather_mul", "combine", "", "mul", 480, 440);
		b.Node("eat", "output", "eat", "replace", 700, 20);
		b.Node("drink", "output", "drink", "replace", 700, 120);
		b.Node("regroup", "output", "regroup", "replace", 700, 280);
		b.Node("gather", "output", "gather", "replace", 700, 440);
		b.Node("any", "output", "*", "replace", 700, 700);
		b.Edge("f_weight", "eat_mul"); b.Edge("f_hunger", "eat_mul"); b.Edge("f_distance", "eat_mul"); b.Edge("eat_mul", "eat");
		b.Edge("f_weight", "drink_mul"); b.Edge("f_thirst", "drink_mul"); b.Edge("f_distance", "drink_mul"); b.Edge("drink_mul", "drink");
		b.Edge("c_regroup", "regroup_mul"); b.Edge("f_fear", "regroup_mul"); b.Edge("regroup_mul", "regroup");
		b.Edge("c_base", "gather_sum"); b.Edge("f_need", "gather_sum", 0.7);
		b.Edge("f_role", "gather_mul"); b.Edge("gather_sum", "gather_mul"); b.Edge("f_distance", "gather_mul"); b.Edge("f_memory", "gather_mul"); b.Edge("gather_mul", "gather");
		b.Edge("any_score", "any");
		return b.G;
	}

	PriorityGraph MakeRawPriorityGraph()
	{
		Builder b;
		b.G.Id = "builtin_raw";
		b.G.Name = "Builtin from raw needs";
		b.G.Description = "Template: eat, drink, sleep, warm and regroup computed from the needs and the distance with curves (urgency, falloff, ramp). Same numbers as the tribe's formulas, without agenda, job or society.";
		b.G.Author = "GB";
		b.G.Tags = { "builtin", "template" };
		b.G.Template = true;
		b.Node("hunger", "input", "need.hunger", "", 0, 0);
		b.Node("thirst", "input", "need.thirst", "", 0, 80);
		b.Node("rest", "input", "need.rest", "", 0, 160);
		b.Node("warmth", "input", "need.warmth", "", 0, 240);
		b.Node("safety", "input", "need.safety", "", 0, 320);
		b.Node("distance", "input", "option.distance", "", 0, 400);
		b.Node("night", "input", "time.night", "", 0, 480);
		b.Node("any_score", "input", "option.score", "", 0, 560);
		b.Node("u_hunger", "curve", "", "urgency", 200, 0, { { "from", 60 } });
		b.Node("u_thirst", "curve", "", "urgency", 200, 80, { { "from", 60 } });
		b.Node("u_rest", "curve", "", "urgency", 200, 160, { { "from", 70 }, { "power", 1.2 } });
		b.Node("u_warmth", "curve", "", "urgency", 200, 240, { { "from", 50 } });
		b.Node("u_safety", "curve", "", "urgency", 200, 320, { { "from", 60 } });
		b.Node("f_60", "curve", "", "falloff", 200, 400, { { "half", 60 } });
		b.Node("f_80", "curve", "", "falloff", 200, 440, { { "half", 80 } });
		b.Node("f_120", "curve", "", "falloff", 200, 480, { { "half", 120 } });
		b.Node("tired_inv", "curve", "", "linear", 200, 520, { { "a", -1 }, { "b", 95 } }, "95 - rest");
		b.Node("dark_ramp", "curve", "", "ramp", 380, 520, { { "lo", 0 }, { "hi", 40 } });
		b.Node("c_eat", "const", "", "", 380, 0, { { "value", 2.2 } });
		b.Node("c_drink", "const", "", "", 380, 80, { { "value", 2.0 } });
		b.Node("c_warm", "const", "", "", 380, 240, { { "value", 1.5 } });
		b.Node("c_regroup", "const", "", "", 380, 320, { { "value", 1.5 } });
		b.Node("dark", "combine", "", "mul", 560, 520, {}, "night x ramp");
		b.Node("tired_dark", "combine", "", "sum", 560, 160, {}, "tired + dark");
		b.Node("eat_mul", "combine", "", "mul", 560, 0);
		b.Node("drink_mul", "combine", "", "mul", 560, 80);
		b.Node("sleep_mul", "combine", "", "mul", 740, 160);
		b.Node("warm_mul", "combine", "", "mul", 560, 240);
		b.Node("regroup_mul", "combine", "", "mul", 560, 320);
		b.Node("eat", "output", "eat", "replace", 920, 0);
		b.Node("drink", "output", "drink", "replace", 920, 80);
		b.Node("sleep", "output", "sleep", "replace", 920, 160);
		b.Node("warm", "output", "warm", "replace", 920, 240);
		b.Node("regroup", "output", "regroup", "replace", 920, 320);
		b.Node("any", "output", "*", "replace", 920, 560);
		b.Edge("hunger", "u_hunger"); b.Edge("thirst", "u_thirst"); b.Edge("rest", "u_rest"); b.Edge("warmth", "u_warmth"); b.Edge("safety", "u_safety");
		b.Edge("distance", "f_60"); b.Edge("distance", "f_80"); b.Edge("distance", "f_120"); b.Edge("rest", "tired_inv"); b.Edge("tired_inv", "dark_ramp");
		b.Edge("night", "dark"); b.Edge("dark_ramp", "dark", 1.3);
		b.Edge("u_rest", "tired_dark", 1.7); b.Edge("dark", "tired_dark");
		b.Edge("c_eat", "eat_mul"); b.Edge("u_hunger", "eat_mul"); b.Edge("f_60", "eat_mul"); b.Edge("eat_mul", "eat");
		b.Edge("c_drink", "drink_mul"); b.Edge("u_thirst", "drink_mul"); b.Edge("f_80", "drink_mul"); b.Edge("drink_mul", "drink");
		b.Edge("tired_dark", "sleep_mul"); b.Edge("f_120", "sleep_mul"); b.Edge("sleep_mul", "sleep");
		b.Edge("c_warm", "warm_mul"); b.Edge("u_warmth", "warm_mul"); b.Edge("f_60", "warm_mul"); b.Edge("warm_mul", "warm");
		b.Edge("c_regroup", "regroup_mul"); b.Edge("u_safety", "regroup_mul"); b.Edge("regroup_mul", "regroup");
		b.Edge("any_score", "any");
		return b.G;
	}

	PriorityGraph MakeNeedUrgencyFragment()
	{
		Builder b;
		b.G.Id = "fragment_need_urgency";
		b.G.Name = "Need urgency (fragment)";
		b.G.Description = "Included graph: how pressing a need is (0 satisfied .. 1 empty) from the parameter 'level' (0..100).";
		b.G.Author = "GB";
		b.G.Tags = { "fragment" };
		b.Node("level", "param", "", "", 0, 0, { { "default", 100 } });
		b.Node("urgent", "curve", "", "urgency", 200, 0, { { "from", 60 } });
		b.Node("result", "return", "", "", 400, 0);
		b.Edge("level", "urgent"); b.Edge("urgent", "result");
		return b.G;
	}

	PriorityGraph MakeCautiousPriorityGraph()
	{
		Builder b;
		b.G.Id = "cautious";
		b.G.Name = "Cautious";
		b.G.Description = "Example: explores less where there is danger and regroups sooner when it feels unsafe (uses the fragment fragment_need_urgency).";
		b.G.Author = "GB";
		b.G.Tags = { "example" };
		b.Node("danger", "input", "danger", "", 0, 0);
		b.Node("safety", "input", "need.safety", "", 0, 120);
		b.Node("one", "const", "", "", 0, 240, { { "value", 1 } });
		b.Node("fear", "subgraph", "fragment_need_urgency", "", 200, 120);
		b.Node("calm", "combine", "", "sum", 400, 0, {}, "1 - 0.7 danger");
		b.Node("jumpy", "combine", "", "sum", 400, 120, {}, "1 + 0.5 fear");
		b.Node("explore", "output", "explore", "multiply", 600, 0);
		b.Node("regroup", "output", "regroup", "multiply", 600, 120);
		b.Edge("safety", "fear", 1.0, "level");
		b.Edge("one", "calm"); b.Edge("danger", "calm", -0.7); b.Edge("calm", "explore");
		b.Edge("one", "jumpy"); b.Edge("fear", "jumpy", 0.5); b.Edge("jumpy", "regroup");
		return b.G;
	}
}
