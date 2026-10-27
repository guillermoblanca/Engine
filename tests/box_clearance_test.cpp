// Carrying a box: the box must not go through the character's legs or torso, and must not jump or drift while the
// character walks or runs for a long time. Needs the Mixamo packs (GB_MIXAMO_DIR and GB_FARMING_DIR); without them
// the test only says it was skipped. CPU only.
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Assets/Model.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	void AppendFolder(GB::Model& model, const std::string& folder)
	{
		for (const auto& entry : std::filesystem::directory_iterator(folder))
			if (entry.path().extension() == ".fbx" && entry.path().filename() != "X Bot.fbx" && entry.path().filename() != "idle.fbx")
				model.AppendAnimations(entry.path().string());
	}

	// Distance from a point to an oriented box (0 inside).
	float DistanceToBox(const glm::vec3& p, const glm::mat4& boxWorld, const glm::vec3& half)
	{
		const glm::vec3 local = glm::vec3(glm::inverse(boxWorld) * glm::vec4(p, 1.0f));
		const glm::vec3 q = glm::max(glm::abs(local) - half, glm::vec3(0.0f));
		return glm::length(q);
	}

	struct Segment { const char* A; const char* B; float Radius; };

	struct Report
	{
		float WorstPenetration = 0.0f;     // meters a leg or the torso reaches into the box
		float WorstJump = 0.0f;            // biggest frame-to-frame move of the box in the character's frame (m)
		float WorstTurn = 0.0f;            // biggest frame-to-frame turn of the hips around the vertical (degrees)
		float NetTurn = 0.0f;              // how far the hips have turned from the first frame at the end (degrees)
		glm::vec3 Min{ 1e9f }, Max{ -1e9f };   // where the box sits relative to the hips
	};
}

