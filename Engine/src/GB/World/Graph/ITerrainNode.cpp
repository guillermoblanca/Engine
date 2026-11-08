#include "GB/World/Graph/ITerrainNode.h"
#include "GB/World/Graph/Nodes/BuiltinNodes.h"
#include "GB/World/Graph/TerrainGraph.h"

namespace GB
{
	const char* TgPortKindName(TgPortKind kind)
	{
		switch (kind)
		{
		case TgPortKind::Field: return "field";
		case TgPortKind::Placements: return "placements";
		case TgPortKind::Paths: return "paths";
		case TgPortKind::Water: return "water";
		default: return "any";
		}
	}

	int TgNodeSchema::InputIndex(const std::string& name) const
	{
		for (size_t i = 0; i < Inputs.size(); ++i) if (Inputs[i].Name == name) return static_cast<int>(i);
		return -1;
	}

	int TgNodeSchema::OutputIndex(const std::string& name) const
	{
		for (size_t i = 0; i < Outputs.size(); ++i) if (Outputs[i].Name == name) return static_cast<int>(i);
		return -1;
	}

	const TgParamInfo* TgNodeSchema::FindParam(const std::string& name) const
	{
		for (const TgParamInfo& p : Params) if (p.Name == name) return &p;
		return nullptr;
	}

	double TgParam(const TgNode& node, const char* key, double fallback)
	{
		const auto it = node.Params.find(key);
		return it == node.Params.end() ? fallback : it->second;
	}

	TgNodeSchema ITerrainNode::Describe() const
	{
		TgNodeSchema s;
		s.Inputs = { { "in", TgPortKind::Field, true } };
		s.Outputs = { { "out", TgPortKind::Field } };
		return s;
	}

	bool ITerrainNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>&, TgField&, std::string& error) const
	{
		error = "node '" + node.Id + "' (" + node.Type + ") has no single-field evaluation";
		return false;
	}

	bool ITerrainNode::Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		std::vector<const TgField*> fields;
		fields.reserve(inputs.size());
		for (const TgValue* v : inputs) fields.push_back(v && v->Kind == TgPortKind::Field ? &v->Field : nullptr);

		const TgNodeSchema schema = Describe();
		auto first = [&](TgPortKind kind) -> const TgPortInfo*
		{
			for (const TgPortInfo& p : schema.Outputs) if (p.Kind == kind) return &p;
			return nullptr;
		};
		if (const TgPortInfo* port = first(TgPortKind::Field))
		{
			TgValue& value = outputs[port->Name];
			value.Kind = TgPortKind::Field;
			if (!Evaluate(node, ctx, fields, value.Field, error)) return false;
		}
		if (const IScatterNode* scatter = AsScatterNode())
		{
			const TgPortInfo* port = first(TgPortKind::Placements);
			TgValue& value = outputs[port ? port->Name : "placements"];
			value.Kind = TgPortKind::Placements;
			if (!scatter->Scatter(node, ctx, fields, value.Placements, error))
			{
				if (error.empty()) error = "node '" + node.Id + "' failed to scatter";
				return false;
			}
		}
		if (const IPathNode* tracer = AsPathNode())
		{
			const TgPortInfo* port = first(TgPortKind::Paths);
			TgValue& value = outputs[port ? port->Name : "paths"];
			value.Kind = TgPortKind::Paths;
			if (!tracer->Trace(node, ctx, fields, value.Paths, error))
			{
				if (error.empty()) error = "node '" + node.Id + "' failed to trace";
				return false;
			}
		}
		return true;
	}

	void TerrainNodeRegistry::Register(std::string type, Factory factory)
	{
		if (!factory) return;
		std::unique_ptr<ITerrainNode> node = factory();
		if (!node) return;
		TgNodeSchema schema = node->Describe();
		m_Nodes[std::move(type)] = { std::move(node), std::move(schema) };
	}

	bool TerrainNodeRegistry::Unregister(const std::string& type) { return m_Nodes.erase(type) > 0; }

	bool TerrainNodeRegistry::IsKnown(const std::string& type) const { return m_Nodes.find(type) != m_Nodes.end(); }

	const ITerrainNode* TerrainNodeRegistry::Find(const std::string& type) const
	{
		const auto it = m_Nodes.find(type);
		return it == m_Nodes.end() ? nullptr : it->second.Node.get();
	}

	const TgNodeSchema* TerrainNodeRegistry::Schema(const std::string& type) const
	{
		const auto it = m_Nodes.find(type);
		return it == m_Nodes.end() ? nullptr : &it->second.Schema;
	}

	std::vector<std::string> TerrainNodeRegistry::KnownTypes() const
	{
		std::vector<std::string> types;
		types.reserve(m_Nodes.size());
		for (const auto& entry : m_Nodes) types.push_back(entry.first);
		return types;
	}

	TerrainNodeRegistry MakeDefaultTerrainNodeRegistry()
	{
		TerrainNodeRegistry registry;
		RegisterBuiltinTerrainNodes(registry);
		return registry;
	}
}
