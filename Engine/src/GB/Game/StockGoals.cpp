// T307: the stock goals of the player's clan (see StockGoals.h).
#include "GB/Game/StockGoals.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Gameplay/GameplayTag.h"
#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		bool IsTag(const std::string& what) { return what.rfind("Item.", 0) == 0; }

		bool Tagged(const ItemDef& d, const std::string& tag)
		{
			for (const std::string& t : d.Tags) if (GameplayTag::TagMatches(tag, t)) return true;
			return false;
		}

		bool Carries(const ItemLibrary& items, const std::string& item, const std::string& what)
		{
			if (!IsTag(what)) return item == what;
			const ItemDef* d = items.Find(item);
			return d && Tagged(*d, what);
		}

		bool Gives(const HuntRule& h, const std::string& item)
		{
			if (h.Item == item || h.Byproduct == item) return true;
			for (const auto& [y, n] : h.Yields) if (y == item) return true;
			return false;
		}
	}

	StockGoals::Reply StockGoals::Set(const Tribe& tribe, const std::string& what, int n)
	{
		if (!tribe.m_Items) return { false, "the clan has no items" };
		if (n < 1 || n > kMost) return { false, "a stock goal is 1 to " + std::to_string(kMost) + " (or off)" };
		if (IsTag(what))
		{
			bool any = false;
			for (const std::string& id : tribe.m_Items->Ids()) any = any || Carries(*tribe.m_Items, id, what);
			if (!any) return { false, "no item carries the tag " + what };
		}
		else if (!tribe.m_Items->Find(what)) return { false, "there is no item called " + what };
		m_Goals[what] = n;
		return { true, "keep " + std::to_string(n) + " " + what + ": " + std::to_string(Have(tribe, what)) + " now, " + Status(tribe, what, n) };
	}

	StockGoals::Reply StockGoals::Off(const std::string& what)
	{
		if (!m_Goals.erase(what)) return { false, "there is no stock goal for " + what };
		return { true, "no stock goal for " + what + " now" };
	}

	int StockGoals::Have(const Tribe& tribe, const std::string& what)
	{
		if (!tribe.m_Items) return 0;
		const bool tag = IsTag(what);
		auto in = [&](const Inventory& inv) { return tag ? inv.CountTagged(*tribe.m_Items, what) : inv.Count(what); };
		int n = in(tribe.m_Store);
		for (const TribeMember& m : tribe.m_Members)
			if (!m.Dead && m.Body) n += in(m.Body->Bag());
		return n;
	}

	std::string StockGoals::ItemFor(const Tribe& tribe, const std::string& what)
	{
		if (!tribe.m_Items) return {};
		if (!IsTag(what)) return tribe.m_Items->Find(what) ? what : std::string();
		// a tag: what a known recipe makes where the clan can make it; else what it knows; else any recipe's; else a node's; else a hunt's
		if (tribe.m_Recipes)
		{
			const std::vector<std::string> ids = tribe.m_Recipes->Ids();
			for (int pass = 0; pass < 3; ++pass)
				for (const std::string& id : ids)
				{
					const Recipe* r = tribe.m_Recipes->Find(id);
					if (!r || !Carries(*tribe.m_Items, r->Output, what)) continue;
					if (pass < 2 && !tribe.ClanKnows(*r)) continue;
					if (pass == 0 && !tribe.StationAt(r->Station)) continue;
					return r->Output;
				}
		}
		for (const auto& [kind, def] : tribe.m_Defs) if (Carries(*tribe.m_Items, def.Item, what)) return def.Item;
		if (tribe.m_Combat)
			for (const auto& [id, h] : tribe.m_Combat->Rules.Hunts)
			{
				if (Carries(*tribe.m_Items, h.Item, what)) return h.Item;
				if (!h.Byproduct.empty() && Carries(*tribe.m_Items, h.Byproduct, what)) return h.Byproduct;
			}
		return {};
	}

	std::string StockGoals::Status(const Tribe& tribe, const std::string& what, int goal)
	{
		if (Have(tribe, what) >= goal) return "ok";
		const std::string item = ItemFor(tribe, what);
		if (item.empty() || !tribe.m_Items) return "no way to get it";
		auto fromNode = [&](const std::string& i) { for (const auto& [kind, def] : tribe.m_Defs) if (def.Item == i) return true; return false; };
		auto fromHunt = [&](const std::string& i)
		{
			if (!tribe.m_Combat) return false;
			for (const auto& [id, h] : tribe.m_Combat->Rules.Hunts) if (Gives(h, i)) return true;
			return false;
		};
		const std::vector<const Recipe*> makers = tribe.m_Recipes ? RecipesFor(*tribe.m_Recipes, item) : std::vector<const Recipe*>();
		if (!makers.empty())
		{
			const Recipe* use = nullptr;
			const Recipe* known = nullptr;
			for (const Recipe* r : makers)
			{
				if (!tribe.ClanKnows(*r)) continue;
				if (!known) known = r;
				if (tribe.StationAt(r->Station)) { use = r; break; }
			}
			if (!known) return "does not know how to make it";
			if (!use) return "no " + known->Station + " to make it at";
			for (const auto& [input, n] : use->Inputs)
			{
				if (tribe.m_Store.Count(input) >= n) continue;
				if (fromNode(input)) return "gathering " + input;
				if (fromHunt(input)) return "hunting for " + input;
				bool makes = false;
				for (const Recipe* r : RecipesFor(*tribe.m_Recipes, input)) makes = makes || (tribe.ClanKnows(*r) && tribe.StationAt(r->Station));
				if (makes) return "making " + input;
				return "lacks " + input;
			}
			return "making";
		}
		if (fromNode(item)) return "gathering";
		if (fromHunt(item))
		{
			for (const auto& [id, h] : tribe.m_Combat->Rules.Hunts)
				if (Gives(h, item))
					for (const Tribe::Animal& a : tribe.m_Animals)
						if (a.Tag == h.Tag && !a.Predator) return "hunting";
			return "no animal to hunt";
		}
		return "no way to get it";
	}

	std::string StockGoals::List(const Tribe& tribe) const
	{
		if (m_Goals.empty()) return "no stock goals";
		std::string s;
		for (const auto& [what, n] : m_Goals)
			s += (s.empty() ? "" : "; ") + what + " " + std::to_string(Have(tribe, what)) + " / " + std::to_string(n) + " " + Status(tribe, what, n);
		return s;
	}

	void StockGoals::Wants(const Tribe& tribe, std::vector<ProductionWant>& out) const
	{
		for (const auto& [what, n] : m_Goals)
		{
			const int lack = n - Have(tribe, what);
			if (lack <= 0) continue;
			const std::string item = ItemFor(tribe, what);
			if (item.empty()) continue;
			// the planner sets aside what the store holds first: it is asked for that and the lack on top
			out.push_back({ item, tribe.m_Store.Count(item) + lack, "you keep " + std::to_string(n) + " " + what });
		}
	}

	void StockGoals::Post(Tribe& tribe) const
	{
		if (m_Goals.empty() || !tribe.m_Combat || !tribe.m_Items) return;
		const JobRules& rules = tribe.Config.Work.Jobs;
		for (const auto& [what, n] : m_Goals)
		{
			const int lack = n - Have(tribe, what);
			const std::string item = lack > 0 ? ItemFor(tribe, what) : std::string();
			if (item.empty() || (tribe.m_Recipes && !RecipesFor(*tribe.m_Recipes, item).empty())) continue;
			bool node = false;
			for (const auto& [kind, def] : tribe.m_Defs) node = node || def.Item == item;
			if (node) continue;   // (the gathers bring it: the plan's raw shortage)
			for (const auto& [id, h] : tribe.m_Combat->Rules.Hunts)
			{
				if (!Gives(h, item)) continue;
				const Tribe::Animal* nearest = nullptr;
				float best = h.Range;
				for (const Tribe::Animal& a : tribe.m_Animals)
					if (a.Tag == h.Tag && !a.Predator && tribe.HuntAllowed(a.Pos)) { const float d = glm::distance(a.Pos, tribe.Config.Economy.Camp); if (d < best) { best = d; nearest = &a; } }
				if (!nearest) continue;
				const Job* posted = tribe.m_Board.Find("hunt " + h.Tag);
				const float base = std::max(h.Priority * 1.1f, posted ? posted->Base : 0.0f);
				char why[128];
				std::snprintf(why, sizeof why, "you keep %d %s; %s %.0f m from the camp", n, what.c_str(), nearest->Name.c_str(), static_cast<double>(best));
				tribe.m_Board.Post("hunt", h.Tag, "hunting", 1.0f, base, h.Hunters, why, tribe.m_Time, rules);
			}
		}
	}

	void StockGoals::Options(const Tribe& tribe, const TribeMember& m, std::vector<UtilityOption>& options) const
	{
		if (m_Goals.empty() || !tribe.m_Recipes) return;
		const Job* held = tribe.m_Board.ClaimOf(m.Name);
		if (!held || held->Kind != "make" || held->Why.find(": you keep ") == std::string::npos) return;   // (a make of another want goes on)
		const Recipe* r = tribe.m_Recipes->Find(held->Target);
		if (!r) return;
		for (const auto& [what, n] : m_Goals)
		{
			if (ItemFor(tribe, what) != r->Output || Have(tribe, what) < n) continue;
			for (UtilityOption& o : options)
				if (o.Kind == "make" && o.Target == held->Target)
				{
					o.Score = 0.0f;
					o.Factors.push_back({ "stock goal met", 0.0f });
				}
		}
	}

	std::string StockGoals::ToJson() const
	{
		return Json{ { "version", 1 }, { "goals", m_Goals } }.dump();
	}

	bool StockGoals::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || j.value("version", 0) != 1 || !j.contains("goals") || !j["goals"].is_object()) return false;
		std::map<std::string, int> goals;
		for (const auto& [what, n] : j["goals"].items())
		{
			if (what.empty() || !n.is_number_integer() || n.get<int>() < 1 || n.get<int>() > kMost) return false;
			goals[what] = n.get<int>();
		}
		m_Goals = std::move(goals);
		return true;
	}
}
