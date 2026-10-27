#pragma once
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Save/SaveGame.h"

namespace GB::Save
{
	// What a game session loaded from a save, validated and ready to put in place.
	struct LoadedGame
	{
		WorldParams World;
		EntityWorld Entities;
		ClockState Clock;
		WeatherState Weather;
		std::vector<GameplayRuntime> Gameplay;
		std::vector<ActorState> Actors;
		std::vector<SectionBlob> Sections;
	};

	// The small interface characters implement to be saved: the PlayerMode / Actor of the editor and the NPCs hook in here.
	class IActorStateSource
	{
	public:
		virtual ~IActorStateSource() = default;
		virtual std::vector<ActorState> CaptureActors() const = 0;
		// Returns "" when the actors can be put back, else why not. Called twice: once to check (Check = true, change nothing), then to apply.
		virtual std::string RestoreActors(const std::vector<ActorState>& actors, bool check) = 0;
	};

	// The application side of saving: what to read and how to put a loaded game in place. The editor and the headless GameSession implement it.
	class ISaveHost
	{
	public:
		virtual ~ISaveHost() = default;
		virtual const EntityWorld& Entities() const = 0;
		virtual WorldParams World() const = 0;
		// The entities the seed generates, in a fresh world, with no side effects on the live one. Must give the same result every time.
		virtual bool BuildBaseline(const WorldParams& world, EntityWorld& out, std::string& error) const = 0;
		virtual ClockState Clock() const = 0;
		virtual WeatherState Weather() const { return {}; }
		virtual const GameplayWorld* Gameplay() const { return nullptr; }
		virtual std::vector<ActorState> Actors() const { return {}; }
		virtual std::vector<SectionBlob> Sections() const { return {}; }
		// Last checks before anything changes: "" = fine. The default checks the ability systems against the host's library.
		virtual std::string Validate(const LoadedGame& game);
		// Puts the game in place. Called only after Validate passed; it must not fail.
		virtual void Commit(LoadedGame&& game) = 0;
	};

	struct CaptureResult
	{
		SaveGame Game;
		std::vector<std::string> Warnings;   // what cannot be saved right now (abilities in flight are lost)
	};

	CaptureResult CaptureGame(const ISaveHost& host);

	struct LoadOptions
	{
		bool AllowBaselineDrift = false;   // apply the diff even if the seed no longer generates the same world (with a warning)
	};
	// Builds the game of the save (generated world + diff), validates it and, only then, commits it. A failure changes nothing.
	Result ApplyGame(ISaveHost& host, const SaveGame& game, const LoadOptions& options = {});

	// ---- the diff against the generated world
	BaselineId BaselineIdOf(const EntityWorld& baseline);
	EntityDiff DiffEntities(const EntityWorld& baseline, const EntityWorld& current);
	// Rebuilds `current` from the baseline and the diff.
	Result PatchEntities(const EntityWorld& baseline, const EntityDiff& diff, EntityWorld& out);

	// ---- ability systems
	std::vector<GameplayRuntime> CaptureGameplay(const GameplayWorld& gameplay);
	// Loads the ability systems of `entities` and puts the clocks back. False and `error` when an effect is unknown.
	bool RestoreGameplay(GameplayWorld& gameplay, const EntityWorld& entities, const std::vector<GameplayRuntime>& runtime, std::string* error = nullptr);

	// ---- fingerprints of the state, to compare runs. Numbers are quantized (Qa::Fingerprint), so tiny float differences do not count.
	struct Digest
	{
		std::map<std::string, uint64_t> Parts;   // "entities", "gameplay", "actors", "clock", "weather", "sections"
		uint64_t Total = 0;
		bool operator==(const Digest& o) const { return Total == o.Total && Parts == o.Parts; }
		bool operator!=(const Digest& o) const { return !(*this == o); }
	};
	Digest ComputeDigest(const ISaveHost& host);
	std::vector<std::string> DifferingParts(const Digest& a, const Digest& b);
	std::string DigestToString(uint64_t value);   // 16 hex digits
}
