#pragma once
#include "GB/Gameplay/WorldResources.h"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class EntityWorld;
	class GameplayWorld;

	// Where a clan settles (H8, "elección de lugar"): every candidate place of the map gets a score from what a camp needs, on the CPU and without a
	// scene, so a test can choose the camp of a whole generated world. Deterministic: a fixed grid of candidates, no randomness, ties broken by place.
	//   water     - close to drinking water, but not on the bank (floods, mud): best at WaterBest metres, nothing beyond WaterFar
	//   resources - the nodes within ResourceRadius of each kind, weighed by what the clan wants of that kind (SiteWeights::Want); a kind counts
	//               fully with Enough of them, nearer ones count more
	//   flat      - the mean slope over the camp's ground (FlatRadius); a camp on a slope steeper than MaxSlope anywhere is refused
	//   biome     - how good the ground is to live on (meadow best; snow and water refused)
	//   shelter   - trees and rocks of the world near the camp break the wind (but not inside the camp)
	// A candidate is refused in the water or too close to it, too steep, on a refused biome, with a tree or a rock inside the camp, or too near the edge.

	struct SiteResource
	{
		std::string Kind;           // node type id ("berry_bush")
		glm::vec2 At{ 0.0f };
	};

	struct SiteWeights
	{
		float Water = 1.0f, Resources = 1.5f, Flat = 0.8f, Biome = 0.6f, Shelter = 0.3f;   // the weight of each part in the total
		float WaterBest = 20.0f;      // the best distance to the water (metres)
		float WaterNear = 4.0f;       // closer than this is refused (the bank)
		float WaterFar = 120.0f;      // farther than this the water part is 0 (the well has to do)
		float ResourceRadius = 40.0f;
		float Enough = 3.0f;          // nodes of a kind (counted with their nearness) that make that kind count fully
		std::map<std::string, float> Want;   // node type -> how much the clan wants it (ClanSiteWants); a kind not listed counts 0
		float MaxSlope = 14.0f;       // degrees, anywhere on the camp's ground
		float FlatRadius = 6.0f;      // the camp's ground
		float ClearRadius = 4.0f;     // no tree or rock of the world this close
		float Edge = 16.0f;           // metres from the edge of the map
		float Spacing = 8.0f;         // between candidates
		std::map<std::string, float> BiomeValue = { { "Meadow", 1.0f }, { "Forest", 0.7f }, { "Shore", 0.5f }, { "Dunes", 0.25f }, { "Rock", 0.2f } };   // a biome not listed is refused
		float ShelterRadius = 18.0f;
		float ShelterEnough = 6.0f;   // trees and rocks within ShelterRadius that make it fully sheltered
	};

	struct SiteRating
	{
		glm::vec2 At{ 0.0f };
		bool Ok = false;
		std::string Why;              // why it is refused ("in the water", "too steep: 21 degrees"...)
		float Total = 0.0f;
		float Water = 0.0f, Resources = 0.0f, Flat = 0.0f, Biome = 0.0f, Shelter = 0.0f;   // each 0..1, before its weight
		std::map<std::string, int> Near;   // node type -> how many within ResourceRadius
		std::string Text() const;     // "score 2.31 at (12, -40): water 0.9, resources 0.7 (berry_bush 4, tree 6), flat 0.8, biome 1, shelter 0.3"
	};

	// The score of one place.
	SiteRating ScoreSite(const ResourceGround& ground, const std::vector<SiteResource>& resources, const SiteWeights& weights, const glm::vec2& at);
	// Every candidate of the grid that is not refused, best first (ties: lower x, then lower z). `top` > 0 keeps only that many.
	std::vector<SiteRating> RankSites(const ResourceGround& ground, const std::vector<SiteResource>& resources, const SiteWeights& weights, int top = 0);
	// The best one; Ok is false when no place fits (then At is the middle of the map and Why says so).
	SiteRating BestSite(const ResourceGround& ground, const std::vector<SiteResource>& resources, const SiteWeights& weights);

	// The resource nodes of a scene (entities with an ability system tagged Resource.Node.<id> for an id of `nodes`), in name order.
	std::vector<SiteResource> SceneResources(const EntityWorld& world, const GameplayWorld& gameplay, const std::map<std::string, ResourceNodeDef>& nodes);
	// The planned spots of PlanWorldResources as site resources (to choose a camp before the nodes are in a scene).
	std::vector<SiteResource> SpotResources(const std::vector<ResourceSpot>& spots);
}
