#include "GB/Game/Diplomacy.h"
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

		// The numbers of each part by their JSON key (as in Exploration.cpp): floats and whole numbers, each with the least it may be, and maps of
		// key -> number. Floats are 0 or more unless the field says it may be negative.
		template <typename T> struct FloatField { const char* Key; float T::*Field; bool Signed = false; };
		template <typename T> struct IntField { const char* Key; int T::*Field; };
		template <typename T> struct MapField { const char* Key; std::map<std::string, float> T::*Field; bool Signed = true; };

		const FloatField<TerritoryRules> kTerritoryFloats[] = { { "every", &TerritoryRules::Every }, { "reach", &TerritoryRules::Reach }, { "scale", &TerritoryRules::Scale },
			{ "perMember", &TerritoryRules::PerMember }, { "perBuilding", &TerritoryRules::PerBuilding }, { "grow", &TerritoryRules::Grow }, { "min", &TerritoryRules::Min } };
		const FloatField<EncounterRules> kEncounterFloats[] = { { "every", &EncounterRules::Every }, { "radius", &EncounterRules::Radius }, { "cooldown", &EncounterRules::Cooldown } };
		const FloatField<ClanRelationRules> kRelationFloats[] = { { "trustGain", &ClanRelationRules::TrustGain }, { "trustLoss", &ClanRelationRules::TrustLoss },
			{ "decayPerDay", &ClanRelationRules::DecayPerDay }, { "grievanceDecay", &ClanRelationRules::GrievanceDecay }, { "memberWeight", &ClanRelationRules::MemberWeight },
			{ "memberBond", &ClanRelationRules::MemberBond }, { "grievanceWeight", &ClanRelationRules::GrievanceWeight }, { "friendly", &ClanRelationRules::Friendly, true },
			{ "hostile", &ClanRelationRules::Hostile, true }, { "grievanceHostile", &ClanRelationRules::GrievanceHostile } };
		const MapField<ClanRelationRules> kRelationMaps[] = { { "events", &ClanRelationRules::Events }, { "grievances", &ClanRelationRules::Grievances, false },
			{ "openness", &ClanRelationRules::Openness }, { "aggression", &ClanRelationRules::Aggression } };
		const FloatField<TradeRules> kTradeFloats[] = { { "every", &TradeRules::Every }, { "minAttitude", &TradeRules::MinAttitude, true }, { "cooldown", &TradeRules::Cooldown },
			{ "surplus", &TradeRules::Surplus }, { "balance", &TradeRules::Balance }, { "gift", &TradeRules::Gift, true }, { "wait", &TradeRules::Wait },
			{ "score", &TradeRules::Score }, { "meet", &TradeRules::Meet } };
		const IntField<TradeRules> kTradeInts[] = { { "maxUnits", &TradeRules::MaxUnits } };
		const MapField<TradeRules> kTradeMaps[] = { { "values", &TradeRules::Values, false } };
		const FloatField<ExchangeRules> kExchangeFloats[] = { { "minAttitude", &ExchangeRules::MinAttitude, true }, { "teach", &ExchangeRules::Teach }, { "visitEvery", &ExchangeRules::VisitEvery } };
		const FloatField<MarriageRules> kMarriageFloats[] = { { "every", &MarriageRules::Every }, { "minAttitude", &MarriageRules::MinAttitude, true }, { "minBond", &MarriageRules::MinBond, true },
			{ "chance", &MarriageRules::Chance } };
		const FloatField<PactRules> kPactFloats[] = { { "allianceAttitude", &PactRules::AllianceAttitude, true }, { "allianceTrust", &PactRules::AllianceTrust },
			{ "truceGrievance", &PactRules::TruceGrievance }, { "truceOpenness", &PactRules::TruceOpenness }, { "days", &PactRules::Days }, { "breakBelow", &PactRules::BreakBelow, true } };

		template <typename T> struct NoInts { static constexpr IntField<T> List[1] = { { "_", nullptr } }; };
		template <typename T> struct NoMaps { static constexpr MapField<T> List[1] = { { "_", nullptr } }; };

		template <typename T, size_t NF, size_t NI, size_t NM>
		std::string ReadPart(const Json& o, const char* part, const FloatField<T> (&floats)[NF], const IntField<T> (&ints)[NI], const MapField<T> (&maps)[NM], T& into)
		{
			const std::string where = std::string("diplomacy: ") + part;
			if (!o.is_object()) return where + " must be an object";
			for (const auto& [key, v] : o.items())
			{
				if (const auto* f = std::find_if(std::begin(floats), std::end(floats), [&](const FloatField<T>& x) { return key == x.Key; }); f != std::end(floats))
				{
					if (!v.is_number() || (!f->Signed && v.get<float>() < 0.0f)) return where + " " + key + (f->Signed ? " must be a number" : " must be a number of 0 or more");
					into.*(f->Field) = v.get<float>();
					continue;
				}
				if (const auto* i = std::find_if(std::begin(ints), std::end(ints), [&](const IntField<T>& x) { return x.Field && key == x.Key; }); i != std::end(ints))
				{
					if (!v.is_number_integer() || v.get<int>() < 1) return where + " " + key + " must be a whole number of 1 or more";
					into.*(i->Field) = v.get<int>();
					continue;
				}
				if (const auto* m = std::find_if(std::begin(maps), std::end(maps), [&](const MapField<T>& x) { return x.Field && key == x.Key; }); m != std::end(maps))
				{
					if (!v.is_object()) return where + " " + key + " must be an object of key -> number";
					std::map<std::string, float> values;
					for (const auto& [k, n] : v.items())
					{
						if (!n.is_number() || (!m->Signed && n.get<float>() < 0.0f)) return where + " " + key + " " + k + (m->Signed ? " must be a number" : " must be a number of 0 or more");
						values[k] = n.get<float>();
					}
					into.*(m->Field) = std::move(values);
					continue;
				}
				return where + " has no " + key;
			}
			return {};
		}

		template <typename T, size_t NF, size_t NI, size_t NM>
		Json WritePart(const FloatField<T> (&floats)[NF], const IntField<T> (&ints)[NI], const MapField<T> (&maps)[NM], const T& from)
		{
			Json o = Json::object();
			for (const auto& f : floats) o[f.Key] = Rounded(from.*(f.Field));
			for (const auto& i : ints) if (i.Field) o[i.Key] = from.*(i.Field);
			for (const auto& m : maps)
				if (m.Field)
				{
					Json v = Json::object();
					for (const auto& [k, n] : from.*(m.Field)) v[k] = Rounded(n);
					o[m.Key] = v;
				}
			return o;
		}
	}

	DiplomacyRules DefaultDiplomacyRules()
	{
		DiplomacyRules r;
		r.Relations.Events = { { "meet", 0.03f }, { "trade", 0.12f }, { "gift", 0.1f }, { "visit", 0.05f }, { "teach", 0.04f }, { "marriage", 0.15f }, { "pact", 0.05f },
			{ "overlap", -0.01f }, { "theft", -0.08f }, { "refuse", -0.03f }, { "raid", -0.5f } };
		r.Relations.Grievances = { { "theft", 0.25f }, { "raid", 1.0f } };
		r.Relations.Openness = { { "sharing", 0.5f }, { "sociability", 0.4f }, { "boldness", -0.2f } };
		r.Relations.Aggression = { { "boldness", 0.6f }, { "daring", 0.4f }, { "sharing", -0.3f } };
		r.Trade.Values = { { "berry", 1.0f }, { "branch", 0.5f }, { "clay", 1.0f }, { "copper_ore", 3.0f }, { "cordage", 2.0f }, { "dried_berry", 2.0f }, { "fiber", 0.5f },
			{ "flint", 2.0f }, { "grain", 1.5f }, { "reed", 0.5f }, { "stone", 1.0f }, { "wood", 1.0f } };
		return r;
	}

	std::string LoadDiplomacyRules(const std::string& json, DiplomacyRules& live)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("diplomacy");
		if (section == j.end()) return {};
		if (!section->is_object()) return "diplomacy: expected an object";
		DiplomacyRules rules = live;
		const DiplomacyRules defaults = DefaultDiplomacyRules();
		for (const auto& [key, v] : section->items())
		{
			std::string e;
			if (key == "seed")
			{
				if (!v.is_number_integer() || v.get<long long>() < 0) return "diplomacy: seed must be a whole number of 0 or more";
				rules.Seed = static_cast<uint32_t>(v.get<long long>());
			}
			else if (key == "territory") { rules.Territory = TerritoryRules(); e = ReadPart(v, "territory", kTerritoryFloats, NoInts<TerritoryRules>::List, NoMaps<TerritoryRules>::List, rules.Territory); }
			else if (key == "encounters") { rules.Encounters = EncounterRules(); e = ReadPart(v, "encounters", kEncounterFloats, NoInts<EncounterRules>::List, NoMaps<EncounterRules>::List, rules.Encounters); }
			else if (key == "relations")
			{
				rules.Relations = ClanRelationRules();
				rules.Relations.Events = defaults.Relations.Events;   // a map the part does not give is the default one
				rules.Relations.Grievances = defaults.Relations.Grievances;
				rules.Relations.Openness = defaults.Relations.Openness;
				rules.Relations.Aggression = defaults.Relations.Aggression;
				e = ReadPart(v, "relations", kRelationFloats, NoInts<ClanRelationRules>::List, kRelationMaps, rules.Relations);
			}
			else if (key == "trade") { rules.Trade = TradeRules(); e = ReadPart(v, "trade", kTradeFloats, kTradeInts, kTradeMaps, rules.Trade); }
			else if (key == "exchange") { rules.Exchange = ExchangeRules(); e = ReadPart(v, "exchange", kExchangeFloats, NoInts<ExchangeRules>::List, NoMaps<ExchangeRules>::List, rules.Exchange); }
			else if (key == "marriage") { rules.Marriage = MarriageRules(); e = ReadPart(v, "marriage", kMarriageFloats, NoInts<MarriageRules>::List, NoMaps<MarriageRules>::List, rules.Marriage); }
			else if (key == "pacts") { rules.Pacts = PactRules(); e = ReadPart(v, "pacts", kPactFloats, NoInts<PactRules>::List, NoMaps<PactRules>::List, rules.Pacts); }
			else return "diplomacy: unknown part " + key;
			if (!e.empty()) return e;
		}
		if (rules.Territory.Every <= 0.0f || rules.Territory.Scale <= 0.0f) return "diplomacy: territory every and scale must be above 0";
		if (rules.Encounters.Every <= 0.0f || rules.Encounters.Radius <= 0.0f) return "diplomacy: encounters every and radius must be above 0";
		if (rules.Trade.Every <= 0.0f || rules.Trade.Balance < 1.0f || rules.Trade.Meet <= 0.0f) return "diplomacy: trade every and meet must be above 0 and balance at least 1";
		if (rules.Marriage.Every <= 0.0f || rules.Marriage.Chance > 1.0f) return "diplomacy: marriage every must be above 0 and chance in 0..1";
		if (rules.Relations.Hostile >= rules.Relations.Friendly) return "diplomacy: relations hostile must be under friendly";
		if (rules.Pacts.Days <= 0.0f) return "diplomacy: pacts days must be above 0";
		live = rules;
		return {};
	}

	std::string LoadDiplomacyRulesFile(const std::string& path, DiplomacyRules& rules)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return "cannot open " + path;
		std::stringstream ss;
		ss << f.rdbuf();
		return LoadDiplomacyRules(ss.str(), rules);
	}

	std::string DiplomacyRulesToJson(const DiplomacyRules& r)
	{
		Json o = Json::object();
		o["seed"] = r.Seed;
		o["territory"] = WritePart(kTerritoryFloats, NoInts<TerritoryRules>::List, NoMaps<TerritoryRules>::List, r.Territory);
		o["encounters"] = WritePart(kEncounterFloats, NoInts<EncounterRules>::List, NoMaps<EncounterRules>::List, r.Encounters);
		o["relations"] = WritePart(kRelationFloats, NoInts<ClanRelationRules>::List, kRelationMaps, r.Relations);
		o["trade"] = WritePart(kTradeFloats, kTradeInts, kTradeMaps, r.Trade);
		o["exchange"] = WritePart(kExchangeFloats, NoInts<ExchangeRules>::List, NoMaps<ExchangeRules>::List, r.Exchange);
		o["marriage"] = WritePart(kMarriageFloats, NoInts<MarriageRules>::List, NoMaps<MarriageRules>::List, r.Marriage);
		o["pacts"] = WritePart(kPactFloats, NoInts<PactRules>::List, NoMaps<PactRules>::List, r.Pacts);
		return o.dump();
	}

	// ------------------------------------------------------------------ barter
	float ItemValue(const TradeRules& rules, const std::string& item)
	{
		if (rules.Values.empty()) return 1.0f;   // no list: everything is a good worth 1
		const auto it = rules.Values.find(item);
		return it != rules.Values.end() ? it->second : 0.0f;   // a list: what it does not name is not traded
	}

	namespace
	{
		// What `from` can spare of each item and what `to` lacks: the units of `to`'s needs (largest first, ties by id) that `from`'s surplus covers.
		std::map<std::string, int> Cover(const TradeRules& rules, const StoreView& from, const StoreView& to)
		{
			std::vector<std::pair<std::string, float>> needs;
			for (const auto& [item, want] : to.Want)
			{
				if (ItemValue(rules, item) <= 0.0f) continue;   // not a good the clans trade
				const auto h = to.Have.find(item);
				const float need = want - static_cast<float>(h != to.Have.end() ? h->second : 0);
				if (need >= 1.0f) needs.emplace_back(item, need);
			}
			std::stable_sort(needs.begin(), needs.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
			std::map<std::string, int> out;
			for (const auto& [item, need] : needs)
			{
				const auto h = from.Have.find(item);
				if (h == from.Have.end()) continue;
				const auto w = from.Want.find(item);
				const float keep = rules.Surplus * (w != from.Want.end() ? w->second : 0.0f);
				const int spare = static_cast<int>(std::floor(static_cast<float>(h->second) - keep));
				const int units = std::min({ spare, static_cast<int>(std::floor(need)), rules.MaxUnits });
				if (units > 0) out[item] = units;
			}
			return out;
		}

		float Worth(const TradeRules& rules, const std::map<std::string, int>& goods)
		{
			float v = 0.0f;
			for (const auto& [item, n] : goods) v += ItemValue(rules, item) * static_cast<float>(n);
			return v;
		}
	}

	Barter PlanBarter(const TradeRules& rules, const StoreView& a, const StoreView& b, bool gift)
	{
		Barter deal;
		deal.Give[0] = Cover(rules, a, b);   // what a can give of what b lacks
		deal.Give[1] = Cover(rules, b, a);
		if (deal.Give[0].empty() != deal.Give[1].empty() && !gift) { deal.Give[0].clear(); deal.Give[1].clear(); }   // one-sided: only between friends
		if (!deal.Give[0].empty() && !deal.Give[1].empty())
			for (int guard = 0; guard < 256; ++guard)   // the larger side gives up its largest stack one unit at a time (ties: the lowest id)
			{
				const float v0 = Worth(rules, deal.Give[0]), v1 = Worth(rules, deal.Give[1]);
				const int big = v0 > v1 * rules.Balance ? 0 : v1 > v0 * rules.Balance ? 1 : -1;
				if (big < 0) break;
				std::map<std::string, int>& side = deal.Give[big];
				auto most = side.begin();
				for (auto it = side.begin(); it != side.end(); ++it) if (it->second > most->second) most = it;
				if (--most->second <= 0) side.erase(most);
				if (side.empty()) break;
			}
		if (deal.Give[0].empty() != deal.Give[1].empty() && !gift) { deal.Give[0].clear(); deal.Give[1].clear(); }   // (cut down to nothing: no deal)
		deal.Value[0] = Worth(rules, deal.Give[0]);
		deal.Value[1] = Worth(rules, deal.Give[1]);
		return deal;
	}
}
