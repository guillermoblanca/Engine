#pragma once
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// A gameplay tag is a dotted path: "Ability.Plant", "State.Carrying.Box". A tag matches another when it is the same or one of its
	// ancestors, so asking for "State.Carrying" is true for a character that holds "State.Carrying.Box". Pure text, no registry.
	class GameplayTag
	{
	public:
		GameplayTag() = default;
		GameplayTag(const std::string& name) : m_Name(name) {}
		GameplayTag(const char* name) : m_Name(name ? name : "") {}

		const std::string& Name() const { return m_Name; }
		bool Valid() const { return IsValidTagName(m_Name); }
		// "State.Carrying.Box" -> "State.Carrying" (empty for a root tag).
		GameplayTag Parent() const;
		// True when `held` is this tag or a descendant of it: Tag("State.Carrying").Matches("State.Carrying.Box").
		bool Matches(const GameplayTag& held) const { return TagMatches(m_Name, held.m_Name); }
		bool operator==(const GameplayTag& o) const { return m_Name == o.m_Name; }
		bool operator<(const GameplayTag& o) const { return m_Name < o.m_Name; }

		static bool TagMatches(const std::string& query, const std::string& held);
		static bool IsValidTagName(const std::string& name);

	private:
		std::string m_Name;
	};

	// The tags something holds, with a count each (two effects can grant the same tag; it goes away when both have). Ordered by name, so
	// listings and saves are deterministic.
	class GameplayTagContainer
	{
	public:
		void Add(const GameplayTag& tag, int count = 1);
		// Takes `count` away (the tag disappears at zero). False when it was not held.
		bool Remove(const GameplayTag& tag, int count = 1);
		// Takes the exact tag away whatever its count.
		bool RemoveAll(const GameplayTag& tag);
		void Clear() { m_Tags.clear(); }

		// True when any held tag is `query` or below it.
		bool Has(const GameplayTag& query) const;
		bool HasExact(const GameplayTag& tag) const { return m_Tags.count(tag.Name()) > 0; }
		bool HasAll(const std::vector<std::string>& queries) const;
		bool HasAny(const std::vector<std::string>& queries) const;
		int Count(const GameplayTag& tag) const;
		bool Empty() const { return m_Tags.empty(); }
		std::vector<std::string> Names() const;
		const std::map<std::string, int>& All() const { return m_Tags; }

	private:
		std::map<std::string, int> m_Tags;
	};
}
