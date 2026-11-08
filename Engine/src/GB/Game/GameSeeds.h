#pragma once
#include "GB/Core/Seed.h"
#include <cstdint>
#include <string_view>

namespace GB
{
	struct KnowledgeRules;
	struct TribeConfig;
	struct CombatRules;
	struct DiplomacyRules;
	class ClanWorld;
	class EcologySystem;

	// T0: the seeds of one game. The master seed is the game's seed, the one that already exists: the world's (`generate <biome> <seed>`,
	// GBSim --seed, SaveGame::World.Seed), so a save keeps it and loading a game (which mounts the same world) gives every system the same seeds.
	// Each system takes its seed from the master by the name of its stream; the "seed" a content file gave each set of rules is gone from the
	// data (it only added the same fixed number to every game) and is overwritten here when a game is mounted (the loaders still read it, for the
	// rules used on their own: tests, tools).
	//
	// The streams of the world itself keep the master as it is (the legacy streams): the terrain and its water (WorldGenerator, WaterPlanner,
	// grass, trees and rocks), the wild resources (PlanWorldNature), the cultures (ValueRules::Seed, ClanWorld::SetCultureSeed) and the props of
	// a session (GameSession). Deriving them would change every world ever made with a number (the renders, the golden images, the long runs).
	namespace SeedStream
	{
		inline constexpr std::string_view Knowledge = "knowledge";   // KnowledgeRules::Seed: discoveries, the waste of a failed try (ClanKnowledge::Roll)
		inline constexpr std::string_view Society = "society";       // SocietyRules::Seed: ages, sex, genes, traits, births, deaths of old age (Society::Roll)
		inline constexpr std::string_view Social = "social";         // SocialRules::Seed: beliefs at the start, the sacred place, omens (SocialRoll)
		inline constexpr std::string_view Explore = "explore";       // ExplorationRules::Seed: the direction each member explores (Tribe)
		inline constexpr std::string_view Diplomacy = "diplomacy";   // DiplomacyRules::Seed: marriages between clans (ClanWorld)
		inline constexpr std::string_view Combat = "combat";         // CombatRules::Seed: hits, damage, wounds, the morale of the defender (CombatRoll)
		inline constexpr std::string_view Ecology = "ecology";       // EcologySystem::Seed: where the animals wander
		inline constexpr std::string_view Weather = "weather";       // Weather::Reseed: rain, snow, dust, sand, the tornado and the lightning
	}

	// The master seed of a game whose world has this seed (the identity: the world's seed is the game's).
	constexpr uint64_t MasterSeedOf(uint32_t worldSeed) { return worldSeed; }
	// The seed a system takes from the master for its stream.
	constexpr uint32_t StreamSeed(uint64_t master, std::string_view stream) { return DeriveSeed32(master, stream); }

	// What a game mounts with seeds (every pointer may be null). Idempotent: mounting again with the same master gives the same seeds, so it can
	// be called after every world build and after every content (re)load, which puts back the seeds a section read from the data.
	struct GameSeedTargets
	{
		KnowledgeRules* Knowledge = nullptr;   // TechTree::Rules
		TribeConfig* Tribe = nullptr;          // its Society, Social and Explore (the first tribe; the config new clans copy)
		ClanWorld* Clans = nullptr;            // its diplomacy and the config of every clan's tribe
		CombatRules* Combat = nullptr;
		EcologySystem* Ecology = nullptr;
	};
	void ApplyGameSeeds(uint64_t master, const GameSeedTargets& to);
	// The tribe's part alone: Society, Social and Explore.
	void ApplyTribeSeeds(uint64_t master, TribeConfig& config);
}
