#pragma once
#include "GB/Game/DirectorVote.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

// T195 (M1 of the god mode, documentation/01_diseno/MODO_DIOS.md §3.4; the faith of MODO_DIOS_DECISIONES.md §4): the director of the world.
// It keeps the god's influence, the waits of each intervention, the pressure on each clan and the director's book, and runs the `director ...`
// orders both hosts register (the editor and GB.exe through EditorApp, GBSim through --director=<day>:<order>).
//
// The influence comes from the faith of the people, not from a clock: each rite held adds its attendees x the clan's faith, each omen a clan
// reads adds a little, every day adds the living x their mean devotion, and a sign that comes true adds what its caller says (Fulfilled). Only
// `director sandbox on` refills it on a fixed clock (and waives the waits), for tests and labs. The director only READS the clans while no
// order is given (no die, no state of a Tribe changes): without orders the simulation is the same as without the director.
//
// The hand of the god (the decisions of MODO_DIOS_DECISIONES: vote, sign, focus...) reaches only a clan whose faith is at least MinFaith; the
// events of the world (the omen) are natural phenomena and do not need it. Every intervention goes through Spend, which says why it cannot be.
namespace GB
{
	class ClanWorld;   // (no forward declaration of Clan here: tests that include GameContent.h have a Clan of their own)

	// T237 (MODO_DIOS_DECISIONES.md §3.a): the sign. Its cost is "sign" in the costs ("sign_site" when the clan already wanted that building:
	// the sign only fixes the place).
	struct DirectorSignRules
	{
		float RefundUnseen = 10.0f;        // influence back when nobody saw the sign in SignDays
		float SignDays = 1.0f;             // the sign stays this long waiting for someone to see it (half of it: the shaman dreams of it)
		float VoteDays = 0.25f;            // from the sign seen to the clan's vote at the fire (a doubt: the same again)
		float BuildCost = 0.15f;           // the cost of a building in the vote (as player.proposals.buildCost)
		float Missing = 0.3f;              // x the share of its material the store lacks
		float Unwanted = 0.35f;            // D43: the clan did not want that building (it is not among its needs)
		float Reach = 30.0f;               // the camp's own ground: always the clan's (beyond it, a region the clan holds)
		float FulfilledGain = 10.0f;       // influence when the building stands (Fulfilled)
		int Keep = 20;                     // closed signs kept for `director signs`
		std::map<std::string, float> Traits = { { "diligence", 0.2f } };
		bool operator==(const DirectorSignRules&) const = default;
	};

	// A sign of the god (`director sign`) and where it is: "waiting" (nobody saw it yet), "seen" (the messenger saw it: the clan votes it at the
	// fire), "accepted" (the site is open), "built", or closed with no building: "refused", "unseen", "dropped".
	struct DirectorSign
	{
		int Id = 0, Clan = -1;
		std::string Building;
		float X = 0.0f, Z = 0.0f, Yaw = 0.0f;
		double Posted = 0.0, SeenAt = 0.0, VotedAt = 0.0;
		std::string Status = "waiting", Messenger, Site, Why;
		std::string Against;               // D43: a refused sign: what weighed most against it ("they do not need it"...)
		float Paid = 0.0f;
		int Tries = 0, Yes = 0, Voters = 0;
		bool Open() const { return Status == "waiting" || Status == "seen"; }
		bool operator==(const DirectorSign&) const = default;
	};

	// What `director sign preview` answers (and the UI paints: a green or red footprint): whether the sign can go there, why not, what it costs,
	// who would likely carry it and the vote foreseen. Changes nothing.
	struct DirectorSignPreview
	{
		bool Ok = false;
		std::string Why;                   // why not ("" when it can)
		std::string Warning;               // it can, but... ("the store lacks 3 reed": the site will wait for it)
		float Cost = 0.0f;
		bool SiteOnly = false;             // the clan already wanted it: the sign only fixes the place
		std::string Messenger;             // the probable one
		int Yes = 0, Voters = 0;
		std::string Outcome;               // "obey", "doubt", "resist"
		std::string Against;               // D43: what weighs most against it ("" when nobody is against)
		float X = 0.0f, Z = 0.0f;
	};

