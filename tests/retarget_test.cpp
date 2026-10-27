// Retargeting: animations from another skeleton (other bone names, proportions, rest pose and facing) applied to a character. The map between
// the two skeletons, the movement kept in world space, the target's own proportions, the hips scaled to its size, the facing corrected,
// bones nobody drives left at rest, and a skeleton retargeted onto itself unchanged. The generated skeletons; CPU only.
#include "GB/Animation/Retarget.h"
#include "GB/Assets/Model.h"
#include "GB/Log.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
	// the limbs point where the source's do, at rest and all through the clip (the target keeps its own proportions, the rest pose is aligned)
	{
		const HumanoidRig srcRig = FindHumanoidRigAuto(*source);
		const struct { int sa, sb, ta, tb; const char* name; } limbs[] = {
			{ srcRig.Right.UpperArm, srcRig.Right.ForeArm, tr.Right.UpperArm, tr.Right.ForeArm, "right upper arm" },
			{ srcRig.Right.ForeArm, srcRig.Right.Hand, tr.Right.ForeArm, tr.Right.Hand, "right forearm" },
			{ srcRig.Left.UpperArm, srcRig.Left.ForeArm, tr.Left.UpperArm, tr.Left.ForeArm, "left upper arm" },
			{ srcRig.LeftLeg.UpLeg, srcRig.LeftLeg.Knee, tr.LeftLeg.UpLeg, tr.LeftLeg.Knee, "left thigh" },
			{ srcRig.RightLeg.Knee, srcRig.RightLeg.Foot, tr.RightLeg.Knee, tr.RightLeg.Foot, "right calf" },
		};
		float worst = 0.0f;
		for (float t : { 0.0f, 0.5f, 1.0f })
		{
			Pose sp;
			SampleClip(*source, 0, t, sp);
			std::vector<glm::mat4> sw, tw;
			PoseToWorld(*source, sp, sw);
			sample(t, tw);
			for (const auto& l : limbs)
			{
				const glm::vec3 ds = glm::normalize(glm::vec3(sw[static_cast<size_t>(l.sb)][3]) - glm::vec3(sw[static_cast<size_t>(l.sa)][3]));
				const glm::vec3 dt = glm::normalize(glm::vec3(tw[static_cast<size_t>(l.tb)][3]) - glm::vec3(tw[static_cast<size_t>(l.ta)][3]));
				worst = std::max(worst, glm::degrees(std::acos(glm::clamp(glm::dot(ds, dt), -1.0f, 1.0f))));
			}
		}
		Check(worst < 1.5f, "arms and legs point where the source's do (within 1.5 degrees), at rest and while it moves");
	}
	Check(map.Chains.size() >= 16, "the chains used to align the rest pose are found");
	Check(std::fabs(glm::degrees(delta(now, tr.Spine)) - 30.0f) < 4.0f && std::fabs(glm::degrees(delta(now, tr.Head)) - 30.0f) < 5.0f, "the trunk turns about 30 degrees and takes the head along");
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

	// ---- the real Mixamo pack onto itself (optional): the same skeleton, so a clip must come out as it went in
	if (const char* mixamo = std::getenv("GB_MIXAMO_DIR"))
	{
		auto a = Model::Load(std::string(mixamo) + "/walking.fbx");
		auto b = Model::Load(std::string(mixamo) + "/walking.fbx");
		if (a && b)
		{
			const RetargetMap m = BuildRetargetMap(*a, *b);
			Check(m.Valid() && m.SourceConvention == "mixamo" && m.Humanoid >= 21 && std::fabs(m.Scale - 1.0f) < 1e-4f && m.YawCorrection == 0.0f, "Mixamo onto Mixamo: all the humanoid bones, same size, same facing");
			const int c = RetargetClip(*a, 0, *b, m);
			float worst = 0.0f;
			for (float t : { 0.2f, 0.6f, 1.0f })
			{
				Pose pa, pb;
				SampleClip(*a, 0, t, pa);
				SampleClip(*b, c, t, pb);
				std::vector<glm::mat4> wa, wb;
				PoseToWorld(*a, pa, wa);
				PoseToWorld(*b, pb, wb);
				for (const RetargetMap::Pair& pair : m.Bones)
					worst = std::max(worst, glm::distance(glm::vec3(wa[static_cast<size_t>(pair.Source)][3]), glm::vec3(wb[static_cast<size_t>(pair.Target)][3])));
			}
			Check(worst < 0.01f, "the real walking clip retargeted onto its own skeleton keeps every paired bone within 1 cm");
		}
	}

	(void)sr;
	if (failures) return 1;
	std::puts("retarget tests passed");
	return 0;
}
