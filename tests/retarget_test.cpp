// Retargeting: animations from another skeleton (other bone names, proportions, rest pose and facing) applied to a character. The map between
// the two skeletons, the movement kept in world space, the target's own proportions, the hips scaled to its size, the facing corrected,
// bones nobody drives left at rest, and a skeleton retargeted onto itself unchanged. The generated skeletons; CPU only.
#include "GB/Animation/Retarget.h"
#include "GB/Assets/Model.h"
#include "GB/Log.h"
#include <cmath>
#include <cstdio>
#include <glm/gtc/quaternion.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	float Angle(const glm::quat& q) { return 2.0f * std::acos(std::min(std::fabs(q.w), 1.0f)); }
	glm::quat Rotation(const glm::mat4& m) { return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2]))))); }
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;
	auto source = Model::Load(dir + "/test_retarget_src.fbx");
	auto target = Model::Load(dir + "/test_humanoid.fbx");
	if (!source || !target) { std::fprintf(stderr, "FAIL: test skeletons\n"); return 1; }
	const HumanoidRig tr = FindHumanoidRig(*target);
	const HumanoidRig sr = FindHumanoidRigAuto(*source);

	// ---- the map
	const RetargetMap map = BuildRetargetMap(*source, *target);
	Check(map.Valid() && map.SourceConvention == "unreal" && map.TargetConvention == "mixamo", "an Unreal skeleton is paired with a Mixamo one");
	Check(map.Humanoid >= 20, "the humanoid bones are paired (hips, spine, neck, head, arms, legs)");
	Check(std::fabs(map.Scale - 0.8f) < 0.02f && map.YawCorrection == 0.0f, "the target is 0.8 times the source's size, facing the same way");
	bool fingerUnmatched = false, spine1Unmatched = false;
	for (const std::string& n : map.UnmatchedTarget)
	{
		fingerUnmatched = fingerUnmatched || n == "mixamorig:LeftHandMiddle1";
		spine1Unmatched = spine1Unmatched || n == "mixamorig:Spine1";
	}
	Check(fingerUnmatched && spine1Unmatched, "bones the source has no counterpart for are listed (they keep their rest pose)");
	Check(DescribeRetargetMap(*source, *target, map).find("unreal -> mixamo") != std::string::npos, "the report names both conventions");

	// ---- the clip
	const int clip = RetargetClip(*source, 0, *target, map);
	Check(clip >= 0 && target->GetClips()[static_cast<size_t>(clip)].Name == source->GetClips()[0].Name && std::fabs(target->GetClips()[static_cast<size_t>(clip)].Duration - 1.0f) < 1e-4f, "the clip is added to the target with its name and length");
	Check(RetargetClip(*source, 0, *target, map) == clip, "doing it again replaces it");

	std::vector<glm::mat4> rest, now;
	PoseToWorld(*target, RestPose(*target), rest);
	auto sample = [&](float t, std::vector<glm::mat4>& world)
	{
		Pose p;
		SampleClip(*target, clip, t, p);
		PoseToWorld(*target, p, world);
	};
	auto delta = [&](const std::vector<glm::mat4>& w, int bone) { return Angle(Rotation(w[static_cast<size_t>(bone)]) * glm::inverse(Rotation(rest[static_cast<size_t>(bone)]))); };
	sample(1.0f, now);
	// the movement is the same: every paired bone has turned from its own rest pose by as much as the source's bone has from its own
	{
		std::vector<glm::mat4> srcRest, srcNow;
		PoseToWorld(*source, RestPose(*source), srcRest);
		Pose sp;
		SampleClip(*source, 0, 1.0f, sp);
		PoseToWorld(*source, sp, srcNow);
		float worst = 0.0f;
		for (const RetargetMap::Pair& pair : map.Bones)
		{
			const float s = Angle(Rotation(srcNow[static_cast<size_t>(pair.Source)]) * glm::inverse(Rotation(srcRest[static_cast<size_t>(pair.Source)])));
			worst = std::max(worst, std::fabs(glm::degrees(s - delta(now, pair.Target))));
		}
		Check(worst < 0.5f, "every paired bone has turned from its own rest pose as much as the source's bone did");
	}
	Check(glm::degrees(delta(now, tr.Left.UpperArm)) > 28.0f && glm::degrees(delta(now, tr.Left.UpperArm)) < 32.0f, "the left arm only follows the trunk (30 degrees)");
	Check(std::fabs(glm::degrees(delta(now, tr.Spine)) - 30.0f) < 1.0f && std::fabs(glm::degrees(delta(now, tr.Head)) - 30.0f) < 1.5f, "the trunk turns 30 degrees and takes the head along");
	Check(glm::degrees(delta(now, tr.Right.UpperArm)) > 100.0f, "the right arm swings 90 degrees on top of that");
	// the arm swings forward (+Z) whatever the source's splayed rest pose looked like
	const glm::vec3 shoulder(now[static_cast<size_t>(tr.Right.UpperArm)][3]), hand(now[static_cast<size_t>(tr.Right.Hand)][3]);
	Check(hand.z > shoulder.z + 0.2f, "the right hand ends up in front of the shoulder");
	// the target keeps its own proportions
	std::vector<glm::mat4> mid;
	sample(0.5f, mid);
	const auto length = [&](const std::vector<glm::mat4>& w, int a, int b) { return glm::distance(glm::vec3(w[static_cast<size_t>(a)][3]), glm::vec3(w[static_cast<size_t>(b)][3])); };
	Check(std::fabs(length(mid, tr.Right.UpperArm, tr.Right.ForeArm) - length(rest, tr.Right.UpperArm, tr.Right.ForeArm)) < 1e-3f, "the bone lengths are the target's, not the source's");
	Check(std::fabs(length(mid, tr.Right.ForeArm, tr.Right.Middle) - length(rest, tr.Right.ForeArm, tr.Right.Middle)) < 1e-3f, "an unpaired bone (a finger) rides with its parent at its rest offset");
	// the hips: travel scaled to the target
	const float travel = now[static_cast<size_t>(tr.Hips)][3].z - rest[static_cast<size_t>(tr.Hips)][3].z;
	Check(std::fabs(travel - 1.5f * 0.8f) < 0.03f, "the hips travel 1.5 m * 0.8 = 1.2 m (the target's size)");
	Check(std::fabs(now[static_cast<size_t>(tr.Hips)][3].y - rest[static_cast<size_t>(tr.Hips)][3].y) < 0.02f, "and stay at their own height");

	// ---- a source that faces the other way
	{
		auto back = Model::Load(dir + "/test_unreal_walk.fbx");
		auto fresh = Model::Load(dir + "/test_humanoid.fbx");
		const RetargetMap m = BuildRetargetMap(*back, *fresh);
		Check(m.Valid() && std::fabs(m.YawCorrection - 3.14159265f) < 1e-4f, "a source that faces -Z is turned around");
		const int c = RetargetClip(*back, 0, *fresh, m);
		Pose p0, p1;
		SampleClip(*fresh, c, 0.0f, p0);
		SampleClip(*fresh, c, 1.0f, p1);
		std::vector<glm::mat4> w0, w1;
		PoseToWorld(*fresh, p0, w0);
		PoseToWorld(*fresh, p1, w1);
		const int hips = FindHumanoidRig(*fresh).Hips;
		Check(std::fabs((w1[static_cast<size_t>(hips)][3].z - w0[static_cast<size_t>(hips)][3].z) - 1.2f * (1.0f / 1.0f)) < 0.05f, "the walk it did along its front (-Z) goes along the target's front (+Z)");
	}

	// ---- not a humanoid, or no map
	{
		auto blob = Model::Load(dir + "/test_blob.fbx");
		const RetargetMap m = BuildRetargetMap(*blob, *target);
		Check(!m.Valid() && RetargetClip(*blob, 0, *target, m) == -1, "a skeleton that cannot be paired is refused");
		Check(RetargetClip(*source, 99, *target, map) == -1, "so is a clip that does not exist");
		std::vector<std::string> names;
		auto again = Model::Load(dir + "/test_humanoid.fbx");
		Check(RetargetAnimations(*source, *again, BuildRetargetMap(*source, *again), &names) == 1 && names.size() == 1, "all the clips of a source can be retargeted at once");
	}

	// ---- a skeleton retargeted onto itself comes out as it went in
	{
		auto same = Model::Load(dir + "/test_retarget_src.fbx");
		auto copy = Model::Load(dir + "/test_retarget_src.fbx");
		const RetargetMap m = BuildRetargetMap(*same, *copy);
		Check(m.Valid() && std::fabs(m.Scale - 1.0f) < 1e-4f, "a skeleton paired with itself has scale 1");
		const int c = RetargetClip(*same, 0, *copy, m);
		float worst = 0.0f;
		for (float t : { 0.0f, 0.3f, 0.7f, 1.0f })
		{
			Pose a, b;
			SampleClip(*same, 0, t, a);
			SampleClip(*copy, c, t, b);
			std::vector<glm::mat4> wa, wb;
			PoseToWorld(*same, a, wa);
			PoseToWorld(*copy, b, wb);
			for (size_t n = 0; n < wa.size(); ++n) worst = std::max(worst, glm::distance(glm::vec3(wa[n][3]), glm::vec3(wb[n][3])));
		}
		Check(worst < 2e-3f, "every bone is where it was (within 2 mm)");
	}

	(void)sr;
	if (failures) return 1;
	std::puts("retarget tests passed");
	return 0;
}
