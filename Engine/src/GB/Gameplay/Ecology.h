#pragma once
#include "GB/Game/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <functional>
#include <map>
#include <string>

namespace GB
{
	// The seasons, from the simulation clock alone (so a saved game has the right season with nothing extra to store).
	enum class Season { Spring, Summer, Autumn, Winter };

	struct SeasonClock
	{
		float SeasonSeconds = 240.0f;   // one season; a year is four of them

		Season At(double seconds) const;
		float Progress(double seconds) const;           // 0..1 through the current season
		static const char* Name(Season s);
		// How fast plants grow and animals breed: spring and summer favour them, autumn slows them, winter all but stops them.
		static float GrowthRate(Season s);
		// How much faster animals get hungry (food is scarce in winter).
		static float HungerRate(Season s);
	};

	// Basic ecology (H3): rabbits that wander, eat the plants the tribe also gathers (berry bushes, fibre tufts), breed when well fed and starve
	// when there is nothing to eat. An animal is an entity with an ability system whose attributes are its whole state (Food, Age, Cooldown, Starving),
	// so animals are saved and loaded like everything else; the system only moves and decides. Deterministic: wandering is a hash of the name and
	// the number of legs walked, never a random generator.
	struct EcologyConfig
	{
		int MaxAnimals = 8;
		float Speed = 1.3f;              // m/s
		float WanderRadius = 12.0f;      // around where it was born
		float FoodMax = 100.0f;
		float HungerPerSecond = 0.2f;    // 500 s from full to empty
		float HungryBelow = 55.0f;       // looks for food under this
		float EatReach = 1.0f;
		float EatSeconds = 3.0f;
		float FoodPerPlant = 25.0f;
		float BreedAbove = 80.0f;
		float AdultSeconds = 120.0f;
		float BreedCooldown = 150.0f;
		float StarveSeconds = 40.0f;     // at zero food this long before dying
	};

	class EcologySystem
	{
	public:
		EcologyConfig Config;

		// Height of the ground at x, z (a flat world when empty).
		using Ground = std::function<float(float x, float z)>;

		void Bind(EntityWorld& entities, GameplayWorld& gameplay, Ground ground = {});
		// Puts `count` rabbits around a point (up to the maximum).
		int Spawn(const glm::vec3& at, int count);
		void Update(double clock, float dt);

		int Count() const;
		int Born() const { return m_Born; }
		int Starved() const { return m_Starved; }
		int Eaten() const { return m_Eaten; }
		std::string Describe() const;

	private:
		struct Walk { glm::vec2 Home{ 0.0f }, Target{ 0.0f }; std::string Plant; float Eating = 0.0f; bool Has = false; };

		EntityId Make(const glm::vec3& at, float food, float age);
		void Think(const std::string& name, AbilitySystemComponent& a, Entity& e, Walk& w);
		float Hash01(const std::string& name, int n, int salt) const;
		float GroundAt(float x, float z) const { return m_Ground ? m_Ground(x, z) : 0.0f; }

		EntityWorld* m_Entities = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		Ground m_Ground;
		std::map<std::string, Walk> m_Walks;
		int m_Serial = 0, m_Born = 0, m_Starved = 0, m_Eaten = 0;
	};
}
