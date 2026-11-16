#pragma once
#include "GB/Game/WinterOutlook.h"
#include <glm/glm.hpp>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class ClanWorld;
	struct HistoryEvent;

	// T220 (U4 of documentation/01_diseno/MODO_DIOS_UX.md 3.7): what needs the god's attention now. It ONLY READS the clans (their food, their
	// winter's outlook, who is alive) and the world's history (raids, killings, discoveries, splits); nothing here is read back by the simulation,
	// so a run is the same with alerts or without. WorldAlerts gives what holds in one look; AlertBoard keeps them over time (the same alert is
	// not repeated, its number is updated; a dismissed one stays away until its condition clears and comes back). The editor draws the board
	// (ObserverView.cpp) and GBSim --report counts what was raised.
	//
	// Kinds (each critical or important):
	//   hunger    critical   FoodDays < Rules.HungerDays, or the clan is in survival (Tribe::MaslowTier) and Rules.HungerOnSurvival; not in the
	//                        world's first Rules.GraceDays days (every clan starts with an empty store and nobody hungry: GBSim 3x6 had a
	//                        hunger alert per clan in its first minute)
	//   winter    critical   the winter is Rules.WinterWithinDays days away or here, and its outlook (WinterOutlook.h) is short
	//   war       critical   a raid between two clans, or one clan's member killed or wounded by another's (one alert per pair of clans: the
	//                        number of clashes goes up); it goes Rules.WarDays days after the last clash
	//   extinct   critical   a clan that had people has nobody alive
	//   inventor  important  someone is the first of their clan to find a technology (the history's "discovery"): one alert per clan, the
	//                        number of discoveries goes up and the last one is named (GBSim 3x6 makes about 12 a day); it goes Rules.EventDays
	//                        days after the last
	//   split     important  a clan splits and founds another (the history's "found")
	// The thresholds are data: the "alerts" section (assets/data/director.json), read by the editor and GBSim alike.
	struct AlertRules
	{
		float HungerDays = 3.0f;          // fewer days of food than this: hunger
		bool HungerOnSurvival = true;     // the survival tier alone also counts
		float GraceDays = 0.5f;           // no hunger before the world is this many days old
		float WinterWithinDays = 7.0f;    // how close the winter must be to warn of it (0: only in it)
		float WarDays = 2.0f;             // a war alert goes this many days after the last clash
		float EventDays = 1.0f;           // an important one (inventor, split) goes after this many days
		bool operator==(const AlertRules&) const = default;
	};
	// Reads the "alerts" section of a pack file over `rules` (all or nothing): "" or what is wrong. A file without it changes nothing.
	std::string LoadAlertRules(const std::string& json, AlertRules& rules);
	std::string AlertRulesToJson(const AlertRules& rules);

	enum class AlertLevel { Important, Critical };
	const char* AlertLevelName(AlertLevel level);
	// The kinds in the order the lists show them.
	const std::vector<std::string>& AlertKinds();

	struct Alert
	{
		std::string Kind;              // see above
		std::string Key;               // what makes two alerts the same one: "hunger:2", "war:0:2", "inventor:1", "split:#57"
		int Clan = -1;                 // the clan it is about (war: the attacker of the last clash)
		int Other = -1;                // war: the other clan; split: the new one
		std::string ClanName, OtherName;   // as the clan world names them (the editor translates: "Clan 1" -> Roble)
		AlertLevel Level = AlertLevel::Important;
		float Value = 0.0f;            // hunger: days of food; winter: days short; war: clashes; extinct: the dead; inventor: discoveries
		float Value2 = 0.0f;           // hunger: people alive; winter: days of food in hand; war: unused
		float Value3 = 0.0f;           // winter: days until the winter (0 in it); the others: unused
		std::string Who, What;         // inventor: who and the line the log said; war, split: the last event's line
		std::string Text;              // in English, for `observer alerts` and the report
		glm::vec2 Where{ 0.0f };       // where the camera goes ([go]): the clan's camp, or where the event happened
		bool HasWhere = false;
		int Event = 0;                 // the history event it came from (0: a condition)
		double Since = 0.0, Last = 0.0;   // world seconds: when it was first raised and last seen
	};

	// One clan as the alerts see it: a snapshot the host fills (AlertInputOf) or a test writes by hand.
	struct ClanAlertState
	{
		int Clan = -1;
		std::string Name;
		int Alive = 0, Dead = 0;
		float FoodDays = 0.0f;
		bool Survival = false;          // Tribe::MaslowTier() == "survival"
		WinterOutlook Winter;
		glm::vec2 Camp{ 0.0f };
	};
	struct AlertInput
	{
		double Now = 0.0;                                   // world seconds
		float DaySeconds = 1200.0f;
		std::vector<ClanAlertState> Clans;
		std::vector<const HistoryEvent*> Events;            // the events since the last look, oldest first
		std::function<int(const std::string&)> ClanOfMember;   // the clan of a member (-1: none: an animal, a stranger)
		std::function<std::string(int)> ClanName;          // a clan's name by id ("" unknown)
	};
	// The snapshot of a clan world: every clan, and the history's events after `sinceEvent` (an id; 0 = all of them). The world must outlive it.
	AlertInput AlertInputOf(const ClanWorld& world, int sinceEvent);

	// What holds in this look: the conditions of each clan, and an alert per event of the input that is one (several events of the same
	// alert, like the clashes of one war, give one alert with their count). Ordered critical first, then by clan.
	std::vector<Alert> WorldAlerts(const AlertInput& in, const AlertRules& rules);

	// The alerts over time. Update with each look; Shown() is what the god sees (no dismissed ones), critical first then the newest first.
	class AlertBoard
	{
	public:
		// The alerts of a look at `now`: a new key is raised (counted by kind in Raised()), a known one is updated (its number, its Last); a
		// condition that no longer holds goes (and may be raised again later), an event's alert goes after its days (Rules.WarDays, EventDays).
		// Returns the keys raised in this update.
		std::vector<std::string> Update(const std::vector<Alert>& found, double now, const AlertRules& rules, float daySeconds);
		// A look at a clan world: the events since the last look, the snapshot, WorldAlerts, Update.
		std::vector<std::string> Look(const ClanWorld& world, const AlertRules& rules);
		// The next look starts after the history's newest event (a loaded game: what happened before is not news).
		void SkipHistory(const ClanWorld& world);
		const std::vector<Alert>& All() const { return m_Alerts; }
		std::vector<const Alert*> Shown() const;
		// Dismisses the n-th of Shown() (1-based); false when there is none. A condition stays dismissed while it holds.
		bool Dismiss(int n);
		bool DismissKey(const std::string& key);
		void Clear();
		const std::map<std::string, int>& Raised() const { return m_Raised; }
		// "hunger 2, winter 0, war 1, ..." (every kind, in AlertKinds order)
		std::string RaisedLine() const;
		// One line per shown alert: "1. critical hunger: Clan 1: 2.0 days of food (6 alive)"
		std::string Text() const;

	private:
		std::vector<Alert> m_Alerts;
		std::map<std::string, bool> m_Dismissed;   // key -> dismissed (kept while the alert lives)
		std::map<std::string, int> m_Raised;
		int m_SeenEvent = 0;
	};
}
