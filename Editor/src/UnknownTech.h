#pragma once
// T327 (documentation/01_diseno/ARBOL_TECNICO.md §11.2.5): what the clan does not know does not show. A building, a recipe or a verb that only a
// technology the clan does not know opens is not drawn, not even greyed: the grid or the radial ends in one "?" with how many are left. These read
// back, from the public knowledge of the clan, the tests the tribe keeps private (Tribe::ClanKnowsBuilding, Tribe::ClanKnows(Recipe)): a living
// member with a body knows one of the technologies. The verbs have their own (GB/Game/TechVerbs.h). UI only: the sim decides nothing from here.
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Techs.h"
#include <algorithm>
#include <string>
#include <vector>

namespace GB::Editor
{
	// a living member of the clan knows one of `techs` (true with no tree of technologies: nothing is gated)
	inline bool ClanKnowsAnyTech(const Tribe& t, const std::vector<std::string>& techs)
	{
		const ClanKnowledge& knowledge = t.Knowledge();
		if (!knowledge.Gated()) return true;
		for (const std::string& tech : techs)
			for (const TribeMember& m : t.Members())
				if (!m.Dead && m.Body && knowledge.Knows(m.Name, tech)) return true;
		return false;
	}

	// the technologies that open a building: its own `tech` and those that list it (by id or by name), as Tribe::ClanKnowsBuilding reads them
	inline std::vector<std::string> BuildingTechs(const Tribe& t, const BuildingDef& d)
	{
		std::vector<std::string> out;
		if (!d.Tech.empty()) out.push_back(d.Tech);
		if (const TechTree* tree = t.Knowledge().Techs())
			for (const auto& [id, tech] : tree->Techs)
				if ((std::find(tech.Buildings.begin(), tech.Buildings.end(), d.Id) != tech.Buildings.end() || std::find(tech.Buildings.begin(), tech.Buildings.end(), d.Name) != tech.Buildings.end())
					&& std::find(out.begin(), out.end(), id) == out.end())
					out.push_back(id);
		return out;
	}

	// a building hidden from the player: some technology locks it and the clan knows none of them (a lock of rank or culture alone is not hidden)
	inline bool BuildingUnknown(const Tribe& t, const BuildingDef& d)
	{
		const std::vector<std::string> techs = BuildingTechs(t, d);
		return !techs.empty() && t.Knowledge().Gated() && !ClanKnowsAnyTech(t, techs);
	}

	// a recipe hidden from the player: some technology locks it and no living member can make it (Tribe::ClanKnows(Recipe))
	inline bool RecipeUnknown(const Tribe& t, const std::string& recipe)
	{
		const ClanKnowledge& knowledge = t.Knowledge();
		if (!knowledge.Gated()) return false;
		for (const TribeMember& m : t.Members())
			if (!m.Dead && m.Body && knowledge.CanMake(m.Name, recipe)) return false;
		return true;
	}
}
