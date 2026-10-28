#include "GB/AI/Memory.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	void SpatialMemory::Observe(const MemoryEntry& entry)
	{
		auto it = m_Entries.find(entry.Name);
		if (it == m_Entries.end()) { m_Entries[entry.Name] = entry; return; }
		MemoryEntry& old = it->second;
		if (entry.Heard && !old.Heard && old.Seen >= entry.Seen) return;   // a sighting is worth more than a sound
		if (entry.Seen >= old.Seen) old = entry;
	}

	const MemoryEntry* SpatialMemory::Find(const std::string& name) const
	{
		const auto it = m_Entries.find(name);
		return it == m_Entries.end() ? nullptr : &it->second;
	}

	bool SpatialMemory::Refresh(const std::string& name, const glm::vec2& pos, int amount, double seen)
	{
		const auto it = m_Entries.find(name);
		if (it == m_Entries.end() || seen < it->second.Seen) return false;
		MemoryEntry& e = it->second;
		e.Pos = pos;
		e.Amount = amount;
		e.Seen = seen;
		e.Heard = false;
		return true;
	}

	bool SpatialMemory::Forget(const std::string& name) { return m_Entries.erase(name) > 0; }

	int SpatialMemory::Expire(double now, const MemoryConfig& cfg)
	{
		int removed = 0;
		for (auto it = m_Entries.begin(); it != m_Entries.end();)
		{
			if (now - it->second.Seen > cfg.Lifetime) { it = m_Entries.erase(it); ++removed; }
			else ++it;
		}
		return removed;
	}

	int SpatialMemory::Merge(const SpatialMemory& other)
	{
		int changed = 0;
		for (const auto& [name, e] : other.m_Entries)
		{
			const auto it = m_Entries.find(name);
			if (it != m_Entries.end() && it->second.Seen >= e.Seen) continue;
			MemoryEntry copy = e;
			copy.Heard = true;   // told, not seen: the next sighting settles it
			m_Entries[name] = copy;
			++changed;
		}
		return changed;
	}

	std::vector<const MemoryEntry*> SpatialMemory::OfKind(const std::string& kind) const
	{
		std::vector<const MemoryEntry*> out;
		for (const auto& [name, e] : m_Entries)
			if (kind.empty() || e.Kind == kind) out.push_back(&e);
		return out;
	}

	float SpatialMemory::Confidence(const MemoryEntry& e, double now, const MemoryConfig& cfg)
	{
		const double age = std::max(0.0, now - e.Seen);
		if (age >= cfg.Lifetime) return 0.0f;
		if (e.Amount == 0 && age < cfg.RecheckSeconds) return 0.0f;
		const float fresh = static_cast<float>(1.0 - age / cfg.Lifetime);
		return 0.35f + 0.65f * fresh;   // old news is still worth a walk, just a bit less
	}

	std::string SpatialMemory::ToJson() const
	{
		Json a = Json::array();
		for (const auto& [name, e] : m_Entries)
			a.push_back(Json{ { "n", e.Name }, { "k", e.Kind }, { "x", e.Pos.x }, { "z", e.Pos.y },
				{ "a", e.Amount }, { "t", e.Seen }, { "h", e.Heard ? 1 : 0 } });
		return a.dump();
	}

	bool SpatialMemory::FromJson(const std::string& json)
	{
		Json a = Json::parse(json, nullptr, false);
		if (a.is_discarded() || !a.is_array()) return false;
		std::map<std::string, MemoryEntry> parsed;
		for (const Json& j : a)
		{
			if (!j.is_object() || !j.contains("n") || !j["n"].is_string()) return false;
			MemoryEntry e;
			e.Name = j["n"].get<std::string>();
			e.Kind = j.value("k", std::string());
			e.Pos = { j.value("x", 0.0f), j.value("z", 0.0f) };
			e.Amount = j.value("a", -1);
			e.Seen = j.value("t", 0.0);
			e.Heard = j.value("h", 0) != 0;
			parsed[e.Name] = e;
		}
		m_Entries = std::move(parsed);
		return true;
	}
}
