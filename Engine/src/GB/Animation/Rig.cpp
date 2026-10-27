#include "GB/Animation/Rig.h"
#include <cctype>
#include <iterator>
#include <vector>

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

	namespace
	{
		std::string Normalize(const std::string& name)
		{
			std::string n;
			for (char c : name)
				if (std::isalnum(static_cast<unsigned char>(c))) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			for (const char* prefix : { "mixamorig", "bip001", "bip01", "armature", "def" })   // skeleton prefixes ("mixamorig:Hips", "Bip001 Pelvis")
			{
				const std::string p(prefix);
				if (n.size() > p.size() && n.compare(0, p.size(), p) == 0) { n = n.substr(p.size()); break; }
			}
			return n;
		}

		// One row per convention: the normalized name of each bone, in the order of the slots below.
		struct Convention
		{
			const char* Name;
			const char* Bones[23];
		};
		// hips, spine, chest, neck, head | left: shoulder, upper arm, forearm, hand, middle finger | right: same | left leg: up, knee, foot, toe | right leg: same
		const Convention kConventions[] = {
			{ "mixamo", { "hips", "spine", "spine2", "neck", "head",
				"leftshoulder", "leftarm", "leftforearm", "lefthand", "lefthandmiddle1",
				"rightshoulder", "rightarm", "rightforearm", "righthand", "righthandmiddle1",
				"leftupleg", "leftleg", "leftfoot", "lefttoebase", "rightupleg", "rightleg", "rightfoot", "righttoebase" } },
			{ "unreal", { "pelvis", "spine01", "spine03", "neck01", "head",
				"claviclel", "upperarml", "lowerarml", "handl", "middle01l",
				"clavicler", "upperarmr", "lowerarmr", "handr", "middle01r",
				"thighl", "calfl", "footl", "balll", "thighr", "calfr", "footr", "ballr" } },
			{ "unity", { "hips", "spine", "chest", "neck", "head",
				"leftshoulder", "leftupperarm", "leftlowerarm", "lefthand", "leftmiddleproximal",
				"rightshoulder", "rightupperarm", "rightlowerarm", "righthand", "rightmiddleproximal",
				"leftupperleg", "leftlowerleg", "leftfoot", "lefttoes", "rightupperleg", "rightlowerleg", "rightfoot", "righttoes" } },
			{ "biped", { "pelvis", "spine", "spine2", "neck", "head",
				"lclavicle", "lupperarm", "lforearm", "lhand", "lfinger1",
				"rclavicle", "rupperarm", "rforearm", "rhand", "rfinger1",
				"lthigh", "lcalf", "lfoot", "ltoe0", "rthigh", "rcalf", "rfoot", "rtoe0" } },
		};
	}

	HumanoidRig FindHumanoidRigAuto(const Model& model, std::string* convention)
	{
		std::vector<std::string> normalized;
		for (const ModelNode& n : model.GetNodes()) normalized.push_back(Normalize(n.Name));
		auto find = [&](const char* bone) -> int
		{
			for (size_t i = 0; i < normalized.size(); ++i)
				if (normalized[i] == bone) return static_cast<int>(i);
			return -1;
		};
		int best = -1, bestScore = 0;
		std::vector<int> ids[std::size(kConventions)];
		for (size_t c = 0; c < std::size(kConventions); ++c)
		{
			int score = 0;
			for (const char* bone : kConventions[c].Bones)
			{
				const int id = find(bone);
				ids[c].push_back(id);
				score += id >= 0 ? 1 : 0;
			}
			if (score > bestScore) { bestScore = score; best = static_cast<int>(c); }
		}
		HumanoidRig rig;
		if (convention) convention->clear();
		if (best < 0 || bestScore < 4) return rig;   // not a humanoid we know
		if (convention) *convention = kConventions[best].Name;
		const std::vector<int>& v = ids[best];
		rig.Hips = v[0]; rig.Spine = v[1]; rig.Spine2 = v[2] >= 0 ? v[2] : v[1]; rig.Neck = v[3]; rig.Head = v[4];
		rig.Left = { v[5], v[6], v[7], v[8], v[9] };
		rig.Right = { v[10], v[11], v[12], v[13], v[14] };
		rig.LeftLeg = { v[15], v[16], v[17], v[18] };
		rig.RightLeg = { v[19], v[20], v[21], v[22] };
		return rig;
	}
}
