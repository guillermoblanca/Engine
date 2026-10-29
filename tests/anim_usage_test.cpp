// Which clip each gameplay action plays (docs/ANIMACIONES_USO.md): the "abilities", "activities" and "stages" of the shipped animation manifest.
// It checks that every ability the game registers (garden, resource nodes, interactives) and every job of the tribe resolves to montages the
// manifest has, that the montage of an ability carries the notifies its events wait for, that the uses survive the JSON round trip, that
// ApplyManifestAbilities only touches the montage names (and only with montages the character can play), and the stage rates. With the Mixamo
// packs at hand (GB_PACKS_DIR), every clip they name exists and the notify of every work montage sits near the moment the clip's hand moves
// fastest (the blow lands, the hand reaches): measured, not guessed. GB_ANIM_PROBE=1 prints those measures for every clip of the packs (the
// table the mapping was chosen from). CPU only.
#include "GB/Animation/AnimManifest.h"
#include "GB/Animation/Pose.h"
#include "GB/Animation/Rig.h"
#include "GB/Assets/Model.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Garden.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/Resources.h"
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
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}
	std::string ReadFile(const std::string& path)
	{
		std::ifstream in(path);
		std::stringstream s;
		s << in.rdbuf();
		return s.str();
	}

	// Where the hands are along a clip, in the model's space: the fraction of the clip at which a hand moves fastest (a blow, a reach), and the
	// lowest and highest a hand goes (kneeling work stays low, an overhead blow goes high), plus how low the hips go.
	struct HandProbe
	{
		float Duration = 0.0f;
		float PeakTime = 0.0f, PeakSpeed = 0.0f;   // fastest hand (either), m/s, at this fraction of the clip
		float LowHand = 1e9f, HighHand = -1e9f;     // metres
		float LowHips = 1e9f;
		float ReachTime = 0.0f, Reach = 0.0f;       // the moment a hand is farthest out from the hips (the hand reaches the fruit, the rope, the soil)
	};

	HandProbe Probe(const GB::Model& model, const GB::HumanoidRig& rig, int clip)
	{
		using namespace GB;
		HandProbe p;
		const AnimationClip& c = model.GetClips()[static_cast<size_t>(clip)];
		p.Duration = c.Duration;
		const int steps = std::max(8, static_cast<int>(c.Duration * 60.0f));
		Pose pose;
		std::vector<glm::mat4> world;
		glm::vec3 lastL(0.0f), lastR(0.0f);
		const float dt = c.Duration / static_cast<float>(steps);
		for (int i = 0; i <= steps; ++i)
		{
			const float t = c.Duration * static_cast<float>(i) / static_cast<float>(steps);
			SampleClip(model, clip, t, pose);
			PoseToWorld(model, pose, world);
			const glm::vec3 hips(world[static_cast<size_t>(rig.Hips)][3]);
			// the hands relative to the hips' ground point, so a clip that walks does not count its travel as a blow
			const glm::vec3 base(hips.x, 0.0f, hips.z);
			const glm::vec3 l = glm::vec3(world[static_cast<size_t>(rig.Left.Hand)][3]) - base, r = glm::vec3(world[static_cast<size_t>(rig.Right.Hand)][3]) - base;
			p.LowHand = std::min({ p.LowHand, l.y, r.y });
			p.HighHand = std::max({ p.HighHand, l.y, r.y });
			p.LowHips = std::min(p.LowHips, hips.y);
			// how far out a hand is: horizontally from the hips and down from the chest (picking low counts as reaching)
			const float outL = glm::length(glm::vec2(l.x, l.z)) + std::max(0.0f, 1.2f - l.y) * 0.5f, outR = glm::length(glm::vec2(r.x, r.z)) + std::max(0.0f, 1.2f - r.y) * 0.5f;
			if (std::max(outL, outR) > p.Reach) { p.Reach = std::max(outL, outR); p.ReachTime = t / std::max(c.Duration, 1e-4f); }
			if (i > 0)
			{
				const float speed = std::max(glm::length(l - lastL), glm::length(r - lastR)) / std::max(dt, 1e-4f);
				if (speed > p.PeakSpeed) { p.PeakSpeed = speed; p.PeakTime = t / std::max(c.Duration, 1e-4f); }
			}
			lastL = l;
			lastR = r;
		}
		return p;
	}

	// The jobs of the tribe a member can be doing (TribeMember::Job; "farm" is split by what it does to the plots, "socialize" by an elder is
	// teaching): each must say what it plays, even if it is only a set or a note that nothing fits.
	const char* const kActivities[] = { "eat", "drink", "sleep", "warm", "regroup", "socialize", "teach", "craft", "make", "deposit", "fire", "stoke",
		"experiment", "rest", "haul", "build", "deliver", "farm.till", "farm.sow", "farm.tend", "farm.harvest", "dead", "flee", "hunt", "injured", "explore", "recover", "care" };
}

