#include "GB/World/Graph/ITerrainNode.h"
#include "GB/World/Graph/Nodes/BuiltinNodes.h"

namespace GB
{
	TerrainNodeRegistry& TerrainNodeRegistry::Instance()
	{
		// Built once, here: see BuiltinNodes.h for why the engine's own node types are registered from a
		// named composition root rather than from self-registering statics in each node's .cpp.
		static TerrainNodeRegistry registry = [] { TerrainNodeRegistry r; RegisterBuiltinTerrainNodes(r); return r; }();
		return registry;
	}

	void TerrainNodeRegistry::Register(std::string type, Factory factory)
	{
		m_Factories[std::move(type)] = std::move(factory);
	}

	bool TerrainNodeRegistry::IsKnown(const std::string& type) const { return m_Factories.find(type) != m_Factories.end(); }

	const ITerrainNode* TerrainNodeRegistry::Find(const std::string& type) const
	{
		const auto cached = m_Instances.find(type);
		if (cached != m_Instances.end()) return cached->second.get();

		const auto factory = m_Factories.find(type);
		if (factory == m_Factories.end()) return nullptr;

		std::unique_ptr<ITerrainNode> instance = factory->second();
		const ITerrainNode* raw = instance.get();
		m_Instances.emplace(type, std::move(instance));
		return raw;
	}

	std::vector<std::string> TerrainNodeRegistry::KnownTypes() const
	{
		std::vector<std::string> types;
		types.reserve(m_Factories.size());
		for (const auto& entry : m_Factories) types.push_back(entry.first);
		return types;
	}
}
