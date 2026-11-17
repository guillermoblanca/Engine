// T238 + T239 (documentation/01_diseno/MODO_DIOS_DECISIONES.md §3.b): the focus of a clan and the calling of a person. Checked: the "focus" and
// "calling" sections of assets/data/director.json load (a mistake is refused whole); with no order no tribe gets a bias; a focus is paid, writes
// the clan's TribeBias (the group's options, m(p) > 1 for the believers), pays its upkeep and moves the group's norm each day, and ends when its
// days are over or the god lets it go; in "survival" only the food can be the focus, and a focus already set pauses (no bias, no upkeep) until
// the clan eats again; saving and loading keeps it and writes the bias again; a calling with a disposition > 0 writes the person's calling into
// the bias, with a disposition <= 0 it does not (half the cost back), and its preview changes nothing. CPU only: clans with no bodies.
#include "GB/Game/Clans.h"
#include "GB/Game/Social.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/WorldDirector.h"
#include <algorithm>
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
	bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

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
				GB::TribeRole crafter;
				crafter.Skill = "crafting";
				c.Folk->Config.Roles["crafter"] = crafter;
				GB::TribeRole forager;
				forager.Skill = "foraging";
				c.Folk->Config.Roles["forager"] = forager;
				c.Folk->Config.Society.Culture.Norms["inquiry"] = GB::NormDef{};   // (the drift moves a norm the content has)
			}
			for (const char* n : { "Messa", "Ona", "Pim", "Quel", "Rua", "Sol" }) Folk(0).AddMember(n, "forager", nullptr);
			Director.Rules = rules;
			Director.Clear();
			Director.SetTargets({ &Clans, [this] { return Clock; } });
		}
		GB::Tribe& Folk(int i) { return *Clans.All()[static_cast<size_t>(i)]->Folk; }
		int Id(int i) const { return Clans.All()[static_cast<size_t>(i)]->Id; }
		void Step(double seconds) { Clock += seconds; Director.Update(); }
		void Fed(bool fed) { Folk(0).Config.Work.Jobs.SurvivalDays = fed ? 0.0f : 3.0f; }   // (no food in the store: survival unless no days are wanted)
	};
	GB::DirectorReply Run(GB::WorldDirector& d, const std::string& line)
	{
		std::vector<std::string> words;
		std::istringstream in(line);
		for (std::string w; in >> w;) words.push_back(w);
		return d.Command(words);
	}
	bool Has(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}

