#include "GB/Game/Economy.h"
#include "GB/Gameplay/GameplayTag.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		double Rounded(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }

		// One part of the section: every key must be known, numbers in range; a mistake returns why.
		struct Reader
		{
			const Json& O;
			std::string Part;
			std::vector<std::string> Seen;

			bool Has(const char* key) { Seen.push_back(key); return O.contains(key); }
			std::string Float(const char* key, float& out, float lo, float hi)
			{
				if (!Has(key)) return {};
				const Json& v = O[key];
				if (!v.is_number() || !(v.get<float>() >= lo && v.get<float>() <= hi)) return "economy: " + Part + " " + key + " must be a number from " + Num(lo) + " to " + Num(hi);
				out = v.get<float>();
				return {};
			}
			std::string Int(const char* key, int& out, int lo, int hi)
			{
				if (!Has(key)) return {};
				const Json& v = O[key];
				if (!v.is_number_integer() || v.get<int>() < lo || v.get<int>() > hi) return "economy: " + Part + " " + key + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi);
				out = v.get<int>();
				return {};
			}
			std::string Bool(const char* key, bool& out)
			{
				if (!Has(key)) return {};
				if (!O[key].is_boolean()) return "economy: " + Part + " " + key + " must be true or false";
				out = O[key].get<bool>();
				return {};
			}
			std::string Text(const char* key, std::string& out)
			{
				if (!Has(key)) return {};
				if (!O[key].is_string()) return "economy: " + Part + " " + key + " must be a text";
				out = O[key].get<std::string>();
				return {};
			}
			std::string Words(const char* key, std::vector<std::string>& out)
			{
				if (!Has(key)) return {};
				if (!O[key].is_array()) return "economy: " + Part + " " + key + " must be a list of texts";
				out.clear();
				for (const Json& s : O[key])
				{
					if (!s.is_string() || s.get<std::string>().empty()) return "economy: " + Part + " " + key + " must be a list of texts";
					out.push_back(s.get<std::string>());
				}
				return {};
			}
			std::string Unknown() const
			{
				for (const auto& [key, v] : O.items())
					if (std::find(Seen.begin(), Seen.end(), key) == Seen.end()) return "economy: " + Part + " has no " + key;
				return {};
			}
			static std::string Num(float v)
			{
				std::ostringstream s;
				s << v;
				return s.str();
			}
		};

		bool IsSeason(const std::string& s) { return s == "spring" || s == "summer" || s == "autumn" || s == "winter"; }

		std::string ReadStore(const Json& o, const std::string& name, StoreKind& k)
		{
			if (!o.is_object()) return "economy: logistics store " + name + " must be an object";
			k = StoreKind();
			Reader r{ o, "logistics store " + name, {} };
			std::string e;
			if (!(e = r.Int("cap", k.Cap, 1, 1000000)).empty() || !(e = r.Int("slots", k.Slots, 1, 10000)).empty() || !(e = r.Float("spoilRate", k.SpoilRate, 0.0f, 100.0f)).empty()
				|| !(e = r.Words("accepts", k.Accepts)).empty() || !(e = r.Bool("haul", k.Haul)).empty()) return e;
			for (const std::string& t : k.Accepts) if (!GameplayTag::IsValidTagName(t)) return "economy: logistics store " + name + " accepts a bad tag " + t;
			return r.Unknown();
		}

		std::string ReadCrop(const Json& o, CropDef& c)
		{
			if (!o.is_object()) return "economy: a crop must be an object";
			c = CropDef();
			Reader r{ o, "crop", {} };
			std::string e;
			if (!(e = r.Text("id", c.Id)).empty() || !(e = r.Text("seed", c.Seed)).empty() || !(e = r.Float("stageSeconds", c.StageSeconds, 1.0f, 1e6f)).empty()
				|| !(e = r.Words("sow", c.Sow)).empty()) return e;
			if (c.Id.empty() || c.Seed.empty()) return "economy: a crop needs an id and a seed";
			for (const std::string& s : c.Sow) if (!IsSeason(s)) return "economy: crop " + c.Id + " is sown in an unknown season " + s;
			if (r.Has("yield"))
			{
				const Json& y = o["yield"];
				if (!y.is_object() || y.empty()) return "economy: crop " + c.Id + " yield must be an object of item -> units";
				for (const auto& [item, n] : y.items())
				{
					if (!n.is_number_integer() || n.get<int>() < 1) return "economy: crop " + c.Id + " yields a whole number of 1 or more of " + item;
					c.Yield[item] = n.get<int>();
				}
			}
			if (c.Yield.empty()) return "economy: crop " + c.Id + " yields nothing";
			return r.Unknown();
		}
	}

	const StoreKind& EconomyRules::Main() const
	{
		static const StoreKind kDefault;
		const auto it = Logistics.Stores.find("main");
		return it != Logistics.Stores.end() ? it->second : kDefault;
	}

	EconomyRules DefaultEconomyRules()
	{
		return EconomyRules();   // (the defaults of each part are the game's: cordage kept, the camp's workbench, the cold of winter, a main store and stockpiles)
	}

	std::string LoadEconomyRules(const std::string& json, EconomyRules& live)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("economy");
		if (section == j.end()) return {};
		if (!section->is_object()) return "economy: expected an object";
		EconomyRules rules = live;
		std::string e;
		for (const auto& [key, v] : section->items())
			if (key != "production" && key != "spoilage" && key != "farming" && key != "crops" && key != "logistics") return "economy: unknown part " + key;
		try
		{
			if (const auto p = section->find("production"); p != section->end())
			{
				if (!p->is_object()) return "economy: production must be an object";
				ProductionRules& x = rules.Production;
				x = ProductionRules();
				Reader r{ *p, "production", {} };
				if (!(e = r.Int("depth", x.Depth, 0, 8)).empty() || !(e = r.Int("maxBatches", x.MaxBatches, 1, 100)).empty() || !(e = r.Float("seconds", x.Seconds, 0.0f, 3600.0f)).empty()
					|| !(e = r.Float("priority", x.Priority, 0.0f, 100.0f)).empty() || !(e = r.Words("campStations", x.CampStations)).empty() || !(e = r.Bool("upgrade", x.Upgrade)).empty()
					|| !(e = r.Bool("experiment", x.Experiment)).empty() || !(e = r.Float("containersPerMember", x.ContainersPerMember, 0.0f, 10.0f)).empty()
					|| !(e = r.Float("preserveAbove", x.PreserveAbove, 0.0f, 100.0f)).empty()) return e;
				if (r.Has("stock"))
				{
					const Json& s = (*p)["stock"];
					if (!s.is_object()) return "economy: production stock must be an object of item -> units";
					x.Stock.clear();
					for (const auto& [item, n] : s.items())
					{
						if (!n.is_number_integer() || n.get<int>() < 0) return "economy: production stock of " + item + " must be a whole number of 0 or more";
						x.Stock[item] = n.get<int>();
					}
				}
				if (!(e = r.Unknown()).empty()) return e;
			}
			if (const auto p = section->find("spoilage"); p != section->end())
			{
				if (!p->is_object()) return "economy: spoilage must be an object";
				SpoilageRules& x = rules.Spoilage;
				x = SpoilageRules();
				Reader r{ *p, "spoilage", {} };
				if (!(e = r.Float("step", x.Step, 0.1f, 3600.0f)).empty() || !(e = r.Float("bag", x.Bag, 0.0f, 100.0f)).empty()) return e;
				if (r.Has("seasons"))
				{
					const Json& s = (*p)["seasons"];
					if (!s.is_object()) return "economy: spoilage seasons must be an object of season -> factor";
					x.Seasons.clear();
					for (const auto& [season, f] : s.items())
					{
						if (!IsSeason(season)) return "economy: spoilage seasons: unknown season " + season;
						if (!f.is_number() || f.get<float>() < 0.0f) return "economy: spoilage factor of " + season + " must be a number of 0 or more";
						x.Seasons[season] = f.get<float>();
					}
				}
				if (!(e = r.Unknown()).empty()) return e;
			}
			if (const auto p = section->find("farming"); p != section->end())
			{
				if (!p->is_object()) return "economy: farming must be an object";
				FarmRules& x = rules.Farming;
				x = FarmRules();
				Reader r{ *p, "farming", {} };
				if (!(e = r.Text("tech", x.Tech)).empty() || !(e = r.Text("tool", x.ToolTag)).empty() || !(e = r.Int("plots", x.Plots, 1, 400)).empty()
					|| !(e = r.Int("columns", x.Columns, 1, 100)).empty() || !(e = r.Float("spacing", x.Spacing, 0.5f, 20.0f)).empty()
					|| !(e = r.Float("moistureSeconds", x.MoistureSeconds, 1.0f, 1e6f)).empty() || !(e = r.Float("workSeconds", x.WorkSeconds, 0.0f, 600.0f)).empty()
					|| !(e = r.Int("plotsPerTrip", x.PlotsPerTrip, 1, 100)).empty() || !(e = r.Float("priority", x.Priority, 0.0f, 100.0f)).empty()
					|| !(e = r.Int("fields", x.Fields, 0, 16)).empty()) return e;
				if (!x.ToolTag.empty() && !GameplayTag::IsValidTagName(x.ToolTag)) return "economy: farming tool is a bad tag " + x.ToolTag;
				if (r.Has("offset"))
				{
					const Json& o = (*p)["offset"];
					if (!o.is_array() || o.size() != 2 || !o[0].is_number() || !o[1].is_number()) return "economy: farming offset must be [x, z]";
					x.Offset = { o[0].get<float>(), o[1].get<float>() };
				}
				if (!(e = r.Unknown()).empty()) return e;
			}
			if (const auto p = section->find("crops"); p != section->end())
			{
				if (!p->is_array()) return "economy: crops must be a list";
				std::map<std::string, CropDef> added;
				for (const Json& o : *p)
				{
					CropDef c;
					if (!(e = ReadCrop(o, c)).empty()) return e;
					if (added.count(c.Id)) return "economy: crop " + c.Id + " twice";
					added[c.Id] = c;
				}
				for (auto& [id, c] : added) rules.Crops[id] = c;
			}
			if (const auto p = section->find("logistics"); p != section->end())
			{
				if (!p->is_object()) return "economy: logistics must be an object";
				LogisticsRules& x = rules.Logistics;
				x = LogisticsRules();
				Reader r{ *p, "logistics", {} };
				if (!(e = r.Float("perBagSlot", x.PerBagSlot, 0.0f, 100.0f)).empty() || !(e = r.Float("priority", x.Priority, 0.0f, 100.0f)).empty()
					|| !(e = r.Int("haulAt", x.HaulAt, 1, 1000)).empty() || !(e = r.Text("fieldStore", x.FieldStore)).empty()) return e;
				if (r.Has("stores"))
				{
					const Json& s = (*p)["stores"];
					if (!s.is_object()) return "economy: logistics stores must be an object of kind -> store";
					x.Stores.clear();
					for (const auto& [name, o] : s.items())
					{
						StoreKind k;
						if (!(e = ReadStore(o, name, k)).empty()) return e;
						x.Stores[name] = k;
					}
				}
				if (!x.Stores.count("main")) x.Stores["main"] = StoreKind();
				if (!x.FieldStore.empty() && !x.Stores.count(x.FieldStore)) return "economy: logistics fieldStore " + x.FieldStore + " is not a kind of store";
				if (!(e = r.Unknown()).empty()) return e;
			}
		}
		catch (const Json::exception& ex)
		{
			return std::string("economy: a value of the wrong type (") + ex.what() + ")";
		}
		live = rules;
		return {};
	}

	std::string LoadEconomyRulesFile(const std::string& path, EconomyRules& rules)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return "cannot open " + path;
		std::stringstream ss;
		ss << f.rdbuf();
		return LoadEconomyRules(ss.str(), rules);
	}

	std::string EconomyRulesToJson(const EconomyRules& r)
	{
		Json p = Json::object(), s = Json::object(), f = Json::object(), c = Json::array(), l = Json::object();
		const ProductionRules& x = r.Production;
		p["depth"] = x.Depth;
		p["maxBatches"] = x.MaxBatches;
		p["seconds"] = Rounded(x.Seconds);
		p["priority"] = Rounded(x.Priority);
		Json stock = Json::object();
		for (const auto& [item, n] : x.Stock) stock[item] = n;
		p["stock"] = stock;
		p["campStations"] = x.CampStations;
		p["upgrade"] = x.Upgrade;
		p["experiment"] = x.Experiment;
		p["containersPerMember"] = Rounded(x.ContainersPerMember);
		p["preserveAbove"] = Rounded(x.PreserveAbove);
		s["step"] = Rounded(r.Spoilage.Step);
		s["bag"] = Rounded(r.Spoilage.Bag);
		Json seasons = Json::object();
		for (const auto& [season, k] : r.Spoilage.Seasons) seasons[season] = Rounded(k);
		s["seasons"] = seasons;
		const FarmRules& m = r.Farming;
		f["tech"] = m.Tech;
		f["tool"] = m.ToolTag;
		f["plots"] = m.Plots;
		f["columns"] = m.Columns;
		f["spacing"] = Rounded(m.Spacing);
		f["offset"] = Json::array({ Rounded(m.Offset.x), Rounded(m.Offset.y) });
		f["moistureSeconds"] = Rounded(m.MoistureSeconds);
		f["workSeconds"] = Rounded(m.WorkSeconds);
		f["plotsPerTrip"] = m.PlotsPerTrip;
		f["priority"] = Rounded(m.Priority);
		f["fields"] = m.Fields;
		for (const auto& [id, crop] : r.Crops)
		{
			Json y = Json::object();
			for (const auto& [item, n] : crop.Yield) y[item] = n;
			c.push_back(Json{ { "id", crop.Id }, { "seed", crop.Seed }, { "yield", y }, { "stageSeconds", Rounded(crop.StageSeconds) }, { "sow", crop.Sow } });
		}
		Json stores = Json::object();
		for (const auto& [name, k] : r.Logistics.Stores)
			stores[name] = Json{ { "cap", k.Cap }, { "slots", k.Slots }, { "spoilRate", Rounded(k.SpoilRate) }, { "accepts", k.Accepts }, { "haul", k.Haul } };
		l["stores"] = stores;
		l["perBagSlot"] = Rounded(r.Logistics.PerBagSlot);
		l["priority"] = Rounded(r.Logistics.Priority);
		l["haulAt"] = r.Logistics.HaulAt;
		l["fieldStore"] = r.Logistics.FieldStore;
		return Json{ { "production", p }, { "spoilage", s }, { "farming", f }, { "crops", c }, { "logistics", l } }.dump();
	}

	std::string CheckEconomy(const EconomyRules& rules, const ItemLibrary& items)
	{
		std::string out;
		auto problem = [&](const std::string& p) { out += (out.empty() ? "" : "; ") + p; };
		for (const auto& [item, n] : rules.Production.Stock)
			if (!items.Find(item)) problem("economy: production stock names an unknown item " + item);
		for (const auto& [id, c] : rules.Crops)
		{
			if (!items.Find(c.Seed)) problem("economy: crop " + id + " is sown with an unknown item " + c.Seed);
			for (const auto& [item, n] : c.Yield) if (!items.Find(item)) problem("economy: crop " + id + " yields an unknown item " + item);
		}
		return out;
	}

	// ------------------------------------------------------------------ the saved state
	std::string EconomyState::ToJson() const
	{
		Json j = Json::object();
		j["fields"] = Json::parse(FieldsToJson(Fields));
		Json piles = Json::array();
		for (const Stockpile& p : Stockpiles)
			piles.push_back(Json{ { "name", p.Name }, { "kind", p.Kind }, { "pos", Json::array({ p.Pos.x, p.Pos.y }) }, { "store", Json::parse(p.Store.ToJson()) } });
		j["stockpiles"] = piles;
		j["spoilTimer"] = SpoilTimer;
		auto counts = [](const std::map<std::string, int>& m) { Json o = Json::object(); for (const auto& [k, n] : m) o[k] = n; return o; };
		j["spoiled"] = counts(Spoiled);
		j["made"] = counts(Made);
		j["harvested"] = counts(Harvested);
		j["hauled"] = Hauled;
		j["ruined"] = Ruined;
		j["dayStart"] = DayStart;
		j["foodToday"] = FoodToday;
		j["foodYesterday"] = FoodYesterday;
		return j.dump();
	}

	bool EconomyState::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		EconomyState s;
		try
		{
			if (j.contains("fields") && !FieldsFromJson(j["fields"].dump(), s.Fields)) return false;
			if (j.contains("stockpiles"))
			{
				if (!j["stockpiles"].is_array()) return false;
				for (const Json& o : j["stockpiles"])
				{
					if (!o.is_object() || !o.contains("pos") || !o["pos"].is_array() || o["pos"].size() != 2 || !o.contains("store")) return false;
					Stockpile p;
					p.Name = o.value("name", std::string());
					p.Kind = o.value("kind", std::string("stockpile"));
					p.Pos = { o["pos"][0].get<float>(), o["pos"][1].get<float>() };
					if (p.Name.empty() || !p.Store.FromJson(o["store"].dump())) return false;
					s.Stockpiles.push_back(std::move(p));
				}
			}
			auto counts = [&](const char* key, std::map<std::string, int>& m)
			{
				if (!j.contains(key)) return true;
				if (!j[key].is_object()) return false;
				for (const auto& [k, n] : j[key].items()) { if (!n.is_number_integer()) return false; m[k] = n.get<int>(); }
				return true;
			};
			if (!counts("spoiled", s.Spoiled) || !counts("made", s.Made) || !counts("harvested", s.Harvested)) return false;
			s.SpoilTimer = j.value("spoilTimer", 0.0f);
			s.Hauled = j.value("hauled", 0);
			s.Ruined = j.value("ruined", 0);
			s.DayStart = j.value("dayStart", 0.0);
			s.FoodToday = j.value("foodToday", 0);
			s.FoodYesterday = j.value("foodYesterday", 0);
		}
		catch (const Json::exception&)
		{
			return false;
		}
		*this = std::move(s);
		return true;
	}
}
