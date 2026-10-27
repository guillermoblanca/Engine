#include "GB/Animation/Retarget.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	namespace
	{
		glm::quat WorldRotation(const glm::mat4& m)
		{
			return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])))));
		}

		void AddPair(std::vector<int>& targetToSource, int source, int target)
		{
			if (source >= 0 && target >= 0 && targetToSource[static_cast<size_t>(target)] < 0) targetToSource[static_cast<size_t>(target)] = source;
		}
	}

	RetargetMap BuildRetargetMap(const Model& source, const Model& target)
	{
		RetargetMap map;
		const HumanoidRig s = FindHumanoidRigAuto(source, &map.SourceConvention);
		const HumanoidRig t = FindHumanoidRigAuto(target, &map.TargetConvention);
		std::vector<int> targetToSource(target.GetNodes().size(), -1);
		auto arm = [&](const HumanoidRig::Arm& a, const HumanoidRig::Arm& b)
		{
			AddPair(targetToSource, a.Shoulder, b.Shoulder); AddPair(targetToSource, a.UpperArm, b.UpperArm);
			AddPair(targetToSource, a.ForeArm, b.ForeArm); AddPair(targetToSource, a.Hand, b.Hand);
		};
		auto leg = [&](const HumanoidRig::Leg& a, const HumanoidRig::Leg& b)
		{
			AddPair(targetToSource, a.UpLeg, b.UpLeg); AddPair(targetToSource, a.Knee, b.Knee);
			AddPair(targetToSource, a.Foot, b.Foot); AddPair(targetToSource, a.Toe, b.Toe);
		};
		AddPair(targetToSource, s.Hips, t.Hips);
		AddPair(targetToSource, s.Spine, t.Spine);
		if (s.Spine2 != s.Spine || t.Spine2 != t.Spine) AddPair(targetToSource, s.Spine2, t.Spine2);
		AddPair(targetToSource, s.Neck, t.Neck);
		AddPair(targetToSource, s.Head, t.Head);
		arm(s.Left, t.Left); arm(s.Right, t.Right);
		leg(s.LeftLeg, t.LeftLeg); leg(s.RightLeg, t.RightLeg);
		for (int v : targetToSource) map.Humanoid += v >= 0 ? 1 : 0;
		auto chain = [&](int sb, int sc, int tb, int tc)
		{
			if (sb >= 0 && sc >= 0 && tb >= 0 && tc >= 0) map.Chains.push_back({ tb, tc, sb, sc });
		};
		auto armChain = [&](const HumanoidRig::Arm& a, const HumanoidRig::Arm& b)
		{
			chain(a.Shoulder, a.UpperArm, b.Shoulder, b.UpperArm); chain(a.UpperArm, a.ForeArm, b.UpperArm, b.ForeArm);
			chain(a.ForeArm, a.Hand, b.ForeArm, b.Hand); chain(a.Hand, a.Middle, b.Hand, b.Middle);
		};
		auto legChain = [&](const HumanoidRig::Leg& a, const HumanoidRig::Leg& b)
		{
			chain(a.UpLeg, a.Knee, b.UpLeg, b.Knee); chain(a.Knee, a.Foot, b.Knee, b.Foot); chain(a.Foot, a.Toe, b.Foot, b.Toe);
		};
		chain(s.Hips, s.Spine, t.Hips, t.Spine);
		chain(s.Spine, s.Spine2 != s.Spine ? s.Spine2 : s.Neck, t.Spine, t.Spine2 != t.Spine ? t.Spine2 : t.Neck);
		if (s.Spine2 != s.Spine && t.Spine2 != t.Spine) chain(s.Spine2, s.Neck, t.Spine2, t.Neck);
		chain(s.Neck, s.Head, t.Neck, t.Head);
		armChain(s.Left, t.Left); armChain(s.Right, t.Right);
		legChain(s.LeftLeg, t.LeftLeg); legChain(s.RightLeg, t.RightLeg);

		// the other bones: the same name in both skeletons
		for (size_t n = 0; n < target.GetNodes().size(); ++n)
			if (targetToSource[n] < 0) targetToSource[n] = source.FindNode(target.GetNodes()[n].Name);
		for (size_t n = 0; n < targetToSource.size(); ++n)
		{
			if (targetToSource[n] >= 0) map.Bones.push_back({ targetToSource[n], static_cast<int>(n) });
			else map.UnmatchedTarget.push_back(target.GetNodes()[n].Name);
		}
		map.SourceHips = s.Hips;
		map.TargetHips = t.Hips;
		if (s.Hips >= 0 && t.Hips >= 0)
		{
			std::vector<glm::mat4> sw, tw;
			PoseToWorld(source, RestPose(source), sw);
			PoseToWorld(target, RestPose(target), tw);
			const float sh = sw[static_cast<size_t>(s.Hips)][3].y, th = tw[static_cast<size_t>(t.Hips)][3].y;
			if (sh > 1e-3f && th > 1e-3f) map.Scale = th / sh;
			// facing: toes in front of the ankle means +Z
			auto faces = [](const std::vector<glm::mat4>& w, const HumanoidRig& r) { return r.LeftLeg.Toe < 0 || r.LeftLeg.Foot < 0 || w[static_cast<size_t>(r.LeftLeg.Toe)][3].z >= w[static_cast<size_t>(r.LeftLeg.Foot)][3].z - 1e-4f; };
			if (faces(sw, s) != faces(tw, t)) map.YawCorrection = 3.14159265f;
		}
		return map;
	}

	std::string DescribeRetargetMap(const Model& source, const Model& target, const RetargetMap& map)
	{
		std::string s = "retarget " + (map.SourceConvention.empty() ? std::string("?") : map.SourceConvention) + " -> " + (map.TargetConvention.empty() ? std::string("?") : map.TargetConvention) + ": ";
		s += std::to_string(map.Bones.size()) + " bones paired (" + std::to_string(map.Humanoid) + " humanoid), scale " + std::to_string(map.Scale).substr(0, 4);
		if (map.YawCorrection != 0.0f) s += ", turned 180 degrees (the source faces the other way)";
		if (!map.Valid()) s += " [not usable: the humanoid bones could not be paired]";
		s += "\n";
		if (!map.UnmatchedTarget.empty())
		{
			s += std::to_string(map.UnmatchedTarget.size()) + " target bones keep their rest pose:";
			for (size_t i = 0; i < map.UnmatchedTarget.size() && i < 8; ++i) s += " " + map.UnmatchedTarget[i];
			if (map.UnmatchedTarget.size() > 8) s += " ...";
			s += "\n";
		}
		(void)source; (void)target;
		return s;
	}

	Retargeter::Retargeter(const Model& source, const Model& target, const RetargetMap& map)
		: m_Source(source), m_Target(target), m_Map(map), m_SourceRest(RestPose(source)), m_TargetRest(RestPose(target)), m_SourceOf(target.GetNodes().size(), -1)
	{
		std::vector<glm::mat4> sw, tw;
		PoseToWorld(source, m_SourceRest, sw);
		PoseToWorld(target, m_TargetRest, tw);
		for (const glm::mat4& m : sw) m_SourceRestRotation.push_back(WorldRotation(m));
		for (const glm::mat4& m : tw) m_TargetRestRotation.push_back(WorldRotation(m));
		// the target's rest pose with its limbs pointing where the source's do
		const glm::quat yawRest = glm::angleAxis(map.YawCorrection, glm::vec3(0.0f, 1.0f, 0.0f));
		for (const RetargetMap::Chain& c : map.Chains)
		{
			const glm::vec3 dt = glm::vec3(tw[static_cast<size_t>(c.TargetChild)][3]) - glm::vec3(tw[static_cast<size_t>(c.TargetBone)][3]);
			const glm::vec3 ds = yawRest * (glm::vec3(sw[static_cast<size_t>(c.SourceChild)][3]) - glm::vec3(sw[static_cast<size_t>(c.SourceBone)][3]));
			if (glm::length(dt) < 1e-5f || glm::length(ds) < 1e-5f) continue;
			const glm::vec3 a = glm::normalize(dt), b = glm::normalize(ds);
			const float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
			glm::quat turn(1.0f, 0.0f, 0.0f, 0.0f);
			if (d < 0.9999f)
			{
				glm::vec3 axis = glm::cross(a, b);
				axis = glm::length(axis) > 1e-6f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
				turn = glm::angleAxis(std::acos(d), axis);
			}
			m_TargetRestRotation[static_cast<size_t>(c.TargetBone)] = glm::normalize(turn * m_TargetRestRotation[static_cast<size_t>(c.TargetBone)]);
		}
		for (const RetargetMap::Pair& p : map.Bones) m_SourceOf[static_cast<size_t>(p.Target)] = p.Source;
		if (map.SourceHips >= 0) m_SourceHipsRest = glm::vec3(sw[static_cast<size_t>(map.SourceHips)][3]);
		if (map.TargetHips >= 0) m_TargetHipsRest = glm::vec3(tw[static_cast<size_t>(map.TargetHips)][3]);
	}

	void Retargeter::Apply(const Pose& sourcePose, Pose& out) const
	{
		std::vector<glm::mat4> sw;
		PoseToWorld(m_Source, sourcePose, sw);
		out = m_TargetRest;
		const glm::quat yaw = glm::angleAxis(m_Map.YawCorrection, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::quat yawInverse = glm::inverse(yaw);
		const auto& nodes = m_Target.GetNodes();
		std::vector<glm::mat4> tw(nodes.size());
		for (int n : m_Target.GetNodeOrder())
		{
			BonePose& b = out[static_cast<size_t>(n)];
			const int parent = nodes[static_cast<size_t>(n)].Parent;
			const glm::mat4 parentWorld = parent >= 0 ? tw[static_cast<size_t>(parent)] : glm::mat4(1.0f);
			const int s = m_SourceOf[static_cast<size_t>(n)];
			if (s >= 0)
			{
				// how far the source bone has turned from its rest pose, in the world; the target bone turns the same from its own rest pose
				const glm::quat delta = yaw * (WorldRotation(sw[static_cast<size_t>(s)]) * glm::inverse(m_SourceRestRotation[static_cast<size_t>(s)])) * yawInverse;
				const glm::quat wanted = glm::normalize(delta * m_TargetRestRotation[static_cast<size_t>(n)]);
				b.R = glm::normalize(glm::inverse(WorldRotation(parentWorld)) * wanted);
				if (n == m_Map.TargetHips)
				{
					const glm::vec3 travel = yaw * (glm::vec3(sw[static_cast<size_t>(s)][3]) - m_SourceHipsRest) * m_Map.Scale;
					b.T = glm::vec3(glm::inverse(parentWorld) * glm::vec4(m_TargetHipsRest + travel, 1.0f));
				}
			}
			tw[static_cast<size_t>(n)] = parentWorld * glm::translate(glm::mat4(1.0f), b.T) * glm::mat4_cast(glm::normalize(b.R)) * glm::scale(glm::mat4(1.0f), b.S);
		}
	}

	int RetargetClip(const Model& source, int clip, Model& target, const RetargetMap& map, float fps)
	{
		if (!map.Valid() || clip < 0 || static_cast<size_t>(clip) >= source.GetClips().size()) return -1;
		const AnimationClip& src = source.GetClips()[static_cast<size_t>(clip)];
		const Retargeter retargeter(source, target, map);
		const int frames = std::max(2, static_cast<int>(std::ceil(src.Duration * fps)) + 1);
		// a rotation anywhere above changes the world rotation of everything below: key every paired bone (the world-space method needs them all)
		std::vector<int> keyed;
		for (const RetargetMap::Pair& p : map.Bones) keyed.push_back(p.Target);

		AnimationClip out;
		out.Name = src.Name;
		out.Duration = src.Duration;
		std::vector<AnimationChannel> rotations(keyed.size()), translation(1);
		for (size_t i = 0; i < keyed.size(); ++i)
		{
			rotations[i].Node = keyed[i];
			rotations[i].Target = AnimationTarget::Rotation;
		}
		translation[0].Node = map.TargetHips;
		translation[0].Target = AnimationTarget::Translation;
		Pose sp, tp;
		for (int f = 0; f < frames; ++f)
		{
			const float t = std::min(static_cast<float>(f) / fps, src.Duration);
			SampleClip(source, clip, t, sp);
			retargeter.Apply(sp, tp);
			for (size_t i = 0; i < keyed.size(); ++i)
			{
				const glm::quat q = tp[static_cast<size_t>(keyed[i])].R;
				rotations[i].Times.push_back(t);
				for (float v : { q.x, q.y, q.z, q.w }) rotations[i].Values.push_back(v);
			}
			translation[0].Times.push_back(t);
			const glm::vec3 p = tp[static_cast<size_t>(map.TargetHips)].T;
			for (float v : { p.x, p.y, p.z }) translation[0].Values.push_back(v);
		}
		for (AnimationChannel& c : rotations) out.Channels.push_back(std::move(c));
		out.Channels.push_back(std::move(translation[0]));
		return target.AddClip(std::move(out));
	}

	int RetargetAnimations(const Model& source, Model& target, const RetargetMap& map, std::vector<std::string>* names)
	{
		int added = 0;
		for (size_t c = 0; c < source.GetClips().size(); ++c)
			if (RetargetClip(source, static_cast<int>(c), target, map) >= 0)
			{
				++added;
				if (names) names->push_back(source.GetClips()[c].Name);
			}
		return added;
	}
}
