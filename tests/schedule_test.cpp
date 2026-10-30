// The timetable alone (GB/Game/Schedule.h): the part of the day for the clan and for a night watch, how dusk and night come earlier in autumn and
// winter, the evening by the plain clock, and the agenda factors (a role's own win over the clan's; what nobody lists is 1). CPU only, no Tribe.
#include "GB/Game/Schedule.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

int main()
{
	using namespace GB;
	const DayShape day;   // night 21..6, dusk 3 h before it, winter 2 h earlier
	Check(PartOfDay(day, 12.0f, Season::Summer) == DayPart::Day, "noon is day");
	Check(PartOfDay(day, 18.5f, Season::Summer) == DayPart::Dusk, "half past six is dusk (the night falls at 21, the dusk 3 h before)");
	Check(PartOfDay(day, 23.0f, Season::Summer) == DayPart::Night && PartOfDay(day, 3.0f, Season::Summer) == DayPart::Night, "the night wraps midnight");
	Check(PartOfDay(day, 6.0f, Season::Summer) == DayPart::Day, "and ends at 6");
	Check(PartOfDay(day, 19.5f, Season::Winter) == DayPart::Night && PartOfDay(day, 19.5f, Season::Autumn) == DayPart::Dusk, "winter brings the night 2 h earlier, autumn 1 h");
	Check(PartOfDay(day, 18.5f, Season::Winter) == DayPart::Dusk && PartOfDay(day, 15.5f, Season::Winter) == DayPart::Day, "and the dusk with it");
	// a night watch lives its day 6 h later: when the clan is at dusk it is still at midday
	Check(PartOfDay(day, 18.5f, Season::Summer, 6.0f) == DayPart::Day && PartOfDay(day, 3.0f, Season::Summer, 6.0f) == DayPart::Night, "a watch of 6 h lives its day later");
	Check(std::fabs(ShiftedHour(2.0f, 6.0f) - 20.0f) < 1e-5f && ShiftedHour(7.0f, 0.0f) == 7.0f, "the shifted hour wraps");
	Check(IsEvening(day, 18.0f) && IsEvening(day, 2.0f) && !IsEvening(day, 12.0f) && !IsEvening(day, 17.9f), "the evening is dusk and night by the plain clock");
	Check(std::strcmp(DayPartName(DayPart::Dusk), "dusk") == 0, "the parts are named as the agenda keys them");

	AgendaFactors clan;
	clan["night"] = { { "sleep", 1.6f }, { "gather", 0.4f } };
	clan["dusk"] = { { "socialize", 2.5f } };
	AgendaFactors watch;
	watch["day"] = { { "sleep", 2.0f } };
	watch["night"] = { { "gather", 1.0f } };
	Check(std::fabs(ScheduleFactor(day, clan, nullptr, 0.0f, "sleep", 23.0f, Season::Summer) - 1.6f) < 1e-6f, "the clan's night favours sleep");
	Check(ScheduleFactor(day, clan, nullptr, 0.0f, "sleep", 12.0f, Season::Summer) == 1.0f, "what nobody lists is 1");
	Check(ScheduleFactor(day, clan, nullptr, 0.0f, "socialize", 19.0f, Season::Summer) == 2.5f, "the dusk is for being together");
	// a watch at 3 h clan time: its clock says 21 h (night): it works (1.0) where the clan sleeps (gather 0.4)
	Check(ScheduleFactor(day, clan, &watch, 6.0f, "gather", 3.0f, Season::Summer) == 1.0f && ScheduleFactor(day, clan, nullptr, 0.0f, "gather", 3.0f, Season::Summer) == 0.4f, "the watch works the night the clan sleeps");
	// a role that lists nothing for the option at that time falls back on the clan's
	Check(ScheduleFactor(day, clan, &watch, 0.0f, "socialize", 19.0f, Season::Summer) == 2.5f, "a role that does not list an option takes the clan's factor");

	if (failures == 0) std::printf("schedule: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
