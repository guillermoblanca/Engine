#include "GB/Game/TribePriorities.h"
#include "GB/AI/Needs.h"
#include "GB/Game/Tribe.h"
#include <cmath>

namespace GB
{
	namespace
	{
		float SeasonIndex(Season s) { return static_cast<float>(static_cast<int>(s)); }

		glm::vec2 PositionOf(const TribeMember& m) { return m.Body ? m.Body->Position() : glm::vec2(0.0f); }
	}

	PgInputs PriorityInputsFor(const Tribe& tribe, const TribeMember& m, const UtilityOption* option)
	{
		PgInputs in;
		auto& v = in.Values;
		const std::map<std::string, float> needs = tribe.NeedLevels(m);
		auto level = [&](const char* n) { const auto it = needs.find(n); return it == needs.end() ? 100.0f : it->second; };
		v["need.hunger"] = level(NeedName::Hunger);
		v["need.thirst"] = level(NeedName::Thirst);
		v["need.rest"] = level(NeedName::Rest);
		v["need.warmth"] = level(NeedName::Warmth);
		v["need.safety"] = level(NeedName::Safety);
		v["need.company"] = level(NeedName::Company);
		v["need.health"] = level(NeedName::Health);
		v["need.vigor"] = level(NeedName::Vigor);
		v["danger"] = (100.0f - level(NeedName::Safety)) / 100.0f;
		const float hour = tribe.Hour();
		v["time.hour"] = hour;
		v["time.night"] = IsNight(tribe.Config.Needs, hour) ? 1.0f : 0.0f;
		v["season"] = SeasonIndex(tribe.CurrentSeason());
		const Person* person = tribe.People().Find(m.Name);
		v["agent.age"] = person ? person->Age : 25.0f;
		const auto role = tribe.Config.Roles.find(m.Role);
		v["agent.skill"] = role != tribe.Config.Roles.end() && !role->second.Skill.empty() ? tribe.SkillOf(m, role->second.Skill) : 0.0f;
		int alive = 0;
		for (const TribeMember& other : tribe.Members()) if (!other.Dead) ++alive;
		v["clan.members"] = static_cast<float>(alive);
		v["fire.burning"] = tribe.FireBurning() ? 1.0f : 0.0f;
		if (option)
		{
			v["option.score"] = option->Score;
			v["option.distance"] = glm::distance(PositionOf(m), option->Where);
			v["option.commit"] = option->Commit;
			v["role.weight"] = tribe.RoleWeight(m.Role, option->Kind);
			const Person* other = option->Target.empty() ? nullptr : tribe.People().Find(option->Target);
			v["option.affinity"] = other ? tribe.People().Affinity(tribe.Config.Society, m.Name, option->Target) : 0.0f;
		}
		const Tribe* t = &tribe;
		const TribeMember* who = &m;
		in.Resolver = [t, who, option, person](const std::string& name, float& out) -> bool
		{
			auto after = [&](const char* prefix) { return name.rfind(prefix, 0) == 0 ? name.substr(std::string(prefix).size()) : std::string(); };
			if (const std::string f = after("factor."); !f.empty())
			{
				out = 0.0f;
				if (option) for (const ScoreFactor& sf : option->Factors) if (sf.Name == f) { out = sf.Value; break; }
				return true;
			}
			if (const std::string s = after("store."); !s.empty()) { out = static_cast<float>(t->Store().Count(s)); return true; }
			if (const std::string s = after("skill."); !s.empty()) { out = t->SkillOf(*who, s); return true; }
			if (const std::string s = after("trait."); !s.empty())
			{
				out = 0.0f;
				if (person) if (const auto it = person->Traits.find(s); it != person->Traits.end()) out = it->second;
				return true;
			}
			return false;
		};
		return in;
	}

	void ApplyPriorityGraphs(const Tribe& tribe, TribeMember& m, const PriorityLibrary& library, std::vector<UtilityOption>& options)
	{
		const CompiledPriorityGraph* graph = library.CompiledFor(m.Role, m.Name);
		if (!graph || (graph->Identity && !library.ForceEvaluate)) return;
		for (UtilityOption& o : options)
		{
			if (o.Score < 0.0f) continue;
			const PgInputs inputs = PriorityInputsFor(tribe, m, &o);
			const PgResult result = EvaluatePriorityGraph(*graph, inputs);
			const PgOutput* out = result.For(o.Kind);
			if (!out) continue;
			float score = ApplyPgMode(out->Mode, o.Score, out->Value);
			if (!std::isfinite(score) || score < 0.0f) score = 0.0f;
			if (score == o.Score) continue;
			o.Factors.push_back({ "before graph", o.Score });   // (the editor shows what the tribe had given it)
			o.Score = score;
			o.Factors.push_back({ "graph", score });
		}
	}
}
