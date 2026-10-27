#pragma once
#include "GB/Assets/Model.h"
#include <string>

namespace GB
{
	// The bones of a humanoid skeleton that sockets, grips and IK work on, found by name (Mixamo naming by default).
	// A missing bone is -1: everything that uses the rig skips what it cannot find.
	struct HumanoidRig
	{
		struct Arm { int Shoulder = -1, UpperArm = -1, ForeArm = -1, Hand = -1, Middle = -1; };   // Middle: the base of the middle finger (the palm's far end)
		struct Leg { int UpLeg = -1, Knee = -1, Foot = -1, Toe = -1; };
		int Hips = -1, Spine = -1, Spine2 = -1, Neck = -1, Head = -1;
		Arm Left, Right;
		Leg LeftLeg, RightLeg;

		bool HasArms() const { return Left.UpperArm >= 0 && Left.ForeArm >= 0 && Left.Hand >= 0 && Right.UpperArm >= 0 && Right.ForeArm >= 0 && Right.Hand >= 0; }
		bool HasLegs() const { return LeftLeg.UpLeg >= 0 && LeftLeg.Knee >= 0 && LeftLeg.Foot >= 0 && RightLeg.UpLeg >= 0 && RightLeg.Knee >= 0 && RightLeg.Foot >= 0; }
	};

	// `prefix` goes before every bone name ("mixamorig:" for Mixamo).
	HumanoidRig FindHumanoidRig(const Model& model, const std::string& prefix = "mixamorig:");
}
