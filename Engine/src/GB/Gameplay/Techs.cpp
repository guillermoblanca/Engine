#include "GB/Gameplay/Techs.h"
#include "GB/Gameplay/ShippedContent.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	const TechDef* TechTree::Find(const std::string& id) const
	{
		const auto it = Techs.find(id);
		return it == Techs.end() ? nullptr : &it->second;
	}

	std::vector<std::string> TechTree::ForRecipe(const std::string& recipe) const
	{
		std::vector<std::string> out;
		for (const auto& [id, t] : Techs)
			if (std::find(t.Recipes.begin(), t.Recipes.end(), recipe) != t.Recipes.end()) out.push_back(id);
		return out;
	}

	std::vector<std::string> TechTree::ForNode(const std::string& node) const
	{
		std::vector<std::string> out;
		for (const auto& [id, t] : Techs)
			if (std::find(t.Nodes.begin(), t.Nodes.end(), node) != t.Nodes.end()) out.push_back(id);
		return out;
	}

	std::vector<std::string> TechTree::Starting() const
	{
		std::vector<std::string> out;
		for (const auto& [id, t] : Techs) if (t.Known) out.push_back(id);
		return out;
	}

	TechTree DefaultTechs()
	{
		return Shipped().Techs;   // what the data says ("techs" and "knowledge" of the game's pack)
	}

	namespace
	{
		double Round3(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }

		std::string Names(const Json& o, const char* key, std::vector<std::string>& out)
		{
			const auto it = o.find(key);
			if (it == o.end()) return {};
			if (!it->is_array()) return std::string(key) + " must be a list of ids";
			out.clear();
			for (const Json& x : *it)
			{
				if (!x.is_string() || x.get<std::string>().empty()) return std::string(key) + " must be a list of ids";
				if (std::find(out.begin(), out.end(), x.get<std::string>()) == out.end()) out.push_back(x.get<std::string>());
			}
			return {};
		}

		// A number of `o` into `out` when present: "" or what is wrong (a text where a number goes, out of [lo, hi], or not above lo when `open`).
		std::string Number(const Json& o, const char* key, float& out, float lo, float hi, bool open = false)
		{
			const auto it = o.find(key);
			if (it == o.end()) return {};
			if (!it->is_number()) return std::string(key) + " must be a number";
			const float v = it->get<float>();
			if (!std::isfinite(v) || v < lo || v > hi || (open && v <= lo)) return std::string(key) + " is out of range";
			out = v;
			return {};
		}
	}

	namespace
	{
		// The eras of a list: into `eras`, which may already hold some (merge: the same id is replaced; otherwise two of one id are a mistake)
		std::string ReadEras(const Json& list, std::vector<EraDef>& eras, bool merge)
		{
			if (!list.is_array()) return "eras must be a list";
			auto ids = [](const Json& o, const char* key, std::vector<std::string>& out)
			{
				if (const auto l = o.find(key); l != o.end())
				{
					if (!l->is_array()) return false;
					for (const Json& s : *l) { if (!s.is_string()) return false; out.push_back(s.get<std::string>()); }
				}
				return true;
			};
			std::vector<std::string> read;
			for (const Json& o : list)
			{
				if (!o.is_object()) return "an era must be an object";
				EraDef era;
				era.Id = o.value("id", std::string());
				era.Name = o.value("name", era.Id);
				era.Pull = o.value("pull", 0.5f);
				era.Leap = o.value("leap", 0.0f);
				if (era.Id.empty()) return "an era has no id";
				if (!ids(o, "key", era.Key) || !ids(o, "from", era.From) || !ids(o, "progression", era.Progression) || !ids(o, "interest", era.Interest))
					return "era " + era.Id + ": key, from, progression and interest are lists of ids";
				if (std::find(read.begin(), read.end(), era.Id) != read.end()) return "two eras called " + era.Id;
				read.push_back(era.Id);
				const auto same = std::find_if(eras.begin(), eras.end(), [&](const EraDef& e) { return e.Id == era.Id; });
				if (same == eras.end() && merge)
				{
					// a new era goes right after the last of the eras it follows (the order says which era a clan that knows the keys of two is in:
					// the bronze after the copper age, not after the middle ages)
					auto at = eras.begin();
					for (auto e = eras.begin(); e != eras.end(); ++e)
						if (std::find(era.From.begin(), era.From.end(), e->Id) != era.From.end()) at = e + 1;
					if (at == eras.begin()) at = eras.end();   // (it follows none of them: at the end)
					eras.insert(at, std::move(era));
				}
				else if (same == eras.end()) eras.push_back(std::move(era));
				else if (merge) *same = std::move(era);
				else return "two eras called " + era.Id;
			}
			return {};
		}
	}

	std::string LoadTechs(const std::string& text, TechTree& tree)
	{
		const Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		std::vector<TechDef> read;
		KnowledgeRules rules = tree.Rules;
		if (const auto it = j.find("techs"); it != j.end())
		{
			if (!it->is_array()) return "techs: expected a list of technologies";
			for (const Json& o : *it)
			{
				if (!o.is_object()) return "techs: every technology is an object";
				TechDef t;
				if (const auto id = o.find("id"); id != o.end() && id->is_string()) t.Id = id->get<std::string>();
				if (t.Id.empty()) return "a technology has no id";
				t.Name = t.Id;
				if (const auto n = o.find("name"); n != o.end())
				{
					if (!n->is_string()) return "tech '" + t.Id + "': name must be a text";
					t.Name = n->get<std::string>();
				}
				if (const auto e = o.find("era"); e != o.end())
				{
					if (!e->is_number_integer() || e->get<long long>() < 0 || e->get<long long>() > 100) return "tech '" + t.Id + "': era must be a whole number from 0 to 100";
					t.Era = e->get<int>();
				}
				if (const auto k = o.find("known"); k != o.end())
				{
					if (!k->is_boolean()) return "tech '" + t.Id + "': known must be true or false";
					t.Known = k->get<bool>();
				}
				for (std::string error : { Names(o, "requires", t.Requires), Names(o, "recipes", t.Recipes), Names(o, "nodes", t.Nodes), Names(o, "buildings", t.Buildings) })
					if (!error.empty()) return "tech '" + t.Id + "': " + error;
				if (std::find(t.Requires.begin(), t.Requires.end(), t.Id) != t.Requires.end()) return "tech '" + t.Id + "' requires itself";
				if (const auto d = o.find("discovery"); d != o.end())
				{
					if (!d->is_object()) return "tech '" + t.Id + "': discovery must be an object";
					for (const auto& [key, v] : d->items())
						if (key != "experiment" && key != "observe" && key != "teach" && key != "seconds" && key != "waste") return "tech '" + t.Id + "': discovery has no " + key;
					TechDiscovery& x = t.Discovery;
					for (std::string error : { Number(*d, "experiment", x.Experiment, 0.0f, 1.0f), Number(*d, "observe", x.Observe, 0.0f, 1.0f), Number(*d, "teach", x.Teach, 0.0f, 1.0f),
						Number(*d, "seconds", x.Seconds, 0.0f, 86400.0f, true), Number(*d, "waste", x.Waste, 0.0f, 1.0f) })
						if (!error.empty()) return "tech '" + t.Id + "': discovery " + error;
				}
				read.push_back(std::move(t));
			}
		}
		if (const auto it = j.find("knowledge"); it != j.end())
		{
			if (!it->is_object()) return "knowledge: expected an object";
			KnowledgeRules r;
			for (std::string error : { Number(*it, "lessonSeconds", r.LessonSeconds, 0.0f, 86400.0f, true), Number(*it, "observeRadius", r.ObserveRadius, 0.0f, 1000.0f),
				Number(*it, "growth", r.Growth, 0.0f, 100.0f), Number(*it, "experimentProgress", r.ExperimentProgress, 0.0f, 100.0f) })
				if (!error.empty()) return "knowledge: " + error;
			if (const auto s = it->find("seed"); s != it->end())
			{
				if (!s->is_number_integer() || s->get<long long>() < 0 || s->get<long long>() > 0xffffffffLL) return "knowledge: seed must be a whole number of 0 or more";
				r.Seed = static_cast<uint32_t>(s->get<long long>());
			}
			if (const auto p = it->find("progression"); p != it->end())
			{
				if (!p->is_array()) return "knowledge: progression must be a list of technology ids";
				for (const Json& id : *p)
				{
					if (!id.is_string() || id.get<std::string>().empty()) return "knowledge: progression must be a list of technology ids";
					r.Progression.push_back(id.get<std::string>());
				}
			}
			if (const auto e = it->find("eras"); e != it->end())   // N6
				if (std::string error = ReadEras(*e, r.Eras, false); !error.empty()) return "knowledge: " + error;
			rules = r;
		}
		// V5: "eras" at the top of a file adds eras to those already read (or replaces the one of the same id): a later pack opens a new era without
		// repeating the knowledge rules of the first
		if (const auto e = j.find("eras"); e != j.end())
			if (std::string error = ReadEras(*e, rules.Eras, true); !error.empty()) return "eras: " + error;
		for (TechDef& t : read) tree.Techs[t.Id] = std::move(t);
		tree.Rules = rules;
		return {};
	}

	std::string TechsToJson(const TechTree& tree)
	{
		Json techs = Json::array();
		const TechDiscovery none;
		for (const auto& [id, t] : tree.Techs)
		{
			Json o = { { "id", t.Id }, { "name", t.Name }, { "era", t.Era }, { "requires", t.Requires }, { "recipes", t.Recipes } };
			if (t.Known) o["known"] = true;
			if (!t.Nodes.empty()) o["nodes"] = t.Nodes;
			if (!t.Buildings.empty()) o["buildings"] = t.Buildings;
			if (!(t.Discovery == none))
				o["discovery"] = { { "experiment", Round3(t.Discovery.Experiment) }, { "observe", Round3(t.Discovery.Observe) }, { "teach", Round3(t.Discovery.Teach) },
					{ "seconds", Round3(t.Discovery.Seconds) }, { "waste", Round3(t.Discovery.Waste) } };
			techs.push_back(o);
		}
		const KnowledgeRules& r = tree.Rules;
		Json out = { { "techs", techs }, { "knowledge", { { "lessonSeconds", Round3(r.LessonSeconds) }, { "observeRadius", Round3(r.ObserveRadius) }, { "growth", Round3(r.Growth) }, { "experimentProgress", Round3(r.ExperimentProgress) }, { "seed", r.Seed }, { "progression", r.Progression } } } };
		if (!r.Eras.empty())
		{
			Json eras = Json::array();
			for (const EraDef& e : r.Eras)
				eras.push_back({ { "id", e.Id }, { "name", e.Name }, { "key", e.Key }, { "from", e.From }, { "progression", e.Progression }, { "interest", e.Interest }, { "pull", Round3(e.Pull) }, { "leap", Round3(e.Leap) } });
			out["knowledge"]["eras"] = eras;
		}
		return out.dump(2);
	}
}
