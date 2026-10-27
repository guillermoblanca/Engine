#include "GB/Animation/IK.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		glm::quat WorldRotation(const glm::mat4& m)
		{
			return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])))));
		}

		// Local rotation that gives `bone` the world rotation `worldRotation` under its current parent.
		glm::quat LocalFor(const Model& model, const std::vector<glm::mat4>& world, int bone, const glm::quat& worldRotation)
		{
			const int parent = model.GetNodes()[static_cast<size_t>(bone)].Parent;
			const glm::quat parentRotation = parent >= 0 ? WorldRotation(world[static_cast<size_t>(parent)]) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			return glm::normalize(glm::inverse(parentRotation) * worldRotation);
		}
	}

	glm::quat RotationBetween(const glm::vec3& from, const glm::vec3& to)
	{
		const float lf = glm::length(from), lt = glm::length(to);
		if (lf < 1e-8f || lt < 1e-8f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		const glm::vec3 a = from / lf, b = to / lt;
		const float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
		if (d > 0.999999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		if (d < -0.999999f)
		{
			glm::vec3 axis = glm::cross(a, glm::vec3(1.0f, 0.0f, 0.0f));
			if (glm::length(axis) < 1e-4f) axis = glm::cross(a, glm::vec3(0.0f, 1.0f, 0.0f));
			return glm::angleAxis(3.14159265f, glm::normalize(axis));
		}
		const glm::vec3 axis = glm::cross(a, b);
		return glm::normalize(glm::quat(1.0f + d, axis.x, axis.y, axis.z));
	}

	IKResult SolveTwoBoneIK(const Model& model, Pose& pose, const TwoBoneChain& chain, const glm::vec3& target, const glm::vec3& pole,
		float weight, const glm::quat* endRotation)
	{
		IKResult result;
		if (!chain.Valid() || weight <= 0.0f) return result;
		std::vector<glm::mat4> world;
		PoseToWorld(model, pose, world);
		const glm::vec3 a(world[static_cast<size_t>(chain.Root)][3]), b(world[static_cast<size_t>(chain.Mid)][3]), c(world[static_cast<size_t>(chain.End)][3]);
		const float l1 = glm::length(b - a), l2 = glm::length(c - b);
		if (l1 < 1e-5f || l2 < 1e-5f) return result;
		result.Solved = true;

		const glm::vec3 toTarget = target - a;
		const float dist = glm::length(toTarget);
		if (dist < 1e-6f) return result;
		const glm::vec3 u = toTarget / dist;
		const float maxReach = l1 + l2 - 1e-4f, minReach = std::fabs(l1 - l2) + 1e-4f;
		const float d = glm::clamp(dist, minReach, maxReach);
		result.Reached = dist <= maxReach && dist >= minReach;

		// the bend plane: toward the pole, falling back to the way the chain already bends
		glm::vec3 p = (pole - a) - u * glm::dot(pole - a, u);
		if (glm::length(p) < 1e-5f) p = (b - a) - u * glm::dot(b - a, u);
		if (glm::length(p) < 1e-5f) p = glm::cross(u, std::fabs(u.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f));
		p = glm::normalize(p);

		const float cosA = glm::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
		const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
		const glm::vec3 newMid = a + u * (l1 * cosA) + p * (l1 * sinA);

		const BonePose original[3] = { pose[static_cast<size_t>(chain.Root)], pose[static_cast<size_t>(chain.Mid)], pose[static_cast<size_t>(chain.End)] };
		Pose solved = pose;
		// 1. the root swings the mid joint to its place
		const glm::quat q1 = RotationBetween(b - a, newMid - a);
		solved[static_cast<size_t>(chain.Root)].R = LocalFor(model, world, chain.Root, q1 * WorldRotation(world[static_cast<size_t>(chain.Root)]));
		PoseToWorld(model, solved, world);
		// 2. the mid joint swings the end onto the target
		const glm::vec3 mid2(world[static_cast<size_t>(chain.Mid)][3]), end2(world[static_cast<size_t>(chain.End)][3]);
		const glm::quat q2 = RotationBetween(end2 - mid2, target - mid2);
		solved[static_cast<size_t>(chain.Mid)].R = LocalFor(model, world, chain.Mid, q2 * WorldRotation(world[static_cast<size_t>(chain.Mid)]));
		PoseToWorld(model, solved, world);
		result.Error = glm::distance(glm::vec3(world[static_cast<size_t>(chain.End)][3]), target);
		// 3. the end bone takes the wanted orientation
		if (endRotation)
			solved[static_cast<size_t>(chain.End)].R = LocalFor(model, world, chain.End, glm::normalize(*endRotation));

		const int bones[3] = { chain.Root, chain.Mid, chain.End };
		for (int i = 0; i < 3; ++i)
		{
			if (i == 2 && !endRotation) break;
			BonePose& out = pose[static_cast<size_t>(bones[i])];
			glm::quat from = original[i].R;
			glm::quat to = solved[static_cast<size_t>(bones[i])].R;
			if (glm::dot(from, to) < 0.0f) to = -to;
			out.R = glm::normalize(glm::slerp(from, to, glm::clamp(weight, 0.0f, 1.0f)));
		}
		return result;
	}
}
