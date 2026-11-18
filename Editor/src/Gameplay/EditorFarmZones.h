#pragma once
// T280 (A1 of documentation/01_diseno/AGRICULTURA.md; MODO_COLONIA.md §4.4): the command of the farm zones of the player's clan (GB/Game/FarmZones.h).
//   game zones                                       every field of the clan, the zones with their crop and plan
//   game zone farm <x0> <z0> <x1> <z1> [crop]        paints a zone over that rectangle (the plots' middles, 2 m apart)
//   game zone farm poly <x0> <z0> <x1> <z1> <x2> <z2> ... [crop]   a zone of that perimeter (the cells whose middle falls inside)
//   game zone <id> crop <crop|any>                   what it is sown with
//   game zone <id> plan <sow|harvest|fallow>         its plan
//   game zone <id> auto on|off                       the clan chooses its crop (on) or keeps the one given (off)
//   game zone <id> remove                            the zone goes
// The UI (T281) only queues these.
#include "../Commands.h"
#include <string>
#include <vector>

namespace GB { class Tribe; }

namespace GB::Editor
{
	// `sub` is "zone" or "zones"; `a` the words after it; `t` the player's clan's tribe (nullptr: none).
	CommandResult FarmZoneCommand(Tribe* t, const std::string& sub, const std::vector<std::string>& a);
}
