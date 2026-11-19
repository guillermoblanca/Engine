// T341 (documentation/02_tecnico/gameplay/PROPUESTA_ARBOL_2026-11-19.md K-b): one state per technology for every view (GB/Game/TechStatus.h).
// A clan of four with no bodies and a tree in memory, with tiers given by hand (open tier 2):
//   (a) each state: known, studying (the queue's current), queued with its place and what it waits for, own pace (progress, not queued),
//       studiable, blocked by a material at hand (named, with the item), by its tier, not by trying, other clan (with a deal), unknown with
//       its hint (a prerequisite that is named) or without one;
//   (b) the naming rule (D2) and the counters: only the technologies with a tier are shown and counted (D6); the "?" of a tier, era 2 as a block;
//   (c) the views agree with the sim's gate: what is studiable or studying is ResearchQueue::Researchable, ResearchQueue::Open less the queue is
//       exactly the studiable ones (tier open), and ResearchQueue::Hidden with the filter leaves out what is not shown;
//   (d) with no tiers in the tree everything is shown (the trees of the tests, and the contract of `game research`).
#include "GB/Log.h"
#include "GB/Game/ResearchQueue.h"
#include "GB/Game/TechStatus.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Techs.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace GB;

// File-local (T51: several tests share one executable)
namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	TechDef Idea(const std::string& id, std::vector<std::string> needs = {})
	{
		TechDef t;
		t.Id = id;
		t.Name = id;
		t.Requires = std::move(needs);
		t.Complexity = 0.0f;
		t.Discovery.Experiment = 1.0f;
		t.Discovery.Observe = 0.2f;
		t.Discovery.Teach = 0.2f;
		return t;
	}

	struct Camp
	{
		explicit Camp(bool withExtra = true)
		{
			TechDef base = Idea("base");
			base.Known = true;
			TechDef flint = Idea("flint");
			flint.Physical.Materials = { "amber_nowhere" };   // (nobody has it)
			TechDef lore = Idea("lore");
			lore.Discovery.Experiment = 0.0f;                 // (only from others)
			for (const TechDef& t : { base, Idea("alpha", { "base" }), Idea("beta"), Idea("gamma"), flint, lore, Idea("deep", { "alpha" }), Idea("deeper", { "deep" }),
				Idea("pot", { "deep" }), Idea("luck", { "deep" }), Idea("shared", { "deep" }), Idea("ridge"), Idea("era2", { "deep" }) })
				Tree.Techs[t.Id] = t;
			if (withExtra) Tree.Techs["extra"] = Idea("extra", { "deep" });   // (no tier: era3, era4, medieval)
			Folk.SetTechs(&Tree);
			for (const char* n : { "Ana", "Biel", "Cleo", "Dan" }) Folk.AddMember(n, "forager", nullptr);
			const std::pair<const char*, int> tiers[] = { { "base", 0 }, { "alpha", 1 }, { "beta", 1 }, { "gamma", 1 }, { "flint", 1 }, { "lore", 1 }, { "deep", 1 },
				{ "deeper", 2 }, { "pot", 2 }, { "luck", 1 }, { "shared", 2 }, { "ridge", 3 }, { "era2", 4 } };
			for (const auto& [id, tier] : tiers) Tiers.Set(id, tier);
		}
		TechTree Tree;
		Tribe Folk;
		TechTierMap Tiers;
	};

	bool Is(const TechStatus& s, const std::string& tech, TechState state) { return s.Of(tech).State == state; }
}

