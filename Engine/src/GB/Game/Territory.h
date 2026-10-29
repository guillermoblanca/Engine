#pragma once
#include "GB/Game/Diplomacy.h"
#include "GB/World/Regions.h"
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// The land the clans claim (H13), region by region (GB/World/Regions.h). A clan can hold a region in proportion to its strength (its living
	// members and its buildings) and to how close the region is to its camp; what it holds grows towards that a little every day (time makes a
	// claim) and shrinks when the clan does or moves away. A region whose strongest claim passes TerritoryRules::Min has an owner; when two or
	// more clans claim it past Min the claims overlap (allies share it; for the rest it is a contested region, a cause of grievance and, with H14,
	// of conflict). Plain CPU state, deterministic (clans and regions in id order), saved with the clans.

	// What the claims need of a clan at a moment.
	struct ClanFootprint
	{
		ClanId Clan = -1;
		glm::vec2 Camp{ 0.0f };
		int Members = 0;               // living
		float Buildings = 0.0f;        // H8's buildings (0 until there are any)
	};

	class TerritoryMap
	{
	public:
		void Clear() { m_Claims.clear(); }
		// Moves every claim `dt` seconds towards what each clan can hold now (a day is `daySeconds`). Regions that are water are never claimed.
		void Step(const RegionMap& regions, const std::vector<ClanFootprint>& clans, const TerritoryRules& rules, float dt, float daySeconds);
		// What a clan could hold of a region now (the target the claim moves to).
		static float Reachable(const RegionSummary& region, const ClanFootprint& clan, int campRegion, const TerritoryRules& rules);

		float Claim(ClanId clan, int region) const;
		// The clan with the strongest claim past Min (ties: the lower id), or -1.
		ClanId Owner(int region, const TerritoryRules& rules) const;
		// The clans claiming a region past Min, strongest first.
		std::vector<ClanId> Claimants(int region, const TerritoryRules& rules) const;
		// The regions a clan holds (it is their owner).
		std::vector<int> Held(ClanId clan, const TerritoryRules& rules) const;
		// Regions claimed past Min by two or more clans, in id order, with their claimants.
		std::vector<std::pair<int, std::vector<ClanId>>> Overlaps(const TerritoryRules& rules) const;
		void Forget(ClanId clan) { m_Claims.erase(clan); }
		const std::map<ClanId, std::map<int, float>>& Claims() const { return m_Claims; }

		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (nothing changed) on malformed text
		bool operator==(const TerritoryMap&) const = default;

	private:
		std::map<ClanId, std::map<int, float>> m_Claims;   // clan -> region -> strength (only the regions it has some claim on)
	};
}
