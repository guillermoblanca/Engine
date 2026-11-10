#include "GB/World/Graph/TerrainGraphIO.h"
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	std::string TerrainGraphToJson(const TerrainGraph& g)
	{
		Json j;
		j["kind"] = "terrain_graph";
		j["version"] = g.Version;
		j["id"] = g.Id;
		j["name"] = g.Name;
		Json nodes = Json::array();
		for (const TgNode& n : g.Nodes)
		{
			Json o;
			o["id"] = n.Id;
			o["type"] = n.Type;
			if (!n.Label.empty()) o["label"] = n.Label;
			if (!n.Source.empty()) o["source"] = n.Source;
			o["x"] = n.X;   // (a float widened to a double: read back exactly)
			o["y"] = n.Y;
			if (!n.Params.empty())
			{
				Json p = Json::object();
				for (const auto& [k, v] : n.Params) p[k] = v;   // nlohmann writes the shortest text that reads back as the same double
				o["params"] = p;
			}
			nodes.push_back(o);
		}
		j["nodes"] = nodes;
		Json edges = Json::array();
		for (const TgEdge& e : g.Edges)
		{
			Json o;
			o["from"] = e.From;
			o["fromPort"] = e.FromPort;
			o["to"] = e.To;
			o["port"] = e.Port;
			edges.push_back(o);
		}
		j["edges"] = edges;
		return j.dump(2) + "\n";
	}

	std::string TerrainGraphFromJson(const std::string& json, TerrainGraph& out)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		if (const auto k = j.find("kind"); k != j.end() && (!k->is_string() || k->get<std::string>() != "terrain_graph"))
			return "not a terrain graph (kind is '" + (k->is_string() ? k->get<std::string>() : std::string("?")) + "')";
		TerrainGraph g;
		if (const auto v = j.find("version"); v != j.end())
		{
			if (!v->is_number_integer()) return "version must be an integer";
			g.Version = v->get<int>();
			if (g.Version > kTerrainGraphVersion)
				return "the graph has version " + std::to_string(g.Version) + " and this engine reads up to " + std::to_string(kTerrainGraphVersion) + ": update the engine";
			if (g.Version < 1) return "version must be 1 or more";
		}
		auto text = [](const Json& o, const char* key, std::string& dst) -> std::string
		{
			const auto it = o.find(key);
			if (it == o.end()) return {};
			if (!it->is_string()) return std::string(key) + " must be text";
			dst = it->get<std::string>();
			return {};
		};
		if (std::string e = text(j, "id", g.Id); !e.empty()) return e;
		if (std::string e = text(j, "name", g.Name); !e.empty()) return e;
		if (g.Id.empty()) return "the graph has no id";
		const auto nodes = j.find("nodes");
		if (nodes == j.end() || !nodes->is_array()) return "nodes must be a list";
		for (const Json& o : *nodes)
		{
			if (!o.is_object()) return "every node must be an object";
			TgNode n;
			const auto id = o.find("id"), type = o.find("type");
			if (id == o.end() || !id->is_string()) return "a node has no id";
			n.Id = id->get<std::string>();
			if (type == o.end() || !type->is_string()) return "node '" + n.Id + "' has no type";
			n.Type = type->get<std::string>();
			if (std::string e = text(o, "label", n.Label); !e.empty()) return "node '" + n.Id + "': " + e;
			if (std::string e = text(o, "source", n.Source); !e.empty()) return "node '" + n.Id + "': " + e;
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
		const auto edges = j.find("edges");
		if (edges != j.end())
		{
			if (!edges->is_array()) return "edges must be a list";
			for (const Json& o : *edges)
			{
				if (!o.is_object()) return "every edge must be an object";
				TgEdge e;
				if (std::string err = text(o, "from", e.From); !err.empty()) return "an edge: " + err;
				if (std::string err = text(o, "to", e.To); !err.empty()) return "an edge: " + err;
				if (e.From.empty() || e.To.empty()) return "every edge needs from and to";
				if (std::string err = text(o, "port", e.Port); !err.empty()) return "edge " + e.From + " -> " + e.To + ": " + err;
				if (std::string err = text(o, "fromPort", e.FromPort); !err.empty()) return "edge " + e.From + " -> " + e.To + ": " + err;
				g.Edges.push_back(std::move(e));
			}
		}
		out = std::move(g);
		return {};
	}
}
