// T219 + T220 (U3 and U4 of documentation/01_diseno/MODO_DIOS_UX.md 3.4 and 3.7): the winter's outlook and the god's alerts. On the CPU, with no window:
//  * WinterOutlook: the examples of 3.4 (6 days of food 6 days before the winter: 14 short; 24 at 6: covered; 2 at 6: 18; 4 at 3: 13) and in the winter;
//  * WorldAlerts on synthetic clans: 2 days of food is hunger (critical), 6 days at 6 days from the winter is the winter's alert, a well-fed clan in
//    the spring has none; a clan with nobody alive is extinct; raids and killings between clans are one war per pair, a wolf's is not; a discovery
//    is an inventor (important);
//  * AlertBoard: the same alert is not repeated (its number is updated), a dismissed one stays away while it holds and comes back when it holds
//    again, a condition that clears goes, a war goes after its days;
//  * the "alerts" section reads, writes back and refuses what it does not know; the game's data loads.
#include "GB/Game/History.h"
#include "GB/Game/WinterOutlook.h"
#include "GB/Game/WorldAlerts.h"
#include "GB/Gameplay/Ecology.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
		else std::printf("ok: %s\n", what.c_str());
	}
	bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
	constexpr double kDay = 1200.0;
	constexpr double kWinter = 3.0 * 16800.0;   // the winter begins after three seasons of 14 days

	const GB::Alert* Find(const std::vector<GB::Alert>& all, const std::string& key)
	{
		for (const GB::Alert& a : all) if (a.Key == key) return &a;
		return nullptr;
	}
}

using namespace GB;

