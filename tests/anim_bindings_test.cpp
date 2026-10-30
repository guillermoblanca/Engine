// Animation bindings (docs/EDITOR_ANIMACIONES.md), headless: the JSON file (versioned, round trip, bad input, a missing file), the validation
// (missing clips, bad trims, loops on single-shot abilities, transitions with no way out), ResolveClip with its fallbacks, ApplyBindings on the
// shipped manifest (no bindings = the same manifest; with bindings the montages / abilities / sets change), and trim, loop, speed, blend curve and
// transitions run on an animation clock (the pure clock and the MontagePlayer over the test rig). CPU only.
#include "GB/Animation/AnimBindings.h"
#include "GB/Animation/AnimManifest.h"
#include "GB/Animation/Montage.h"
#include "GB/Assets/Model.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Log.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}
	bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
	std::string ReadFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::stringstream s;
		s << in.rdbuf();
		return s.str();
	}
	GB::BindingTransition Tr(const char* from, const char* to, float duration, GB::BlendCurve curve) { GB::BindingTransition t; t.From = from; t.To = to; t.Duration = duration; t.Curve = curve; return t; }
	bool Has(const std::vector<std::string>& v, const std::string& s)
	{
		for (const std::string& e : v) if (e.find(s) != std::string::npos) return true;
		return false;
	}
}

