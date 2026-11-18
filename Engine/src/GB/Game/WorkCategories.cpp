// T306: the categories of work as data (see WorkCategories.h).
#include "GB/Game/WorkCategories.h"
#include <algorithm>
#include <set>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		// How well a pattern fits a piece of work: -1 not at all, 0 by its kind only, 1 + the length of its `what` when that fits too.
		int Fit(const std::string& pattern, const std::string& kind, const std::string& target, const std::string& item, const std::vector<std::string>& tags)
		{
			const size_t colon = pattern.find(':');
			if (pattern.compare(0, colon, kind) != 0 || (colon == std::string::npos ? pattern.size() : colon) != kind.size()) return -1;
			if (colon == std::string::npos) return 0;
			const std::string what = pattern.substr(colon + 1);
			bool fits = what == target || (!item.empty() && what == item);
			for (const std::string& t : tags) fits = fits || t == what || (t.size() > what.size() && t.compare(0, what.size(), what) == 0 && t[what.size()] == '.');
			return fits ? 1 + static_cast<int>(what.size()) : -1;
		}

		bool ValidPattern(const std::string& p)
		{
			if (p.empty() || p.front() == ':' || p.back() == ':') return false;
			return std::count(p.begin(), p.end(), ':') <= 1 && p.find(' ') == std::string::npos;
		}
	}

	const WorkCategoryDef* WorkCategories::Category(const std::string& id) const
	{
		for (const WorkCategoryDef& c : List) if (c.Id == id) return &c;
		return nullptr;
	}

	const WorkCategoryDef* WorkCategories::CategoryOfJob(const std::string& job) const
	{
		for (const WorkCategoryDef& c : List)
			for (const WorkJobDef& j : c.Jobs) if (j.Id == job) return &c;
		return nullptr;
	}

	std::string WorkCategories::JobOf(const std::string& rawKind, const std::string& target, const std::string& item, const std::vector<std::string>& tags) const
	{
		if (List.empty() || rawKind.empty()) return {};
		const auto alias = Aliases.find(rawKind);
		const std::string& kind = alias != Aliases.end() ? alias->second : rawKind;
		for (const std::string& p : Free)
			if (Fit(p, rawKind, target, item, tags) >= 0 || Fit(p, kind, target, item, tags) >= 0) return {};
		const WorkJobDef* best = nullptr;
		int bestFit = -1;
		for (const WorkCategoryDef& c : List)
			for (const WorkJobDef& j : c.Jobs)
				for (const std::string& p : j.Match)
					if (const int f = Fit(p, kind, target, item, tags); f > bestFit) { bestFit = f; best = &j; }
		return best ? best->Id : std::string();
	}

	std::string LoadWorkCategories(const std::string& json, WorkCategories& out)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "work: categories must be an object";
		for (const auto& [key, v] : j.items())
			if (key != "list" && key != "aliases" && key != "free") return "work: categories has no " + key;
		WorkCategories c;
		std::set<std::string> ids;
		auto number = [](const Json& v, int& n) { if (!v.is_number_integer() || v.get<int>() < 0 || v.get<int>() > 10) return false; n = v.get<int>(); return true; };
		if (const auto list = j.find("list"); list != j.end())
		{
			if (!list->is_array()) return "work: categories list must be an array";
			for (const Json& cat : *list)
			{
				if (!cat.is_object() || !cat.contains("id") || !cat["id"].is_string() || cat["id"].get<std::string>().empty()) return "work: every category needs an id";
				WorkCategoryDef def;
				def.Id = cat["id"].get<std::string>();
				if (!ids.insert(def.Id).second) return "work: the category or job " + def.Id + " is twice";
				for (const auto& [key, v] : cat.items())
					if (key != "id" && key != "jobs" && key != "default" && key != "suggest") return "work: category " + def.Id + " has no " + key;
				if (const auto d = cat.find("default"); d != cat.end() && !number(*d, def.Default)) return "work: category " + def.Id + " default must be 0 (\"-\") .. 10";
				if (const auto s = cat.find("suggest"); s != cat.end())
				{
					if (!s->is_object()) return "work: category " + def.Id + " suggest must be an object of role -> number";
					for (const auto& [role, n] : s->items())
					{
						int v = 0;
						if (!number(n, v)) return "work: category " + def.Id + " suggest " + role + " must be 0 (\"-\") .. 10";
						def.Suggest[role] = v;
					}
				}
				const auto jobs = cat.find("jobs");
				if (jobs == cat.end() || !jobs->is_array() || jobs->empty()) return "work: category " + def.Id + " needs a list of jobs";
				for (const Json& job : *jobs)
				{
					if (!job.is_object() || !job.contains("id") || !job["id"].is_string() || job["id"].get<std::string>().empty()) return "work: every job of " + def.Id + " needs an id";
					WorkJobDef jd;
					jd.Id = job["id"].get<std::string>();
					if (!ids.insert(jd.Id).second) return "work: the category or job " + jd.Id + " is twice";
					for (const auto& [key, v] : job.items())
						if (key != "id" && key != "match") return "work: job " + jd.Id + " has no " + key;
					const auto match = job.find("match");
					if (match == job.end() || !match->is_array() || match->empty()) return "work: job " + jd.Id + " needs a list of patterns to match";
					for (const Json& p : *match)
					{
						if (!p.is_string() || !ValidPattern(p.get<std::string>())) return "work: job " + jd.Id + ": a pattern is \"kind\" or \"kind:what\"";
						jd.Match.push_back(p.get<std::string>());
					}
					def.Jobs.push_back(std::move(jd));
				}
				c.List.push_back(std::move(def));
			}
		}
		if (const auto a = j.find("aliases"); a != j.end())
		{
			if (!a->is_object()) return "work: categories aliases must be an object of kind -> kind";
			for (const auto& [kind, to] : a->items())
			{
				if (!to.is_string() || to.get<std::string>().empty()) return "work: categories alias " + kind + " must name a kind";
				c.Aliases[kind] = to.get<std::string>();
			}
		}
		if (const auto f = j.find("free"); f != j.end())
		{
			if (!f->is_array()) return "work: categories free must be an array of patterns";
			for (const Json& p : *f)
			{
				if (!p.is_string() || !ValidPattern(p.get<std::string>())) return "work: categories free: a pattern is \"kind\" or \"kind:what\"";
				c.Free.push_back(p.get<std::string>());
			}
		}
		out = std::move(c);
		return {};
	}

	std::string WorkCategoriesToJson(const WorkCategories& c)
	{
		Json list = Json::array();
		for (const WorkCategoryDef& def : c.List)
		{
			Json jobs = Json::array();
			for (const WorkJobDef& jd : def.Jobs) jobs.push_back(Json{ { "id", jd.Id }, { "match", jd.Match } });
			Json cat = Json{ { "id", def.Id }, { "default", def.Default }, { "jobs", jobs } };
			if (!def.Suggest.empty()) cat["suggest"] = def.Suggest;
			list.push_back(cat);
		}
		Json out = Json::object();
		out["list"] = list;
		out["aliases"] = c.Aliases;
		out["free"] = c.Free;
		return out.dump();
	}
}