int GBTest_world_alerts(int, char**)
{
	const SeasonClock seasons;   // 14-day seasons of 1200 s days
	// ---- T219: the winter's outlook
	{
		const WinterOutlook w = ForecastWinter(seasons, kWinter - 6.0 * kDay, 1200.0f, 6.0f);
		Check(!w.In && Near(w.ToWinter, 6.0f) && Near(w.UntilSpring, 20.0f) && Near(w.Short, 14.0f) && !w.Covered(),
			"6 days of food 6 days before the winter: 20 until the spring, 14 short (" + std::to_string(w.Short) + ")");
		Check(ForecastWinter(seasons, kWinter - 6.0 * kDay, 1200.0f, 24.0f).Covered(), "24 days at 6: covered");
		Check(Near(ForecastWinter(seasons, kWinter - 6.0 * kDay, 1200.0f, 2.0f).Short, 18.0f), "2 days at 6: 18 short");
		Check(Near(ForecastWinter(seasons, kWinter - 3.0 * kDay, 1200.0f, 4.0f).Short, 13.0f), "4 days at 3: 17 until the spring, 13 short");
		const WinterOutlook in = ForecastWinter(seasons, kWinter + 4.0 * kDay, 1200.0f, 3.0f);
		Check(in.In && in.ToWinter == 0.0f && Near(in.UntilSpring, 10.0f) && Near(in.Short, 7.0f), "4 days into the winter: 10 left of it, 3 of food: 7 short");
		const WinterOutlook spring = ForecastWinter(seasons, 0.0, 1200.0f, 6.0f);
		Check(!spring.In && Near(spring.ToWinter, 42.0f) && Near(spring.UntilSpring, 56.0f), "the first morning of the spring: 42 days to the winter, 56 to the next spring");
		Check(ForecastWinter(seasons, kWinter - 6.0 * kDay, 1200.0f, -3.0f).FoodDays == 0.0f, "no food is 0 days, never less");
	}

	// ---- T220: the alerts of a look
	AlertRules rules;   // hunger under 3 days, the winter within 7
	auto clan = [&](int id, const char* name, int alive, float food, double now)
	{
		ClanAlertState c;
		c.Clan = id;
		c.Name = name;
		c.Alive = alive;
		c.FoodDays = food;
		c.Winter = ForecastWinter(seasons, now, 1200.0f, food);
		c.Camp = glm::vec2(10.0f * static_cast<float>(id), 0.0f);
		return c;
	};
	const double now = kWinter - 6.0 * kDay;
	AlertInput in;
	in.Now = now;
	in.Clans = { clan(0, "Clan 1", 6, 2.0f, now), clan(1, "Reed", 6, 6.0f, now), clan(2, "Ash", 6, 40.0f, now) };
	std::vector<Alert> found = WorldAlerts(in, rules);
	const Alert* hunger = Find(found, "hunger:0");
	Check(hunger && hunger->Level == AlertLevel::Critical && Near(hunger->Value, 2.0f) && hunger->HasWhere && hunger->Where == glm::vec2(0.0f),
		"a clan with 2 days of food: a critical hunger alert at its camp" + (hunger ? ": " + hunger->Text : std::string()));
	Check(!Find(found, "hunger:1") && !Find(found, "hunger:2"), "6 and 40 days of food: no hunger");
	const Alert* winter = Find(found, "winter:1");
	Check(winter && winter->Level == AlertLevel::Critical && Near(winter->Value, 14.0f) && Near(winter->Value2, 6.0f) && Near(winter->Value3, 6.0f),
		"6 days of food 6 days from the winter: the winter's alert, 14 short" + (winter ? ": " + winter->Text : std::string()));
	Check(Find(found, "winter:0") != nullptr && !Find(found, "winter:2"), "the hungry clan is short for the winter too; the one with 40 days is covered");
	Check(found.size() == 3, "three alerts in all (" + std::to_string(found.size()) + ")");
	{
		AlertInput spring = in;
		spring.Now = 0.0;
		spring.Clans = { clan(1, "Reed", 6, 0.0f, 0.0) };
		Check(WorldAlerts(spring, rules).empty(), "the world's first morning: every store is empty and nobody is hungry yet: no hunger (graceDays)");
		spring.Now = kDay;
		spring.Clans = { clan(1, "Reed", 6, 6.0f, kDay) };
		Check(WorldAlerts(spring, rules).empty(), "6 days of food on the second day of the spring: nothing (the winter is 41 days away)");
		spring.Clans[0].Survival = true;
		Check(Find(WorldAlerts(spring, rules), "hunger:1") != nullptr, "a clan in survival is hungry whatever its days");
		AlertRules quiet = rules;
		quiet.HungerOnSurvival = false;
		Check(WorldAlerts(spring, quiet).empty(), "unless the data says the survival tier alone does not count");
		spring.Clans = { clan(3, "Elk", 0, 0.0f, kDay) };
		spring.Clans[0].Dead = 5;
		const std::vector<Alert> gone = WorldAlerts(spring, rules);
		Check(gone.size() == 1 && gone[0].Kind == "extinct" && gone[0].Level == AlertLevel::Critical && Near(gone[0].Value, 5.0f), "a clan with nobody alive: extinct, and no hunger nor winter");
	}

	// ---- the events: war between clans (one per pair), not a wolf's; an inventor
	{
		std::vector<HistoryEvent> events(6);
		events[0] = { 1, now, "raid", "", "", 1, 0, glm::vec2(0.0f), false, "Reed and Clan 1: Reed raided Clan 1", 4 };
		events[1] = { 2, now + 10.0, "killed", "Tik", "Ama", 0, -1, glm::vec2(3.0f, 4.0f), true, "Ama (forager) is killed by Tik (hunter)", 5 };
		events[2] = { 3, now + 20.0, "wounded", "Wolf 2", "Ana", 0, -1, glm::vec2(1.0f), true, "Ana (forager) is wounded (bite, by Wolf 2)", 3 };
		events[3] = { 4, now + 30.0, "discovery", "Bo", "", 2, -1, glm::vec2(7.0f), true, "Bo (crafter) discovers pottery", 4 };
		events[4] = { 5, now + 40.0, "found", "", "", 2, 4, glm::vec2(0.0f), false, "3 of the Ash leave to found the Ash II: crowded", 5 };
		events[5] = { 6, now + 50.0, "discovery", "Lia", "", 2, -1, glm::vec2(8.0f), true, "Lia (forager) discovers weaving", 4 };
		AlertInput ev;
		ev.Now = now + 60.0;
		for (const HistoryEvent& e : events) ev.Events.push_back(&e);
		ev.ClanOfMember = [](const std::string& who) { return who == "Tik" ? 1 : who == "Ama" || who == "Ana" ? 0 : -1; };
		ev.ClanName = [](int id) { return id == 0 ? std::string("Clan 1") : id == 1 ? std::string("Reed") : id == 2 ? std::string("Ash") : id == 4 ? std::string("Ash II") : std::string(); };
		const std::vector<Alert> got = WorldAlerts(ev, rules);
		const Alert* war = Find(got, "war:0:1");
		Check(war && Near(war->Value, 2.0f) && war->Clan == 1 && war->Other == 0 && war->HasWhere && war->Where == glm::vec2(3.0f, 4.0f),
			"a raid and a killing between Reed and Clan 1: one war alert with 2 clashes, where the last was" + (war ? ": " + war->Text : std::string()));
		int wars = 0;
		for (const Alert& a : got) wars += a.Kind == "war" ? 1 : 0;
		Check(wars == 1, "a wolf's bite is no war");
		const Alert* inventor = Find(got, "inventor:2");
		int inventors = 0;
		for (const Alert& a : got) inventors += a.Kind == "inventor" ? 1 : 0;
		Check(inventor && inventors == 1 && inventor->Level == AlertLevel::Important && inventor->Clan == 2 && inventor->Who == "Lia" && Near(inventor->Value, 2.0f),
			"two discoveries of the Ash: one important inventor alert, 2 discoveries, the last named" + (inventor ? ": " + inventor->Text : std::string()));
		const Alert* split = Find(got, "split:#5");
		Check(split && split->Level == AlertLevel::Important && split->OtherName == "Ash II", "a split: important, naming the new clan" + (split ? ": " + split->Text : std::string()));
		Check(!got.empty() && got.front().Level == AlertLevel::Critical && got.back().Level == AlertLevel::Important, "critical first");

		// ---- the board: grouped over time
		AlertBoard board;
		std::vector<std::string> raised = board.Update(found, now, rules, 1200.0f);
		Check(raised.size() == 3 && board.Shown().size() == 3, "the first look raises its three alerts");
		AlertInput later = in;
		later.Now = now + 600.0;
		later.Clans[0].FoodDays = 1.5f;
		raised = board.Update(WorldAlerts(later, rules), later.Now, rules, 1200.0f);
		int hungers = 0;
		for (const Alert* a : board.Shown()) hungers += a->Kind == "hunger" ? 1 : 0;
		const Alert* h = nullptr;
		for (const Alert& a : board.All()) if (a.Key == "hunger:0") h = &a;
		Check(raised.empty() && hungers == 1 && h && Near(h->Value, 1.5f) && h->Since == now, "the same hunger is not repeated: its number goes to 1.5 days and it keeps when it began");
		Check(board.Raised().at("hunger") == 1 && board.Raised().at("winter") == 2, "counted once each (hunger 1, winter 2)");
		// dismiss the hunger (the first shown is the newest critical: find its place)
		const std::vector<const Alert*> shown = board.Shown();
		int at = 0;
		for (size_t i = 0; i < shown.size(); ++i) if (shown[i]->Key == "hunger:0") at = static_cast<int>(i) + 1;
		Check(at > 0 && board.Dismiss(at) && board.Shown().size() == 2, "a dismissed alert is no longer shown");
		Check(!board.Dismiss(9), "there is no 9th to dismiss");
		board.Update(WorldAlerts(later, rules), later.Now + 60.0, rules, 1200.0f);
		Check(board.Shown().size() == 2, "and stays away while the clan is still hungry");
		AlertInput fed = later;
		fed.Clans[0].FoodDays = 30.0f;
		fed.Clans[0].Winter = ForecastWinter(seasons, now, 1200.0f, 30.0f);
		board.Update(WorldAlerts(fed, rules), later.Now + 120.0, rules, 1200.0f);
		Check(board.Shown().size() == 1 && board.All().size() == 1, "fed again, its hunger and its winter go (Reed's winter stays)");
		raised = board.Update(WorldAlerts(later, rules), later.Now + 180.0, rules, 1200.0f);
		Check(raised.size() == 2 && board.Shown().size() == 3 && board.Raised().at("hunger") == 2, "hungry again: raised again and shown (the dismissal was for the last time)");
		// a war: its clashes add up over the looks and it goes after its days
		board.Update(got, ev.Now, rules, 1200.0f);
		AlertInput more;
		more.Now = ev.Now + 100.0;
		HistoryEvent again = events[0];
		again.Id = 9;
		more.Events = { &again };
		board.Update(WorldAlerts(more, rules), more.Now, rules, 1200.0f);
		const Alert* w = nullptr;
		for (const Alert& a : board.All()) if (a.Key == "war:0:1") w = &a;
		Check(w && Near(w->Value, 3.0f) && board.Raised().at("war") == 1, "a third clash a look later: the same war, 3 clashes" + (w ? ": " + w->Text : std::string()));
		board.Update({}, more.Now + 2.5 * kDay, rules, 1200.0f);
		bool warLeft = false;
		for (const Alert& a : board.All()) warLeft = warLeft || a.Kind == "war";
		Check(!warLeft && board.All().empty(), "two and a half days of peace: the war goes; nothing holds: the board is empty");
		Check(board.Text() == "no alerts", "and says so");
		Check(board.RaisedLine() == "hunger 2, winter 3, war 1, extinct 0, inventor 1, split 1", "the count by kind: " + board.RaisedLine());
	}

	// ---- the section
	{
		AlertRules r;
		Check(LoadAlertRules(R"({ "alerts": { "hungerDays": 2, "hungerOnSurvival": false, "graceDays": 1, "winterWithinDays": 10, "warDays": 3, "eventDays": 0.5 } })", r).empty()
			&& r.HungerDays == 2.0f && !r.HungerOnSurvival && r.GraceDays == 1.0f && r.WinterWithinDays == 10.0f && r.WarDays == 3.0f && r.EventDays == 0.5f, "the section reads");
		AlertRules back;
		Check(LoadAlertRules("{ \"alerts\": " + AlertRulesToJson(r) + " }", back).empty() && back == r, "and is written back as it reads");
		AlertRules same = r;
		Check(!LoadAlertRules(R"({ "alerts": { "thirstDays": 2 } })", same).empty() && same == r, "an unknown key is a mistake and changes nothing");
		Check(!LoadAlertRules(R"({ "alerts": { "hungerDays": -1 } })", same).empty() && same == r, "so is a negative threshold");
		Check(LoadAlertRules(R"({ "items": [] })", same).empty() && same == r, "a file without the section changes nothing");
#ifdef GB_ASSETS_DIR
		std::ifstream f(std::string(GB_ASSETS_DIR) + "/data/director.json");
		const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		AlertRules game;
		const std::string error = LoadAlertRules(text, game);
		Check(!text.empty() && error.empty() && game == AlertRules{}, "the game's data (director.json) loads and says the built-in thresholds" + (error.empty() ? std::string() : ": " + error));
#endif
	}

	std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "passed", failures);
	return failures ? 1 : 0;
}
