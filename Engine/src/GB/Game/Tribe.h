#pragma once
#include "GB/Game/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Items.h"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// The first tribe (H2 + H3 put together): a handful of workers with a role each who, with no one steering them, gather, carry to a shared store,
	// make their tools there and light a campfire. It is the simplest "society": rules over the same abilities, items and resource nodes the player
	// uses, so what a worker does is what the player could do. Deterministic: ties are broken by name and nothing is random.
	//
	// Roles: "forager" (fibre, then berries), "woodcutter" (needs an axe) and "miner" (needs a pickaxe). A worker that lacks its tool goes to the
	// camp to make it when the store has the materials; when it has not, it gathers what is missing. Workers deposit what they carry at the camp
	// when the bag fills, and the woodcutter builds the campfire once the store can pay for it.

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
		std::string Job, Detail;      // what it is doing now: "gather", "deposit", "craft", "fire", "rest" and the node or item
		int Deposited = 0;            // units carried to the store
		int Made = 0;                 // tools and objects made
		int Failures = 0;
		std::string Why;              // when resting: why there is nothing to do
		int Meals = 0;                // berries eaten
		bool Hungry = false;          // the store had nothing to eat at the last meal
	};

	struct TribeConfig
	{
		glm::vec2 Camp{ 0.0f };       // where the store and the campfire are (x, z)
		float CampY = 0.0f;           // ground height there
		float ThinkSeconds = 0.5f;    // how often idle members decide
		int DepositAt = 4;            // units in the bag that send a worker to the store
		int Swings = 3;               // gathers per trip to a node
		float EatSeconds = 40.0f;     // every worker eats a berry from the store this often
		float FireSeconds = 30.0f;    // the campfire burns a piece of wood from the store this often
		int StockCap = 40;            // units of one resource in the store after which nobody gathers more of it
	};

	class Tribe
	{
	public:
		TribeConfig Config;

		void Bind(EntityWorld& entities, GameplayWorld& gameplay, const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes);
		void AddMember(const std::string& name, const std::string& role, ITribeBody* body);
		void Clear();
		void Update(float dt);

		Inventory& Store() { return m_Store; }
		const Inventory& Store() const { return m_Store; }
		const std::vector<TribeMember>& Members() const { return m_Members; }
		bool HasFire() const { return m_Fire; }
		bool FireBurning() const { return m_Fire && m_Burning; }
		double Time() const { return m_Time; }
		// What happened, one line each, with the time: "12.5 Ana (forager) gathers fiber_grass 1".
		const std::vector<std::string>& Log() const { return m_Log; }
		// A summary for people: the store, who does what and what has been made.
		std::string Report() const;

	private:
		void Think(TribeMember& m);
		void Finish(TribeMember& m);
		bool ToolsReady() const;
		bool StartGather(TribeMember& m, const std::string& kind);
		bool StartCraftTrip(TribeMember& m, const std::string& tool);
		std::string NearestNode(const TribeMember& m, const std::string& kind) const;
		void Say(const TribeMember& m, const std::string& text);
		void BuildFire();
		glm::vec2 CampSpot(const TribeMember& m) const;
		void Consume(float dt);
		void ShowFlame(bool on);
		std::string WhyNot(const std::string& kind) const;

		EntityWorld* m_Entities = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		const ItemLibrary* m_Items = nullptr;
		const RecipeBook* m_Recipes = nullptr;
		std::map<std::string, ResourceNodeDef> m_Defs;
		std::vector<TribeMember> m_Members;
		std::map<std::string, std::string> m_Claims;   // node -> worker that goes there
		Inventory m_Store{ 40, 0.0f };
		std::vector<std::string> m_Log;
		double m_Time = 0.0;
		float m_Think = 0.0f;
		bool m_Fire = false;
		bool m_Burning = false;
		float m_EatTimer = 0.0f, m_FireTimer = 0.0f;
	};
}
