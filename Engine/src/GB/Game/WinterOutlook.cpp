#include "GB/Game/WinterOutlook.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Ecology.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	WinterOutlook ForecastWinter(const SeasonClock& seasons, double now, float daySeconds, float foodDays)
	{
		const float day = std::max(1.0f, daySeconds);
		const float seasonDays = static_cast<float>(std::max(1, static_cast<int>(std::lround(seasons.SeasonSeconds / day))));   // (as the god's bar counts them)
		const Season season = seasons.At(now);
		const float left = (1.0f - std::clamp(seasons.Progress(now), 0.0f, 1.0f)) * seasonDays;
		WinterOutlook w;
		w.SeasonDays = seasonDays;
		w.In = season == Season::Winter;
		w.ToWinter = w.In ? 0.0f : left + static_cast<float>(2 - static_cast<int>(season)) * seasonDays;   // (the whole seasons between this one and the winter)
		w.UntilSpring = w.In ? left : w.ToWinter + seasonDays;
		w.FoodDays = std::max(0.0f, foodDays);
		w.Short = std::max(0.0f, w.UntilSpring - w.FoodDays);
		return w;
	}

	WinterOutlook ForecastWinter(const Tribe& tribe)
	{
		return ForecastWinter(tribe.Config.Seasons, tribe.WorldSeconds(), tribe.Config.Needs.DaySeconds, tribe.FoodDays());
	}
}
