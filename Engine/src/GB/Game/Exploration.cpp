#include "GB/Game/Exploration.h"
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

		// The numbers of each part by their JSON key: floats (0 or more) and whole numbers (1 or more).
		template <typename T> struct FloatField { const char* Key; float T::*Field; };
		template <typename T> struct IntField { const char* Key; int T::*Field; };

		const FloatField<RegionRules> kRegionFloats[] = { { "tile", &RegionRules::Tile }, { "waterShare", &RegionRules::WaterShare }, { "scoutCell", &RegionRules::ScoutCell },
			{ "nearWater", &RegionRules::NearWater }, { "predatorDanger", &RegionRules::PredatorDanger } };
		const IntField<RegionRules> kRegionInts[] = { { "blockTiles", &RegionRules::BlockTiles }, { "minTiles", &RegionRules::MinTiles } };
		const FloatField<ExpeditionRules> kExpeditionFloats[] = { { "every", &ExpeditionRules::Every }, { "scarcity", &ExpeditionRules::Scarcity }, { "gather", &ExpeditionRules::Gather },
			{ "travelScale", &ExpeditionRules::TravelScale }, { "maxDistance", &ExpeditionRules::MaxDistance }, { "riskWeight", &ExpeditionRules::RiskWeight },
			{ "retry", &ExpeditionRules::Retry }, { "maxSeconds", &ExpeditionRules::MaxSeconds }, { "priority", &ExpeditionRules::Priority }, { "score", &ExpeditionRules::Score },
			{ "seeEvery", &ExpeditionRules::SeeEvery } };
		const IntField<ExpeditionRules> kExpeditionInts[] = { { "minParty", &ExpeditionRules::MinParty }, { "maxParty", &ExpeditionRules::MaxParty },
			{ "scoutPoints", &ExpeditionRules::ScoutPoints }, { "food", &ExpeditionRules::Food } };
		const FloatField<MigrationRules> kMigrationFloats[] = { { "every", &MigrationRules::Every }, { "homeRadius", &MigrationRules::HomeRadius }, { "settle", &MigrationRules::Settle },
			{ "poor", &MigrationRules::Poor }, { "waterLost", &MigrationRules::WaterLost }, { "after", &MigrationRules::After }, { "recover", &MigrationRules::Recover },
			{ "minGain", &MigrationRules::MinGain }, { "cooldown", &MigrationRules::Cooldown }, { "arrive", &MigrationRules::Arrive }, { "score", &MigrationRules::Score } };
		const IntField<MigrationRules> kMigrationInts[] = { { "_", nullptr } };

		template <typename T, size_t NF, size_t NI>
		std::string ReadPart(const Json& o, const char* part, const FloatField<T> (&floats)[NF], const IntField<T> (&ints)[NI], T& into, std::initializer_list<const char*> others)
		{
			if (!o.is_object()) return std::string("exploration: ") + part + " must be an object";
			for (const auto& [key, v] : o.items())
			{
				if (std::find_if(others.begin(), others.end(), [&](const char* k) { return key == k; }) != others.end()) continue;
				if (const auto* f = std::find_if(std::begin(floats), std::end(floats), [&](const FloatField<T>& x) { return key == x.Key; }); f != std::end(floats))
				{
					if (!v.is_number() || v.get<float>() < 0.0f) return std::string("exploration: ") + part + " " + key + " must be a number of 0 or more";
					into.*(f->Field) = v.get<float>();
					continue;
				}
				if (const auto* i = std::find_if(std::begin(ints), std::end(ints), [&](const IntField<T>& x) { return x.Field && key == x.Key; }); i != std::end(ints))
				{
					if (!v.is_number_integer() || v.get<int>() < 1) return std::string("exploration: ") + part + " " + key + " must be a whole number of 1 or more";
					into.*(i->Field) = v.get<int>();
					continue;
				}
				return std::string("exploration: ") + part + " has no " + key;
			}
			return {};
		}

		template <typename T, size_t NF, size_t NI>
		void WritePart(Json& o, const FloatField<T> (&floats)[NF], const IntField<T> (&ints)[NI], const T& from)
		{
			for (const auto& f : floats) o[f.Key] = Rounded(from.*(f.Field));
			for (const auto& i : ints) if (i.Field) o[i.Key] = from.*(i.Field);
		}

		bool IsBiome(const std::string& name)
		{
			for (int b = 0; b < 7; ++b) if (name == BiomeName(static_cast<Biome>(b))) return true;
			return false;
		}
	}

	ExplorationRules DefaultExplorationRules()
	{
		ExplorationRules r;
		r.Regions = DefaultRegionRules();
		return r;
	}

	std::string LoadExplorationRules(const std::string& json, ExplorationRules& live)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("exploration");
		if (section == j.end()) return {};
		if (!section->is_object()) return "exploration: expected an object";
		ExplorationRules rules = live;
		for (const auto& [key, v] : section->items())
			if (key != "regions" && key != "expeditions" && key != "migration") return "exploration: unknown part " + key;
		if (const auto r = section->find("regions"); r != section->end())
		{
			rules.Regions = RegionRules();
			if (std::string e = ReadPart(*r, "regions", kRegionFloats, kRegionInts, rules.Regions, { "danger", "names" }); !e.empty()) return e;
			if (const auto d = r->find("danger"); d != r->end())
			{
				if (!d->is_object()) return "exploration: regions danger must be an object of biome -> 0..1";
				for (const auto& [biome, v] : d->items())
				{
					if (!IsBiome(biome)) return "exploration: regions danger: unknown biome " + biome;
					if (!v.is_number() || v.get<float>() < 0.0f || v.get<float>() > 1.0f) return "exploration: regions danger of " + biome + " must be in 0..1";
					rules.Regions.Danger[biome] = v.get<float>();
				}
			}
			if (const auto n = r->find("names"); n != r->end())
			{
				if (!n->is_object()) return "exploration: regions names must be an object of biome -> word";
				for (const auto& [biome, v] : n->items())
				{
					if (!IsBiome(biome)) return "exploration: regions names: unknown biome " + biome;
					if (!v.is_string() || v.get<std::string>().empty()) return "exploration: regions name of " + biome + " must be a word";
					rules.Regions.Names[biome] = v.get<std::string>();
				}
			}
			if (rules.Regions.Tile <= 0.0f || rules.Regions.ScoutCell <= 0.0f) return "exploration: regions tile and scoutCell must be above 0";
			if (rules.Regions.WaterShare > 1.0f) return "exploration: regions waterShare must be in 0..1";
		}
		if (const auto x = section->find("expeditions"); x != section->end())
		{
			rules.Expeditions = ExpeditionRules();
			if (std::string e = ReadPart(*x, "expeditions", kExpeditionFloats, kExpeditionInts, rules.Expeditions, {}); !e.empty()) return e;
			if (rules.Expeditions.Every <= 0.0f || rules.Expeditions.SeeEvery <= 0.0f || rules.Expeditions.TravelScale <= 0.0f) return "exploration: expeditions every, seeEvery and travelScale must be above 0";
			if (rules.Expeditions.MinParty > rules.Expeditions.MaxParty) return "exploration: expeditions minParty must not be above maxParty";
		}
		if (const auto m = section->find("migration"); m != section->end())
		{
			rules.Migration = MigrationRules();
			if (std::string e = ReadPart(*m, "migration", kMigrationFloats, kMigrationInts, rules.Migration, {}); !e.empty()) return e;
			if (rules.Migration.Every <= 0.0f || rules.Migration.HomeRadius <= 0.0f) return "exploration: migration every and homeRadius must be above 0";
			if (rules.Migration.Poor > 1.0f) return "exploration: migration poor must be in 0..1";
		}
		live = rules;
		return {};
	}

	std::string LoadExplorationRulesFile(const std::string& path, ExplorationRules& rules)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return "cannot open " + path;
		std::stringstream ss;
		ss << f.rdbuf();
		return LoadExplorationRules(ss.str(), rules);
	}

	std::string ExplorationRulesToJson(const ExplorationRules& rules)
	{
		Json r = Json::object(), x = Json::object(), m = Json::object();
		WritePart(r, kRegionFloats, kRegionInts, rules.Regions);
		Json danger = Json::object(), names = Json::object();
		for (const auto& [b, d] : rules.Regions.Danger) danger[b] = Rounded(d);
		for (const auto& [b, n] : rules.Regions.Names) names[b] = n;
		r["danger"] = danger;
		r["names"] = names;
		WritePart(x, kExpeditionFloats, kExpeditionInts, rules.Expeditions);
		WritePart(m, kMigrationFloats, kMigrationInts, rules.Migration);
		return Json{ { "regions", r }, { "expeditions", x }, { "migration", m } }.dump();
	}

	// ------------------------------------------------------------------ the saved state
	namespace
	{
		// The seen cells as hex, two characters per eight cells: compact and readable enough.
		std::string Bits(const std::vector<uint8_t>& seen)
		{
			static const char* kHex = "0123456789abcdef";
			std::string s;
			for (size_t i = 0; i < seen.size(); i += 8)
			{
				unsigned byte = 0;
				for (size_t k = 0; k < 8 && i + k < seen.size(); ++k) if (seen[i + k]) byte |= 1u << k;
				s += kHex[byte >> 4];
				s += kHex[byte & 15u];
			}
			return s;
		}

		bool Unbits(const std::string& s, size_t cells, std::vector<uint8_t>& out)
		{
			if (s.size() != (cells + 7) / 8 * 2) return false;
			out.assign(cells, 0);
			auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
			for (size_t i = 0; i < s.size(); i += 2)
			{
				const int hi = nibble(s[i]), lo = nibble(s[i + 1]);
				if (hi < 0 || lo < 0) return false;
				const unsigned byte = static_cast<unsigned>(hi * 16 + lo);
				for (size_t k = 0; k < 8 && (i / 2) * 8 + k < cells; ++k) out[(i / 2) * 8 + k] = (byte >> k) & 1u ? 1 : 0;
			}
			return true;
		}

		Json Vec(const glm::vec2& v) { return Json::array({ v.x, v.y }); }
		bool ReadVec(const Json& j, glm::vec2& v)
		{
			if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return false;
			v = { j[0].get<float>(), j[1].get<float>() };
			return true;
		}
		Json Strings(const std::vector<std::string>& v) { Json a = Json::array(); for (const std::string& s : v) a.push_back(s); return a; }
		std::vector<std::string> ReadStrings(const Json& j)
		{
			std::vector<std::string> out;
			if (j.is_array()) for (const Json& s : j) if (s.is_string()) out.push_back(s.get<std::string>());
			return out;
		}
	}

	std::string ExplorationState::ToJson() const
	{
		Json j;
		j["cells"] = Atlas.Seen.size();
		j["seen"] = Bits(Atlas.Seen);
		Json lore = Json::object();
		for (const auto& [id, l] : Atlas.Lore)
		{
			Json res = Json::object();
			for (const auto& [k, n] : l.Resources) res[k] = n;
			lore[std::to_string(id)] = Json{ { "cells", l.SeenCells }, { "explored", l.Explored }, { "resources", res }, { "visit", l.LastVisit }, { "expeditions", l.Expeditions } };
		}
		j["lore"] = lore;
		Json pending = Json::object();
		for (const auto& [who, cells] : Atlas.Pending)
		{
			Json a = Json::array();
			for (const auto& [c, t] : cells) a.push_back(Json::array({ c, t }));
			pending[who] = a;
		}
		j["pending"] = pending;
		if (Current.Active())
		{
			Json legs = Json::object();
			for (const auto& [who, leg] : Current.Leg) legs[who] = leg;
			Json points = Json::array();
			for (const glm::vec2& p : Current.Points) points.push_back(Vec(p));
			j["expedition"] = Json{ { "id", Current.Id }, { "item", Current.Item }, { "region", Current.Region }, { "regionName", Current.RegionName }, { "stage", Current.Stage },
				{ "party", Strings(Current.Party) }, { "legs", legs }, { "points", points }, { "posted", Current.Posted }, { "started", Current.Started }, { "why", Current.Why } };
		}
		Json history = Json::array();
		for (const ExpeditionRecord& r : History)
			history.push_back(Json{ { "id", r.Id }, { "item", r.Item }, { "region", r.Region }, { "regionName", r.RegionName }, { "found", r.Found ? 1 : 0 }, { "started", r.Started },
				{ "ended", r.Ended }, { "party", Strings(r.Party) } });
		j["history"] = history;
		j["nextId"] = NextId;
		Json unmet = Json::object();
		for (const auto& [k, v] : Unmet) unmet[k] = v;
		j["unmet"] = unmet;
		Json tried = Json::object();
		for (const auto& [k, v] : Tried) tried[k] = v;
		j["tried"] = tried;
		j["timer"] = Timer;
		j["seeTimer"] = SeeTimer;
		j["homeTimer"] = HomeTimer;
		j["campSince"] = CampSince;
		j["peak"] = Peak;
		j["poor"] = Poor;
		j["poorWhy"] = PoorWhy;
		Json moves = Json::array();
		for (const MigrationRecord& m : Moves) moves.push_back(Json{ { "from", Vec(m.From) }, { "to", Vec(m.To) }, { "region", m.Region }, { "why", m.Why }, { "at", m.At } });
		j["moves"] = moves;
		j["arrived"] = Strings(std::vector<std::string>(Arrived.begin(), Arrived.end()));
		j["moving"] = Moving ? 1 : 0;
		return j.dump();
	}

	bool ExplorationState::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		ExplorationState s;
		const size_t cells = j.value("cells", static_cast<size_t>(0));
		if (cells > 0 && !Unbits(j.value("seen", std::string()), cells, s.Atlas.Seen)) return false;
		if (const auto lore = j.find("lore"); lore != j.end())
		{
			if (!lore->is_object()) return false;
			for (const auto& [id, l] : lore->items())
			{
				if (!l.is_object() || id.empty() || id.find_first_not_of("0123456789") != std::string::npos) return false;
				RegionLore r;
				r.SeenCells = l.value("cells", 0);
				r.Explored = l.value("explored", 0.0f);
				r.LastVisit = l.value("visit", -1.0);
				r.Expeditions = l.value("expeditions", 0);
				if (const auto res = l.find("resources"); res != l.end() && res->is_object())
					for (const auto& [k, n] : res->items()) if (n.is_number_integer()) r.Resources[k] = n.get<int>();
				s.Atlas.Lore[std::stoi(id)] = r;
			}
		}
		if (const auto pending = j.find("pending"); pending != j.end() && pending->is_object())
			for (const auto& [who, a] : pending->items())
			{
				if (!a.is_array()) return false;
				for (const Json& e : a)
				{
					if (!e.is_array() || e.size() != 2 || !e[0].is_number_integer() || !e[1].is_number()) return false;
					s.Atlas.Pending[who][e[0].get<int>()] = e[1].get<double>();
				}
			}
		if (const auto x = j.find("expedition"); x != j.end() && x->is_object())
		{
			Expedition& e = s.Current;
			e.Id = x->value("id", 0);
			e.Item = x->value("item", std::string());
			e.Region = x->value("region", -1);
			e.RegionName = x->value("regionName", std::string());
			e.Stage = x->value("stage", std::string());
			e.Party = ReadStrings(x->value("party", Json::array()));
			if (const auto legs = x->find("legs"); legs != x->end() && legs->is_object())
				for (const auto& [who, leg] : legs->items()) if (leg.is_number_integer()) e.Leg[who] = leg.get<int>();
			if (const auto points = x->find("points"); points != x->end() && points->is_array())
				for (const Json& p : *points)
				{
					glm::vec2 v;
					if (!ReadVec(p, v)) return false;
					e.Points.push_back(v);
				}
			e.Posted = x->value("posted", 0.0);
			e.Started = x->value("started", 0.0);
			e.Why = x->value("why", std::string());
			if (e.Stage != "forming" && e.Stage != "out") return false;
		}
		if (const auto h = j.find("history"); h != j.end() && h->is_array())
			for (const Json& r : *h)
			{
				if (!r.is_object()) return false;
				ExpeditionRecord rec;
				rec.Id = r.value("id", 0);
				rec.Item = r.value("item", std::string());
				rec.Region = r.value("region", -1);
				rec.RegionName = r.value("regionName", std::string());
				rec.Found = r.value("found", 0) != 0;
				rec.Started = r.value("started", 0.0);
				rec.Ended = r.value("ended", 0.0);
				rec.Party = ReadStrings(r.value("party", Json::array()));
				s.History.push_back(rec);
			}
		s.NextId = j.value("nextId", 1);
		if (const auto u = j.find("unmet"); u != j.end() && u->is_object())
			for (const auto& [k, v] : u->items()) if (v.is_number()) s.Unmet[k] = v.get<float>();
		if (const auto t = j.find("tried"); t != j.end() && t->is_object())
			for (const auto& [k, v] : t->items()) if (v.is_number()) s.Tried[k] = v.get<double>();
		s.Timer = j.value("timer", 0.0f);
		s.SeeTimer = j.value("seeTimer", 0.0f);
		s.HomeTimer = j.value("homeTimer", 0.0f);
		s.CampSince = j.value("campSince", 0.0);
		s.Peak = j.value("peak", 0.0f);
		s.Poor = j.value("poor", 0.0f);
		s.PoorWhy = j.value("poorWhy", std::string());
		if (const auto m = j.find("moves"); m != j.end() && m->is_array())
			for (const Json& r : *m)
			{
				if (!r.is_object()) return false;
				MigrationRecord rec;
				if (!ReadVec(r.value("from", Json()), rec.From) || !ReadVec(r.value("to", Json()), rec.To)) return false;
				rec.Region = r.value("region", std::string());
				rec.Why = r.value("why", std::string());
				rec.At = r.value("at", 0.0);
				s.Moves.push_back(rec);
			}
		for (const std::string& a : ReadStrings(j.value("arrived", Json::array()))) s.Arrived.insert(a);
		s.Moving = j.value("moving", 0) != 0;
		*this = std::move(s);
		return true;
	}
}
