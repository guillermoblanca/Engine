#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Gameplay/Garden.h"
#include "GB/Save/Replay.h"
#include <functional>
#include <map>
#include <memory>

namespace GB::Save
{
	// A game with no window, the way tests, tools and replays run it: entities, ability systems, the garden, characters walking on flat ground
	// and a clock in fixed ticks. Without a generated world (that needs a GL context) the seed scatters trees and rocks of its own.
	//
	// Commands (what a replay records):
	//   garden build <x> <z>                  the vegetable garden
	//   spawn <entity> [x z yaw_degrees]      a character that walks (at the entity's place by default)
	//   input <entity> <right> <forward> [run]  what the character holds (0 0 = stop)
	//   activate <entity> <Ability> [target]  uses an ability
	//   fell <entity>                          destroys it (a tree)   |  add <name> <x> <y> <z>   a cube
	//   move <entity> <x> <y> <z>              |  time <hours>   |  weather <preset>
	struct SessionConfig
	{
		WorldParams World;
		double Dt = 1.0 / 60.0;
		float DayLengthSeconds = 1200.0f;
		// Replaces the built-in scatter as the generated world (the editor passes the real generator).
		std::function<bool(const WorldParams&, EntityWorld&, std::string&)> Baseline;
	};

	class GameSession : public ISimulation
	{
	public:
		explicit GameSession(SessionConfig config = {});

		// A new world from the seed.
		void Reset(const WorldParams& world);

		EntityWorld& MutableEntities() { return m_Entities; }
		GameplayWorld& MutableGameplay() { return m_Gameplay; }
		GardenSystem& Garden() { return m_Garden; }
		uint64_t Tick() const { return m_Clock.Tick; }
		const CharacterController* Character(const std::string& name) const;
		void SetActorSource(IActorStateSource* source) { m_External = source; }   // replaces the built-in walkers (an Actor system)

		// ISaveHost
		const EntityWorld& Entities() const override { return m_Entities; }
		WorldParams World() const override { return m_World; }
		bool BuildBaseline(const WorldParams& world, EntityWorld& out, std::string& error) const override;
		ClockState Clock() const override { return m_Clock; }
		WeatherState Weather() const override { return m_Weather; }
		const GameplayWorld* Gameplay() const override { return &m_Gameplay; }
		std::vector<ActorState> Actors() const override;
		std::string Validate(const LoadedGame& game) override;
		void Commit(LoadedGame&& game) override;
		// ISimulation
		void Step() override;
		bool Command(const std::string& line, std::string& message) override;

	private:
		struct Walker
		{
			CharacterController Controller;
			CharacterInput Input;
		};
		bool Spawn(const std::string& entity, const glm::vec3* at, float yawDegrees, std::string& message);
		void BindRules();

		SessionConfig m_Config;
		WorldParams m_World;
		EntityWorld m_Entities;
		GameplayWorld m_Gameplay;
		GardenSystem m_Garden;
		ClockState m_Clock;
		WeatherState m_Weather;
		CharacterWorld m_Ground;   // flat
		std::map<std::string, Walker> m_Walkers;
		IActorStateSource* m_External = nullptr;
	};

	// The trees and rocks a seed scatters in the built-in baseline (same naming as the editor: groups "Trees" and "Rocks").
	void ScatterWorld(const WorldParams& world, EntityWorld& out);
}
