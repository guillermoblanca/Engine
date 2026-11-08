// T0: the master seed of a game (GB/Core/Seed.h, GB/Game/GameSeeds.h). The derivation is pinned to values computed apart from this code
// (FNV-1a 64 and SplitMix64 by hand); names give different seeds; mounting a game twice with the same master gives the same game, and
// another master changes the terrain, the simulation (combat, society, discoveries) and the effects (weather, lightning). CPU only.
#include "GB/Core/Seed.h"
#include "GB/Game/Clans.h"
#include "GB/Effects/Weather.h"
#include "GB/Game/GameSeeds.h"
#include "GB/Game/Society.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Techs.h"
#include "GB/Renderer/Camera.h"
#include "GB/World/Terrain.h"
#include "GB/World/WorldGen.h"
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	// The reference values (a Python script beside the work order; the first two are the published ones of SplitMix64 and FNV-1a).
	static_assert(GB::SplitMix64(0) == 0xE220A8397B1DCDAFull, "SplitMix64 of 0");
	static_assert(GB::Fnv1a64("") == 0xCBF29CE484222325ull && GB::Fnv1a64("a") == 0xAF63DC4C8601EC8Cull, "FNV-1a 64");
	static_assert(GB::DeriveSeed(5, "combat") == 0x9C93A469077B4E45ull, "DeriveSeed(5, combat)");

	// One game mounted from a master seed: the world and every seeded system, then a few results of each.
	struct Game
	{
		std::vector<float> Heights;      // the terrain
		std::vector<double> Blows;       // combat rolls
		std::vector<float> Ages;         // the founders' ages (society)
		uint32_t Knowledge = 0, Explore = 0, Social = 0, Diplomacy = 0, Ecology = 0;
		glm::vec3 Strike{ 0.0f };        // the weather's first lightning strike
		std::vector<glm::vec3> Rain;     // the first raindrops

		bool operator==(const Game& o) const
		{
			return Heights == o.Heights && Blows == o.Blows && Ages == o.Ages && Knowledge == o.Knowledge && Explore == o.Explore && Social == o.Social
				&& Diplomacy == o.Diplomacy && Ecology == o.Ecology && Strike == o.Strike && Rain == o.Rain;
		}
	};

	Game Mount(uint32_t worldSeed)
	{
		Game g;
		GB::WorldDesc desc;
		desc.Type = GB::WorldType::Temperate;
		desc.Seed = worldSeed;
		desc.Size = 192.0f;
		desc.Resolution = 129;
		desc.TreeBudget = 0;
		desc.RockBudget = 0;
		const GB::GeneratedWorld world = GB::WorldGenerator::GenerateData(desc);
		for (int i = 0; i < 16; ++i)
			g.Heights.push_back(world.TerrainRef->GetHeight(-80.0f + 10.0f * static_cast<float>(i), 30.0f - 4.0f * static_cast<float>(i)));

		// the systems with their rules' fixed seeds, as the content leaves them, and then the game's
		const uint64_t master = GB::MasterSeedOf(desc.Seed);
		GB::TechTree techs = GB::DefaultTechs();
		GB::TribeConfig tribe;
		GB::CombatRules combat;
		GB::EcologySystem ecology;
		GB::ClanWorld clans;
		clans.Add("Clan 2");   // a clan with a tribe of its own: the mount seeds its config too
		GB::ApplyGameSeeds(master, { &techs.Rules, &tribe, &clans, &combat, &ecology });
		for (uint64_t n = 0; n < 8; ++n) g.Blows.push_back(GB::CombatRoll(combat.Seed, "Ana", "Wolf", n));
		GB::Society people;
		for (const char* name : { "Ana", "Biel", "Cora", "Dan" }) g.Ages.push_back(people.Join(tribe.Society, name).Age);
		g.Knowledge = techs.Rules.Seed;   // (the rolls of the discoveries are ClanKnowledge's own, private: their seed is what a game gives)
		g.Explore = tribe.Explore.Seed;
		g.Social = tribe.Social.Seed;
		g.Diplomacy = clans.Rules.Seed;
		Check(clans.All().front()->Folk && clans.All().front()->Folk->Config.Society.Seed == tribe.Society.Seed && g.Diplomacy == GB::StreamSeed(master, GB::SeedStream::Diplomacy),
			"every clan's tribe and the diplomacy take the game's seeds");
		g.Ecology = ecology.Seed;

		GB::Weather weather(1);   // built with a fixed seed, as the editor's at start; the game reseeds it
		weather.Reseed(GB::StreamSeed(master, GB::SeedStream::Weather));
		weather.GetLightning().Strike();
		g.Strike = weather.GetLightning().GetStrikePosition();
		GB::Camera cam;
		cam.Position = { 0.0f, 2.0f, 0.0f };
		weather.SetPreset(GB::WeatherPreset::Rain);
		weather.Snap();
		for (int i = 0; i < 6; ++i) weather.Update(1.0f / 60.0f, cam);
		for (const GB::Particle& p : weather.GetRainSystem().GetParticles())
			if (g.Rain.size() < 8) g.Rain.push_back(p.Position);
		return g;
	}
}

