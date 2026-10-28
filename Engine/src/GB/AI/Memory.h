#pragma once
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// What an agent (or a clan) knows about the world (H5): the resources and places it has seen or heard about, where they are, how much was
	// there and when. An agent decides from this, not from the whole world. Knowledge fades: an entry not refreshed for `Lifetime` seconds is
	// forgotten, and a resource seen empty is not trusted again until `RecheckSeconds` have gone by. Keyed by the name of the thing, ordered,
	// so listing, merging and saving are deterministic.
	struct MemoryEntry
	{
		std::string Name;       // the entity: "Oak 2"
		std::string Kind;       // "tree", "rock", "berry_bush", "fiber_grass", "well", "agent"...
		glm::vec2 Pos{ 0.0f };
		int Amount = -1;        // what was left when last seen (-1 = unknown)
		double Seen = 0.0;      // when it was last seen or heard
		bool Heard = false;     // known by ear only (the position may be a few metres off)
	};

	struct MemoryConfig
	{
		double Lifetime = 2400.0;       // seconds before an entry is forgotten
		double RecheckSeconds = 90.0;   // seconds before a node seen empty is worth a visit again
	};

	class SpatialMemory
	{
	public:
		// Newer knowledge replaces older; a sound never replaces a sighting that is not older than it.
		void Observe(const MemoryEntry& entry);
		const MemoryEntry* Find(const std::string& name) const;
		// Looks at a known thing again: updates where, how much and when in place (the fast path of Observe). False when it was not known.
		bool Refresh(const std::string& name, const glm::vec2& pos, int amount, double seen);
		bool Forget(const std::string& name);
		void Clear() { m_Entries.clear(); }
		// Drops what has not been seen for longer than the lifetime; returns how many.
		int Expire(double now, const MemoryConfig& cfg);
		// Learns what `other` knows that is newer than its own (agents sharing at the camp); returns how many entries changed here.
		int Merge(const SpatialMemory& other);

		// Entries of a kind in name order (an empty kind = all).
		std::vector<const MemoryEntry*> OfKind(const std::string& kind) const;
		// Is it worth going there? 0 for unknown or an empty node that was seen too recently, fading to ~0 as it reaches the end of its lifetime.
		static float Confidence(const MemoryEntry& e, double now, const MemoryConfig& cfg);

		const std::map<std::string, MemoryEntry>& All() const { return m_Entries; }
		size_t Size() const { return m_Entries.size(); }

		// Compact JSON ([{"n":..,"k":..,"x":..,"z":..,"a":..,"t":..,"h":0}]); FromJson returns false (and changes nothing) on malformed text.
		std::string ToJson() const;
		bool FromJson(const std::string& json);

	private:
		std::map<std::string, MemoryEntry> m_Entries;
	};
}
