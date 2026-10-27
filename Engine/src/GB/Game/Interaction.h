#pragma once
#include "GB/Animation/AnimInstance.h"
#include "GB/Game/AIDriver.h"
#include "GB/Game/CharacterController.h"
#include "GB/Game/Entity.h"
#include "GB/Game/MotionWarp.h"
#include <string>
#include <vector>

namespace GB
{
	// One interaction point of one entity, in the world.
	struct InteractionTarget
	{
		std::string Entity, Point;
		glm::vec2 Position{ 0.0f };   // xz where the character has to stand
		float Yaw = 0.0f;             // radians: the facing, atan2(dirX, dirZ) like CharacterController::GetYaw
		std::string Montage, Requires, Gives;
		float WarpSeconds = 0.4f;
	};

	// Finds an interaction point of the entity called `entity` (by name; `point` empty = its first one) and puts it in world space.
	bool FindInteraction(const EntityWorld& world, const std::string& entity, const std::string& point, InteractionTarget& out, std::string* error = nullptr);
	// Every "entity/point" the scene offers, for listings.
	std::vector<std::string> ListInteractions(const EntityWorld& world);

	// Goes to an interaction point and uses it: approach (the controller is led by an AIDriver, exactly the input a player's keys would
	// give) -> align (motion warp onto the exact spot and facing while the montage blends in) -> act (the montage plays; its notifies are
	// reported) -> done. Players and NPCs run the same flow; only who starts it differs.
	class InteractionRunner
	{
	public:
		enum class Phase { Idle, Approach, Align, Act, Done, Failed };

		float ActSeconds = 1.0f;   // how long "acting" lasts with no animation or no montage to wait for

		// False (and Failed) when the character lacks the prop the point requires. `run`: hurry there.
		bool Start(const InteractionTarget& target, AnimInstance* anim, bool run = false);
		void Cancel();
		Phase GetPhase() const { return m_Phase; }
		bool Active() const { return m_Phase == Phase::Approach || m_Phase == Phase::Align || m_Phase == Phase::Act; }
		const std::string& Error() const { return m_Error; }
		const InteractionTarget& Target() const { return m_Target; }

		// Call once per frame before the controller's step; returns the input to step it with (nothing while it is being placed or acting).
		CharacterInput Update(CharacterController& controller, const CharacterWorld& world, AnimInstance* anim, float dt);

		// What happened this frame: "notify:<name>" for the montage's notifies, "gives:<prop>" and "done" at the end.
		const std::vector<std::string>& Events() const { return m_Events; }

	private:
		void Fail(const std::string& why);

		Phase m_Phase = Phase::Idle;
		InteractionTarget m_Target;
		AIDriver m_AI;
		MotionWarp m_Warp;
		std::string m_Error;
		std::vector<std::string> m_Events;
		float m_ActTime = 0.0f;
		bool m_MontageStarted = false;
	};
}
