// T195 (documentation/01_diseno/MODO_DIOS.md §3.4, MODO_DIOS_DECISIONES.md §4): the god's director. Checked: assets/data/director.json loads and
// says what the code says, a mistake is refused whole; the faith of a clan is its dominant belief and the influence grows from it (rites held,
// omens read, the living x their devotion every day); a decision of the hand is refused on a clan with faith under minFaith (with why) and
// changes nothing; the omen order is a natural event (no faith needed), costs its influence and waits a day on the same clan; the sandbox
// refills on a fixed clock and lets the faith be set; saving and loading keeps influence, pressure, waits and the book; two runs of the same
// orders end the same. CPU only: clans of the clan world with no bodies.
#include "GB/Game/Clans.h"
#include "GB/Game/Social.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/WorldDirector.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

	struct World
	{
		GB::ClanWorld Clans;
		GB::WorldDirector Director;
		double Clock = 0.0;
		World(const GB::DirectorRules& rules)
		{
			for (const char* name : { "Ash", "Reed" })
			{
				GB::Clan& c = Clans.Add(name);
				c.Folk->Config.Social = GB::DefaultSocialRules();
			}
			Director.Rules = rules;
			Director.Clear();
			Director.SetTargets({ &Clans, [this] { return Clock; } });
		}
		GB::Tribe& Folk(int i) { return *Clans.All()[static_cast<size_t>(i)]->Folk; }
		int Id(int i) const { return Clans.All()[static_cast<size_t>(i)]->Id; }
		void Step(double seconds) { Clock += seconds; Director.Update(); }
	};
	GB::DirectorReply Run(GB::WorldDirector& d, const std::string& line)
	{
		std::vector<std::string> words;
		std::istringstream in(line);
		for (std::string w; in >> w;) words.push_back(w);
		return d.Command(words);
	}
}

