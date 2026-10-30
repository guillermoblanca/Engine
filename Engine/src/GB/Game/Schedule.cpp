#include "GB/Game/Schedule.h"
#include <cmath>

namespace GB
{
	const char* DayPartName(DayPart part) { return part == DayPart::Night ? "night" : part == DayPart::Dusk ? "dusk" : "day"; }

	float ShiftedHour(float hour, float nightShift)
	{
		if (nightShift == 0.0f) return hour;
		hour = std::fmod(hour - nightShift, 24.0f);
		if (hour < 0.0f) hour += 24.0f;
		return hour;
	}

	DayPart PartOfDay(const DayShape& day, float hour, Season season, float nightShift)
	{
		hour = ShiftedHour(hour, nightShift);
		const float shift = season == Season::Winter ? day.DuskShift : season == Season::Autumn ? day.DuskShift * 0.5f : 0.0f;
		const float dusk = day.NightFrom - day.DuskHours - shift, night = day.NightFrom - shift;
		const bool inNight = hour >= night || hour < day.NightTo;
		return inNight ? DayPart::Night : hour >= dusk ? DayPart::Dusk : DayPart::Day;
	}

	bool IsEvening(const DayShape& day, float hour) { return hour >= day.NightFrom - day.DuskHours || hour < day.NightTo; }

	float ScheduleFactor(const DayShape& day, const AgendaFactors& clan, const AgendaFactors* role, float nightShift, const std::string& kind, float hour, Season season)
	{
		const char* part = DayPartName(PartOfDay(day, hour, season, nightShift));
		auto lookup = [&](const AgendaFactors& factors, float& out)
		{
			const auto p = factors.find(part);
			if (p == factors.end()) return false;
			const auto k = p->second.find(kind);
			if (k == p->second.end()) return false;
			out = k->second;
			return true;
		};
		float f = 1.0f;
		if (role && lookup(*role, f)) return f;
		lookup(clan, f);
		return f;
	}
}
