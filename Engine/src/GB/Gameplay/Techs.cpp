#include "GB/Gameplay/Techs.h"
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
		TechTree tree;
		auto add = [&](const char* id, const char* name, int era, bool known, std::vector<std::string> requires_, std::vector<std::string> recipes, TechDiscovery d = {})
		{
			TechDef t;
			t.Id = id;
			t.Name = name;
			t.Era = era;
			t.Known = known;
			t.Requires = std::move(requires_);
			t.Recipes = std::move(recipes);
			t.Discovery = d;
			tree.Techs[t.Id] = t;
		};
		auto how = [](float experiment, float observe, float teach, float seconds, float waste)
		{
			TechDiscovery d;
			d.Experiment = experiment;
			d.Observe = observe;
			d.Teach = teach;
			d.Seconds = seconds;
			d.Waste = waste;
			return d;
		};
		auto builds = [&](const char* id, std::vector<std::string> buildings) { tree.Techs[id].Buildings = std::move(buildings); };
		// The Stone-Age tree (docs/PROGRESION.md): tools, fire and cooking, fibre, hunting and hides, building, food storage, herbs and rites, and a
		// preview of era 2 (pottery and the kiln, copper and the forge).
		add("stone_tools", "Stone tools", 1, true, {}, { "stone_axe", "stone_pickaxe" });
		add("fire_making", "Fire making", 1, true, {}, { "campfire" });
		add("cordage", "Cordage", 1, true, {}, { "cordage" });
		add("flint_knapping", "Flint knapping", 1, false, { "cordage", "stone_tools" }, { "flint_axe", "flint_pickaxe" }, how(0.15f, 0.34f, 0.25f, 20.0f, 0.3f));
		add("basketry", "Basketry", 1, false, { "cordage" }, { "basket_wicker" }, how(0.2f, 0.34f, 0.25f, 15.0f, 0.3f));
		add("spear_making", "Hafted spears", 1, false, { "flint_knapping" }, { "flint_spear" }, how(0.12f, 0.3f, 0.25f, 20.0f, 0.3f));
		add("hide_working", "Hide working", 1, false, { "flint_knapping" }, { "leather", "leather_bag" }, how(0.1f, 0.3f, 0.25f, 25.0f, 0.3f));
		builds("hide_working", { "hide_frame" });
		add("cooking", "Cooking", 1, false, { "fire_making" }, { "cooked_meat" }, how(0.2f, 0.34f, 0.3f, 15.0f, 0.2f));
		builds("cooking", { "hearth" });
		add("smoking", "Smoking", 1, false, { "cooking" }, { "smoked_meat" }, how(0.1f, 0.3f, 0.25f, 25.0f, 0.3f));
		builds("smoking", { "smoking_rack" });
		add("weaving", "Weaving", 1, false, { "basketry" }, { "reed_mat" }, how(0.15f, 0.3f, 0.25f, 20.0f, 0.2f));
		add("wattle_and_daub", "Wattle and daub", 1, false, { "basketry" }, { "daub" }, how(0.15f, 0.3f, 0.25f, 20.0f, 0.2f));
		builds("wattle_and_daub", { "hut" });
		add("timber_framing", "Timber framing", 1, false, { "flint_knapping", "wattle_and_daub", "weaving" }, {}, how(0.04f, 0.0f, 0.2f, 60.0f, 0.0f));
		builds("timber_framing", { "longhouse", "granary" });
		add("palisade_building", "Palisades", 1, false, { "flint_knapping" }, { "stakes" }, how(0.08f, 0.3f, 0.2f, 30.0f, 0.1f));
		builds("palisade_building", { "palisade" });
		add("food_storage", "Food storage", 1, false, { "basketry" }, {}, how(0.1f, 0.0f, 0.25f, 20.0f, 0.0f));
		builds("food_storage", { "cache_pit" });
		add("herb_lore", "Herb lore", 1, false, { "basketry" }, { "poultice" }, how(0.15f, 0.3f, 0.25f, 20.0f, 0.3f));
		builds("herb_lore", { "healer_hut" });
		add("ritual", "Rites", 1, false, { "fire_making" }, {}, how(0.06f, 0.0f, 0.3f, 30.0f, 0.0f));
		builds("ritual", { "totem" });
		add("pottery", "Pottery", 2, false, { "fire_making", "wattle_and_daub" }, { "fired_brick" }, how(0.01f, 0.2f, 0.15f, 60.0f, 0.4f));
		builds("pottery", { "kiln" });
		add("smelting", "Smelting", 2, false, { "pottery", "flint_knapping" }, { "copper_ingot" }, how(0.01f, 0.2f, 0.1f, 60.0f, 0.5f));
		builds("smelting", { "forge" });
		return tree;
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
				Number(*it, "growth", r.Growth, 0.0f, 100.0f) })
				if (!error.empty()) return "knowledge: " + error;
			if (const auto s = it->find("seed"); s != it->end())
			{
				if (!s->is_number_integer() || s->get<long long>() < 0 || s->get<long long>() > 0xffffffffLL) return "knowledge: seed must be a whole number of 0 or more";
				r.Seed = static_cast<uint32_t>(s->get<long long>());
			}
			rules = r;
		}
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
		Json out = { { "techs", techs }, { "knowledge", { { "lessonSeconds", Round3(r.LessonSeconds) }, { "observeRadius", Round3(r.ObserveRadius) }, { "growth", Round3(r.Growth) }, { "seed", r.Seed } } } };
		return out.dump(2);
	}
}
