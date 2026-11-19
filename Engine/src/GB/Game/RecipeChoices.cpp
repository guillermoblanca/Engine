// PROTOTYPE of TL4 (see RecipeChoices.h).
#include "GB/Game/RecipeChoices.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/SettlementPlanner.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	std::vector<const Recipe*> RecipeChoices::RecipesOf(const Tribe& tribe, const std::string& station)
	{
		std::vector<const Recipe*> out;
		if (!tribe.m_Recipes || station.empty()) return out;
		for (const std::string& id : tribe.m_Recipes->Ids())
			if (const Recipe* r = tribe.m_Recipes->Find(id); r && r->Station == station) out.push_back(r);
		return out;
	}

	bool RecipeChoices::Known(const Tribe& tribe, const Recipe& r) { return tribe.ClanKnows(r); }

	int RecipeChoices::State(const std::string& building, const std::string& recipe) const
	{
		const auto b = m_Choices.find(building);
		if (b == m_Choices.end()) return 0;
		const auto r = b->second.find(recipe);
		return r == b->second.end() ? 0 : r->second;
	}

	RecipeChoices::Reply RecipeChoices::Set(Tribe& tribe, const std::string& building, const std::string& recipe, const std::string& mode, int n)
	{
		const Tribe::Placed* at = nullptr;
		const Tribe::PlacedList placed = tribe.Placements();   // (kept: `at` points into it)
		for (const Tribe::Placed& p : placed) if (p.Name == building) at = &p;
		if (!at) return { false, "no building of the clan called '" + building + "'" };
		if (at->Def->Station.empty()) return { false, building + " has no station: nothing is made there" };
		const Recipe* r = tribe.m_Recipes ? tribe.m_Recipes->Find(recipe) : nullptr;
		if (!r || r->Station != at->Def->Station) return { false, "no recipe '" + recipe + "' at a " + at->Def->Station };
		const int before = State(building, recipe);
		// a keep of this building's that ends takes its stock goal with it, unless another building keeps the same thing
		auto dropKeep = [&]()
		{
			if (before <= 0) return;
			for (const auto& [b, list] : m_Choices)
				if (b != building)
					for (const auto& [rid, s] : list)
						if (s > 0) if (const Recipe* q = tribe.m_Recipes->Find(rid); q && q->Output == r->Output) return;
			tribe.Goals().Off(r->Output);
		};
		if (mode == "on" || mode == "auto")
		{
			dropKeep();
			if (auto b = m_Choices.find(building); b != m_Choices.end()) { b->second.erase(recipe); if (b->second.empty()) m_Choices.erase(b); }
			return { true, building + ": " + recipe + " auto" };
		}
		if (mode == "off")
		{
			dropKeep();
			m_Choices[building][recipe] = kOff;
			const bool anywhere = Allows(tribe, *r);
			return { true, building + ": " + recipe + " off" + (anywhere ? std::string() : "; no " + r->Station + " of the clan makes it now") };
		}
		if (mode == "keep")
		{
			if (n < 1 || n > 9999) return { false, "usage: game recipe <building> <recipe> keep <1-9999>" };
			const StockGoals::Reply g = tribe.Goals().Set(tribe, r->Output, n);
			if (!g.Ok) return { false, g.Text };
			m_Choices[building][recipe] = n;
			return { true, building + ": " + recipe + " keep " + std::to_string(n) + " of " + r->Output };
		}
		return { false, "usage: game recipe <building> <recipe> on|off|keep <n>" };
	}

	std::string RecipeChoices::List(const Tribe& tribe, const std::string& building) const
	{
		const Tribe::PlacedList placed = tribe.Placements();
		const Tribe::Placed* at = nullptr;
		for (const Tribe::Placed& p : placed) if (p.Name == building) at = &p;
		if (!at) return "no building of the clan called '" + building + "'";
		if (at->Def->Station.empty()) return building + " has no station";
		std::string s = building + " (" + at->Def->Station + "):";
		for (const Recipe* r : RecipesOf(tribe, at->Def->Station))
		{
			const int st = State(building, r->Id);
			s += " " + r->Id + (st == 0 ? std::string(" auto") : st == kOff ? std::string(" off") : " keep " + std::to_string(st)) + (tribe.ClanKnows(*r) ? "" : " (unknown)") + ";";
		}
		if (!s.empty() && s.back() == ';') s.pop_back();
		return s;
	}

	void RecipeChoices::Forget(Tribe& tribe, const std::string& building)
	{
		const auto b = m_Choices.find(building);
		if (b == m_Choices.end()) return;
		const std::map<std::string, int> list = b->second;
		m_Choices.erase(b);
		for (const auto& [rid, s] : list)
			if (s > 0)
				if (const Recipe* r = tribe.m_Recipes ? tribe.m_Recipes->Find(rid) : nullptr)
				{
					bool kept = false;
					for (const auto& [other, l] : m_Choices) for (const auto& [q, st] : l) if (st > 0) if (const Recipe* x = tribe.m_Recipes->Find(q); x && x->Output == r->Output) kept = true;
					if (!kept) tribe.Goals().Off(r->Output);
				}
	}

	bool RecipeChoices::Allows(const Tribe& tribe, const Recipe& r) const
	{
		if (m_Choices.empty() || r.Station.empty()) return true;
		bool any = false;
		for (const Tribe::Placed& p : tribe.Placements())
		{
			if (!p.State.Done || p.Def->Station != r.Station) continue;
			any = true;
			if (State(p.Name, r.Id) != kOff) return true;
		}
		return !any;   // (no building of its station: the camp's improvised one, which nobody forbids)
	}

	bool RecipeChoices::Spot(const Tribe& tribe, const Recipe& r, const glm::vec2& from, glm::vec2& at) const
	{
		if ((m_Choices.empty() && tribe.m_Modules.Empty()) || r.Station.empty() || !tribe.m_Entities) return false;
		bool found = false;
		float best = 0.0f;
		for (const Tribe::Placed& p : tribe.Placements())
		{
			if (!p.State.Done || p.Def->Station != r.Station || State(p.Name, r.Id) == kOff) continue;
			glm::vec2 spot;
			if (!tribe.m_Modules.UseSpot(tribe, p.Name, spot))
			{
				const Entity* e = tribe.m_Entities->Find(tribe.m_Entities->FindByName(p.Name));
				spot = SiteFrontTurned(p.Pos, *p.Def, e ? e->Transform.Rotation.y : 0.0f);
			}
			const float d = glm::distance(from, spot);
			if (!found || d < best) { found = true; best = d; at = spot; }
		}
		return found;
	}

	std::string RecipeChoices::ToJson() const
	{
		Json j = Json::object();
		for (const auto& [b, list] : m_Choices) j[b] = list;
		return j.dump();
	}

	bool RecipeChoices::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		std::map<std::string, std::map<std::string, int>> choices;
		for (const auto& [b, list] : j.items())
		{
			if (!list.is_object()) return false;
			for (const auto& [r, s] : list.items())
			{
				if (!s.is_number_integer() || (s.get<int>() != kOff && s.get<int>() < 1)) return false;
				choices[b][r] = s.get<int>();
			}
		}
		m_Choices = std::move(choices);
		return true;
	}
}