	// T238 (MODO_DIOS_DECISIONES.md §3.b): a group of the focus: the utility options it pushes, the trait that follows it and the norm it leaves.
	struct DirectorFocusGroup
	{
		std::vector<std::string> Kinds;
		bool FoodGather = false, OtherGather = false;   // a "gather" of food / of anything else is of the group
		std::string Trait, Norm;
		bool operator==(const DirectorFocusGroup&) const = default;
	};
	// "focus": m(p) = 1 + (M - 1) x clamp(Faith x devotion(p) + Trait x (2 trait(p) - 1), 0, 1); M is Soft or Strong. The cost is "focus" or
	// "focus_strong" in the costs; Upkeep a day after; each day the group's norm moves Drift towards 1. In "survival" only "food" works.
	struct DirectorFocusRules
	{
		float Soft = 2.0f, Strong = 3.0f;  // D43: x1.25 on the score moved the time in lore +25 % in 1 seed of 3 (the choice is nearly an argmax)
		float UpkeepSoft = 3.0f, UpkeepStrong = 5.0f;
		int MaxDays = 7;
		float Faith = 1.4f, Trait = 0.3f, Drift = 0.01f;   // (Faith x devotion: with the clan's faith ~0.5 a devotion ~0.35 follows it by half)
		std::map<std::string, DirectorFocusGroup> Groups = {
			{ "food", { { "hunt", "snare", "farm", "haul" }, true, false, "diligence", "industry" } },
			{ "build", { { "build", "deliver", "pave", "make", "craft" }, false, true, "diligence", "industry" } },
			{ "lore", { { "experiment", "learn", "teach", "explore" }, false, false, "curiosity", "inquiry" } },
			{ "defense", { { "regroup", "gear", "care" }, false, false, "boldness", "daring" } },
			{ "faith", { { "rite", "talk", "socialize" }, false, false, "sociability", "sharing" } } };
		bool operator==(const DirectorFocusRules&) const = default;
	};
	// T239: "calling": the cost is "calling" in the costs; Days long; Skill points more in the review of the trades; Traits weigh in the
	// person's disposition (DirectorDisposition, with no messenger); with a disposition <= 0 it does not take it and Refund of the cost comes back.
	struct DirectorCallingRules
	{
		float Days = 14.0f, Skill = 30.0f, Refund = 0.5f;
		std::map<std::string, float> Traits;
		bool operator==(const DirectorCallingRules&) const = default;
	};
	// T240 (MODO_DIOS_DECISIONES.md §3.c): the vision. Its cost is "vision" in the costs. A dreamer of the clan dreams the technology and the clan
	// takes it as its aim (Tribe::SetAim, as `game research`); the aim lapses after Days with no step forward on its way. It orients, never
	// grants: without its material the clan never learns it (the physical gate of T181 holds). Traits weigh in the vote at the fire.
	struct DirectorVisionRules
	{
		float Days = 3.0f;                 // days with no step forward on the way before the aim lapses
		float FulfilledGain = 5.0f;        // influence when the clan learns it while the vision holds (Fulfilled)
		int Keep = 20;                     // closed visions kept for `director visions`
		std::map<std::string, float> Traits = { { "curiosity", 0.3f } };
		bool operator==(const DirectorVisionRules&) const = default;
	};
	// A vision and where it is: "aim" (the clan seeks it), "learnt", "lapsed" (no step forward in Days), "replaced" (the aim changed), "refused".
	struct DirectorVision
	{
		int Id = 0, Clan = -1;
		std::string Tech, Dreamer, Status = "aim";
		double Since = 0.0, Advanced = 0.0;   // when it was dreamt, and the last step forward on the way
		float Progress = 0.0f;                // the way's progress last seen (techs of the way known + the best understanding of each)
		float Paid = 0.0f;
		bool operator==(const DirectorVision&) const = default;
	};
	// What `director vision preview` answers (and the tree paints): changes nothing.
	struct DirectorVisionPreview
	{
		bool Ok = false;                   // it can be dreamt now
		std::string Why;                   // why not ("" when it can)
		std::string Name;                  // the technology's name
		float Cost = 0.0f;
		std::vector<std::string> Way;      // what the clan does not know yet, in the order it would seek it (the tech last)
		std::vector<std::string> Missing;  // the materials (and the stations) the clan lacks: without them it is never learnt
		std::string Dreamer;               // who would dream it
		bool Inventor = false;             // somebody of the clan has the intellect to work it out (else only from another clan)
		int Yes = 0, Voters = 0;
		std::string Outcome;               // "obey", "doubt", "resist"
	};

