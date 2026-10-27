#include "GB/Animation/Character.h"
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/Montage.h"
#include "GB/Animation/Pose.h"
#include "GB/Animation/Socket.h"
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	namespace
	{
		std::string Lower(std::string s)
		{
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}
		bool Has(const std::string& s, const char* word) { return s.find(word) != std::string::npos; }

		// The clip that plays a role: among the ones whose name says so and none of the words that mark a variant (carrying, crouching, sitting,
		// pushing...), the one with the shortest name ("Idle_Loop" before "Idle_Talking_Loop").
		std::string FindClip(const Model& model, std::initializer_list<const char*> all, std::initializer_list<const char*> none = {})
		{
			std::string best;
			for (const AnimationClip& c : model.GetClips())
			{
				const std::string n = Lower(c.Name);
				bool ok = true;
				for (const char* w : all) ok = ok && Has(n, w);
				for (const char* w : none) ok = ok && !Has(n, w);
				for (const char* w : { "box", "holding", "wheelbarrow", "arc", "kneel", "sitting", "crouch", "formal", "torch", "talking", "pistol", "sword", "spell", "swim", "driving", "dance", "death", "hit_" }) ok = ok && !Has(n, w);
				if (ok && (best.empty() || c.Name.size() < best.size())) best = c.Name;
			}
			return best;
		}
		std::string FindClipAny(const Model& model, std::initializer_list<std::initializer_list<const char*>> options, std::initializer_list<const char*> none = {})
		{
			for (const auto& all : options)
			{
				const std::string c = FindClip(model, all, none);
				if (!c.empty()) return c;
			}
			return "";
		}
	}

	bool CharacterProfile::CanLocomote() const
	{
		return RoleClip("idle") && (RoleClip("walk") || RoleClip("run"));
	}

	const std::string* CharacterProfile::RoleClip(const std::string& role) const
	{
		for (const Role& r : Roles)
			if (r.Name == role) return &r.Clip;
		return nullptr;
	}

	CharacterProfile AnalyzeCharacter(const Model& model)
	{
		CharacterProfile p;
		p.Rig = FindHumanoidRigAuto(model, &p.Convention);
		p.ClipCount = static_cast<int>(model.GetClips().size());
		const auto& nodes = model.GetNodes();
		auto nameOf = [&](int bone) { return bone >= 0 ? nodes[static_cast<size_t>(bone)].Name : std::string(); };

		if (p.Convention.empty())
		{
			p.Warnings.push_back("not a humanoid skeleton the engine recognises (Mixamo, Unreal, Unity or Biped naming)");
		}
		else
		{
			// the analysis and the locomotion work on this skeleton's own bone names
			if (p.Rig.Hips >= 0) p.Analysis.RootBone = nameOf(p.Rig.Hips);
			if (p.Rig.LeftLeg.Foot >= 0) p.Analysis.LeftFoot = nameOf(p.Rig.LeftLeg.Foot);
			if (p.Rig.RightLeg.Foot >= 0) p.Analysis.RightFoot = nameOf(p.Rig.RightLeg.Foot);
			if (p.Rig.LeftLeg.UpLeg >= 0) p.Analysis.LeftHip = nameOf(p.Rig.LeftLeg.UpLeg);
			if (p.Rig.RightLeg.UpLeg >= 0) p.Analysis.RightHip = nameOf(p.Rig.RightLeg.UpLeg);

			const struct { int Bone; const char* Name; } required[] = {
				{ p.Rig.Hips, "hips" }, { p.Rig.Spine, "spine" }, { p.Rig.Head, "head" },
				{ p.Rig.Left.UpperArm, "left upper arm" }, { p.Rig.Left.ForeArm, "left forearm" }, { p.Rig.Left.Hand, "left hand" },
				{ p.Rig.Right.UpperArm, "right upper arm" }, { p.Rig.Right.ForeArm, "right forearm" }, { p.Rig.Right.Hand, "right hand" },
				{ p.Rig.LeftLeg.UpLeg, "left upper leg" }, { p.Rig.LeftLeg.Knee, "left knee" }, { p.Rig.LeftLeg.Foot, "left foot" },
				{ p.Rig.RightLeg.UpLeg, "right upper leg" }, { p.Rig.RightLeg.Knee, "right knee" }, { p.Rig.RightLeg.Foot, "right foot" },
			};
			for (const auto& r : required)
				if (r.Bone < 0) p.MissingBones.push_back(r.Name);
			if (!p.MissingBones.empty())
			{
				std::string list;
				for (const std::string& m : p.MissingBones) list += (list.empty() ? "" : ", ") + m;
				p.Warnings.push_back("missing bones: " + list + " (IK, grips and sockets need them)");
			}

			// size and facing, from the rest pose
			std::vector<glm::mat4> rest;
			PoseToWorld(model, RestPose(model), rest);
			auto pos = [&](int bone) { return glm::vec3(rest[static_cast<size_t>(bone)][3]); };
			if (p.Rig.Head >= 0 && p.Rig.LeftLeg.Foot >= 0)
				p.Height = pos(p.Rig.Head).y - std::min(pos(p.Rig.LeftLeg.Foot).y, p.Rig.RightLeg.Foot >= 0 ? pos(p.Rig.RightLeg.Foot).y : 1e9f) + 0.2f;   // + the head above its bone, the foot below its ankle
			const float meshHeight = model.GetBoundsMax().y - model.GetBoundsMin().y;
			if (meshHeight > 0.1f) p.Height = meshHeight;
			if (p.Height > 0.0f && (p.Height < 0.5f || p.Height > 3.0f))
			{
				char b[120];
				std::snprintf(b, sizeof b, "the character is %.2f m tall: a humanoid is expected between 0.5 and 3 m (is the file in centimeters?)", static_cast<double>(p.Height));
				p.Warnings.push_back(b);
			}
			if (p.Rig.LeftLeg.Foot >= 0 && p.Rig.LeftLeg.Toe >= 0)
			{
				p.FacesForward = pos(p.Rig.LeftLeg.Toe).z >= pos(p.Rig.LeftLeg.Foot).z - 1e-4f;
				if (!p.FacesForward) p.Warnings.push_back("the character faces -Z: the engine expects +Z");
			}
			if (p.Rig.Hips >= 0 && p.Rig.Left.Hand >= 0 && p.Rig.Right.Hand >= 0 && pos(p.Rig.Left.Hand).x < pos(p.Rig.Right.Hand).x)
				p.Warnings.push_back("the left hand is on the -X side: the engine expects the character's left at +X");

			p.Sockets = StandardSockets(model, p.Rig).Names();
		}

		// clips by role
		auto role = [&](const char* name, const std::string& clip) { if (!clip.empty()) p.Roles.push_back({ name, clip }); };
		role("idle", FindClip(model, { "idle" }));
		role("walk", FindClip(model, { "walk" }, { "strafe", "turn" }));
		role("run", FindClipAny(model, { { "run" }, { "jog" }, { "sprint" } }, { "strafe", "turn" }));
		role("strafe_left", FindClip(model, { "left", "strafe" }));
		role("strafe_right", FindClip(model, { "right", "strafe" }));
		role("jump", FindClipAny(model, { { "jump" } }, { "land" }));
		role("turn_left", FindClip(model, { "left", "turn" }));
		role("turn_right", FindClip(model, { "right", "turn" }));
		if (p.ClipCount == 0) p.Warnings.push_back("no animation clips: put the animation files next to the character");
		else if (!p.RoleClip("idle")) p.Warnings.push_back("no idle clip found");
		else if (!p.RoleClip("walk") && !p.RoleClip("run")) p.Warnings.push_back("no walk or run clip found");

		// what the farming packs add
		for (const LocomotionSetDesc& d : MixamoSets())
		{
			int found = 0;
			for (const std::string& c : d.Clips) found += model.FindClip(c) >= 0 ? 1 : 0;
			if (found >= 2) p.Sets.push_back(d.Name);
		}
		for (const MontageDesc& m : MixamoMontages())
			if (model.FindClip(m.Clip) >= 0) p.Montages.push_back(m.Name);
		for (const MontageDesc& m : UniversalMontages())
			if (model.FindClip(m.Clip) >= 0) p.Montages.push_back(m.Name);
		return p;
	}

	std::string FormatCharacterProfile(const CharacterProfile& p)
	{
		std::string s;
		char b[200];
		s += "skeleton: " + (p.Convention.empty() ? std::string("not recognised") : p.Convention) + "\n";
		if (!p.Convention.empty())
		{
			std::snprintf(b, sizeof b, "size: %.2f m, faces %s; %zu sockets", static_cast<double>(p.Height), p.FacesForward ? "+Z" : "-Z", p.Sockets.size());
			s += std::string(b) + "\n";
			s += std::string("rig: ") + (p.Rig.HasArms() ? "arms " : "no arms ") + (p.Rig.HasLegs() ? "legs " : "no legs ") + (p.MissingBones.empty() ? "(complete)" : "(" + std::to_string(p.MissingBones.size()) + " bones missing)") + "\n";
		}
		s += std::to_string(p.ClipCount) + " clips; roles:";
		for (const CharacterProfile::Role& r : p.Roles) s += " " + r.Name + "=\"" + r.Clip + "\"";
		if (p.Roles.empty()) s += " none";
		s += "\n";
		s += "locomotion: " + std::string(p.CanLocomote() ? "yes" : "no");
		s += "; sets:";
		for (const std::string& n : p.Sets) s += " " + n;
		s += p.Sets.empty() ? " none" : "";
		s += "; montages: " + std::to_string(p.Montages.size()) + "\n";
		for (const std::string& w : p.Warnings) s += "warning: " + w + "\n";
		if (p.Warnings.empty()) s += "ready\n";
		return s;
	}

	std::string CharacterProfileToJson(const CharacterProfile& p)
	{
		using Json = nlohmann::json;
		Json j;
		j["convention"] = p.Convention;
		j["height"] = p.Height;
		j["facesForward"] = p.FacesForward;
		j["humanoid"] = p.Humanoid();
		j["canLocomote"] = p.CanLocomote();
		j["clipCount"] = p.ClipCount;
		j["bones"] = { { "root", p.Analysis.RootBone }, { "leftFoot", p.Analysis.LeftFoot }, { "rightFoot", p.Analysis.RightFoot } };
		j["missingBones"] = p.MissingBones;
		j["sockets"] = p.Sockets;
		Json roles = Json::object();
		for (const CharacterProfile::Role& r : p.Roles) roles[r.Name] = r.Clip;
		j["roles"] = roles;
		j["sets"] = p.Sets;
		j["montages"] = p.Montages;
		j["warnings"] = p.Warnings;
		return j.dump(2) + "\n";
	}
}
