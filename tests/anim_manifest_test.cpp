// The animation manifest (AnimManifest.h): the sets, turns, jump, slots and montages of a character as data. It checks the JSON round trip,
// the validation, the file the assets ship (consistent with the pack listings it carries), loading into a character with and without the clips,
// and, when the Mixamo packs are at hand (GB_PACKS_DIR: a folder with the clips of all of them), that every clip the manifest names exists, that
// every set blends without breaking at any speed and direction, and how much of the packs nothing uses yet. CPU only.
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/AnimManifest.h"
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Assets/Model.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/Garden.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	std::string ReadFile(const std::string& path)
	{
		std::ifstream in(path);
		std::stringstream s;
		s << in.rdbuf();
		return s.str();
	}
	bool Contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}

int main()
{
	using namespace GB;
	Log::Init();

	// ---- the built-in manifest and its JSON
	const AnimManifest builtIn = MixamoManifest();
	Check(ValidateManifest(builtIn).empty(), "the built-in manifest is valid");
	Check(builtIn.Sets.size() >= 4 && builtIn.Montages.size() == 11 && builtIn.Free.Clips.size() == 7, "it has the sets, the eleven farming montages and the free set");
	const std::string json = AnimManifestToJson(builtIn);
	AnimManifest back;
	std::string error;
	Check(AnimManifestFromJson(json, back, &error), "its JSON reads back");
	Check(AnimManifestToJson(back) == json, "write, read and write again gives the same text");
	Check(back.Montages.size() == builtIn.Montages.size() && back.Montages[0].Name == builtIn.Montages[0].Name, "the montages survive the round trip");
	AnimManifest bad;
	Check(!AnimManifestFromJson("not json", bad, &error) && !error.empty(), "garbage is refused with a reason");
	Check(!AnimManifestFromJson("{\"version\": 99}", bad, &error), "an unknown version is refused");

	// ---- validation
	{
		AnimManifest m = builtIn;
		m.Sets.push_back(m.Sets[0]);
		Check(!ValidateManifest(m).empty(), "a repeated set name is reported");
		m = builtIn;
		m.Montages[0].Slot = "NoSuchSlot";
		Check(!ValidateManifest(m).empty(), "a montage in an unknown slot is reported");
		m = builtIn;
		m.Montages[0].Notifies.push_back({ 1.5f, "late" });
		Check(!ValidateManifest(m).empty(), "a notify outside 0..1 is reported");
		m = builtIn;
		m.Sets[0].Clips.resize(1);
		Check(!ValidateManifest(m).empty(), "a set with a single clip is reported");
		m = builtIn;
		m.Sets[0].Name = "default";
		Check(!ValidateManifest(m).empty(), "a set that takes the free set's name is reported");
	}

	// ---- the file the assets ship
	AnimManifest shipped;
	{
		const std::string path = std::string(GB_ASSET_DIR) + "/animation/mixamo.manifest.json";
		Check(std::filesystem::exists(path), "the assets ship an animation manifest");
		Check(AnimManifestFromJson(ReadFile(path), shipped, &error), "and it reads");
		const std::vector<std::string> problems = ValidateManifest(shipped);
		for (const std::string& p : problems) std::fprintf(stderr, "problem: %s\n", p.c_str());
		Check(problems.empty(), "the shipped manifest is valid");
		Check(shipped.Packs.size() == 7, "it lists the seven packs");
		Check(shipped.Sets.size() >= 9, "it has the sets of every pack (box, holding, wheelbarrow, injured, standing, crouch, sword and shield, bow)");
		Check(shipped.Montages.size() >= 90, "and the montages of every pack (farming actions, magic, blocks, hits, deaths, sword, bow, gestures)");
		std::set<std::string> inPacks;
		for (const ManifestPack& p : shipped.Packs) inPacks.insert(p.Clips.begin(), p.Clips.end());
		int unknown = 0;
		auto known = [&](const std::string& clip) { if (!inPacks.count(clip)) { std::fprintf(stderr, "not in any pack: %s\n", clip.c_str()); ++unknown; } };
		for (const std::string& c : shipped.Free.Clips) known(c);
		for (const std::string& c : shipped.Free.Turns) known(c);
		for (const ManifestSet& s : shipped.Sets) { for (const std::string& c : s.Clips) known(c); for (const std::string& c : s.Turns) known(c); }
		for (const MontageDesc& m : shipped.Montages) known(m.Clip);
		known(shipped.AirClip);
		Check(unknown == 0, "every clip the manifest names is in the listing of one of its packs");
		int unused = 0;
		for (const ManifestPack& p : shipped.Packs)
			for (const std::string& c : p.Clips)
			{
				bool used = Contains(shipped.Free.Clips, c) || Contains(shipped.Free.Turns, c) || c == shipped.AirClip;
				for (const ManifestSet& s : shipped.Sets) used = used || Contains(s.Clips, c) || Contains(s.Turns, c);
				for (const MontageDesc& m : shipped.Montages) used = used || m.Clip == c;
				unused += used ? 0 : 1;
			}
		std::fprintf(stderr, "info: %d clip(s) of the packs are not used by anything in the shipped manifest\n", unused);
	}

	// ---- loading into a character: the generated rig has no Mixamo clips, so nothing but the report comes out
	{
		const std::string dir = GB_TEST_DATA_DIR;
		auto model = Model::Load(dir + "/test_rig.fbx");
		if (!model) { std::fprintf(stderr, "FAIL: test rig\n"); return 1; }
		model->AppendAnimations(dir + "/test_clip.fbx");
		model->AppendAnimations(dir + "/test_idle.fbx");
		ModelInstance inst(*model);
		CharacterAnimator animator(*model, inst);
		AnimManifest m;
		m.Free.Clips = { "test_idle", "test_clip" };
		ManifestSet extra;
		extra.Name = "alt";
		extra.Clips = { "test_idle", "test_clip", "no such clip" };
		m.Sets.push_back(extra);
		ManifestSet ghost;
		ghost.Name = "ghost";
		ghost.Clips = { "nope", "nothing" };
		m.Sets.push_back(ghost);
		MontageDesc act;
		act.Name = "act";
		act.Clip = "test_clip";
		m.Montages.push_back(act);
		MontageDesc lost;
		lost.Name = "lost";
		lost.Clip = "not there";
		m.Montages.push_back(lost);
		m.AirClip = "not a jump";
		const ManifestReport r = animator.LoadManifest(m);
		Check(Contains(r.Sets, "default") && Contains(r.Sets, "alt") && !Contains(r.Sets, "ghost"), "the sets the model has clips for are registered, the others are not");
		Check(r.Montages == 1 && animator.Anim().Montages().Has("act") && !animator.Anim().Montages().Has("lost"), "the montages are registered by the clips the model has");
		Check(Contains(r.Missing, "set alt: no such clip") && Contains(r.Missing, "montage lost: not there") && Contains(r.Missing, "air: not a jump"), "the report names what was missing");
		Check(!r.Air, "no jump without its clip");
		Check(animator.Anim().HasSet("alt") && !animator.Anim().HasSet("ghost"), "the AnimInstance has the registered set only");
	}

	// ---- the real packs
	const char* packs = std::getenv("GB_PACKS_DIR");
	if (packs && std::filesystem::exists(std::string(packs) + "/idle.fbx"))
	{
		auto model = Model::Load(std::string(packs) + "/idle.fbx");   // the skeleton with one clip, no mesh: no GL context needed
		int loaded = 0;
		for (const auto& entry : std::filesystem::directory_iterator(packs))
			if (entry.path().extension() == ".fbx" && entry.path().filename() != "X Bot.fbx" && entry.path().filename() != "idle.fbx")
				loaded += model->AppendAnimations(entry.path().string()) > 0 ? 1 : 0;
		std::fprintf(stderr, "info: %d clip files loaded\n", loaded);
		ModelInstance inst(*model);
		CharacterAnimator animator(*model, inst);
		const ManifestReport r = animator.LoadManifest(shipped);
		for (const std::string& m : r.Missing) std::fprintf(stderr, "missing: %s\n", m.c_str());
		Check(r.Missing.empty(), "the packs have every clip the shipped manifest names");
		Check(r.Sets.size() == shipped.Sets.size() + 1, "every set is registered");
		Check(r.Montages == static_cast<int>(shipped.Montages.size()), "every montage is registered");
		Check(r.Air, "and the jump");
		std::fprintf(stderr, "info: %zu clip(s) of the folder are used by nothing\n", r.Unused.size());
		Check(UnusedClips(shipped, *model).size() == r.Unused.size(), "UnusedClips agrees with the report");

		// every set blends without breaking from standing to running, in every direction
		AnimInstance& anim = animator.Anim();
		const int hips = model->FindNode("mixamorig:Hips");
		for (const std::string& set : r.Sets)
		{
			Check(anim.SetOverlay(set == "default" ? "free" : set, 0.0f), "the set can be activated");
			bool finite = true;
			float worst = 0.0f;
			for (float speed : { 0.0f, 0.6f, 1.2f, 2.0f, 3.5f, 5.0f })
				for (int dir = 0; dir < 8; ++dir)
				{
					const float angle = static_cast<float>(dir) * 0.7853982f;
					MotionState s;
					s.LocalVelocity = { std::sin(angle) * speed, std::cos(angle) * speed };
					s.Velocity = { s.LocalVelocity.x, 0.0f, s.LocalVelocity.y };
					s.Speed = speed;
					for (int i = 0; i < 40; ++i) anim.Update(s, 1.0f / 60.0f, glm::mat4(1.0f));
					const glm::vec3 p = glm::vec3(inst.GetNodeWorld(hips)[3]);
					finite = finite && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
					worst = std::max(worst, std::fabs(p.y - 1.0f));
				}
			if (!finite || worst > 0.6f) std::fprintf(stderr, "set %s: finite %d, hips height off by %.2f m\n", set.c_str(), finite ? 1 : 0, static_cast<double>(worst));
			Check(finite, "the set blends to finite poses at every speed and direction");
			Check(worst < 0.6f, "and the hips stay near their height");
		}
	}
	else
		std::fprintf(stderr, "note: GB_PACKS_DIR not set, the pack-backed checks were skipped\n");

	return failures;
}
