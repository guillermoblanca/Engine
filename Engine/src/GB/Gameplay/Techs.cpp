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

	std::vector<std::string> TechTree::ForVerb(const std::string& verb) const
	{
		std::vector<std::string> out;
		for (const auto& [id, t] : Techs)
			if (std::find(t.Verbs.begin(), t.Verbs.end(), verb) != t.Verbs.end()) out.push_back(id);
		return out;
	}

	std::vector<std::string> TechTree::Verbs() const
	{
		std::set<std::string> all;
		for (const auto& [id, t] : Techs) all.insert(t.Verbs.begin(), t.Verbs.end());
		return { all.begin(), all.end() };
	}

	// ------------------------------------------------------------------ T322: verbs
	namespace
	{
		// The kinds of work and orders of today, and the verbs to come (ARBOL_TECNICO.md §11.5), with their word for people ("" = the kind itself).
		struct VerbKind { const char* Kind; const char* Word; };
		constexpr VerbKind kVerbKinds[] = {
			// the work of the clan (the kinds of the job board and of the brain's options)
			{ "gather", "" }, { "craft", "" }, { "make", "" }, { "farm", "farm" }, { "haul", "" }, { "hunt", "" }, { "build", "" }, { "deliver", "" },
			{ "stoke", "" }, { "fire", "light a fire" }, { "designate", "gather what is marked" }, { "expedition", "go on an expedition" },
			// the player's direct orders (ColonyOrders.h)
			{ "goto", "go" }, { "use", "" }, { "talk", "" }, { "follow", "" },
			// the verbs to come
			{ "reap", "" }, { "transplant", "" }, { "transplant_tree", "transplant a tree" }, { "select_seed", "select seed" }, { "dig_ditch", "dig a ditch" },
			{ "tame", "" }, { "drive", "drive game" },
		};
		constexpr const char* kFarmActions[] = { "till", "sow", "tend", "harvest" };   // FarmZones: the actions of a field
	}

	const std::vector<std::string>& VerbKinds()
	{
		static const std::vector<std::string> kinds = [] { std::vector<std::string> v; for (const VerbKind& k : kVerbKinds) v.push_back(k.Kind); return v; }();
		return kinds;
	}

	std::string CheckVerb(const std::string& verb)
	{
		if (verb.empty()) return "a verb has no id";
		const size_t colon = verb.find(':');
		const std::string kind = verb.substr(0, colon), what = colon == std::string::npos ? std::string() : verb.substr(colon + 1);
		const std::vector<std::string>& kinds = VerbKinds();
		if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) return "'" + verb + "' is not a verb the game knows (a kind of work or order: farm:till, reap, tame...)";
		if (colon != std::string::npos && (what.empty() || what.find(':') != std::string::npos)) return "'" + verb + "' must be kind or kind:what";
		if (kind == "farm" && colon != std::string::npos && std::none_of(std::begin(kFarmActions), std::end(kFarmActions), [&](const char* a) { return what == a; }))
			return "'" + verb + "': a field's work is farm:till, farm:sow, farm:tend or farm:harvest";
		return {};
	}

	std::string VerbWord(const std::string& verb)
	{
		const size_t colon = verb.find(':');
		const std::string kind = verb.substr(0, colon), what = colon == std::string::npos ? std::string() : verb.substr(colon + 1);
		if (kind == "farm" && !what.empty()) return what == "tend" ? "water" : what;   // "till", "sow", "harvest"
		std::string word = kind;
		for (const VerbKind& k : kVerbKinds) if (kind == k.Kind && *k.Word) word = k.Word;
		return what.empty() ? word : word + " " + what;
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

	const std::vector<std::string>& TechBranches()
	{
		static const std::vector<std::string> branches = { "tools", "hunt", "field", "food", "crafts", "homes", "lore" };
		return branches;
	}

	const std::vector<std::string>& TechModifierKeys()
	{
		static const std::vector<std::string> keys = { "hunt", "heal", "grief", "mood", "night", "spoil", "carry" };
		return keys;
	}

	const std::vector<std::string>& TierGoalTypes()
	{
		static const std::vector<std::string> types = { "population", "building", "buildings", "techs", "fire", "seasons", "winters" };
		return types;
	}

	namespace
	{
		// T323: "tiers" of the knowledge rules: the steps in order, each one above the one before
		std::string ReadTiers(const Json& o, std::vector<TierStep>& out)
		{
			if (!o.is_array()) return "tiers must be a list of steps";
			int last = 0;
			for (const Json& x : o)
			{
				if (!x.is_object()) return "tiers: every step is an object";
				for (const auto& [key, v] : x.items())
					if (key != "tier" && key != "need" && key != "goals" && key != "_doc") return "tiers has no " + key;
				TierStep step;
				const auto t = x.find("tier");
				if (t == x.end() || !t->is_number_integer() || t->get<int>() < 1 || t->get<int>() > kMaxTier) return "tiers: tier must be a whole number from 1 to " + std::to_string(kMaxTier);
				step.Tier = t->get<int>();
				if (step.Tier != last + 1) return "tiers: the steps go in order from 1 (" + std::to_string(step.Tier) + " after " + std::to_string(last) + ")";
				last = step.Tier;
				const std::string w = "tiers: step " + std::to_string(step.Tier) + ": ";
				if (const auto n = x.find("need"); n != x.end())
				{
					if (!n->is_number_integer() || n->get<int>() < 0) return w + "need must be a whole number of 0 or more (0 = all)";
					step.Need = n->get<int>();
				}
				const auto g = x.find("goals");
				if (g == x.end() || !g->is_array() || g->empty()) return w + "goals must be a list of one goal or more";
				for (const Json& gj : *g)
				{
					if (!gj.is_object()) return w + "every goal is an object";
					for (const auto& [key, v] : gj.items())
						if (key != "type" && key != "building" && key != "count") return w + "a goal has no " + key;
					TierGoal goal;
					const auto ty = gj.find("type");
					if (ty == gj.end() || !ty->is_string()) return w + "a goal has no type";
					goal.Type = ty->get<std::string>();
					const auto& types = TierGoalTypes();
					if (std::find(types.begin(), types.end(), goal.Type) == types.end()) return w + "no goal type '" + goal.Type + "'";
					if (const auto b = gj.find("building"); b != gj.end())
					{
						if (!b->is_string() || b->get<std::string>().empty()) return w + "building must be a building id";
						goal.Building = b->get<std::string>();
					}
					if (goal.Type == "building" && goal.Building.empty()) return w + "a goal of type building needs its building";
					if (const auto c = gj.find("count"); c != gj.end())
					{
						if (!c->is_number_integer() || c->get<int>() < 0) return w + "count must be a whole number of 0 or more";
						goal.Count = c->get<int>();
					}
					step.Goals.push_back(goal);
				}
				if (step.Need > static_cast<int>(step.Goals.size())) return w + "need is above the number of goals";
				out.push_back(std::move(step));
			}
			return {};
		}

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
				if (const auto tier = o.find("tier"); tier != o.end())   // T323
				{
					if (!tier->is_number_integer() || tier->get<int>() < 0 || tier->get<int>() > kMaxTier) return "tech '" + t.Id + "': tier must be a whole number from 0 to " + std::to_string(kMaxTier);
					t.Tier = tier->get<int>();
				}
				if (const auto br = o.find("branch"); br != o.end())   // D77
				{
					const auto& branches = TechBranches();
					if (!br->is_string() || std::find(branches.begin(), branches.end(), br->get<std::string>()) == branches.end())
					{
						std::string list;
						for (const std::string& b : branches) list += (list.empty() ? "" : ", ") + b;
						return "tech '" + t.Id + "': branch must be one of " + list;
					}
					t.Branch = br->get<std::string>();
				}
				if (t.Tier >= 0 && t.Branch.empty()) return "tech '" + t.Id + "': a technology with a tier needs its branch";
				for (std::string error : { Names(o, "requires", t.Requires), Names(o, "recipes", t.Recipes), Names(o, "nodes", t.Nodes), Names(o, "buildings", t.Buildings) })
					if (!error.empty()) return "tech '" + t.Id + "': " + error;
				if (const auto v = o.find("verbs"); v != o.end())   // T322: checked here (a verb is no name of another section)
				{
					if (!v->is_array()) return "tech '" + t.Id + "': verbs must be a list of verbs";
					for (const Json& x : *v)
					{
						if (!x.is_string()) return "tech '" + t.Id + "': verbs must be a list of verbs";
						const std::string verb = x.get<std::string>();
						if (std::string error = CheckVerb(verb); !error.empty()) return "tech '" + t.Id + "': verbs: " + error;
						if (std::find(t.Verbs.begin(), t.Verbs.end(), verb) != t.Verbs.end()) return "tech '" + t.Id + "': the verb '" + verb + "' is twice in verbs";
						t.Verbs.push_back(verb);
					}
				}
				if (const auto mo = o.find("modifiers"); mo != o.end())   // T331: a known key and a factor above 0
				{
					if (!mo->is_object()) return "tech '" + t.Id + "': modifiers must be an object of key -> factor";
					const auto& keys = TechModifierKeys();
					for (const auto& [key, v] : mo->items())
					{
						if (std::find(keys.begin(), keys.end(), key) == keys.end())
						{
							std::string list;
							for (const std::string& k : keys) list += (list.empty() ? "" : ", ") + k;
							return "tech '" + t.Id + "': modifiers has no " + key + " (one of " + list + ")";
						}
						if (!v.is_number() || !std::isfinite(v.get<float>()) || v.get<float>() <= 0.0f || v.get<float>() > 10.0f) return "tech '" + t.Id + "': modifiers " + key + " must be a factor above 0 and up to 10";
						t.Modifiers[key] = v.get<float>();
					}
				}
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
			if (const auto ts = it->find("tiers"); ts != it->end())   // T323
				if (std::string error = ReadTiers(*ts, r.Tiers); !error.empty()) return "knowledge: " + error;
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
			if (t.Tier >= 0) o["tier"] = t.Tier;          // T323
			if (!t.Branch.empty()) o["branch"] = t.Branch;   // D77
			if (t.Complexity >= 0.0f) o["complexity"] = Round3(t.Complexity);
			if (!t.Physical.Empty()) o["physical"] = { { "materials", t.Physical.Materials }, { "stations", t.Physical.Stations } };
			if (!t.Nodes.empty()) o["nodes"] = t.Nodes;
			if (!t.Buildings.empty()) o["buildings"] = t.Buildings;
			if (!t.Verbs.empty()) o["verbs"] = t.Verbs;   // T322
			if (!t.Modifiers.empty()) { Json mo = Json::object(); for (const auto& [key, f] : t.Modifiers) mo[key] = Round3(f); o["modifiers"] = mo; }   // T331
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
		if (!r.Tiers.empty())   // T323
		{
			Json tiers = Json::array();
			for (const TierStep& step : r.Tiers)
			{
				Json goals = Json::array();
				for (const TierGoal& g : step.Goals)
				{
					Json gj = { { "type", g.Type }, { "count", g.Count } };
					if (!g.Building.empty()) gj["building"] = g.Building;
					goals.push_back(gj);
				}
				Json sj = { { "tier", step.Tier }, { "goals", goals } };
				if (step.Need > 0) sj["need"] = step.Need;
				tiers.push_back(sj);
			}
			out["knowledge"]["tiers"] = tiers;
		}
		return out.dump(2);
	}
}
