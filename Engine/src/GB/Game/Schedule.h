#pragma once
// The hours of the day (H6): which part of the day it is for someone, and what the agenda makes of an option then. Apart from the Tribe so that the
// whole timetable (work blocks, shifts, school and rest of the children, F5 of docs/PLAN_JUGABILIDAD.md) grows here and is tested alone: it takes the
// shape of the day (DayShape) and the factors by value or pointer, and knows nothing of members, jobs or the scene.
#include "GB/Gameplay/Ecology.h"
#include <map>
#include <string>

namespace GB
{
	using AgendaFactors = std::map<std::string, std::map<std::string, float>>;   // part of the day -> option kind ("gather", "sleep"...) -> factor

	enum class DayPart { Day, Dusk, Night };
	const char* DayPartName(DayPart part);   // "day", "dusk", "night": the keys of AgendaFactors

	// The shape of the day: when the night falls and ends, how long the dusk before it is and how much earlier dusk and night come in winter
	// (and half of it in autumn).
	struct DayShape
	{
		float NightFrom = 21.0f, NightTo = 6.0f;
		float DuskHours = 3.0f;
		float DuskShift = 2.0f;
	};

	// The hour on the clock of someone whose whole day runs `nightShift` hours later than the clan's (a night watch).
	float ShiftedHour(float hour, float nightShift);
	// The part of the day it is at `hour` of the clan's clock for someone with that shift.
	DayPart PartOfDay(const DayShape& day, float hour, Season season, float nightShift = 0.0f);
	// Dusk or night by the plain clock (no season shift): the clan's time together.
	bool IsEvening(const DayShape& day, float hour);
	// What the agenda makes of an option now: the role's own factor when it lists the option in that part of the day, else the clan's, else 1.
	float ScheduleFactor(const DayShape& day, const AgendaFactors& clan, const AgendaFactors* role, float nightShift, const std::string& kind, float hour, Season season);
}
