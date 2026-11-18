#include "GB/Game/MealLog.h"
#include "GB/Game/Social.h"

namespace GB
{
	std::string MealLog::Source(bool pantry, bool bag, const std::string& shed)
	{
		if (pantry) return "pantry";
		if (bag) return "bag";
		return shed.empty() ? std::string("store") : "store@" + shed;
	}

	float MealLog::Nutrition(const DietRules& diet, bool social, int foodHunger, const std::string& item)
	{
		if (!social) return static_cast<float>(foodHunger);
		const auto f = diet.Foods.find(item);
		return f != diet.Foods.end() ? f->second.Nutrition : static_cast<float>(foodHunger);
	}

	void MealLog::Record(const std::string& item, const std::string& source, float nutrition)
	{
		for (Tally* t : { &m_ByItem[item], &m_BySource[source.rfind("store@", 0) == 0 ? std::string("store@") : source], &m_Total })
		{
			++t->Units;
			t->Nutrition += static_cast<double>(nutrition);
		}
	}
}
