#include "GB/Game/UseLearning.h"
#include "GB/AI/Utility.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cstdio>

// T181: no eras. Knowledge won by using materials, sped by curiosity and by what one already knows, gated only by what the world gives (UseLearning.h).
namespace GB
{
	namespace
	{
		const UseRules* Rules(const Tribe& t)
		{
			const TechTree* tree = t.Knowledge().Techs();
			return tree ? &tree->Rules.Use : nullptr;
		}

		int Alive(const Tribe& t)
		{
			return static_cast<int>(std::count_if(t.Members().begin(), t.Members().end(), [](const TribeMember& m) { return !m.Dead; }));
		}

		// The physical gate is asked for every technology every time a member weighs what to try or a lesson is held: what the clan has found
		// changes slowly, so it is worked out once per tribe and instant (pure: the same state gives the same answer, so it stays deterministic).
		struct PhysicalCache
		{
			const Tribe* Of = nullptr;
			double At = -1.0;
			std::map<std::string, bool> Answers;
		};
		thread_local PhysicalCache s_Physical;
	}

	std::vector<KnowledgeEvent> UseLearning::Handled(Tribe& t, const TribeMember& m, const std::vector<std::string>& items)
	{
		std::vector<KnowledgeEvent> out;
		const UseRules* use = Rules(t);
		if (!use || use->Materials.empty() || m.Dead) return out;
		float pace = -1.0f;   // (worked out once, on the first material that leads somewhere)
		for (const std::string& item : items)
		{
			const auto leads = use->Materials.find(item);
			if (leads == use->Materials.end()) continue;
			for (const auto& [tech, rate] : leads->second)
			{
				if (rate <= 0.0f || !t.Knowledge().CanLearn(m.Name, tech) || !t.Knowledge().TierOpen(tech)) continue;   // (CanLearn: the physical gate; T323: finding it by use is inventing: its step open)
				if (pace < 0.0f) pace = Pace(t, m);
				for (KnowledgeEvent& e : t.Knowledge().Instruct(m.Name, tech, rate * pace, "by working " + item))
					if (!e.Tech.empty()) out.push_back(std::move(e));   // (the small steps stay quiet: one a trip would flood the log)
			}
		}
		return out;
	}

	std::vector<KnowledgeEvent> UseLearning::Used(Tribe& t, const TribeMember& m, const std::string& recipe, const std::string& node)
	{
		std::vector<std::string> items;
		if (!recipe.empty() && t.m_Recipes)
			if (const Recipe* r = t.m_Recipes->Find(recipe))
				for (const auto& [input, n] : r->Inputs) items.push_back(input);
		if (!node.empty())
			if (const auto def = t.NodeDefs().find(node); def != t.NodeDefs().end() && !def->second.Item.empty()) items.push_back(def->second.Item);
		return items.empty() ? std::vector<KnowledgeEvent>() : Handled(t, m, items);
	}

	std::vector<KnowledgeEvent> UseLearning::Built(Tribe& t, const TribeMember& m, const BuildingDef& def, int phase)
	{
		if (def.Phases.empty()) return {};
		const BuildingPhase& p = def.Phases[static_cast<size_t>(std::clamp(phase, 0, static_cast<int>(def.Phases.size()) - 1))];
		std::vector<std::string> items;
		for (const auto& [item, n] : p.Needs) items.push_back(item);
		return Handled(t, m, items);
	}

	bool UseLearning::Physical(const Tribe& t, const std::string& tech)
	{
		const TechTree* tree = t.Knowledge().Techs();
		const TechDef* def = tree ? tree->Find(tech) : nullptr;
		if (!def || def->Physical.Empty()) return true;
		if (s_Physical.Of != &t || s_Physical.At != t.Time()) { s_Physical.Of = &t; s_Physical.At = t.Time(); s_Physical.Answers.clear(); }
		if (const auto it = s_Physical.Answers.find(tech); it != s_Physical.Answers.end()) return it->second;
		bool ok = std::all_of(def->Physical.Materials.begin(), def->Physical.Materials.end(), [&](const std::string& item) { return Have(t, item) > 0.0f; });
		if (ok && !def->Physical.Stations.empty())
			ok = std::any_of(def->Physical.Stations.begin(), def->Physical.Stations.end(), [&](const std::string& s)
				{ return (!tree->Rules.Use.FireStation.empty() && s == tree->Rules.Use.FireStation) ? t.HasFire() : t.StationAt(s); });   // (a lit campfire: Use.FireStation)
		s_Physical.Answers[tech] = ok;
		return ok;
	}

	void UseLearning::Forget()
	{
		s_Physical = PhysicalCache();
	}

	void UseLearning::Bind(Tribe& t)
	{
		Forget();
		t.m_Knowledge.SetPhysical([&t](const std::string& tech) { return Physical(t, tech); });   // T181: the world is the gate
		t.m_Knowledge.SetIntellect([&t](const std::string& name) { return Intellect(t, name); });   // T181 (2): and the intellect, to invent
	}

