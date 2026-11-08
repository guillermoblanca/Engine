#pragma once
#include <cstdint>
#include <string_view>

namespace GB
{
	// The seeds of a game (T0, documentation/01_diseno/AGUA.md §3): one master seed per game, and every system takes its own seed from it by
	// NAME (DeriveSeed(master, "combat")), never a fixed number of its own. The same master replays the whole game; another master varies
	// everything. The names of the streams a game uses, and who reads each one, are in GB/Game/GameSeeds.h.
	//
	// Stable on every platform and compiler: FNV-1a 64 of the name (bytes, not std::hash) mixed with SplitMix64 of the parent. Changing any
	// constant here changes every game ever played with a seed: tests/seed_test.cpp pins the values.

	// SplitMix64's step and finaliser (Steele, Lea, Flood 2014): a bijection of the 64-bit numbers that spreads every bit.
	constexpr uint64_t SplitMix64(uint64_t x)
	{
		x += 0x9E3779B97F4A7C15ull;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		return x ^ (x >> 31);
	}

	// FNV-1a 64 of the bytes of `text`.
	constexpr uint64_t Fnv1a64(std::string_view text)
	{
		uint64_t h = 14695981039346656037ull;
		for (const char c : text)
		{
			h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
			h *= 1099511628211ull;
		}
		return h;
	}

	// The seed of the stream `name` under `parent` (the game's master seed, or a seed derived from it).
	constexpr uint64_t DeriveSeed(uint64_t parent, std::string_view name)
	{
		return SplitMix64(SplitMix64(parent) ^ Fnv1a64(name));
	}

	// A sub-stream: one per clan, tile or person (`index`) of the stream `name`.
	constexpr uint64_t DeriveSeed(uint64_t parent, std::string_view name, uint64_t index)
	{
		return SplitMix64(DeriveSeed(parent, name) ^ SplitMix64(index));
	}

	// The same folded to 32 bits, for the systems whose seed is a uint32_t (the high half xor the low half).
	constexpr uint32_t Fold32(uint64_t seed) { return static_cast<uint32_t>(seed ^ (seed >> 32)); }
	constexpr uint32_t DeriveSeed32(uint64_t parent, std::string_view name) { return Fold32(DeriveSeed(parent, name)); }
	constexpr uint32_t DeriveSeed32(uint64_t parent, std::string_view name, uint64_t index) { return Fold32(DeriveSeed(parent, name, index)); }
}