int main()
{
	// --- the derivation: pinned values (also as static_asserts above), stable sub-streams
	Check(GB::DeriveSeed(5, "knowledge") == 0xA29C3A014C6E08D9ull, "DeriveSeed(5, knowledge)");
	Check(GB::DeriveSeed(7, "combat") == 0x2CC04F672C02B4BEull, "DeriveSeed(7, combat)");
	Check(GB::DeriveSeed(0, "weather") == 0xF3843214CC6A3A49ull, "DeriveSeed(0, weather)");
	Check(GB::DeriveSeed(5, "society", 3) == 0xFEDAC7B137509637ull, "DeriveSeed(5, society, 3)");
	Check(GB::DeriveSeed32(5, "combat") == 0x9BE8EA2Cu && GB::DeriveSeed32(7, "combat") == 0x00C2FBD9u && GB::DeriveSeed32(5, "society", 3) == 0xC98A5186u, "DeriveSeed32 folds the halves");
	Check(GB::StreamSeed(5, GB::SeedStream::Combat) == GB::DeriveSeed32(5, "combat"), "a stream's seed is DeriveSeed32 of its name");
	Check(GB::MasterSeedOf(5) == 5u, "the master seed is the world's seed");

	// --- different names, different seeds (and every stream of a game has its own)
	{
		const std::string_view streams[] = { GB::SeedStream::Knowledge, GB::SeedStream::Society, GB::SeedStream::Social, GB::SeedStream::Explore,
			GB::SeedStream::Diplomacy, GB::SeedStream::Combat, GB::SeedStream::Ecology, GB::SeedStream::Weather };
		for (const uint64_t master : { 0ull, 1ull, 5ull, 7ull, 0xFFFFFFFFull })
		{
			std::set<uint64_t> seen;
			for (const std::string_view s : streams) seen.insert(GB::DeriveSeed(master, s));
			Check(seen.size() == std::size(streams), "every stream of a game has its own seed");
		}
		Check(GB::DeriveSeed(5, "combat") != GB::DeriveSeed(5, "Combat") && GB::DeriveSeed(5, "") != GB::DeriveSeed(6, ""), "the name and the parent both count");
		std::set<uint32_t> clans;
		for (uint64_t i = 0; i < 64; ++i) clans.insert(GB::DeriveSeed32(5, "society", i));
		Check(clans.size() == 64 && GB::DeriveSeed(5, "society", 0) != GB::DeriveSeed(5, "society"), "the sub-streams (one per clan, tile, person) differ");
	}

	// --- mounting the seeds is idempotent: again after a content reload gives the same
	{
		GB::TribeConfig a, b;
		GB::ApplyTribeSeeds(5, a);
		GB::ApplyTribeSeeds(5, b);
		GB::ApplyTribeSeeds(5, b);
		Check(a.Society.Seed == b.Society.Seed && a.Social.Seed == b.Social.Seed && a.Explore.Seed == b.Explore.Seed, "mounting twice gives the same seeds");
		Check(a.Society.Seed != GB::DefaultSocietyRules().Seed && a.Explore.Seed != 0u, "the game replaces the rules' fixed seeds");
	}

	// --- Weather(s) and Reseed(s) give the same effects
	{
		GB::Weather built(42), reseeded(3);
		reseeded.Reseed(42);
		built.GetLightning().Strike();
		reseeded.GetLightning().Strike();
		Check(built.GetSeed() == 42u && reseeded.GetSeed() == 42u && built.GetLightning().GetStrikePosition() == reseeded.GetLightning().GetStrikePosition(),
			"Reseed(s) gives what Weather(s) gives");
	}

	// --- a game: the same master twice is the same game; another changes the terrain, the simulation and the effects
	{
		const Game five = Mount(5), again = Mount(5), seven = Mount(7);
		Check(five == again, "the same master seed mounts the same game");
		Check(five.Heights != seven.Heights, "another master: another terrain");
		Check(five.Blows != seven.Blows, "another master: other blows (combat)");
		Check(five.Ages != seven.Ages, "another master: other founders (society)");
		Check(five.Knowledge != seven.Knowledge && five.Knowledge != GB::DefaultTechs().Rules.Seed, "another master: other discoveries (knowledge)");
		Check(five.Explore != seven.Explore && five.Social != seven.Social && five.Diplomacy != seven.Diplomacy && five.Ecology != seven.Ecology,
			"another master: other exploring, beliefs, marriages and wanders");
		Check(five.Strike != seven.Strike, "another master: other lightning (weather)");
		Check(!five.Rain.empty() && five.Rain != seven.Rain, "another master: other rain (particles)");
	}

	if (failures == 0) std::printf("seed: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
