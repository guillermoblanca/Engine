#include "GB/Gameplay/GameplayTag.h"

namespace GB
{
	bool GameplayTag::IsValidTagName(const std::string& name)
	{
		if (name.empty() || name.front() == '.' || name.back() == '.') return false;
		char last = 0;
		for (char c : name)
		{
			const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
			if (!ok || (c == '.' && last == '.')) return false;
			last = c;
		}
		return true;
	}

	GameplayTag GameplayTag::Parent() const
	{
		const size_t dot = m_Name.rfind('.');
		return dot == std::string::npos ? GameplayTag() : GameplayTag(m_Name.substr(0, dot));
	}

	bool GameplayTag::TagMatches(const std::string& query, const std::string& held)
	{
		if (query.empty()) return false;
		if (held == query) return true;
		return held.size() > query.size() && held.compare(0, query.size(), query) == 0 && held[query.size()] == '.';
	}

	void GameplayTagContainer::Add(const GameplayTag& tag, int count)
	{
		if (!tag.Valid() || count <= 0) return;
		m_Tags[tag.Name()] += count;
	}

	bool GameplayTagContainer::Remove(const GameplayTag& tag, int count)
	{
		auto it = m_Tags.find(tag.Name());
		if (it == m_Tags.end()) return false;
		it->second -= count;
		if (it->second <= 0) m_Tags.erase(it);
		return true;
	}

	bool GameplayTagContainer::RemoveAll(const GameplayTag& tag) { return m_Tags.erase(tag.Name()) > 0; }

	bool GameplayTagContainer::Has(const GameplayTag& query) const
	{
		for (const auto& [name, count] : m_Tags)
			if (GameplayTag::TagMatches(query.Name(), name)) return true;
		return false;
	}

	bool GameplayTagContainer::HasAll(const std::vector<std::string>& queries) const
	{
		for (const std::string& q : queries)
			if (!Has(q)) return false;
		return true;
	}

	bool GameplayTagContainer::HasAny(const std::vector<std::string>& queries) const
	{
		for (const std::string& q : queries)
			if (Has(q)) return true;
		return false;
	}

	int GameplayTagContainer::Count(const GameplayTag& tag) const
	{
		auto it = m_Tags.find(tag.Name());
		return it == m_Tags.end() ? 0 : it->second;
	}

	std::vector<std::string> GameplayTagContainer::Names() const
	{
		std::vector<std::string> out;
		for (const auto& [name, count] : m_Tags) out.push_back(name);
		return out;
	}
}
