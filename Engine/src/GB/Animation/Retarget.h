#pragma once
#include "GB/Animation/Pose.h"
#include "GB/Animation/Rig.h"
#include <string>
#include <vector>

namespace GB
{
	// Which bone of a source skeleton drives which bone of a target one. The humanoid bones are paired through the rig finder (so a Mixamo
	// character can take Unreal's animations and the other way round, whatever the naming); the other bones (fingers, props) are paired when
	// their names are identical.
	struct RetargetMap
	{
		struct Pair { int Source = -1, Target = -1; };
		std::vector<Pair> Bones;
		std::vector<std::string> UnmatchedTarget;   // target bones with no source bone: they keep their rest pose
		int Humanoid = 0;                           // how many of the pairs are humanoid bones
		int SourceHips = -1, TargetHips = -1;
		float Scale = 1.0f;                         // the target's hips height over the source's: the hips' travel is scaled by it
		float YawCorrection = 0.0f;                 // radians about +Y: the source faces -Z while the target faces +Z (or the other way)
		std::string SourceConvention, TargetConvention;
		bool Valid() const { return SourceHips >= 0 && TargetHips >= 0 && Humanoid >= 12; }
	};

	RetargetMap BuildRetargetMap(const Model& source, const Model& target);
	std::string DescribeRetargetMap(const Model& source, const Model& target, const RetargetMap& map);

	// Applies source poses to the target skeleton: every mapped bone takes the world-space rotation it has moved from its own rest pose by in the
	// source (the movement is kept, the proportions and the rest pose are the target's), the hips' travel is scaled to the target's size, and the
	// bones nobody drives keep their rest pose.
	class Retargeter
	{
	public:
		Retargeter(const Model& source, const Model& target, const RetargetMap& map);
		void Apply(const Pose& sourcePose, Pose& out) const;
		bool Valid() const { return m_Map.Valid(); }

	private:
		const Model& m_Source;
		const Model& m_Target;
		RetargetMap m_Map;
		Pose m_SourceRest, m_TargetRest;
		std::vector<glm::quat> m_SourceRestRotation, m_TargetRestRotation;
		glm::vec3 m_SourceHipsRest{ 0.0f }, m_TargetHipsRest{ 0.0f };
		std::vector<int> m_SourceOf;   // per target node: the source node that drives it, -1 for none
	};

	// Bakes a clip of `source` for `target` (30 fps by default); returns the new clip's index in the target or -1.
	int RetargetClip(const Model& source, int clip, Model& target, const RetargetMap& map, float fps = 30.0f);
	// Every clip of the source. Returns how many were added.
	int RetargetAnimations(const Model& source, Model& target, const RetargetMap& map, std::vector<std::string>* names = nullptr);
}
