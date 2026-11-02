#include "GB/World/Graph/ITerrainNode.h"
#include "GB/World/Graph/Nodes/BuiltinNodes.h"

namespace GB
{
	void TerrainNodeRegistry::Register(std::string type, Factory factory)
	{
		if (!factory) return;
		std::unique_ptr<ITerrainNode> node = factory();
		if (!node) return;
		m_Nodes[std::move(type)] = std::move(node);
	}

	bool TerrainNodeRegistry::Unregister(const std::string& type) { return m_Nodes.erase(type) > 0; }

	bool TerrainNodeRegistry::IsKnown(const std::string& type) const { return m_Nodes.find(type) != m_Nodes.end(); }

	const ITerrainNode* TerrainNodeRegistry::Find(const std::string& type) const
	{
		const auto it = m_Nodes.find(type);
		return it == m_Nodes.end() ? nullptr : it->second.get();
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
