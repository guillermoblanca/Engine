// Montages, slots and the air layer on top of the locomotion (AnimInstance): a one-shot action blends in, fires its notifies,
// blends out and finishes; a new montage in the same slot interrupts without a pop; an upper-body slot leaves the legs to the
// locomotion; the jump pose follows the controller's flight. The generated rig always; the real Mixamo and farming packs when
// GB_MIXAMO_DIR / GB_FARMING_DIR are set. CPU only.
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Assets/Model.h"
#include "GB/Game/CharacterController.h"
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
	GB::MotionState Walking(float forward)
	{
		GB::MotionState s;
		s.LocalVelocity = { 0.0f, forward };
		s.Velocity = { 0.0f, 0.0f, forward };
		s.Speed = forward;
		return s;
	}
	std::vector<glm::vec3> Positions(const GB::ModelInstance& inst, const GB::Model& model)
	{
		std::vector<glm::vec3> p;
		for (size_t i = 0; i < model.GetNodes().size(); ++i) p.push_back(glm::vec3(inst.GetNodeWorld(static_cast<int>(i))[3]));
		return p;
	}
	float MaxJump(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b)
	{
		float m = 0.0f;
		for (size_t i = 0; i < a.size(); ++i) m = std::max(m, glm::distance(a[i], b[i]));
		return m;
	}
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;
	const float dt = 1.0f / 60.0f;

	auto model = Model::Load(dir + "/test_rig.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test rig\n"); return 1; }
	model->AppendAnimations(dir + "/test_clip.fbx");
	model->AppendAnimations(dir + "/test_idle.fbx");

	// two characters that move the same way: one gets the montages, the other is the control
	ModelInstance instA(*model), instB(*model);
	AnimInstance a(*model, instA), b(*model, instB);
	for (const char* n : { "test_idle", "test_rig", "test_clip" }) { a.Default().AddClip(n); b.Default().AddClip(n); }
	const MotionState idle = Walking(0.0f);
	const int foot = model->FindNode("mixamorig:LeftFoot");
	auto runBoth = [&](int frames)
	{
		for (int i = 0; i < frames; ++i) { a.Update(idle, dt, glm::mat4(1.0f)); b.Update(idle, dt, glm::mat4(1.0f)); }
	};
	runBoth(30);

	// ---- registration
	MontagePlayer& m = a.Montages();
	Check(!m.AddSlot("Nope", "no such bone") && m.AddSlot("UpperBody", "mixamorig:Spine"), "a slot needs an existing bone");
	MontageDesc wave;
	wave.Name = "wave"; wave.Clip = "test_clip"; wave.BlendIn = 0.2f; wave.BlendOut = 0.2f;
	wave.Notifies = { { 0.5f, "half" } };
	Check(m.Add(wave) && !m.Has("nothing"), "a montage with a known clip is added");
	MontageDesc bad = wave; bad.Name = "bad"; bad.Clip = "no such clip";
	Check(!m.Add(bad), "an unknown clip is refused");
	bad = wave; bad.Name = "bad"; bad.Slot = "Missing";
	Check(!m.Add(bad), "an unknown slot is refused");
	Check(!m.Play("bad"), "playing an unknown montage fails");
	MontageDesc upper = wave; upper.Name = "upper"; upper.Slot = "UpperBody";
	Check(m.Add(upper), "an upper-body montage is added");
	const float duration = m.Duration("wave");
	Check(duration > 0.2f, "the montage has the clip's length");

	// ---- full-body montage: blend in, notify, blend out, finished
	Check(m.Play("wave") && m.IsPlaying("wave"), "play starts the montage");
	int notifies = 0, finished = 0, interrupted = 0;
	float maxWeight = 0.0f;
	float worstDelta = 0.0f;
	bool footMoved = false;
	std::vector<glm::vec3> last = Positions(instA, *model);
	for (int i = 0; i < static_cast<int>((duration + 0.8f) / dt); ++i)
	{
		runBoth(1);
		for (const MontageEvent& e : m.Events())
		{
			notifies += e.What == MontageEvent::Kind::Notify && e.Name == "half";
			finished += e.What == MontageEvent::Kind::Finished;
			interrupted += e.What == MontageEvent::Kind::Interrupted;
		}
		maxWeight = std::max(maxWeight, m.FullBodyWeight());
		const auto now = Positions(instA, *model);
		worstDelta = std::max(worstDelta, MaxJump(last, now));
		last = now;
		footMoved = footMoved || glm::distance(glm::vec3(instA.GetNodeWorld(foot)[3]), glm::vec3(instB.GetNodeWorld(foot)[3])) > 0.02f;
	}
	Check(notifies == 1 && finished == 1 && interrupted == 0, "one notify and one finished event, nothing interrupted");
	Check(maxWeight > 0.99f && !m.Busy() && m.FullBodyWeight() == 0.0f, "it reaches full weight and leaves nothing behind");
	Check(footMoved, "a full-body montage moves the legs away from the locomotion");
	Check(worstDelta < 0.2f, "no bone jumps while it plays");
	Check(MaxJump(Positions(instA, *model), Positions(instB, *model)) < 1e-4f, "afterwards the pose is the locomotion's again");

	// ---- interrupting and replacing
	m.Play("wave");
	runBoth(10);   // still before its blend out
	last = Positions(instA, *model);
	Check(m.Play("wave"), "the same montage can restart");
	float worstRestart = 0.0f;
	for (int i = 0; i < 40; ++i)
	{
		runBoth(1);
		for (const MontageEvent& e : m.Events()) interrupted += e.What == MontageEvent::Kind::Interrupted;
		const auto now = Positions(instA, *model);
		worstRestart = std::max(worstRestart, MaxJump(last, now));
		last = now;
	}
	Check(interrupted == 1, "restarting interrupts the previous run once");
	Check(worstRestart < 0.25f, "replacing a montage in the middle does not pop");
	m.StopSlot("FullBody", 0.1f);
	runBoth(30);
	Check(!m.Busy(), "stopping a slot ends its montages");

	// ---- the upper-body slot leaves the legs to the locomotion
	Check(m.Play("upper"), "an upper-body montage plays");
	float feetDiff = 0.0f, bodyDiff = 0.0f, upperWeight = 0.0f, fullWeight = 0.0f;
	for (int i = 0; i < 60; ++i)
	{
		runBoth(1);
		upperWeight = std::max(upperWeight, m.SlotWeight("UpperBody"));
		fullWeight = std::max(fullWeight, m.SlotWeight("FullBody"));
		feetDiff = std::max(feetDiff, glm::distance(glm::vec3(instA.GetNodeWorld(foot)[3]), glm::vec3(instB.GetNodeWorld(foot)[3])));
		bodyDiff = std::max(bodyDiff, MaxJump(Positions(instA, *model), Positions(instB, *model)));
	}
	Check(feetDiff < 1e-3f, "the legs stay with the locomotion");
	Check(upperWeight > 0.5f && fullWeight == 0.0f, "slots report their own weight");

	// ---- the real packs
	const char* mixamo = std::getenv("GB_MIXAMO_DIR");
	const char* farming = std::getenv("GB_FARMING_DIR");
	if (mixamo && farming)
	{
		auto bot = Model::Load(std::string(mixamo) + "/idle.fbx");
		if (bot)
		{
			for (const std::string& folder : { std::string(mixamo), std::string(farming) })
				for (const auto& e : std::filesystem::directory_iterator(folder))
					if (e.path().extension() == ".fbx" && e.path().filename() != "X Bot.fbx" && e.path().filename() != "idle.fbx") bot->AppendAnimations(e.path().generic_string());
			ModelInstance realInst(*bot);
			CharacterAnimator real(*bot, realInst);
			real.AddDefaultLocomotionClips();
			Check(real.AddMixamoActions() == 11, "farming: the eleven actions are found");
			Check(real.Anim().Air().HasClip(), "the jump clip has a flight phase");

			// an action: the hips go where the clip puts them (kneeling) and come back, without moving off the spot
			const int hips = bot->FindNode("mixamorig:Hips");
			for (int i = 0; i < 30; ++i) real.Update(Walking(0.0f), dt, glm::mat4(1.0f));
			const float standing = realInst.GetNodeWorld(hips)[3].y;
			Check(real.Anim().Montages().Play("pick_fruit"), "pick fruit plays");
			float lowest = standing, drift = 0.0f;
			const int frames = static_cast<int>((real.Anim().Montages().Duration("pick_fruit") + 0.6f) / dt);
			int fin = 0;
			std::vector<glm::vec3> before = Positions(realInst, *bot);
			float worstReal = 0.0f;
			for (int i = 0; i < frames; ++i)
			{
				real.Update(Walking(0.0f), dt, glm::mat4(1.0f));
				for (const MontageEvent& e : real.Anim().Montages().Events()) fin += e.What == MontageEvent::Kind::Finished;
				const glm::vec4 h = realInst.GetNodeWorld(hips)[3];
				lowest = std::min(lowest, h.y);
				drift = std::max(drift, std::hypot(h.x, h.z));
				const auto now = Positions(realInst, *bot);
				worstReal = std::max(worstReal, MaxJump(before, now));
				before = now;
			}
			Check(fin == 1 && !real.Anim().Montages().Busy(), "the action finishes");
			Check(drift < 0.35f, "the action stays on the spot (the controller owns the position)");
			Check(worstReal < 0.3f, "the action blends in and out without a pop");
			Check(std::fabs(realInst.GetNodeWorld(hips)[3].y - standing) < 0.03f, "the character stands up again afterwards");

			// a jump driven by a real controller
			CharacterController c;
			const CharacterWorld world;
			c.SetPosition(world, 0, 0, 0.0f);
			MotionTracker tracker;
			CharacterInput in;
			in.Jump = true;
			bool airSeen = false;
			before = Positions(realInst, *bot);
			float worstAir = 0.0f;
			for (int i = 0; i < 150; ++i)
			{
				c.Step(in, world, dt);
				in.Jump = i < 3;
				const MotionState s = tracker.Update(c, dt);
				real.Update(s, dt, glm::mat4(1.0f));
				airSeen = airSeen || real.Anim().Air().InAir();
				const auto now = Positions(realInst, *bot);
				if (i > 0) worstAir = std::max(worstAir, MaxJump(before, now));
				before = now;
			}
			Check(airSeen && !real.Anim().Air().Active(), "the jump pose follows the flight and fades out after landing");
			Check(worstAir < 0.35f, "the jump pose does not pop");
		}
	}

	if (failures) return 1;
	std::puts("montage tests passed");
	return 0;
}
