// T14: the weather of each day comes from the game's seed, within the climate of each season (ClimateRules, the "climate" section of
// assets/data/world.json). Two seeds give two calendars of rain, the same seed the same one; over many seeds the share of wet days of each
// season is what the data says, and the years differ (a dry year, a wet year). The weather keeps no state: a clock rebuilt from the data and
// the master seed (what loading a game does) gives the same sky. Seed 0 is the fixed calendar of before. CPU only.
#include "GB/Game/Clans.h"
#include "GB/Game/GameSeeds.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Ecology.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	// The weather of every day of `years` years, one letter a day (c clear, l light rain, r rain, s storm, n snow, b blizzard).
	std::string Calendar(const GB::SeasonClock& clock, int years)
	{
		const double day = static_cast<double>(clock.Climate.DaySeconds);
		const int days = static_cast<int>(std::lround(static_cast<double>(clock.SeasonSeconds) * 4.0 / day)) * years;
		std::string out;
		for (int d = 0; d < days; ++d)
		{
			const std::string w = clock.WeatherAt((static_cast<double>(d) + 0.5) * day);
			out += w == "clear" ? 'c' : w == "lightrain" ? 'l' : w == "rain" ? 'r' : w == "storm" ? 's' : w == "snow" ? 'n' : w == "blizzard" ? 'b' : '?';
		}
		return out;
	}

	GB::SeasonClock Seeded(uint64_t master)
	{
		GB::TribeConfig config;   // the clock a game's tribe has, seeded as a mounted game seeds it
		GB::ApplyTribeSeeds(master, config);
		return config.Seasons;
	}
}

