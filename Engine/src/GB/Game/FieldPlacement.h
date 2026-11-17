#pragma once
// T215: where the clan marks out a new field. The farming rules put it at a fixed offset from the camp (FarmRules::Offset, one row of plots
// further for each field); there a plot could fall on a rock, a trunk or the river, and the member sent to work it walked to a point it never
// reached and starved (GBSim, temperate seed 7). Now the plots, and the store beside the first, go on ground a member can stand on, dry, off
// the other fields and the buildings: the wanted origin when it is all free, else the nearest that is (PlaceField). Only the public face of the
// Tribe is read; Tribe::PostEconomyJobs calls it in one line.
// T260: "off the other fields and the buildings" is the footprints' (GroundFootprint.h), any clan's; and with no room there is no field (it was
// marked on the wanted spot anyway, on top of whatever stood there).
#include <glm/glm.hpp>
#include <optional>

namespace GB
{
	class Tribe;

	// The origin for the next field, deterministic for the same scene; nothing when no spot near the wanted one is free.
	std::optional<glm::vec2> FieldOriginFor(const Tribe& tribe, const glm::vec2& wanted);
}
