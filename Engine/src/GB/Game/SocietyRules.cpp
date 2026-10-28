// The "society" section of a content file (H9 + H10): the data of GB/Game/Society.h, read and written. Atomic: the rules are read into a copy and
// only replace the live ones when the whole section is right.
#include "GB/Game/Society.h"
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
		double Rounded(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }   // 0.35f is written as 0.35

		// The plain numbers of each part, by their JSON key.
		struct LifeField { const char* Key; float LifeRules::*Field; };
		constexpr LifeField kLifeFields[] = { { "daySeconds", &LifeRules::DaySeconds }, { "daysPerYear", &LifeRules::DaysPerYear }, { "childUntil", &LifeRules::ChildUntil },
			{ "elderFrom", &LifeRules::ElderFrom }, { "maxAge", &LifeRules::MaxAge }, { "longevity", &LifeRules::Longevity }, { "oldAge", &LifeRules::OldAge },
			{ "founderMin", &LifeRules::FounderMin }, { "founderMax", &LifeRules::FounderMax }, { "childWork", &LifeRules::ChildWork }, { "elderWork", &LifeRules::ElderWork },
			{ "childLearn", &LifeRules::ChildLearn }, { "childTeach", &LifeRules::ChildTeach }, { "elderTeach", &LifeRules::ElderTeach }, { "childPace", &LifeRules::ChildPace },
			{ "elderPace", &LifeRules::ElderPace }, { "childHeight", &LifeRules::ChildHeight } };
		struct BirthField { const char* Key; float BirthRules::*Field; };
		constexpr BirthField kBirthFields[] = { { "pairAge", &BirthRules::PairAge }, { "ageGap", &BirthRules::AgeGap }, { "minAge", &BirthRules::MinAge },
			{ "maxAge", &BirthRules::MaxAge }, { "spacing", &BirthRules::Spacing }, { "chance", &BirthRules::Chance }, { "foodPerMember", &BirthRules::FoodPerMember } };
		struct RelationField { const char* Key; float RelationRules::*Field; };
		constexpr RelationField kRelationFields[] = { { "decayPerDay", &RelationRules::DecayPerDay }, { "keep", &RelationRules::Keep }, { "kin", &RelationRules::Kin },
			{ "campSeconds", &RelationRules::CampSeconds }, { "campRadius", &RelationRules::CampRadius }, { "workRadius", &RelationRules::WorkRadius } };

		std::string Fail(const std::string& part, const std::string& text) { return "society: " + part + " " + text; }

		// The numbers of `o` through the table; keys in `others` are left to the caller; an unknown key or a negative number is a mistake.
		template <typename T, typename F, size_t N>
		std::string ReadNumbers(const Json& o, const char* part, const F (&fields)[N], T& into, std::initializer_list<const char*> others)
		{
			for (const auto& [key, v] : o.items())
			{
				if (std::find_if(others.begin(), others.end(), [&](const char* k) { return key == k; }) != others.end()) continue;
				const F* f = std::find_if(std::begin(fields), std::end(fields), [&](const F& x) { return key == x.Key; });
				if (f == std::end(fields)) return Fail(part, "has no " + key);
				if (!v.is_number() || v.get<float>() < 0.0f) return Fail(part, key + " must be a number of 0 or more");
				into.*(f->Field) = v.get<float>();
			}
			return {};
		}

		// name -> number (any sign when `signedValues`, else 0 or more)
		std::string ReadMap(const Json& o, const std::string& part, const char* key, std::map<std::string, float>& out, bool signedValues)
		{
			const auto m = o.find(key);
			if (m == o.end()) return {};
			if (!m->is_object()) return Fail(part, std::string(key) + " must be an object of name -> number");
			out.clear();
			for (const auto& [k, v] : m->items())
			{
				if (!v.is_number() || (!signedValues && v.get<float>() < 0.0f)) return Fail(part, std::string(key) + ": " + k + " must be a number" + (signedValues ? "" : " of 0 or more"));
				out[k] = v.get<float>();
			}
			return {};
		}

		// A value in 0..1 (mean, start, heritability).
		std::string ReadUnit(const Json& o, const std::string& part, const char* key, float& out)
		{
			const auto v = o.find(key);
			if (v == o.end()) return {};
			if (!v->is_number() || v->get<float>() < 0.0f || v->get<float>() > 1.0f) return Fail(part, std::string(key) + " must be a number from 0 to 1");
			out = v->get<float>();
			return {};
		}

		std::string OnlyKeys(const Json& o, const std::string& part, std::initializer_list<const char*> keys)
		{
			for (const auto& [key, v] : o.items())
				if (std::find_if(keys.begin(), keys.end(), [&](const char* k) { return key == k; }) == keys.end()) return Fail(part, "has no " + key);
			return {};
		}

		Json WriteMap(const std::map<std::string, float>& m)
		{
			Json o = Json::object();
			for (const auto& [k, v] : m) o[k] = Rounded(v);
			return o;
		}
	}

	SocietyRules DefaultSocietyRules()
	{
		SocietyRules r;
		r.Genes["strength"] = { 0.5f, 0.2f, { { "woodcutting", 0.25f }, { "mining", 0.25f } }, {}, { { "woodcutting", 0.2f }, { "mining", 0.2f } } };
		r.Genes["endurance"] = { 0.5f, 0.2f, { { "sleep", -0.2f }, { "explore", 0.2f } }, {}, {} };
		r.Genes["perception"] = { 0.5f, 0.2f, { { "explore", 0.3f }, { "experiment", 0.15f } }, { { "foraging", 0.3f } }, {} };
		r.Genes["dexterity"] = { 0.5f, 0.2f, { { "crafting", 0.2f } }, { { "crafting", 0.4f }, { "foraging", 0.2f } }, { { "crafting", 0.2f }, { "foraging", 0.15f } } };
		r.Genes["longevity"] = { 0.5f, 0.2f, {}, {}, {} };
		r.Traits["sociability"] = { 0.5f, 0.25f, 0.5f, { { "socialize", 0.8f }, { "regroup", 0.2f }, { "warm", 0.1f } } };
		r.Traits["diligence"] = { 0.5f, 0.25f, 0.5f, { { "gather", 0.35f }, { "deposit", 0.2f }, { "tool", 0.2f }, { "rest", -0.7f } } };
		r.Traits["curiosity"] = { 0.5f, 0.25f, 0.5f, { { "experiment", 0.8f }, { "explore", 0.5f } } };
		r.Traits["boldness"] = { 0.5f, 0.25f, 0.5f, { { "explore", 0.4f }, { "regroup", -0.5f } } };
		r.Relations.Events = { { "work", 0.02f }, { "food", 0.03f }, { "camp", 0.015f }, { "conflict", -0.12f }, { "teach", 0.04f }, { "pair", 0.3f },
			{ "birth", 0.4f }, { "help", 0.1f }, { "theft", -0.4f }, { "rescue", 0.4f } };
		r.Culture.Norms["sharing"] = { 0.5f, "sociability", { { "socialize", 0.3f }, { "deposit", 0.2f }, { "warm", 0.1f } } };
		r.Culture.Norms["industry"] = { 0.5f, "diligence", { { "gather", 0.2f }, { "rest", -0.3f }, { "sleep", -0.1f } } };
		r.Culture.Norms["inquiry"] = { 0.5f, "curiosity", { { "experiment", 0.4f }, { "explore", 0.2f } } };
		r.Culture.Norms["daring"] = { 0.5f, "boldness", { { "explore", 0.2f }, { "regroup", -0.2f } } };
		r.Culture.Traditions["fire_vigil"] = { "Fire vigil", "stoke", 30, { { "warm", 0.15f }, { "socialize", 0.1f } } };
		r.Culture.Traditions["naming_feast"] = { "Naming feast", "birth", 3, { { "socialize", 0.15f } } };
		r.Culture.Traditions["mourning"] = { "Mourning", "death", 3, { { "regroup", 0.15f } } };
		r.Culture.Traditions["lore_circle"] = { "Lore circle", "learn", 5, { { "experiment", 0.2f } } };
		r.Culture.Traditions["toolmakers"] = { "Toolmakers' pride", "craft", 20, { { "tool", 0.2f } } };
		return r;
	}

	std::string LoadSocietyRules(const std::string& json, SocietyRules& live)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("society");
		if (section == j.end()) return {};
		if (!section->is_object()) return "society: expected an object";
		const Json& s = *section;
		SocietyRules rules = live;
		if (std::string e = OnlyKeys(s, "section", { "seed", "mutation", "life", "births", "genes", "traits", "relations", "culture" }); !e.empty()) return e;
		if (const auto seed = s.find("seed"); seed != s.end())
		{
			if (!seed->is_number_integer() || seed->get<int64_t>() < 0) return "society: seed must be a whole number of 0 or more";
			rules.Seed = static_cast<uint32_t>(seed->get<int64_t>());
		}
		if (std::string e = ReadUnit(s, "section", "mutation", rules.Mutation); !e.empty()) return e;

		if (const auto l = s.find("life"); l != s.end())
		{
			if (!l->is_object()) return "society: life must be an object";
			rules.Life = LifeRules();
			if (std::string e = ReadNumbers(*l, "life", kLifeFields, rules.Life, { "heavy", "childRole" }); !e.empty()) return e;
			if (const auto h = l->find("heavy"); h != l->end())
			{
				if (!h->is_array()) return "society: life heavy must be an array of names";
				rules.Life.Heavy.clear();
				for (const Json& k : *h)
				{
					if (!k.is_string() || k.get<std::string>().empty()) return "society: life heavy must be an array of names";
					rules.Life.Heavy.push_back(k.get<std::string>());
				}
			}
			if (const auto c = l->find("childRole"); c != l->end())
			{
				if (!c->is_string()) return "society: life childRole must be a role name";
				rules.Life.ChildRole = c->get<std::string>();
			}
			const LifeRules& life = rules.Life;
			if (life.DaysPerYear <= 0.0f) return "society: life daysPerYear must be above 0";
			if (!(life.ChildUntil < life.ElderFrom && life.ElderFrom < life.MaxAge)) return "society: life must have childUntil < elderFrom < maxAge";
			if (life.FounderMin > life.FounderMax) return "society: life founderMin must not be above founderMax";
			if (life.ChildHeight <= 0.0f || life.ChildHeight > 1.0f) return "society: life childHeight must be above 0 and at most 1";
		}

		if (const auto b = s.find("births"); b != s.end())
		{
			if (!b->is_object()) return "society: births must be an object";
			rules.Births = BirthRules();
			if (std::string e = ReadNumbers(*b, "births", kBirthFields, rules.Births, { "pairMin", "needsFire", "maxMembers" }); !e.empty()) return e;
			if (const auto p = b->find("pairMin"); p != b->end())
			{
				if (!p->is_number() || p->get<float>() < -1.0f || p->get<float>() > 1.0f) return "society: births pairMin must be a number from -1 to 1";
				rules.Births.PairMin = p->get<float>();
			}
			if (const auto f = b->find("needsFire"); f != b->end())
			{
				if (!f->is_boolean()) return "society: births needsFire must be true or false";
				rules.Births.NeedsFire = f->get<bool>();
			}
			if (const auto m = b->find("maxMembers"); m != b->end())
			{
				if (!m->is_number_integer() || m->get<int>() < 0) return "society: births maxMembers must be a whole number of 0 or more";
				rules.Births.MaxMembers = m->get<int>();
			}
			if (rules.Births.Chance > 1.0f) return "society: births chance must be at most 1";
			if (rules.Births.MinAge > rules.Births.MaxAge) return "society: births minAge must not be above maxAge";
		}

		if (const auto g = s.find("genes"); g != s.end())
		{
			if (!g->is_object()) return "society: genes must be an object of gene -> its definition";
			rules.Genes.clear();
			for (const auto& [id, o] : g->items())
			{
				const std::string part = "gene " + id;
				if (!o.is_object()) return Fail(part, "must be an object");
				if (std::string e = OnlyKeys(o, part, { "mean", "spread", "effects", "learn", "pace" }); !e.empty()) return e;
				GeneDef d;
				if (std::string e = ReadUnit(o, part, "mean", d.Mean); !e.empty()) return e;
				if (std::string e = ReadUnit(o, part, "spread", d.Spread); !e.empty()) return e;
				if (std::string e = ReadMap(o, part, "effects", d.Effects, true); !e.empty()) return e;
				if (std::string e = ReadMap(o, part, "learn", d.Learn, true); !e.empty()) return e;
				if (std::string e = ReadMap(o, part, "pace", d.Pace, true); !e.empty()) return e;
				rules.Genes[id] = d;
			}
		}

		if (const auto t = s.find("traits"); t != s.end())
		{
			if (!t->is_object()) return "society: traits must be an object of trait -> its definition";
			rules.Traits.clear();
			for (const auto& [id, o] : t->items())
			{
				const std::string part = "trait " + id;
				if (!o.is_object()) return Fail(part, "must be an object");
				if (std::string e = OnlyKeys(o, part, { "mean", "spread", "heritability", "effects" }); !e.empty()) return e;
				TraitDef d;
				if (std::string e = ReadUnit(o, part, "mean", d.Mean); !e.empty()) return e;
				if (std::string e = ReadUnit(o, part, "spread", d.Spread); !e.empty()) return e;
				if (std::string e = ReadUnit(o, part, "heritability", d.Heritability); !e.empty()) return e;
				if (std::string e = ReadMap(o, part, "effects", d.Effects, true); !e.empty()) return e;
				rules.Traits[id] = d;
			}
		}
		for (const auto& [id, t] : rules.Traits)
			if (rules.Genes.count(id)) return "society: " + id + " is both a gene and a trait (a trait has a gene of its own name)";

		if (const auto r = s.find("relations"); r != s.end())
		{
			if (!r->is_object()) return "society: relations must be an object";
			rules.Relations = RelationRules();
			if (std::string e = ReadNumbers(*r, "relations", kRelationFields, rules.Relations, { "events", "maxPerMember", "campNearest" }); !e.empty()) return e;
			if (std::string e = ReadMap(*r, "relations", "events", rules.Relations.Events, true); !e.empty()) return e;
			for (const char* key : { "maxPerMember", "campNearest" })
				if (const auto n = r->find(key); n != r->end())
				{
					if (!n->is_number_integer() || n->get<int>() < 0) return std::string("society: relations ") + key + " must be a whole number of 0 or more";
					(std::string(key) == "maxPerMember" ? rules.Relations.MaxPerMember : rules.Relations.CampNearest) = n->get<int>();
				}
			if (rules.Relations.DecayPerDay > 1.0f) return "society: relations decayPerDay must be at most 1";
			if (rules.Relations.CampSeconds <= 0.0f) return "society: relations campSeconds must be above 0";
		}

		if (const auto c = s.find("culture"); c != s.end())
		{
			if (!c->is_object()) return "society: culture must be an object";
			if (std::string e = OnlyKeys(*c, "culture", { "drift", "norms", "traditions" }); !e.empty()) return e;
			rules.Culture = CultureRules();
			if (std::string e = ReadUnit(*c, "culture", "drift", rules.Culture.Drift); !e.empty()) return e;
			if (const auto n = c->find("norms"); n != c->end())
			{
				if (!n->is_object()) return "society: culture norms must be an object of norm -> its definition";
				for (const auto& [id, o] : n->items())
				{
					const std::string part = "norm " + id;
					if (!o.is_object()) return Fail(part, "must be an object");
					if (std::string e = OnlyKeys(o, part, { "start", "trait", "effects" }); !e.empty()) return e;
					NormDef d;
					if (std::string e = ReadUnit(o, part, "start", d.Start); !e.empty()) return e;
					if (const auto tr = o.find("trait"); tr != o.end())
					{
						if (!tr->is_string()) return Fail(part, "trait must be the name of a trait");
						d.Trait = tr->get<std::string>();
					}
					if (std::string e = ReadMap(o, part, "effects", d.Effects, true); !e.empty()) return e;
					rules.Culture.Norms[id] = d;
				}
			}
			if (const auto tr = c->find("traditions"); tr != c->end())
			{
				if (!tr->is_object()) return "society: culture traditions must be an object of id -> its definition";
				for (const auto& [id, o] : tr->items())
				{
					const std::string part = "tradition " + id;
					if (!o.is_object()) return Fail(part, "must be an object");
					if (std::string e = OnlyKeys(o, part, { "name", "event", "count", "effects" }); !e.empty()) return e;
					TraditionDef d;
					d.Name = o.value("name", id);
					if (!o.contains("event") || !o["event"].is_string() || o["event"].get<std::string>().empty()) return Fail(part, "needs the event that founds it");
					d.Event = o["event"].get<std::string>();
					if (const auto n = o.find("count"); n != o.end())
					{
						if (!n->is_number_integer() || n->get<int>() < 1) return Fail(part, "count must be a whole number of 1 or more");
						d.Count = n->get<int>();
					}
					if (std::string e = ReadMap(o, part, "effects", d.Effects, true); !e.empty()) return e;
					rules.Culture.Traditions[id] = d;
				}
			}
		}
		for (const auto& [id, n] : rules.Culture.Norms)
			if (!n.Trait.empty() && !rules.Traits.count(n.Trait)) return "society: the norm " + id + " follows the trait " + n.Trait + ", which is not a trait";
		live = std::move(rules);
		return {};
	}

	std::string LoadSocietyRulesFile(const std::string& path, SocietyRules& rules)
	{
		std::ifstream f(path);
		if (!f) return "cannot open " + path;
		std::stringstream ss;
		ss << f.rdbuf();
		return LoadSocietyRules(ss.str(), rules);
	}

	std::string SocietyRulesToJson(const SocietyRules& r)
	{
		Json s;
		s["seed"] = r.Seed;
		s["mutation"] = Rounded(r.Mutation);
		Json life = Json::object();
		for (const LifeField& f : kLifeFields) life[f.Key] = Rounded(r.Life.*(f.Field));
		life["heavy"] = r.Life.Heavy;
		life["childRole"] = r.Life.ChildRole;
		s["life"] = life;
		Json births = Json::object();
		for (const BirthField& f : kBirthFields) births[f.Key] = Rounded(r.Births.*(f.Field));
		births["pairMin"] = Rounded(r.Births.PairMin);
		births["needsFire"] = r.Births.NeedsFire;
		births["maxMembers"] = r.Births.MaxMembers;
		s["births"] = births;
		Json genes = Json::object();
		for (const auto& [id, g] : r.Genes)
			genes[id] = { { "mean", Rounded(g.Mean) }, { "spread", Rounded(g.Spread) }, { "effects", WriteMap(g.Effects) }, { "learn", WriteMap(g.Learn) }, { "pace", WriteMap(g.Pace) } };
		s["genes"] = genes;
		Json traits = Json::object();
		for (const auto& [id, t] : r.Traits)
			traits[id] = { { "mean", Rounded(t.Mean) }, { "spread", Rounded(t.Spread) }, { "heritability", Rounded(t.Heritability) }, { "effects", WriteMap(t.Effects) } };
		s["traits"] = traits;
		Json rel = Json::object();
		for (const RelationField& f : kRelationFields) rel[f.Key] = Rounded(r.Relations.*(f.Field));
		rel["events"] = WriteMap(r.Relations.Events);
		rel["maxPerMember"] = r.Relations.MaxPerMember;
		rel["campNearest"] = r.Relations.CampNearest;
		s["relations"] = rel;
		Json norms = Json::object();
		for (const auto& [id, n] : r.Culture.Norms) norms[id] = { { "start", Rounded(n.Start) }, { "trait", n.Trait }, { "effects", WriteMap(n.Effects) } };
		Json traditions = Json::object();
		for (const auto& [id, t] : r.Culture.Traditions) traditions[id] = { { "name", t.Name }, { "event", t.Event }, { "count", t.Count }, { "effects", WriteMap(t.Effects) } };
		s["culture"] = { { "drift", Rounded(r.Culture.Drift) }, { "norms", norms }, { "traditions", traditions } };
		return s.dump();
	}
}