int GBTest_director_focus(int, char**)
{
	using namespace GB;
	// ---- the data
	std::ifstream f(std::string(GB_ASSETS_DIR) + "/data/director.json");
	std::stringstream ss;
	ss << f.rdbuf();
	DirectorRules rules;
	Check(!ss.str().empty() && LoadDirectorRules(ss.str(), rules).empty(), "assets/data/director.json loads with its focus and calling");
	Check(rules == DefaultDirectorRules(), "and says what DefaultDirectorRules says");
	Check(Near(rules.Costs["focus"], 10.0f) && Near(rules.Costs["focus_strong"], 20.0f) && Near(rules.Costs["calling"], 15.0f), "the focus costs 10 (strong 20), the calling 15");
	Check(rules.Focus.Groups.size() == 5 && Has(rules.Focus.Groups["lore"].Kinds, "experiment") && rules.Focus.Groups["food"].FoodGather, "five groups; lore pushes experiment, food the gathering of food");
	DirectorRules bad = rules;
	Check(!LoadDirectorRules("{\"director\":{\"focus\":{\"soft\":0.5}}}", bad).empty() && bad == rules, "a focus that would lower the options is refused and changes nothing");
	Check(!LoadDirectorRules("{\"director\":{\"calling\":{\"dream\":1}}}", bad).empty() && bad == rules, "an unknown key of the calling is refused");

	// ---- no order: no bias
	World w(rules);
	w.Fed(true);
	w.Step(1.0);
	w.Step(rules.DaySeconds);
	Check(!w.Folk(0).Bias.Focus.Active() && w.Folk(0).Bias.Callings.empty() && !w.Folk(1).Bias.Focus.Active(), "with no order no tribe gets a bias");
	Check(Run(w.Director, "sandbox on").Ok, "sandbox on (every clan believes)");

	// ---- survival: only the food
	w.Fed(false);
	Check(w.Folk(0).MaslowTier() == "survival", "a clan with an empty store lives in survival");
	const DirectorReply hungry = Run(w.Director, "focus Ash lore");
	Check(!hungry.Ok && hungry.Text.find("survival") != std::string::npos, "in survival the focus on lore is refused, with why");
	const DirectorReply food = Run(w.Director, "focus Ash food 2");
	Check(food.Ok && w.Folk(0).Bias.Focus.Active() && w.Folk(0).Bias.Focus.Group == "food" && w.Folk(0).Bias.Focus.FoodGather, "the focus on food can be set in survival: the bias is written");
	Check(Run(w.Director, "focus Ash off").Ok && !w.Folk(0).Bias.Focus.Active() && !w.Director.FocusOf(w.Id(0)), "focus off ends it and clears the bias");

	// ---- the focus on lore: bias, upkeep, the mark on the culture, the end
	w.Fed(true);
	const float before = w.Director.Influence();
	const DirectorReply lore = Run(w.Director, "focus Ash lore 2");
	Check(lore.Ok && Near(w.Director.Influence(), before - rules.Costs["focus"]), "the focus on lore is paid");
	const TribeFocusBias& bias = w.Folk(0).Bias.Focus;
	Check(bias.Active() && Has(bias.Kinds, "experiment") && Has(bias.Kinds, "learn") && !bias.FoodGather, "the bias pushes the options of lore");
	bool above = !bias.Mult.empty();
	for (const auto& [who, m] : bias.Mult) above = above && m > 1.0f && m <= rules.Focus.Soft + 1e-4f;
	Check(above && bias.Mult.size() == 6, "every believer follows it: 1 < m(p) <= soft");
	float mean = 0.0f;
	for (const auto& [who, m] : bias.Mult) mean += m / static_cast<float>(bias.Mult.size());
	std::fprintf(stderr, "  (mean m(p) of the believers %.2f)\n", mean);
	Check(mean >= 1.25f, "D43: the focus shows (a soft focus pushes a believer more than x1.25 on average: less barely moved the time in lore)");
	Check(!w.Folk(1).Bias.Focus.Active(), "the other clan has no bias");
	const float inquiry = w.Folk(0).People().Norm(w.Folk(0).Config.Society, "inquiry");
	w.Step(rules.DaySeconds);   // a day turns
	Check(Near(w.Folk(0).People().Norm(w.Folk(0).Config.Society, "inquiry"), std::min(1.0f, inquiry + rules.Focus.Drift), 2e-3f), "a day of focus moves the group's norm (inquiry) by the drift");
	bool upkeep = false;
	for (const DirectorEntry& e : w.Director.Book()) upkeep = upkeep || (e.Action == "focus_upkeep" && Near(e.Cost, rules.Focus.UpkeepSoft));
	Check(upkeep, "and pays its upkeep");
	// survival pauses it: no bias, no upkeep, no drift
	w.Fed(false);
	w.Step(1.0);
	Check(w.Director.FocusOf(w.Id(0)) && w.Director.FocusOf(w.Id(0))->Paused && !w.Folk(0).Bias.Focus.Active(), "a hungry clan pauses the focus on lore (survival overrides it)");
	w.Fed(true);
	w.Step(1.0);
	Check(w.Folk(0).Bias.Focus.Active(), "and it comes back when the clan eats again");
	// save and load
	const std::string saved = w.Director.ToJson();
	WorldDirector back;
	back.Rules = rules;
	back.SetTargets(w.Director.Targets());
	w.Folk(0).Bias = TribeBias{};
	Check(back.FromJson(saved) && back.ToJson() == saved && back.FocusOf(w.Id(0)) && w.Folk(0).Bias.Focus.Active(), "saving and loading keeps the focus and writes the bias again");
	w.Step(rules.DaySeconds * 1.5);
	Check(!w.Director.FocusOf(w.Id(0)) && !w.Folk(0).Bias.Focus.Active(), "the focus ends when its days are over");

	// ---- the calling
	const std::string still = w.Director.ToJson();
	const DirectorReply yes = Run(w.Director, "calling preview Messa crafter");
	Check(yes.Ok && yes.Text.find("would accept") != std::string::npos && w.Director.ToJson() == still, "the preview of a calling says whether it would be accepted and changes nothing");
	const DirectorReply given = Run(w.Director, "calling Messa crafter");
	Check(given.Ok && w.Folk(0).Bias.Callings.count("Messa") && w.Folk(0).Bias.CallingSkill("Messa", "crafter") == rules.Calling.Skill, "a believer takes the calling: the review of the trades will see its points");
	Check(Near(w.Folk(0).Bias.CallingSkill("Messa", "forager"), 0.0f), "only in that trade");
	Check(!Run(w.Director, "calling Messa forager").Ok, "nobody is called to the trade it already has");
	Check(Run(w.Director, "faith Ash 0.2").Ok, "a clan that barely believes (faith 0.2: the hand still reaches it)");
	const DirectorReply no = Run(w.Director, "calling preview Ona crafter");
	Check(no.Ok && no.Text.find("would not") != std::string::npos, "the preview says it would not");
	const float influence = w.Director.Influence();
	const DirectorReply ignored = Run(w.Director, "calling Ona crafter");
	Check(ignored.Ok && ignored.Text.find("pays no heed") != std::string::npos && !w.Folk(0).Bias.Callings.count("Ona"), "with a disposition <= 0 the person does not take it");
	Check(Near(w.Director.Influence(), influence - rules.Costs["calling"] * (1.0f - rules.Calling.Refund)), "and half the cost comes back");
	w.Step(rules.DaySeconds * (rules.Calling.Days + 1.0f));
	Check(w.Folk(0).Bias.Callings.empty() && w.Director.Callings().empty(), "the calling ends after its days");

	if (failures == 0) std::printf("director_focus: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
