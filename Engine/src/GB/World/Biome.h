#pragma once

// Pulled out of WorldGen.h so a node of the terrain graph (GB/World/Graph/*) can speak the same biome the rest of the engine does without pulling in
// WorldGen.h's heavier includes (Terrain.h, Rocks.h, Trees.h): this header has no dependency beyond the language itself.
namespace GB
{
	enum class Biome { Water, Shore, Meadow, Forest, Rock, Snow, Dunes };
	const char* BiomeName(Biome b);   // "Water", "Shore", "Meadow", "Forest", "Rock", "Snow", "Dunes" (the names the data uses)
}
