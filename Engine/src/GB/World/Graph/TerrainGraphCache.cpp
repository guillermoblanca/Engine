#include "GB/World/Graph/TerrainGraphCache.h"
#include <algorithm>

namespace GB
{
	void TerrainGraphCache::Clear()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		std::map<std::string, Entry>().swap(m_Entries);
		m_Stats = {};
		Tally();
		++m_Generation;
	}

	TerrainGraphCache::Stats TerrainGraphCache::LastStats() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Stats;
	}

	size_t TerrainGraphCache::Size() const { return m_Count.load(); }

	void TerrainGraphCache::Tally()
	{
		size_t bytes = 0;
		for (const auto& [id, entry] : m_Entries) bytes += entry.Bytes;
		m_Bytes = bytes;
		m_Count = m_Entries.size();
	}

	size_t TerrainGraphCache::BytesOf(const TgValue& v)
	{
		size_t bytes = v.Field.Values.capacity() * sizeof(float) + v.Placements.Items.capacity() * sizeof(TgPlacement);
		for (const TgPath& p : v.Paths.Items) bytes += p.Points.capacity() * sizeof(glm::vec2) + p.Values.capacity() * sizeof(float);
		if (const RiverData* river = v.Water.River())
			bytes += river->Points.capacity() * sizeof(glm::vec3) + (river->Speed.capacity() + river->HalfWidths.capacity()) * sizeof(float);
		return bytes;
	}

	size_t TerrainGraphCache::Bytes() const { return m_Bytes.load(); }

	void TerrainGraphCache::SetByteBudget(size_t bytes)
	{
		m_Budget = bytes;
		std::unique_lock<std::mutex> lock(m_Mutex, std::try_to_lock);
		if (!lock.owns_lock()) return;   // (the evaluation running fits it when it ends)
		FitBudget();
		Tally();
	}

	size_t TerrainGraphCache::ByteBudget() const { return m_Budget.load(); }

	void TerrainGraphCache::SetPinned(const std::string& nodeId)
	{
		std::lock_guard<std::mutex> lock(m_PinMutex);
		m_Pinned = nodeId;
	}

	void TerrainGraphCache::FitBudget()
	{
		const size_t budget = m_Budget.load();
		if (budget == 0) return;
		size_t total = 0;
		for (const auto& [id, entry] : m_Entries) total += entry.Bytes;
		if (total <= budget) return;
		std::string pinned;
		{
			std::lock_guard<std::mutex> lock(m_PinMutex);
			pinned = m_Pinned;
		}
		// cheapest to run again per byte first; ties by id, so what is dropped does not depend on the order things were stored in
		std::vector<std::pair<double, std::string>> order;
		for (const auto& [id, entry] : m_Entries)
			if (id != pinned && entry.Bytes > 0) order.push_back({ entry.Ms / static_cast<double>(entry.Bytes), id });
		std::sort(order.begin(), order.end());
		for (const auto& [value, id] : order)
		{
			if (total <= budget) break;
			const auto it = m_Entries.find(id);
			total -= it->second.Bytes;
			m_Entries.erase(it);
			++m_Stats.Evicted;
		}
	}

	uint64_t TerrainGraphCache::Generation() const { return m_Generation.load(); }

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
