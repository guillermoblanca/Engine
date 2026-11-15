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

	// ---- T236: the vote of a clan (pure): a synthetic clan of 7 voters and the messenger
	{
		auto ballot = [&](float faith)
		{
			DirectorBallot b;
			b.Messenger = "Ama";
			b.Cost = rules.Sign.BuildCost;
			b.Traits = rules.Sign.Traits;
			b.Norms = { { "sharing", 0.5f } };
			int k = 0;
			for (const char* n : { "Ama", "Gil", "Bo", "Cai", "Dun", "Eda", "Fen", "Hal" })   // (out of order: counted by name)
			{
				const float attendance = static_cast<float>(k++ % 3) / 2.0f;
				b.Voters.push_back({ n, faith * (0.5f + 0.5f * attendance), 0.0f, { { "diligence", 0.5f } } });   // DevotionOf's formula
			}
			return b;
		};
		const DirectorVoteResult high = CountDirectorVote(rules.Vote, ballot(0.9f));
		const DirectorVoteResult low = CountDirectorVote(rules.Vote, ballot(0.1f));
		Check(high.Voters == 7 && low.Voters == 7, "the messenger does not vote (7 of the 8 listed)");
		Check(high.Accepted() && high.Yes == 7, "a clan of 7 with faith 0.9 obeys");
		Check(!low.Accepted() && low.Outcome == "resist" && low.Yes == 0, "with faith 0.1 it resists");
		Check(low.Against.size() == 7 && low.Against.front() == "Bo", "the voters against, in name order");
		DirectorBallot shaman = ballot(0.3f);
		const DirectorVoteResult plain = CountDirectorVote(rules.Vote, shaman);
		shaman.MessengerIsShaman = true;
		Check(CountDirectorVote(rules.Vote, shaman).Yes > plain.Yes, "the shaman as messenger weighs more");
		Check(high.Text() == "vote 7 of 7", "the count reads 'vote a of b'");
	}

	// ---- T236 / T237 over a clan of the clan world: the ballot, the preview and the sign's checks
	{
		World v(rules);
		Tribe& ash = v.Folk(0);
		for (const char* n : { "Messa", "Ona", "Pim", "Quel", "Rua", "Sol", "Tor", "Ula" }) ash.AddMember(n, "forager", nullptr);
		std::map<std::string, BuildingDef> defs;
		defs["shelter"] = DefaultBuildings().at("shelter");
		ash.SetBuildings(&defs);
		ash.Social().Beliefs = { { "sky", 0.9f } };
		v.Step(1.0);
		const DirectorVoteResult yes = v.Director.ForeseeVote(v.Id(0), "Messa", rules.Sign.Traits, rules.Sign.BuildCost);
		Check(yes.Voters == 7 && yes.Accepted(), "a clan of 7 adults with faith 0.9 accepts a sign (BallotFor: the messenger out)");
		ash.Social().Beliefs = { { "sky", 0.1f } };
		Check(!v.Director.ForeseeVote(v.Id(0), "Messa", rules.Sign.Traits, rules.Sign.BuildCost).Accepted(), "with faith 0.1 it rejects it");
		ash.Social().Beliefs = { { "sky", 0.9f } };
		const std::string before = v.Director.ToJson();
		const size_t book = v.Director.Book().size();
		const glm::vec2 camp = ash.Config.Economy.Camp;
		const DirectorSignPreview far = v.Director.PreviewSign(v.Id(0), "shelter", camp.x + 500.0f, camp.y);
		Check(!far.Ok && far.Why.find("outside the land") != std::string::npos, "a sign outside the clan's land is refused, with why");
		const DirectorSignPreview near = v.Director.PreviewSign(v.Id(0), "shelter", camp.x + 14.0f, camp.y - 6.0f);
		Check(near.Ok && near.Voters == 7 && !near.Messenger.empty() && Near(near.Cost, rules.Costs["sign"]), "a sign on the camp's ground can be: its cost, its messenger and the vote foreseen");
		Check(near.Outcome == "resist" && near.Warning.find("the store lacks") != std::string::npos, "an empty store in survival weighs against it (missing + survival in the cost)");
		Check(!v.Director.PreviewSign(v.Id(0), "palace", camp.x + 14.0f, camp.y).Ok, "a building that does not exist cannot");
		const DirectorReply said = Run(v.Director, "sign preview Ash shelter camp 14 -6");
		Check(said.Ok && said.Text.find("vote 0 of 7") != std::string::npos, "the preview order says the vote foreseen");
		Check(v.Director.ToJson() == before && v.Director.Book().size() == book && v.Director.Signs().empty(), "a preview changes nothing");
		const DirectorReply far2 = Run(v.Director, "sign Ash shelter camp 500 0");
		Check(!far2.Ok && far2.Text.rfind("cannot: outside the land", 0) == 0 && v.Director.ToJson() == before, "the sign order outside the land: cannot, and nothing changes");
		ash.Social().Beliefs = { { "sky", 0.1f } };
		Check(!Run(v.Director, "sign Ash shelter camp 14 -6").Ok, "a clan with faith under minFaith cannot be given a sign");
		ash.Social().Beliefs = { { "sky", 0.9f } };
		const float influence = v.Director.Influence();
		const DirectorReply sign = Run(v.Director, "sign Ash shelter camp 14 -6");
		Check(sign.Ok && sign.Text.rfind("sign 1 for Ash", 0) == 0 && v.Director.OpenSign(v.Id(0)), "the sign is placed and waits");
		Check(Near(v.Director.Influence(), influence - rules.Costs["sign"]), "and costs its influence");
		Check(!Run(v.Director, "sign Ash shelter camp -14 6").Ok, "one open sign per clan");
		WorldDirector back;   // saving keeps the open sign
		back.Rules = rules;
		back.SetTargets(v.Director.Targets());
		Check(back.FromJson(v.Director.ToJson()) && back.Signs() == v.Director.Signs() && back.OpenSign(v.Id(0)), "saving and loading keeps the open sign");
		// nobody has a body here, so nobody is near: after half a day the most devout dreams of it, then the clan votes at the fire
		v.Step(rules.DaySeconds * 0.5);
		Check(v.Director.Signs().front().Status == "seen" && !v.Director.Signs().front().Messenger.empty(), "with nobody near, after half a day someone dreams of the sign");
		v.Step(rules.DaySeconds * rules.Sign.VoteDays + 1.0);
		Check(v.Director.Signs().front().Voters == 7 && v.Director.Signs().front().Status != "seen", "the clan votes the sign at the fire (7 voters)");   // (no scene: the site cannot open, dropped)
		Check(Run(v.Director, "signs").Text.find("sign 1 Ash shelter") != std::string::npos, "director signs lists it");
	}

	if (failures == 0) std::printf("director: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