	struct DirectorFocus                   // the focus of a clan now
	{
		int Clan = -1;
		std::string Group;
		bool Strong = false;
		double Since = 0.0, Until = 0.0;
		int PaidDay = 0;                   // the last day whose upkeep was paid (and whose drift was given)
		bool Paused = false;               // the clan is in "survival" and the group is not food
		bool operator==(const DirectorFocus&) const = default;
	};
	struct DirectorCalling                 // a calling of one person
	{
		int Clan = -1, Order = 0;
		std::string Person, Role;
		double Since = 0.0, Until = 0.0;
		bool operator==(const DirectorCalling&) const = default;
	};
	struct DirectorCallingPreview
	{
		bool Ok = false;                   // it can be given
		bool Accepts = false;              // and the person would take it (disposition > 0)
		std::string Why, Person, Role, Current;
		float Disposition = 0.0f, Cost = 0.0f, Skill = 0.0f, Own = 0.0f;
	};

	// D43: the faith of a clan in the god, kept by the director (the clan's beliefs are the simulation's and fill up in a day and a half; this one
	// is only the god's, so nothing of the clans changes with it). A day: F += (Grow x dominant belief + Rite x min(1, rites)) x (1 - F) - Decay x F,
	// `rites` the attendances of the day over the living (one rite of the whole clan = 1); it settles at (Grow + Rite) / (Grow + Rite + Decay) with
	// a rite a day and the belief full, at Grow / (Grow + Decay) with none. An omen read adds Omen x (1 - F) at once, a sign or a vision that comes
	// true Fulfilled x (1 - F). The hand wears it: each decision on the clan takes Hand, a refusal of the clan Refused. A tired clan (pressure >=
	// PressureTired) gains at Tired x the sources.
	struct DirectorFaithRules
	{
		float Start = 0.3f, Grow = 0.06f, Decay = 0.07f, Rite = 0.04f, Omen = 0.02f, Fulfilled = 0.06f, Hand = 0.02f, Refused = 0.04f, Tired = 0.5f;
		bool operator==(const DirectorFaithRules&) const = default;
	};
	// T241 (MODO_DIOS_DECISIONES.md §4.4): at the heresy pressure the adults with devotion under Devotion and boldness over Boldness turn skeptics;
	// when at least Share of the clan's adults are skeptics for Days (a season: a day), the clan splits with them (ClanWorld::Split, cause
	// "heresy"), and the new clan starts with faith Faith.
	struct DirectorSkepticRules
	{
		float Devotion = 0.3f, Boldness = 0.6f, Share = 0.334f, Days = 1.0f, Faith = 0.05f;
		float Ease = 0.02f;   // the boldness asked falls by this for each point of pressure over heresy (a god who keeps pushing makes laughing easy)
		// D49 (MODO_DIOS_DECISIONES §6.3): only a group of MinGroup or more leaves (fewer skeptics, or a clan too small to let them go with half of
		// it staying: no split), and GapDays pass between two heresies of the same clan (the clan that stayed)
		int MinGroup = 3;
		float GapDays = 10.0f;
		bool operator==(const DirectorSkepticRules&) const = default;
	};
	// §6.2 A: the god's game is lost when no clan believes any more (faith >= MinFaith), once GraceDays have gone by.
	struct DirectorDefeatRules
	{
		float GraceDays = 3.0f;
		bool operator==(const DirectorDefeatRules&) const = default;
	};
	struct DirectorSkeptic                 // a skeptic (T241): in the director's book, never in the Tribe
	{
		int Clan = -1;
		std::string Person;
		double Since = 0.0;
		float Devotion = 0.0f;             // its devotion when it turned: it does not rise again
		bool operator==(const DirectorSkeptic&) const = default;
	};
	struct DirectorBelievers { int Clans = 0, People = 0; };

