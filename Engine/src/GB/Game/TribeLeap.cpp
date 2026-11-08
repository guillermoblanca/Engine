#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cstdio>

// V6 (docs/VISION_MUNDO_VIVO.md §9): the leap of years between two lives. The clan lives them in aggregate: the days of its society (ageing, old age,
// couples, births: the same SocietyDay a day of play runs, so the people are the same kind of people) and, every so many years, its next step
// learnt, without anyone walking or working. What happened is written to the history as one event the whole clan knows, so the next life
// remembers it ("in the years before you were grown, ...").
namespace GB
{
	namespace
	{
		constexpr int kLeapDaysMax = 40;            // the society lives at most a generation of days in a leap (births spawn bodies: a leap stays cheap)
		constexpr float kYearsPerDiscovery = 50.0f; // one next step learnt for every this many years of the leap (at most three)
		constexpr float kYearTogether = 6.0f;       // a year of the leap is worth this many evenings at the camp to the bonds
	}

	float Tribe::EraLeap() const
	{
		const TechTree* tree = m_Knowledge.Techs();
		const std::string era = m_Era.empty() ? Era() : m_Era;
		if (tree) for (const EraDef& e : tree->Rules.Eras) if (e.Id == era) return e.Leap;
		return 0.0f;
	}

	Tribe::LeapResult Tribe::Leap(float years)
	{
		LeapResult r;
		r.Years = std::max(0.0f, years);
		if (r.Years <= 0.0f) return r;
		const int aliveBefore = static_cast<int>(std::count_if(m_Members.begin(), m_Members.end(), [](const TribeMember& m) { return !m.Dead; }));
		const size_t membersBefore = m_Members.size();
		const float daysPerYear = std::max(Config.Society.Life.DaysPerYear, 1e-3f);
		const int days = std::clamp(static_cast<int>(r.Years * daysPerYear), 1, kLeapDaysMax);
		m_Leaping = true;
		m_LeapCap = std::max(aliveBefore + 4, aliveBefore * 3 / 2);   // a generation: the clan grows by half at most (a small one by four)
		for (int d = 0; d < days; ++d)
		{
			// a year together: the living grow fond as they do at the camp (Relate "camp"), so couples form and children come by the society's own rules
			std::vector<std::string> living;
			for (const TribeMember& m : m_Members) if (!m.Dead) living.push_back(m.Name);
			for (size_t a = 0; a < living.size(); ++a)
				for (size_t b = a + 1; b < living.size(); ++b) Relate(living[a], living[b], "camp", kYearTogether);
			SocietyDay();
		}
		m_Leaping = false;
		for (size_t i = membersBefore; i < m_Members.size(); ++i) ++r.Born;
		const int aliveAfter = static_cast<int>(std::count_if(m_Members.begin(), m_Members.end(), [](const TribeMember& m) { return !m.Dead; }));
		r.Died = std::max(0, aliveBefore + r.Born - aliveAfter);
		// the next steps the clan was after, learnt over the years (its goals, in their order)
		const int discoveries = std::min(3, static_cast<int>(r.Years / kYearsPerDiscovery));
		for (int i = 0; i < discoveries && m_Knowledge.Techs(); ++i)
		{
			UpdateGoals();
			const std::vector<std::string> goals = m_Knowledge.Goals();
			const auto next = std::find_if(goals.begin(), goals.end(), [&](const std::string& t) { return !m_Knowledge.Clan().count(t); });
			if (next == goals.end() || !m_Knowledge.GrantClan(*next).empty()) break;
			r.Learnt.push_back(m_Knowledge.Name(*next));
		}
		UpdateGoals();   // (and the era it is in now)
		char text[160];
		std::snprintf(text, sizeof text, "%.0f years pass: %d born, %d died", static_cast<double>(r.Years), r.Born, r.Died);
		std::string line = text;
		for (size_t i = 0; i < r.Learnt.size(); ++i) line += (i == 0 ? "; the clan learns " : ", ") + r.Learnt[i];
		Happen("leap", std::string(), std::string(), line);   // (no place: everyone of the clan knows it)
		char t[48];
		std::snprintf(t, sizeof t, "%6.1f ", m_Time);
		m_Log.push_back(std::string(t) + line);
		return r;
	}
}
