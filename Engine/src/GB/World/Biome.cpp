#include "GB/World/Biome.h"

namespace GB
{
	const char* BiomeName(Biome b)
	{
		switch (b)
		{
		case Biome::Water: return "Water";
		case Biome::Shore: return "Shore";
		case Biome::Meadow: return "Meadow";
		case Biome::Forest: return "Forest";
		case Biome::Rock: return "Rock";
		case Biome::Snow: return "Snow";
		case Biome::Dunes: return "Dunes";
		}
		return "Water";
	}
}
