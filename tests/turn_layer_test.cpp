// Turning on the spot (TurnLayer) and the injured locomotion set. The generated rig has no turning clip, so what can be
// checked without the Mixamo packs is the refusals and the idle behaviour; with GB_MIXAMO_DIR (and GB_INJURED_DIR,
// GB_FARMING_DIR) the real turn clips are used: the side comes from the clip, the hips' spin is taken out, and the
// layer only plays when the character stands still and turns. CPU only.
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Animation/ClipAnalysis.h"
#include "GB/Animation/TurnLayer.h"
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

	GB::MotionState Turning(float degreesPerSecond, float speed = 0.0f)
	{
		GB::MotionState s;
		s.YawRate = degreesPerSecond;
		s.Speed = speed;
		s.LocalVelocity = { 0.0f, speed };
		return s;
	}

	// Adds the clips of every FBX in a folder (except the character and the idle that is the model) to the model.
	void AppendFolder(GB::Model& model, const std::string& folder)
	{
		for (const auto& entry : std::filesystem::directory_iterator(folder))
			if (entry.path().extension() == ".fbx" && entry.path().filename() != "X Bot.fbx" && entry.path().filename() != "idle.fbx")
				model.AppendAnimations(entry.path().string());
	}

	// How far the rotation of `bone` is from `reference`, in degrees.
	float AngleBetween(const glm::quat& a, const glm::quat& b)
	{
		return glm::degrees(2.0f * std::acos(std::min(std::fabs(glm::dot(a, b)), 1.0f)));
	}
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;

	// ---- without a turning clip
	{
		auto model = Model::Load(dir + "/test_rig.fbx");
		if (!model) { std::fprintf(stderr, "FAIL: test rig\n"); return 1; }
		model->AppendAnimations(dir + "/test_clip.fbx");
		TurnLayer layer(*model);
		Check(!layer.AddClip("", "no such clip"), "an unknown clip is refused");
		Check(!layer.AddClip("", "test_clip"), "a clip that does not turn the root is refused");
		for (int i = 0; i < 60; ++i) layer.Update(Turning(180.0f), 1.0f / 60.0f, "");
		Check(!layer.Active() && layer.Side() == 0, "without clips the layer never plays");
	}

	// ---- the real packs
	const char* mixamo = std::getenv("GB_MIXAMO_DIR");
	if (mixamo && std::filesystem::exists(std::string(mixamo) + "/idle.fbx"))
	{
		auto bot = Model::Load(std::string(mixamo) + "/idle.fbx");   // the skeleton and a clip, no mesh: no GL context needed
		AppendFolder(*bot, mixamo);
		TurnLayer layer(*bot);
		Check(layer.AddClip("", "left turn 90") && layer.AddClip("", "right turn 90"), "the Mixamo turn clips register");
		const int left = bot->FindClip("left turn 90"), right = bot->FindClip("right turn 90");
		const ClipStats l = AnalyzeClip(*bot, left), r = AnalyzeClip(*bot, right);
		Check(l.RootYawDegrees > 60.0f && r.RootYawDegrees < -60.0f, "the left turn turns the root positively, the right turn negatively");

		const float dt = 1.0f / 60.0f;
		// a character that is walking, or not turning, does not play the turn
		for (int i = 0; i < 30; ++i) layer.Update(Turning(180.0f, 1.5f), dt, "");
		Check(!layer.Active(), "no turn clip while the character walks");
		for (int i = 0; i < 30; ++i) layer.Update(Turning(5.0f), dt, "");
		Check(!layer.Active(), "no turn clip for a tiny turn");

		// standing and turning left, then right
		for (int i = 0; i < 20; ++i) layer.Update(Turning(110.0f), dt, "");
		Check(layer.Active() && layer.Side() == 1, "turning left on the spot plays the left turn");
		Pose pose = RestPose(*bot);
		const Pose rest = pose;
		layer.Apply(pose);
		const int hips = bot->FindNode("mixamorig:Hips");
		Check(hips >= 0 && AngleBetween(pose[static_cast<size_t>(hips)].R, rest[static_cast<size_t>(hips)].R) < 70.0f, "the hips are not left at the pose of a half turn");
		for (int i = 0; i < 60; ++i) layer.Update(Turning(110.0f), dt, "");
		Pose end = RestPose(*bot);
		layer.Apply(end);
		// the raw clip ends ~90 degrees around from where it began; with the spin taken out the hips stay near the start
		Pose rawStart, rawEnd;
		SampleClip(*bot, left, 0.0f, rawStart);
		SampleClip(*bot, left, bot->GetClips()[static_cast<size_t>(left)].Duration, rawEnd);
		const float rawSpin = AngleBetween(rawStart[static_cast<size_t>(hips)].R, rawEnd[static_cast<size_t>(hips)].R);
		Check(rawSpin > 60.0f, "(the raw clip does spin the hips)");
		Check(AngleBetween(end[static_cast<size_t>(hips)].R, rawStart[static_cast<size_t>(hips)].R) < 0.35f * rawSpin, "the spin of the hips is taken out of the layer");

		for (int i = 0; i < 90; ++i) layer.Update(Turning(0.0f), dt, "");
		Check(!layer.Active(), "the turn fades out when the character stops turning");
		for (int i = 0; i < 20; ++i) layer.Update(Turning(-110.0f), dt, "");
		Check(layer.Active() && layer.Side() == -1, "turning right plays the right turn");

		// a full character: the injured set and the per-set turns
		const char* injured = std::getenv("GB_INJURED_DIR");
		if (injured && std::filesystem::exists(std::string(injured) + "/injured walk.fbx"))
		{
			AppendFolder(*bot, injured);
			if (const char* farming = std::getenv("GB_FARMING_DIR")) AppendFolder(*bot, farming);
			ModelInstance inst(*bot);
			CharacterAnimator animator(*bot, inst);
			Check(animator.AddDefaultLocomotionClips() >= 5, "the free locomotion clips are found");
			const auto sets = animator.AddMixamoSets();
			Check(std::find(sets.begin(), sets.end(), "injured") != sets.end(), "the injured set is registered");
			animator.AddMixamoActions();
			AnimInstance& anim = animator.Anim();
			Check(anim.Turn().HasClips("injured") && anim.Turn().HasClips(""), "the injured set and the free set have their own turn clips");
			Check(anim.SetOverlay("injured", 0.0f), "the injured set can be activated");
			// walking backwards is a real gait of the injured set: the blend follows the velocity
			MotionState back;
			back.LocalVelocity = { 0.0f, -1.2f };
			back.Velocity = { 0.0f, 0.0f, -1.2f };
			back.Speed = 1.2f;
			for (int i = 0; i < 60; ++i) anim.Update(back, 1.0f / 60.0f, glm::mat4(1.0f));
			Check(anim.Active().ClipCount() >= 4, "the injured set has idle, walk, run and the backwards gaits");
		}
	}
	else
		std::fprintf(stderr, "note: GB_MIXAMO_DIR not set, the pack-backed checks were skipped\n");

	return failures;
}
