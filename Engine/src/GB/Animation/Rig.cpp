#include "GB/Animation/Rig.h"

namespace GB
{
	HumanoidRig FindHumanoidRig(const Model& model, const std::string& prefix)
	{
		auto find = [&](const std::string& name) { return model.FindNode(prefix + name); };
		HumanoidRig rig;
		rig.Hips = find("Hips");
		rig.Spine = find("Spine");
		rig.Spine2 = find("Spine2");
		if (rig.Spine2 < 0) rig.Spine2 = find("Spine1");
		if (rig.Spine2 < 0) rig.Spine2 = rig.Spine;
		rig.Neck = find("Neck");
		rig.Head = find("Head");
		auto arm = [&](const std::string& side)
		{
			HumanoidRig::Arm a;
			a.Shoulder = find(side + "Shoulder");
			a.UpperArm = find(side + "Arm");
			a.ForeArm = find(side + "ForeArm");
			a.Hand = find(side + "Hand");
			a.Middle = find(side + "HandMiddle1");
			return a;
		};
		auto leg = [&](const std::string& side)
		{
			HumanoidRig::Leg l;
			l.UpLeg = find(side + "UpLeg");
			l.Knee = find(side + "Leg");
			l.Foot = find(side + "Foot");
			l.Toe = find(side + "ToeBase");
			return l;
		};
		rig.Left = arm("Left");
		rig.Right = arm("Right");
		rig.LeftLeg = leg("Left");
		rig.RightLeg = leg("Right");
		return rig;
	}
}
