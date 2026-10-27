#pragma once
#include "GB/Assets/Model.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace GB
{
	// Local transform of one bone.
	struct BonePose
	{
		glm::vec3 T{ 0.0f };
		glm::quat R{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 S{ 1.0f };
	};
	// One BonePose per model node: what blending, masking and root-motion removal work on, before it is
	// handed to a ModelInstance (SetPose) to become world matrices and joint palettes.
	using Pose = std::vector<BonePose>;

	Pose RestPose(const Model& model);
	// Samples a clip at `time` seconds (clamped to its length; wrap the time yourself for loops). Bones the clip
	// does not animate keep the rest pose.
	void SampleClip(const Model& model, int clip, float time, Pose& out);
	// out = a * (1 - t) + b * t (rotations take the short way round). `out` may be `a`.
	void BlendPoses(const Pose& a, const Pose& b, float t, Pose& out);
	// Like BlendPoses with a per-bone weight in [0, 1] that scales t (an upper-body mask, for example).
	void BlendPosesMasked(const Pose& a, const Pose& b, float t, const std::vector<float>& mask, Pose& out);
	// Model-space world matrix of every node for `pose` (what ModelInstance computes after SetPose, without needing an instance).
	void PoseToWorld(const Model& model, const Pose& pose, std::vector<glm::mat4>& world);
	// 1 for `rootBone` and everything below it, 0 elsewhere. Empty when the bone does not exist.
	std::vector<float> MakeBoneMask(const Model& model, const std::string& rootBone);
}