	// D48 (MODO_DIOS_DECISIONES §6.2 B, §6.3): the god's legacy, in stages. A stage is a list of goals and how many of them win it (Need; 0 = all).
	// The goals are generic, so a new stage is data only ("stages" of assets/data/director.json). Every goal looks at the believing clans (faith >=
	// MinFaith) at that moment:
	//   population        Clans believing clans have Count living people
	//   tech              Clans believing clans know Tech (the clan's knowledge, ClanKnowledge::Clan)
	//   techs             Clans believing clans know Count technologies
	//   believers         Count clans believe
	//   signs_built       Count signs of the god stand (built since the world began; DirectorSign "built")
	//   no_hunger_winter  Count whole winters (SeasonClock, watched from their start) in which no believing clan went hungry: fewer than FoodDays
	//                     days of food (Tribe::FoodDays), the survival tier when Survival, or somebody dead of hunger
	//   peace             Count days with no raid nor a death or wound between clans (the world's history) since the stage opened
	// A goal met stays met (the tick does not go away when a clan falls back). Only with the stakes on (GB.exe's god's game, GBSim) do they count.
	struct DirectorGoalDef
	{
		std::string Id, Type, Tech;
		int Count = 1, Clans = 1;
		float FoodDays = 3.0f;
		bool Survival = false;
		bool operator==(const DirectorGoalDef&) const = default;
	};
	struct DirectorStageDef
	{
		std::string Id, Name;              // Name in English (the UI translates it: es.json)
		int Need = 0;                      // goals that win it (0: all)
		bool Draft = false;                // its numbers are not tuned yet
		std::vector<DirectorGoalDef> Goals;
		bool operator==(const DirectorStageDef&) const = default;
	};
	struct DirectorStageRules
	{
		float LookSeconds = 30.0f;         // how often the goals are looked at (the deaths of hunger are counted, never missed)
		std::vector<DirectorStageDef> List;
		bool operator==(const DirectorStageRules&) const = default;
	};
	// What a goal stands at (the director's book: saved with it).
	struct DirectorGoalState
	{
		bool Done = false;
		int Value = 0;                     // the progress towards its Count (tech: believing clans that know it, towards Clans)
		int Leader = -1;                   // the clan nearest to it (population, tech, techs; -1: none)
		double DoneAt = -1.0;
		// no_hunger_winter: the winter watched (its year; -1 none), whether it was watched from its start, whether a believing clan went hungry in it
		// and the dead of hunger of each clan when it began
		int Year = -1;
		bool Whole = false, Hungry = false;
		std::map<int, int> Starved;
		bool operator==(const DirectorGoalState&) const = default;
	};

