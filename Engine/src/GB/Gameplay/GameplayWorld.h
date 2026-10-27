#pragma once
#include "GB/Game/Entity.h"
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
		void Clear() { m_Systems.clear(); }
		std::vector<std::string> Names() const;
		size_t Count() const { return m_Systems.size(); }

		// One ability system per entity with a GameplayComponent (the old ones go). Returns how many.
		int Load(const EntityWorld& world);
		// Writes every system's state into the component of the entity of the same name; returns how many.
		int Store(EntityWorld& world) const;

		void Update(float dt);

		// "dig 'Plot 1'" for a listing, "Player: Energy 80/100 ..." for a status line.
		std::string Describe(const std::string& name) const;

	private:
		std::map<std::string, std::unique_ptr<AbilitySystemComponent>> m_Systems;
	};
}
