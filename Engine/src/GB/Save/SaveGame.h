#pragma once
#include "GB/Game/Entity.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/World/WorldGenVersion.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Saved games: a versioned JSON file with the header of the session (world seed and parameters, simulation clock, hour, weather), the
// entities that changed with respect to the world the seed generates, the live state of the ability systems, the actors and any
// extra section a system registers. Pure CPU (no GL, no window). Format and policy: docs/GUARDADO.md.
namespace GB::Save
{
	// The format written today. Every change of the layout bumps it and adds a migration (see BuiltinMigrations).
	// 2: the world records the version of its generator (`generator`) and the density of its wild resource nodes (`wild`).
	constexpr int kFormatVersion = 2;
	constexpr const char* kEngineVersion = "0.1.0";
	constexpr const char* kFileExtension = ".gbsave";

	struct Result
	{
		bool Ok = true;
		std::string Error;
		std::vector<std::string> Warnings;   // what loaded but not entirely (a baseline that drifted, something that was not saved)
		explicit operator bool() const { return Ok; }
		static Result Fail(std::string error) { Result r; r.Ok = false; r.Error = std::move(error); return r; }
	};

	// What the seed needs to generate the world again (mirrors WorldDesc without depending on the renderer). Generated = false: no
	// generated world, the entities are all there is.
	struct WorldParams
	{
		bool Generated = false;
		std::string Type = "temperate";   // temperate | alpine | desert
		uint32_t Seed = 1;
		// The version of the generator that made the world (WorldGenVersion.h). A save from before format 2 has none: it is migrated as 1, the
		// generator of those days, which still makes the very same world from the seed.
		uint32_t GeneratorVersion = kWorldGeneratorLatest;
		float Size = 360.0f;
		uint32_t Resolution = 641;
		bool River = true, Lake = true;
		float ForestAmount = 0.55f;
		int TreeBudget = 420, RockBudget = 70;
		// H16: tiles per side of the world (1: the single map of Size metres). With more the world is Tiles x Tiles maps of the same generator made as
		// one (seamless). Written only when above 1, so a save from before H16 is a single tile, as it was.
		int Tiles = 1;
		// Density of the resource nodes the data spreads over the world (PlanWorldResources, the editor's `resources world`), 0 = none. They come
		// from the seed too, so they are part of the baseline and the save keeps only the ones that changed. Written only when not 0.
		float WildResources = 0.0f;
		bool operator==(const WorldParams& o) const
		{
			return Generated == o.Generated && Type == o.Type && Seed == o.Seed && GeneratorVersion == o.GeneratorVersion && Size == o.Size && Resolution == o.Resolution &&
				River == o.River && Lake == o.Lake && ForestAmount == o.ForestAmount && TreeBudget == o.TreeBudget && RockBudget == o.RockBudget && WildResources == o.WildResources && Tiles == o.Tiles;
		}
		bool operator!=(const WorldParams& o) const { return !(*this == o); }
	};

	// The simulation runs in fixed ticks of `Dt` seconds; `Tick` counts them since the world began, so the time is exact.
	struct ClockState
	{
		uint64_t Tick = 0;
		double Dt = 1.0 / 60.0;
		float Hours = 12.0f;             // time of day, 0..24
		float DayLengthSeconds = 1200.0f;   // real seconds one 24-hour day lasts (0 = the hour is frozen)
		bool TimeOfDay = false;          // the sky follows the hour
		uint32_t Day = 0;                // days gone by
		double Seconds() const { return static_cast<double>(Tick) * Dt; }
	};

	struct WeatherState
	{
		std::string Preset = "clear";
	};

	struct ItemStack
	{
		std::string Item;
		int Count = 1;
		// Seconds the stack has been ageing, for goods that spoil (E6, H18: ItemSlot::Age; in use since the economy, see docs/ECONOMIA.md). Written only
		// when not 0 and read back when there, so a save without it (or of things that do not spoil) is a fresh stack.
		float Age = 0.0f;
	};

	// One body of a ragdoll (or any simulated body) at the moment of saving.
	struct BodyState
	{
		std::string Name;                // the bone / body
		glm::vec3 Position{ 0.0f };
		glm::vec4 Rotation{ 0.0f, 0.0f, 0.0f, 1.0f };   // quaternion x y z w
		glm::vec3 LinearVelocity{ 0.0f };
		glm::vec3 AngularVelocity{ 0.0f };
	};