	// assets/data/director.json, the "director" section.
	struct DirectorRules
	{
		float Start = 50.0f;               // the influence of a new world
		float Max = 100.0f;
		float MinFaith = 0.2f;             // the hand reaches only a clan whose faith is at least this
		float RiteGain = 0.5f;             // influence per attendee of a rite x the clan's faith
		float OmenGain = 1.0f;             // influence per omen a clan reads x (0.5 + its faith)
		float DailyGain = 0.5f;            // influence a day per living member x its devotion
		float ShamanDevotion = 0.2f;       // what being the clan's shaman adds to a person's devotion
		float SandboxPerDay = 20.0f;       // `director sandbox on`: the fixed clock that refills the influence
		std::map<std::string, float> Costs = { { "omen", 10.0f }, { "sign", 20.0f }, { "sign_site", 10.0f }, { "focus", 10.0f }, { "focus_strong", 20.0f }, { "calling", 15.0f }, { "vision", 10.0f } };   // action -> influence it costs
		std::map<std::string, float> Waits = { { "omen", 1.0f }, { "sign", 0.0f }, { "sign_site", 0.0f }, { "focus", 0.0f }, { "focus_strong", 0.0f }, { "calling", 0.0f }, { "vision", 0.0f } };      // action -> days before it can fall on the same clan again (a sign: one open per clan instead)
		// the pressure on a clan (§4.4): tired (the faith's sources and its weight in the vote at Faith.Tired), heresy (skeptics; T241)
		float PressureDecision = 1.0f;     // a decision of the hand on the clan
		float PressureInsist = 3.0f;       // the same decision asked again within InsistDays after a refusal (the caller says it)
		float PressureContradict = 2.0f;   // an order against the last one within a day (the caller says it)
		float PressureMurmur = 2.0f;       // T241: the clan says no to an idea of the god (it murmurs)
		float PressureDecay = 1.0f;        // a day
		float PressureTired = 6.0f;
		float PressureHeresy = 12.0f;
		float InsistDays = 3.0f;
		float DaySeconds = 1200.0f;
		int LogMax = 200;
		DirectorVoteRules Vote;            // T236: how a clan votes an idea of the god ("vote")
		float MessengerRadius = 40.0f;     // T237: who is this near a sign can see it ("messenger": { "radius" })
		DirectorSignRules Sign;            // T237: the sign ("sign")
		DirectorFocusRules Focus;          // T238: the focus of a clan ("focus")
		DirectorCallingRules Calling;      // T239: the calling of a person ("calling")
		DirectorVisionRules Vision;        // T240: the vision ("vision")
		DirectorFaithRules Faith;          // D43: the clan's faith in the god ("faith": { "start", "grow", "decay", "riteFaith", ... })
		DirectorSkepticRules Skeptic;      // T241 ("skeptic")
		DirectorDefeatRules Defeat;        // §6.2 A ("defeat")
		DirectorStageRules Stages;         // D48: the top-level "stages" of the same file (content, not tuning: the code has none of its own)
		bool operator==(const DirectorRules&) const = default;
	};
	DirectorRules DefaultDirectorRules();   // assets/data/director.json says the same (but the stages, which only the data has)
	// { "director": { "start", "max", "minFaith", "faith": { "rite", "omen", "daily", "shaman" }, "sandboxPerDay", "costs": {}, "waits": {},
	//   "pressure": { "decision", "insist", "contradict", "decay", "tired", "heresy", "insistDays" }, "logMax",
	//   "vote": { "base", "faith", "affinity", "shaman", "survival", "majority", "doubtBand", "retries", "culture": {} }, "messenger": { "radius" },
	//   "sign": { "refundUnseen", "signDays", "voteDays", "buildCost", "missing", "reach", "fulfilled", "keep", "traits": {} } } }. What it does not
	// say keeps its value; a file without the section changes nothing; on a mistake it returns why and changes nothing.
	std::string LoadDirectorRules(const std::string& json, DirectorRules& rules);

	// Where the director acts: the hosts fill it the same way (SimRun, EditorApp), as ContentTargets. The clans and the clock of the world.
	struct DirectorTargets
	{
		ClanWorld* Clans = nullptr;
		std::function<double()> Now;       // the world's clock in seconds (the simulation's, never the wall's)
	};

	struct DirectorEntry                   // a line of the director's book
	{
		double At = 0.0;
		std::string Action, Target, Text;
		float Cost = 0.0f;
		bool operator==(const DirectorEntry&) const = default;
	};

	struct DirectorCheck                   // what Spend / CanSpend answer
	{
		bool Ok = false;
		std::string Why;                   // why not ("" when it can)
		float Cost = 0.0f;
	};

	struct DirectorReply { bool Ok = false; std::string Text; };

	class WorldDirector
	{
	public:
		DirectorRules Rules = DefaultDirectorRules();

		void SetTargets(const DirectorTargets& to) { m_To = to; }
		const DirectorTargets& Targets() const { return m_To; }
		// After the clans' step: reads the rites and omens held since the last look and the day that turned (no state of a clan changes).
		void Update();
		// `director <args...>` (status, sandbox, faith, omen, log); the reply is what the order prints (a contract of the tests: English).
		DirectorReply Command(const std::vector<std::string>& args);

		// ---- what the clans believe (read only)
		float FaithOf(int clan) const;                                     // the clan's faith in the god 0..1 (D43: the director's own; or the sandbox's value)
		float BeliefOf(int clan) const;                                    // its dominant belief 0..1 (the simulation's: what the faith grows from)
		void SetFaith(int clan, float faith);                              // (a test, a heresy split's new clan)
		float DevotionOf(int clan, const std::string& person) const;       // a person's 0..1 (MODO_DIOS_DECISIONES §4.1)
		float MeanDevotion(int clan) const;                                // of its living members (0 with none)

