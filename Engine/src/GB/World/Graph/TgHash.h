#pragma once
#include "GB/World/Graph/TerrainGraph.h"
#include <cstdint>

namespace GB
{
	// Bit-exact 64-bit hashes of what a terrain graph produces (FNV-1a over the raw bytes; not quantized like GB::Fingerprint): two values hash
	// alike only if every float has the same bit pattern, barring a 64-bit collision. The per-node cache (TerrainGraphCache.h) keys on them and
	// the tests compare whole evaluations with them ("the same world, bit for bit").
	inline constexpr uint64_t kTgHashSeed = 1469598103934665603ull;
	uint64_t TgHashBytes(const void* data, size_t size, uint64_t hash = kTgHashSeed);
	uint64_t TgHashString(const std::string& text, uint64_t hash = kTgHashSeed);
	uint64_t TgHashField(const TgField& field, uint64_t hash = kTgHashSeed);
	uint64_t TgHashValue(const TgValue& value, uint64_t hash = kTgHashSeed);   // only the member of its Kind
	uint64_t TgHashResult(const TgResult& result);   // every output of every node (TgResult::Nodes) and what the output nodes published
}
