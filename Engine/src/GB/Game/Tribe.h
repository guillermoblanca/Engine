#pragma once
#include "GB/AI/Memory.h"
#include "GB/AI/Mind.h"
#include "GB/AI/Needs.h"
#include "GB/AI/Perception.h"
#include "GB/AI/Utility.h"
#include "GB/Game/Entity.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Items.h"
#include <glm/glm.hpp>
#include <map>
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
		virtual glm::vec2 Position() const = 0;                           // x, z
		virtual bool Do(const std::string& tasks, std::string& error) = 0;   // a task list (see GB/Game/TaskList.h)
		virtual Inventory& Bag() = 0;
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
	};

	struct TribeConfig
	{
		glm::vec2 Camp{ 0.0f };       // where the store and the campfire are (x, z)
		float CampY = 0.0f;           // ground height there
		float ThinkSeconds = 0.5f;    // how often the members look around and idle ones decide
		int DepositAt = 4;            // units in the bag that send a worker to the store
		int Swings = 3;               // gathers per trip to a node
		float FireSeconds = 30.0f;    // the campfire burns a piece of wood from the store this often
		int StockCap = 40;            // units of one resource in the store after which nobody gathers more of it
		float NeedSeconds = 1.0f;     // how often the needs are moved forward
		float ExploreRadius = 22.0f;  // how far from the camp an unknown-ground trip goes (grows with each trip)
		float FreshSeconds = 6.0f;    // a sighting this recent is trusted as it is; older ones are checked on arrival
		NeedsConfig Needs;
		VisionParams Vision;
		MemoryConfig Memory;
		ChooserConfig Chooser;
		SeasonClock Seasons;
		float Hearing = 1.0f;         // scales how far sounds reach the members
		// Weights of the options by role: kind -> weight (see DefaultRoles). Gathering kinds are node kinds; also "tool" and "fire".
		std::map<std::string, std::map<std::string, float>> Roles;
	};

	// The built-in roles: "forager", "woodcutter", "miner".
	std::map<std::string, std::map<std::string, float>> DefaultRoles();

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

		Inventory& Store() { return m_Store; }
		const Inventory& Store() const { return m_Store; }
		const std::vector<TribeMember>& Members() const { return m_Members; }
		std::vector<TribeMember>& Members() { return m_Members; }
		bool HasFire() const { return m_Fire; }
		bool FireBurning() const { return m_Fire && m_Burning; }
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
		void Listen(TribeMember& m);
		void Share(TribeMember& m);
		void StepNeeds(TribeMember& m, float dt);
		void Die(TribeMember& m);
		void Decide(TribeMember& m);
		bool Execute(TribeMember& m, const UtilityOption& option);
		void Finish(TribeMember& m);
		void Eat(TribeMember& m);
		void Drink(TribeMember& m);
		bool ToolsReady() const;
		bool CanCraftTool(const TribeMember& m) const;
		float RoleWeight(const TribeMember& m, const std::string& kind) const;
		std::string NearestKnown(const TribeMember& m, const std::string& kind, float* distance) const;
		void Say(const TribeMember& m, const std::string& text);
		void BuildFire();
		glm::vec2 CampSpot(const TribeMember& m) const;
		glm::vec2 FireSpot(const TribeMember& m) const;
		glm::vec2 FirePos() const { return Config.Camp + glm::vec2(1.5f, 0.0f); }
		size_t IndexOf(const TribeMember& m) const;
		void Burn(float dt);
		void ShowFlame(bool on);
		std::string WhyNot(const std::string& kind) const;
		bool Order(TribeMember& m, const std::string& orders);

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
		float m_Think = 0.0f, m_NeedTimer = 0.0f;
		bool m_Fire = false;
		bool m_Burning = false;
		float m_FireTimer = 0.0f;

		SoundBus m_Sounds;
		SpatialMemory m_Clan;
		std::vector<Sight> m_Sights;
		std::map<std::string, size_t> m_SightIndex;
		std::vector<Occluder> m_Occluders;
		std::map<std::string, glm::vec2> m_PosCache;
		size_t m_CacheEntities = 0;
	};
}
