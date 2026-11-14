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

	float PrereqFactor(const TechTree& tree, const std::string& tech, const std::set<std::string>& known)
	{
		const TechDef* t = tree.Find(tech);
		if (!t) return 1.0f;
		float f = 1.0f;
		for (const std::string& r : t->Requires) f *= known.count(r) ? tree.Rules.PrereqBonus : tree.Rules.MissingPrereqFactor;
		return f;
	}

	float Complexity(const TechTree& tree, const std::string& tech)
	{
		const TechDef* t = tree.Find(tech);
		if (!t) return 0.0f;
		return t->Complexity >= 0.0f ? t->Complexity : tree.Rules.DefaultComplexity;
	}

	float GraspFactor(const TechTree& tree, const std::string& tech, float intellect)
	{
		const float gap = std::max(0.0f, Complexity(tree, tech) - intellect);
		return std::max(tree.Rules.GraspFloor, 1.0f - tree.Rules.GraspSlope * gap);
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
		// T181: "use" of the knowledge rules (learning by using a material; UseLearning.h)
		std::string ReadUse(const Json& o, UseRules& u)
		{
			if (!o.is_object()) return "use must be an object";
			for (const auto& [key, v] : o.items())
				if (key != "materials" && key != "effect" && key != "pull" && key != "enough" && key != "near" && key != "damp" && key != "basics" && key != "paceMin" && key != "paceMax" && key != "fireStation") return "use has no " + key;
			if (const auto m = o.find("materials"); m != o.end())
			{
				if (!m->is_object()) return "use: materials must be an object of item -> { tech: rate }";
				for (const auto& [item, techs] : m->items())
				{
					if (!techs.is_object()) return "use: materials of " + item + " must be an object of tech -> rate";
					for (const auto& [tech, rate] : techs.items())
					{
						if (!rate.is_number() || rate.get<float>() < 0.0f || rate.get<float>() > 1.0f) return "use: the rate of " + item + " -> " + tech + " must be a number from 0 to 1";
						u.Materials[item][tech] = rate.get<float>();
					}
				}
			}
			if (const auto e = o.find("effect"); e != o.end())
			{
				if (!e->is_string()) return "use: effect must be an option key";
				u.Effect = e->get<std::string>();
			}
			if (const auto f = o.find("fireStation"); f != o.end())
			{
				if (!f->is_string()) return "use: fireStation must be a station id (\"\" = none)";
				u.FireStation = f->get<std::string>();
			}
			for (std::string error : { Number(o, "pull", u.Pull, 0.0f, 10.0f), Number(o, "enough", u.Enough, 0.0f, 100000.0f, true), Number(o, "near", u.Near, 0.0f, 100000.0f, true),
				Number(o, "damp", u.Damp, 0.0f, 1.0f), Number(o, "paceMin", u.PaceMin, 0.0f, 10.0f), Number(o, "paceMax", u.PaceMax, 0.0f, 10.0f) })
				if (!error.empty()) return "use: " + error;
			if (u.PaceMin > u.PaceMax) return "use: paceMin is above paceMax";
			if (const auto b = o.find("basics"); b != o.end())
			{
				if (!b->is_object()) return "use: basics must be an object";
				for (const auto& [key, v] : b->items())
					if (key != "food" && key != "fire" && key != "beds") return "use: basics has no " + key;
				for (std::string error : { Number(*b, "food", u.BasicsFood, 0.0f, 1000.0f), Number(*b, "beds", u.BasicsBeds, 0.0f, 10.0f) })
					if (!error.empty()) return "use: basics " + error;
				if (const auto f = b->find("fire"); f != b->end())
				{
					if (!f->is_boolean()) return "use: basics fire must be true or false";
					u.BasicsFire = f->get<bool>();
				}
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
				if (o.contains("era")) return "tech '" + t.Id + "': there are no eras (T181): what it needs goes in requires and physical";
				if (const auto ph = o.find("physical"); ph != o.end())
				{
					if (!ph->is_object()) return "tech '" + t.Id + "': physical must be an object";
					for (const auto& [key, v] : ph->items())
						if (key != "materials" && key != "stations") return "tech '" + t.Id + "': physical has no " + key;
					for (std::string error : { Names(*ph, "materials", t.Physical.Materials), Names(*ph, "stations", t.Physical.Stations) })
						if (!error.empty()) return "tech '" + t.Id + "': physical " + error;
				}
				if (const auto k = o.find("known"); k != o.end())
				{
					if (!k->is_boolean()) return "tech '" + t.Id + "': known must be true or false";
					t.Known = k->get<bool>();
				}
				for (std::string error : { Names(o, "requires", t.Requires), Names(o, "recipes", t.Recipes), Names(o, "nodes", t.Nodes), Names(o, "buildings", t.Buildings) })
					if (!error.empty()) return "tech '" + t.Id + "': " + error;
				if (std::find(t.Requires.begin(), t.Requires.end(), t.Id) != t.Requires.end()) return "tech '" + t.Id + "' requires itself";
				if (std::string error = Number(o, "complexity", t.Complexity, 0.0f, 1.0f); !error.empty()) return "tech '" + t.Id + "': " + error;
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
				Number(*it, "growth", r.Growth, 0.0f, 100.0f), Number(*it, "experimentProgress", r.ExperimentProgress, 0.0f, 100.0f),
				Number(*it, "prereqBonus", r.PrereqBonus, 0.0f, 10.0f), Number(*it, "missingPrereqFactor", r.MissingPrereqFactor, 0.0f, 1.0f),
				Number(*it, "leapYears", r.LeapYears, 0.0f, 100000.0f), Number(*it, "defaultComplexity", r.DefaultComplexity, 0.0f, 1.0f),
				Number(*it, "graspFloor", r.GraspFloor, 0.0f, 1.0f), Number(*it, "graspSlope", r.GraspSlope, 0.0f, 100.0f) })
				if (!error.empty()) return "knowledge: " + error;
			if (const auto g = it->find("intellectGene"); g != it->end())
			{
				if (!g->is_string()) return "knowledge: intellectGene must be the id of a gene (\"\" = none)";
				r.IntellectGene = g->get<std::string>();
			}
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
			if (it->contains("eras")) return "knowledge: there are no eras (T181)";
			if (const auto u = it->find("use"); u != it->end())
				if (std::string error = ReadUse(*u, r.Use); !error.empty()) return "knowledge: " + error;
			rules = r;
		}
		if (j.contains("eras")) return "eras: there are no eras (T181)";
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
			Json o = { { "id", t.Id }, { "name", t.Name }, { "requires", t.Requires }, { "recipes", t.Recipes } };
			if (t.Known) o["known"] = true;
			if (t.Complexity >= 0.0f) o["complexity"] = Round3(t.Complexity);
			if (!t.Physical.Empty()) o["physical"] = { { "materials", t.Physical.Materials }, { "stations", t.Physical.Stations } };
			if (!t.Nodes.empty()) o["nodes"] = t.Nodes;
			if (!t.Buildings.empty()) o["buildings"] = t.Buildings;
			if (!(t.Discovery == none))
				o["discovery"] = { { "experiment", Round3(t.Discovery.Experiment) }, { "observe", Round3(t.Discovery.Observe) }, { "teach", Round3(t.Discovery.Teach) },
					{ "seconds", Round3(t.Discovery.Seconds) }, { "waste", Round3(t.Discovery.Waste) } };
			techs.push_back(o);
		}
		const KnowledgeRules& r = tree.Rules;
		Json out = { { "techs", techs }, { "knowledge", { { "lessonSeconds", Round3(r.LessonSeconds) }, { "observeRadius", Round3(r.ObserveRadius) }, { "growth", Round3(r.Growth) }, { "experimentProgress", Round3(r.ExperimentProgress) }, { "seed", r.Seed }, { "progression", r.Progression },
			{ "prereqBonus", Round3(r.PrereqBonus) }, { "missingPrereqFactor", Round3(r.MissingPrereqFactor) },
			{ "intellectGene", r.IntellectGene }, { "defaultComplexity", Round3(r.DefaultComplexity) }, { "graspFloor", Round3(r.GraspFloor) }, { "graspSlope", Round3(r.GraspSlope) } } } };
		const UseRules& u = r.Use;
		Json materials = Json::object();
		for (const auto& [item, leads] : u.Materials)
			for (const auto& [tech, rate] : leads) materials[item][tech] = Round3(rate);
		out["knowledge"]["use"] = { { "materials", materials }, { "effect", u.Effect }, { "pull", Round3(u.Pull) }, { "enough", Round3(u.Enough) }, { "near", Round3(u.Near) },
			{ "damp", Round3(u.Damp) }, { "basics", { { "food", Round3(u.BasicsFood) }, { "fire", u.BasicsFire }, { "beds", Round3(u.BasicsBeds) } } }, { "paceMin", Round3(u.PaceMin) }, { "paceMax", Round3(u.PaceMax) }, { "fireStation", u.FireStation } };
		if (r.LeapYears > 0.0f) out["knowledge"]["leapYears"] = Round3(r.LeapYears);
		return out.dump(2);
	}
}
