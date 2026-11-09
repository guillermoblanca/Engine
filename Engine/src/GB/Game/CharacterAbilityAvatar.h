#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Game/Interaction.h"
#include "GB/Gameplay/AbilityAvatar.h"
#include <functional>
#include <memory>
#include <string>

namespace GB
{
	// The avatar a character lends its abilities (IAbilityAvatar, GB/Gameplay/AbilityAvatar.h): its controller, the ground it walks on and
	// how to find interaction points. A move is an InteractionRunner with no action of its own. The pointers are observed (the actor owns
	// what they point at) and can change from one frame to the next: a move reads them when it runs.
	class CharacterAbilityAvatar : public IAbilityAvatar
	{
	public:
		using Resolver = std::function<bool(const std::string& entity, const std::string& point, InteractionTarget& out, std::string* error)>;

		CharacterController* Controller = nullptr;
		const CharacterWorld* World = nullptr;
		Resolver Resolve;   // finds an interaction point of an entity (see FindInteraction)

		bool CanWalk() const override { return Controller && World && Resolve; }
		std::unique_ptr<IAbilityMove> MoveTo(const std::string& entity, const std::string& point, AnimInstance* anim, std::string& error) override;
	};
}
