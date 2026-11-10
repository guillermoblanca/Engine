#include "GB/World/Graph/TerrainGraphCache.h"

namespace GB
{
	void TerrainGraphCache::Clear()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Entries.clear();
		m_Stats = {};
		++m_Generation;
	}

	TerrainGraphCache::Stats TerrainGraphCache::LastStats() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Stats;
	}

	size_t TerrainGraphCache::Size() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Entries.size();
	}

	uint64_t TerrainGraphCache::Generation() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Generation;
	}

	bool TerrainGraphCache::Footprint(float& size, glm::vec2& center) const
	{
		std::unique_lock<std::mutex> lock(m_Mutex, std::try_to_lock);
		if (!lock.owns_lock() || !m_HasFootprint) return false;
		size = m_Size;
		center = m_Center;
		return true;
	}

	bool TerrainGraphCache::TryCopyOutput(const std::string& nodeId, const std::string& port, TgValue& out) const
	{
		std::unique_lock<std::mutex> lock(m_Mutex, std::try_to_lock);
		if (!lock.owns_lock()) return false;
		const auto it = m_Entries.find(nodeId);
		if (it == m_Entries.end() || it->second.Outputs.empty()) return false;
		const auto& outputs = it->second.Outputs;
		const auto value = port.empty() ? outputs.begin() : outputs.find(port);
		if (value == outputs.end()) return false;
		out = value->second;
		return true;
	}

	std::vector<std::string> TerrainGraphCache::OutputPorts(const std::string& nodeId) const
	{
		std::vector<std::string> ports;
		std::unique_lock<std::mutex> lock(m_Mutex, std::try_to_lock);
		if (!lock.owns_lock()) return ports;
		const auto it = m_Entries.find(nodeId);
		if (it != m_Entries.end())
			for (const auto& [port, value] : it->second.Outputs) ports.push_back(port);
		return ports;
	}
}
