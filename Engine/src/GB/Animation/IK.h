#pragma once
#include "GB/Animation/Pose.h"
#include <glm/gtc/quaternion.hpp>

namespace GB
{
	// Three joints in a row (shoulder - elbow - wrist, hip - knee - ankle): `Root` and `Mid` are rotated, `End` is the one that
	// has to arrive at the target.
	struct TwoBoneChain
	{
		int Root = -1, Mid = -1, End = -1;
		bool Valid() const { return Root >= 0 && Mid >= 0 && End >= 0; }
	};

	struct IKResult
	{
		bool Solved = false;        // the chain exists and has a length
		bool Reached = false;       // the target was within the chain's reach (the end is on it, not just aimed at it)
		float Error = 0.0f;         // meters between the end and the target afterwards (before the weight blend)
	};

	// Analytic two-bone IK on a pose (model space). The chain bends in the plane that contains the target and `pole` (the
	// elbow / knee points toward it); lengths are kept, so an unreachable target gives a fully stretched chain aimed at it.
	// `weight` 0..1 blends the result over the pose. `endRotation` (optional) is the world rotation the end bone should end up
	// with (a hand matching the grip of a tool), blended by `weight` too.
	IKResult SolveTwoBoneIK(const Model& model, Pose& pose, const TwoBoneChain& chain, const glm::vec3& target, const glm::vec3& pole,
		float weight = 1.0f, const glm::quat* endRotation = nullptr);

	// The shortest rotation taking direction `from` to direction `to` (both need not be unit length).
	glm::quat RotationBetween(const glm::vec3& from, const glm::vec3& to);
}