		// ---- the generic way to intervene (G4-1 vote, G4-2 sign, G4-3 focus ...): `action` names its cost and wait in the rules; `hand` is a
		// decision of the hand (it needs the clan's faith >= MinFaith and adds PressureDecision to the clan); clan -1 = the world (no faith).
		DirectorCheck CanSpend(const std::string& action, int clan, bool hand) const;
		// Pays it (and starts its wait, adds the pressure, writes the book and a telemetry row "director"); when it cannot, changes nothing.
		DirectorCheck Spend(const std::string& action, int clan, bool hand, const std::string& text);
		void Fulfilled(int clan, float influence, const std::string& text);   // a sign or a vision came true: the god gains (G4-2)
		void AddPressure(int clan, float amount, const std::string& why);     // an insistence, a contradiction, a murmur (the caller decides)
		// D43/T241: the clan said no to an idea of the god (`what`: "sign", "vision"...): its faith drops, it murmurs (PressureMurmur); counted
		void Refused(int clan, const std::string& what, const std::string& text);
		void Voted() { ++m_Votes; }                                        // a vote of a clan on an idea of the god closed (counted for the report)
		float PressureOf(int clan) const;
		std::string PressureLevel(int clan) const;                        // "low", "tired", "heresy"
		bool Tired(int clan) const { return PressureOf(clan) >= Rules.PressureTired; }
		// ---- T241: the skeptics (in the director's book): a skeptic votes against with a no that weighs two, and its devotion does not rise
		bool IsSkeptic(int clan, const std::string& person) const;
		const std::vector<DirectorSkeptic>& Skeptics() const { return m_Skeptics; }
		int HeresySplits() const { return m_HeresySplits; }
		int Refusals() const { return m_Refusals; }
		int Votes() const { return m_Votes; }
		// ---- §6.2 A: the believers and the defeat. Only a host that plays the god's game puts the stakes on (GB.exe's `shell new`, GBSim): with
		// them off (the editor, the player's game) there is no defeat.
		DirectorBelievers Believers() const;                               // the clans with faith >= MinFaith and their living
		void SetStakes(bool on) { m_Stakes = on; }
		bool Stakes() const { return m_Stakes; }
		bool Defeated() const { return m_DefeatAt >= 0.0; }
		double DefeatAt() const { return m_DefeatAt; }                     // world seconds (-1: none)
		bool DefeatSeen() const { return m_DefeatSeen; }                   // the player chose to go on watching the world
		std::string ReportLine() const;                                    // GBSim --report: the faith, the votes, the skeptics, the defeat
		// ---- D48: the stages of the legacy (DirectorStage.cpp). Stage() is null when there are none or all were fulfilled.
		const DirectorStageDef* Stage() const;
		int StageNumber() const { return m_Stage + 1; }                    // 1-based
		DirectorGoalState GoalOf(const std::string& goal) const;           // (a goal not looked at yet: the default)
		int GoalsDone() const;
		int GoalsNeeded() const;                                           // Need, or every goal
		bool StageWon() const { return m_StageWonAt >= 0.0; }              // the stage is fulfilled and the player has not moved on yet
		double StageWonAt() const { return m_StageWonAt; }
		double StageSince() const { return m_StageSince; }
		bool StagesEnded() const { return m_StagesEnded; }                 // every stage fulfilled: the god only keeps the world
		int SignsBuilt() const { return m_SignsBuilt; }
		std::string StageText() const;                                     // `director stage`: the goals and their progress (a contract of the tests)
		std::string StageReport() const;                                   // GBSim --report: "stage: 1, goals 2/4, won on day 21.30" (or "none")
		double WaitLeft(const std::string& action, int clan) const;        // seconds until `action` can fall on the clan again (0 = now)

		// ---- T236: the vote of a clan on an idea one of its people brings (`messenger`): every living adult but the players, the children and
		// the messenger, with its devotion and its bond with the messenger (read only).
		DirectorBallot BallotFor(int clan, const std::string& messenger, const std::map<std::string, float>& traits, float cost) const;
		DirectorVoteResult ForeseeVote(int clan, const std::string& messenger, const std::map<std::string, float>& traits, float cost) const
		{
			return CountDirectorVote(Rules.Vote, BallotFor(clan, messenger, traits, cost));
		}

