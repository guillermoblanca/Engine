#pragma once
#include "GB/AI/Abstract.h"
#include "GB/AI/Memory.h"
#include "GB/AI/Mind.h"
#include "GB/AI/Needs.h"
#include "GB/AI/Perception.h"
#include "GB/AI/Utility.h"
#include "GB/Game/Entity.h"
#include "GB/Game/Exploration.h"
#include "GB/Game/JobBoard.h"
#include "GB/Game/Skills.h"
#include "GB/Game/Knowledge.h"
#include "GB/Game/Society.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Items.h"
#include "GB/Navigation/NavGrid.h"
#include <functional>
#include <glm/glm.hpp>
#include <functional>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace GB
{
	// The first tribe (H2 + H3 put together, now driven by the brain of H5 + H6): a handful of workers with a role each who, with no one steering them,
	// gather, carry to a shared store, make their tools there and light a campfire. It is the simplest "society": the same abilities, items and resource
	// nodes the player uses, so what a worker does is what the player could do. Deterministic: ties go to the first option and nothing is random.
	//
	// Each worker is an agent (see GB/AI): it SEES (a cone with obstacles) and HEARS (axe blows, steps, shouts) and remembers what it learned in its
	// own spatial memory, which it shares with the clan's at the camp; its NEEDS (hunger, thirst, rest, warmth, safety, company) are attributes of its
	// ability system; and every few seconds it lists what it could do, scores each option from its needs, its role and what it remembers (utility AI),
	// picks one with hysteresis and carries it out with task lists. The role is only a set of weights on the options ("forager" likes berries, the
	// "woodcutter" trees and the fire, the "miner" rocks); anyone helps with what the tribe lacks and eats, drinks and sleeps when it must.

	// What the tribe needs from a character, so the logic runs the same with a real actor in the editor and with a stand-in in a test.
	class ITribeBody
	{
	public:
		virtual ~ITribeBody() = default;
		virtual bool Busy() const = 0;                                     // still doing the last orders
		virtual bool Failed() const = 0;                                   // the last orders failed
		virtual std::string Error() const { return {}; }                   // why, when it can say
		virtual glm::vec2 Position() const = 0;                           // x, z
		virtual bool Do(const std::string& tasks, std::string& error) = 0;   // a task list (see GB/Game/TaskList.h)
		virtual Inventory& Bag() = 0;
		// Optional (a body that cannot say answers as if everything were fine):
		virtual bool CanStandAt(const glm::vec2&) const { return true; }   // can it be sent to this point (not a wall, deep water...)
		virtual bool CanReach(const glm::vec2& to) const { return CanStandAt(to); }   // ... and is there a way from where it is
		virtual void Teleport(const glm::vec2&) {}                          // put it there (the abstract agent hands its place back to the body)
		virtual void SetSuspended(bool) {}                                  // stop animating and drawing it while the tribe simulates it abstractly
		// How it values the terrain from now on (its role's profile and how loaded it is): its next paths, CanReach and Route use it.
		virtual void SetNavigation(const NavProfile&) {}
		// The way the body would walk from `from` to within `reach` of `to` (see AbstractAgent::Route): the abstract agent walks it so it goes
		// round water and walls as the body would. False: there is no way. The default is the straight line.
		virtual bool Route(const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points) const { (void)from; (void)reach; points.assign(1, to); return true; }
		// H9: how grown it is, 0..1 of an adult's height (a child grows every day; see Society::Growth). A body that cannot change its size ignores it.
		virtual void SetGrowth(float) {}
	};

	struct TribeMember
	{
		std::string Name, Role;
		ITribeBody* Body = nullptr;
		std::string Job, Detail;      // what it is doing now: "gather", "deposit", "craft", "fire", "eat", "sleep", "rest"... and the node or item
		int Deposited = 0;            // units carried to the store
		int Made = 0;                 // tools and objects made
		int Failures = 0;
		std::string Why;              // when resting: why there is nothing to do
		int Meals = 0;                // berries eaten
		bool Hungry = false;          // the store had nothing to eat at the last meal
		bool Dead = false;
		std::string Cause;            // what killed it ("Hunger", "Thirst", "Warmth")
		AgentMind Mind;               // memory, decision and the options it weighed
		bool Abstract = false;        // simulated without its body (far from the camera): see AbstractAgent
		AbstractAgent Agent;
		mutable glm::vec2 Spot{ 0.0f };   // its place at the camp (chosen the first time it is needed)
		mutable bool HasSpot = false;
		int SpotTries = 0;
		// H7: its aptitudes (family -> level, see GB/Game/Skills.h), which rise with the work it does and fall slowly without it, and what it was
		// gathering when its last gather began (the node kind and how many of its item the bag held), to know how much it gathered.
		SkillSet Skills;
		std::string WorkKind;
		int WorkStart = 0;
		// H12: the things it saw were gone (felled, taken) since it last told the clan: the clan forgets them too when it is back at the camp.
		std::vector<std::string> Gone;
	};

	// The parts of the day of the agenda (see TribeAgenda): "night", "dusk" (the hours before the night) and "day" (the rest).
	using AgendaFactors = std::map<std::string, std::map<std::string, float>>;   // part of the day -> option kind ("gather", "sleep"...) -> factor

	// A role: how much it likes each option and the tool of its trade. Data, so a new role or a new kind of node needs no code. The weight of gathering
	// from a node is looked up by the node's id ("fiber_grass"), then by its kind from the most specific up ("Resource.Stone.Flint", then "Resource.Stone"),
	// so a new node of a known kind is already worked; other options are looked up by name ("fire"). Anything the role does not list gets Default.
	struct TribeRole
	{
		std::map<std::string, float> Weights;
		float Default = 0.3f;         // what it gives to what it does not list: anyone helps
		std::string Tool;             // the item it makes and carries for its trade ("" = none)
		std::string ToolTag;          // the tag that counts as having it ("Item.Tool.Axe": a flint axe is one too)
		// How the role walks (H5, JSON "nav"): its weights of slope, water and undergrowth, its limits and how much a load makes slopes and water
		// weigh (LoadFactor). Load itself is not data: the tribe sets it from what the bag holds before every order, so a porter with a full bag
		// goes round the hill it would climb with an empty one.
		NavProfile Nav;
		// Its own agenda (H6, JSON "agenda"): factors that replace the tribe's for this role, part of the day by part of the day and option by option
		// (what it does not list is the tribe's), and NightShift: hours later than the clan that its whole day runs (a night watch: 6).
		AgendaFactors Agenda;
		float NightShift = 0.0f;
		// H7: the role is a profession preset. Skill: the aptitude family it stands for ("woodcutting"): it multiplies the gain and the effect of
		// that family (SkillRules::Profession) and is how the clan finds who is good at it. Fixed: members with this role keep it, and the review
		// of the trades (RoleRules) never gives it to anyone (a guard, a leader set by hand or by data).
		std::string Skill;
		bool Fixed = false;
		bool operator==(const TribeRole&) const = default;
	};

	// The daily agenda of the tribe (H6), as data (the "tribe" section of a content file, see LoadTribeRules): the night is for sleeping, the dusk for coming
	// home, being together and keeping warm, and the work is for the day. Dusk and night come earlier in autumn and winter (TribeConfig::DuskShift).
	struct TribeAgenda
	{
		float DuskHours = 3.0f;       // the dusk is this many hours before the night (NeedsConfig::NightFrom)
		AgendaFactors Factors;        // DefaultAgenda(): night sleep 1.6 and work 0.4; dusk socialize 2.5, warm 1.5, deposit 1.3, explore 0.3
		bool operator==(const TribeAgenda&) const = default;
	};

	// How much of an item the tribe wants in its store before gathering it stops being urgent: the larger of Min and PerMember per member.
	struct StockTarget
	{
		float Min = 12.0f;
		float PerMember = 0.0f;
		bool operator==(const StockTarget&) const = default;
	};

	// What the tribe eats and how much of each thing it wants to keep (the "tribe" section of a content file): no item id in the code.
	struct TribeEconomy
	{
		std::string FoodTag = "Item.Food";          // an item tagged this (or below) is food: eaten when hungry, looked for when there is none ...
		std::string DrinkTag = "Item.Food.Water";   // ... unless it is tagged this (or below): that is drunk, not eaten
		StockTarget DefaultStock;                   // the target of anything Stock does not list (12)
		std::map<std::string, StockTarget> Stock;   // item id -> its target (berry: 6, or 3 per member; wood 10; stone 8)
		bool operator==(const TribeEconomy&) const = default;
	};

	struct TribeConfig
	{
		glm::vec2 Camp{ 0.0f };       // where the store and the campfire are (x, z)
		float CampY = 0.0f;           // ground height there
		float ThinkSeconds = 0.5f;    // how often the members look around and idle ones decide
		int DepositAt = 4;            // units in the bag that send a worker to the store
		int Swings = 3;               // gathers per trip to a node
		float FireSeconds = 30.0f;    // the campfire burns FireHeat of fuel this often (its BurnSeconds attribute is FireSeconds / FireHeat)
		int StockCap = 40;            // units of one resource in the store after which nobody gathers more of it
		float NeedSeconds = 1.0f;     // how often the needs are moved forward
		float ExploreRadius = 22.0f;  // how far from the camp an unknown-ground trip goes (grows with each trip)
		float FreshSeconds = 6.0f;    // a sighting this recent is trusted as it is; older ones are checked on arrival
		int LogLines = 800;           // the log is a ring: the oldest lines go
		float BusyThinkSeconds = 2.0f;   // a member walking or working with orders in flight looks around this often (and decides the moment its orders end); 4x that when abstract
		float IdleThinkSeconds = 1.5f;   // a member with nothing to do looks again this often
		float ShareSeconds = 20.0f;      // the least time between two times a member tells the clan what it knows
		int ThinkPerFrame = 4;        // members that look around and decide in one call to Update (the rest wait their turn: the work is spread over frames)
		float DetailRadius = 60.0f;   // members farther than this from the viewer are abstract (0 = always full detail)
		float DetailMargin = 10.0f;   // ... and become full again within this much less (no flicker at the border)
		float UpkeepSeconds = 60.0f;  // the camp uses one fibre per member this often (thatch, rope) ...
		float SpoilSeconds = 100.0f;  // ... and one berry above 20 in the store spoils this often: sinks for what is gathered in excess
		float DuskShift = 2.0f;       // hours earlier that dusk and night come in winter (and half of it in autumn)
		NeedsConfig Needs;
		VisionParams Vision;
		MemoryConfig Memory;
		ChooserConfig Chooser;
		SeasonClock Seasons;
		float Hearing = 1.0f;         // scales how far sounds reach the members
		// The roles by name (see TribeRole and DefaultRoles); a content file can change them or add more (LoadRoles).
		std::map<std::string, TribeRole> Roles;
		float FireHeat = 2.0f;        // heat the campfire burns every FireSeconds: one wood (fuel 2) or two branches (fuel 1); see ItemDef::Fuel
		// The campfire is an interactive (H3): the entity FireName built from the def FireDef (the "fire_pit" of the interactives), lit with the
		// kit's FireHeat and fed by a member who takes a piece of wood from the store and Stokes it, when its Fuel is at StokeBelow or less.
		std::string FireName = "Campfire";
		std::string FireDef = "fire_pit";
		float StokeBelow = 2.0f;
		TribeAgenda Agenda;           // the agenda of the day (DefaultAgenda); a role can change its part (TribeRole::Agenda)
		TribeEconomy Economy;         // what is food and how much of each thing the store should hold (DefaultEconomy)
		float FullLoad = 8.0f;        // things in a bag with no weight limit that make a member fully loaded (NavProfile::Load 1); a bag with a limit uses its weight
		WorkRules Work;               // H7: names, aptitudes, the job board and the review of the trades (DefaultWorkRules; the "work" section of a content file)
		ExplorationRules Explore;     // H12: regions, expeditions and migration (DefaultExplorationRules; the "exploration" section of a content file)
		SocietyRules Society;         // H9 + H10: life, births, genes, personality, relations and culture (DefaultSocietyRules; the "society" section)
	};

	// The built-in roles: "forager", "woodcutter", "miner" (assets/data/era1.json says the same in its "roles" section).
	std::map<std::string, TribeRole> DefaultRoles();
	// Reads the "roles" section of a content file: { "roles": { "<name>": { "weights": { "<key>": w }, "default": w, "tool": "<item>", "toolTag": "<tag>" } } }.
	// A role is added or replaced whole. Other sections are ignored, and a file without "roles" changes nothing. On a mistake it returns why and changes nothing.
	std::string LoadRoles(const std::string& json, std::map<std::string, TribeRole>& roles);
	std::string LoadRolesFile(const std::string& path, std::map<std::string, TribeRole>& roles);
	// The roles as the "roles" section reads them back (one JSON object: name -> role).
	std::string RolesToJson(const std::map<std::string, TribeRole>& roles);

	// The built-in agenda and economy (assets/data/era1.json says the same in its "tribe" section).
	TribeAgenda DefaultAgenda();
	TribeEconomy DefaultEconomy();
	// Reads the "tribe" section of a content file into an agenda and an economy (the content pack's "tribe" section, see GB/Game/GameContent.h):
	// { "tribe": { "agenda": { "duskHours": 3, "night": { "sleep": 1.6 }, "dusk": { ... }, "day": { ... } },
	//              "food": "Item.Food", "drink": "Item.Food.Water", "stock": { "default": 12, "berry": { "min": 6, "perMember": 3 }, "wood": 10 } } }.
	// An "agenda" replaces the whole agenda, and "food", "drink" or "stock" the whole economy; a file without the section changes nothing; on a mistake it
	// returns why and changes nothing.
	std::string LoadTribeRules(const std::string& json, TribeAgenda& agenda, TribeEconomy& economy);
	std::string LoadTribeRules(const std::string& json, TribeConfig& config);   // into config.Agenda and config.Economy
	std::string LoadTribeRulesFile(const std::string& path, TribeConfig& config);
	// The agenda and the economy as the "tribe" section reads them back (one JSON object).
	std::string TribeRulesToJson(const TribeConfig& config);

	class Tribe
	{
	public:
		TribeConfig Config;

		Tribe();
		void Bind(EntityWorld& entities, GameplayWorld& gameplay, const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes);
		void AddMember(const std::string& name, const std::string& role, ITribeBody* body);
		void Clear();
		void Update(float dt);
		// The clock of the world in seconds (seasons and the hour of the day follow it); without it the tribe uses its own time.
		void SetWorldClock(double seconds) { m_Clock = seconds; m_ClockSet = true; }
		// Where the camera is (x, z): members far from it are simulated as abstract agents. Without a viewer everyone is full.
		void SetViewer(const glm::vec2& pos) { m_Viewer = pos; m_HasViewer = true; }
		void ClearViewer() { m_HasViewer = false; }
		int AbstractCount() const { int n = 0; for (const TribeMember& m : m_Members) if (m.Abstract) ++n; return n; }
		// Takes a member out of the tribe (its claims go too). False when there is none with that name.
		bool RemoveMember(const std::string& name);
		// The hours of the day: 1 for a normal hour; what the daily agenda makes of an option now (see IA.md). With a role, that role's agenda.
		float AgendaFactor(const std::string& kind, float hour, Season season) const { return AgendaFactor(std::string(), kind, hour, season); }
		float AgendaFactor(const std::string& role, const std::string& kind, float hour, Season season) const;
		// How a member walks now: its role's profile with the load of its bag (see TribeRole::Nav).
		NavProfile Navigation(const TribeMember& m) const;
		// Whether an item is food for the tribe, and the stock it wants of an item (see TribeEconomy).
		bool IsFood(const std::string& item) const;
		float StockWanted(const std::string& item) const;
		// How much a role likes an option: a node id ("flint_outcrop", weighed by its kind when the role does not name it) or "fire" (see TribeRole).
		float RoleWeight(const std::string& role, const std::string& kind) const;
		// ---- H7: aptitudes, derived roles and the job board (TribeWork.cpp)
		// How much a member likes an option: its role's weight plus what its aptitude for the option's family adds (SkillRules::Weight).
		float MemberWeight(const TribeMember& m, const std::string& kind) const;
		// The aptitude family of a node id or an option name ("" = none) and a member's level in it.
		std::string SkillFamilyOf(const std::string& kind) const { return SkillFamily(Config.Work.Skills, kind, &m_Defs); }
		float SkillOf(const TribeMember& m, const std::string& family) const { return SkillLevel(m.Skills, family); }
		// The roles a clan of `count` members should have, from its needs (the stock it wants of what each role gathers best, and the campfire):
		// the i-th entry is the role of the i-th member. Deterministic. With `current`, the needs of now (what the store lacks) weigh in too.
		std::vector<std::string> PlanRoles(int count, bool current = false) const;
		const JobBoard& Board() const { return m_Board; }
		// The job board for people: one line per job, then who holds what.
		std::string JobsReport() const;
		// Refreshes the job board and reviews the trades at once (they also run on their own clocks inside Update).
		void PostJobs();
		bool ReviewRoles();

		// ---- H12: regions, expeditions and migration (TribeExplore.cpp). Without regions none of it runs (the members still explore around the camp).
		// The regions of the world the tribe lives in (they must outlive it, or be set again) and the height of the ground (for a camp that moves).
		void SetRegions(const RegionMap* regions, std::function<float(float, float)> height = {});
		const RegionMap* Regions() const { return m_Regions; }
		const ExplorationState& Exploration() const { return m_Explore; }
		// Who chooses where the camp moves to (the settlement work, H8, can give its own); without one, ChooseCampSite.
		void SetSiteChooser(CampSiteChooser chooser) { m_SiteChooser = std::move(chooser); }
		// What a place is worth as a home, 0..~1.3, from what the clan knows grows near it, the water and the danger (see EXPLORACION.md).
		float SiteScore(const glm::vec2& at) const;
		// The built-in chooser: the best dry place of the regions the clan has seen, at least a home radius away.
		bool ChooseCampSite(const CampSiteRequest& request, CampSite& site) const;
		// Moves the camp (the store, the campfire and the members' places go with it); the members walk there. False without members alive.
		bool MoveCamp(const glm::vec2& to, const std::string& why);
		// For people: the expedition, the home and the moves; and what the clan knows of a region.
		std::string ExploreReport() const;
		std::string RegionLoreReport(int region) const;

		Inventory& Store() { return m_Store; }
		const Inventory& Store() const { return m_Store; }
		const std::vector<TribeMember>& Members() const { return m_Members; }
		std::vector<TribeMember>& Members() { return m_Members; }
		// The defs the campfire is built from (the editor's InteractiveSystem::Defs); without them the built-in ones.
		void SetInteractives(const std::map<std::string, InteractiveDef>* defs) { m_InteractiveDefs = defs; }
		// The campfire has been built and its interactive is in the scene / it is lit (State.Lit: its Fuel is not spent).
		bool HasFire() const;
		bool FireBurning() const;
		float FireFuel() const;
		double Time() const { return m_Time; }
		double WorldSeconds() const { return m_ClockSet ? m_Clock : m_Time; }
		Season CurrentSeason() const { return Config.Seasons.At(WorldSeconds()); }
		float Hour() const { return HourOfDay(Config.Needs, WorldSeconds()); }
		// What happened, one line each, with the time: "12.5 Ana (forager) gathers fiber_grass 1".
		const std::vector<std::string>& Log() const { return m_Log; }
		// A summary for people: the store, who does what and what has been made.
		std::string Report() const;

		// ---- the senses and the clan's knowledge
		SoundBus& Sounds() { return m_Sounds; }
		const SpatialMemory& ClanMemory() const { return m_Clan; }
		const std::vector<Occluder>& Occluders() const { return m_Occluders; }
		// A shout at a place: whoever is within `radius` hears it, learns where it came from and feels less safe.
		void Shout(const std::string& who, const glm::vec2& pos, float radius = 30.0f);
		// The needs of a member as numbers (the attributes of its ability system), for panels and tests; empty when it has none.
		std::map<std::string, float> NeedLevels(const TribeMember& m) const;
		// Why it is doing what it does: the decision and its best factors, in a sentence.
		std::string Because(const TribeMember& m) const;

		// ---- knowledge and technology (H11, GB/Game/Knowledge.h): what each member knows, what the clan knows, and how it is found. Without
		// technologies (the default) nothing is gated. The technologies must outlive the tribe (the editor's content).
		void SetTechs(const TechTree* techs) { m_Knowledge.SetTechs(techs); }
		ClanKnowledge& Knowledge() { return m_Knowledge; }
		const ClanKnowledge& Knowledge() const { return m_Knowledge; }

		// ---- society (H9 + H10, GB/Game/Society.h; the hooks are in TribeSociety.cpp): ages, families, genes, personality, relations and culture.
		Society& People() { return m_Society; }
		const Society& People() const { return m_Society; }
		// Gives a newborn a body: the editor spawns an actor, a test a stand-in (its ability system must exist when it returns). `at` is where its
		// mother is; `growth` its height (0..1 of an adult's). nullptr: no body, no child. Without a spawner the clan has no children.
		using Spawner = std::function<ITribeBody*(const std::string& name, const glm::vec2& at, float growth)>;
		void SetSpawner(Spawner spawner) { m_Spawner = std::move(spawner); }
		// Something happened between two members (the editor and tests: "rescue", "theft"...; the tribe itself: "work", "food", "camp", "conflict",
		// "teach"): their affinity moves by the event's weight (SocietyRules Relations.Events).
		void Relate(const std::string& a, const std::string& b, const std::string& event, float scale = 1.0f) { m_Society.Relate(Config.Society, a, b, event, scale); }
		// One simulated day of the society now (it also runs on its own clock inside Update): ages, deaths, pairs, births, culture.
		void SocietyDay();
		// For people: the family and character of a member, its relations, and the culture of the clan.
		std::string FamilyReport(const std::string& name) const;
		std::string RelationsReport(const std::string& name) const;
		std::string CultureReport() const { return m_Society.Culture(Config.Society); }

		// ---- saving: the store, the fire, the clan and each member's memory and decision (their needs are in their ability systems).
		std::string ToJson() const;
		// Members must already be added (AddMember). `reissue`: give the orders again to those who were in the middle of a job (their bodies lost them).
		bool FromJson(const std::string& json, bool reissue);
		// Names and roles in a saved tribe, to recreate the members before FromJson.
		static std::vector<std::pair<std::string, std::string>> SavedMembers(const std::string& json);

	private:
		struct Sight { std::string Name, Kind; glm::vec2 Pos{ 0.0f }; int Amount = -1; };

		void RefreshSights();
		void Look(TribeMember& m);
		bool IsBusy(const TribeMember& m) const;
		bool HasFailed(const TribeMember& m) const;
		glm::vec2 Where(const TribeMember& m) const;
		bool Run(TribeMember& m, const std::string& orders, std::string& error);
		std::string WhyFailed(const TribeMember& m) const;
		void SetDetail(TribeMember& m, bool full);
		void Upkeep(float dt);
		void StepAbstract(float dt);
		void Listen(TribeMember& m);
		void Share(TribeMember& m);
		void StepNeeds(TribeMember& m, float dt);
		void Die(TribeMember& m, const std::string& cause = std::string());   // cause: what killed it (default: the need that ran out)
		void Decide(TribeMember& m);
		bool Execute(TribeMember& m, const UtilityOption& option);
		void Finish(TribeMember& m);
		void Eat(TribeMember& m);
		void Drink(TribeMember& m);
		bool ToolsReady() const;
		bool CanCraftTool(const TribeMember& m) const;
		float RoleWeight(const TribeMember& m, const std::string& kind) const { return MemberWeight(m, kind); }
		bool IsProfession(const TribeMember& m, const std::string& family) const;
		float StockFor(const std::string& item, size_t members) const;
		struct Nearest { std::string Name; float Distance = 1e30f; bool Any = false; };
		std::map<std::string, Nearest> ScanMemory(const TribeMember& m) const;
		// H7 (TribeWork.cpp): claims the job worth most to the member; the bonus of its job on the options that do it; what the work taught it.
		void ClaimJob(TribeMember& m, const std::map<std::string, Nearest>& known);
		void JobBonus(const TribeMember& m, std::vector<UtilityOption>& options) const;
		void Practise(TribeMember& m, const std::string& activity, const std::string& key, float units);
		float JobScore(const TribeMember& m, const Job& job, const std::map<std::string, Nearest>& known) const;
		void Say(const TribeMember& m, const std::string& text);
		void BuildFire(float fuel);
		void WatchFire();
		glm::vec2 CampSpot(const TribeMember& m) const;
		glm::vec2 FireSpot(const TribeMember& m) const;
		glm::vec2 StandingSpot(const TribeMember& m, const glm::vec2& centre, float angle, float radius, bool path = true) const;
		glm::vec2 FirePos() const { return Config.Camp + glm::vec2(1.5f, 0.0f); }
		size_t IndexOf(const TribeMember& m) const;
		std::string WhyNot(const std::string& kind) const;
		int FoodIn(const Inventory& inv) const;
		std::string FirstFood(const Inventory& inv) const;
		void Steer(TribeMember& m) const;   // hands the body its navigation profile (role and load) before it is sent anywhere
		bool Order(TribeMember& m, const std::string& orders);
		// knowledge (H11): the members as the knowledge sees them, what it says in the log, and someone seeing `m` use a recipe or a node
		std::vector<KnowledgeAgent> KnowledgeView(bool lesson = false) const;   // lesson: with who teaches and learns how well (H9, H10)
		// society (H9 + H10, TribeSociety.cpp): the clock of the days, the life events, the factors on the options, the events between members
		void StepSociety(float dt);
		void SocietyEvents(const std::vector<SocietyEvent>& events);
		void Born(const std::string& mother, const std::string& father);
		void SocietyFactors(const TribeMember& m, std::vector<UtilityOption>& options) const;
		float WorkPace(const TribeMember& m, const std::string& family) const { return m_Society.WorkPace(Config.Society, m.Name, family); }
		bool IsChild(const TribeMember& m) const { return m_Society.IsChild(Config.Society, m.Name); }
		void Worked(const TribeMember& m);                     // gathered beside others: closer to them
		void SharedFood(const TribeMember& m);                 // ate what another brought to the store
		void Quarrel(const TribeMember& m, const std::string& node);   // found a node emptied by another
		void ClanEvent(const std::string& event);              // counted towards the traditions
		void Tell(const std::vector<KnowledgeEvent>& events);
		void Witnessed(const TribeMember& m, const std::string& recipe, const std::string& node);
		// H12 (TribeExplore.cpp): the clock, the options of a party member or of a moving clan, carrying them out and their ends, and the map the
		// members tell at the camp.
		void StepExploration(float dt);
		void ExploreOptions(TribeMember& m, std::vector<UtilityOption>& options);
		bool ExecuteExplore(TribeMember& m, const UtilityOption& option);
		void FinishExplore(TribeMember& m, const std::string& job);
		void ShareAtlas(TribeMember& m);
		void PostExpedition();
		float ExpeditionJobScore(const TribeMember& m, const Job& job) const;
		void StepExpedition(float step);
		void CheckHome(float step);
		void EndExpedition();
		bool ChooseExpeditionRegion(const std::string& item, int& region, std::string& why) const;
		bool KnowsNodeOf(const std::string& item, bool clanOnly) const;
		bool CanGatherItem(const TribeMember& m, const std::string& item) const;
		void RefreshLore();
		void TellClan(const std::string& text);

		EntityWorld* m_Entities = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		const ItemLibrary* m_Items = nullptr;
		const RecipeBook* m_Recipes = nullptr;
		std::map<std::string, ResourceNodeDef> m_Defs;
		std::vector<TribeMember> m_Members;
		std::map<std::string, std::string> m_Claims;   // node -> worker that goes there
		Inventory m_Store{ 40, 0.0f };
		std::vector<std::string> m_Log;
		double m_Time = 0.0, m_Clock = 0.0;
		bool m_ClockSet = false;
		float m_Think = 0.0f, m_NeedTimer = 0.0f, m_UpkeepTimer = 0.0f, m_SpoilTimer = 0.0f;
		size_t m_Cursor = 0;                           // the member that gets the next turn at deciding
		double m_SightsTime = -1e9;
		glm::vec2 m_Viewer{ 0.0f };
		bool m_HasViewer = false;
		std::map<std::string, AbilityRuntime*> m_Runtimes;   // the runtime of each abstract member's ability system, lent back when it is full again
		std::map<std::string, std::vector<size_t>> m_ByKind;   // the sights by kind of node
		bool m_Fire = false;          // the tribe built its campfire (the interactive Config.FireName)
		bool m_WasBurning = false;    // what the log last said of it (it says when it goes out and when it burns again)
		const std::map<std::string, InteractiveDef>* m_InteractiveDefs = nullptr;

		SoundBus m_Sounds;
		SpatialMemory m_Clan;
		std::vector<Sight> m_Sights;
		std::map<std::string, size_t> m_SightIndex;
		std::vector<Occluder> m_Occluders;
		OccluderGrid m_OccGrid;
		std::unordered_map<long long, std::vector<size_t>> m_SightCells;   // the sights filed by place, so a look only visits what is near
		std::vector<Occluder> m_OccBuilt;   // what the grid was built from
		float m_LodTimer = 0.0f;
		JobBoard m_Board;             // H7: the clan's jobs and who holds them
		float m_BoardTimer = 0.0f, m_ReviewTimer = 0.0f;
		std::map<std::string, glm::vec2> m_PosCache;
		size_t m_CacheEntities = 0;
		ClanKnowledge m_Knowledge;
		ExplorationState m_Explore;   // H12: the clan's map, its expedition and its moves (saved under "explore")
		const RegionMap* m_Regions = nullptr;
		std::function<float(float, float)> m_Height;
		CampSiteChooser m_SiteChooser;
		Society m_Society;            // H9 + H10
		Spawner m_Spawner;
		float m_CampTimer = 0.0f;     // members sitting together at the camp grow closer this often (Relations.CampSeconds)
		std::string m_FoodBy;         // who last brought food to the store
		std::map<std::string, std::string> m_LastWorker;   // node -> who last gathered from it
	};
}
