#include "GB/Game/Values.h"
#include <algorithm>
#include <cstdint>

namespace GB
{
	namespace
	{
		// FNV-1a: the same number on every machine and every run
		std::uint32_t Hash(const std::string& s, std::uint32_t h = 2166136261u)
		{
			for (unsigned char c : s) { h ^= c; h *= 16777619u; }
			return h;
		}

		int Spread(std::uint32_t h, int spread)
		{
			return spread <= 0 ? 0 : static_cast<int>(h % static_cast<std::uint32_t>(2 * spread + 1)) - spread;
		}
	}

	ValueRules DefaultValueRules()
	{
		ValueRules r;
		r.Kinds = {
			{ "birth", { "family", 1.0f } }, { "pair", { "family", 1.0f } }, { "marriage", { "family", 1.0f } }, { "death", { "family", -1.0f } },
			{ "discovery", { "knowledge", 1.0f } }, { "learnt", { "knowledge", 1.0f } }, { "era", { "knowledge", 1.0f } },
			{ "built", { "craft", 1.0f } },
			{ "tradition", { "tradition", 1.0f } }, { "shaman", { "tradition", 1.0f } }, { "norm", { "tradition", 1.0f } },
			{ "raid", { "peace", -1.0f } }, { "killed", { "peace", -1.0f } }, { "wounded", { "peace", -1.0f } }, { "theft", { "peace", -1.0f } },
			{ "break", { "peace", -1.0f } }, { "truce", { "peace", 1.0f } }, { "alliance", { "peace", 1.0f } },
			{ "trade", { "trade", 1.0f } }, { "gift", { "trade", 1.0f } }, { "pact", { "trade", 1.0f } }, { "contact", { "trade", 1.0f } },
			{ "hunt", { "nature", -1.0f } }, { "first_kill", { "power", 1.0f } }, { "camp_moved", { "nature", 1.0f } },
			{ "help", { "family", 1.0f } }, { "found", { "power", 1.0f } },
		};
		return r;
	}

	int CultureValue(const ValueRules& rules, int clan, const std::string& value)
	{
		if (clan < 0) return 0;
		return Spread(Hash(value, Hash("clan " + std::to_string(clan), Hash(std::to_string(rules.Seed)))), rules.CultureSpread);
	}

	int PersonValue(const ValueRules& rules, int clan, const std::string& person, const std::string& value)
	{
		const int own = Spread(Hash(value, Hash("person " + person, Hash(std::to_string(rules.Seed)))), rules.PersonSpread);
		return std::clamp(CultureValue(rules, clan, value) + own, -50, 50);
	}

	std::map<std::string, int> PersonValues(const ValueRules& rules, int clan, const std::string& person)
	{
		std::map<std::string, int> out;
		for (const std::string& v : rules.Names) out[v] = PersonValue(rules, clan, person, v);
		return out;
	}

	float Opinion(const ValueRules& rules, int clan, const std::string& person, const HistoryEvent& e, float feeling)
	{
		const auto it = rules.Kinds.find(e.Kind);
		if (it == rules.Kinds.end()) return std::clamp(feeling, -1.0f, 1.0f);
		const float v = static_cast<float>(PersonValue(rules, clan, person, it->second.Value));
		return std::clamp(feeling + it->second.Sign * v * rules.Weight, -1.0f, 1.0f);
	}

	std::string ValuesReport(const ValueRules& rules, int clan, const std::string& person)
	{
		std::vector<std::pair<std::string, int>> all;
		for (const auto& [name, v] : PersonValues(rules, clan, person)) all.push_back({ name, v });
		std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return std::abs(a.second) > std::abs(b.second); });
		std::string s;
		for (size_t i = 0; i < all.size() && i < 2; ++i)
			s += (s.empty() ? "" : ", ") + std::string(all[i].second >= 0 ? "values " : "scorns ") + all[i].first + " (" + std::to_string(all[i].second) + ")";
		return s;
	}
}
