// T310: the "founding" block of assets/data/colony.json (see FoundingRules.h).
#include "GB/Game/FoundingRules.h"
#include "GB/Gameplay/Items.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB
{
	using OJson = nlohmann::ordered_json;   // (the kit keeps the order of the file: the store is filled in that order)

	FoundingRules DefaultFoundingRules()
	{
		FoundingRules r;
		r.Trades = { { "strength", "woodcutter" }, { "dexterity", "crafter" }, { "perception", "forager" }, { "endurance", "builder" } };
		r.Base = { { "wood", 2, 2 }, { "stone", 0, 3 } };
		r.Extra[1] = { { "wood", 4 }, { "stone", 3 }, { "dried_berry", 12 }, { "stone_axe", 1 }, { "cordage", 2 }, { "flint", 4 }, { "hide", 2 }, { "herbs", 3 } };
		r.Extra[2] = { { "wood", 3 }, { "stone", 2 }, { "dried_berry", 10 }, { "stone_axe", 1 }, { "cordage", 2 }, { "flint", 3 }, { "hide", 1 }, { "herbs", 2 } };
		r.Extra[3] = { { "wood", 2 }, { "stone", 1 }, { "dried_berry", 8 }, { "stone_axe", 1 }, { "cordage", 1 }, { "flint", 2 }, { "herbs", 1 } };
		r.Extra[4] = { { "wood", 1 }, { "dried_berry", 6 }, { "cordage", 1 }, { "flint", 1 } };
		r.Extra[5] = { { "dried_berry", 3 } };
		return r;
	}

	namespace
	{
		bool Int(const OJson& j, const char* key, int& out, int lo, int hi, std::string& error, const std::string& where)
		{
			const auto it = j.find(key);
			if (it == j.end()) return true;
			if (!it->is_number_integer() || it->get<int>() < lo || it->get<int>() > hi) { error = where + "." + key + ": expected a whole number from " + std::to_string(lo) + " to " + std::to_string(hi); return false; }
			out = it->get<int>();
			return true;
		}
		bool Num(const OJson& j, const char* key, float& out, float lo, float hi, std::string& error, const std::string& where)
		{
			const auto it = j.find(key);
			if (it == j.end()) return true;
			if (!it->is_number() || it->get<float>() < lo || it->get<float>() > hi) { error = where + "." + key + ": expected a number from " + std::to_string(static_cast<int>(lo)) + " to " + std::to_string(static_cast<int>(hi)); return false; }
			out = it->get<float>();
			return true;
		}
		bool Items(const OJson& j, std::vector<std::pair<std::string, int>>& out, std::string& error, const std::string& where)
		{
			if (!j.is_object()) { error = where + ": expected an object of item -> count"; return false; }
			out.clear();
			for (const auto& [item, n] : j.items())
			{
				if (item.empty() || !n.is_number_integer() || n.get<int>() < 0 || n.get<int>() > 1000) { error = where + "." + item + ": expected a count from 0 to 1000"; return false; }
				out.emplace_back(item, n.get<int>());
			}
			return true;
		}
	}

	std::string LoadFoundingRules(const std::string& json, FoundingRules& live)
	{
		const OJson doc = OJson::parse(json, nullptr, false);
		if (doc.is_discarded() || !doc.is_object()) return "the content is not a JSON object";
		const auto section = doc.find("founding");
		if (section == doc.end()) return {};
		const OJson& f = *section;
		if (!f.is_object()) return "founding: expected an object";
		FoundingRules r = live;
		std::string error;
		if (!Int(f, "candidates", r.Candidates, 1, 32, error, "founding")) return error;
		if (const auto p = f.find("party"); p != f.end())
		{
			if (!p->is_object()) return "founding.party: expected an object";
			if (!Int(*p, "min", r.PartyMin, 1, 40, error, "founding.party") || !Int(*p, "max", r.PartyMax, 1, 40, error, "founding.party") || !Int(*p, "group", r.Group, 1, 40, error, "founding.party")) return error;
		}
		if (r.PartyMax < r.PartyMin) return "founding.party: max is below min";
		if (r.Group < r.PartyMin || r.Group > r.PartyMax) return "founding.party.group: outside min..max";
		if (r.PartyMax > r.Candidates) return "founding.party.max: more than the candidates";
		if (const auto s = f.find("startSkill"); s != f.end())
		{
			if (!s->is_object()) return "founding.startSkill: expected an object";
			if (!Num(*s, "min", r.SkillMin, 0.0f, 100.0f, error, "founding.startSkill") || !Num(*s, "max", r.SkillMax, 0.0f, 100.0f, error, "founding.startSkill")) return error;
			if (const auto n = s->find("neighbours"); n != s->end())
			{
				if (!n->is_boolean()) return "founding.startSkill.neighbours: expected true or false";
				r.SkillNeighbours = n->get<bool>();
			}
		}
		if (r.SkillMax < r.SkillMin) return "founding.startSkill: max is below min";
		if (const auto t = f.find("trades"); t != f.end())
		{
			if (!t->is_object()) return "founding.trades: expected an object of gene -> role";
			r.Trades.clear();
			for (const auto& [gene, role] : t->items())
			{
				if (!role.is_string() || role.get<std::string>().empty()) return "founding.trades." + gene + ": expected the id of a role";
				r.Trades[gene] = role.get<std::string>();
			}
		}
		if (const auto k = f.find("kit"); k != f.end())
		{
			if (!k->is_object()) return "founding.kit: expected an object";
			if (const auto b = k->find("base"); b != k->end())
			{
				if (!b->is_object()) return "founding.kit.base: expected an object of item -> { fixed, perMember }";
				r.Base.clear();
				for (const auto& [item, v] : b->items())
				{
					if (item.empty() || !v.is_object()) return "founding.kit.base." + item + ": expected { fixed, perMember }";
					FoundingRules::BaseItem bi;
					bi.Item = item;
					if (!Int(v, "fixed", bi.Fixed, 0, 1000, error, "founding.kit.base." + item) || !Int(v, "perMember", bi.PerMember, 0, 1000, error, "founding.kit.base." + item)) return error;
					r.Base.push_back(bi);
				}
			}
			if (const auto e = k->find("extra"); e != k->end())
			{
				if (!e->is_object()) return "founding.kit.extra: expected an object of people -> { item: count }";
				r.Extra.clear();
				for (const auto& [people, items] : e->items())
				{
					const int n = std::atoi(people.c_str());
					if (n < 1 || n > 40 || std::to_string(n) != people) return "founding.kit.extra." + people + ": expected a number of people from 1 to 40";
					if (!Items(items, r.Extra[n], error, "founding.kit.extra." + people)) return error;
				}
			}
		}
		live = std::move(r);
		return {};
	}

	std::string FoundingRulesToJson(const FoundingRules& r)
	{
		OJson f;
		f["candidates"] = r.Candidates;
		f["party"] = { { "min", r.PartyMin }, { "max", r.PartyMax }, { "group", r.Group } };
		f["startSkill"] = { { "min", r.SkillMin }, { "max", r.SkillMax }, { "neighbours", r.SkillNeighbours } };
		f["trades"] = OJson::object();
		for (const auto& [gene, role] : r.Trades) f["trades"][gene] = role;
		OJson base = OJson::object(), extra = OJson::object();
		for (const FoundingRules::BaseItem& b : r.Base) base[b.Item] = { { "fixed", b.Fixed }, { "perMember", b.PerMember } };
		for (const auto& [n, items] : r.Extra)
		{
			OJson e = OJson::object();
			for (const auto& [item, count] : items) e[item] = count;
			extra[std::to_string(n)] = e;
		}
		f["kit"] = { { "base", base }, { "extra", extra } };
		OJson doc;
		doc["founding"] = f;
		return doc.dump();
	}

	std::vector<std::pair<std::string, int>> FoundingKit(const FoundingRules& rules, int people, bool party)
	{
		std::vector<std::pair<std::string, int>> kit;
		auto add = [&kit](const std::string& item, int n)
		{
			if (n <= 0) return;
			const auto it = std::find_if(kit.begin(), kit.end(), [&](const auto& p) { return p.first == item; });
			if (it != kit.end()) it->second += n;
			else kit.emplace_back(item, n);
		};
		people = std::max(0, people);
		for (const FoundingRules::BaseItem& b : rules.Base) add(b.Item, b.Fixed + b.PerMember * people);
		if (party)
			if (const auto e = rules.Extra.find(people); e != rules.Extra.end())
				for (const auto& [item, n] : e->second) add(item, n);
		return kit;
	}

	std::string AddFoundingKit(Inventory& store, const ItemLibrary& items, const FoundingRules& rules, int people, bool party)
	{
		std::string missed;
		for (const auto& [item, n] : FoundingKit(rules, people, party))
			if (const int in = store.Add(items, item, n); in < n) missed += (missed.empty() ? "" : ", ") + item + " " + std::to_string(in) + " of " + std::to_string(n);
		return missed;
	}
}