int main()
{
	using namespace GB;
	Log::Init();
	const std::string assets = GB_ASSET_DIR, data = GB_TEST_DATA_DIR;

	AnimManifest shipped;
	std::string error;
	Check(AnimManifestFromJson(ReadFile(assets + "/animation/mixamo.manifest.json"), shipped, &error), "the shipped manifest reads: " + error);
	GameplayLibrary library;
	RegisterCombatContent(library, DefaultCombatRules());
	const BindingDefaults defaults = MakeBindingDefaults(shipped, &library);

	// ---- the defaults: the C++ lists of the combat rules are visible as data
	Check(defaults.Abilities.count("Attack.axe") && defaults.Abilities.at("Attack.axe").size() == 2, "Attack.axe's C++ montages are listed as the default");
	Check(defaults.Abilities.count("Block") && defaults.Abilities.count("Stagger"), "Block and Stagger (damage received) are listed");
	Check(defaults.Abilities.count("Chop") && defaults.Abilities.at("Chop") == shipped.Abilities[0].Montages, "the manifest's abilities are listed");
	bool hasAttack = false, hasLoco = false, hasSleep = false;
	for (const BindableAction& a : ListBindableActions(shipped, defaults))
	{
		hasAttack |= a.Key == "ability:Attack.axe" && a.Category == "Attack";
		hasLoco |= a.Key == "loco:default";
		hasSleep |= a.Key == "activity:sleep";
	}
	Check(hasAttack && hasLoco && hasSleep, "the action list has attacks, locomotion and sleeping");

	// ---- JSON: round trip, versioning, bad input, missing file
	AnimBindings b;
	{
		AnimBinding chop;
		chop.Action = "ability:Chop";
		BindingClip c;
		c.Source = "chop_1"; c.Speed = 1.25f; c.TrimStart = 0.1f; c.TrimEnd = 0.8f; c.BlendIn = 0.05f; c.Curve = BlendCurve::EaseOut; c.Loop = false; c.KeepRootMotion = true;
		chop.Clips.push_back(c);
		chop.Clips.push_back({ "chop_2" });
		b.Bindings.push_back(chop);
		AnimBinding sleep;
		sleep.Action = "activity:sleep"; sleep.Skeleton = "x bot"; sleep.When["node"] = "bed"; sleep.Set = "default"; sleep.Repeat = false; sleep.Note = "n";
		sleep.Clips.push_back({ "act_lie_down" });
		b.Bindings.push_back(sleep);
		b.Transitions.push_back(Tr("ability:Chop", "*", 0.3f, BlendCurve::Linear));
	}
	const std::string json = AnimBindingsToJson(b);
	AnimBindings back;
	Check(AnimBindingsFromJson(json, back, &error), "the bindings read back: " + error);
	Check(AnimBindingsToJson(back) == json, "writing what was read gives the same text");
	Check(back.Bindings.size() == 2 && back.Bindings[0].Clips[0].TrimStart && Near(*back.Bindings[0].Clips[0].TrimStart, 0.1f) && back.Bindings[0].Clips[0].Curve == BlendCurve::EaseOut
		&& back.Bindings[0].Clips[0].KeepRootMotion && !back.Bindings[0].Clips[1].Speed && back.Bindings[1].When.at("node") == "bed" && back.Transitions[0].Duration == 0.3f,
		"fields and unset overrides survive the round trip");
	Check(!AnimBindingsFromJson("not json", back, &error) && !error.empty(), "garbage is refused with a reason");
	Check(!AnimBindingsFromJson("{\"version\": 99}", back, &error) && error.find("99") != std::string::npos, "an unknown version is refused");
	Check(!AnimBindingsFromJson("{\"version\":1,\"bindings\":[{\"clips\":[]}]}", back, &error), "a binding without an action is refused");
	bool found = true;
	AnimBindings none;
	Check(LoadAnimBindingsFile(data + "/no_such_bindings.json", none, &error, &found) && !found && none.Bindings.empty(), "a missing file is not an error: empty bindings");
	{
		AnimBindings shippedBindings;
		Check(LoadAnimBindingsFile(assets + "/data/animation_bindings.json", shippedBindings, &error, &found) && found, "the shipped assets/data/animation_bindings.json loads: " + error);
		Check(DescribeIssues(ValidateBindings(shippedBindings, shipped, defaults)).empty(), "and it validates clean");
		AnimManifest copy = shipped;
		ApplyBindings(copy, shippedBindings, defaults);
		Check(AnimManifestToJson(copy) == AnimManifestToJson(shipped), "the shipped bindings leave the manifest exactly as it is: the game's behaviour is the default");
	}

	// ---- ResolveClip: bindings, defaults, fallbacks, skeleton and context
	{
		auto has = [](const std::string& c) { return c != "ghost"; };
		ResolvedClip none2 = ResolveClip({}, shipped, defaults, "ability:Chop", {}, "", 0, has);
		const MontageDesc* chop1 = nullptr;
		for (const MontageDesc& m : shipped.Montages) if (m.Name == shipped.Abilities[0].Montages[0]) chop1 = &m;
		Check(none2.Found && !none2.FromBinding && chop1 && none2.Clip == chop1->Clip && Near(none2.Speed, chop1->PlayRate), "no binding: the manifest's montage answers");
		ResolvedClip atk = ResolveClip({}, shipped, defaults, "ability:Attack.axe", {}, "", 1, has);
		Check(atk.Found && atk.Montage == "sword_slash_3" && !atk.FromBinding, "no binding: the C++ default of Attack.axe answers, in turn");
		Check(!ResolveClip({}, shipped, defaults, "ability:Nothing", {}, "", 0, has).Found, "an action nobody defines is not found");

		ResolvedClip r = ResolveClip(back, shipped, defaults, "ability:Chop", {}, "any", 0, has);
		Check(r.Found && r.FromBinding && Near(r.Speed, 1.25f) && Near(r.TrimStart, 0.1f) && Near(r.TrimEnd, 0.8f) && Near(r.BlendIn, 0.05f) && r.Curve == BlendCurve::EaseOut
			&& r.KeepRootMotion && !r.Loop && r.Clip == chop1->Clip && Near(r.BlendOut, chop1->BlendOut), "the binding's overrides apply on top of the montage's own values");
		Check(ResolveClip(back, shipped, defaults, "ability:Chop", {}, "", 1, has).Montage == "chop_2", "the second use takes the second clip");
		Check(ResolveClip(back, shipped, defaults, "activity:sleep", {}, "x bot", 0, has).Why == "default", "a binding that needs a context is not chosen without it");
		Check(ResolveClip(back, shipped, defaults, "activity:sleep", { { "node", "bed" } }, "X Bot", 0, has).FromBinding, "with the context and the skeleton (any case) it is");
		Check(!ResolveClip(back, shipped, defaults, "activity:sleep", { { "node", "bed" } }, "fox", 0, has).FromBinding, "another skeleton keeps the default");

		AnimBindings ghost;
		AnimBinding g;
		g.Action = "ability:Chop";
		g.Clips.push_back({ "ghost" });
		ghost.Bindings.push_back(g);
		ResolvedClip fb = ResolveClip(ghost, shipped, defaults, "ability:Chop", {}, "", 0, has);
		Check(fb.Found && !fb.FromBinding && fb.Why.find("ghost") != std::string::npos && fb.Clip == chop1->Clip, "a missing clip falls back to the default and says why");

		AnimBindings two = ghost;
		AnimBinding specific = g;
		specific.Skeleton = "x bot";
		specific.Clips = { { "chop_2" } };
		two.Bindings = { { "ability:Chop", "", {}, { { "chop_1" } }, "", std::nullopt, "" }, specific };
		Check(ResolveClip(two, shipped, defaults, "ability:Chop", {}, "x bot", 0, has).Montage == "chop_2" && ResolveClip(two, shipped, defaults, "ability:Chop", {}, "fox", 0, has).Montage == "chop_1",
			"the binding for this skeleton wins over the general one");
	}

	// ---- ApplyBindings
	{
		AnimManifest m = shipped;
		BindingsReport rep = ApplyBindings(m, back, defaults, "x bot");
		Check(rep.Applied == 2 && rep.Transitions == 1, "applied the Chop action, made the montage of the sleep one (it plays only in its context) and one transition");
		const ManifestUse* chop = FindUse(m.Abilities, "Chop");
		Check(chop && chop->Montages.size() == 2 && chop->Montages[0].rfind("bind:ability:Chop#", 0) == 0 && chop->Montages[1] == "chop_2",
			"Chop plays an alias with overrides, then the plain manifest montage (it had no overrides)");
		const MontageDesc* alias = nullptr;
		for (const MontageDesc& d : m.Montages) if (d.Name == chop->Montages[0]) alias = &d;
		Check(alias && Near(alias->PlayRate, 1.25f) && Near(alias->TrimStart, 0.1f) && Near(alias->TrimEnd, 0.8f) && alias->Curve == BlendCurve::EaseOut && !alias->StripTravel && !alias->Notifies.empty(),
			"the alias keeps the base montage's notifies and carries the overrides");
		Check(ValidateManifest(m).empty(), "the manifest with bindings is still valid");
		Check(m.Transitions.size() == 2 && m.Transitions[0].To == "*", "the transition expanded to the montages of the action");
		AnimManifest m2;
		AnimManifestFromJson(AnimManifestToJson(m), m2, &error);
		Check(AnimManifestToJson(m2) == AnimManifestToJson(m) && m2.Transitions.size() == m.Transitions.size() && m2.Montages.back().Curve == m.Montages.back().Curve, "the manifest keeps trims, curves and transitions through its own JSON");

		// attacks live in the C++ rules, not the manifest: a binding adds them to the manifest's abilities, which is where the game already looks
		AnimBindings atk;
		AnimBinding a;
		a.Action = "ability:Attack.axe";
		a.Clips = { { "sword_attack_2" } };
		atk.Bindings.push_back(a);
		AnimManifest m3 = shipped;
		ApplyBindings(m3, atk, defaults);
		GameplayLibrary lib2;
		RegisterCombatContent(lib2, DefaultCombatRules());
		Check(lib2.Ability("Attack.axe")->Montages.size() == 2, "(before) the C++ attack montages");
		const auto pristine = AbilityMontageLists(lib2);
		ApplyManifestAbilities(lib2, m3);
		Check(lib2.Ability("Attack.axe")->Montages == std::vector<std::string>{ "sword_attack_2" } && lib2.Ability("Attack.fists")->Montages.size() == 2, "(after) the axe plays the bound montage, the others keep their defaults");
		Check(RestoreAbilityMontages(lib2, pristine) == 1 && lib2.Ability("Attack.axe")->Montages.size() == 2, "taking the binding off puts the C++ list back (the library is shared and ApplyManifestAbilities only ever writes)");
		Check(MakeBindingDefaults(shipped, std::map<std::string, std::vector<std::string>>{}).Abilities.count("Attack.axe") == 1, "the combat defaults are listed even where the game has not registered combat");

		// locomotion and the jump pose
		AnimBindings loco;
		AnimBinding l;
		l.Action = "loco:default";
		l.Clips = { { "other walk", "walking" } };
		AnimBinding air;
		air.Action = "air";
		air.Clips = { { "hop" } };
		loco.Bindings = { l, air };
		AnimManifest m4 = shipped;
		auto hasClip = [](const std::string& c) { return c == "other walk" || c == "hop"; };
		ApplyBindings(m4, loco, defaults, "", hasClip);
		bool replaced = false;
		for (const std::string& c : m4.Free.Clips) replaced |= c == "other walk";
		Check(replaced && m4.AirClip == "hop", "a locomotion binding replaces one clip of the set, and the air binding the jump");
		AnimManifest m5 = shipped;
		BindingsReport rep5 = ApplyBindings(m5, loco, defaults, "", [](const std::string&) { return false; });
		Check(rep5.Applied == 0 && rep5.Skipped.size() == 2 && AnimManifestToJson(m5) == AnimManifestToJson(shipped), "with the clips missing nothing changes and each skip is reported");
	}

	// ---- validation
	{
		AnimBindings v;
		auto add = [&](const char* action, BindingClip c) { AnimBinding x; x.Action = action; x.Clips.push_back(std::move(c)); v.Bindings.push_back(std::move(x)); };
		BindingClip loopy; loopy.Source = "chop_1"; loopy.Loop = true;
		add("ability:Chop", loopy);
		BindingClip badTrim; badTrim.Source = "chop_1"; badTrim.TrimStart = 0.8f; badTrim.TrimEnd = 0.3f;
		add("ability:Mine", badTrim);
		BindingClip badSpeed; badSpeed.Source = "chop_1"; badSpeed.Speed = 0.0f;
		add("ability:Gather", badSpeed);
		add("ability:Block", { "no_such_clip_anywhere" });
		add("activity:sleep", [] { BindingClip c; c.Source = "act_lie_down"; c.Loop = true; return c; }());
		add("loco:default", { "x", "ghost walk" });
		add("loco:nonexistent", { "x", "y" });
		add("fly:high", { "chop_1" });
		v.Transitions = { Tr("ability:Chop", "activity:nothing_at_all", 0.2f, BlendCurve::Smooth), Tr("ability:Chop", "ability:Chop", 0.2f, BlendCurve::Smooth), Tr("ghost_montage", "*", 0.2f, BlendCurve::Smooth), Tr("ability:Chop", "*", -1.0f, BlendCurve::Smooth) };
		auto hasClip = [&](const std::string& c) { return c.rfind("no_such", 0) != 0; };
		const std::vector<std::string> lines = DescribeIssues(ValidateBindings(v, shipped, defaults, hasClip));
		for (const std::string& l : lines) std::fprintf(stdout, "  issue: %s\n", l.c_str());
		Check(Has(lines, "warning: ability:Chop clip #1: loops, but an ability is a single shot"), "a loop on a single-shot ability is warned");
		Check(!Has(lines, "activity:sleep clip #1: loops"), "a looping activity is fine");
		Check(Has(lines, "error: ability:Mine clip #1: trim end must be after trim start"), "an inverted trim is an error");
		Check(Has(lines, "error: ability:Gather clip #1: speed must be"), "a zero speed is an error");
		Check(Has(lines, "error: ability:Block clip #1: clip 'no_such_clip_anywhere' does not exist"), "a clip that does not exist is an error");
		Check(Has(lines, "loco:default clip #1: replaces 'ghost walk'"), "locomotion that replaces a clip the set lacks is an error");
		Check(Has(lines, "error: loco:nonexistent: no such locomotion set"), "a locomotion set that does not exist is an error");
		Check(Has(lines, "unknown kind of action"), "an unknown kind of action is an error");
		Check(Has(lines, "transition ability:Chop -> activity:nothing_at_all: 'activity:nothing_at_all' plays nothing"), "a transition into nothing is an error (no way out)");
		Check(Has(lines, "transition ghost_montage -> *: 'ghost_montage' plays nothing"), "a transition out of an unknown montage is an error");
		{
			AnimBindings cut;
			AnimBinding cb;
			cb.Action = "ability:Chop";
			BindingClip cc;
			cc.Source = "chop_1"; cc.TrimStart = 0.8f; cc.TrimEnd = 0.95f;   // chop_1 notifies "chop" at 0.4
			cb.Clips.push_back(cc);
			cut.Bindings.push_back(cb);
			Check(Has(DescribeIssues(ValidateBindings(cut, shipped, defaults, hasClip)), "falls outside the trim"), "a trim that cuts out a montage's notify is warned (the ability would never get its event)");
			AnimManifest mm = shipped;
			BindingsReport rr = ApplyBindings(mm, cut, defaults);
			Check(rr.Applied == 1, "(a trim that cuts a notify is still applied: it is only a warning)");
			const MontageDesc* cutAlias = nullptr;
			for (const MontageDesc& d : mm.Montages) if (d.Name == "bind:ability:Chop#0") cutAlias = &d;
			bool inside = cutAlias && !cutAlias->Notifies.empty();
			if (cutAlias) for (const MontageNotify& n : cutAlias->Notifies) inside = inside && n.Time >= 0.8f - 1e-4f && n.Time <= 0.95f + 1e-4f;
			Check(inside, "the notifies a trim leaves outside are moved inside it (the ability still gets its event)");
			cut.Bindings[0].Clips[0].TrimEnd = 0.1f;   // inverted: refused, the default is kept
			AnimManifest m6 = shipped;
			BindingsReport r6 = ApplyBindings(m6, cut, defaults);
			Check(r6.Applied == 0 && r6.Skipped.size() == 1 && AnimManifestToJson(m6) == AnimManifestToJson(shipped), "an inverted trim is skipped by ApplyBindings, which keeps the default");
		}
		Check(Has(lines, "goes from an end to itself") && Has(lines, "duration cannot be negative"), "self transitions and negative durations are reported");
		AnimBindings okay;
		AnimBinding good;
		good.Action = "ability:Chop";
		good.Clips.push_back({ "chop_1" });
		okay.Bindings.push_back(good);
		okay.Transitions.push_back(Tr("ability:Chop", "ability:Block", 0.2f, BlendCurve::Smooth));
		Check(ValidateBindings(okay, shipped, defaults, hasClip).empty(), "a sound binding set validates clean");

		// the default notifies of an ability (hit, chop...) land inside the part of a trimmed montage that plays, in proportion
		GameplayLibrary events;
		AbilityDef swing;
		swing.Name = "Swing";
		swing.Montages = { "cut", "plain" };
		swing.Events = { { "hit", 0.5f, "", {}, {} } };
		events.AddAbility(swing);
		MontageDesc cut, plain;
		cut.Name = "cut"; cut.Clip = "x"; cut.TrimStart = 0.5f; cut.TrimEnd = 1.0f;
		plain.Name = "plain"; plain.Clip = "x";
		MontageDesc aliasOf = cut;
		aliasOf.Name = "bind:ability:Swing?node=tree#0";
		const std::vector<MontageDesc> made = AbilityMontages(events, { cut, plain, aliasOf });
		Check(made.size() == 3 && Near(made[0].Notifies.at(0).Time, 0.75f) && Near(made[1].Notifies.at(0).Time, 0.5f), "the ability's 'hit' at 0.5 of the swing is at 0.75 of the clip when the trim is 0.5..1, and unchanged without a trim");
		Check(Near(made[2].Notifies.at(0).Time, 0.75f), "the montages the bindings make for the ability (by context) get its events too");
	}

	// ---- the animation clock: trim, loop, speed (no window, no model)
	{
		// 2 s clip, trimmed to 0.5..1.5 s
		ClockStep s = AdvanceClipClock(0.5f, 0.25f, 2.0f, 0.25f, 0.75f, false);
		Check(Near(s.Time, 0.75f) && !s.Wrapped && !s.Ended, "the clock advances inside the trim");
		s = AdvanceClipClock(1.4f, 0.3f, 2.0f, 0.25f, 0.75f, false);
		Check(Near(s.Time, 1.5f) && s.Ended && !s.Wrapped, "a one-shot stops at the trim end");
		s = AdvanceClipClock(1.4f, 0.3f, 2.0f, 0.25f, 0.75f, true);
		Check(s.Wrapped && !s.Ended && Near(s.Time, 0.5f + 0.2f), "a loop wraps to the trim start, not to zero");
		s = AdvanceClipClock(0.0f, 0.0f, 2.0f, 0.25f, 0.75f, false);
		Check(Near(s.Time, 0.5f), "a clock outside the trim is pulled to its start");
		Check(Near(ShapeBlend(BlendCurve::Linear, 0.3f), 0.3f) && Near(ShapeBlend(BlendCurve::Smooth, 0.5f), 0.5f) && ShapeBlend(BlendCurve::EaseIn, 0.5f) < 0.5f && ShapeBlend(BlendCurve::EaseOut, 0.5f) > 0.5f
			&& ShapeBlend(BlendCurve::EaseIn, 0.0f) == 0.0f && ShapeBlend(BlendCurve::EaseOut, 1.0f) == 1.0f, "the blend curves are shaped as named and keep their ends");
		Check(BlendCurveFromName(BlendCurveName(BlendCurve::EaseIn)) == BlendCurve::EaseIn && BlendCurveFromName("???") == BlendCurve::Smooth, "curve names round trip, unknown means smooth");
	}

	// ---- the montage player on the test rig: trim / loop / speed / blends / transitions
	auto model = Model::Load(data + "/test_rig.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test rig\n"); return 1; }
	model->AppendAnimations(data + "/test_clip.fbx");
	model->AppendAnimations(data + "/test_idle.fbx");
	const float dt = 1.0f / 60.0f;
	{
		MontagePlayer mp(*model);
		MontageDesc base;
		base.Name = "full"; base.Clip = "test_clip"; base.BlendIn = 0.0f; base.BlendOut = 0.0f;
		base.Notifies = { { 0.1f, "early" }, { 0.9f, "late" } };
		Check(mp.Add(base), "the plain montage is added");
		const float whole = mp.ClipDuration("full");
		Check(whole > 0.3f && Near(mp.Duration("full"), whole) && Near(mp.PlayDuration("full"), whole), "the whole clip has a length (Duration stays the whole clip at the rate)");

		MontageDesc trimmed = base;
		trimmed.Name = "trim"; trimmed.TrimStart = 0.25f; trimmed.TrimEnd = 0.75f;
		Check(mp.Add(trimmed) && Near(mp.PlayDuration("trim"), whole * 0.5f, 1e-3f) && Near(mp.Duration("trim"), whole, 1e-3f), "a trim of half the clip lasts half as long");
		MontageDesc fast = trimmed;
		fast.Name = "fast"; fast.PlayRate = 2.0f;
		Check(mp.Add(fast) && Near(mp.PlayDuration("fast"), whole * 0.25f, 1e-3f), "speed x2 halves it again");

		auto run = [&](const std::string& name, bool pause, int& early, int& late, int& finished, float& firstTime)
		{
			early = late = finished = 0;
			firstTime = -1.0f;
			mp.Play(name);
			for (int i = 0; i < static_cast<int>((mp.PlayDuration(name) + 0.5f) / dt); ++i)
			{
				mp.Update(dt);
				if (firstTime < 0.0f) firstTime = mp.Time(name);
				for (const MontageEvent& e : mp.Events())
				{
					early += e.What == MontageEvent::Kind::Notify && e.Name == "early";
					late += e.What == MontageEvent::Kind::Notify && e.Name == "late";
					finished += e.What == MontageEvent::Kind::Finished;
				}
			}
			(void)pause;
		};
		int early = 0, late = 0, fin = 0;
		float first = 0.0f;
		run("full", false, early, late, fin, first);
		Check(early == 1 && late == 1 && fin == 1, "the whole clip fires both notifies and finishes");
		run("trim", false, early, late, fin, first);
		Check(early == 0 && late == 0 && fin == 1, "the trimmed clip skips the notifies outside it and finishes");
		Check(first >= whole * 0.25f - 1e-3f && first < whole * 0.25f + 0.05f, "and starts at the trim start, not at zero");
		MontageDesc mid = trimmed;
		mid.Name = "mid"; mid.TrimStart = 0.0f; mid.TrimEnd = 0.5f;
		mp.Add(mid);
		run("mid", false, early, late, fin, first);
		Check(early == 1 && late == 0, "a trim that keeps the early notify and drops the late one");

		// the clock props and reach windows read: the fraction of the part that plays
		mp.Play("trim");
		Check(Near(mp.RunFraction("trim"), 0.0f, 0.05f), "a trimmed run starts at 0 of its own clock");
		for (int i = 0; i < static_cast<int>(whole * 0.25f / dt); ++i) mp.Update(dt);
		Check(Near(mp.RunFraction("trim"), 0.5f, 0.1f), "halfway through the trim is 0.5 on the run clock (not 0.5 + the trim start of the whole clip)");
		mp.Stop("trim", 0.0f);
		Check(mp.RunFraction("trim") < 0.0f, "it is -1 when it is not playing");
		mp.Play("full");
		for (int i = 0; i < static_cast<int>(whole * 0.5f / dt); ++i) mp.Update(dt);
		Check(Near(mp.RunFraction("full"), 0.5f, 0.1f), "an untrimmed run: the clock is the clip's, as it was");
		mp.Stop("full", 0.0f);

		// a looping trimmed montage never ends and stays inside the window
		MontageDesc loop = trimmed;
		loop.Name = "loop"; loop.Loop = true;
		mp.Add(loop);
		mp.Play("loop");
		float lo = 1e9f, hi = -1e9f;
		int finishedLoop = 0;
		for (int i = 0; i < static_cast<int>(whole / dt) * 4; ++i)
		{
			mp.Update(dt);
			const float t = mp.Time("loop");
			lo = std::min(lo, t); hi = std::max(hi, t);
			for (const MontageEvent& e : mp.Events()) finishedLoop += e.What == MontageEvent::Kind::Finished;
		}
		Check(lo >= whole * 0.25f - 1e-3f && hi <= whole * 0.75f + 1e-3f && finishedLoop == 0 && mp.IsPlaying("loop"), "a looping trim stays inside its window and never finishes");
		mp.Stop("loop", 0.05f);
		for (int i = 0; i < 20; ++i) mp.Update(dt);

		// blend in / out: the weight follows the time and the curve
		MontageDesc slow = base;
		slow.Name = "blendy"; slow.BlendIn = 0.5f; slow.BlendOut = 0.5f; slow.Curve = BlendCurve::Linear; slow.Loop = true;
		mp.Add(slow);
		mp.Play("blendy");
		for (int i = 0; i < 15; ++i) mp.Update(dt);   // 0.25 s: half way in
		Check(Near(mp.SlotWeight("FullBody"), 0.5f, 0.05f), "the weight is half way after half the blend-in");
		for (int i = 0; i < 60; ++i) mp.Update(dt);
		Check(Near(mp.SlotWeight("FullBody"), 1.0f, 0.01f) || mp.SlotWeight("FullBody") > 0.9f, "and full afterwards (while the clip is long enough)");
		mp.Stop("blendy", 0.1f);
		for (int i = 0; i < 30; ++i) mp.Update(dt);
		Check(!mp.Busy(), "stopping leaves nothing behind");

		// transitions: A -> B uses the pair's duration instead of the montages' own blends
		MontageDesc a2 = base; a2.Name = "A"; a2.Loop = true; a2.BlendIn = 0.0f; a2.BlendOut = 1.0f;
		MontageDesc b2 = base; b2.Name = "B"; b2.Loop = true; b2.BlendIn = 1.0f; b2.BlendOut = 1.0f;
		mp.Add(a2); mp.Add(b2);
		auto weightsAfter = [&](float seconds, float& wa, float& wb)
		{
			mp.Play("A");
			for (int i = 0; i < 20; ++i) mp.Update(dt);
			mp.Play("B");
			for (int i = 0; i < static_cast<int>(seconds / dt); ++i) mp.Update(dt);
			wb = mp.Weight("B");
			wa = mp.Weight("A");
			mp.StopSlot("FullBody", 0.01f);
			for (int i = 0; i < 100; ++i) mp.Update(dt);   // (long enough for A's slow blend out to end too: nothing is left for the next run)
		};
		float wa = 0.0f, wb = 0.0f;
		weightsAfter(0.2f, wa, wb);
		Check(wb < 0.5f, "without a transition B fades in over its own second");
		mp.SetTransitions({ MontageTransition{ "A", "B", 0.1f, BlendCurve::Linear } });
		weightsAfter(0.2f, wa, wb);
		Check(wb > 0.99f && wa < 0.01f, "with an A->B transition of 0.1 s B is in full after 0.2 s and A is gone");
		mp.SetTransitions({ MontageTransition{ "*", "B", 0.1f, BlendCurve::Linear } });
		weightsAfter(0.2f, wa, wb);
		Check(wb > 0.99f, "a wildcard source matches too");
		mp.SetTransitions({ MontageTransition{ "B", "A", 0.1f, BlendCurve::Linear } });
		weightsAfter(0.2f, wa, wb);
		Check(wb < 0.5f, "a transition in the other direction does not");
	}

	if (failures == 0) std::printf("animation bindings: all good\n");
	return failures == 0 ? 0 : 1;
}
