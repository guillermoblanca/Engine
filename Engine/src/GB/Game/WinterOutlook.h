#pragma once

namespace GB
{
	struct SeasonClock;
	class Tribe;

	// T219 (U3 of documentation/01_diseno/MODO_DIOS_UX.md 3.4): the winter's outlook of a clan, the one definition the god's list, the clan's sheet,
	// the alerts (WorldAlerts.h) and GBSim read. It only reads (the clock and the food in hand); nothing here is kept or decides anything.
	//   UntilSpring = the days until the winter begins plus the winter's own days (SeasonSeconds / DaySeconds); in the winter, the days left of it
	//   Short       = max(0, UntilSpring - FoodDays): 0 means the winter is covered
	// FoodDays is Tribe::FoodDays (the store and the bags over the living of today): no births nor deaths are guessed. Which food counts as a
	// reserve (all of it, or only what keeps) is open (MODO_DIOS_UX 5, doubt 7: T213 fixes it); today it is all of it.
	// Examples with 14-day seasons: 24 days of food 6 days before the winter: 20 until the spring, covered; 6 at 6: 14 short; 2 at 6: 18 short.
	struct WinterOutlook
	{
		float ToWinter = 0.0f;      // days until the winter begins (0 in it)
		float UntilSpring = 0.0f;   // days until the spring after it
		float FoodDays = 0.0f;      // the food in hand, in days
		float Short = 0.0f;         // days of food missing to reach the spring (0: covered)
		float SeasonDays = 14.0f;   // the days of a season (rounded, as the god's bar counts them)
		bool In = false;            // the winter is now
		bool Covered() const { return Short <= 0.0f; }
	};

	// From the clock (now in world seconds, a day of `daySeconds`) and the days of food in hand.
	WinterOutlook ForecastWinter(const SeasonClock& seasons, double now, float daySeconds, float foodDays);
	// A clan's: its clock (Tribe::WorldSeconds, TribeConfig::Seasons, NeedsConfig::DaySeconds) and Tribe::FoodDays.
	WinterOutlook ForecastWinter(const Tribe& tribe);
}
