#include "GB/Game/Territory.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	float TerritoryMap::Reachable(const RegionSummary& region, const ClanFootprint& clan, int campRegion, const TerritoryRules& rules)
	{
		if (region.Water || clan.Members <= 0) return 0.0f;
		const float d = region.Id == campRegion ? 0.0f : glm::distance(clan.Camp, region.Anchor);
		if (d > rules.Reach) return 0.0f;
		const float power = static_cast<float>(clan.Members) * rules.PerMember + clan.Buildings * rules.PerBuilding;
		return power / (1.0f + d / std::max(rules.Scale, 1.0f));
	}

	void TerritoryMap::Step(const RegionMap& regions, const std::vector<ClanFootprint>& clans, const TerritoryRules& rules, float dt, float daySeconds)
	{
		const float rate = std::clamp(rules.Grow * dt / std::max(daySeconds, 1.0f), 0.0f, 1.0f);
		std::map<ClanId, std::map<int, float>> next;
		for (const ClanFootprint& c : clans)
		{
			const int home = regions.RegionAt(c.Camp);
			std::map<int, float>& claims = next[c.Clan];
			const auto old = m_Claims.find(c.Clan);
			for (const RegionSummary& g : regions.Regions())
			{
				float claim = 0.0f;
				if (old != m_Claims.end())
					if (const auto it = old->second.find(g.Id); it != old->second.end()) claim = it->second;
				const float target = Reachable(g, c, home, rules);
				claim += (target - claim) * rate;
				if (claim >= 0.01f) claims[g.Id] = claim;   // a claim that has faded to nothing goes (the map stays small)
			}
			if (claims.empty()) next.erase(c.Clan);
		}
		m_Claims = std::move(next);   // (a clan that is not in the list any more loses its claims at once)
	}

	float TerritoryMap::Claim(ClanId clan, int region) const
	{
		const auto c = m_Claims.find(clan);
		if (c == m_Claims.end()) return 0.0f;
		const auto r = c->second.find(region);
		return r == c->second.end() ? 0.0f : r->second;
	}

	std::vector<ClanId> TerritoryMap::Claimants(int region, const TerritoryRules& rules) const
	{
		std::vector<std::pair<ClanId, float>> all;
		for (const auto& [clan, claims] : m_Claims)
			if (const auto r = claims.find(region); r != claims.end() && r->second >= rules.Min) all.emplace_back(clan, r->second);
		std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		std::vector<ClanId> out;
		for (const auto& [clan, s] : all) out.push_back(clan);
		return out;
	}

	ClanId TerritoryMap::Owner(int region, const TerritoryRules& rules) const
	{
		const std::vector<ClanId> c = Claimants(region, rules);
		return c.empty() ? -1 : c.front();
	}

	std::vector<int> TerritoryMap::Held(ClanId clan, const TerritoryRules& rules) const
	{
		std::vector<int> out;
		if (const auto c = m_Claims.find(clan); c != m_Claims.end())
			for (const auto& [region, s] : c->second) if (s >= rules.Min && Owner(region, rules) == clan) out.push_back(region);
		return out;
	}

	std::vector<std::pair<int, std::vector<ClanId>>> TerritoryMap::Overlaps(const TerritoryRules& rules) const
	{
		std::map<int, int> count;
		for (const auto& [clan, claims] : m_Claims)
			for (const auto& [region, s] : claims) if (s >= rules.Min) ++count[region];
		std::vector<std::pair<int, std::vector<ClanId>>> out;
		for (const auto& [region, n] : count) if (n >= 2) out.emplace_back(region, Claimants(region, rules));
		return out;
	}

	std::string TerritoryMap::ToJson() const
	{
		Json j = Json::object();
		for (const auto& [clan, claims] : m_Claims)
		{
			Json c = Json::object();
			for (const auto& [region, s] : claims) c[std::to_string(region)] = s;
			j[std::to_string(clan)] = c;
		}
		return j.dump();
	}

	bool TerritoryMap::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		std::map<ClanId, std::map<int, float>> claims;
		for (const auto& [clan, c] : j.items())
		{
			if (!c.is_object()) return false;
			for (const auto& [region, s] : c.items())
			{
				if (!s.is_number()) return false;
				claims[std::atoi(clan.c_str())][std::atoi(region.c_str())] = s.get<float>();
			}
		}
		m_Claims = std::move(claims);
		return true;
	}
}
