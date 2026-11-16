#include "GB/Game/FieldPlacement.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/Tribe.h"
#include <utility>
#include <vector>

namespace GB
{
	glm::vec2 FieldOriginFor(const Tribe& tribe, const glm::vec2& wanted)
	{
		const FarmRules& f = tribe.Config.Econ.Farming;
		const ITribeBody* body = nullptr;
		for (const TribeMember& m : tribe.Members()) if (!m.Dead && m.Body) { body = m.Body; break; }
		// what is taken already: the plots (and stores) of the other fields, and every site and building with its footprint
		std::vector<std::pair<glm::vec2, float>> taken;
		for (const Field& field : tribe.Economy().Fields)
		{
			for (size_t i = 0; i < field.Plots.size(); ++i) taken.push_back({ field.PlotPos(i), 0.9f * f.Spacing });
			taken.push_back({ field.Origin + glm::vec2(-f.Spacing, 0.0f), 0.9f * f.Spacing });
		}
		if (const auto* defs = tribe.Buildings())
			for (const Tribe::SettlementSite& s : tribe.SettlementSites())
				if (const auto d = defs->find(s.Building); d != defs->end()) taken.push_back({ s.Pos, d->second.Radius() + 0.5f });
		auto free = [&](const glm::vec2& p)
		{
			if (body && (!body->CanStandAt(p) || body->WaterDepthAt(p) > kWetDepth)) return false;   // a rock, a trunk, the river
			for (const auto& [c, r] : taken) if (glm::distance(p, c) < r) return false;
			return true;
		};
		return PlaceField(wanted, f.Plots, f.Columns, f.Spacing, free).value_or(wanted);
	}
}
