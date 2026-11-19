// T331: the generic modifiers of the technologies (see TechModifiers.h).
#include "GB/Game/TechModifiers.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Techs.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	float TechModifiers::Of(const TechTree* tree, const std::set<std::string>& known, const std::string& key)
	{
		if (!tree) return 1.0f;
		float f = 1.0f;
		for (const auto& [id, t] : tree->Techs)
		{
			if (t.Modifiers.empty()) continue;
			const auto k = t.Modifiers.find(key);
			if (k != t.Modifiers.end() && known.count(id)) f *= k->second;
		}
		return f;
	}

	float TechModifiers::Of(const Tribe& tribe, const std::string& key)
	{
		const ClanKnowledge& knowledge = tribe.Knowledge();
		return Of(knowledge.Techs(), knowledge.Clan(), key);
	}

	int TechModifiers::Units(int units, float factor)
	{
		if (units <= 0 || factor == 1.0f) return units;
		return std::max(1, static_cast<int>(std::lround(static_cast<float>(units) * factor)));
	}

	float TechModifiers::Night(float agenda, float factor)
	{
		if (agenda >= 1.0f || factor == 1.0f) return agenda;
		return std::min(1.0f, agenda * factor);
	}
}
