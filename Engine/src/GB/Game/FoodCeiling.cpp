#include "GB/Game/FoodCeiling.h"
#include "GB/Game/Tribe.h"

namespace GB
{
	float FoodCeiling::Days(const Tribe& tribe)
	{
		const float days = tribe.Config.Work.Jobs.FoodCeilingDays;
		if (days <= 0.0f) return 0.0f;
		return days + WinterReserveDays(tribe.Config.Econ, tribe.Config.Seasons, tribe.WorldSeconds());
	}

	bool FoodCeiling::Reached(const Tribe& tribe)
	{
		const float days = Days(tribe);
		return days > 0.0f && tribe.FoodDays() >= days;
	}
}