int GBTest_director(int, char**)
{
	using namespace GB;
	// ---- the data
	std::ifstream f(std::string(GB_ASSETS_DIR) + "/data/director.json");
	std::stringstream ss;
	ss << f.rdbuf();
	DirectorRules rules;
	Check(!ss.str().empty() && LoadDirectorRules(ss.str(), rules).empty(), "assets/data/director.json loads");
	Check(rules == DefaultDirectorRules(), "and says what DefaultDirectorRules says");
	Check(Near(rules.MinFaith, 0.2f) && Near(rules.Costs["omen"], 10.0f), "minFaith 0.2, the omen costs 10");
	DirectorRules bad = rules;
	Check(!LoadDirectorRules("{\"director\":{\"minFaith\":2}}", bad).empty() && bad == rules, "a faith out of 0..1 is refused and changes nothing");
	Check(!LoadDirectorRules("{\"director\":{\"faith\":{\"prayer\":1}}}", bad).empty() && bad == rules, "an unknown key is refused");

	// ---- faith -> influence
	World w(rules);
	w.Folk(0).Social().Beliefs = { { "sky", 0.6f }, { "earth", 0.3f } };
	w.Folk(1).Social().Beliefs = { { "sky", 0.1f } };
	Check(Near(w.Director.FaithOf(w.Id(0)), 0.6f) && Near(w.Director.FaithOf(w.Id(1)), 0.1f), "the faith of a clan is its dominant belief");
	Check(Near(w.Director.Influence(), rules.Start), "a new world starts with the start influence");
	w.Step(1.0);   // the first look takes the counters
	SocialState& s0 = w.Folk(0).Social();
	s0.Members["Ama"].Rites = 4;   // a rite with four there
	s0.Members["Biel"].Rites = 0;
	s0.Counts["rite.funeral"] = 1;
	w.Step(1.0);
	Check(Near(w.Director.Influence(), rules.Start + rules.RiteGain * 4.0f * 0.6f), "a rite held adds its attendees x the clan's faith");
	Check(w.Director.DevotionOf(w.Id(0), "Ama") > w.Director.DevotionOf(w.Id(0), "Biel"), "who goes to the rites is more devout");
	const float before = w.Director.Influence();
	w.Step(1.0);
	Check(Near(w.Director.Influence(), before), "no rite, no omen, the same day: the influence stays (no clock)");

	// ---- the hand needs faith; the omen does not
	const float pressure1 = w.Director.PressureOf(w.Id(1));
	const DirectorCheck denied = w.Director.Spend("omen", w.Id(1), true, "a decision on Reed");
	Check(!denied.Ok && denied.Why.find("does not believe enough") != std::string::npos, "a decision of the hand on a clan with faith 0.1 is refused, with why");
	Check(Near(w.Director.Influence(), before) && Near(w.Director.PressureOf(w.Id(1)), pressure1) && w.Director.Book().empty(), "and a refusal changes nothing");
	const DirectorCheck hand = w.Director.Spend("vote", w.Id(0), true, "a decision on Ash");
	Check(hand.Ok && Near(w.Director.PressureOf(w.Id(0)), rules.PressureDecision), "a decision on a believing clan is paid and adds pressure");
	const int omens = w.Folk(1).Social().Counts["omen.eclipse"];
	const DirectorReply omen = Run(w.Director, "omen eclipse Reed");
	Check(omen.Ok && w.Folk(1).Social().Counts["omen.eclipse"] == omens + 1, "the omen falls on an unbelieving clan (a natural event): Tribe::Omen read it");
	Check(Near(w.Director.Influence(), before - rules.Costs["omen"]), "and costs its influence");
	const DirectorReply again = Run(w.Director, "omen eclipse Reed");
	Check(!again.Ok && again.Text.find("wait") != std::string::npos, "the same omen on the same clan waits a day");
	w.Step(rules.DaySeconds);   // a day: the omen read counts, the daily gain, the pressure decays
	Check(w.Director.Influence() > before - rules.Costs["omen"], "the omen read and the devotion of the day add influence");
	Check(Run(w.Director, "omen eclipse Reed").Ok, "after a day the omen can fall again");
	Check(!Run(w.Director, "faith Reed 0.5").Ok, "the faith is set only in the sandbox");

	// ---- saving and loading
	w.Director.AddPressure(w.Id(1), 3.0f, "insists");
	const std::string saved = w.Director.ToJson();
	WorldDirector back;
	back.Rules = rules;
	back.SetTargets(w.Director.Targets());
	Check(back.FromJson(saved), "the director reads back");
	Check(Near(back.Influence(), w.Director.Influence()) && Near(back.PressureOf(w.Id(1)), w.Director.PressureOf(w.Id(1))) && back.Book() == w.Director.Book(), "influence, pressure and book read back the same");
	Check(back.WaitLeft("omen", w.Id(1)) > 0.0 && back.ToJson() == saved, "and the waits: the same state");
	Check(!back.FromJson("[1,2]") && back.ToJson() == saved, "an unreadable save changes nothing");

	// ---- the sandbox
	Check(Run(w.Director, "sandbox on").Ok && w.Director.Sandbox(), "sandbox on");
	Check(Run(w.Director, "faith Reed 0.5").Ok && Near(w.Director.FaithOf(w.Id(1)), 0.5f), "in the sandbox the faith can be set");
	const float lab = w.Director.Influence();
	w.Step(rules.DaySeconds / 2.0);
	Check(Near(w.Director.Influence(), std::min(rules.Max, lab + rules.SandboxPerDay * 0.5f)), "in the sandbox the influence refills on a fixed clock");
	Check(Run(w.Director, "omen eclipse Reed").Ok, "and there are no waits");
	Run(w.Director, "sandbox off");
	Check(Near(w.Director.FaithOf(w.Id(1)), w.Folk(1).Social().Beliefs["sky"]) && w.Folk(1).Social().Beliefs["sky"] > 0.1f, "out of the sandbox the clan's own faith is back (the eclipses raised its sky)");
	Check(Run(w.Director, "status").Text.find("clan ") != std::string::npos, "status names the clans");

	// ---- the same orders, the same end (what a replay checks)
	auto play = [&]()
	{
		World r(rules);
		r.Folk(0).Social().Beliefs = { { "sky", 0.5f } };
		r.Folk(1).Social().Beliefs = { { "earth", 0.3f } };
		r.Step(1.0);
		Run(r.Director, "omen eclipse all");
		r.Step(rules.DaySeconds);
		Run(r.Director, "omen eclipse clan Ash");
		r.Step(10.0);
		return r.Director.ToJson() + r.Folk(0).Social().ToJson() + r.Folk(1).Social().ToJson();
	};
	Check(play() == play(), "two runs of the same orders end the same");

	if (failures == 0) std::printf("director: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