	// A character of the game (the player, an NPC): where it is and what it is doing. Its attributes, abilities and cooldowns are the ones of the
	// ability system of the same name (see GameplayRuntime and the entity's gameplay component).
	struct ActorState
	{
		std::string Name;
		std::string Kind;                // what it was made from ("fox", "cesium" or a character file): with it a game loaded into an empty world makes the actor again; "" = unknown
		// Reserved (docs/GUARDADO.md): what this agent knows (recipes, places, techniques) as names; written only when not empty, nothing fills it yet.
		std::vector<std::string> Knowledge;
		glm::vec3 Position{ 0.0f };
		float Yaw = 0.0f;                // radians, 0 = +Z
		std::string Set;                 // animation set (locomotion) in use
		std::vector<std::string> Props;  // what it carries / has in hand
		std::vector<ItemStack> Inventory;
		glm::vec2 Move{ 0.0f };          // the input it holds (an NPC or the player mid-walk)
		bool Run = false;
		bool Ragdoll = false;            // the body is simulated
		std::vector<BodyState> Bodies;   // the ragdoll's bodies when Ragdoll
	};

	// The part of an ability system that its scene component does not keep (see AbilityRuntimeState), by entity name.
	struct GameplayRuntime
	{
		std::string Entity;
		AbilityRuntimeState State;
	};

	// Something a system saves on its own (inventories, terrain edits, clans...): opaque JSON text under a name and its own version.
	struct SectionBlob
	{
		std::string Name;
		int Version = 1;
		std::string Json = "null";
	};

	struct DiffEntity
	{
		EntityId Id = NullEntity;
		std::string Json;   // the entity as SceneIO writes it (compact JSON text)
	};

	// What changed in the entities with respect to the world the seed generates. Ids are the ones of that world.
	struct EntityDiff
	{
		std::vector<EntityId> Removed;
		std::vector<DiffEntity> Changed;   // present in both, different
		std::vector<DiffEntity> Added;     // not in the generated world
		EntityId NextId = 1;
		size_t Size() const { return Removed.size() + Changed.size() + Added.size(); }
	};

	// Identifies the generated world the diff is relative to, to notice when the generator no longer produces it.
	struct BaselineId
	{
		uint32_t Entities = 0;
		std::string Hash;   // 16 hex digits
	};

	struct SaveGame
	{
		int Format = kFormatVersion;
		std::string Engine = kEngineVersion;
		WorldParams World;
		BaselineId Baseline;
		ClockState Clock;
		WeatherState Weather;
		EntityDiff Entities;
		std::vector<GameplayRuntime> Gameplay;
		std::vector<ActorState> Actors;
		std::vector<SectionBlob> Sections;
	};

	// ---- text form. SaveGameToString is deterministic: the same game writes the same bytes (no dates inside; the file's own date is the date).
	std::string SaveGameToString(const SaveGame& game);
	// Reads any version this engine knows (migrating it), checks the checksum and the values. On failure `out` is left as it was.
	Result ParseSaveGame(const std::string& text, SaveGame& out);

	// ---- versions
	// One step of the chain: turns the JSON text of a file at version `From` into one of version From + 1 (and sets its "format").
	struct Migration
	{
		int From = 0;
		std::string Note;   // what changed, for the log and the docs
		std::function<bool(std::string& json, std::string& error)> Apply;
	};
	// The chain from the oldest format to kFormatVersion: step i has From == i. v0 is a bare scene file (the SceneIO JSON, before saved games).
	const std::vector<Migration>& BuiltinMigrations();
	// The version a JSON text declares: its "format", 0 for a bare scene (no "format" but "entities"), -1 when it is neither.
	int DetectFormat(const std::string& json);
	// Runs the steps from `version` up to `target` over `json`. Fails when a step is missing or fails (json is then unspecified).
	Result Migrate(std::string& json, int version, int target, const std::vector<Migration>& chain, std::vector<std::string>* log = nullptr);

	// 64-bit FNV-1a as 16 hex digits: the checksum of the file and the identity of the generated world.
	std::string HashText(const std::string& text);
}
