#include "GB/Game/Skills.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	SkillRules DefaultSkillRules()
	{
		SkillRules r;
		// (Resource.Plant.Branch is a branch of a young tree: woodcutting, though the forager can take it too.)
		r.Families = { { "Resource.Plant", "foraging" }, { "Resource.Loose", "foraging" }, { "Resource.Plant.Branch", "woodcutting" }, { "Resource.Wood", "woodcutting" },
			{ "Resource.Stone", "mining" }, { "craft", "crafting" }, { "fire", "firekeeping" }, { "stoke", "firekeeping" }, { "build", "building" }, { "deliver", "building" } };   // (H8: carrying to a site and working it)
		r.Practice = { { "gather", 1.0f }, { "craft", 4.0f }, { "stoke", 2.0f }, { "fire", 3.0f }, { "build", 2.0f }, { "deliver", 0.5f } };
		return r;
	}

	std::string SkillFamily(const SkillRules& rules, const std::string& key, const std::map<std::string, ResourceNodeDef>* defs)
	{
		if (const auto f = rules.Families.find(key); f != rules.Families.end()) return f->second;
		if (defs)
			if (const auto def = defs->find(key); def != defs->end())
				for (std::string tag = ResourceKindTag(def->second); tag.find('.') != std::string::npos; tag.resize(tag.rfind('.')))
					if (const auto f = rules.Families.find(tag); f != rules.Families.end()) return f->second;
		return {};
	}

	float SkillLevel(const SkillSet& skills, const std::string& family)
	{
		const auto it = skills.find(family);
		return it == skills.end() ? 0.0f : it->second;
	}

	float Practise(SkillSet& skills, const SkillRules& rules, const std::string& family, float units, bool profession)
	{
		if (family.empty() || units <= 0.0f || rules.Max <= 0.0f) return 0.0f;
		float& level = skills[family];
		const float before = level;
		// one unit at a time, so a long trip has the same diminishing returns as many short ones
		const float gain = rules.Gain * (profession ? rules.Profession : 1.0f);
		for (float left = units; left > 0.0f; left -= 1.0f)
			level = std::min(rules.Max, level + gain * std::min(left, 1.0f) * (1.0f - level / rules.Max));
		return level - before;
	}

	void DecaySkills(SkillSet& skills, const SkillRules& rules, float seconds, float daySeconds)
	{
		if (rules.DecayPerDay <= 0.0f || seconds <= 0.0f) return;
		const float loss = rules.DecayPerDay * seconds / std::max(daySeconds, 1.0f);
		for (auto it = skills.begin(); it != skills.end();)
		{
			it->second -= loss;
			it = it->second <= 0.0f ? skills.erase(it) : std::next(it);
		}
	}

	float SkillEffect(const SkillSet& skills, const SkillRules& rules, const std::string& family, bool profession)
	{
		if (family.empty() || rules.Max <= 0.0f) return 0.0f;
		return std::clamp(SkillLevel(skills, family) / rules.Max, 0.0f, 1.0f) * (profession ? rules.Profession : 1.0f);
	}

	std::string BestSkill(const SkillSet& skills)
	{
		std::string best;
		float level = 0.0f;
		for (const auto& [family, l] : skills)
			if (l > level) { level = l; best = family; }
		return best;
	}

	std::string SkillsToJson(const SkillSet& skills)
	{
		Json o = Json::object();
		for (const auto& [family, level] : skills) o[family] = level;   // full precision: a loaded game continues exactly
		return o.dump();
	}

	SkillSet SkillsFromJson(const std::string& json)
	{
		SkillSet out;
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return out;
		for (const auto& [family, v] : j.items())
			if (v.is_number() && v.get<float>() > 0.0f) out[family] = v.get<float>();
		return out;
	}
}
