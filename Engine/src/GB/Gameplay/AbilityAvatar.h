#pragma once
#include "GB/Scene/CharacterTypes.h"
#include <memory>
#include <string>

namespace GB
{
	class AnimInstance;

	// The body an ability system moves (architecture step 4, H8). The abilities live in Gameplay and do not know the character controller,
	// the interaction points or the walker of Game: they ask their avatar to take the character somewhere and read back, each frame, the input
	// that walk wants. Game implements it (CharacterAbilityAvatar, GB/Game/CharacterAbilityAvatar.h); without an avatar the abilities run on timers.

	// One walk of the character to an interaction point and its settling there (MoveToTargetTask). It keeps its own state, so cancelling it
	// needs nothing else.
	class IAbilityMove
	{
	public:
		enum class Status { Running, Done, Failed };
		virtual ~IAbilityMove() = default;
		// Once per frame, before the character's controller step: the input to step it with.
		virtual CharacterInput Update(AnimInstance* anim, float dt) = 0;
		virtual Status GetStatus() const = 0;
		virtual const std::string& Error() const = 0;   // why it failed
		virtual void Cancel() = 0;
	};

	class IAbilityAvatar
	{
	public:
		virtual ~IAbilityAvatar() = default;
		// There is a character to walk (an ability that goes to its target walks there; otherwise it acts in place).
		virtual bool CanWalk() const = 0;
		// Starts walking to the interaction point `point` of `entity` ("" = its first one), to stand there with no action of its own (the
		// montage is the ability's next task). Null and `error` ("" when the avatar has nothing to say) when there is no such point; a move
		// that could not start comes back already Failed, with its error.
		virtual std::unique_ptr<IAbilityMove> MoveTo(const std::string& entity, const std::string& point, AnimInstance* anim, std::string& error) = 0;
	};
}
