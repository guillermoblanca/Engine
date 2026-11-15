#pragma once
#include "GB/Scene/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// The seasons, from the simulation clock alone (so a saved game has the right season with nothing extra to store).
	enum class Season { Spring, Summer, Autumn, Winter };

	// T14: the weather a season brings, as data (the "climate" section, assets/data/world.json). Each day of the season is wet or dry; a wet day is
	// a storm, snow, rain or a light rain. The shares are over many years: a given year is wetter or drier (ClimateRules::YearSpread).
	struct SeasonWeather
	{
		float Wet = 0.0f;       // 0..1: the share of the season's days that are wet (rain, storm or snow), over many years
		float Persist = 0.0f;   // 0..1: how much a day keeps the weather of the day before (0 = every day apart; more = longer spells, wet and dry)
		float Storm = 0.0f;     // 0..1: the share of the wet days that are storms (a blizzard when it falls as snow)
		float Snow = 0.0f;      // 0..1: the share of the wet days that fall as snow (the cold of the season: snow does not water the fields)
		float Heavy = 0.0f;     // 0..1: the share of the other wet days that are "rain" rather than "lightrain"
		bool operator==(const SeasonWeather&) const = default;
	};

	struct ClimateRules
	{
		float DaySeconds = 1200.0f;   // the length of a day of weather (NeedsConfig::DaySeconds)
		float YearSpread = 0.0f;      // 0..1: each year is wetter or drier than the mean by up to this part (one draw a year: a dry year, a wet year)
		float SeasonSpread = 0.0f;    // 0..1: the same for each season of each year, on top of its year's
		std::array<SeasonWeather, 4> Seasons{};   // by Season
		// T0: the seed of the weather, from the game's master seed (GB/Game/GameSeeds.h, stream "climate"); not in the data. 0 = the fixed calendar
		// of before (SeasonClock::Weather): the clock of the tests and tools that mount no game.
		uint32_t Seed = 0;
		bool operator==(const ClimateRules&) const = default;
	};
	// What the data says (the "climate" section of the game's pack), read once.
	ClimateRules DefaultClimate();
	// Reads the "climate" section: { "climate": { "daySeconds": s, "yearSpread": f, "seasonSpread": f, "seasons": { "<season>": { "wet": f, "persist": f,
	// "storm": f, "snow": f, "heavy": f } } } }. A season given in part keeps the rest. No section changes nothing; on a mistake it says why and changes nothing.
	std::string LoadClimate(const std::string& json, ClimateRules& rules);
	std::string ClimateToJson(const ClimateRules& rules);   // the section's object

	struct SeasonClock
	{
		float SeasonSeconds = 16800.0f;   // one season: two weeks of 1200 s days (NeedsConfig::DaySeconds); a year is four of them, 56 days
		ClimateRules Climate = DefaultClimate();   // T14: the weather of each day (WeatherAt)

		Season At(double seconds) const;
		float Progress(double seconds) const;           // 0..1 through the current season
		static const char* Name(Season s);
		// How fast plants grow and animals breed: spring and summer favour them, autumn slows them, winter all but stops them.
		static float GrowthRate(Season s);
		// How much faster animals get hungry (food is scarce in winter).
		static float HungerRate(Season s);
		// What the season looks like. All of it a function of the season and how far through it we are, so there is nothing to save.
		static float SnowCover(Season s, float progress);        // 0..1 on the ground: it falls early in winter and melts at winter's end
		static glm::vec3 Foliage(Season s, float progress);      // colour of crowns and bushes, blending into the next season's near the end
		static const char* Weather(Season s, float progress);    // "clear", "lightrain", "rain", "storm" or "snow": the fixed calendar of before (seed 0)
		// T14: the weather of the day `seconds` falls in: "clear", "lightrain", "rain", "storm", "snow" or "blizzard". A function of the seed, the data and
		// the clock alone (nothing to save: a loaded game, seeded again from its world, has the same sky). Each season of each year draws how wet it is
		// (the year's and the season's spread around Wet), and its days follow one another as a chain (Persist) from the first day of the season.
		// Climate.Seed 0 gives Weather(At(seconds), Progress(seconds)).
		const char* WeatherAt(double seconds) const;
		// The share of wet days the season of `seconds` has in its year (Wet with the year's and the season's draw); the data's Wet with seed 0.
		float WetnessAt(double seconds) const;
		// Rain that waters the fields: "lightrain", "rain" or "storm" (not snow).
		static bool Rains(const std::string& weather);
	};

	// Basic ecology (H3): animals that wander, eat, breed when well fed and starve when there is nothing to eat. An animal is an entity with an
	// ability system whose attributes are its whole state (Food, Age, Cooldown, Starving), so animals are saved and loaded like everything else; the
	// system only moves and decides. Deterministic: wandering is a hash of the name and the number of legs walked, never a random generator.
	//
	// The species are data (SpeciesDef, the "species" section of a content file: assets/data/world.json). A species grazes plants (rabbits eat the
	// berry bushes and fibre tufts the tribe also gathers, one unit of the node a meal) or hunts other animals (the fox catches rabbits: the prey dies).
	// The numbers of one species' life:
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
		bool operator==(const EcologyConfig&) const = default;
	};

	struct SpeciesDef
	{
		std::string Id;                  // "rabbit"
		std::string Name = "Rabbit";     // the entities are "<Name> <n>"
		std::string Plural = "rabbits";
		std::string Tag = "Animal.Rabbit";
		std::string Body = "rabbit";     // the look (code): "rabbit" (round, long ears) or "quadruped" (four legs and a snout: a fox, a deer)
		glm::vec3 Color{ 0.62f, 0.52f, 0.4f };
		float Size = 1.0f;
		// What it eats: tags of plants (Resource.Plant: it grazes resource nodes) or of animals (Animal.Rabbit: it hunts them). Not both kinds.
		std::vector<std::string> Eats{ "Resource.Plant" };
		float Sight = 20.0f;             // how far it looks for food
		float BreedCost = 15.0f;         // food a birth costs the parent ...
		float NewbornFood = 60.0f;       // ... and the food the young starts with
		float SpawnFood = 0.7f;          // food of one put in the world (Spawn), as a part of FoodMax; it comes adult
		float LegSeconds = 5.0f;         // a new leg of its wander every this many seconds of age
		EcologyConfig Life;
		bool Hunts() const { return !Eats.empty() && Eats.front().rfind("Animal.", 0) == 0; }
		bool operator==(const SpeciesDef&) const = default;
	};

	// The built-in species: "rabbit" (the rabbits as they always were) and "fox" (hunts rabbits); assets/data/world.json says the same.
	std::map<std::string, SpeciesDef> DefaultSpecies();
	// Reads the "species" section of a content file: { "species": { "<id>": { "name": ..., "tag": ..., "eats": [...], "maxAnimals": n, "speed": s, ... } } }.
	// A species is added or replaced whole. A file without the section changes nothing; on a mistake it returns why and changes nothing.
	std::string LoadSpecies(const std::string& json, std::map<std::string, SpeciesDef>& species);
	std::string LoadSpeciesFile(const std::string& path, std::map<std::string, SpeciesDef>& species);
	// The species as the section reads them back (one JSON object: id -> species).
	std::string SpeciesToJson(const std::map<std::string, SpeciesDef>& species);

	// What the editor remembers of the look the season gave the world (the weather, the snow on the ground and the colour of the leaves it last wrote), so a
	// loaded game does not write them again from nothing. Plain text for a save section.
	struct SeasonLook
	{
		bool Enabled = false;
		std::string Weather;
		float Snow = -1.0f;
		glm::vec3 Leaf{ -1.0f };
		std::string ToJson() const;
		bool FromJson(const std::string& text);
	};

	class EcologySystem
	{
	public:
		// What lives here (see SpeciesDef). The editor reads assets/data/world.json into it at start and with `content load`.
		std::map<std::string, SpeciesDef> Species = DefaultSpecies();
		// T0: the seed of the animals' wanders (Hash01), from the game's master seed (GB/Game/GameSeeds.h, stream "ecology"). 0 = the hash of before.
		uint32_t Seed = 0;

		// Height of the ground at x, z (a flat world when empty).
		using Ground = std::function<float(float x, float z)>;

		void Bind(EntityWorld& entities, GameplayWorld& gameplay, Ground ground = {});
		// Puts `count` animals of a species around a point (up to its maximum). 0 for an unknown species.
		int Spawn(const glm::vec3& at, int count, const std::string& species = "rabbit");
		void Update(double clock, float dt);

		int Count() const;                                // every animal
		int Count(const std::string& species) const;
		// Totals over the species: births, deaths of hunger, meals (plants grazed and prey caught) and animals caught by a hunter.
		int Born() const;
		int Starved() const;
		int Eaten() const;
		int Killed() const;
		// "4 rabbits (born 2, starved 0, plants eaten 9)", and "; 1 foxes (born 0, starved 0, caught 3)" for each other species in the world.
		std::string Describe() const;

		// What the animals' attributes do not keep: where each one was heading (its centre of wandering, the plant it chose, how long it has been eating) and
		// the counters. FromJson goes after Bind; "" (a save from before this existed) leaves every animal choosing again from where it stands.
		std::string ToJson() const;
		bool FromJson(const std::string& text);

	private:
		// Plant: the plant it is going to eat, or the prey it chases.
		struct Walk { glm::vec2 Home{ 0.0f }, Target{ 0.0f }; std::string Plant; float Eating = 0.0f; bool Has = false; EntityId Id = NullEntity; };
		struct Counts { int Born = 0, Starved = 0, Eaten = 0, Killed = 0; };
		struct Plant { std::string Name; glm::vec2 At; };

		EntityId Make(const SpeciesDef& species, const glm::vec3& at, float food, float age);
		const SpeciesDef* SpeciesOf(const AbilitySystemComponent& a) const;
		void Kill(const std::string& name);
		float Hash01(const std::string& name, int n, int salt) const;
		float GroundAt(float x, float z) const { return m_Ground ? m_Ground(x, z) : 0.0f; }

		EntityWorld* m_Entities = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		Ground m_Ground;
		std::map<std::string, Walk> m_Walks;
		std::map<std::string, int> m_Serials;     // species -> the last number given to one
		std::map<std::string, Counts> m_Counts;   // species -> what happened to them
		// The plants of each species' diet and where they stand, kept while the scene's systems and entities stay as many (plants do not move;
		// whether one still has something is asked at the moment of choosing).
		std::map<std::string, std::vector<Plant>> m_Plants;
		uint64_t m_PlantsRevision = 0;
		size_t m_PlantsEntities = 0;
		// The systems that may be animals (every species' tag is Animal.<something>), listed again only when the world gains or loses one: the
		// scan of every system (thousands of resource nodes) cost more than the animals themselves.
		mutable TaggedSystems m_Animals{ { "Animal" } };
	};
}