		// ---- T237: the sign (`director sign`). PreviewSign changes nothing; PlaceSign pays it and leaves it waiting for a messenger; Update
		// carries it on (seen, voted, the site opened, the building standing, or refused, unseen, dropped with their refunds).
		DirectorSignPreview PreviewSign(int clan, const std::string& building, float x, float z) const;
		DirectorReply PlaceSign(int clan, const std::string& building, float x, float z, float yaw = 0.0f);
		const std::vector<DirectorSign>& Signs() const { return m_Signs; }
		const DirectorSign* OpenSign(int clan) const;                      // the clan's sign still waiting or being voted (null: none)
		// Where `camp dx dz` is for this clan (its camp + the offset), so one line of orders holds on any seed; false without the clan.
		bool CampOffset(int clan, float dx, float dz, float& x, float& z) const;

		// ---- T238: the focus of a clan (`director focus <clan> <group>|off [soft|strong] [days]`; DirectorFocus.cpp). CanFocus changes nothing.
		DirectorCheck CanFocus(int clan, const std::string& group, bool strong) const;
		DirectorReply SetFocus(int clan, const std::string& group, bool strong, int days);
		DirectorReply EndFocus(int clan, const std::string& why);
		const DirectorFocus* FocusOf(int clan) const;                      // null: none
		float FocusMultiplier(int clan, const std::string& person, bool strong, const std::string& group) const;   // m(p) of §3.b
		// ---- T239: the calling of a person (`director calling [preview] <person> <role>`). PreviewCalling changes nothing.
		DirectorCallingPreview PreviewCalling(const std::string& person, const std::string& role) const;
		DirectorReply GiveCalling(const std::string& person, const std::string& role);
		const std::vector<DirectorCalling>& Callings() const { return m_Callings; }
		// ---- T240: the vision (`director vision [preview] <clan> <tech>|none`, `director visions`; DirectorVision.cpp). PreviewVision changes nothing.
		DirectorVisionPreview PreviewVision(int clan, const std::string& tech) const;
		DirectorReply GiveVision(int clan, const std::string& tech);
		DirectorReply EndVision(int clan, const std::string& why);
		const DirectorVision* VisionOf(int clan) const;                    // the clan's vision still held as its aim (null: none)
		const std::vector<DirectorVision>& Visions() const { return m_Visions; }

		float Influence() const { return m_Influence; }
		bool Sandbox() const { return m_Sandbox; }
		const std::vector<DirectorEntry>& Book() const { return m_Book; }
		std::string Status() const;

		void Clear();                                                      // a new world: the start influence, nothing else
		std::string ToJson() const;
		bool FromJson(const std::string& json);                            // false (nothing changed) when it is not readable

	private:
		double Now() const { return m_To.Now ? m_To.Now() : 0.0; }
		void Gain(float amount);
		void Write(const std::string& action, const std::string& target, float cost, const std::string& text, int clan);
		void Look(bool count);                                             // reads the clans' counters (count: their deltas become influence)
		DirectorReply Omen(const std::vector<std::string>& args);
		DirectorReply SignCommand(const std::vector<std::string>& args);   // T237: sign [preview] ..., signs
		void UpdateSigns();                                                // T237: nothing to do without signs
		void Refund(int clan, float amount, const std::string& text);      // influence back (not counted as gained from faith)
		std::string PickMessenger(int clan, float x, float z, bool nearOnly) const;
		std::string SignText(const DirectorSign& s) const;
		DirectorReply FocusCommand(const std::vector<std::string>& args);  // T238
		DirectorReply CallingCommand(const std::vector<std::string>& args);   // T239
		void UpdateFocus();                                                // T238/T239: upkeep, drift, the end; and writes the tribes' Bias
		void PushBias();                                                   // writes every clan's TribeBias from m_Focus and m_Callings
		bool FindPerson(const std::string& person, int& clan) const;       // the clan of a living member (by name, or "clan:name")
		std::string FocusToJson() const;                                   // "" with no focus and no calling
		bool FocusFromJson(const std::string& json);
		DirectorReply VisionCommand(const std::vector<std::string>& args);    // T240
		void UpdateVisions();                                              // T240: nothing to do without visions
		std::string VisionsToJson() const;                                 // "" with no vision
		bool VisionsFromJson(const std::string& json);
		void UpdateFaith(int days);                                        // D43: a day of the clans' faith (DirectorFaith.cpp)
		void UpdateAbuse();                                                // T241: tired, skeptics, the heresy split (nothing under the pressure)
		void CheckDefeat();                                                // §6.2 A (only with the stakes on)
		void FaithGain(int clan, float amount);                            // a source of faith: x (1 - F), at Faith.Tired when tired
		DirectorReply DefeatCommand(const std::vector<std::string>& args);
		std::string FaithToJson() const;                                   // the faith, the skeptics, the defeat ("" before the first look)
		bool FaithFromJson(const std::string& json);
		void Chronicle(int clan, const std::string& kind, const std::string& who, const std::string& text);   // an event of the world's history
		void UpdateStage();                                                // D48 (only with the stakes on; reads, never writes the clans nor the history)
		void LookGoal(const DirectorGoalDef& g, DirectorGoalState& s, double now);
		void OpenStage(int index, double now);
		DirectorReply StageCommand(const std::vector<std::string>& args);
		std::string StageToJson() const;                                   // "" before any stage was looked at and with no sign built
		bool StageFromJson(const std::string& json);

