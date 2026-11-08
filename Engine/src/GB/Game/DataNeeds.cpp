#include "GB/Game/DataNeeds.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <tuple>

namespace GB
{
	using Json = nlohmann::json;

	const NeedDef* NeedBook::Find(const std::string& id) const
	{
		for (const NeedDef& n : Needs) if (n.Id == id) return &n;
		return nullptr;
	}

	const NeedDef* NeedBook::ByVerb(const std::string& verb) const
	{
		if (verb.empty()) return nullptr;
		for (const NeedDef& n : Needs) if (n.Verb == verb) return &n;
		return nullptr;
	}

	bool ReservedVerb(const std::string& verb)
	{
		// the kinds of option Tribe::Decide and its lanes already build (Execute and Finish tell them apart by this name)
		static const char* const kTaken[] = { "eat", "drink", "sleep", "warm", "regroup", "socialize", "tool", "craft", "gather", "approach", "deposit", "fire", "stoke",
			"explore", "experiment", "rest", "build", "haul", "make", "farm", "hunt", "recover", "care", "talk", "rite", "snare", "gear", "expedition", "migrate", "pave",
			"envoy", "raid", "defend", "flee", "fight", "player" };
		return std::any_of(std::begin(kTaken), std::end(kTaken), [&](const char* k) { return verb == k; });
	}

	std::string LoadNeeds(const std::string& json, NeedBook& book)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto it = j.find("needs");
		if (it == j.end()) return {};
		if (!it->is_array()) return "needs: expected a list";
		NeedBook out = book;
		std::vector<std::string> read;
		for (const Json& o : *it)
		{
			if (!o.is_object()) return "needs: every need is an object";
			NeedDef n;
			n.Id = o.value("id", std::string());
			if (n.Id.empty()) return "needs: a need has no id";
			if (std::find(read.begin(), read.end(), n.Id) != read.end()) return "needs: two needs called " + n.Id;
			read.push_back(n.Id);
			n.Name = o.value("name", n.Id);
			n.Era = o.value("era", std::string());
			n.Verb = o.value("verb", std::string());
			n.Provision = o.value("provision", std::string());
			n.Item = o.value("item", std::string());
			for (const auto& [key, field, lo, hi] : { std::tuple{ "decayPerDay", &n.DecayPerDay, 0.0f, 10000.0f }, std::tuple{ "threshold", &n.Threshold, 0.0f, 100.0f },
				std::tuple{ "weight", &n.Weight, 0.0f, 10.0f }, std::tuple{ "seconds", &n.Seconds, 0.1f, 600.0f }, std::tuple{ "amount", &n.Amount, 0.0f, 100.0f } })
			{
				const auto v = o.find(key);
				if (v == o.end()) continue;
				if (!v->is_number() || v->get<float>() < lo || v->get<float>() > hi) return "need " + n.Id + ": " + key + " must be a number from " + std::to_string(static_cast<int>(lo)) + " to " + std::to_string(static_cast<int>(hi));
				*field = v->get<float>();
			}
			for (const auto& [key, v] : o.items())
				if (key != "id" && key != "name" && key != "era" && key != "verb" && key != "provision" && key != "item" && key != "decayPerDay" && key != "threshold" && key != "weight" && key != "seconds" && key != "amount")
					return "need " + n.Id + " has no " + key;
			if (n.Verb.empty()) return "need " + n.Id + ": it has no verb (what a member does about it)";
			if (ReservedVerb(n.Verb)) return "need " + n.Id + ": the verb " + n.Verb + " is one the engine already uses";
			if (n.Provision.empty() && n.Item.empty()) return "need " + n.Id + ": it needs a provision (a building that gives it) or an item";
			const auto same = std::find_if(out.Needs.begin(), out.Needs.end(), [&](const NeedDef& x) { return x.Id == n.Id; });
			if (same != out.Needs.end()) *same = n;
			else out.Needs.push_back(n);
		}
		for (const NeedDef& a : out.Needs)
			for (const NeedDef& b : out.Needs)
				if (&a != &b && a.Verb == b.Verb) return "needs " + a.Id + " and " + b.Id + " have the same verb " + a.Verb;
		book = std::move(out);
		return {};
	}

	std::string NeedsToJson(const NeedBook& book)
	{
		Json list = Json::array();
		for (const NeedDef& n : book.Needs)
		{
			Json o = { { "id", n.Id }, { "name", n.Name }, { "verb", n.Verb }, { "decayPerDay", n.DecayPerDay }, { "threshold", n.Threshold }, { "weight", n.Weight },
				{ "seconds", n.Seconds }, { "amount", n.Amount } };
			if (!n.Era.empty()) o["era"] = n.Era;
			if (!n.Provision.empty()) o["provision"] = n.Provision;
			if (!n.Item.empty()) o["item"] = n.Item;
			list.push_back(o);
		}
		return Json{ { "needs", list } }.dump(2);
	}
}