int main()
{
	using namespace GB;
	Log::Init();

	AnimManifest shipped;
	std::string error;
	const std::string path = std::string(GB_ASSET_DIR) + "/animation/mixamo.manifest.json";
	Check(AnimManifestFromJson(ReadFile(path), shipped, &error), "the shipped manifest reads: " + error);
	for (const std::string& p : ValidateManifest(shipped)) Check(false, "manifest: " + p);

	// ---- the uses go and come back through JSON
	{
		AnimManifest back;
		const std::string json = AnimManifestToJson(shipped);
		Check(AnimManifestFromJson(json, back, &error), "the uses read back");
		Check(AnimManifestToJson(back) == json, "write, read and write again gives the same text (uses and stages included)");
		Check(back.Abilities.size() == shipped.Abilities.size() && back.Activities.size() == shipped.Activities.size() && back.Stages.size() == shipped.Stages.size()
			&& !back.Abilities.empty() && !back.Activities.empty() && back.Stages.size() == 3,
			"abilities, activities and stages survive the round trip (" + std::to_string(back.Abilities.size()) + "/" + std::to_string(shipped.Abilities.size()) + ", "
			+ std::to_string(back.Activities.size()) + "/" + std::to_string(shipped.Activities.size()) + ", " + std::to_string(back.Stages.size()) + ")");
		AnimManifest broken = shipped;
		broken.Abilities.push_back({ "Ghost", { "no_such_montage" }, "", false, "" });
		Check(!ValidateManifest(broken).empty(), "an ability that plays an unknown montage is reported");
		broken = shipped;
		broken.Activities.push_back({ "limp", {}, "no_such_set", false, "" });
		Check(!ValidateManifest(broken).empty(), "an activity that walks with an unknown set is reported");
		broken = shipped;
		broken.Stages.push_back({ "ghost", 0.0f });
		Check(!ValidateManifest(broken).empty(), "a stage rate of 0 is reported");
	}

	// ---- every ability the game registers resolves to montages of the manifest, with the notifies its events wait for
	GameplayLibrary lib;
	RegisterGardenContent(lib);
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> nodes;
	RegisterEraOneItems(items, recipes, nodes);
	RegisterResourceContent(lib, nodes);
	std::map<std::string, InteractiveDef> interactives;
	Check(LoadInteractivesFile(std::string(GB_ASSET_DIR) + "/data/world.json", interactives).empty(), "world.json's interactives load");
	RegisterInteractiveContent(lib, interactives);
	RegisterBuildingContent(lib);   // H8: Build
	auto montageOf = [&](const std::string& name) -> const MontageDesc* { for (const MontageDesc& d : shipped.Montages) if (d.Name == name) return &d; return nullptr; };
	int abilities = 0;
	for (const auto& [name, def] : lib.Abilities())
	{
		const ManifestUse* use = FindUse(shipped.Abilities, name);
		if (def.Montages.empty() && def.Events.empty())
		{
			// carrying and letting go: no montage of their own, the locomotion set changes (the manifest may still say which)
			if (use) Check(!use->Set.empty() || !use->Montages.empty(), "ability " + name + " is listed with something to play");
			continue;
		}
		++abilities;
		Check(use != nullptr, "ability " + name + " is in the manifest's abilities");
		if (!use) continue;
		Check(!use->Montages.empty(), "ability " + name + " plays at least one montage");
		for (const std::string& m : use->Montages)
		{
			const MontageDesc* d = montageOf(m);
			Check(d != nullptr, "ability " + name + ": montage " + m + " is in the manifest");
			if (!d) continue;
			for (const AbilityEventDef& e : def.Events)
			{
				if (e.Notify.empty()) continue;
				bool has = false;
				for (const MontageNotify& n : d->Notifies) has = has || n.Name == e.Notify;
				Check(has, "ability " + name + ": montage " + m + " carries the notify '" + e.Notify + "' (its timing is data)");
			}
		}
	}
	Check(abilities >= 15, "the garden, the resource nodes and the interactives give at least fifteen abilities that play something");
	for (const ManifestUse& use : shipped.Abilities)
		Check(lib.Ability(use.Name) != nullptr || use.Note.find("H14") != std::string::npos || use.Note.find("future") != std::string::npos,
			"the manifest's ability " + use.Name + " exists (or is marked as one still to come)");

	// ---- every job of the tribe says what it plays
	for (const char* job : kActivities)
	{
		const ManifestUse* use = FindUse(shipped.Activities, job);
		Check(use != nullptr, std::string("activity ") + job + " is in the manifest's activities");
		if (use) Check(!use->Montages.empty() || !use->Set.empty(), std::string("activity ") + job + " plays a montage or walks with a set");
	}
	Check(StageRate(shipped, "child") > 1.0f && StageRate(shipped, "elder") < 1.0f && StageRate(shipped, "adult") == 1.0f && StageRate(shipped, "nobody") == 1.0f,
		"a child plays the clips faster, an elder slower, an adult (and anything unlisted) as they are");

	// ---- ApplyManifestAbilities: only the names of the montages, only those the character can play
	{
		GameplayLibrary copy = lib;
		const int changed = ApplyManifestAbilities(copy, shipped);
		Check(changed > 0, "the manifest changes the montages of some abilities");
		const ManifestUse* chop = FindUse(shipped.Abilities, "Chop");
		Check(chop && copy.Ability("Chop")->Montages == chop->Montages, "Chop plays the manifest's montages");
		Check(copy.Ability("Chop")->Events.size() == lib.Ability("Chop")->Events.size() && copy.Ability("Chop")->Events[0].Time == lib.Ability("Chop")->Events[0].Time
			&& copy.Ability("Chop")->Cost == lib.Ability("Chop")->Cost && copy.Ability("Chop")->Duration == lib.Ability("Chop")->Duration,
			"its events, their times (the headless timers), its cost and its duration do not change");
		GameplayLibrary none = lib;
		Check(ApplyManifestAbilities(none, shipped, [](const std::string&) { return false; }) == 0 && none.Ability("Chop")->Montages == lib.Ability("Chop")->Montages,
			"a character that can play none of them keeps the abilities' own montages");
		GameplayLibrary some = lib;
		ApplyManifestAbilities(some, shipped, [&](const std::string& m) { return chop && m == chop->Montages[0]; });
		Check(some.Ability("Chop")->Montages.size() == 1, "and one that has some keeps only those");
	}

	// ---- the real clips
	const char* packs = std::getenv("GB_PACKS_DIR");
	if (packs && std::filesystem::exists(std::string(packs) + "/idle.fbx"))
	{
		auto model = Model::Load(std::string(packs) + "/idle.fbx");   // the skeleton with one clip, no mesh: no GL context needed
		for (const auto& entry : std::filesystem::directory_iterator(packs))
			if (entry.path().extension() == ".fbx" && entry.path().filename() != "X Bot.fbx" && entry.path().filename() != "idle.fbx")
				model->AppendAnimations(entry.path().string());
		const HumanoidRig rig = FindHumanoidRigAuto(*model);
		Check(rig.HasArms() && rig.Hips >= 0, "the packs' skeleton is a humanoid");
		std::set<std::string> named;
		for (const ManifestUse& u : shipped.Abilities) named.insert(u.Montages.begin(), u.Montages.end());
		for (const ManifestUse& u : shipped.Activities) named.insert(u.Montages.begin(), u.Montages.end());
		for (const std::string& m : named)
		{
			const MontageDesc* d = montageOf(m);
			Check(d && model->FindClip(d->Clip) >= 0, "the clip of montage " + m + " is in the packs");
		}
		// the notify of a work montage sits near the moment the hand moves fastest (the blow lands, the hand reaches)
		for (const ManifestUse& u : shipped.Abilities)
			for (const std::string& m : u.Montages)
			{
				const MontageDesc* d = montageOf(m);
				const int clip = d ? model->FindClip(d->Clip) : -1;
				if (clip < 0 || d->Notifies.empty()) continue;
				const HandProbe p = Probe(*model, rig, clip);
				const float t = d->Notifies[0].Time;
				// a blow (a hand faster than 5 m/s: the axe, the pick, the push) lands when the hand is fastest; a slow action happens when the hand
				// is farthest out (it reaches the fruit, the soil, the rope)
				const bool blow = p.PeakSpeed > 5.0f;
				const float moment = blow ? p.PeakTime : p.ReachTime;
				std::fprintf(stderr, "info: %-12s %-22s notify %.2f, %s at %.2f (fastest %.1f m/s at %.2f), hands %.2f..%.2f m, hips down to %.2f m, %.2f s at x%.2f\n",
					u.Name.c_str(), m.c_str(), static_cast<double>(t), blow ? "blow" : "reach", static_cast<double>(moment), static_cast<double>(p.PeakSpeed),
					static_cast<double>(p.PeakTime), static_cast<double>(p.LowHand), static_cast<double>(p.HighHand), static_cast<double>(p.LowHips),
					static_cast<double>(p.Duration / d->PlayRate), static_cast<double>(d->PlayRate));
				Check(std::fabs(t - moment) <= (blow ? 0.12f : 0.2f), "montage " + m + ": the notify sits at the measured moment of the action (" + std::to_string(moment) + ")");
			}
		if (std::getenv("GB_ANIM_PROBE"))
			for (size_t i = 0; i < model->GetClips().size(); ++i)
			{
				const HandProbe p = Probe(*model, rig, static_cast<int>(i));
				std::fprintf(stderr, "probe: %-40s %5.2f s  fastest %.2f (%.1f m/s)  reach %.2f  hands %.2f..%.2f  hips %.2f\n", model->GetClips()[i].Name.c_str(),
					static_cast<double>(p.Duration), static_cast<double>(p.PeakTime), static_cast<double>(p.PeakSpeed), static_cast<double>(p.ReachTime), static_cast<double>(p.LowHand),
					static_cast<double>(p.HighHand), static_cast<double>(p.LowHips));
			}
	}
	else
		std::fprintf(stderr, "info: GB_PACKS_DIR not set (or without idle.fbx): the clips themselves are not checked\n");

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::printf("anim_usage: all checks passed\n");
	return 0;
}
