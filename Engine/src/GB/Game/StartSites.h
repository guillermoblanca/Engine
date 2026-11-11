#pragma once
#include "GB/Game/Exploration.h"
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	struct GeneratedWorld;
	struct SettlementSite;

	// T118 (docs GRAFO_TERRENO 14.6): the game on a world whose terrain graph published settlement sites and trails (GeneratedWorld::Settlements,
	// Trails, PathField; GB/World/Settle.h). The clans that start settle on the sites in the order of their Rank, one each, and the camp's ground is
	// the site's levelled platform (PlatformLevel); the clans beyond the sites settle as on any world. The navigation sees a levelled trail as
	// trodden ground. On a world without sites (every generator 1-4 world, a graph that does not publish them) every function here gives what the
	// game did before: no camp, the terrain's height, the undergrowth it was given.

	// The graph's sites as places a clan may settle, best first (the order of Rank). Why says which site it is; Region is -1 (the host sets it).
	std::vector<CampSite> GraphCampSites(const GeneratedWorld& world);

	// The camps of up to `count` starting clans on the graph's sites, in Rank order, skipping a site nearer than `minApart` to a camp already
	// `taken` (a place the player gave). The sites are kept apart by the graph itself (SiteParams::Separation), so they are not checked against
	// each other. Fewer than `count` when the sites run out: the rest is the host's own choice.
	std::vector<glm::vec2> GraphStartCamps(const std::vector<SettlementSite>& sites, int count, const std::vector<glm::vec2>& taken = {}, float minApart = 0.0f);

	// The graph site whose levelled platform holds `at` (within its Radius), or null.
	const SettlementSite* GraphSiteAt(const std::vector<SettlementSite>& sites, const glm::vec2& at);

	// The ground of a camp: the platform's level on a graph site, else the terrain's height there.
	float CampGround(const GeneratedWorld& world, const glm::vec2& at);

	// The undergrowth the navigation should see at x,z (NavConfig::Vegetation): none on a levelled trail (PathField over 0.5, the tread), so the
	// walkers prefer it (NavProfile::VegetationWeight); elsewhere `undergrowth`, as given.
	float TrailUndergrowth(const GeneratedWorld& world, float x, float z, float undergrowth);
}
