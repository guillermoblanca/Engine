#pragma once
#include "GB/Animation/Pose.h"
#include "GB/Game/MotionState.h"
#include <string>

namespace GB
{
	// A layer over the locomotion pose: the jump, the turn on the spot, the montages, or one the game adds (AnimInstance::AddLayer). Each frame
	// the instance updates it and, while it is active, lets it change the pose.
	class IAnimLayer
	{
	public:
		virtual ~IAnimLayer() = default;
		// `set` is the locomotion set that is active (a turn clip belongs to a set).
		virtual void Update(const MotionState& state, float dt, const std::string& set) = 0;
		virtual bool Active() const = 0;
		virtual void Apply(Pose& pose) = 0;
		// How much of the legs the layer takes over, 0..1: past one half the feet are not walking, so there are no footsteps.
		virtual float LegsWeight() const = 0;
	};
}