		DirectorTargets m_To;
		float m_Influence = 50.0f;
		bool m_Sandbox = false;
		bool m_Looked = false;                                             // the first look only takes the counters
		int m_Day = 0;                                                     // the last day the daily gain was given
		double m_SandboxAt = 0.0;                                          // the sandbox clock's last refill
		std::map<int, float> m_Pressure;
		std::map<int, float> m_FaithSet;                                   // `director faith <clan> <v>` (sandbox only)
		std::map<std::string, double> m_Waits;                             // "action:clan" -> when it can be again
		std::map<int, std::pair<int, int>> m_Seen;                         // clan -> (attendances, omens) last seen
		std::vector<DirectorEntry> m_Book;
		double m_Gained = 0.0;                                             // influence gained from faith over the game (for the status)
		std::vector<DirectorSign> m_Signs;                                 // T237: open ones and the last closed ones (saved only when there are any)
		int m_NextSign = 1;
		std::vector<DirectorFocus> m_Focus;                                // T238: one per clan at most (saved only when there are any)
		std::vector<DirectorCalling> m_Callings;                           // T239: the callings still running (saved only when there are any)
		int m_NextCalling = 1;
		std::vector<DirectorVision> m_Visions;                             // T240: the held ones and the last closed ones (saved only when there are any)
		int m_NextVision = 1;
		std::vector<int> m_Biased;                                        // the clans whose TribeBias was written last (to clear them after)
		std::map<int, float> m_Faith;                                      // D43: each clan's faith in the god
		std::map<int, float> m_RiteShare;                                  // D43: the share of the clan at its rites since the day turned
		std::map<int, int> m_OmenDay;                                      // D43: the omens each clan read since the day turned (one counts)
		std::vector<DirectorSkeptic> m_Skeptics;                           // T241
		std::map<int, double> m_HeresySince;                               // T241: since when a third of the clan are skeptics
		std::map<int, double> m_HeresyLast;                                // D49: the clan's last heresy split (it and the heretics' clan)
		std::map<int, std::string> m_Level;                                // T241: the pressure level last told ("tired", "heresy")
		int m_HeresySplits = 0, m_Refusals = 0, m_Votes = 0;
		bool m_Stakes = false;                                             // §6.2 A: the god's game can be lost
		double m_DefeatAt = -1.0;
		bool m_DefeatSeen = false;
		// D48: the stage open (its index in Rules.Stages.List), its goals by id, when it opened, when it was won (-1: not yet), all of them ended
		int m_Stage = 0;
		bool m_StageOpen = false;                                          // the stage's goals are being looked at (set at the first look)
		std::map<std::string, DirectorGoalState> m_Goals;
		double m_StageSince = 0.0, m_StageWonAt = -1.0, m_StageLook = -1.0;
		bool m_StagesEnded = false;
		int m_StageEvent = 0;                                              // the last event of the world's history looked at (peace)
		double m_LastClash = -1.0;                                         // the last clash between clans seen (peace)
		int m_SignsBuilt = 0;                                              // signs of the god that came to stand (T237 "built")
	};
}
