#include "GB/Game/GameSeeds.h"
#include "GB/Game/Clans.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Techs.h"

namespace GB
{
	void ApplyTribeSeeds(uint64_t master, TribeConfig& config)
	{
		config.Society.Seed = StreamSeed(master, SeedStream::Society);
		config.Social.Seed = StreamSeed(master, SeedStream::Social);
		config.Explore.Seed = StreamSeed(master, SeedStream::Explore);
	}

	void ApplyGameSeeds(uint64_t master, const GameSeedTargets& to)
	{
		if (to.Knowledge) to.Knowledge->Seed = StreamSeed(master, SeedStream::Knowledge);
		if (to.Tribe) ApplyTribeSeeds(master, *to.Tribe);
		if (to.Clans)
		{
			to.Clans->Rules.Seed = StreamSeed(master, SeedStream::Diplomacy);
			for (const auto& clan : to.Clans->All())
				if (clan->Folk) ApplyTribeSeeds(master, clan->Folk->Config);
		}
		if (to.Combat) to.Combat->Seed = StreamSeed(master, SeedStream::Combat);
		if (to.Ecology) to.Ecology->Seed = StreamSeed(master, SeedStream::Ecology);
	}
}