int GBTest_climate(int, char**)
{
	using namespace GB;
	const ClimateRules data = DefaultClimate();
	const int daysPerSeason = static_cast<int>(std::lround(static_cast<double>(SeasonClock().SeasonSeconds) / static_cast<double>(data.DaySeconds)));

	// --- the data: the section loads, round-trips, and a mistake is refused
	{
		Check(data.Seasons[static_cast<int>(Season::Winter)].Snow > 0.5f && data.Seasons[static_cast<int>(Season::Summer)].Wet > 0.0f, "the shipped climate has rain in summer and snow in winter");
		Check(data.YearSpread > 0.0f, "the shipped climate has years wetter and drier than the mean");
		ClimateRules back;
		Check(LoadClimate("{ \"climate\": " + ClimateToJson(data) + " }", back).empty() && back == data, "the climate section round-trips");
		ClimateRules keep = data;
		Check(!LoadClimate(R"({ "climate": { "seasons": { "summer": { "wet": 1.5 } } } })", keep).empty() && keep == data, "a chance above 1 is refused and nothing changes");
		Check(!LoadClimate(R"({ "climate": { "seasons": { "monsoon": { "wet": 0.5 } } } })", keep).empty() && keep == data, "an unknown season is refused");
		Check(LoadClimate(R"({ "items": [] })", keep).empty() && keep == data, "a file without the section changes nothing");
		ClimateRules part = data;
		Check(LoadClimate(R"({ "climate": { "seasons": { "spring": { "wet": 0.5 } } } })", part).empty() && part.Seasons[0].Wet == 0.5f && part.Seasons[0].Persist == data.Seasons[0].Persist
			&& part.Seasons[1] == data.Seasons[1], "a season given in part keeps the rest of what it had");
	}

	// --- seed 0: the fixed calendar of before (the unseeded clock of tests and tools)
	{
		const SeasonClock fixed;
		Check(fixed.Climate.Seed == 0u, "a clock nobody seeded has seed 0");
		bool same = true;
		for (double t = 0.0; t < static_cast<double>(fixed.SeasonSeconds) * 4.0; t += 300.0)
			same = same && std::string(fixed.WeatherAt(t)) == SeasonClock::Weather(fixed.At(t), fixed.Progress(t));
		Check(same, "seed 0 gives the fixed calendar (SeasonClock::Weather)");
	}

	// --- the same seed, the same sky; another seed, another one
	const SeasonClock five = Seeded(5), seven = Seeded(7), seventySeven = Seeded(77);
	{
		Check(five.Climate.Seed == StreamSeed(5, SeedStream::Climate) && five.Climate.Seed != 0u, "a game's clock takes its seed from the master (stream climate)");
		Check(Calendar(five, 3) == Calendar(Seeded(5), 3), "the same seed gives the same calendar (a clock rebuilt from the data and the master: a loaded game)");
		Check(Calendar(five, 3) != Calendar(seven, 3) && Calendar(seven, 3) != Calendar(seventySeven, 3) && Calendar(five, 3) != Calendar(seventySeven, 3),
			"two seeds give two calendars");
		// the clans of a world share the sky: every tribe of a game is seeded from the same master
		TribeConfig a, b;
		ApplyTribeSeeds(9, a);
		ApplyTribeSeeds(9, b);
		Check(Calendar(a.Seasons, 1) == Calendar(b.Seasons, 1), "every clan of a world has the same weather");
		// ... the clans of a mounted game, and a clan founded later (a split copies the first tribe's config, as SimQA and the editor do)
		Tribe first;
		ClanWorld clans;
		clans.Add("Clan 1", &first);
		clans.Add("Clan 2", nullptr, -1, &first.Config);
		ApplyGameSeeds(9, { nullptr, &first.Config, &clans, nullptr, nullptr });
		const Clan& later = clans.Add("Clan 3", nullptr, -1, &first.Config);
		Check(clans.All().size() == 3 && clans.All()[1]->Folk->Config.Seasons.Climate.Seed == a.Seasons.Climate.Seed && later.Folk->Config.Seasons.Climate.Seed == a.Seasons.Climate.Seed
			&& Calendar(later.Folk->Config.Seasons, 1) == Calendar(a.Seasons, 1), "the clans of a game, and one founded after the mount, share the sky");
		// the weather of a moment does not depend on what was asked before (no state: it is the same asked in any order)
		const std::string forward = Calendar(five, 1);
		std::string backward(forward.size(), '?');
		for (int d = static_cast<int>(forward.size()) - 1; d >= 0; --d)
		{
			const std::string w = five.WeatherAt((static_cast<double>(d) + 0.5) * static_cast<double>(data.DaySeconds));
			backward[static_cast<size_t>(d)] = w == "clear" ? 'c' : w == "lightrain" ? 'l' : w == "rain" ? 'r' : w == "storm" ? 's' : w == "snow" ? 'n' : w == "blizzard" ? 'b' : '?';
		}
		const bool order = backward == forward;
		Check(order, "the weather of a day is the same asked backwards");
		// the whole of a day has one weather
		bool day = true;
		for (int d = 0; d < 56; ++d)
		{
			const double t0 = static_cast<double>(d) * static_cast<double>(data.DaySeconds);
			day = day && std::string(five.WeatherAt(t0 + 1.0)) == five.WeatherAt(t0 + static_cast<double>(data.DaySeconds) - 1.0);
		}
		Check(day, "a day has one weather from its start to its end");
	}

	// --- over many seeds and years, the share of wet days of each season is the data's, and the years differ
	{
		const int seeds = 300, years = 4;
		double wet[4] = {}, rain[4] = {}, storms[4] = {};
		int counted[4] = {};
		double driest = 1.0, wettest = 0.0;   // of the springs and autumns of single years (the seasons that water the fields)
		bool kinds = true;
		for (int s = 1; s <= seeds; ++s)
		{
			const SeasonClock clock = Seeded(static_cast<uint64_t>(s) * 7919u);
			const std::string cal = Calendar(clock, years);
			for (int y = 0; y < years; ++y)
				for (int q = 0; q < 4; ++q)
				{
					int w = 0, r = 0;
					for (int d = 0; d < daysPerSeason; ++d)
					{
						const char c = cal[static_cast<size_t>((y * 4 + q) * daysPerSeason + d)];
						w += c != 'c';
						r += c == 'l' || c == 'r' || c == 's';
						storms[q] += c == 's' || c == 'b';
						if (q == static_cast<int>(Season::Winter) && data.Seasons[3].Snow >= 1.0f && (c == 'l' || c == 'r' || c == 's')) kinds = false;
					}
					wet[q] += w;
					rain[q] += r;
					counted[q] += daysPerSeason;
					if (q == static_cast<int>(Season::Spring) || q == static_cast<int>(Season::Autumn))
					{
						const double share = static_cast<double>(w) / daysPerSeason;
						driest = std::min(driest, share);
						wettest = std::max(wettest, share);
					}
				}
		}
		for (int q = 0; q < 4; ++q)
		{
			const double share = wet[q] / counted[q];
			const double want = data.Seasons[q].Wet;
			const double storm = wet[q] > 0.0 ? storms[q] / wet[q] : 0.0;
			std::fprintf(stderr, "  %s: %.3f of the days wet (data %.3f), %.3f rain on the fields, %.3f of the wet days storms (data %.3f)\n", SeasonClock::Name(static_cast<Season>(q)), share, want,
				rain[q] / counted[q], storm, static_cast<double>(data.Seasons[q].Storm));
			Check(std::fabs(share - want) < 0.03, "the share of wet days of a season, over many seeds, is the data's");
			Check(std::fabs(storm - data.Seasons[q].Storm) < 0.04, "the share of storms among the wet days is the data's");
		}
		Check(kinds, "a winter of all snow brings no rain");
		std::fprintf(stderr, "  springs and autumns of one year: %.2f to %.2f of the days wet\n", driest, wettest);
		Check(driest < data.Seasons[0].Wet * 0.5 && wettest > data.Seasons[2].Wet * 1.5, "some years are dry and some wet");
	}

	// --- streaks: with persistence, wet days come together more than by chance
	{
		int pairs = 0, both = 0, wetDays = 0, days = 0;
		const int q = static_cast<int>(Season::Autumn);
		for (int s = 1; s <= 200; ++s)
		{
			const std::string cal = Calendar(Seeded(static_cast<uint64_t>(s) * 104729u), 2);
			for (int y = 0; y < 2; ++y)
				for (int d = 0; d < daysPerSeason; ++d)
				{
					const size_t i = static_cast<size_t>((y * 4 + q) * daysPerSeason + d);
					const bool w = cal[i] != 'c';
					wetDays += w;
					++days;
					if (d > 0) { const bool prev = cal[i - 1] != 'c'; pairs += prev; both += prev && w; }
				}
		}
		const double p = static_cast<double>(wetDays) / days, repeat = pairs ? static_cast<double>(both) / pairs : 0.0;
		std::fprintf(stderr, "  autumn: a wet day follows a wet day %.2f of the time (%.2f of all days are wet)\n", repeat, p);
		Check(data.Seasons[q].Persist <= 0.0f || repeat > p + 0.1, "rain comes in spells");
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("climate: all checks passed\n");
	return failures ? 1 : 0;
}
