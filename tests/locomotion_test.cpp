// Pose blending, blend spaces, the locomotion blender (shared phase aligned on the feet, speed scaling, root motion
// removal, foot events) and footstep sounds, on the generated rig (tools/make_test_fbx.py: idle, a 1.2 m/s walk and a
// 3 m/s run whose feet really plant). No GL needed.
// Optional: GB_MIXAMO_DIR=<folder with the Mixamo pack> blends the real clips too.
#include "GB/Animation/BlendSpace.h"
#include "GB/Animation/Locomotion.h"
#include "GB/Animation/Pose.h"
#include "GB/Assets/Model.h"
#include "GB/Audio/FootstepSounds.h"
#include "GB/Log.h"
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
	bool Near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) < eps; }
	float WeightOf(const std::vector<GB::BlendSpace::Weight>& w, size_t index)
	{
		for (const auto& x : w) if (x.Index == index) return x.Value;
		return 0.0f;
	}
	float Sum(const std::vector<GB::BlendSpace::Weight>& w)
	{
		float s = 0; for (const auto& x : w) s += x.Value; return s;
	}
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;

	auto model = Model::Load(dir + "/test_rig.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test rig\n"); return 1; }
	model->AppendAnimations(dir + "/test_clip.fbx");
	model->AppendAnimations(dir + "/test_idle.fbx");
	Check(model->GetClips().size() == 3, "three clips: walk, run, idle");
	const int walkClip = model->FindClip("test_rig"), runClip = model->FindClip("test_clip"), idleClip = model->FindClip("test_idle");

	// ---- poses
	{
		const Pose rest = RestPose(*model);
		Check(rest.size() == model->GetNodes().size(), "a rest pose per node");
		Pose a = rest, b = rest;
		b[0].T = { 2.0f, 0.0f, 0.0f };
		b[0].R = glm::angleAxis(glm::radians(120.0f), glm::vec3(0, 1, 0));
		Pose mid;
		BlendPoses(a, b, 0.5f, mid);
		Check(Near(mid[0].T.x, 1.0f, 1e-4f), "positions blend linearly");
		Check(Near(glm::degrees(glm::angle(mid[0].R)), 60.0f, 0.1f), "rotations blend as a slerp");
		// the short way: q and -q are the same rotation, so blending them must not swing through the long way
		b[0].R = -a[0].R;
		BlendPoses(a, b, 0.5f, mid);
		Check(Near(glm::degrees(glm::angle(mid[0].R)), 0.0f, 0.1f), "rotations take the short way round");
		BlendPoses(a, b, 0.0f, a);
		Check(Near(a[0].T.x, rest[0].T.x, 1e-6f), "blending in place works");

		const std::vector<float> mask = MakeBoneMask(*model, "mixamorig:Spine");
		Check(mask.size() == rest.size() && mask[static_cast<size_t>(model->FindNode("mixamorig:Head"))] == 1.0f && mask[static_cast<size_t>(model->FindNode("mixamorig:LeftFoot"))] == 0.0f, "a bone mask covers a bone and its children");
		Pose shifted = rest;
		for (BonePose& p : shifted) p.T += glm::vec3(1.0f, 0.0f, 0.0f);
		Pose masked;
		BlendPosesMasked(rest, shifted, 1.0f, mask, masked);
		Check(Near(masked[static_cast<size_t>(model->FindNode("mixamorig:Head"))].T.x, rest[static_cast<size_t>(model->FindNode("mixamorig:Head"))].T.x + 1.0f, 1e-4f) &&
			Near(masked[static_cast<size_t>(model->FindNode("mixamorig:LeftFoot"))].T.x, rest[static_cast<size_t>(model->FindNode("mixamorig:LeftFoot"))].T.x, 1e-4f), "masked blends touch only the masked bones");
		Check(MakeBoneMask(*model, "nope").empty(), "unknown mask bone");

		Pose sampled;
		SampleClip(*model, walkClip, 0.5f, sampled);
		ModelInstance inst(*model);
		inst.Play(walkClip, false, 0.0f);
		inst.SetTime(0.5f);
		Check(Near(sampled[static_cast<size_t>(model->FindNode("mixamorig:Hips"))].T.z, inst.GetPose()[static_cast<size_t>(model->FindNode("mixamorig:Hips"))].T.z, 1e-5f), "SampleClip matches a playing instance");
		inst.SetPose(rest);
		Check(Near(inst.GetNodeWorld(model->FindNode("mixamorig:Hips"))[3].z, 0.0f, 1e-5f), "SetPose poses the instance");
		inst.Update(1.0f);
		Check(Near(inst.GetNodeWorld(model->FindNode("mixamorig:Hips"))[3].z, 0.0f, 1e-5f), "an externally posed instance does not play clips");
	}

	// ---- clip info: placed by what the clip really does
	const GaitInfo walkInfo = MakeGaitInfo(*model, walkClip), runInfo = MakeGaitInfo(*model, runClip), idleInfo = MakeGaitInfo(*model, idleClip);
	Check(Near(walkInfo.Velocity.y, 1.2f, 0.05f) && Near(runInfo.Velocity.y, 3.0f, 0.1f) && Near(idleInfo.Velocity.y, 0.0f, 0.01f), "clips know their root velocity");
	Check(walkInfo.HasGait() && runInfo.HasGait() && !idleInfo.HasGait(), "only the walk and the run have steps");
	Check(Near(walkInfo.LeftPlant, 0.25f, 0.03f) && Near(walkInfo.RightPlant, 0.75f, 0.03f), "the walk's feet land at 25 % and 75 %");

	// ---- blend space
	{
		BlendSpace line;   // a 1D tree: idle, walk, run along the forward speed
		line.Add(idleInfo, { 0, 0 }); line.Add(walkInfo, { 0, 1.2f }); line.Add(runInfo, { 0, 3.0f });
		auto w = line.Evaluate({ 0, 0 });
		Check(Near(WeightOf(w, 0), 1.0f, 1e-4f), "1D: exact at idle");
		w = line.Evaluate({ 0, 2.1f });
		Check(Near(WeightOf(w, 1), 0.5f, 1e-3f) && Near(WeightOf(w, 2), 0.5f, 1e-3f), "1D: halfway between walk and run");
		w = line.Evaluate({ 0, 9.0f });
		Check(w.size() == 1 && w[0].Index == 2, "1D: beyond the last clip it is that clip");
		w = line.Evaluate({ 0, -2.0f });
		Check(w.size() == 1 && w[0].Index == 0, "1D: before the first clip it is that clip");
		w = line.Evaluate({ 0.8f, 0.6f });
		Check(Near(Sum(w), 1.0f, 1e-4f), "1D: off-axis queries project onto the line");

		BlendSpace plane;  // idle at the origin and four clips around it
		plane.Add(idleInfo, { 0, 0 }); plane.Add(walkInfo, { 0, 2 }); plane.Add(runInfo, { 2, 0 }); plane.Add(walkInfo, { -2, 0 });
		for (size_t i = 0; i < plane.Size(); ++i)
		{
			w = plane.Evaluate(plane.Position(i));
			Check(Near(WeightOf(w, i), 1.0f, 1e-3f), "2D: exact at every clip");
		}
		w = plane.Evaluate({ 0.5f, 0.5f });
		Check(Near(Sum(w), 1.0f, 1e-4f) && Near(WeightOf(w, 0), 0.5f, 0.01f) && Near(WeightOf(w, 1), 0.25f, 0.01f) && Near(WeightOf(w, 2), 0.25f, 0.01f), "2D: barycentric inside a triangle");
		w = plane.Evaluate({ 0.0f, 5.0f });
		Check(w.size() == 1 && w[0].Index == 1, "2D: outside the clips it clamps to the nearest point");
		w = plane.Evaluate({ 0.0f, -3.0f });
		Check(Near(Sum(w), 1.0f, 1e-4f), "2D: behind the idle there is still a valid blend");
		Check(BlendSpace().Evaluate({ 1, 1 }).empty(), "an empty space has no weights");
	}

	// ---- the locomotion blender
	{
		LocomotionBlender loco(*model);
		Check(loco.AddClip("test_rig") && loco.AddClip("test_clip") && loco.AddClip("test_idle"), "clips are added");
		Check(!loco.AddClip("no such clip"), "an unknown clip is refused");
		LocomotionOptions instant; instant.VelocitySmoothing = 0.0f;
		LocomotionBlender direct(*model, instant);
		direct.AddClip("test_rig"); direct.AddClip("test_clip"); direct.AddClip("test_idle");
		const int hips = model->FindNode("mixamorig:Hips"), leftFoot = model->FindNode("mixamorig:LeftFoot"), rightFoot = model->FindNode("mixamorig:RightFoot");
		const float ground = 1.0f - 0.05f - 0.9f;   // rest height of the foot bones
		ModelInstance inst(*model);

		// standing still: idle, no steps
		direct.SetVelocity({ 0, 0 });
		for (int i = 0; i < 30; ++i) direct.Update(1.0f / 30.0f);
		Check(Near(direct.GaitWeight(), 0.0f, 1e-4f) && direct.Events().empty(), "standing: no gait, no steps");

		// walking at the walk's own speed: speed scale 1, two steps per second, root motion removed
		direct.SetVelocity({ 0, 1.2f });
		int lefts = 0, rights = 0;
		float maxHipsZ = 0.0f;
		for (int i = 0; i < 90; ++i)   // three seconds
		{
			direct.Update(1.0f / 30.0f);
			for (const FootEvent& e : direct.Events()) (e.Left ? lefts : rights)++;
			maxHipsZ = std::max(maxHipsZ, std::fabs(direct.GetPose()[static_cast<size_t>(hips)].T.z));
		}
		Check(Near(direct.SpeedScale(), 1.0f, 0.01f), "walking at the walk's own speed plays it at normal speed");
		Check(lefts >= 2 && lefts <= 4 && rights >= 2 && rights <= 4, "about three steps per foot in three seconds");
		Check(maxHipsZ < 0.02f, "the clip's own forward travel is taken out of the pose");

		// faster than the walk (with only idle and walk available): the clip speeds up to keep the feet on the ground
		LocomotionBlender walkOnly(*model, instant);
		walkOnly.AddClip("test_idle"); walkOnly.AddClip("test_rig");
		walkOnly.SetVelocity({ 0, 1.8f });
		walkOnly.Update(1.0f / 30.0f);
		Check(Near(walkOnly.SpeedScale(), 1.5f, 1e-3f), "1.5x the walk speed plays 1.5x faster");
		walkOnly.SetVelocity({ 0, 12.0f });
		walkOnly.Update(1.0f / 30.0f);
		Check(Near(walkOnly.SpeedScale(), 1.5f, 1e-3f), "far above the walk the factor is limited");
		walkOnly.SetVelocity({ 0, 1.0f });
		walkOnly.Update(1.0f / 30.0f);
		Check(Near(walkOnly.SpeedScale(), 1.0f, 0.02f), "below the walk the idle blend covers the speed: normal speed");
		// with the run available, 1.8 m/s is a blend that already travels at that speed: no scaling needed
		direct.SetVelocity({ 0, 1.8f });
		direct.Update(1.0f / 30.0f);
		Check(Near(direct.SpeedScale(), 1.0f, 0.02f), "a blend that covers the speed plays at normal speed");

		// between walk and run: blended, and the feet are in step (a left landing puts the left foot on the ground)
		direct.SetVelocity({ 0, 2.1f });
		bool checkedLanding = false;
		for (int i = 0; i < 300 && !checkedLanding; ++i)
		{
			direct.Update(1.0f / 120.0f);
			for (const FootEvent& e : direct.Events())
				if (e.Left)
				{
					direct.Apply(inst);
					const float y = inst.GetNodeWorld(leftFoot)[3].y;
					Check(y < ground + 0.03f, "the blended left foot is on the ground when the left foot lands");
					checkedLanding = true;
				}
		}
		Check(checkedLanding, "a left landing happens while blending walk and run");
		const auto parts = direct.Contributions();
		Check(parts.size() == 2 && parts[0].Weight > 0.3f && parts[1].Weight > 0.3f, "both clips contribute to the blend");

		// the right landing comes half a cycle after the left
		LocomotionBlender timing(*model, instant);
		timing.AddClip("test_rig");
		timing.SetVelocity({ 0, 1.2f });
		float leftAt = -1.0f, rightAt = -1.0f, t = 0.0f;
		for (int i = 0; i < 400 && (leftAt < 0 || rightAt < 0); ++i)
		{
			timing.Update(1.0f / 200.0f);
			t += 1.0f / 200.0f;
			for (const FootEvent& e : timing.Events()) { if (e.Left && leftAt < 0) leftAt = t; if (!e.Left && rightAt < 0) rightAt = t; }
		}
		Check(leftAt > 0 && rightAt > 0 && Near(std::fabs(rightAt - leftAt), 0.5f, 0.05f), "feet land half a cycle apart");

		// the smoothed velocity follows the command
		LocomotionBlender smooth(*model);
		smooth.AddClip("test_idle"); smooth.AddClip("test_rig");
		smooth.SetVelocity({ 0, 1.2f });
		smooth.Update(0.01f);
		Check(smooth.Velocity().y > 0.0f && smooth.Velocity().y < 1.2f, "the velocity eases in");
		for (int i = 0; i < 100; ++i) smooth.Update(0.05f);
		Check(Near(smooth.Velocity().y, 1.2f, 0.01f), "and arrives");
	}

	// ---- footstep sounds
	{
		AudioConfig cfg; cfg.Device = false;
		AudioEngine audio(cfg);
		FootstepSounds steps(audio);
		Surface asked = Surface::Grass;
		steps.SetSurfaceFunction([&](const glm::vec3& p) { asked = p.x > 0 ? Surface::Stone : Surface::Snow; return asked; });
		const VoiceId a = steps.Step({ 5, 0, 0 }, 3.0f);
		Check(a != NoVoice && steps.LastSurface() == Surface::Stone && audio.Mixer().ActiveVoices() == 1, "a step plays the surface's sound");
		steps.Step({ -5, 0, 0 }, 1.0f);
		Check(steps.LastSurface() == Surface::Snow && steps.Count() == 2, "the surface under each foot decides");
		const auto voices = audio.Mixer().Voices();
		Check(voices.size() == 2 && voices[0].Spatial && voices[0].Clip.rfind("step_stone", 0) == 0 && voices[1].Clip.rfind("step_snow", 0) == 0, "steps are spatial voices of the right presets");
		Check(steps.Step({ 0, 0, 0 }, 3.0f, 0.0f) != NoVoice, "a silent step still plays (volume 0)");
		Check(std::string(FootstepSounds::PresetFor(Surface::Wood)) == "step_wood", "preset names");
	}

	// ---- the real Mixamo pack, when available
	if (const char* pack = std::getenv("GB_MIXAMO_DIR"))
	{
		const std::string p = pack;
		if (std::filesystem::exists(p + "/idle.fbx"))
		{
			// the animation files carry the skeleton too, and no meshes (so no GL context is needed)
			auto bot = Model::Load(p + "/idle.fbx");
			if (bot)
			{
				for (const auto& e : std::filesystem::directory_iterator(p))
					if (e.path().extension() == ".fbx" && e.path().filename() != "X Bot.fbx" && e.path().filename() != "idle.fbx") bot->AppendAnimations(e.path().generic_string());
				LocomotionOptions instant; instant.VelocitySmoothing = 0.0f;
				LocomotionBlender loco(*bot, instant);
				int added = 0;
				for (const char* n : { "idle", "walking", "standard run", "left strafe walking", "right strafe walking", "left strafe", "right strafe" }) added += loco.AddClip(n) ? 1 : 0;
				Check(added == 7, "mixamo: all seven locomotion clips are added");
				loco.SetVelocity({ 0, 0 });
				loco.Update(0.1f);
				Check(loco.Contributions().size() == 1 && loco.Contributions()[0].Clip == "idle", "mixamo: standing is the idle");
				loco.SetVelocity({ 0, 3.0f });
				loco.Update(0.1f);
				Check(loco.GaitWeight() > 0.99f && loco.Contributions().size() == 2, "mixamo: 3 m/s blends walking and running");
				int steps = 0;
				for (int i = 0; i < 120; ++i) { loco.Update(1.0f / 60.0f); steps += static_cast<int>(loco.Events().size()); }
				Check(steps >= 4 && steps <= 8, "mixamo: two seconds of 3 m/s is a handful of steps");
			}
		}
	}

	if (failures) return 1;
	std::puts("locomotion tests passed");
	return 0;
}
