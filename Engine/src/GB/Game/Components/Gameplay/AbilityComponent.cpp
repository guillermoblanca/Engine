#include "GB/Game/Components/Gameplay/AbilityComponent.h"
#include "GB/Game/Interaction.h"

namespace GB
{
	void AbilityComponent::Detach()
	{
		if (AbilitySystemComponent* a = Abilities()) { a->CancelAll(); a->SetRuntime(nullptr); }
	}

	void AbilityComponent::Link(GameplayWorld* gameplay, const std::string& name)
	{
		Detach();
		m_Gameplay = gameplay;
		m_Name = name;
	}

	void AbilityComponent::Prepare(AnimInstance* anim, bool fallen, CharacterController& controller, const CharacterWorld& world, const TaskRunner::Resolver& interactions)
	{
		AbilitySystemComponent* asc = Abilities();
		if (!asc) return;
		for (const char* work : { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "OpenDoor", "CloseDoor", "Build" }) asc->Grant(work);   // every actor can work resource nodes, interactives and construction sites (H2, H3, H8); a library without them ignores this
		if (!anim || fallen)
		{
			if (asc->Runtime()) { asc->CancelAll(); asc->SetRuntime(nullptr); }
			return;
		}
		m_Runtime.Anim = anim;
		m_Runtime.Controller = &controller;
		m_Runtime.World = &world;
		m_Runtime.Resolve = interactions ? interactions : TaskRunner::Resolver([](const std::string&, const std::string&, InteractionTarget&, std::string*) { return false; });
		m_Runtime.Equip = m_Equip;
		m_Runtime.PickMontage = m_PickMontage;
		asc->SetRuntime(&m_Runtime);
	}
}
