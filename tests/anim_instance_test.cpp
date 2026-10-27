// Locomotion sets with cross-faded overlays (AnimInstance): registering sets, switching while walking (no pop, the new set
// continues the same step, footsteps are not doubled), interrupting a change, and the Mixamo sets of the real packs
// (GB_MIXAMO_DIR and GB_FARMING_DIR, optional). CPU only.
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
	bool Near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) < eps; }

	GB::MotionState Walking(float forward)
	{
		GB::MotionState s;
		s.LocalVelocity = { 0.0f, forward };
		s.Velocity = { 0.0f, 0.0f, forward };
		s.Speed = forward;
		return s;
	}

	// The biggest movement of any bone between two poses of the instance, in meters.
	float MaxJump(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b)
	{
		float m = 0.0f;
		for (size_t i = 0; i < a.size(); ++i) m = std::max(m, glm::distance(a[i], b[i]));
		return m;
	}
	std::vector<glm::vec3> Positions(const GB::ModelInstance& inst, const GB::Model& model)
	{
		std::vector<glm::vec3> p;
		for (size_t i = 0; i < model.GetNodes().size(); ++i) p.push_back(glm::vec3(inst.GetNodeWorld(static_cast<int>(i))[3]));
		return p;
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

	ModelInstance inst(*model);
	AnimInstance anim(*model, inst);
	for (const char* n : { "test_idle", "test_rig", "test_clip" }) anim.Default().AddClip(n);

	// ---- sets
	Check(anim.AddSet({ "alt", { "test_idle", "test_clip", "no such clip" } }), "a set takes the clips the model has");
	Check(!anim.AddSet({ "empty", { "nope", "nothing" } }), "a set without any clip is refused");
	const auto names = anim.SetNames();
	Check(names.size() == 2 && std::find(names.begin(), names.end(), "alt") != names.end(), "the sets are listed");
	Check(!anim.SetOverlay("empty") && !anim.SetOverlay("nope"), "an unknown set changes nothing");
	Check(anim.Overlay() == AnimInstance::kDefaultSet && anim.SetOverlay("free") && !anim.Transitioning(), "\"free\" is the default set");

	// ---- switching while walking
	AudioConfig cfg; cfg.Device = false;
	AudioEngine audio(cfg);
	FootstepSounds steps(audio);
	anim.SetFootsteps(&steps);
	const float dt = 1.0f / 60.0f;
	const MotionState walk = Walking(1.2f);
	for (int i = 0; i < 120; ++i) anim.Update(walk, dt, glm::mat4(1.0f));
	const int stepsBefore = steps.Count();
	Check(stepsBefore >= 3, "walking plays footsteps");
	const float phaseBefore = anim.Active().Phase();

	Check(anim.SetOverlay("alt", 0.5f) && anim.Transitioning() && anim.Overlay() == "alt", "switching starts a cross-fade");
	Check(Near(anim.Active().Phase(), phaseBefore, 0.01f), "the new set continues the same step");
	std::vector<glm::vec3> last = Positions(inst, *model);
	float worst = 0.0f;
	bool progressed = true;
	float previous = 0.0f;
	for (int i = 0; i < 60; ++i)
	{
		anim.Update(walk, dt, glm::mat4(1.0f));
		const std::vector<glm::vec3> now = Positions(inst, *model);
		worst = std::max(worst, MaxJump(last, now));
		last = now;
		if (anim.TransitionProgress() < previous - 1e-5f) progressed = false;
		previous = anim.TransitionProgress();
	}
	Check(!anim.Transitioning() && progressed, "the cross-fade ends and only moves forward");
	Check(worst < 0.12f, "no bone jumps while changing sets");
	Check(steps.Count() - stepsBefore <= 3 + 2 && steps.Count() - stepsBefore >= 1, "footsteps are not doubled during the change");

	// ---- interrupting a change: the new fade starts from the pose on screen
	anim.SetOverlay("free", 0.5f);
	for (int i = 0; i < 15; ++i) anim.Update(walk, dt, glm::mat4(1.0f));
	last = Positions(inst, *model);
	Check(anim.SetOverlay("alt", 0.4f) && anim.Transitioning(), "a change can be interrupted");
	worst = 0.0f;
	for (int i = 0; i < 40; ++i)
	{
		anim.Update(walk, dt, glm::mat4(1.0f));
		const std::vector<glm::vec3> now = Positions(inst, *model);
		worst = std::max(worst, MaxJump(last, now));
		last = now;
	}
	Check(worst < 0.12f && !anim.Transitioning() && anim.Overlay() == "alt", "no pop when interrupting, and it ends on the last set asked for");

	// ---- an instant change
	anim.SetOverlay("free", 0.0f);
	Check(!anim.Transitioning() && anim.Overlay() == AnimInstance::kDefaultSet, "no fade: the change is immediate");

	// ---- the animator wrapper keeps working, and exposes the sets
	{
		ModelInstance inst2(*model);
		CharacterAnimator animator(*model, inst2);
		for (const char* n : { "test_idle", "test_rig" }) animator.Locomotion().AddClip(n);
		Check(animator.Anim().AddSet({ "carry", { "test_idle", "test_rig" } }), "the animator takes sets too");
		animator.Update(Walking(1.2f), dt, glm::mat4(1.0f));
		Check(animator.Locomotion().ClipCount() == 2, "Locomotion() is the active blender");
	}

	// ---- the real packs, when available
	const char* mixamo = std::getenv("GB_MIXAMO_DIR");
	const char* farming = std::getenv("GB_FARMING_DIR");
	if (mixamo && farming && std::filesystem::exists(std::string(mixamo) + "/idle.fbx") && std::filesystem::exists(std::string(farming) + "/box idle.fbx"))
	{
		auto bot = Model::Load(std::string(mixamo) + "/idle.fbx");
		if (bot)
		{
			for (const std::string& folder : { std::string(mixamo), std::string(farming) })
				for (const auto& e : std::filesystem::directory_iterator(folder))
					if (e.path().extension() == ".fbx" && e.path().filename() != "X Bot.fbx" && e.path().filename() != "idle.fbx") bot->AppendAnimations(e.path().generic_string());
			ModelInstance realInst(*bot);
			CharacterAnimator real(*bot, realInst);
			Check(real.AddDefaultLocomotionClips() == 7, "mixamo: the free set has its seven clips");
			const auto added = real.AddMixamoSets();
			Check(added.size() == 3, "farming: box, holding and wheelbarrow sets are found");
			float worstReal = 0.0f;
			std::vector<glm::vec3> before = Positions(realInst, *bot);
			for (const char* set : { "box", "wheelbarrow", "holding", "free" })
			{
				for (int i = 0; i < 40; ++i) real.Update(Walking(1.2f), dt, glm::mat4(1.0f));
				before = Positions(realInst, *bot);
				Check(real.Anim().SetOverlay(set, 0.4f), "switching to a farming set works");
				for (int i = 0; i < 40; ++i)
				{
					real.Update(Walking(1.2f), dt, glm::mat4(1.0f));
					const auto now = Positions(realInst, *bot);
					worstReal = std::max(worstReal, MaxJump(before, now));
					before = now;
				}
			}
			Check(worstReal < 0.25f, "farming sets cross-fade without a pop");
		}
	}

	if (failures) return 1;
	std::puts("anim instance tests passed");
	return 0;
}