int main()
{
	using namespace GB;
	Log::Init();
	const char* mixamo = std::getenv("GB_MIXAMO_DIR");
	const char* farming = std::getenv("GB_FARMING_DIR");
	if (!mixamo || !farming || !std::filesystem::exists(std::string(mixamo) + "/idle.fbx"))
	{
		std::fprintf(stderr, "note: GB_MIXAMO_DIR / GB_FARMING_DIR not set, the box clearance check was skipped\n");
		return 0;
	}

	auto model = Model::Load(std::string(mixamo) + "/idle.fbx");   // skeleton and a clip, no mesh: no GL context needed
	AppendFolder(*model, mixamo);
	AppendFolder(*model, farming);
	ModelInstance inst(*model);
	CharacterAnimator animator(*model, inst);
	animator.AddDefaultLocomotionClips();
	animator.AddMixamoSets();
	animator.AddMixamoActions();
	AnimInstance& anim = animator.Anim();

	const float dt = 1.0f / 60.0f;
	MotionState still;
	anim.SetOverlay("box", 0.0f);
	for (int i = 0; i < 60; ++i) anim.Update(still, dt, glm::mat4(1.0f));

	// the same box the Editor makes: between the palms of the box idle, following the carry socket
	const SocketSet& sockets = anim.Sockets();
	Check(sockets.Find("carry") && sockets.Find("hand_l") && sockets.Find("hand_r"), "the rig has the carry and hand sockets");
	const glm::mat4 carry = sockets.Matrix(inst, "carry");
	const glm::vec3 l = glm::vec3(glm::inverse(carry) * sockets.Matrix(inst, "hand_l")[3]), r = glm::vec3(glm::inverse(carry) * sockets.Matrix(inst, "hand_r")[3]);
	glm::vec3 x = l - r;
	const float width = glm::length(x);
	x /= std::max(width, 1e-4f);
	glm::vec3 z = glm::normalize(glm::cross(x, glm::vec3(0, 1, 0)));
	const glm::vec3 y = glm::cross(z, x);
	glm::vec3 half(0.5f * (width + 0.04f), 0.16f, 0.18f);
	auto attach = [&](float depth, float shift)
	{
		Prop prop;
		prop.Name = "box";
		prop.Socket = "carry";
		prop.Offset = glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4((l + r) * 0.5f + z * shift, 1));
		anim.SnapGrips(prop);
		half.z = 0.5f * depth;
		return anim.Attach(prop);
	};
	Check(attach(0.26f, 0.11f), "the box attaches");   // as PlayerMode::AttachProp

	const Segment body[] = {
		{ "mixamorig:LeftUpLeg", "mixamorig:LeftLeg", 0.085f }, { "mixamorig:LeftLeg", "mixamorig:LeftFoot", 0.065f },
		{ "mixamorig:RightUpLeg", "mixamorig:RightLeg", 0.085f }, { "mixamorig:RightLeg", "mixamorig:RightFoot", 0.065f },
		{ "mixamorig:Hips", "mixamorig:Spine2", 0.12f },
	};
	const int hips = model->FindNode("mixamorig:Hips");

	auto simulate = [&](float speed, float seconds, const char* label)
	{
		Report rep;
		MotionState s;
		s.LocalVelocity = { 0.0f, speed };
		s.Velocity = { 0.0f, 0.0f, speed };
		s.Speed = speed;
		glm::vec3 previous(0.0f);
		bool havePrevious = false;
		float previousYaw = 0.0f, firstYaw = 0.0f;
		for (int i = 0; i < static_cast<int>(seconds / dt); ++i)
		{
			anim.Update(s, dt, glm::mat4(1.0f));
			glm::mat4 box;
			if (!anim.PropMatrix("box", box)) continue;
			if (i < 90) continue;   // let the speed settle
			const glm::mat4 toLocal = glm::inverse(inst.GetNodeWorld(hips));
			const glm::vec3 centre = glm::vec3(toLocal * box[3]);
			rep.Min = glm::min(rep.Min, centre);
			rep.Max = glm::max(rep.Max, centre);
			const glm::vec3 fwd = glm::vec3(inst.GetNodeWorld(hips) * glm::vec4(0, 0, 1, 0));
			const float yaw = glm::degrees(std::atan2(fwd.x, fwd.z));
			if (havePrevious)
			{
				float d = std::fabs(yaw - previousYaw);
				d = d > 180.0f ? 360.0f - d : d;
				rep.WorstTurn = std::max(rep.WorstTurn, d);
			}
			else firstYaw = yaw;
			previousYaw = yaw;
			rep.NetTurn = std::fabs(yaw - firstYaw);
			if (havePrevious) rep.WorstJump = std::max(rep.WorstJump, glm::distance(centre, previous));
			previous = centre;
			havePrevious = true;
			for (const Segment& seg : body)
			{
				const int a = model->FindNode(seg.A), b = model->FindNode(seg.B);
				if (a < 0 || b < 0) continue;
				const glm::vec3 pa = glm::vec3(inst.GetNodeWorld(a)[3]), pb = glm::vec3(inst.GetNodeWorld(b)[3]);
				for (int k = 0; k <= 8; ++k)
				{
					const glm::vec3 pt = glm::mix(pa, pb, static_cast<float>(k) / 8.0f);
					const float pen = seg.Radius - DistanceToBox(pt, box, half);
						if (pen > rep.WorstPenetration + 0.02f)
						{
							const glm::vec3 lp = glm::vec3(glm::inverse(box) * glm::vec4(pt, 1.0f));
							std::fprintf(stderr, "debug: %s->%s k=%d pen %.3f local (%.2f %.2f %.2f) pt (%.2f %.2f %.2f)\n", seg.A, seg.B, k, static_cast<double>(pen),
								static_cast<double>(lp.x), static_cast<double>(lp.y), static_cast<double>(lp.z),
								static_cast<double>(pt.x), static_cast<double>(pt.y), static_cast<double>(pt.z));
						}
					rep.WorstPenetration = std::max(rep.WorstPenetration, pen);
				}
			}
		}
		std::fprintf(stderr, "info: %s %.1f m/s: penetration %.3f m, box jump %.3f m/frame, hips turn %.1f deg/frame (net %.1f), box range y %.2f..%.2f z %.2f..%.2f\n",
			label, static_cast<double>(speed), static_cast<double>(rep.WorstPenetration), static_cast<double>(rep.WorstJump),
			static_cast<double>(rep.WorstTurn), static_cast<double>(rep.NetTurn), static_cast<double>(rep.Min.y), static_cast<double>(rep.Max.y), static_cast<double>(rep.Min.z), static_cast<double>(rep.Max.z));
		return rep;
	};

	if (std::getenv("GB_BOX_SWEEP"))
	{
		for (float depth : { 0.36f, 0.30f, 0.26f, 0.22f })
			for (float shift : { 0.0f, 0.04f, 0.08f, 0.12f })
			{
				attach(depth, shift);
				std::fprintf(stderr, "sweep depth %.2f shift %.2f -> ", static_cast<double>(depth), static_cast<double>(shift));
				simulate(1.7f, 6.0f, "fast");
				std::fprintf(stderr, "          run -> ");
				simulate(4.6f, 6.0f, "run");
			}
		return 0;
	}
	const float cap = anim.Active().MaxSpeed() * 1.1f;   // the controller never goes faster than this while the box set is on
	Check(cap > 1.0f && cap < 1.6f, "the box set tops out at its walk speed");
	const Report standing = simulate(0.0f, 4.0f, "standing");
	const Report slow = simulate(0.6f, 20.0f, "slow");
	const Report walking = simulate(cap, 40.0f, "at the cap");
	Check(standing.WorstPenetration < 0.02f, "standing: the box is clear of the body");
	Check(slow.WorstPenetration < 0.02f && walking.WorstPenetration < 0.02f, "walking with the box: no leg or torso goes through it");
	Check(slow.WorstJump < 0.05f && walking.WorstJump < 0.05f, "the box does not jump between frames");
	Check(slow.WorstTurn < 4.0f && walking.WorstTurn < 4.0f, "the hips do not snap round at the end of a cycle (the walk with a box is an arc that does not loop by itself)");
	Check(walking.NetTurn < 20.0f, "and they do not drift round over time");
	return failures;
}
