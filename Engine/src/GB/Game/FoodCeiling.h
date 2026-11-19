#pragma once

namespace GB
{
	class Tribe;

	// T359 (N2 of documentation/01_diseno/CIERRE_AGRICULTURA_2026-11-19.md §4.3): the ceiling of the food a clan gathers. Food was the one thing
	// gathered without a top (Tribe::Decide cut what is not food at twice what the clan wants, and the board asked for food up to its survival
	// days, but a forager's trade and hunger kept it picking), so the stores rotted: 44-53 % of the food brought in went off in 12 days with 23-55
	// days of food in hand. With work.jobs.foodCeilingDays > 0, once the clan holds that many days of food (Tribe::FoodDays, which since T360 counts
	// a unit only for what it lasts), plus the winter reserve in the season before the lean one (WinterReserveDays), it gathers no more food: the
	// board posts no food gathering and its members take no food gather option, unless one is hungry with no food at hand. Pure: it reads the
	// tribe; nothing is kept.
	class FoodCeiling
	{
	public:
		// The days of food above which the clan gathers no more (0: no ceiling).
		static float Days(const Tribe& tribe);
		// The clan holds them.
		static bool Reached(const Tribe& tribe);
	};
}
