// The check that runs when a character is imported: skeleton convention detection (Mixamo, Unreal, none), missing bones, size and
// facing, which clips play which role, which locomotion sets and montages the clips allow, the report text and its JSON. The generated
// skeletons always; the real packs when GB_MIXAMO_DIR / GB_FARMING_DIR are set. CPU only.
#include "GB/Animation/Character.h"
#include "GB/Animation/Montage.h"
#include "GB/Animation/Rig.h"
#include "GB/Assets/Model.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool HasWarning(const GB::CharacterProfile& p, const char* text)
	{
		return std::any_of(p.Warnings.begin(), p.Warnings.end(), [&](const std::string& w) { return w.find(text) != std::string::npos; });
	}
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;

	// ---- a Mixamo-named humanoid with its clips
	auto humanoid = Model::Load(dir + "/test_humanoid.fbx");
	if (!humanoid) { std::fprintf(stderr, "FAIL: test humanoid\n"); return 1; }
	{
		const CharacterProfile before = AnalyzeCharacter(*humanoid);
		Check(before.Convention == "mixamo" && before.Humanoid() && before.MissingBones.empty(), "a Mixamo-named skeleton is recognised and complete");
		Check(std::fabs(before.Height - 1.8f) < 0.06f, "its size comes from the skeleton (1.8 m)");
		Check(before.FacesForward && !HasWarning(before, "faces"), "it faces +Z");
		Check(before.Sockets.size() == 9, "all nine standard sockets can be made");
		Check(!before.CanLocomote() && HasWarning(before, "no idle"), "without clips it says there is no idle");
		Check(!before.Ok(), "so it is not ready");

		humanoid->AppendAnimations(dir + "/test_idle.fbx");
		humanoid->AppendAnimations(dir + "/test_walk.fbx");
		humanoid->AppendAnimations(dir + "/test_run.fbx");
		const CharacterProfile p = AnalyzeCharacter(*humanoid);
		Check(p.RoleClip("idle") && *p.RoleClip("idle") == "test_idle" && p.RoleClip("walk") && *p.RoleClip("walk") == "test_walk" && p.RoleClip("run") && *p.RoleClip("run") == "test_run", "the idle, walk and run clips are found by name");
		Check(!p.RoleClip("jump") && !p.RoleClip("strafe_left"), "roles without a clip are left out");
		Check(p.CanLocomote() && p.ClipCount == 4, "with them it can locomote");
		Check(p.Analysis.RootBone == "mixamorig:Hips" && p.Analysis.LeftFoot == "mixamorig:LeftFoot", "the analysis bone names are this skeleton's");
		Check(p.Sets.empty() && p.Montages.empty(), "no farming sets or montages without those clips");
		Check(p.Ok(), "and nothing is wrong");

		const std::string text = FormatCharacterProfile(p);
		Check(text.find("mixamo") != std::string::npos && text.find("idle=\"test_idle\"") != std::string::npos && text.find("ready") != std::string::npos, "the report says it all");
		const nlohmann::json j = nlohmann::json::parse(CharacterProfileToJson(p), nullptr, false);
		Check(!j.is_discarded() && j["convention"] == "mixamo" && j["roles"]["walk"] == "test_walk" && j["sockets"].size() == 9 && j["canLocomote"] == true, "and so does the JSON");
	}

	// ---- another naming convention
	{
		auto unreal = Model::Load(dir + "/test_unreal.fbx");
		const CharacterProfile p = AnalyzeCharacter(*unreal);
		Check(p.Convention == "unreal" && p.Humanoid() && p.MissingBones.empty(), "Unreal's mannequin naming is recognised");
		Check(p.Analysis.RootBone == "pelvis" && p.Analysis.LeftFoot == "foot_l" && p.Analysis.RightHip == "thigh_r", "with its own bone names for the analysis");
		Check(!p.FacesForward && HasWarning(p, "faces -Z"), "toes backwards give a facing warning");
		Check(p.Rig.Left.Hand >= 0 && p.Rig.LeftLeg.Knee >= 0 && p.Rig.Spine2 >= 0, "its bones are mapped to the humanoid rig");
		std::string name;
		const HumanoidRig rig = FindHumanoidRigAuto(*unreal, &name);
		Check(name == "unreal" && rig.HasArms() && rig.HasLegs(), "the rig finder works on its own");
	}

	// ---- not a humanoid
	{
		auto blob = Model::Load(dir + "/test_blob.fbx");
		const CharacterProfile p = AnalyzeCharacter(*blob);
		Check(p.Convention.empty() && !p.Humanoid() && HasWarning(p, "not a humanoid"), "an unknown skeleton is reported");
		Check(p.Sockets.empty() && !p.CanLocomote(), "with no sockets and no locomotion");
		Check(FormatCharacterProfile(p).find("not recognised") != std::string::npos, "the report says so");
	}
	{
		// a skeleton with a missing limb
		auto partial = Model::Load(dir + "/test_rig.fbx");
		const CharacterProfile p = AnalyzeCharacter(*partial);
		Check(!p.MissingBones.empty() && HasWarning(p, "missing bones") && HasWarning(p, "left hand"), "missing bones are named");
	}

	// ---- the real packs (optional)
	const char* mixamo = std::getenv("GB_MIXAMO_DIR");
	const char* farming = std::getenv("GB_FARMING_DIR");
	if (mixamo && farming)
	{
		auto bot = Model::Load(std::string(mixamo) + "/idle.fbx");
		if (bot)
		{
			// the packs may be one flat folder (packs/full: every pack together) or one folder per pack; a folder is read once
			const bool oneFolder = std::filesystem::equivalent(mixamo, farming);
			std::vector<std::string> folders = { std::string(mixamo) };
			if (!oneFolder) folders.push_back(farming);
			for (const std::string& folder : folders)
				for (const auto& e : std::filesystem::directory_iterator(folder))
					if (e.path().extension() == ".fbx" && e.path().filename() != "X Bot.fbx" && e.path().filename() != "idle.fbx") bot->AppendAnimations(e.path().generic_string());
			const CharacterProfile p = AnalyzeCharacter(*bot);
			Check(p.Convention == "mixamo" && p.Humanoid() && p.MissingBones.empty(), "the real X Bot: Mixamo, complete");
			Check(p.CanLocomote() && p.RoleClip("jump") && p.RoleClip("strafe_left") && p.RoleClip("turn_left") && p.RoleClip("turn_right"), "with idle, locomotion, strafes, jump and turns");
			const auto hasSet = [&](const char* n) { return std::find(p.Sets.begin(), p.Sets.end(), n) != p.Sets.end(); };
			const bool farmingSets = hasSet("box") && hasSet("holding") && hasSet("wheelbarrow");
			// the locomotion and farming packs alone: exactly the three farming sets and their 11 montages. The flat folder has every pack, so
			// it also allows the other packs' sets and montages; then every montage of the manifest whose clip is there must be listed
			size_t present = 0;
			for (const MontageDesc& m : MixamoMontages()) present += bot->FindClip(m.Clip) >= 0 ? 1 : 0;
			for (const MontageDesc& m : UniversalMontages()) present += bot->FindClip(m.Clip) >= 0 ? 1 : 0;
			Check(farmingSets && (oneFolder ? p.Sets.size() >= 3 && p.Montages.size() == present && present >= 11 : p.Sets.size() == 3 && p.Montages.size() == 11),
				"and the farming sets and montages");
			Check(p.Ok(), "ready");
		}
	}

	// ---- a library with variants of every action (the Universal Animation Library's names): the plain clip plays each role
	{
		auto model = Model::Load(std::string(GB_TEST_DATA_DIR) + "/test_humanoid.fbx");
		if (!model) { std::fprintf(stderr, "FAIL: test skeleton\n"); return 1; }
		for (const char* n : { "Crouch_Idle_Loop", "Idle_Talking_Loop", "Idle_Loop", "Walk_Formal_Loop", "Walk_Loop", "Sprint_Loop", "Jog_Fwd_Loop", "Jump_Land", "Jump_Start", "Jump_Loop", "Interact", "Roll" })
		{
			AnimationClip c;
			c.Name = n;
			c.Duration = 1.0f;
			model->AddClip(c);
		}
		const CharacterProfile p = AnalyzeCharacter(*model);
		auto role = [&](const char* r) { const std::string* c = p.RoleClip(r); return c ? *c : std::string(); };
		Check(role("idle") == "Idle_Loop" && role("walk") == "Walk_Loop", "the plain idle and walk clips win over the crouching, talking and formal variants");
		Check(role("run") == "Jog_Fwd_Loop" && role("jump") == "Jump_Loop", "run accepts jog and sprint, and the jump is not its landing");
		Check(std::find(p.Montages.begin(), p.Montages.end(), "interact") != p.Montages.end() && std::find(p.Montages.begin(), p.Montages.end(), "roll") != p.Montages.end(), "the library's actions are offered as montages");
	}

	if (failures) return 1;
	std::puts("character profile tests passed");
	return 0;
}