int GBTest_tech_status(int, char**)
{
	Log::Init();

	Camp c;
	Tribe& t = c.Folk;
	ResearchQueue& q = t.Research();
	Check(q.Add(t, "alpha").Ok && q.Add(t, "beta").Ok && q.Add(t, "pot").Ok, "the queue is alpha, beta, pot");
	Check(!t.Knowledge().Instruct("Ana", "luck", 0.3f, "watching").empty() || t.Knowledge().Of("Ana")->Progress.count("luck"), "Ana has worked out some of luck");
	const std::set<std::string> ceniza = { "base", "shared", "flint" };
	TechStatus::Input in;
	in.Tiers = &c.Tiers;
	in.Neighbours = { { "Ceniza", &ceniza } };
	in.OpenTier = 2;
	const TechStatus s(t, in);
	for (const auto& [id, e] : s.All()) std::printf("  %-7s %s\n", id.c_str(), s.Describe(id).c_str());

	// ---- (a) the states
	Check(Is(s, "base", TechState::Known) && s.Named("base"), "(a) base: known");
	Check(Is(s, "alpha", TechState::Studying) && s.Of("alpha").Queue == 1 && s.Of("alpha").Why == TechWhy::None, "(a) alpha: studying, first of the queue");
	Check(Is(s, "beta", TechState::Queued) && s.Of("beta").Queue == 2 && s.Of("beta").Why == TechWhy::None, "(a) beta: queued 2, nothing missing");
	Check(Is(s, "pot", TechState::Queued) && s.Of("pot").Queue == 3 && s.Of("pot").Why == TechWhy::Requires && s.Of("pot").Missing == std::vector<std::string>{ "deep" } && s.Named("pot"),
		"(a) pot: queued 3, waits for deep, and named (the queue names it)");
	Check(Is(s, "luck", TechState::OwnPace) && s.Of("luck").Progress > 0.0f && s.Named("luck"), "(a) luck: by its own pace, named though deep is unknown");
	Check(Is(s, "gamma", TechState::Studiable) && s.Named("gamma"), "(a) gamma: studiable");
	Check(Is(s, "flint", TechState::Blocked) && s.Of("flint").Why == TechWhy::Material && s.Of("flint").Missing == std::vector<std::string>{ "amber_nowhere" } && s.Named("flint"),
		"(a) flint: blocked, the material at hand missing (and the clan that knows it does not change that)");
	Check(Is(s, "lore", TechState::Blocked) && s.Of("lore").Why == TechWhy::NotByTrying, "(a) lore: blocked, not by trying");
	Check(Is(s, "ridge", TechState::Blocked) && s.Of("ridge").Why == TechWhy::Tier && s.Named("ridge"), "(a) ridge: blocked by its tier (3 > 2)");
	Check(Is(s, "shared", TechState::OtherClan) && s.Of("shared").From == "Ceniza" && s.Named("shared"), "(a) shared: Ceniza knows it");
	Check(Is(s, "deep", TechState::Unknown) && !s.Named("deep") && s.Of("deep").Hint == "alpha", "(a) deep: ?, after alpha (named)");
	Check(Is(s, "deeper", TechState::Unknown) && s.Of("deeper").Hint.empty(), "(a) deeper: ?, no hint (deep is not named)");

	// ---- (b) shown and counted
	Check(!s.Shown("extra") && s.Shown("era2") && s.Shown("base"), "(b) a technology without a tier is not shown");
	Check(s.Total() == 13 && s.KnownCount() == 1 && s.StudiableCount() == 1, "(b) 1 of 13 known, 1 studiable");
	Check(s.UnnamedCount() == 3 && s.UnnamedIn(1) == 1 && s.UnnamedIn(2) == 1 && s.UnnamedIn(TechStatus::kEraBlockTier) == 1, "(b) ? 3: deep (tier 1), deeper (2), era 2 as a block");

	// ---- (c) the same as the sim's gate
	{
		bool agree = true;
		for (const auto& [id, e] : s.All())
			if ((e.State == TechState::Studiable || e.State == TechState::Studying) && !ResearchQueue::Researchable(t, id)) { agree = false; std::fprintf(stderr, "  %s\n", id.c_str()); }
		Check(agree, "(c) studiable and studying are researchable for the sim");
		TechStatus::Input open = in;
		open.OpenTier = 99;
		const TechStatus all(t, open);
		std::vector<std::string> studiable;
		for (const auto& [id, e] : all.All()) if (e.State == TechState::Studiable) studiable.push_back(id);
		Check(ResearchQueue::Open(t, q.All()) == studiable, "(c) what the queue offers is what the tree calls studiable");
		Check(Is(all, "ridge", TechState::Studiable), "(c) with every tier open ridge is studiable");
		const int hidden = ResearchQueue::Hidden(t), shownHidden = ResearchQueue::Hidden(t, s.ShownFilter());
		Check(hidden == shownHidden + 1, "(c) Hidden with the filter leaves out the technology without a tier");
		Check(q.List(t).find("? " + std::to_string(hidden) + " more") != std::string::npos && q.List(t, s.ShownFilter()).find("? " + std::to_string(shownHidden) + " more") != std::string::npos,
			"(c) game research counts with the filter");
	}

	// ---- (d) no tiers: everything shown
	{
		Camp d;
		const TechTierMap none;
		TechStatus::Input in2;
		in2.Tiers = &none;
		const TechStatus s2(d.Folk, in2);
		Check(s2.Shown("extra") && s2.Total() == static_cast<int>(d.Tree.Techs.size()), "(d) without tiers every technology is shown");
		Check(ResearchQueue::Hidden(d.Folk, s2.ShownFilter()) == ResearchQueue::Hidden(d.Folk), "(d) and Hidden does not change");
		const TechStatus shipped(d.Folk, {});
		Check(shipped.Total() == static_cast<int>(d.Tree.Techs.size()), "(d) the shipped tiers do not name this tree's ids: everything shown");
	}

	if (failures == 0) std::printf("tech_status: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
