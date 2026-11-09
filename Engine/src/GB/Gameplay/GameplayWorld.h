#pragma once
#include <atomic>
#include "GB/Scene/Entity.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// The ability systems of a scene, by entity name, and the library of effects and abilities they share. Entities with a GameplayComponent get
	// one when the scene is loaded into it (Load) and the state goes back into the components with Store (so a scene file keeps it).
	// Ordered by name: updating, listing and saving are deterministic.
	class GameplayWorld
	{
	public:
		GameplayLibrary Library;

		AbilitySystemComponent* Create(const std::string& name);
		AbilitySystemComponent* Find(const std::string& name);
		const AbilitySystemComponent* Find(const std::string& name) const;
		bool Remove(const std::string& name);
		void Clear() { m_Systems.clear(); m_Revision = NextRevision(); }
		std::vector<std::string> Names() const;
		// The systems by name, to walk without copying the names (a world with thousands of resource nodes walks them every tick). Not while
		// systems are created or removed: then walk a copy of Names().
		const std::map<std::string, std::unique_ptr<AbilitySystemComponent>>& Systems() const { return m_Systems; }
		size_t Count() const { return m_Systems.size(); }
		// Changes whenever a system is created or removed (or all of them: Clear, Load), so what lists the systems of some kind can keep its list
		// until it changes.
		uint64_t Revision() const { return m_Revision; }
		// The systems carrying a tag (or one under it: "Animal" lists every Animal.<kind>), in name order, kept until the next Revision: what walks
		// the few animals of a world of thousands of resource nodes asks this, not Systems(). The tags are read when the list is made (after a
		// system is created and given its tags); a tag added to an existing system without one being created or removed shows at the next change,
		// so a caller still checks what may change on the way (State.Dead, State.Held).
		struct Tagged { const std::string* Name; AbilitySystemComponent* System; };
		const std::vector<Tagged>& WithTag(const std::string& tag) const;
		// The names of those systems, copied: for a loop that may create or remove systems on the way (look each up again with Find).
		std::vector<std::string> NamesWithTag(const std::string& tag) const { std::vector<std::string> out; for (const Tagged& t : WithTag(tag)) out.push_back(*t.Name); return out; }

		// One ability system per entity with a GameplayComponent (the old ones go). Returns how many.
		int Load(const EntityWorld& world);
		// Writes every system's state into the component of the entity of the same name; returns how many.
		int Store(EntityWorld& world) const;

		void Update(float dt);

		// "dig 'Plot 1'" for a listing, "Player: Energy 80/100 ..." for a status line.
		std::string Describe(const std::string& name) const;

	private:
		std::map<std::string, std::unique_ptr<AbilitySystemComponent>> m_Systems;
		// Unique across every world of the process, not counted per world: a cache keyed by (world address, revision) cannot take a new world made
		// where a freed one was for it (the clans' sights keep pointers to the systems: a test that made its worlds on the stack crashed on them).
		static uint64_t NextRevision() { static std::atomic<uint64_t> next{ 1 }; return next++; }
		uint64_t m_Revision = NextRevision();
		mutable std::map<std::string, std::pair<uint64_t, std::vector<Tagged>>> m_Tagged;   // tag -> (the revision it was listed at, the list)
	};

	// The systems that hold any of some tags (asked as GameplayTagContainer::Has asks: "Building" holds "Building.Kind.hut" too), listed again only
	// when the world's systems change (GameplayWorld::Revision): what updates the plots, the sites or the resource nodes every tick walks them, not
	// every system of the scene. For tags a system has from when it is made, which is what kinds are (a kind tag added later is seen at the next
	// change of the world).
	class TaggedSystems
	{
	public:
		explicit TaggedSystems(std::vector<std::string> anyOf) : m_Tags(std::move(anyOf)) {}
		struct Entry { const std::string* Name; AbilitySystemComponent* System; };
		const std::vector<Entry>& Of(const GameplayWorld& world)
		{
			if (m_World != &world || m_Revision != world.Revision() || !m_Built)
			{
				m_List.clear();
				for (const auto& [name, system] : world.Systems())
					for (const std::string& tag : m_Tags)
						if (system && system->HasTag(tag)) { m_List.push_back({ &name, system.get() }); break; }
				m_World = &world;
				m_Revision = world.Revision();
				m_Built = true;
			}
			return m_List;
		}
		void Reset() { m_Built = false; }

	private:
		std::vector<std::string> m_Tags;
		std::vector<Entry> m_List;
		const GameplayWorld* m_World = nullptr;
		uint64_t m_Revision = 0;
		bool m_Built = false;
	};
}
