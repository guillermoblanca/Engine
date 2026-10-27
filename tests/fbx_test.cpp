// FBX import and clip analysis on small generated files (tools/make_test_fbx.py): units and hierarchy, baked animation,
// clips appended by bone name, and the measured root motion. No GL needed (the rig has no meshes).
// Optional: GB_MIXAMO_DIR=<folder with "X Bot.fbx" and the Mixamo locomotion clips> also checks the real pack.
#include "GB/Animation/ClipAnalysis.h"
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
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
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;

	auto model = Model::Load(dir + "/test_rig.fbx");
	Check(model != nullptr, "the FBX loads");
	if (!model) return 1;

	// ---- nodes: names, hierarchy, centimeters -> meters
	Check(model->GetNodes().size() == 7, "seven bones");
	const int hips = model->FindNode("mixamorig:Hips"), head = model->FindNode("mixamorig:Head"), foot = model->FindNode("mixamorig:LeftFoot");
	Check(hips >= 0 && head >= 0 && foot >= 0, "bones are found by name");
	Check(model->GetNodes()[static_cast<size_t>(hips)].Parent == -1 && model->GetRootNodes().size() == 1, "the hips are the root");
	Check(Near(model->GetNodes()[static_cast<size_t>(hips)].Translation.y, 1.0f, 1e-3f), "100 cm became 1 m");
	ModelInstance rest(*model);
	Check(Near(rest.GetNodeWorld(head)[3].y, 1.0f + 0.10f + 0.50f, 1e-3f), "world positions add up in meters");
	Check(Near(rest.GetNodeWorld(foot)[3].y, 1.0f - 0.05f - 0.90f, 1e-3f), "the foot hangs below the hip");

	// ---- the baked animation
	Check(model->GetClips().size() == 1 && model->GetClips()[0].Name == "test_rig", "a single take is named after the file");
	const AnimationClip& walk = model->GetClips()[0];
	Check(Near(walk.Duration, 1.0f, 0.04f), "duration is one second");
	ModelInstance inst(*model);
	inst.Play(0, false, 0.0f);
	inst.SetTime(0.5f);
	Check(Near(inst.GetNodeWorld(hips)[3].z, 0.6f, 0.02f), "halfway the hips have moved 0.6 m");
	inst.SetTime(1.0f);
	Check(Near(inst.GetNodeWorld(hips)[3].z, 1.2f, 0.02f), "at the end 1.2 m");
	Check(Near(inst.GetNodeWorld(hips)[3].y, 1.0f, 1e-3f), "height untouched");

	// ---- clips from another file, matched by bone name
	std::string report;
	const int added = model->AppendAnimations(dir + "/test_clip.fbx", &report);
	Check(added == 1 && model->GetClips().size() == 2, "a second clip is appended");
	Check(model->FindClip("test_clip") == 1, "named after its file");
	Check(report.find("1 animated bone") != std::string::npos, "the bone this rig lacks is reported");
	Check(model->AppendAnimations(dir + "/test_clip.fbx") == 1 && model->GetClips().size() == 2, "appending a file again replaces its clip");
	Check(model->AppendAnimations(dir + "/missing.fbx", &report) == 0, "a missing file adds nothing");

	// ---- analysis: root motion measured from the clips
	const ClipStats a = AnalyzeClip(*model, 0), b = AnalyzeClip(*model, 1);
	Check(a.Valid && b.Valid, "the root bone is found");
	Check(Near(a.RootDelta.z, 1.2f) && Near(a.RootSpeed, 1.2f, 0.05f), "walk: 1.2 m, 1.2 m/s");
	Check(Near(b.RootDelta.z, 1.5f) && Near(b.RootSpeed, 3.0f, 0.1f), "run: 1.5 m in half a second = 3 m/s");
	Check(Near(a.RootYawDegrees, 0.0f, 1.0f), "no turning");
	Check(a.LoopError < 1.0f, "no rotation change: a perfect loop");
	Check(a.RootHeightMax - a.RootHeightMin < 0.01f, "no bobbing");
	Check(!FormatClipStats(a).empty(), "stats print");
	ClipAnalysisConfig wrong; wrong.RootBone = "nope";
	Check(!AnalyzeClip(*model, 0, wrong).Valid, "wrong bone names are reported as invalid");

	// ---- the real Mixamo pack, when available (not part of the repository)
	if (const char* pack = std::getenv("GB_MIXAMO_DIR"))
	{
		const std::string p = pack;
		if (std::filesystem::exists(p + "/walking.fbx"))
		{
			auto walkClips = Model::Load(p + "/walking.fbx");
			Check(walkClips && walkClips->GetClips().size() == 1, "mixamo: one clip in walking.fbx");
			if (walkClips)
			{
				const ClipStats w = AnalyzeClip(*walkClips, 0);
				Check(w.Valid && Near(w.RootSpeed, 1.67f, 0.05f) && Near(w.Duration, 1.03f, 0.05f), "mixamo walking: 1.67 m/s in 1.03 s");
				Check(w.LeftPlants.size() == 1 && w.RightPlants.size() == 1, "mixamo walking: one plant per foot per cycle");
			}
			auto run = Model::Load(p + "/standard run.fbx");
			if (run) Check(Near(AnalyzeClip(*run, 0).RootSpeed, 4.42f, 0.1f), "mixamo run: 4.42 m/s");
		}
	}

	if (failures) return 1;
	std::puts("fbx tests passed");
	return 0;
}
