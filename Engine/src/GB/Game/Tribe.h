#pragma once
#include "GB/AI/Abstract.h"
#include "GB/AI/Memory.h"
#include "GB/AI/Mind.h"
#include "GB/AI/Needs.h"
#include "GB/AI/Perception.h"
#include "GB/AI/Utility.h"
#include "GB/Game/Chronicle.h"
#include "GB/Game/Conflict.h"
#include "GB/Game/Economy.h"
#include "GB/Game/Entity.h"
#include "GB/Game/Exploration.h"
#include "GB/Game/JobBoard.h"
#include "GB/Game/Skills.h"
#include "GB/Game/Knowledge.h"
#include "GB/Game/PlayerMember.h"
#include "GB/Game/Society.h"
#include "GB/Game/Social.h"
#include "GB/Game/Production.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/SiteChoice.h"
#include "GB/Gameplay/Items.h"
#include "GB/Navigation/NavGrid.h"
#include <algorithm>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>

namespace GB
{
	class TelemetrySink;
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
		// H16: may the abstract agent plan a Route now? A body on a grid asks the grid's plan budget (NavGrid::AllowPlan with its own ticket), so far
		// members and actors share the searches of a frame fairly. False: it waits a frame. The default: always.
		virtual bool MayPlan() { return true; }
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
		// Nodes this member could not get to (name -> until when it does not go there again, how many times it failed): the perception sees them whole every
		// moment, so marking them empty in the memory is not enough to stop it trying the same one again at once. Not saved: it is a matter of minutes.
		std::map<std::string, std::pair<double, int>> Unreachable;
		int HomeTries = 0;   // times it failed to walk home from an expedition (after a few it gives up the report: telemetry showed 25,000 in a row)
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
		// H15: a person controls it (GB/Game/PlayerMember.h): the brain never decides for it nor sends its body anywhere, it is never abstract and the
		// review of the trades leaves it alone; the clan counts it (needs, meals, beds, the board, relations) and hears what it does (TribePlayer.cpp).
		bool Player = false;
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
		// Hands the clan always keeps in this trade (JSON "min"), whatever its stores say: the trade without which something essential stops (the
		// woodcutter: no axe, no wood, no fire). PlanRoles gives them first, and the review of the trades never takes the last of them away.
		int Min = 0;
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
		// ---- the camp and its store (not in the content file: set by whoever builds the tribe)
		glm::vec2 Camp{ 0.0f };       // where the store and the campfire are (x, z)
		float CampY = 0.0f;           // ground height there
		int StoreCapacity = 40;       // slots of the main store (the units of one item it takes are the economy rules' StoreCap, see Tribe::StoreCap)
		int DepositAt = 4;            // units in the bag that send a worker to the store
		int Swings = 3;               // gathers per trip to a node
		float FireSeconds = 30.0f;    // the campfire burns FireHeat of fuel this often (its BurnSeconds attribute is FireSeconds / FireHeat)
		float UpkeepSeconds = 60.0f;  // the camp uses one fibre per member this often (thatch, rope)
		int FireWoodRequired = 4;     // wood in the store before the clan builds its campfire ...
		int FireStoneRequired = 3;    // ... and stone
		bool operator==(const TribeEconomy&) const = default;
	};

	// How often the members look around, decide and feel their needs, and how the log is kept.
	struct TribeSimulationConfig
	{
		float DecisionSeconds = 0.5f;     // a member with nothing special to do decides this often
		float PerceptionSeconds = 0.5f;   // the sights of the world are refreshed (and trusted) about this often
		float LodUpdateSeconds = 0.5f;    // the level of detail follows the viewer this often
		float NeedSeconds = 1.0f;         // how often the needs are moved forward
		float BusyThinkSeconds = 2.0f;    // a member walking or working with orders in flight looks around this often (and decides the moment its orders end); 4x that when abstract
		float IdleThinkSeconds = 1.5f;    // a member with nothing to do looks again this often
		int ThinkPerFrame = 4;            // members that look around and decide in one call to Update (the rest wait their turn: the work is spread over frames)
		int LogLines = 800;               // the log is a ring: the oldest lines go
	};

	// How far an unknown-ground trip goes and how long a sighting is trusted.
	struct TribeExplorationConfig
	{
		float ExploreRadius = 22.0f;   // how far from the camp an unknown-ground trip goes ...
		float RadiusPerTrip = 8.0f;    // ... and how much farther each of the next three trips goes
		float FreshSeconds = 6.0f;     // a sighting this recent is trusted as it is; older ones are checked on arrival
	};

	// Being together: telling what one knows, being near the camp and the fire, hearing.
	struct TribeCommunityConfig
	{
		float ShareSeconds = 20.0f;     // the least time between two times a member tells the clan what it knows
		float CompanyRadius = 6.0f;     // within this of another member it is not alone
		float NearCampRadius = 5.0f;    // within this of the camp a member is "at the camp" (and trades what it knows)
		float NearFireRadius = 4.5f;    // within this of a lit fire it warms
		float Hearing = 1.0f;           // scales how far sounds reach the members
	};

	// The level of detail: who is simulated with a body.
	struct TribePerformanceConfig
	{
		float DetailRadius = 60.0f;   // members farther than this from the viewer are abstract (0 = always full detail)
		float DetailMargin = 10.0f;   // ... and become full again within this much less (no flicker at the border)
	};

	struct TribeConfig
	{
		TribeSimulationConfig Simulation;
		TribeExplorationConfig Exploration;
		TribeCommunityConfig Community;
		TribePerformanceConfig Performance;
		float DuskShift = 2.0f;       // hours earlier that dusk and night come in winter (and half of it in autumn)
		NeedsConfig Needs;
		VisionParams Vision;
		MemoryConfig Memory;
		ChooserConfig Chooser;
		SeasonClock Seasons;
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
		EconomyRules Econ;            // H18: production chains, spoilage, fields and stores (DefaultEconomyRules; the "economy" section of a content file)
		SocialRules Social;           // social life: mood, talk, rites, beliefs, diet, traps (DefaultSocialRules; the "social" section, docs/VIDA_SOCIAL.md)
		// The settlement (H8, see Settlement.cpp and docs/ASENTAMIENTOS.md): only with buildings (Tribe::SetBuildings).
		float PlanSeconds = 10.0f;    // how often the clan looks at what it should build
		float SiteRadius = 7.0f;      // the first ring around the camp where buildings go
		int Builders = 2;             // members working one site at the same time
		int HaulAt = 6;               // most units of a material a member carries to a site in one trip
		float SettlementRadius = 40.0f;   // the buildings that count as the clan's: within this of the camp (a clan that migrates leaves the others behind)
		PlayerRules Player = DefaultPlayerRules();   // H15: standing, proposals, gifts... (the "player" section of a content file)
	};

	class Tribe;

	// H13: what the clans around a tribe add to the lives of its members (GB/Game/Clans.h sets it; a tribe alone has none). The options of its own
	// it adds to a member's decision (an envoy going to a meeting), carrying them out and their end (the tribe hands it the options and jobs of the
	// kinds it lists), and word of every gather (whose land the node was on). The hooks in the tribe are one line each.
	struct TribeOutside
	{
		std::vector<std::string> Kinds;   // the option kinds (and jobs) it handles ("envoy")
		std::function<void(Tribe&, TribeMember&, std::vector<UtilityOption>&)> Options;
		std::function<bool(Tribe&, TribeMember&, const UtilityOption&)> Execute;   // false: the orders could not be given
		std::function<void(Tribe&, TribeMember&, const std::string& job)> Finish;
		std::function<void(Tribe&, const TribeMember&, const std::string& node, int units)> Gathered;
	};

	// A member as it leaves one clan for another (H13: a marriage, a move): the member itself (its body, mind, aptitudes), its person in the society
	// (age, sex, genes, personality; its bonds stay behind) and what it knows.
	struct MemberTransfer
	{
		TribeMember Member;
		Person Who;
		bool HasPerson = false;
		AgentKnowledge Knows;
		bool HasKnowledge = false;
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

	class Tribe;
	// What a clan of these roles wants near its camp (SiteWeights::Want): for every node type, the mean of its roles' weights for it, and food half again
	// as much. The tribe must be bound (its node defs and roles). See SiteChoice.h.
	SiteWeights ClanSiteWeights(const Tribe& tribe, const std::vector<std::string>& roles);
	// H8's camp-site chooser for a migrating clan (Tribe::SetSiteChooser, H12): the places of the ground scored by ScoreSite (water, flat ground, biome,
	// shelter) with the resource nodes the CLAN KNOWS (its shared memory) weighed by its members' roles; only places at least a home radius away, near
	// something the clan knows, that a member can reach and that H12's own measure (Tribe::SiteScore) finds worth the move. The ground must outlive the
	// tribe's use of the chooser (the editor sets it again with every new world).
	// With `resources` (a new clan, H13: it knows nothing yet) the nodes it gives are scored instead of the clan's memory, the place is worth its own
	// score and only H8's measure decides.
	CampSiteChooser SettlementSiteChooser(const Tribe& tribe, ResourceGround ground, std::function<std::vector<SiteResource>()> resources = {});

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
		// ---- H16: what the brain costs, and a budget given from outside
		// The members that may look around and decide in the next calls to Update, set by whoever shares one budget among several tribes (ClanWorld's
		// ThinkPerFrame); -1: the tribe's own rule (ThinkPerFrame, or the turns needed so nobody falls behind, whichever is more).
		void SetThinkBudget(int thinks) { m_ThinkBudget = thinks; }
		// The turns needed in a step of `dt` so that no living member falls behind its DecisionSeconds (what the own rule never goes below).
		int ThinksNeeded(float dt) const;
		struct FrameCost
		{
			int Thinks = 0;       // members that looked and decided in the last Update
			int Budget = 0;       // the turns it had
			int Due = 0;          // members whose turn had come (or whose orders ended) and did not get it: they wait for the next frame
			double Ms = 0.0;      // wall time of the last Update (measured; the simulation never reads it)
			double ThinkMs = 0.0; // ... of it, the turns at looking and deciding (the rest is the clocks of the clan: needs, board, economy, conflict, abstract walking)
			double EconomyMs = 0.0, SocietyMs = 0.0, ExploreMs = 0.0, ConflictMs = 0.0, AbstractMs = 0.0;   // ... and some of the clocks (needs and board in Society)
		};
		const FrameCost& LastCost() const { return m_Cost; }
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
		const std::map<std::string, ResourceNodeDef>& NodeDefs() const { return m_Defs; }

		// ---- the settlement (H8): buildings by phases, planned from the clan's needs (Settlement.cpp). Without buildings the tribe builds nothing.
		// The defs must outlive the tribe (the editor's BuildingSystem::Defs); the sites are entities with the tags of GB/Gameplay/Buildings.h.
		// With buildings the tribe also tells the economy (H18) where the stations of its buildings are (SetStations) and what its sites still need (SetDemand).
		void SetBuildings(const std::map<std::string, BuildingDef>* defs);
		const std::map<std::string, BuildingDef>* Buildings() const { return m_Buildings; }
		// The finished buildings of the camp (what makes a clan's claims stronger, H13).
		int BuildingCount() const;
		// The height of the ground (x, z), for the sites; without it the camp's height.
		void SetGround(std::function<float(float, float)> height) { m_Ground = std::move(height); }
		// Whether a tech is known, for a tribe without technologies (SetTechs); with them, what the members know decides (ClanKnowledge, H11).
		void SetTechCheck(std::function<bool(const std::string&)> knows) { m_KnowsTech = std::move(knows); }
		// Beds in the finished shelters of the camp (the society, H9, counts a roof as shelter for births).
		int Beds() const;
		// A station is there: the camp's (Config.Econ.Production.CampStations) or a finished building's.
		bool StationAvailable(const std::string& station) const;
		// What the clan wants built now, most urgent first (empty without buildings, before the campfire and the tools, or with as many sites open as
		// EconomyRules::Construction allows for its members).
		std::vector<BuildingNeed> BuildingNeeds() const;
		// What the open sites still lack: item -> units not yet delivered nor on their way (what the job board would ask to gather or haul).
		std::map<std::string, int> SiteDemand() const;
		// One line per site and building: phase, work and what it lacks.
		std::string SettlementReport() const;
		bool CanBuild(const std::string& building) const;   // the clan knows how: no technology locks it, or a living member knows one that unlocks it (H11)
		// The same as data, for an interface that draws them: where each is, how far it is built and what it gives once done.
		struct SettlementSite { std::string Name, Building; glm::vec2 Pos{ 0.0f }; SiteState State; int Beds = 0; std::string Station, Store; };
		std::vector<SettlementSite> SettlementSites() const;
		// The pyramid of needs as the clan lives it (JobRules::Survival*): days of food in hand (store and bags), how short of it the clan is (0 = it has
		// the days it wants, 1 = none) and the lowest tier not covered: "survival" (food), "warmth" (no fire and no roof), "safety" (a member afraid) or
		// "growth" (everything below is covered: tools, buildings, knowledge, company).
		float FoodDays() const;
		float FoodShortage() const;
		std::string MaslowTier() const;
		float SurvivalFactor(const Job& job) const;   // the pyramid on a job: food jobs worth more while the clan is short of food, the rest worth less
		// What the clan has lived through, row by row (Chronicle.h): `tribe chronicle`. It records by itself while the tribe is updated.
		Chronicle& Record() { return m_Chronicle; }
		const Chronicle& Record() const { return m_Chronicle; }
		// The telemetry of a run (GB/Telemetry/TelemetrySink.h): a row per member and per clan every `every` simulation seconds, the sheet of each individual when it
		// is first seen and every event. The sink belongs to the host and nothing reads it back, so the run is the same with it or without it. Null = off.
		void SetTelemetry(TelemetrySink* sink, int clanId = 0, double every = 60.0);
		TelemetrySink* Telemetry() const { return m_Telemetry; }
		// Starts a site of a building at a place (the planner does this by itself); "" or why not. The name is "<Name> <n>".
		std::string StartSite(const std::string& building, const glm::vec2& at, std::string* name = nullptr);
		// The generic provisions of the finished buildings of the camp (BuildingDef, docs/PROGRESION.md): its defense against raids (H14), how many
		// times as fast wounds heal at a place (1 = no healer near) and where a wounded member rests (before the healer's hut, or at the camp), and
		// what they add to the main store (its cap per item, and the factor on how fast its food goes off: the lowest of them, 1 = none).
		float Defense() const;
		float HealingAt(const glm::vec2& at) const;
		glm::vec2 RecoverSpot(const TribeMember& m) const;
		int StoreBonus() const;
		float StoreKeep() const;

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

		// ---- H18: the economy (TribeEconomy.cpp): production chains, spoilage and preservation, fields, stockpiles and hauling.
		// Who says where a station is: the buildings (H8) give the drying rack, the kiln...; without one only the camp's stations (EconomyRules::
		// Production.CampStations, the workbench) exist. True and `where` set when the clan has one.
		using StationFinder = std::function<bool(const std::string& station, glm::vec2& where)>;
		void SetStations(StationFinder finder) { m_Stations = std::move(finder); }
		bool StationAt(const std::string& station, glm::vec2* where = nullptr) const;
		// Only a finished building's station (not the camp's own): the visuals draw the camp's workbench just while no building gives one.
		bool BuiltStationAt(const std::string& station, glm::vec2* where = nullptr) const;
		// A want from outside the tribe (a building site's materials, H8; a player's order): `units` of `item` for `source` (0 takes it away).
		// The production plan makes what it can of it and the gather jobs ask for the rest.
		void SetDemand(const std::string& source, const std::string& item, int units);
		// What the clan wants made now and how (refreshed with the job board).
		const ProductionPlan& Plan() const { return m_Plan; }
		ProductionPlan PlanNow() const;
		const EconomyState& Economy() const { return m_Econ; }
		EconomyState& Economy() { return m_Econ; }
		// Puts a stockpile of a kind of the rules (EconomyRules::Logistics) at a place: members carry there what it takes when it is nearer than
		// the camp, and haulers bring what it holds to the main store. Null when the kind is unknown or the name is taken.
		Stockpile* AddStockpile(const std::string& name, const glm::vec2& pos, const std::string& kind = "stockpile");
		Stockpile* FindStockpile(const std::string& name);
		// Marks out a field (the farm rules' plots, columns and spacing) with its first plot at `origin`; with a field store kind, a stockpile beside it.
		Field& MarkField(const glm::vec2& origin);
		// Units of one item after which nobody gathers more of it into the main store (EconomyRules::Main().Cap, 40 by default).
		int StoreCap() const;
		// Units of an item in the main store and every stockpile.
		int Stored(const std::string& item) const;
		// Things a member carries before it goes to a store: DepositAt plus what its best container adds (PerBagSlot per extra slot).
		int CarryLimit(const TribeMember& m) const;
		// For people: what is made, spoilt, stored and grown, and the clan's carrying capacity; the fields one by one.
		std::string EconomyReport() const;
		std::string FieldReport() const;

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

		// ---- H13: the clans around (GB/Game/Clans.h; TribeClans.cpp). What they add to the members' lives, and what they need of the tribe to do it.
		void SetOutside(TribeOutside outside) { m_Outside = std::move(outside); }
		// Names other clans already use: a child is not given one (members' names are the actors' and must be unique in the world).
		void SetNameTaken(std::function<bool(const std::string&)> taken) { m_NameTaken = std::move(taken); }
		glm::vec2 Position(const TribeMember& m) const { return Where(m); }
		const ItemLibrary* Items() const { return m_Items; }
		bool MemberBusy(const TribeMember& m) const { return IsBusy(m); }
		glm::vec2 CampSpotOf(const TribeMember& m) const { return CampSpot(m); }
		// Gives a member orders (with its navigation profile) and the job and detail they stand for. False: they could not be given (it says why).
		bool Send(TribeMember& m, const std::string& orders, const std::string& job, const std::string& detail);
		// A line in the log, by a member or by the clan.
		void Note(const TribeMember& m, const std::string& text) { Say(m, text); }
		void NoteClan(const std::string& text);
		// A member leaves the tribe with everything that is its own (body, mind, aptitudes, person, knowledge); false when there is none of that name.
		// Its claims and job go, its bonds stay behind, and its partner (if any) is free.
		bool Detach(const std::string& name, MemberTransfer& out);
		// A member from another tribe joins this one as it was (no founder's draw): it walks to this camp when it next decides.
		void Admit(MemberTransfer&& in);

		// ---- H14: conflict, combat and violence (TribeConflict.cpp; the core is GB/Gameplay/Combat.h, the clan's state GB/Game/Conflict.h).
		// The fights of the world (shared by every clan and the animals: it must outlive the tribe) and who is hostile to whom (H13 gives its own;
		// without one, nobody but the predators of the rules is). Without a combat system there is no conflict at all.
		void SetCombat(CombatSystem* combat, IHostility* hostility = nullptr) { m_Combat = combat; m_Hostility = hostility; }
		CombatSystem* Combat() const { return m_Combat; }
		IHostility* Hostility() const { return m_Hostility; }
		// The clan's name: its side in the fights and in the hostility table ("Clan" by default).
		void SetClanName(const std::string& name) { m_ClanName = name.empty() ? std::string("Clan") : name; }
		const std::string& ClanName() const { return m_ClanName; }
		const ConflictState& Conflict() const { return m_Conflict; }
		// Sends a raid on a clan's camp now (the clan weighs raids by itself too). False and why when it cannot (no such target, too few fit).
		bool StartRaid(const std::string& clan, std::string* why = nullptr);
		// For people: threats, fights, the wounded, hunts and the raid.
		std::string ConflictReport() const;
		// ---- H15: the player inside the society (TribePlayer.cpp, GB/Game/PlayerMember.h). The member a person controls joins like any other (its
		// needs, its knowledge, its person in the society) but the brain never drives it: these calls are what the person does, through keys in the
		// game mode or orders in a test, and what the clan makes of it (standing, bonds, the board, proposals).
		struct PlayerAct
		{
			bool Ok = false;
			std::string Text;   // what happened, or why not
		};
		// Joins `name` as a player member with `role` ("" = PlayerRules::Role); false when the name is taken. SetPlayer marks an existing member.
		bool AddPlayer(const std::string& name, ITribeBody* body, const std::string& role = std::string());
		bool SetPlayer(const std::string& name, bool on);
		// The first living player member (nullptr: none), and any member by name.
		const TribeMember* PlayerMember() const;
		const TribeMember* FindMember(const std::string& name) const;
		const PlayerSociety& Players() const { return m_Players; }
		float Standing(const std::string& player) const;
		bool Leads(const std::string& player) const;
		// The store, from the camp (within PlayerRules::CampReach): everything the bag holds that is not a tool, or `count` of `item`; and back.
		PlayerAct PlayerDeposit(const std::string& player, const std::string& item = std::string(), int count = 0);
		PlayerAct PlayerTake(const std::string& player, const std::string& item, int count = 1);
		// Eats what it carries or the store's oldest food (from the camp), drinks the water it carries, lies down in its bed (or gets up).
		PlayerAct PlayerEat(const std::string& player);
		PlayerAct PlayerDrink(const std::string& player);
		PlayerAct PlayerSleep(const std::string& player, bool on);
		// What the player's body is doing now ("gather" a node, "stoke" the campfire, "build" a site, "" when it is done), as an NPC's job and detail:
		// the visual layer (TribeVisuals: props in the hands, the activity's set and montage, the hands on the work) reads it the same for both.
		bool SetPlayerActivity(const std::string& player, const std::string& job, const std::string& detail = std::string());
		// Gives `count` of `item` from its bag to a member within Reach (their bond grows by the worth of the gift).
		PlayerAct PlayerGive(const std::string& player, const std::string& to, const std::string& item, int count = 1);
		// The job board: claims a job by id ("gather wood"; "" = the most pressing it can do) or lets it go.
		PlayerAct PlayerClaim(const std::string& player, const std::string& job = std::string());
		PlayerAct PlayerRelease(const std::string& player);
		// The host tells the clan what the player did with its abilities: "gather" (`key` a node id or the item, `units` gathered), "craft" (a recipe),
		// "stoke", "fire", "build" (a site): its aptitude practises, its claim moves on, its standing grows, whoever watches may learn (H11).
		PlayerAct PlayerWorked(const std::string& player, const std::string& activity, const std::string& key, float units = 1.0f);
		// Makes a recipe from its bag (at a station of the camp when the recipe asks for one), as `actor bag craft` would, and counts it.
		PlayerAct PlayerCraft(const std::string& player, const std::string& recipe);
		// A proposal to the clan (see ProposalRules): "gather" or "make" an item (`units`), or "build" a building (at `at`, or by the camp when it is 0).
		PlayerAct Propose(const std::string& player, const std::string& kind, const std::string& target, int units = 0, glm::vec2 at = glm::vec2(0.0f));
		// What a member is doing and why (from its mind), its trade, its job and how it feels about the player; a talk now and then brings them closer.
		PlayerAct PlayerTalk(const std::string& player, const std::string& other);
		// A lesson: the player shows a member a technology it knows, or a member shows the player one ("" = the first it can).
		PlayerAct PlayerTeach(const std::string& player, const std::string& other, const std::string& tech = std::string());
		PlayerAct PlayerLearn(const std::string& player, const std::string& other, const std::string& tech = std::string());
		// Marriage with a member that likes the player enough (Society::Marry).
		PlayerAct PlayerMarry(const std::string& player, const std::string& other);
		// H14 for the player: attacks `target` ("" = the nearest foe within reach: a predator, a hunted animal, a stranger of a hostile clan, whoever
		// attacks it), raises its guard (the ability Block) or runs from whoever fights it (Flee). The swings themselves are the combat system's: the
		// person walks its character into reach. A hunted animal killed gives its meat (H14's hunts); a raider driven off or killed counts as defence.
		PlayerAct PlayerAttack(const std::string& player, const std::string& target = std::string());
		PlayerAct PlayerBlock(const std::string& player);
		PlayerAct PlayerFlee(const std::string& player);
		PlayerAct PlayerStop(const std::string& player);   // breaks off the fight the player is in (and the chase of its foe): nothing is lost, the foe may come after it
		bool PlayerFighting(const std::string& player, float* gap = nullptr) const;   // in a fight with a foe still alive; gap = metres beyond its weapon's reach
		// Health, stamina and wounds of a player for the interface ("" without a combat system).
		std::string PlayerCondition(const std::string& player) const;
		// For people: standing, what it has done, its job and bed; the proposals and their votes.
		std::string PlayerReport(const std::string& player) const;
		std::string ProposalsReport() const;
		// Credits a contribution (kind of PlayerRules::Credit, x units) to a player's standing; the clan world calls it for a trade.
		void Credit(const std::string& player, const std::string& kind, float units);

		// ---- social life (docs/VIDA_SOCIAL.md; TribeSocial.cpp, the state in GB/Game/Social.h): mood and grief, conversations, rites and rites of
		// passage, beliefs (taboos, omens, the sacred place, the shaman), the variety of the diet and the clan's traps.
		const SocialState& Social() const { return m_Social; }
		SocialState& Social() { return m_Social; }
		float Mood(const std::string& member) const;
		float Grief(const std::string& member) const;
		// Calls a rite of the rules now (it goes to the front of the agenda; `subject`: whom it is for). False and why when it cannot be called.
		bool StartRite(const std::string& rite, const std::string& subject = std::string(), std::string* why = nullptr);
		// An event of the world the clan reads as an omen ("storm", "eclipse", "harvest", "big_kill", "two_deaths": SocialRules Beliefs.Omens).
		void Omen(const std::string& event);
		// Whether the clan's taboos let it hunt an animal at a place (the sacred place's, when its belief holds it).
		bool HuntAllowed(const glm::vec2& at) const;
		// A hunter killed an animal of a hunt rule (H14): its first kill (a rite), the shared kill (a taboo), a great kill (an omen).
		void Hunted(TribeMember& hunter, const HuntRule& rule, const std::string& animal);
		// For people: moods and talks (or one member's), the agenda and the rite in course, the beliefs, and the hunting (hunts, traps, game near).
		std::string SocialReport(const std::string& member = std::string()) const;
		std::string RiteReport() const;
		std::string BeliefReport() const;
		std::string HuntReport() const;

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
		void RecordRow();   // one row of the chronicle: the clan and each member now
		// The pyramid on a member's options (the last word before it chooses): while the clan or the member itself is short of food, the options
		// that feed it (eat, drink, gather or look for food, carry food) score more and what is not about staying alive scores less.
		void MaslowOptions(const TribeMember& m, float hunger, int foodInBag, bool foodHeld, std::vector<UtilityOption>& options) const;
		void Practise(TribeMember& m, const std::string& activity, const std::string& key, float units);
		float JobScore(const TribeMember& m, const Job& job, const std::map<std::string, Nearest>& known) const;
		void Say(const TribeMember& m, const std::string& text);
		void BuildFire(float fuel);
		void WatchFire();
		glm::vec2 CampSpot(const TribeMember& m) const;
		glm::vec2 FireSpot(const TribeMember& m) const;
		glm::vec2 StandingSpot(const TribeMember& m, const glm::vec2& centre, float angle, float radius, bool path = true) const;
		glm::vec2 FirePos() const { return Config.Economy.Camp + glm::vec2(1.5f, 0.0f); }
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
		// H18 (TribeEconomy.cpp): the clock of spoilage and the fields, the plan and the jobs it posts, the options of a member that holds one
		// of them, carrying them out and their ends, the tools and baskets handed out at the camp, and where a member leaves what it carries.
		void StepEconomy(float dt);
		void PlanEconomy();
		void PostEconomyJobs();
		float EconomyJobScore(const TribeMember& m, const Job& job) const;
		void EconomyOptions(TribeMember& m, std::vector<UtilityOption>& options);
		bool ExecuteEconomy(TribeMember& m, const UtilityOption& option);
		bool FinishEconomy(TribeMember& m, const std::string& job);
		void HandOut(TribeMember& m);
		std::vector<ProductionWant> Wants() const;
		bool ClanKnows(const Recipe& r) const;
		bool CanFarm(const TribeMember& m) const;
		std::string DepositPlace(const TribeMember& m, glm::vec2& where) const;
		int DepositAt(TribeMember& m, const std::string& place);
		std::string FoodToEat(const Inventory& inv) const;
		// social life (TribeSocial.cpp): the clock (omens, seasonal and recurring rites, the rite in course, traps), the talk, rite and snare
		// options and the mood on every option, carrying them out and their ends, a meal (nutrition and diet), a death, a life event of the society
		void StepSocial(float dt);
		void SocialDay();
		void SocialOptions(TribeMember& m, std::vector<UtilityOption>& options);
		bool ExecuteSocial(TribeMember& m, const UtilityOption& option);
		bool FinishSocial(TribeMember& m, const std::string& job);
		void Ate(TribeMember& m, const std::string& item);
		void ChooseMeal(const TribeMember& m, std::string& carried, std::string& stored) const;
		void SocialDied(const TribeMember& m);
		void SocialHears(const SocietyEvent& e);
		void CallRites(const std::string& event, const std::string& subject);
		bool CallRite(const std::string& rite, const std::string& subject, float delay, std::string* why = nullptr);
		void StepRites();
		bool BeginRite(PendingRite& p);
		void EndRite(const std::string& why);
		void StepTraps();
		bool ClanKnowsTech(const std::string& tech) const;
		glm::vec2 RitePlace(const RiteDef& def) const;
		glm::vec2 CircleSpot(const std::string& member) const;
		std::string ChooseTalk(const TribeMember& m, const TribeMember& other, std::string& about) const;
		SocialMember& SocialOf(const std::string& name);
		void MoveMood(const std::string& name, float delta);
		// H15 (TribePlayer.cpp): a player member's turn in Update (it tells the clan what it knows at the camp; the days of its standing; the wants
		// of its proposals), the floor of its health, the members it can reach and the wants a proposal leaves.
		void StepPlayer(TribeMember& m);
		void StepPlayers(float dt);
		void GuardPlayer(const TribeMember& m, AbilitySystemComponent& asc) const;
		void KnockOut(TribeMember& m, const std::string& cause);   // an immortal player who would die: it wakes at the camp, wounds tended
		TribeMember* MemberNamed(const std::string& name);
		bool NearCampForPlayer(const TribeMember& m) const;
		PlayerRecord& RecordOf(const std::string& player);
		// the settlement (Settlement.cpp)
		struct Placed { std::string Name; const BuildingDef* Def = nullptr; glm::vec2 Pos{ 0.0f }; SiteState State; };
		std::vector<Placed> Placements() const;   // every site and building of the scene, by name
		void PlanSettlement(float dt);
		void SettlementOptions(TribeMember& m, std::vector<UtilityOption>& opts) const;
		int ExecuteSettlement(TribeMember& m, const UtilityOption& o);   // -1: not a settlement option; 0: failed; 1: given
		bool FinishSettlement(TribeMember& m, const std::string& job);   // false: not a settlement job
		void ShelterNeeds(TribeMember& m, float dt);
		glm::vec2 SleepSpot(const TribeMember& m) const;                   // its bed in a shelter, else its place at the camp
		glm::vec2 StationSpot(const TribeMember& m, const std::string& station) const;   // where to craft at a station: its building, else the camp
		std::string CraftStation(const std::string& recipe) const;        // the station the recipe can be made at now ("" = none there)
		int InFlight(const std::string& site, const std::string& item) const;
		void PostSettlementJobs();                                         // "haul <site>" and "build <site>" on the job board (H7)
		float SettlementJobScore(const TribeMember& m, const Job& job) const;
		bool SiteWorkable(const std::string& site) const;                 // a site whose current phase can still be worked (Build after it moved on is no fault)
		bool ClanKnowsBuilding(const BuildingDef& def) const;              // no technology locks it, or a living member knows one that unlocks it (H11)
		// H14 (TribeConflict.cpp): the clock of conflict (the members' places and stats in the fights, what happened in them, the animals near,
		// the raid), a member's turn when a threat, a fight, a flight or a raid steers it (true: it does not decide as usual), the options of the
		// hunt, rest and care, carrying them out and their ends, and the hunt jobs.
		void StepConflict(float dt);
		bool ConflictTurn(TribeMember& m);
		void ConflictOptions(TribeMember& m, std::vector<UtilityOption>& options);
		bool ExecuteConflict(TribeMember& m, const UtilityOption& option);
		bool FinishConflict(TribeMember& m, const std::string& job);
		float ConflictJobScore(const TribeMember& m, const Job& job) const;
		void PostConflictJobs();
		void StepRaid(float dt);
		void ConflictEvents();
		void JoinFight(TribeMember& m);
		bool SteerFight(TribeMember& m);
		std::string SeeThreat(TribeMember& m);
		void LeaveFight(TribeMember& m, const std::string& why);
		float Odds(const std::string& member, const std::string& threat) const;

		EntityWorld* m_Entities = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		const ItemLibrary* m_Items = nullptr;
		const RecipeBook* m_Recipes = nullptr;
		std::map<std::string, ResourceNodeDef> m_Defs;
		std::vector<TribeMember> m_Members;
		std::map<std::string, std::string> m_Claims;   // node -> worker that goes there
		Inventory m_Store{ Config.Economy.StoreCapacity, 0.0f };
		std::vector<std::string> m_Log;
		Chronicle m_Chronicle;
		TelemetrySink* m_Telemetry = nullptr;   // not owned
		int m_TelemetryClan = 0;
		double m_TelemetryEvery = 60.0, m_TelemetryNext = 0.0;
		double m_TelemetryRelationsNext = 0.0;   // the bonds are written about once a day
		std::map<std::string, std::string> m_TelemetryLast;   // what each member last decided (kind + target): a decision is written when it changes
		std::set<std::string> m_TelemetryKnown;   // the members whose sheet is written
		void EmitTelemetry();
		void EmitDecision(const TribeMember& m, const std::vector<UtilityOption>& options, const UtilityOption& chosen);
		void EmitEvent(const char* kind, const std::string& who, const std::string& other, const std::string& result, const std::string& text);
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
		const std::map<std::string, BuildingDef>* m_Buildings = nullptr;
		std::function<float(float, float)> m_Ground;
		std::function<bool(const std::string&)> m_KnowsTech;
		float m_PlanTimer = 0.0f;
		mutable std::vector<std::pair<std::string, glm::vec2>> m_Placed;   // the sites and buildings of the scene (name, x z), looked up once per tick
		mutable double m_PlacedAt = -1.0;
		mutable size_t m_PlacedSystems = 0;
		std::set<std::string> m_Announced;   // the buildings whose end the log has told
		std::map<std::string, std::map<std::string, int>> m_SiteDemandSet;   // what the sites asked the economy for (SetDemand), to take back what they no longer need

		SoundBus m_Sounds;
		SpatialMemory m_Clan;
		std::vector<Sight> m_Sights;
		std::map<std::string, size_t> m_SightIndex;
		std::vector<Occluder> m_Occluders;
		OccluderGrid m_OccGrid;
		std::unordered_map<long long, std::vector<size_t>> m_SightCells;   // the sights filed by place, so a look only visits what is near
		std::vector<Occluder> m_OccBuilt;   // what the grid was built from
		float m_LodTimer = 0.0f;
		int m_ThinkBudget = -1;   // H16: set from outside (SetThinkBudget)
		FrameCost m_Cost;
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
		EconomyState m_Econ;          // H18: fields, stockpiles and counters (saved under "economy")
		ProductionPlan m_Plan;
		StationFinder m_Stations;
		std::map<std::string, std::map<std::string, int>> m_Demand;   // source -> item -> units
		TribeOutside m_Outside;       // H13: the clans around (none for a tribe alone)
		std::function<bool(const std::string&)> m_NameTaken;
		PlayerSociety m_Players;      // H15: standing and proposals of the players of the clan (saved under "player")
		bool IsOutside(const std::string& kind) const { return std::find(m_Outside.Kinds.begin(), m_Outside.Kinds.end(), kind) != m_Outside.Kinds.end(); }

		CombatSystem* m_Combat = nullptr;   // H14: the fights of the world (not owned)
		IHostility* m_Hostility = nullptr;
		std::string m_ClanName = "Clan";
		ConflictState m_Conflict;     // H14 (saved under "conflict")
		SocialState m_Social;         // social life (saved under "social")
		struct Animal { std::string Name, Tag; glm::vec2 Pos{ 0.0f }; bool Predator = false; };
		std::vector<Animal> m_Animals;   // the animals of the rules near the camp, refreshed with the conflict clock (not saved: the world has them)

	};
}
