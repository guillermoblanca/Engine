#include "GB/Game/FieldPlacement.h"
#include "GB/Game/GroundFootprint.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/Tribe.h"
#include <vector>

namespace GB
{
	std::optional<glm::vec2> FieldOriginFor(const Tribe& tribe, const glm::vec2& wanted)
	{
		const FarmRules& f = tribe.Config.Econ.Farming;
		const ITribeBody* body = nullptr;
		for (const TribeMember& m : tribe.Members()) if (!m.Dead && m.Body) { body = m.Body; break; }
		// T260: what is taken already, as footprints: every site and building round the camp (any clan's), this clan's fields and the other clans'
		const std::vector<GroundRect> taken = TakenGround(tribe);
		auto free = [&](const glm::vec2& p) { return !body || (body->CanStandAt(p) && body->WaterDepthAt(p) <= kWetDepth); };   // a rock, a trunk, the river
		auto room = [&](const glm::vec2& origin) { return GroundFree(FieldGround(origin, f.Plots, f.Columns, f.Spacing), taken, kFieldGap).empty(); };
		return PlaceField(wanted, f.Plots, f.Columns, f.Spacing, free, 8, room);
	}
}
