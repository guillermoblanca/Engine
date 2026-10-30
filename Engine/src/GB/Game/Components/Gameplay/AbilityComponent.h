#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Game/GBObject.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Game/TaskList.h"
#include <functional>
#include <map>
#include <string>

namespace GB
{
	// The link of an actor to its ability system: the one called `name` in a GameplayWorld (the scene keeps its attributes and cooldowns), and the
	// runtime it lends it (the body, the animation, the interaction points) so the abilities can walk and play montages. Without a runtime the
	// abilities run on timers, which is all an actor with no model needs.
	class AbilityComponent : public GBObjectComponent
	{
	public:
		// What the abilities may ask of the actor: a prop in the hand, and the montage the animation bindings pick.
		void SetHooks(std::function<bool(const std::string&, bool)> equip,
			std::function<std::string(const AbilityDef&, int, const std::map<std::string, std::string>&)> pickMontage)
		{
			m_Equip = std::move(equip);
			m_PickMontage = std::move(pickMontage);
		}

		// This actor is the ability system called `name` of `gameplay` (null: none). The one it had is cancelled and let go.
		void Link(GameplayWorld* gameplay, const std::string& name);
		GameplayWorld* Gameplay() const { return m_Gameplay; }
		AbilitySystemComponent* Abilities() const { return m_Gameplay ? m_Gameplay->Find(m_Name) : nullptr; }
		const std::string& AbilityName() const { return m_Name; }

		// Grants the work every actor can do and lends the runtime, or takes it back when there is nothing to lend (no animation, a fallen ragdoll).
		void Prepare(AnimInstance* anim, bool fallen, CharacterController& controller, const CharacterWorld& world, const TaskRunner::Resolver& interactions);
		// Cancels what runs and takes the runtime back.
		void Detach();

		void EndPlay(GBObject&) override { Detach(); }
		void OnDestroy(GBObject&) override { Detach(); }

	private:
		GameplayWorld* m_Gameplay = nullptr;
		std::string m_Name;   // the actor's own, except the player, which the editor links to the one it chose
		AbilityRuntime m_Runtime;
		std::function<bool(const std::string&, bool)> m_Equip;
		std::function<std::string(const AbilityDef&, int, const std::map<std::string, std::string>&)> m_PickMontage;
	};
}
