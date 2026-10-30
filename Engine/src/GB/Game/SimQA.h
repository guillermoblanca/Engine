#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Game/Entity.h"
#include "GB/Game/ClanHostility.h"
#include "GB/Game/Clans.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/Navigation/NavGrid.h"
#include <deque>
#include <set>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	struct GeneratedWorld;

	// Simulation QA (H20): the invariants a living tribe must keep at every step, the metrics of a run, and a headless harness that runs a whole tribe on
	// a generated world (CPU only: WorldGenerator::GenerateData, the resource nodes the data spreads over it, the real navigation grid) for days of
	// simulated time. The same table of checks is used by the seed sweep of the simulation (tests/sim_sweep_test.cpp), by `qa sim` in the editor (a
	// headless run of the current scene) and by the editor's QA panel (the live scene, checked as it runs). See docs/QA_SIMULACION.md.
	//
	// Extending it (another lane adds a system): write a function that looks at a SimView and reports what is wrong with SimReport::Add, and append it
	// to SimInvariants() (a metric: to SimMetricTable()). Nothing else changes: the sweep, `qa sim` and the panel pick it up.

	// What the checks look at. Pointers the host fills; a check skips what is missing.
	struct SimView
	{
		const Tribe* Clan = nullptr;     // (not "Tribe": a member may not share the name of a type it uses)
		const ClanWorld* Clans = nullptr;   // H13: every clan of the world (the "clans" and "territory" checks; null: one tribe alone)
		const GameplayWorld* Gameplay = nullptr;
		const EntityWorld* Entities = nullptr;
		double Clock = 0.0;   // seconds of simulation
		// Whether a point can be stood on: "" when it can, else what is wrong ("in water 1.6 m deep", "inside an obstacle", "off the map").
		std::function<std::string(const glm::vec2&)> Ground;
	};

	// Where a member is: its abstract agent's place when the tribe simulates it without its body, else its body's.
	glm::vec2 SimMemberPosition(const TribeMember& m);

	// What the checks of a run remember from one step to the next (stuck members, failures over time). One per run; Reset() forgets.
	struct SimTrack
	{
		struct Member
		{
			int Failures = 0;
			std::deque<double> FailedAt;   // when its recent failed trips happened
			glm::vec2 Pos{ 0.0f };
			std::string Doing;             // job and detail when it last moved
			double StillSince = -1.0;
			bool Seen = false;
			std::string Role;              // its trade when last sampled (the metric of trade changes)
			// H9: what it had done when it died (a dead member never acts again: these never change)
			bool DeadSeen = false;
			glm::vec2 DeadPos{ 0.0f };
			int DeadCount = 0;             // Deposited + Made + Meals at death
		};
		std::map<std::string, Member> Members;
		// The tribe's log, read as it grows (the monitor fills NewLines before each check): what a member made, what an experiment spoilt and
		// what was discovered are said there, and some checks and metrics need exactly that.
		struct Line { double At = 0.0; std::string Who, Text; };
		std::vector<Line> NewLines;
		std::string LastLog;                          // the last line already read
		std::map<std::string, int> MadeBy;            // item -> things made (H2, H11)
		std::map<std::string, int> Spoiled;           // item -> units an experiment spoilt (H11)
		int FailedTries = 0;                          // experiments that found nothing
		std::vector<std::string> Discoveries;         // "Flint knapping 812 s (Ana, by experimenting)", in order
		std::set<std::string> ClanTechs;              // what the clan knew at the last look
		std::vector<std::string> ClanGains;           // "flint_knapping 900 s": when the clan came to know each technology
		// H12: the party members of each finished expedition not yet seen back home (expedition id -> member -> when it ended), and the moves of the
		// camp not yet checked (each needs its members at the new camp).
		std::map<int, std::map<std::string, double>> Away;
		std::set<int> SeenExpeditions;
		size_t SeenMoves = 0;
		double LastMoveAt = -1.0;
		std::map<std::string, double> NotArrived;     // member -> when the last move began, until it stood at the new camp
		std::map<std::string, std::pair<size_t, float>> Wounds;   // H14: member -> its wounds and their total severity at the last check
		double ReturnGrace = 900.0;                  // seconds a party member may take to come home after its expedition ends
		double ArriveGrace = 1800.0;                  // seconds a member may take to reach a new camp
		std::map<std::string, float> SiteProgress;    // H8: how far each construction site was built at the last look (it never goes back)
		double FailWindow = 600.0;   // seconds over which failed trips are counted ...
		int FailLimit = 8;           // ... and how many make a member "stuck failing"
		double StillLimit = 900.0;   // seconds a member may stay on the same spot with the same busy job
	};

	// A broken invariant: which check, about what (a member, a node, "store"), what was wrong, when it was first and last seen and how many times.
	struct SimFinding
	{
		std::string Check, Subject, What;
		double First = 0.0, Last = 0.0;
		int Count = 0;
	};

	// Collects what the checks find during one step.
	class SimReport
	{
	public:
		void Add(const std::string& subject, const std::string& what) { Items.push_back({ m_Check, subject, what, 0.0, 0.0, 1 }); }
		void Begin(const std::string& check) { m_Check = check; }
		std::vector<SimFinding> Items;
	private:
		std::string m_Check;
	};

	// One invariant: a name (short, for the panel and the logs), what it promises, and the function that checks it.
	struct SimInvariant
	{
		std::string Name, Promise;
		std::function<void(const SimView&, SimTrack&, SimReport&)> Check;
	};

	// The table of invariants. Built in: counts, claims, stuck, deaths, store, fire, positions, ground, for H7 board and skills, for H11 knowledge
	// for H12 expeditions and migration, for H9 + H10 society and for H8 buildings (see the .cpp and docs/QA_SIMULACION.md).
	// Mutable so another system can add its own at start-up.
	std::vector<SimInvariant>& SimInvariants();

	// The metrics of a run.
	struct SimMetrics
	{
		int Members = 0, Alive = 0, Deaths = 0, Failures = 0, Meals = 0, Made = 0, Deposited = 0;
		int RoleChanges = 0;                    // H7: times a member changed its trade during the run (the review of the trades)
		std::map<std::string, int> DeathsBy;    // cause -> deaths
		std::map<std::string, int> Gathered;    // item -> units that came out of nodes into the members' bags
		long Samples = 0, IdleSamples = 0;      // member-steps alive, and those with nothing to do (rest)
		double Seconds = 0.0;
		bool Fire = false, FireLit = false;
		std::map<std::string, int> MadeBy;      // item -> things made (from the log)
		std::map<std::string, int> Spoiled;     // H11: item -> units spoilt by failed experiments
		int Experiments = 0, FailedTries = 0;   // H11: tries made (failed + discoveries by trying)
		std::vector<std::string> Discoveries;   // H11: "Flint knapping 812 s (Ana, by experimenting)": who found or learnt what and when
		std::vector<std::string> ClanGains;     // H11: "flint_knapping 900 s": when the clan came to know each technology it did not start with
		int Expeditions = 0, ExpeditionsFound = 0, Migrations = 0;   // H12: parties sent (and those that found what they looked for), camps moved
		int RegionsKnown = 0;                   // H12: regions the clan's atlas knows something of
		int Births = 0, Population = 0, PeakPopulation = 0;   // H9: children born, the living at the end and the most there were
		std::map<std::string, int> SpoiledFood, Intermediates, Harvests;   // H18: units that went off, made by "make" jobs (production chains), harvested
		int Hauled = 0;                                       // H18: units hauled from the stockpiles to the main store
		std::map<std::string, double> Extra;    // what SimMetricTable() adds
		double IdlePercent() const { return Samples ? 100.0 * static_cast<double>(IdleSamples) / static_cast<double>(Samples) : 0.0; }
		// One line: "alive 6/6 | gathered berry 41, wood 22 | idle 23% | failures 1 | deaths 0 | meals 54 | made 2 | deposited 80 | fire lit".
		std::string Line() const;
	};

	// A metric another system adds: called at the end of a run (and when the panel refreshes) to fill SimMetrics::Extra.
	struct SimMetricDef
	{
		std::string Name;
		std::function<void(const SimView&, SimMetrics&)> Measure;
	};
	std::vector<SimMetricDef>& SimMetricTable();

	// Runs the table over a view and keeps what it found and the metrics. The host calls Step after each simulation step (or every few).
	class SimMonitor
	{
	public:
		void Reset();
		// Checks every invariant now. Returns how many findings this step made.
		int Step(const SimView& view);
		// Samples the metrics (idle time) without checking: for steps between two checks.
		void Sample(const SimView& view);
		// Closes the metrics with the final state of the tribe (deaths, meals, failures...) and the metric table.
		void Finish(const SimView& view);
		// A member's bag received `count` of `item` from a node (the ResourceSystem's yield): the "gathered" metric.
		void NoteYield(const std::string& item, int count) { m_Metrics.Gathered[item] += count; }

		const std::vector<SimFinding>& Findings() const { return m_Findings; }
		int Violations() const { return static_cast<int>(m_Findings.size()); }
		// Findings per invariant (every invariant of the table is listed, with 0 when it held).
		std::map<std::string, int> CountByCheck() const;
		const SimMetrics& Metrics() const { return m_Metrics; }
		int Steps() const { return m_Steps; }
		// The findings as lines "[check] subject: what (x count, first at t s)", at most `max`.
		std::string Describe(size_t max = 12) const;
	private:
		void ReadLog(const SimView& view);   // the new lines of the tribe's log into m_Track (and its counters)
		SimTrack m_Track;
		std::vector<SimFinding> m_Findings;
		std::map<std::string, size_t> m_Index;   // check + subject + what -> finding
		SimMetrics m_Metrics;
		int m_Steps = 0;
	};

	// ---------------------------------------------------------------- the headless harness

	// A stand-in body for a tribe member that walks the real navigation grid: it carries out the same task text an actor does (goto, wait, ability) with
	// an AbstractAgent that follows the grid's paths with the member's profile, so it goes round water and walls, and a goal it cannot reach fails as it
	// would for an actor. No animation, no physics: what the sweep needs is the decisions, the needs and the economy, at thousands of steps per second.
	class SimBody : public ITribeBody
	{
	public:
		SimBody(std::string name, GameplayWorld& gameplay, const EntityWorld& entities, const NavGrid& nav, const ItemLibrary& items, glm::vec2 start);
		bool Busy() const override { return m_Agent.Busy(); }
		bool Failed() const override { return m_Agent.Failed(); }
		std::string Error() const override { return m_Agent.Error(); }
		glm::vec2 Position() const override { return m_Agent.Position(); }
		bool Do(const std::string& tasks, std::string& error) override { return m_Agent.Do(tasks, error); }
		Inventory& Bag() override { return m_Bag; }
		bool CanStandAt(const glm::vec2& p) const override { return !m_Nav.Ready() || m_Nav.Walkable(p, m_Profile); }
		bool CanReach(const glm::vec2& to) const override;
		void Teleport(const glm::vec2& p) override { m_Agent.Place(p); }
		void SetSuspended(bool on) override { m_Suspended = on; }
		void SetNavigation(const NavProfile& profile) override { m_Profile = profile; }
		bool Route(const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points) const override;
		bool MayPlan() override { return m_Nav.AllowPlan(&m_Ticket); }   // H16: the plan budget, when the run has one (SimSetup::PlanBudget)
		void Update(float dt);
		bool Suspended() const { return m_Suspended; }
		const Inventory& Carried() const { return m_Bag; }
	private:
		std::string m_Name;
		GameplayWorld& m_Gameplay;
		const EntityWorld& m_Entities;
		const NavGrid& m_Nav;
		const ItemLibrary& m_Items;
		AbstractAgent m_Agent;
		Inventory m_Bag{ 12, 0.0f };
		NavProfile m_Profile;
		bool m_Suspended = false;
		PlanTicket m_Ticket;
	};

	// What a headless run is made of.
	struct SimSetup
	{
		const GeneratedWorld* World = nullptr;   // required; must outlive the run
		int Members = 4;                          // when Roster is empty: that many
		std::vector<std::pair<std::string, std::string>> Roster;   // name, role (empty: Members members named by the data, trades from Tribe::PlanRoles as `tribe build`)
		float ResourceDensity = 1.0f;             // of the wild nodes PlanWorldResources spreads (when there is no Scene)
		const NatureConfig* Nature = nullptr;     // forests, stony ground and flower beds on top of them (PlanWorldNature, which then takes its density from Nature->Abundance); null = none
		bool Abstract = false;                    // the viewer far away: every member is an abstract agent (H6 level of detail)
		float Step = 0.1f;                        // seconds per simulation step
		int CheckEvery = 1;                       // run the invariants every this many steps (the metrics are sampled at every one)
		// Optional: a scene to start from (the editor's current one, with its nodes, well and members' entities) instead of planting the world's
		// resources; the camp, the store (Inventory JSON) and the content (when null, era 1 built in).
		const EntityWorld* Scene = nullptr;
		bool HasCamp = false;
		glm::vec2 Camp{ 0.0f };
		std::string Store;
		std::map<std::string, std::string> Bags;   // member -> what it carries (Inventory JSON): its tools above all
		const ItemLibrary* Items = nullptr;
		const RecipeBook* Recipes = nullptr;
		const std::map<std::string, ResourceNodeDef>* Nodes = nullptr;
		const TribeConfig* Config = nullptr;     // roles, work rules (H7), agenda and economy of the clan (null: the built-in ones)
		const TechTree* Techs = nullptr;         // H11: the technologies (null: the built-in DefaultTechs(), what era1.json says)
		const RegionMap* Regions = nullptr;      // H12: the regions (null: cut from the world with the clan's rules and the planned nodes); must outlive the run
		const std::map<std::string, BuildingDef>* Buildings = nullptr;   // H8: with them the clan builds (null: it builds nothing, as before); must outlive the run
		// H16, scale: several clans on the world (a ClanWorld; the first is the tribe of the run, Brain()). Members are shared out among them and each
		// further clan settles where ClanWorld::ChooseHome says (at least ClanApart from every camp), with a well of its own.
		int Clans = 1;
		float ClanApart = 70.0f;
		int ThinkPerFrame = -1;                  // the clan world's shared budget of thinking turns (-1: its default; see ClanWorld::ThinkPerFrame)
		int PlanBudget = 0;                      // searches of the grid per step, shared fairly by every body (0: no budget, as before H16)
		int PathCache = 0;                       // paths the grid keeps for repeated trips (NavConfig::PathCache; 0: none, as before H16)
		int PlanCells = 0;                       // cells the searches of a step may open in all (NavConfig::MaxExpandedPerFrame; 0: no limit)
		bool ViewerAtCamp = false;               // the viewer at the first camp: its members are full, those of camps beyond DetailRadius abstract
		// H14 in the scale runs: a combat system shared by the clans (threats, fights, raids); `HostileClans`: every pair of clans starts hostile
		bool Combat = false;
		bool HostileClans = false;
		// The telemetry of the run (GB/Telemetry/TelemetrySink.h), already started by the host: every clan writes to it. Null = off.
		class TelemetrySink* Telemetry = nullptr;
		double TelemetryEvery = 60.0;
	};

	// A tribe living on a generated world with no window: the harness of the simulation sweep and of `qa sim`. Deterministic: the same setup gives
	// the same run, step for step (Fingerprint).
	class SimRun
	{
	public:
		explicit SimRun(const SimSetup& setup);
		~SimRun();
		bool Ok() const { return m_Error.empty(); }
		const std::string& Error() const { return m_Error; }
		void Run(double seconds);
		void Finish() { m_Monitor.Finish(View()); }
		SimView View() const;
		std::string Fingerprint() const;

		const Tribe& Brain() const { return m_Tribe; }
		Tribe& Brain() { return m_Tribe; }   // (a test may steer it: move the camp, shout...)
		const SimMonitor& Monitor() const { return m_Monitor; }
		const NavGrid& Navigation() const { return m_Nav; }
		const CharacterWorld& Ground() const { return m_Ground; }
		double Clock() const { return m_Clock; }
		size_t Nodes() const { return m_NodeCount; }
		glm::vec2 Camp() const { return m_Tribe.Config.Camp; }
		// H16: the clan world (its clans, when the setup asked for more than one; the first is Brain()) and what the last step cost.
		const ClanWorld& Clans() const { return m_Clans; }
		const CombatSystem& Combat() const { return m_Combat; }
		size_t Bodies() const { return m_Bodies.size(); }
		struct StepCost
		{
			double BodiesMs = 0.0, TribesMs = 0.0, ThinkMs = 0.0, DiplomacyMs = 0.0, CombatMs = 0.0, Ms = 0.0;   // wall time (the simulation never reads it)
			int Thinks = 0, Due = 0;
			NavFrameStats Plans;
		};
		const StepCost& LastCost() const { return m_Cost; }

	private:
		void Frame();

		SimSetup m_Setup;
		std::string m_Error;
		ItemLibrary m_Items;
		RecipeBook m_Recipes;
		std::map<std::string, ResourceNodeDef> m_Defs;
		TechTree m_Techs;
		RegionMap m_Regions;
		std::vector<ResourceSpot> m_Spots;   // the wild nodes planned (none when the run starts from a scene)
		EntityWorld m_Entities;
		GameplayWorld m_Gameplay;
		ResourceSystem m_Resources;
		InteractiveSystem m_Rules;
		BuildingSystem m_Builds;   // H8 (only with SimSetup::Buildings)
		CombatSystem m_Combat;            // H14 (only with SimSetup::Combat): the fights of every clan of the run
		HostilityTable m_Hostility;
		ClanHostility m_ClanHostility;
		Tribe m_Tribe;
		CharacterWorld m_Ground;
		NavGrid m_Nav;
		std::vector<std::unique_ptr<SimBody>> m_Bodies;
		std::map<std::string, size_t> m_Index;
		SimMonitor m_Monitor;
		double m_Clock = 0.0;
		long m_Frames = 0;
		size_t m_NodeCount = 0;
		ClanWorld m_Clans;   // (declared after the tribe: destroyed first, so it unhooks m_Tribe while it lives)
		StepCost m_Cost;
	};

	// "" when a point of a CharacterWorld can be stood on (dry or wadeable, not inside a static obstacle, on the map), else what is wrong.
	std::string SimGroundAt(const CharacterWorld& world, const glm::vec2& p, float maxWade = 1.0f);
}
