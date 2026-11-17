// T240 (documentation/01_diseno/MODO_DIOS_DECISIONES.md §3.c): the vision. Checked: the "vision" section of assets/data/director.json loads and
// costs 10; the preview says the way, the dreamer, the vote and the material the clan lacks, and changes nothing; a vision is paid and sets the
// clan's aim with the dreamer as who asked (Tribe::SetAim, no player); without its material the clan cannot learn it (the physical gate holds);
// a clan of low faith refuses it; a second vision waits for the first; the aim lapses after vision.days with no step forward; a step forward
// keeps it; learning it closes it (fulfilled); saving and loading keeps it; `none` lets it go. CPU only: clans with no bodies.
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
	bool Has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

	struct World
	{
		GB::ClanWorld Clans;
		GB::WorldDirector Director;
		GB::TechTree Tree;
		double Clock = 0.0;
		World(const GB::DirectorRules& rules)
		{
			GB::TechDef fire;
			fire.Id = "fire";
			fire.Name = "Fire";
			fire.Known = true;
			GB::TechDef ideas;
			ideas.Id = "ideas";
			ideas.Name = "Ideas";
			ideas.Requires = { "fire" };
			GB::TechDef chant;
			chant.Id = "chant";
			chant.Name = "Chant";
			GB::TechDef song;
			song.Id = "song";
			song.Name = "Song";
			song.Requires = { "chant" };
			GB::TechDef pottery;
			pottery.Id = "pottery";
			pottery.Name = "Pottery";
			pottery.Requires = { "fire" };
			pottery.Physical.Materials = { "clay" };
			for (const GB::TechDef& t : { fire, ideas, chant, song, pottery }) Tree.Techs[t.Id] = t;
			for (const char* name : { "Ash", "Reed" })
			{
				GB::Clan& c = Clans.Add(name);
				c.Folk->Config.Social = GB::DefaultSocialRules();
			}
			for (const char* n : { "Messa", "Ona", "Pim", "Quel", "Rua", "Sol" }) Folk(0).AddMember(n, "forager", nullptr);
			Folk(0).Config.Work.Jobs.SurvivalDays = 0.0f;   // (fed: not in survival, where the clan puts eating first and resists)
			Folk(0).SetTechs(&Tree);
			Folk(1).SetTechs(&Tree);
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

int GBTest_director_vision(int, char**)
{
	using namespace GB;
	// ---- the data
	std::ifstream f(std::string(GB_ASSETS_DIR) + "/data/director.json");
	std::stringstream ss;
	ss << f.rdbuf();
	DirectorRules rules;
	Check(!ss.str().empty() && LoadDirectorRules(ss.str(), rules).empty(), "assets/data/director.json loads with its vision");
	Check([&] { DirectorRules r = rules; r.Stages = {}; return r; }() == DefaultDirectorRules(), "and says what DefaultDirectorRules says (D48: but the stages, only data)");
	Check(Near(rules.Costs["vision"], 10.0f) && rules.Waits.count("vision") && Near(rules.Vision.Days, 3.0f), "the vision costs 10 and lapses in 3 days");
	DirectorRules bad = rules;
	Check(!LoadDirectorRules("{\"director\":{\"vision\":{\"dream\":1}}}", bad).empty() && bad == rules, "an unknown key of the vision is refused");

	World w(rules);
	w.Step(1.0);
	w.Step(rules.DaySeconds);
	Check(w.Folk(0).ResearchAim().empty() && w.Director.Visions().empty(), "with no order there is no aim and no vision");

	// ---- low faith refuses
	Check(Run(w.Director, "sandbox on").Ok, "sandbox on");
	Check(Run(w.Director, "faith Ash 0.05").Ok, "faith of Ash set low");
	const DirectorReply weak = Run(w.Director, "vision Ash ideas");
	Check(!weak.Ok && Has(weak.Text, "does not believe enough") && w.Folk(0).ResearchAim().empty(), "a clan of low faith refuses the vision, with why");
	Check(Run(w.Director, "faith Ash 1").Ok, "faith of Ash set high");

	// ---- the preview changes nothing
	const std::string still = w.Director.ToJson();
	const DirectorReply look = Run(w.Director, "vision preview Ash pottery");
	Check(look.Ok && Has(look.Text, ": ok") && Has(look.Text, "the way: pottery") && Has(look.Text, "they lack clay") && Has(look.Text, "dreamer "), "the preview of pottery: ok, its way, the dreamer and the clay it lacks");
	Check(w.Director.ToJson() == still && w.Folk(0).ResearchAim().empty(), "and changes nothing");
	const DirectorVisionPreview p = w.Director.PreviewVision(w.Id(0), "song");
	Check(p.Ok && p.Way.size() == 2 && p.Way[0] == "chant" && p.Way[1] == "song" && p.Missing.empty() && p.Inventor && Near(p.Cost, 10.0f), "the way of song: chant first, then song; nothing physical lacks");
	Check(!w.Director.PreviewVision(w.Id(0), "fire").Ok && !w.Director.PreviewVision(w.Id(0), "nothing").Ok, "nothing known or unknown to the tree can be dreamt");

	// ---- without its material: the aim is set, but the clan cannot learn it
	const float before = w.Director.Influence();
	const DirectorReply clay = Run(w.Director, "vision Ash pottery");
	Check(clay.Ok && Has(clay.Text, "they lack clay") && w.Folk(0).ResearchAim() == "pottery", "a vision of pottery without clay: the aim is set and the reply says what lacks");
	Check(Near(w.Director.Influence(), before - rules.Costs["vision"]), "it is paid");
	const DirectorVision* held = w.Director.VisionOf(w.Id(0));
	Check(held && held->Tech == "pottery" && w.Folk(0).ResearchAimBy() == held->Dreamer && !held->Dreamer.empty(), "the dreamer is who asked, as with game research");
	bool canLearn = false;
	for (const auto& [name, k] : w.Folk(0).Knowledge().Agents()) canLearn = canLearn || w.Folk(0).Knowledge().CanLearn(name, "pottery");
	Check(!canLearn, "without clay nobody of the clan can learn pottery (the physical gate holds)");
	Check(!Run(w.Director, "vision Ash ideas").Ok, "one vision at a time: a second one waits for the first");

	// ---- saving and loading
	const std::string saved = w.Director.ToJson();
	WorldDirector back;
	back.Rules = rules;
	back.SetTargets(w.Director.Targets());
	Check(back.FromJson(saved) && back.ToJson() == saved && back.VisionOf(w.Id(0)), "saving and loading keeps the vision");
	const std::string tribe = w.Folk(0).ToJson();
	Check(Has(tribe, "\"aim\":\"pottery\""), "the tribe saves the aim with no player in it");

	// ---- the lapse: no step forward in vision.days
	w.Step(rules.DaySeconds * 1.0);
	Check(w.Director.VisionOf(w.Id(0)) != nullptr, "a day with no step forward: still the aim");
	w.Step(rules.DaySeconds * (rules.Vision.Days + 0.1));
	Check(!w.Director.VisionOf(w.Id(0)) && w.Folk(0).ResearchAim().empty() && w.Director.Visions().back().Status == "lapsed", "after vision.days with no step forward the aim lapses");

	// ---- a step forward keeps it; learning it closes it
	Check(Run(w.Director, "vision Ash song").Ok && w.Folk(0).ResearchAim() == "song", "a vision of song");
	w.Step(rules.DaySeconds * 2.0);
	Check(w.Folk(0).Knowledge().GrantClan("chant").empty(), "the clan learns chant (a step on the way)");
	w.Step(1.0);
	w.Step(rules.DaySeconds * 2.0);
	Check(w.Director.VisionOf(w.Id(0)) != nullptr, "a step forward on the way keeps the vision past its days");
	const float gained = w.Director.Influence();
	Check(w.Folk(0).Knowledge().GrantClan("song").empty(), "the clan learns song");
	w.Step(1.0);
	Check(!w.Director.VisionOf(w.Id(0)) && w.Director.Visions().back().Status == "learnt" && w.Director.Influence() >= gained, "learning it closes the vision: fulfilled");

	// ---- none lets it go
	Check(Run(w.Director, "vision Ash ideas").Ok && Run(w.Director, "vision Ash none").Ok && w.Folk(0).ResearchAim().empty(), "vision none lets it go and the clan chooses again");
	Check(Run(w.Director, "visions").Ok, "director visions lists them");
	return failures == 0 ? 0 : 1;
}