	float UseLearning::Intellect(const Tribe& t, const std::string& name)
	{
		const TechTree* tree = t.Knowledge().Techs();
		if (!tree || tree->Rules.IntellectGene.empty()) return 1.0f;
		const auto gene = t.Config.Society.Genes.find(tree->Rules.IntellectGene);
		if (gene == t.Config.Society.Genes.end()) return 1.0f;   // a society without the gene: everyone reaches every idea
		if (const Person* p = t.People().Find(name))
			if (const auto g = p->Genes.find(tree->Rules.IntellectGene); g != p->Genes.end()) return g->second;
		return gene->second.Mean;   // (a member from an older save, without the gene)
	}

	bool UseLearning::Basics(const Tribe& t)
	{
		const UseRules* use = Rules(t);
		if (!use) return true;
		const int alive = Alive(t);
		if (alive <= 0) return false;
		if (static_cast<float>(t.FoodIn(t.Store())) < use->BasicsFood * static_cast<float>(alive)) return false;
		if (use->BasicsFire && !t.HasFire()) return false;
		if (static_cast<float>(t.Beds()) < use->BasicsBeds * static_cast<float>(alive)) return false;
		return true;
	}

	float UseLearning::Pace(const Tribe& t, const TribeMember& m)
	{
		const UseRules* use = Rules(t);
		if (!use || use->Effect.empty()) return 1.0f;
		const Society::Factors f = t.People().OptionFactor(t.Config.Society, m.Name, use->Effect, std::string());
		// (nature: the trait; culture: the clan's norm. The stage of life has its own pace for lessons.) T181 (2): kept within PaceMin..PaceMax
		return std::clamp(f.Nature * f.Culture, use->PaceMin, std::max(use->PaceMin, use->PaceMax));
	}

	float UseLearning::Room(const Tribe& t, const TribeMember& m)
	{
		const UseRules* use = Rules(t);
		if (!use || Basics(t)) return 1.0f;
		return use->Damp * Pace(t, m);
	}

	float UseLearning::Have(const Tribe& t, const std::string& item)
	{
		float have = static_cast<float>(t.Store().Count(item));
		for (const TribeMember& m : t.Members())
			if (!m.Dead && m.Body) have += static_cast<float>(m.Body->Bag().Count(item));
		const UseRules* use = Rules(t);
		const float half = use ? use->Near : 300.0f;
		for (const auto& [name, mem] : t.ClanMemory().All())
			if (const auto d = t.NodeDefs().find(mem.Kind); d != t.NodeDefs().end() && d->second.Item == item)
				have += static_cast<float>(mem.Amount >= 0 ? mem.Amount : d->second.Capacity) * Falloff(glm::distance(mem.Pos, t.Config.Economy.Camp), half);
		return have;
	}

	std::string UseLearning::NextStep(const Tribe& t)
	{
		const TechTree* tree = t.Knowledge().Techs();
		if (!tree) return {};
		const std::set<std::string>& known = t.Knowledge().Clan();
		std::string rung;
		for (const std::string& id : tree->Rules.Progression)   // the basics: gathering, the fire, a roof, the first tools
			if (!known.count(id) && tree->Find(id) && t.Knowledge().TierOpen(id)) { rung = id; break; }   // (T323: within the open step)
		const UseRules& use = tree->Rules.Use;
		if (use.Materials.empty() || !Basics(t)) return rung;   // survival first: what the clan found pulls once it eats and sleeps under a roof
		float best = rung.empty() ? -1.0f : 1.0f;   // (the ladder of the basics pulls 1)
		for (const auto& [item, techs] : use.Materials)
		{
			const float have = Have(t, item);
			if (have <= 0.0f) continue;
			const float pull = use.Pull + 2.0f * std::min(1.0f, have / std::max(1.0f, use.Enough));
			for (const auto& [tech, rate] : techs)
				if (rate > 0.0f && !known.count(tech) && tree->Find(tech) && Physical(t, tech) && t.Knowledge().TierOpen(tech) && pull > best) { best = pull; rung = tech; }
		}
		return rung;
	}

	float UseLearning::LeapYears(const Tribe& t)
	{
		const TechTree* tree = t.Knowledge().Techs();
		return tree ? tree->Rules.LeapYears : 0.0f;   // (one value of the data: no eras, no formula; the leap itself goes in a task of its own)
	}

	void UseLearning::Noted(Tribe& t, const KnowledgeEvent& e)
	{
		if (e.Tech.empty() || e.Who.empty() || !t.m_Telemetry) return;
		const TechTree* tree = t.Knowledge().Techs();
		const TechDef* def = tree ? tree->Find(e.Tech) : nullptr;
		const AgentKnowledge* k = t.Knowledge().Of(e.Who);
		if (!def || !k) return;
		std::string skipped;
		for (const std::string& r : def->Requires)
			if (!k->Known.count(r)) skipped += (skipped.empty() ? "" : ",") + r;
		const bool physical = Physical(t, e.Tech);
		char mind[64];
		std::snprintf(mind, sizeof(mind), "intellect: %.2f; complexity: %.2f", static_cast<double>(Intellect(t, e.Who)), static_cast<double>(Complexity(*tree, e.Tech)));
		t.EmitEvent("learn", e.Who, e.Tech, skipped.empty() ? "full" : "skipped",
			"skipped: " + skipped + "; physical: " + (physical ? "ok" : "MISSING") + "; " + mind + "; " + e.Text);
	}
}
