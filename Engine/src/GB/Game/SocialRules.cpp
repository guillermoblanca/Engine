// The "social" section of a content file (docs/VIDA_SOCIAL.md): the data of GB/Game/Social.h, its defaults, read and written. Atomic: the rules
// are read into a copy and only replace the live ones when the whole section is right. Every part is a list of fields (key, kind, address,
// range) read and written by the same code, as the "combat" section does (GB/Gameplay/Combat.cpp); the entries of a map (talk kinds, rites,
// belief systems, taboos, omens, foods, traps) are parts of their own.
#include "GB/Game/Social.h"
#include "GB/Gameplay/ShippedContent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	SocialRules DefaultSocialRules()
	{
		// the "social" section of the game's data (rules/social.json: mood, talk, rites, beliefs, the shaman, hunting and traps, and the diet). Not
		// a second copy here (T347: the copy's diet had 16 foods of the data's 29 until T346, and its rites, talk and beliefs reached only the tests).
		static const SocialRules rules = ShippedSection<SocialRules>([](const std::string& doc, SocialRules& r) { return LoadSocialRules(doc, r); });
		return rules;
	}

	// ---- JSON
	namespace
	{
		std::string Num(float v) { char b[32]; std::snprintf(b, sizeof b, "%g", static_cast<double>(v)); return b; }
		double Rounded(float v) { return std::round(static_cast<double>(v) * 10000.0) / 10000.0; }

		// A field of a part: its key, how it is read and how it is written.
		struct Field
		{
			std::string Key;
			std::function<std::string(const Json&, const std::string& where)> Read;
			std::function<Json()> Write;
		};
		Field Fl(const char* k, float& v, float lo, float hi)
		{
			return { k, [&v, lo, hi, k](const Json& j, const std::string& where) -> std::string
			{
				if (!j.is_number() || !(j.get<float>() >= lo && j.get<float>() <= hi)) return where + " " + k + " must be a number from " + Num(lo) + " to " + Num(hi);
				v = j.get<float>();
				return {};
			}, [&v] { return Json(Rounded(v)); } };
		}
		Field In(const char* k, int& v, int lo, int hi)
		{
			return { k, [&v, lo, hi, k](const Json& j, const std::string& where) -> std::string
			{
				if (!j.is_number_integer() || j.get<int>() < lo || j.get<int>() > hi) return where + " " + k + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi);
				v = j.get<int>();
				return {};
			}, [&v] { return Json(v); } };
		}
		Field Tx(const char* k, std::string& v)
		{
			return { k, [&v, k](const Json& j, const std::string& where) -> std::string
			{
				if (!j.is_string()) return where + " " + k + " must be a text";
				v = j.get<std::string>();
				return {};
			}, [&v] { return Json(v); } };
		}
		Field Bo(const char* k, bool& v)
		{
			return { k, [&v, k](const Json& j, const std::string& where) -> std::string
			{
				if (!j.is_boolean()) return where + " " + k + " must be true or false";
				v = j.get<bool>();
				return {};
			}, [&v] { return Json(v); } };
		}
		// name -> number (signed or not)
		Field Nm(const char* k, std::map<std::string, float>& v, bool signedValues)
		{
			return { k, [&v, k, signedValues](const Json& j, const std::string& where) -> std::string
			{
				if (!j.is_object()) return where + " " + k + " must be an object of name -> number";
				std::map<std::string, float> out;
				for (const auto& [id, n] : j.items())
				{
					if (id.empty() || !n.is_number() || (!signedValues && n.get<float>() < 0.0f) || std::abs(n.get<float>()) > 1000.0f)
						return where + " " + k + ": " + id + " must be a number" + (signedValues ? "" : " of 0 or more");
					out[id] = n.get<float>();
				}
				v = std::move(out);
				return {};
			}, [&v] { Json o = Json::object(); for (const auto& [id, n] : v) o[id] = Rounded(n); return o; } };
		}
		// name -> whole number of 0 or more
		Field Ni(const char* k, std::map<std::string, int>& v)
		{
			return { k, [&v, k](const Json& j, const std::string& where) -> std::string
			{
				if (!j.is_object()) return where + " " + k + " must be an object of name -> whole number";
				std::map<std::string, int> out;
				for (const auto& [id, n] : j.items())
				{
					if (id.empty() || !n.is_number_integer() || n.get<int>() < 0 || n.get<int>() > 1000) return where + " " + k + ": " + id + " must be a whole number from 0 to 1000";
					out[id] = n.get<int>();
				}
				v = std::move(out);
				return {};
			}, [&v] { Json o = Json::object(); for (const auto& [id, n] : v) o[id] = n; return o; } };
		}

		std::string ReadFields(const Json& o, std::vector<Field> fields, const std::string& where)
		{
			if (!o.is_object()) return where + " must be an object";
			for (const auto& [key, v] : o.items())
			{
				const auto f = std::find_if(fields.begin(), fields.end(), [&](const Field& x) { return x.Key == key; });
				if (f == fields.end()) return where + " has no " + key;
				if (std::string e = f->Read(v, where); !e.empty()) return e;
			}
			return {};
		}
		Json WriteFields(const std::vector<Field>& fields)
		{
			Json o = Json::object();
			for (const Field& f : fields) o[f.Key] = f.Write();
			return o;
		}

		std::vector<Field> Fields(TalkKind& k)
		{
			return { Fl("seconds", k.Seconds, 0.5f, 600.0f), Fl("relation", k.Relation, -1.0f, 1.0f), Fl("mood", k.Mood, -1.0f, 1.0f), Fl("company", k.Company, 0.0f, 100.0f),
				Fl("grief", k.Grief, -1.0f, 1.0f), Fl("teach", k.Teach, 0.0f, 1.0f), Fl("third", k.Third, -1.0f, 1.0f), Tx("event", k.Event), Tx("tech", k.Tech) };
		}
		std::vector<Field> Fields(RiteDef& d)
		{
			return { Tx("name", d.Name), Tx("event", d.Event), Tx("season", d.Season), Fl("at", d.At, 0.0f, 1.0f), Fl("every", d.Every, 0.0f, 1000.0f), Tx("place", d.Place),
				Fl("seconds", d.Seconds, 1.0f, 3600.0f), Fl("gatherSeconds", d.GatherSeconds, 0.0f, 3600.0f), Fl("quorum", d.Quorum, 0.0f, 1.0f), Fl("delay", d.Delay, 0.0f, 3600.0f),
				Ni("cost", d.Cost), Tx("tech", d.Tech), Bo("shaman", d.Shaman), Fl("mood", d.Mood, -1.0f, 1.0f), Fl("relation", d.Relation, -1.0f, 1.0f),
				Fl("grief", d.Grief, -1.0f, 1.0f), Fl("heal", d.Heal, 0.0f, 3600.0f), Nm("norms", d.Norms, true), Nm("beliefs", d.Beliefs, true), Tx("gesture", d.Gesture) };
		}
		std::vector<Field> Fields(BeliefSystem& s) { return { Tx("name", s.Name), Fl("start", s.Start, 0.0f, 1.0f), Fl("spread", s.Spread, 0.0f, 1.0f) }; }
		std::vector<Field> Fields(TabooDef& t) { return { Tx("name", t.Name), Tx("system", t.System), Tx("kind", t.Kind), Fl("radius", t.Radius, 0.0f, 1000.0f) }; }
		std::vector<Field> Fields(OmenDef& o)
		{
			return { Tx("name", o.Name), Tx("event", o.Event), Fl("chance", o.Chance, 0.0f, 1.0f), Fl("mood", o.Mood, -1.0f, 1.0f), Nm("beliefs", o.Beliefs, true), Tx("rite", o.Rite) };
		}
		std::vector<Field> Fields(FoodDef& f) { return { Tx("group", f.Group), Fl("nutrition", f.Nutrition, 0.0f, 100.0f) }; }
		std::vector<Field> Fields(TrapDef& t)
		{
			return { Tx("name", t.Name), Tx("tag", t.Tag), Ni("cost", t.Cost), Tx("tech", t.Tech), Fl("arm", t.Arm, 0.0f, 1e5f), Fl("radius", t.Radius, 0.1f, 100.0f),
				Fl("seconds", t.Seconds, 0.0f, 600.0f), In("max", t.Max, 0, 50), Fl("priority", t.Priority, 0.0f, 10.0f), Ni("yields", t.Yields) };
		}

		// A map of entries read whole by id (each a part of its own); `merge` keeps the entries it does not name.
		template <class T>
		Field Entries(const char* k, std::map<std::string, T>& v, bool merge = false)
		{
			return { k, [&v, k, merge](const Json& j, const std::string& where) -> std::string
			{
				if (!j.is_object()) return where + " " + k + " must be an object of id -> entry";
				std::map<std::string, T> out = merge ? v : std::map<std::string, T>();
				for (const auto& [id, e] : j.items())
				{
					if (id.empty()) return where + " " + k + " has an entry with no id";
					T fresh;
					if (std::string err = ReadFields(e, Fields(fresh), where + " " + k + " " + id); !err.empty()) return err;
					out[id] = std::move(fresh);
				}
				v = std::move(out);
				return {};
			}, [&v] { Json o = Json::object(); for (auto& [id, e] : v) { T copy = e; o[id] = WriteFields(Fields(copy)); } return o; } };
		}

		std::vector<Field> Fields(MoodRules& m)
		{
			return { Fl("start", m.Start, 0.0f, 1.0f), Fl("rest", m.Rest, 0.0f, 1.0f), Fl("return", m.Return, 0.0f, 1.0f), Fl("grief", m.Grief, 0.0f, 1.0f),
				Nm("effects", m.Effects, true), Fl("low", m.Low, 0.0f, 1.0f), Fl("high", m.High, 0.0f, 1.0f) };
		}
		std::vector<Field> Fields(GriefRules& g)
		{
			return { Fl("kin", g.Kin, 0.0f, 1.0f), Fl("friend", g.Friend, 0.0f, 1.0f), Fl("decayPerDay", g.DecayPerDay, 0.0f, 1.0f), Fl("mood", g.Mood, -1.0f, 1.0f) };
		}
		std::vector<Field> Fields(TalkRules& t)
		{
			return { Fl("radius", t.Radius, 0.5f, 200.0f), Fl("score", t.Score, 0.0f, 10.0f), Fl("want", t.Want, 0.0f, 100.0f), Fl("cooldown", t.Cooldown, 0.0f, 1e5f),
				Fl("greetAfter", t.GreetAfter, 0.0f, 1e6f), Fl("groomAbove", t.GroomAbove, -1.0f, 1.0f), Fl("settleBelow", t.SettleBelow, -1.0f, 1.0f),
				Fl("consoleAbove", t.ConsoleAbove, 0.0f, 1.0f), Fl("storyAge", t.StoryAge, 0.0f, 200.0f), Fl("storySociability", t.StorySociability, 0.0f, 1.0f),
				Fl("gossipSociability", t.GossipSociability, 0.0f, 1.0f), Fl("hungerBelow", t.HungerBelow, 0.0f, 100.0f), Fl("evening", t.Evening, 0.0f, 10.0f),
				Entries("kinds", t.Kinds) };
		}
		std::vector<Field> Fields(BeliefRules& b)
		{
			return { Entries("systems", b.Systems), Fl("drift", b.Drift, 0.0f, 1.0f), Fl("spread", b.Spread, 0.0f, 1.0f), Fl("trust", b.Trust, 0.0f, 1.0f),
				Fl("holdAbove", b.HoldAbove, 0.0f, 1.0f), Fl("sacredDistance", b.SacredDistance, 0.0f, 1000.0f), Entries("taboos", b.Taboos), Entries("omens", b.Omens) };
		}
		std::vector<Field> Fields(ShamanRules& s)
		{
			return { Nm("traits", s.Traits, true), Fl("age", s.Age, 0.0f, 1.0f), Fl("rites", s.Rites, 0.0f, 1.0f), Fl("minAge", s.MinAge, 0.0f, 200.0f), Fl("bonus", s.Bonus, 0.0f, 10.0f) };
		}
		std::vector<Field> Fields(DietRules& d)
		{
			return { In("window", d.Window, 1, 50), In("varied", d.Varied, 1, 50), Fl("bonus", d.Bonus, -1.0f, 1.0f), Fl("health", d.Health, 0.0f, 100.0f),
				Fl("monotony", d.Monotony, -1.0f, 1.0f), Entries("foods", d.Foods) };
		}
		std::vector<Field> Fields(HuntingRules& h) { return { Ni("wildlife", h.Wildlife), Entries("traps", h.Traps), Fl("short", h.Short, 0.0f, 100.0f) }; }
	}

	namespace
	{
		// A part that replaces the live one whole: read into its default (what the part does not say is the default).
		template <class T>
		std::string ReadPart(const Json& s, const char* key, T fresh, T& into)
		{
			const auto it = s.find(key);
			if (it == s.end()) return {};
			if (std::string e = ReadFields(*it, Fields(fresh), std::string("social: ") + key); !e.empty()) return e;
			into = std::move(fresh);
			return {};
		}
	}

	std::string LoadSocialRules(const std::string& json, SocialRules& live)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto it = j.find("social");
		if (it == j.end()) return {};
		const Json& s = *it;
		if (!s.is_object()) return "social: expected an object";
		static const char* kKeys[] = { "on", "seed", "stepSeconds", "mood", "grief", "talk", "rites", "beliefs", "shaman", "diet", "hunting" };
		for (const auto& [key, v] : s.items())
			if (std::find_if(std::begin(kKeys), std::end(kKeys), [&](const char* k) { return key == k; }) == std::end(kKeys)) return "social: unknown part " + key;
		SocialRules r = live;
		const SocialRules def{};   // (what a part does not say is the value of the structure: T347, the data gives every field it uses)
		r.On = true;   // (a content file with the section turns the social life on, unless it says "on": false)
		if (s.contains("on"))
		{
			if (!s["on"].is_boolean()) return "social: on must be true or false";
			r.On = s["on"].get<bool>();
		}
		if (s.contains("seed"))
		{
			if (!s["seed"].is_number_integer() || s["seed"].get<int64_t>() < 0 || s["seed"].get<int64_t>() > 0xffffffffll) return "social: seed must be a whole number from 0";
			r.Seed = static_cast<uint32_t>(s["seed"].get<int64_t>());
		}
		if (s.contains("stepSeconds"))
		{
			if (!s["stepSeconds"].is_number() || s["stepSeconds"].get<float>() < 0.1f || s["stepSeconds"].get<float>() > 600.0f) return "social: stepSeconds must be a number from 0.1 to 600";
			r.StepSeconds = s["stepSeconds"].get<float>();
		}
		std::string e;
		if (!(e = ReadPart(s, "mood", def.Mood, r.Mood)).empty() || !(e = ReadPart(s, "grief", def.Grief, r.Grief)).empty()
			|| !(e = ReadPart(s, "talk", def.Talk, r.Talk)).empty() || !(e = ReadPart(s, "beliefs", def.Beliefs, r.Beliefs)).empty()
			|| !(e = ReadPart(s, "shaman", def.Shaman, r.Shaman)).empty() || !(e = ReadPart(s, "diet", def.Diet, r.Diet)).empty()
			|| !(e = ReadPart(s, "hunting", def.Hunting, r.Hunting)).empty()) return e;
		if (s.contains("rites"))
			if (std::string err = Entries("rites", r.Rites, true).Read(s["rites"], "social:"); !err.empty()) return err;
		// across the parts
		for (const auto& [id, d] : r.Rites)
		{
			if (d.Event.empty() && d.Season.empty()) return "social: rite " + id + " has neither an event nor a season";
			if (!d.Season.empty() && d.Season != "spring" && d.Season != "summer" && d.Season != "autumn" && d.Season != "winter") return "social: rite " + id + " has an unknown season " + d.Season;
			if (d.Place != "fire" && d.Place != "totem" && d.Place != "sacred" && d.Place != "camp") return "social: rite " + id + " is held at fire, totem, sacred or camp, not " + d.Place;
			for (const auto& [sys, w] : d.Beliefs) if (!r.Beliefs.Systems.count(sys)) return "social: rite " + id + " honours " + sys + ", which is not a belief system";
		}
		for (const auto& [id, t] : r.Beliefs.Taboos)
		{
			if (!r.Beliefs.Systems.count(t.System)) return "social: taboo " + id + " belongs to " + t.System + ", which is not a belief system";
			if (t.Kind != "sacred_hunt" && t.Kind != "share_kill") return "social: taboo " + id + " kind must be sacred_hunt or share_kill";
		}
		for (const auto& [id, o] : r.Beliefs.Omens)
		{
			if (o.Event.empty()) return "social: omen " + id + " has no event";
			if (!o.Rite.empty() && !r.Rites.count(o.Rite)) return "social: omen " + id + " calls " + o.Rite + ", which is not a rite";
			for (const auto& [sys, w] : o.Beliefs) if (!r.Beliefs.Systems.count(sys)) return "social: omen " + id + " moves " + sys + ", which is not a belief system";
		}
		for (const auto& [id, t] : r.Hunting.Traps) if (t.Tag.empty()) return "social: trap " + id + " needs the tag of its animal (Animal.Rabbit)";
		live = std::move(r);
		return {};
	}

	std::string LoadSocialRulesFile(const std::string& path, SocialRules& rules)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return "cannot open " + path;
		std::stringstream ss;
		ss << f.rdbuf();
		return LoadSocialRules(ss.str(), rules);
	}

	std::string SocialRulesToJson(const SocialRules& rules)
	{
		SocialRules r = rules;   // (the fields take addresses: of a copy)
		Json o;
		o["on"] = r.On;
		o["seed"] = r.Seed;
		o["stepSeconds"] = Rounded(r.StepSeconds);
		o["mood"] = WriteFields(Fields(r.Mood));
		o["grief"] = WriteFields(Fields(r.Grief));
		o["talk"] = WriteFields(Fields(r.Talk));
		o["rites"] = Entries("rites", r.Rites).Write();
		o["beliefs"] = WriteFields(Fields(r.Beliefs));
		o["shaman"] = WriteFields(Fields(r.Shaman));
		o["diet"] = WriteFields(Fields(r.Diet));
		o["hunting"] = WriteFields(Fields(r.Hunting));
		return o.dump(2);
	}
}
