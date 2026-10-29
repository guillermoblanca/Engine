#include "GB/Gameplay/Combat.h"
#include "GB/AI/Needs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr const char* kHealth = "Health";
		constexpr const char* kStamina = "Stamina";
		constexpr const char* kHitEvent = "Event.Combat.Hit";
		constexpr size_t kMaxEvents = 400;

		bool TagUnder(const std::string& tag, const std::string& parent)
		{
			return !parent.empty() && (tag == parent || (tag.size() > parent.size() && tag.compare(0, parent.size(), parent) == 0 && tag[parent.size()] == '.'));
		}

		uint64_t Mix(uint64_t h)
		{
			h ^= h >> 33; h *= 0xff51afd7ed558ccdull;
			h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull;
			h ^= h >> 33;
			return h;
		}

		uint64_t HashText(const std::string& s, uint64_t h)
		{
			for (const unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
			return h;
		}

		std::string Num(float v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%g", static_cast<double>(v));
			return b;
		}
	}

	double CombatRoll(uint32_t seed, const std::string& a, const std::string& b, uint64_t n)
	{
		uint64_t h = HashText(a, 1469598103934665603ull ^ (static_cast<uint64_t>(seed) * 0x9e3779b97f4a7c15ull));
		h = HashText(b, Mix(h) ^ 0x51ed270b27f3a1c5ull);
		h = Mix(h ^ (n * 0x2545f4914f6cdd1dull + 0x632be59bd9b4e019ull));
		return static_cast<double>(h >> 11) / 9007199254740992.0;   // 53 bits: [0, 1)
	}

	const char* FightModeName(FightMode m)
	{
		switch (m)
		{
		case FightMode::Engage: return "fighting";
		case FightMode::Flee: return "fleeing";
		case FightMode::Dead: return "dead";
		default: return "calm";
		}
	}

	float Combatant::Severity() const
	{
		float s = 0.0f;
		for (const Wound& w : Wounds) s += w.Severity;
		return s;
	}

	// ------------------------------------------------------------------ the rules
	const WeaponDef& CombatRules::Weapon(const std::string& id) const
	{
		static const WeaponDef kBare;
		if (const auto it = Weapons.find(id); it != Weapons.end()) return it->second;
		if (const auto it = Weapons.find(Unarmed); it != Weapons.end()) return it->second;
		return kBare;
	}

	const PredatorRule* CombatRules::PredatorFor(const AbilitySystemComponent& a) const
	{
		for (const auto& [id, p] : Predators) if (!p.Tag.empty() && a.HasTag(p.Tag)) return &p;
		return nullptr;
	}

	const HuntRule* CombatRules::HuntFor(const AbilitySystemComponent& a) const
	{
		for (const auto& [id, h] : Hunts) if (!h.Tag.empty() && a.HasTag(h.Tag)) return &h;
		return nullptr;
	}

	CombatRules DefaultCombatRules()
	{
		CombatRules r;
		auto weapon = [](const char* tag, float damage, float reach, float seconds, float stamina, float wound, const char* kind, float power, std::vector<std::string> montages, float hitAt)
		{
			WeaponDef w;
			w.Tag = tag; w.Damage = damage; w.Reach = reach; w.Seconds = seconds; w.Stamina = stamina; w.Wound = wound; w.WoundKind = kind; w.Power = power;
			w.Montages = std::move(montages); w.HitAt = hitAt;
			return w;
		};
		// The montages are the sword and shield pack's (packs/full, names of assets/animation/mixamo.manifest.json); the packs have no clips of their
		// own for these weapons yet (docs/CONFLICTO.md): sword attacks for the fists (there are no punches in the X Bot packs), the sword slashes for an
		// axe, the sword attacks for a pick and a spear. The bite has no montage (the fox's body is drawn from primitives).
		r.Weapons["fists"] = weapon("", 4.0f, 1.1f, 1.1f, 5.0f, 0.05f, "bruise", 0.0f, { "sword_attack_1", "sword_attack_3" }, 0.4f);
		r.Weapons["axe"] = weapon("Item.Tool.Axe", 13.0f, 1.5f, 1.6f, 10.0f, 0.45f, "cut", 2.0f, { "sword_slash_1", "sword_slash_3" }, 0.45f);
		r.Weapons["pickaxe"] = weapon("Item.Tool.Pickaxe", 11.0f, 1.5f, 1.8f, 11.0f, 0.4f, "cut", 2.0f, { "sword_attack_4" }, 0.5f);
		r.Weapons["spear"] = weapon("Item.Weapon.Spear", 12.0f, 2.4f, 1.5f, 9.0f, 0.5f, "cut", 2.0f, { "sword_attack_2" }, 0.5f);
		r.Weapons["sword"] = weapon("Item.Weapon.Sword", 16.0f, 1.7f, 1.2f, 8.0f, 0.55f, "cut", 2.5f, { "sword_slash_1", "sword_slash_2", "sword_attack_3" }, 0.45f);
		r.Weapons["bow"] = weapon("Item.Weapon.Bow", 10.0f, 14.0f, 1.8f, 7.0f, 0.4f, "cut", 1.5f, { "sword_attack_2" }, 0.6f);   // (ranged: a long reach; the shot is the blow, no arrow in flight yet)
		r.Weapons["bite"] = weapon("", 6.0f, 1.0f, 1.2f, 6.0f, 0.5f, "bite", 0.0f, {}, 0.4f);
		PredatorRule fox;
		fox.Tag = "Animal.Fox";
		r.Predators["fox"] = fox;
		HuntRule rabbit;
		rabbit.Tag = "Animal.Rabbit";
		rabbit.Byproduct = "hide";   // the skin of the kill: what hide working is about (docs/PROGRESION.md)
		rabbit.ByproductUnits = 1;
		r.Hunts["rabbit"] = rabbit;
		return r;
	}

	// ---- JSON: every part is a list of fields (key, kind, address, range), read and written by the same code
	namespace
	{
		struct Field
		{
			const char* Key;
			enum Kind { F, I, S, B, L, M } Type;
			void* Ptr;
			float Lo = 0.0f, Hi = 0.0f;
		};
		Field Fl(const char* k, float& v, float lo, float hi) { return { k, Field::F, &v, lo, hi }; }
		Field In(const char* k, int& v, int lo, int hi) { return { k, Field::I, &v, static_cast<float>(lo), static_cast<float>(hi) }; }
		Field Tx(const char* k, std::string& v) { return { k, Field::S, &v }; }
		Field Bo(const char* k, bool& v) { return { k, Field::B, &v }; }
		Field Li(const char* k, std::vector<std::string>& v) { return { k, Field::L, &v }; }
		Field Ma(const char* k, std::map<std::string, int>& v) { return { k, Field::M, &v }; }   // id -> whole number (0..1000)

		std::vector<Field> Fields(WeaponDef& w)
		{
			return { Tx("tag", w.Tag), Fl("damage", w.Damage, 0.0f, 1000.0f), Fl("reach", w.Reach, 0.1f, 50.0f), Fl("seconds", w.Seconds, 0.1f, 60.0f),
				Fl("stamina", w.Stamina, 0.0f, 1000.0f), Fl("wound", w.Wound, 0.0f, 1.0f), Tx("woundKind", w.WoundKind), Fl("power", w.Power, 0.0f, 100.0f),
				Li("montages", w.Montages), Fl("hitAt", w.HitAt, 0.0f, 1.0f) };
		}
		std::vector<Field> Fields(HitRules& h)
		{
			return { Fl("base", h.Base, 0.0f, 1.0f), Fl("dexterity", h.Dexterity, 0.0f, 5.0f), Fl("stamina", h.Stamina, 0.0f, 1.0f), Fl("min", h.Min, 0.0f, 1.0f),
				Fl("max", h.Max, 0.0f, 1.0f), Fl("strength", h.Strength, 0.0f, 5.0f), Fl("endurance", h.Endurance, 0.0f, 1.9f), Fl("blockChance", h.BlockChance, 0.0f, 1.0f),
				Fl("blockLets", h.BlockLets, 0.0f, 1.0f), Fl("blockStamina", h.BlockStamina, 0.0f, 1000.0f), Fl("maxStamina", h.MaxStamina, 1.0f, 10000.0f),
				Fl("staminaRegen", h.StaminaRegen, 0.0f, 1000.0f), Fl("runStamina", h.RunStamina, 0.0f, 1000.0f) };
		}
		std::vector<Field> Fields(InjuryRules& i)
		{
			return { Fl("woundAt", i.WoundAt, 0.0f, 1000.0f), Fl("severity", i.Severity, 1.0f, 1000.0f), Fl("bleedSeconds", i.BleedSeconds, 0.0f, 1e5f),
				Fl("bleed", i.Bleed, 0.0f, 100.0f), Fl("healPerDay", i.HealPerDay, 0.0f, 100.0f), Fl("restFactor", i.RestFactor, 1.0f, 100.0f),
				Fl("careFactor", i.CareFactor, 1.0f, 100.0f), Fl("slow", i.Slow, 0.0f, 0.95f), Fl("lethal", i.Lethal, 0.1f, 100.0f),
				Fl("restAbove", i.RestAbove, 0.0f, 100.0f), Fl("careSeconds", i.CareSeconds, 1.0f, 1e4f) };
		}
		std::vector<Field> Fields(MoraleRules& m)
		{
			return { Fl("start", m.Start, 0.0f, 1.0f), Fl("boldness", m.Boldness, 0.0f, 1.0f), Fl("hurt", m.Hurt, 0.0f, 100.0f), Fl("allyDown", m.AllyDown, 0.0f, 1.0f),
				Fl("fleeBelow", m.FleeBelow, 0.0f, 1.0f), Fl("fleeHealth", m.FleeHealth, 0.0f, 100.0f), Fl("recover", m.Recover, 0.0f, 10.0f) };
		}
		std::vector<Field> Fields(ThreatRules& t)
		{
			return { Fl("hostile", t.Hostile, 0.0f, 1.0f), Fl("sight", t.Sight, 0.0f, 1000.0f), Fl("safety", t.Safety, 0.0f, 100.0f), Fl("alarm", t.Alarm, 0.0f, 1000.0f),
				Fl("groupRadius", t.GroupRadius, 0.0f, 1000.0f), Fl("fleeDistance", t.FleeDistance, 1.0f, 1000.0f), Fl("safeDistance", t.SafeDistance, 1.0f, 1000.0f),
				Fl("forgetSeconds", t.ForgetSeconds, 0.0f, 1e5f), Fl("bold", t.Bold, 0.0f, 100.0f), Fl("health", t.Health, 0.0f, 100.0f), Fl("odds", t.Odds, 0.0f, 100.0f),
				Fl("kin", t.Kin, 0.0f, 100.0f), Fl("camp", t.Camp, 0.0f, 100.0f), Fl("walls", t.Walls, 0.0f, 100.0f), Fl("fear", t.Fear, 0.0f, 100.0f), Fl("timid", t.Timid, 0.0f, 100.0f),
				Bo("childrenFight", t.ChildrenFight) };
		}
		std::vector<Field> Fields(PredatorRule& p)
		{
			return { Tx("tag", p.Tag), Tx("prey", p.Prey), Fl("weakHealth", p.WeakHealth, 0.0f, 100.0f), Fl("alone", p.Alone, 0.0f, 1000.0f), Fl("hungry", p.Hungry, 0.0f, 1000.0f),
				Fl("sight", p.Sight, 0.0f, 1000.0f), Tx("weapon", p.Weapon), Fl("health", p.Health, 1.0f, 10000.0f), Fl("speed", p.Speed, 0.1f, 100.0f),
				Fl("boldness", p.Boldness, 0.0f, 1.0f), Fl("meal", p.Meal, 0.0f, 1000.0f), Fl("giveUp", p.GiveUp, 1.0f, 1000.0f) };
		}
		std::vector<Field> Fields(HuntRule& h)
		{
			return { Tx("tag", h.Tag), Tx("item", h.Item), In("units", h.Units, 0, 1000), Tx("byproduct", h.Byproduct), In("byproductUnits", h.ByproductUnits, 0, 1000), Fl("health", h.Health, 1.0f, 10000.0f), Fl("speed", h.Speed, 0.1f, 100.0f),
				Tx("weapon", h.Weapon), Bo("fightsBack", h.FightsBack), Tx("toolTag", h.ToolTag), Fl("priority", h.Priority, 0.0f, 100.0f),
				Fl("range", h.Range, 0.0f, 10000.0f), In("hunters", h.Hunters, 1, 20), Ma("yields", h.Yields), In("group", h.Group, 1, 20) };
		}
		std::vector<Field> Fields(RaidRules& r)
		{
			return { Fl("every", r.Every, 1.0f, 1e6f), Fl("cooldown", r.Cooldown, 0.0f, 1e7f), Fl("want", r.Want, 0.0f, 100.0f), Fl("bold", r.Bold, 0.0f, 100.0f),
				Fl("hunger", r.Hunger, 0.0f, 100.0f), Fl("grievance", r.Grievance, 0.0f, 100.0f), Fl("hostility", r.Hostility, 0.0f, 100.0f), Fl("defense", r.Defense, 0.0f, 100.0f), In("party", r.Party, 1, 50),
				In("maxParty", r.MaxParty, 1, 50), Fl("partyHealth", r.PartyHealth, 0.0f, 100.0f), Fl("stealSeconds", r.StealSeconds, 0.0f, 1000.0f), In("carry", r.Carry, 0, 1000),
				Li("steal", r.Steal), Fl("retreat", r.Retreat, 0.0f, 1.0f), Fl("wrong", r.Wrong, 0.0f, 100.0f), Fl("forget", r.Forget, 0.0f, 100.0f),
				Fl("reach", r.Reach, 0.5f, 100.0f), Fl("maxSeconds", r.MaxSeconds, 1.0f, 1e7f) };
		}

		std::string Read(const Json& o, std::vector<Field> fields, const std::string& part)
		{
			if (!o.is_object()) return "combat: " + part + " must be an object";
			for (const auto& [key, v] : o.items())
			{
				const auto f = std::find_if(fields.begin(), fields.end(), [&](const Field& x) { return key == x.Key; });
				if (f == fields.end()) return "combat: " + part + " has no " + key;
				switch (f->Type)
				{
				case Field::F:
					if (!v.is_number() || !(v.get<float>() >= f->Lo && v.get<float>() <= f->Hi)) return "combat: " + part + " " + key + " must be a number from " + Num(f->Lo) + " to " + Num(f->Hi);
					*static_cast<float*>(f->Ptr) = v.get<float>();
					break;
				case Field::I:
					if (!v.is_number_integer() || v.get<int>() < static_cast<int>(f->Lo) || v.get<int>() > static_cast<int>(f->Hi))
						return "combat: " + part + " " + key + " must be a whole number from " + Num(f->Lo) + " to " + Num(f->Hi);
					*static_cast<int*>(f->Ptr) = v.get<int>();
					break;
				case Field::S:
					if (!v.is_string()) return "combat: " + part + " " + key + " must be a text";
					*static_cast<std::string*>(f->Ptr) = v.get<std::string>();
					break;
				case Field::B:
					if (!v.is_boolean()) return "combat: " + part + " " + key + " must be true or false";
					*static_cast<bool*>(f->Ptr) = v.get<bool>();
					break;
				case Field::L:
				{
					if (!v.is_array()) return "combat: " + part + " " + key + " must be a list of texts";
					std::vector<std::string> out;
					for (const Json& s : v)
					{
						if (!s.is_string() || s.get<std::string>().empty()) return "combat: " + part + " " + key + " must be a list of texts";
						out.push_back(s.get<std::string>());
					}
					*static_cast<std::vector<std::string>*>(f->Ptr) = std::move(out);
					break;
				}
				case Field::M:
				{
					if (!v.is_object()) return "combat: " + part + " " + key + " must be an object of id -> whole number";
					std::map<std::string, int> out;
					for (const auto& [id, n] : v.items())
					{
						if (id.empty() || !n.is_number_integer() || n.get<int>() < 0 || n.get<int>() > 1000) return "combat: " + part + " " + key + " must be an object of id -> whole number from 0 to 1000";
						out[id] = n.get<int>();
					}
					*static_cast<std::map<std::string, int>*>(f->Ptr) = std::move(out);
					break;
				}
				}
			}
			return {};
		}

		Json Write(const std::vector<Field>& fields)
		{
			Json o = Json::object();
			for (const Field& f : fields)
				switch (f.Type)
				{
				case Field::F: o[f.Key] = std::round(static_cast<double>(*static_cast<const float*>(f.Ptr)) * 10000.0) / 10000.0; break;
				case Field::I: o[f.Key] = *static_cast<const int*>(f.Ptr); break;
				case Field::S: o[f.Key] = *static_cast<const std::string*>(f.Ptr); break;
				case Field::B: o[f.Key] = *static_cast<const bool*>(f.Ptr); break;
				case Field::L: o[f.Key] = *static_cast<const std::vector<std::string>*>(f.Ptr); break;
				case Field::M: o[f.Key] = *static_cast<const std::map<std::string, int>*>(f.Ptr); break;
				}
			return o;
		}

		template <class T>
		Json WriteOf(const T& value)
		{
			T copy = value;   // (the field list takes addresses: of a copy, so the original stays const)
			return Write(Fields(copy));
		}

		// A part that replaces the default whole: what it does not say is the default.
		template <class T>
		std::string ReadPart(const Json& section, const char* key, T& out)
		{
			if (!section.contains(key)) return {};
			T fresh;
			if (std::string e = Read(section[key], Fields(fresh), key); !e.empty()) return e;
			out = fresh;
			return {};
		}

		// Entries added or replaced by id, each whole.
		template <class T>
		std::string ReadEntries(const Json& section, const char* key, std::map<std::string, T>& out)
		{
			if (!section.contains(key)) return {};
			const Json& o = section[key];
			if (!o.is_object()) return std::string("combat: ") + key + " must be an object of id -> entry";
			for (const auto& [id, v] : o.items())
			{
				if (id.empty()) return std::string("combat: ") + key + " has an entry with no id";
				T fresh;
				if (std::string e = Read(v, Fields(fresh), std::string(key) + " " + id); !e.empty()) return e;
				out[id] = fresh;
			}
			return {};
		}
	}

	std::string LoadCombatRules(const std::string& json, CombatRules& live)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto it = j.find("combat");
		if (it == j.end()) return {};
		const Json& s = *it;
		if (!s.is_object()) return "combat: expected an object";
		CombatRules r = live;
		for (const auto& [key, v] : s.items())
		{
			static const char* kKeys[] = { "seed", "unarmed", "weapons", "hits", "injuries", "morale", "threats", "predators", "hunts", "raids", "hostile" };
			if (std::find_if(std::begin(kKeys), std::end(kKeys), [&](const char* k) { return key == k; }) == std::end(kKeys)) return "combat: unknown part " + key;
			(void)v;
		}
		if (s.contains("seed"))
		{
			if (!s["seed"].is_number_integer() || s["seed"].get<int64_t>() < 0 || s["seed"].get<int64_t>() > 0xffffffffll) return "combat: seed must be a whole number from 0";
			r.Seed = static_cast<uint32_t>(s["seed"].get<int64_t>());
		}
		if (s.contains("unarmed"))
		{
			if (!s["unarmed"].is_string() || s["unarmed"].get<std::string>().empty()) return "combat: unarmed must be the id of a weapon";
			r.Unarmed = s["unarmed"].get<std::string>();
		}
		std::string e;
		if (!(e = ReadEntries(s, "weapons", r.Weapons)).empty() || !(e = ReadPart(s, "hits", r.Hits)).empty() || !(e = ReadPart(s, "injuries", r.Injuries)).empty()
			|| !(e = ReadPart(s, "morale", r.Morale)).empty() || !(e = ReadPart(s, "threats", r.Threats)).empty() || !(e = ReadEntries(s, "predators", r.Predators)).empty()
			|| !(e = ReadEntries(s, "hunts", r.Hunts)).empty() || !(e = ReadPart(s, "raids", r.Raids)).empty()) return e;
		if (s.contains("hostile"))
		{
			const Json& h = s["hostile"];
			if (!h.is_array()) return "combat: hostile must be a list of pairs [ a, b ]";
			r.Hostile.clear();
			for (const Json& p : h)
			{
				if (!p.is_array() || p.size() != 2 || !p[0].is_string() || !p[1].is_string() || p[0].get<std::string>().empty() || p[1].get<std::string>().empty())
					return "combat: hostile must be a list of pairs [ a, b ]";
				r.Hostile.emplace_back(p[0].get<std::string>(), p[1].get<std::string>());
			}
		}
		// across the parts
		if (r.Weapons.find(r.Unarmed) == r.Weapons.end()) return "combat: the unarmed weapon " + r.Unarmed + " is not a weapon";
		for (const auto& [id, w] : r.Weapons)
		{
			if (!w.Tag.empty() && !GameplayTag::IsValidTagName(w.Tag)) return "combat: weapon " + id + " has a bad tag " + w.Tag;
			if (w.Montages.size() > 16) return "combat: weapon " + id + " has too many montages";
		}
		for (const auto& [id, p] : r.Predators)
		{
			if (!GameplayTag::IsValidTagName(p.Tag)) return "combat: predator " + id + " needs a tag (Animal.Fox)";
			if (p.Prey != "child" && p.Prey != "weak" && p.Prey != "any" && p.Prey != "none") return "combat: predator " + id + " prey must be child, weak, any or none";
			if (r.Weapons.find(p.Weapon) == r.Weapons.end()) return "combat: predator " + id + " fights with " + p.Weapon + ", which is not a weapon";
		}
		for (const auto& [id, h] : r.Hunts)
		{
			if (!GameplayTag::IsValidTagName(h.Tag)) return "combat: hunt " + id + " needs a tag (Animal.Rabbit)";
			if (h.Item.empty()) return "combat: hunt " + id + " gives no item";
			if (r.Weapons.find(h.Weapon) == r.Weapons.end()) return "combat: hunt " + id + " fights back with " + h.Weapon + ", which is not a weapon";
			if (!h.ToolTag.empty() && !GameplayTag::IsValidTagName(h.ToolTag)) return "combat: hunt " + id + " has a bad tool tag " + h.ToolTag;
		}
		if (r.Raids.MaxParty < r.Raids.Party) return "combat: raids maxParty is less than party";
		live = std::move(r);
		return {};
	}

	std::string LoadCombatRulesFile(const std::string& path, CombatRules& rules)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return "cannot open " + path;
		std::stringstream ss;
		ss << f.rdbuf();
		return LoadCombatRules(ss.str(), rules);
	}

	std::string CombatRulesToJson(const CombatRules& r)
	{
		Json o;
		o["seed"] = r.Seed;
		o["unarmed"] = r.Unarmed;
		Json weapons = Json::object();
		for (const auto& [id, w] : r.Weapons) weapons[id] = WriteOf(w);
		o["weapons"] = weapons;
		o["hits"] = WriteOf(r.Hits);
		o["injuries"] = WriteOf(r.Injuries);
		o["morale"] = WriteOf(r.Morale);
		o["threats"] = WriteOf(r.Threats);
		Json predators = Json::object();
		for (const auto& [id, p] : r.Predators) predators[id] = WriteOf(p);
		o["predators"] = predators;
		Json hunts = Json::object();
		for (const auto& [id, h] : r.Hunts) hunts[id] = WriteOf(h);
		o["hunts"] = hunts;
		o["raids"] = WriteOf(r.Raids);
		Json hostile = Json::array();
		for (const auto& [a, b] : r.Hostile) hostile.push_back(Json::array({ a, b }));
		o["hostile"] = hostile;
		return o.dump(2);
	}

	std::string CheckCombat(const CombatRules& rules, const ItemLibrary& items)
	{
		for (const auto& [id, h] : rules.Hunts)
		{
			if (!items.Find(h.Item)) return "combat: hunt " + id + " gives " + h.Item + ", which is not an item";
			if (!h.Byproduct.empty() && !items.Find(h.Byproduct)) return "combat: hunt " + id + " gives " + h.Byproduct + ", which is not an item";
			for (const auto& [item, n] : h.Yields)
				if (!items.Find(item)) return "combat: hunt " + id + " yields " + item + ", which is not an item";
		}
		return {};
	}

	// ------------------------------------------------------------------ the abilities
	void RegisterCombatContent(GameplayLibrary& library, const CombatRules& rules)
	{
		// Wounds slow the body in three steps (the effects scale Vigor like the moods of the needs do).
		const float steps[3] = { 0.25f, 0.6f, 1.0f };
		const char* names[3] = { "Combat.Wounded.Light", "Combat.Wounded.Serious", "Combat.Wounded.Grave" };
		for (int i = 0; i < 3; ++i)
		{
			GameplayEffectDef e;
			e.Name = names[i];
			e.Policy = EffectPolicy::Infinite;
			e.Modifiers = { { NeedName::Vigor, ModOp::Multiply, std::max(0.05f, 1.0f - rules.Injuries.Slow * steps[i]) } };
			e.GrantedTags = { "State.Wounded", names[i] };
			library.AddEffect(e);
		}
		{
			GameplayEffectDef e;
			e.Name = "Combat.Fleeing";
			e.Policy = EffectPolicy::Duration;
			e.Duration = 6.0f;
			e.GrantedTags = { "State.Fleeing" };
			library.AddEffect(e);
		}
		library.AddEffect(MakeCostEffect("Combat.Cost.Block", kStamina, rules.Hits.BlockStamina));
		for (const auto& [id, w] : rules.Weapons)
		{
			library.AddEffect(MakeCostEffect("Combat.Cost." + id, kStamina, w.Stamina));
			AbilityDef a;
			a.Name = "Attack." + id;
			a.Tag = "Ability.Attack." + id;
			a.NeedsTarget = true;
			a.MoveToTarget = false;   // the combat brings it within reach (or its owner does): the swing is where it stands
			a.BlockedTags = { "State.Dead", "State.Attacking", "State.Staggered" };
			a.TargetBlockedTags = { "State.Dead" };
			a.Cost = "Combat.Cost." + id;
			a.Montages = w.Montages;
			a.Duration = std::max(0.3f, std::min(1.0f, w.Seconds * 0.7f));
			a.Events = { { w.Montages.empty() ? std::string() : std::string("hit"), w.HitAt, kHitEvent, {}, {} } };   // (no montage: the blow lands as the swing starts)
			a.ActiveTags = { "State.Attacking", "State.Busy" };
			a.Note = "a blow with the " + id;
			library.AddAbility(a);
		}
		AbilityDef block;
		block.Name = "Block";
		block.Tag = "Ability.Block";
		block.BlockedTags = { "State.Dead", "State.Attacking", "State.Blocking" };
		block.Cost = "Combat.Cost.Block";
		block.Montages = { "sword_block_1", "sword_block_2" };
		block.Duration = 0.9f;
		block.ActiveTags = { "State.Blocking" };
		block.Note = "blocks a blow";
		library.AddAbility(block);
		AbilityDef flee;
		flee.Name = "Flee";
		flee.Tag = "Ability.Flee";
		flee.BlockedTags = { "State.Dead" };
		flee.Duration = 0.1f;
		flee.ActivationEffects = { "Combat.Fleeing" };
		flee.Note = "turns and runs";
		library.AddAbility(flee);
		AbilityDef stagger;
		stagger.Name = "Stagger";
		stagger.Tag = "Ability.Stagger";
		stagger.BlockedTags = { "State.Dead", "State.Staggered" };
		stagger.Montages = { "sword_impact_1", "react_small_front", "sword_impact_2" };
		stagger.Duration = 0.45f;
		stagger.ActiveTags = { "State.Staggered" };
		stagger.Note = "reels from a blow";
		library.AddAbility(stagger);
	}

	std::string BestWeapon(const CombatRules& rules, const ItemLibrary& items, const Inventory& bag, float* power)
	{
		std::string best = rules.Unarmed;
		float bestDamage = rules.Weapon(rules.Unarmed).Damage, bestPower = 0.0f;
		for (const ItemSlot& s : bag.Stacks())
		{
			const ItemDef* d = items.Find(s.Id);
			if (!d || s.Count <= 0) continue;
			for (const auto& [id, w] : rules.Weapons)
			{
				if (w.Tag.empty()) continue;
				if (!std::any_of(d->Tags.begin(), d->Tags.end(), [&](const std::string& t) { return TagUnder(t, w.Tag); })) continue;
				const float damage = w.Damage + w.Power * static_cast<float>(d->Power);
				if (damage > bestDamage) { bestDamage = damage; best = id; bestPower = static_cast<float>(d->Power); }
			}
		}
		if (power) *power = bestPower;
		return best;
	}

	// ------------------------------------------------------------------ hostility
	void HostilityTable::Set(const std::string& a, const std::string& b, float value, bool both)
	{
		m_Table[{ a, b }] = std::clamp(value, 0.0f, 1.0f);
		if (both) m_Table[{ b, a }] = std::clamp(value, 0.0f, 1.0f);
	}

	void HostilityTable::Clear()
	{
		m_Table.clear();
		m_Grievances.clear();
		m_Predators.clear();
		m_Camps.clear();
	}

	void HostilityTable::FromRules(const CombatRules& rules)
	{
		for (const auto& [a, b] : rules.Hostile) Set(a, b, 1.0f);
		m_Predators.clear();
		for (const auto& [id, p] : rules.Predators) if (p.Prey != "none") m_Predators.push_back(p.Tag);
	}

	float HostilityTable::Hostility(const std::string& a, const std::string& b) const
	{
		if (a == b) return 0.0f;
		if (const auto it = m_Table.find({ a, b }); it != m_Table.end() && it->second > 0.0f) return it->second;
		float h = 0.0f;
		if (std::find(m_Predators.begin(), m_Predators.end(), a) != m_Predators.end()) h = 1.0f;
		if (const auto g = m_Grievances.find({ a, b }); g != m_Grievances.end()) h = std::max(h, std::min(1.0f, g->second * GrievanceHostility));
		if (const auto it = m_Table.find({ a, b }); it != m_Table.end() && h == 0.0f) h = it->second;
		return h;
	}

	std::vector<RaidTarget> HostilityTable::RaidTargets(const std::string& raider) const
	{
		std::vector<RaidTarget> out;
		for (const auto& [clan, t] : m_Camps)
			if (clan != raider && t.Store && Hostility(raider, clan) > 0.0f) out.push_back(t);
		return out;
	}

	void HostilityTable::Settled(const std::string& clan, const glm::vec2& camp, Inventory* store, float defense)
	{
		m_Camps[clan].Defense = defense;
		RaidTarget& t = m_Camps[clan];
		t.Clan = clan;
		t.Camp = camp;
		t.Store = store;
	}

	void HostilityTable::Wronged(const std::string& victim, const std::string& culprit, const std::string& what, float weight)
	{
		(void)what;
		if (victim == culprit || weight <= 0.0f) return;
		m_Grievances[{ victim, culprit }] += weight;
	}

	float HostilityTable::Grievance(const std::string& victim, const std::string& culprit) const
	{
		const auto it = m_Grievances.find({ victim, culprit });
		return it == m_Grievances.end() ? 0.0f : it->second;
	}

	void HostilityTable::Forget(float days)
	{
		for (auto it = m_Grievances.begin(); it != m_Grievances.end();)
		{
			it->second -= ForgetPerDay * days;
			it = it->second <= 0.0f ? m_Grievances.erase(it) : std::next(it);
		}
	}

	std::string HostilityTable::Report() const
	{
		std::string s;
		char b[160];
		for (const auto& [pair, v] : m_Table)
		{
			std::snprintf(b, sizeof b, "%s -> %s hostile %.2f\n", pair.first.c_str(), pair.second.c_str(), static_cast<double>(v));
			s += b;
		}
		for (const std::string& p : m_Predators) s += p + " -> anyone (a predator)\n";
		for (const auto& [pair, v] : m_Grievances)
		{
			std::snprintf(b, sizeof b, "%s holds a grievance of %.2f against %s\n", pair.first.c_str(), static_cast<double>(v), pair.second.c_str());
			s += b;
		}
		for (const auto& [clan, t] : m_Camps)
		{
			std::snprintf(b, sizeof b, "%s camps at %.0f, %.0f\n", clan.c_str(), static_cast<double>(t.Camp.x), static_cast<double>(t.Camp.y));
			s += b;
		}
		return s.empty() ? "nobody is hostile to anybody\n" : s;
	}

	std::string HostilityTable::ToJson() const
	{
		Json table = Json::array(), grievances = Json::array();
		for (const auto& [pair, v] : m_Table) table.push_back(Json::array({ pair.first, pair.second, v }));
		for (const auto& [pair, v] : m_Grievances) grievances.push_back(Json::array({ pair.first, pair.second, v }));
		return Json{ { "table", table }, { "grievances", grievances }, { "predators", m_Predators } }.dump();
	}

	bool HostilityTable::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		std::map<std::pair<std::string, std::string>, float> table, grievances;
		auto read = [](const Json& list, std::map<std::pair<std::string, std::string>, float>& out)
		{
			if (!list.is_array()) return false;
			for (const Json& e : list)
			{
				if (!e.is_array() || e.size() != 3 || !e[0].is_string() || !e[1].is_string() || !e[2].is_number()) return false;
				out[{ e[0].get<std::string>(), e[1].get<std::string>() }] = e[2].get<float>();
			}
			return true;
		};
		if (j.contains("table") && !read(j["table"], table)) return false;
		if (j.contains("grievances") && !read(j["grievances"], grievances)) return false;
		std::vector<std::string> predators = m_Predators;
		if (j.contains("predators"))
		{
			if (!j["predators"].is_array()) return false;
			predators.clear();
			for (const Json& p : j["predators"]) { if (!p.is_string()) return false; predators.push_back(p.get<std::string>()); }
		}
		m_Table = std::move(table);
		m_Grievances = std::move(grievances);
		m_Predators = std::move(predators);
		return true;
	}

	// ------------------------------------------------------------------ the fight
	void CombatSystem::Bind(EntityWorld& entities, GameplayWorld& gameplay)
	{
		m_Entities = &entities;
		m_Gameplay = &gameplay;
		RegisterNeedContent(gameplay.Library);   // (Vigor and the moods: a fighter without needs gets them too)
		Refresh();
	}

	void CombatSystem::Refresh()
	{
		if (!m_Gameplay) return;
		RegisterCombatContent(m_Gameplay->Library, Rules);
		m_Registered = Rules;
		m_HasRegistered = true;
		for (auto& [name, c] : m_Fighters) Arm(c);
	}

	void CombatSystem::Clear()
	{
		if (m_Gameplay)
			for (const auto& [name, armed] : m_Armed)
				if (AbilitySystemComponent* asc = m_Gameplay->Find(name); asc && asc == armed.first) asc->RemoveListener(armed.second);
		m_Fighters.clear();
		m_Armed.clear();
		m_Events.clear();
		m_Seq = 0;
		m_Time = 0.0;
		m_Scan = 0.0f;
	}

	// The ability system of a fighter gets what fighting needs (Health, Stamina, Vigor, the abilities of its weapon) and the listener that
	// resolves its blows. Again whenever its ability system is a new one (a scene loaded into the gameplay world makes new ones).
	void CombatSystem::Arm(Combatant& c)
	{
		if (!m_Gameplay) return;
		AbilitySystemComponent* asc = m_Gameplay->Find(c.Name);
		if (!asc) return;
		AttributeSet& a = asc->Attributes();
		if (!a.Has(kStamina)) a.Define(kStamina, Rules.Hits.MaxStamina, 0.0f, Rules.Hits.MaxStamina);
		if (!a.Has(NeedName::Vigor)) a.Define(NeedName::Vigor, 1.0f, 0.0f, 1.0f);
		const std::string attack = "Attack." + c.Weapon;
		if (!asc->HasAbility(attack)) asc->Grant(attack);
		for (const char* ab : { "Block", "Flee", "Stagger" }) if (!asc->HasAbility(ab)) asc->Grant(ab);
		const auto armed = m_Armed.find(c.Name);
		if (armed != m_Armed.end() && armed->second.first == asc) return;
		const std::string name = c.Name;
		const int id = asc->AddListener(kHitEvent, [this, name, asc](const GameplayEventData& e)
		{
			if (e.Instigator != asc || !e.Target) return;   // (the target hears it too: only the attacker's side resolves it)
			Resolve(name, e.Target->Name());
		});
		m_Armed[c.Name] = { asc, id };
	}

	Combatant* CombatSystem::Join(const std::string& name, const std::string& side, bool entity, float health)
	{
		if (!m_Gameplay) return nullptr;
		AbilitySystemComponent* asc = m_Gameplay->Find(name);
		if (!asc) return nullptr;
		auto it = m_Fighters.find(name);
		if (it != m_Fighters.end())
		{
			it->second.Side = side;
			Arm(it->second);
			return &it->second;
		}
		Combatant c;
		c.Name = name;
		c.Side = side;
		c.Entity = entity;
		c.Weapon = Rules.Unarmed;
		if (!asc->Attributes().Has(kHealth)) asc->Attributes().Define(kHealth, health, 0.0f, health);
		if (entity) Locate(name, c.Pos);
		c.Home = c.Pos;
		c.Morale = std::clamp(Rules.Morale.Start + Rules.Morale.Boldness * c.Boldness, 0.0f, 1.0f);
		if (::GB::IsDead(*asc)) c.Mode = FightMode::Dead;
		Combatant& stored = m_Fighters.emplace(name, std::move(c)).first->second;
		Arm(stored);
		return &stored;
	}

	Combatant* CombatSystem::JoinAnimal(const std::string& name)
	{
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(name) : nullptr;
		if (!asc) return nullptr;
		const PredatorRule* p = Rules.PredatorFor(*asc);
		const HuntRule* h = p ? nullptr : Rules.HuntFor(*asc);
		if (!p && !h) return nullptr;
		const bool fresh = !Find(name);
		Combatant* c = Join(name, p ? p->Tag : h->Tag, true, p ? p->Health : h->Health);
		if (!c || !fresh) return c;
		c->Animal = true;
		c->Speed = p ? p->Speed : h->Speed;
		c->Weapon = p ? p->Weapon : h->Weapon;
		c->Boldness = p ? p->Boldness : 0.0f;
		c->Morale = std::clamp(Rules.Morale.Start + Rules.Morale.Boldness * c->Boldness, 0.0f, 1.0f);
		Arm(*c);
		return c;
	}

	void CombatSystem::Leave(const std::string& name)
	{
		const auto armed = m_Armed.find(name);
		if (armed != m_Armed.end())
		{
			if (AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(name) : nullptr; asc && asc == armed->second.first) asc->RemoveListener(armed->second.second);
			m_Armed.erase(armed);
		}
		Release(name);
		m_Fighters.erase(name);
		for (auto& [n, c] : m_Fighters)
			if (c.Target == name && c.Mode != FightMode::Dead) { c.Mode = FightMode::None; c.Target.clear(); }
	}

	Combatant* CombatSystem::Find(const std::string& name)
	{
		const auto it = m_Fighters.find(name);
		return it == m_Fighters.end() ? nullptr : &it->second;
	}

	const Combatant* CombatSystem::Find(const std::string& name) const
	{
		const auto it = m_Fighters.find(name);
		return it == m_Fighters.end() ? nullptr : &it->second;
	}

	void CombatSystem::Place(const std::string& name, const glm::vec2& pos)
	{
		if (Combatant* c = Find(name); c && !c->Entity) c->Pos = pos;
	}

	bool CombatSystem::Locate(const std::string& name, glm::vec2& pos)
	{
		const Combatant* c = Find(name);
		if (c && !c->Entity) { pos = c->Pos; return true; }
		if (!m_Entities) return false;
		const EntityId id = m_Entities->FindByName(name);
		if (id == NullEntity) return false;
		const Entity* e = m_Entities->Find(id);
		pos = { e->Transform.Position.x, e->Transform.Position.z };
		return true;
	}

	bool CombatSystem::IsDead(const std::string& name) const
	{
		const Combatant* c = Find(name);
		if (c && c->Mode == FightMode::Dead) return true;
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(name) : nullptr;
		return !asc || ::GB::IsDead(*asc);
	}

	bool CombatSystem::InFight(const std::string& name) const
	{
		const Combatant* c = Find(name);
		return c && (c->Mode == FightMode::Engage || c->Mode == FightMode::Flee);
	}

	std::vector<std::string> CombatSystem::AttackersOf(const std::string& name) const
	{
		std::vector<std::string> out;
		for (const auto& [n, c] : m_Fighters) if (c.Mode == FightMode::Engage && c.Target == name) out.push_back(n);
		return out;
	}

	bool CombatSystem::Accompanied(const std::string& name, float radius) const
	{
		const Combatant* me = Find(name);
		if (!me) return false;
		for (const auto& [n, c] : m_Fighters)
			if (n != name && c.Side == me->Side && c.Mode != FightMode::Dead && glm::distance(c.Pos, me->Pos) <= radius) return true;
		return false;
	}

	float CombatSystem::Strength(const std::string& name) const
	{
		const Combatant* c = Find(name);
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(name) : nullptr;
		if (!c || !asc || c->Mode == FightMode::Dead) return 0.0f;
		const WeaponDef& w = Rules.Weapon(c->Weapon);
		const float damage = w.Damage + w.Power * c->Power;
		return std::max(0.0f, asc->Attribute(kHealth)) / 100.0f * damage / w.Seconds * (0.5f + c->Strength) * (0.75f + 0.5f * c->Dexterity);
	}

	void CombatSystem::Emit(const std::string& kind, const std::string& who, const std::string& other, const std::string& text, float amount)
	{
		CombatEvent e;
		e.Seq = ++m_Seq;
		e.Time = m_Time;
		e.Kind = kind;
		e.Who = who;
		e.Other = other;
		e.Text = text;
		e.Amount = amount;
		if (const Combatant* c = Find(other.empty() ? who : other)) { e.Pos = c->Pos; e.Side = c->Side; }
		m_Events.push_back(e);
		if (m_Events.size() > kMaxEvents + kMaxEvents / 4) m_Events.erase(m_Events.begin(), m_Events.begin() + static_cast<std::ptrdiff_t>(m_Events.size() - kMaxEvents));
	}

	std::vector<CombatEvent> CombatSystem::EventsSince(uint64_t seq) const
	{
		std::vector<CombatEvent> out;
		for (const CombatEvent& e : m_Events) if (e.Seq > seq) out.push_back(e);
		return out;
	}

	bool CombatSystem::Engage(const std::string& a, const std::string& b)
	{
		Combatant* ca = Find(a);
		Combatant* cb = Find(b);
		if (!ca || !cb || a == b || IsDead(a) || IsDead(b)) return false;
		const bool fresh = ca->Mode != FightMode::Engage || ca->Target != b;
		if (ca->Mode == FightMode::None) ca->Lost = 0.0f;
		ca->Mode = FightMode::Engage;
		ca->Target = b;
		ca->Fled = 0.0f;
		if (ca->Animal) if (AbilitySystemComponent* asc = m_Gameplay->Find(a); asc && !asc->Tags().HasExact("State.Held")) asc->Tags().Add("State.Held");
		if (fresh) Emit("engage", a, b);
		// an Entity foe answers: prey runs, the rest fights back (an External one is its owner's to decide). An animal only notices a foe
		// within GroupRadius: a hunter far away is not seen yet (the fights of Update make it answer when the hunter comes near).
		if (cb->Entity && cb->Mode != FightMode::Engage && cb->Mode != FightMode::Flee && (!cb->Animal || glm::distance(ca->Pos, cb->Pos) <= Rules.Threats.GroupRadius))
			Answer(b, a);
		return true;
	}

	void CombatSystem::Answer(const std::string& b, const std::string& a)
	{
		const AbilitySystemComponent* asc = m_Gameplay->Find(b);
		const HuntRule* prey = asc ? Rules.HuntFor(*asc) : nullptr;
		if (prey && !prey->FightsBack) Flee(b, a);
		else Engage(b, a);
	}

	bool CombatSystem::Flee(const std::string& a, const std::string& from)
	{
		Combatant* c = Find(a);
		if (!c || IsDead(a)) return false;
		const bool fresh = c->Mode != FightMode::Flee;
		c->Mode = FightMode::Flee;
		c->Target = from;
		c->Fled = 0.0f;
		if (AbilitySystemComponent* asc = m_Gameplay->Find(a))
		{
			if (c->Animal && !asc->Tags().HasExact("State.Held")) asc->Tags().Add("State.Held");
			if (fresh) asc->Activate("Flee", nullptr);
		}
		if (fresh) Emit("flee", a, from);
		return true;
	}

	void CombatSystem::Release(const std::string& a)
	{
		Combatant* c = Find(a);
		if (!c) return;
		if (c->Mode != FightMode::Dead) { c->Mode = FightMode::None; c->Target.clear(); }
		if (AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(a) : nullptr) asc->Tags().RemoveAll("State.Held");
	}

	void CombatSystem::Kill(Combatant& c, const std::string& by, const std::string& cause)
	{
		if (c.Mode == FightMode::Dead) return;
		AbilitySystemComponent* asc = m_Gameplay->Find(c.Name);
		c.Mode = FightMode::Dead;
		c.Target.clear();
		c.KilledBy = by;
		if (asc)
		{
			asc->Attributes().SetBase(kHealth, 0.0f);
			asc->CancelAll();
			asc->Tags().Add("State.Dead");
			asc->Tags().Add("Death.Wounds");
			asc->Tags().RemoveAll("State.Held");
		}
		if (Combatant* k = Find(by)) ++k->Kills;
		Emit("death", by, c.Name, cause);
		// its side loses heart, and whoever fought it is free
		for (auto& [n, o] : m_Fighters)
		{
			if (n == c.Name || o.Mode == FightMode::Dead) continue;
			if (o.Side == c.Side && glm::distance(o.Pos, c.Pos) <= Rules.Threats.GroupRadius) o.Morale = std::max(0.0f, o.Morale - Rules.Morale.AllyDown);
			if (o.Target == c.Name)
			{
				o.Mode = FightMode::None;
				o.Target.clear();
				// the others' swings in the air at it stop: a dead animal is taken away (the ecology) and a blow landing later would reach a
				// target that no longer exists (a group hunt: several hunters on one deer)
				if (n != by && m_Gameplay)
					if (AbilitySystemComponent* oa = m_Gameplay->Find(n)) oa->CancelAll();
				// a predator eats what it killed, then goes back to its life
				if (o.Animal && m_Gameplay)
					if (AbilitySystemComponent* pa = m_Gameplay->Find(n))
						if (const PredatorRule* p = Rules.PredatorFor(*pa))
							if (pa->Attributes().Has("Food")) pa->Attributes().AddBase("Food", p->Meal);
				if (o.Animal) Release(n);
			}
		}
	}

	bool CombatSystem::Hurt(const std::string& name, float amount, const std::string& woundKind, float severity, const std::string& by)
	{
		Combatant* c = Find(name);
		AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(name) : nullptr;
		if (!c || !asc || c->Mode == FightMode::Dead) return false;
		asc->Attributes().AddBase(kHealth, -amount);
		c->Lost += amount;
		if (severity > 0.0f)
		{
			c->Wounds.push_back({ woundKind, std::min(1.0f, severity), Rules.Injuries.BleedSeconds });
			SetWoundEffect(*asc, c->Severity());
			Emit("wound", by, name, woundKind, severity);
		}
		if (asc->Attribute(kHealth) <= 0.0f || c->Severity() >= Rules.Injuries.Lethal) { Kill(*c, by, "Wounds"); return true; }
		return false;
	}

	// The blow of `attacker` lands on `defender` (its montage reached the notify "hit"): whether it hits, how hard, a wound, morale, death.
	void CombatSystem::Resolve(const std::string& attacker, const std::string& defender)
	{
		Combatant* a = Find(attacker);
		Combatant* d = Find(defender);
		AbilitySystemComponent* da = m_Gameplay ? m_Gameplay->Find(defender) : nullptr;
		const AbilitySystemComponent* aa = m_Gameplay ? m_Gameplay->Find(attacker) : nullptr;
		if (!a || !d || !da || !aa || a->Mode == FightMode::Dead || d->Mode == FightMode::Dead) return;
		const WeaponDef& w = Rules.Weapon(a->Weapon);
		// a foe that stepped out of reach between the swing and the blow is missed (with a little slack: the swing carries)
		if (glm::distance(a->Pos, d->Pos) > w.Reach * 1.35f + 0.2f) { ++a->Missed; Emit("miss", attacker, defender, "out of reach"); return; }
		const HitRules& h = Rules.Hits;
		const float spent = 1.0f - std::clamp(aa->Attribute(kStamina) / std::max(1.0f, h.MaxStamina), 0.0f, 1.0f);
		const float chance = std::clamp(h.Base + h.Dexterity * (a->Dexterity - d->Dexterity) - h.Stamina * spent, h.Min, h.Max);
		const double roll = CombatRoll(Rules.Seed, attacker, defender, static_cast<uint64_t>(a->Rolls++));
		if (roll >= chance) { ++a->Missed; Emit("miss", attacker, defender); return; }
		float damage = (w.Damage + w.Power * a->Power) * (1.0f + h.Strength * (a->Strength - 0.5f)) * (1.0f - h.Endurance * (d->Endurance - 0.5f));
		damage *= 0.85f + 0.3f * static_cast<float>(CombatRoll(Rules.Seed + 1u, attacker, defender, static_cast<uint64_t>(a->Rolls)));   // a blow is never quite the same
		const bool blocked = da->HasTag("State.Blocking");
		if (blocked) { damage *= h.BlockLets; ++d->Blocked; }
		damage = std::max(0.0f, damage);
		++a->Landed;
		++d->Taken;
		d->LastHitBy = attacker;
		Emit(blocked ? "block" : "hit", attacker, defender, a->Weapon, damage);
		// a wound: a heavy blow always, a lighter one by the weapon's chance (never through a block)
		float severity = 0.0f;
		if (!blocked)
		{
			const InjuryRules& in = Rules.Injuries;
			const double wroll = CombatRoll(Rules.Seed + 2u, attacker, defender, static_cast<uint64_t>(a->Rolls));
			if (damage >= in.WoundAt || wroll < static_cast<double>(w.Wound)) severity = std::max(0.05f, damage / in.Severity);
		}
		d->Morale = std::max(0.0f, d->Morale - Rules.Morale.Hurt * damage / 100.0f);
		const bool died = Hurt(defender, damage, w.WoundKind, severity, attacker);
		if (!died && !blocked && damage > 0.0f && !da->AnyActive()) da->Activate("Stagger", nullptr);
	}

	void CombatSystem::SetWoundEffect(AbilitySystemComponent& asc, float severity)
	{
		const char* want = severity <= 0.02f ? nullptr : severity < 0.35f ? "Combat.Wounded.Light" : severity < 0.8f ? "Combat.Wounded.Serious" : "Combat.Wounded.Grave";
		for (const char* e : { "Combat.Wounded.Light", "Combat.Wounded.Serious", "Combat.Wounded.Grave" })
		{
			const bool on = asc.EffectActive(e);
			if (want && std::string(want) == e) { if (!on) asc.ApplyEffect(e); }
			else if (on) asc.RemoveEffect(e);
		}
	}

	void CombatSystem::StepWounds(Combatant& c, AbilitySystemComponent& asc, float dt, float daySeconds)
	{
		if (c.Wounds.empty()) { if (asc.HasTag("State.Wounded")) SetWoundEffect(asc, 0.0f); return; }
		const InjuryRules& in = Rules.Injuries;
		float heal = in.HealPerDay / std::max(1.0f, daySeconds) * dt;
		if (c.Resting) heal *= in.RestFactor;
		if (c.Cared) heal *= in.CareFactor;
		heal *= std::max(0.0f, c.Heal);
		float bleed = 0.0f;
		for (Wound& w : c.Wounds)
		{
			if (w.Bleeding > 0.0f)
			{
				bleed += in.Bleed * w.Severity * (c.Cared ? 0.5f : 1.0f);
				w.Bleeding = std::max(0.0f, w.Bleeding - dt * (c.Cared ? 2.0f : 1.0f));
			}
			else w.Severity -= heal;   // (a wound heals once it stops bleeding)
		}
		c.Wounds.erase(std::remove_if(c.Wounds.begin(), c.Wounds.end(), [](const Wound& w) { return w.Severity <= 0.0f; }), c.Wounds.end());
		if (bleed > 0.0f)
		{
			asc.Attributes().AddBase(kHealth, -bleed * dt);
			if (asc.Attribute(kHealth) <= 0.0f) { Kill(c, c.LastHitBy, "Bleeding"); return; }
		}
		SetWoundEffect(asc, c.Severity());
	}

	// A hungry predator alone with a child (or a weak member, by its rule) goes for it.
	void CombatSystem::StepPredators()
	{
		if (Rules.Predators.empty() || !m_Gameplay) return;
		for (const std::string& name : m_Gameplay->Names())
		{
			AbilitySystemComponent* asc = m_Gameplay->Find(name);
			if (!asc || ::GB::IsDead(*asc) || asc->HasTag("State.Held")) continue;
			const PredatorRule* rule = Rules.PredatorFor(*asc);
			if (!rule || rule->Prey == "none") continue;
			if (asc->Attributes().Has("Food") && asc->Attribute("Food") >= rule->Hungry) continue;
			glm::vec2 at;
			if (!Locate(name, at)) continue;
			std::string best;
			float bestDistance = rule->Sight;
			for (const auto& [n, c] : m_Fighters)
			{
				if (c.Animal || c.Mode == FightMode::Dead || c.Side == rule->Tag) continue;
				const AbilitySystemComponent* pa = m_Gameplay->Find(n);
				if (!pa) continue;
				const bool weak = c.Child || pa->Attribute(kHealth) < rule->WeakHealth;
				if (rule->Prey == "child" ? !c.Child : rule->Prey == "weak" ? !weak : false) continue;
				const float d = glm::distance(at, c.Pos);
				if (d >= bestDistance || Accompanied(n, rule->Alone)) continue;
				best = n;
				bestDistance = d;
			}
			if (best.empty() || !JoinAnimal(name)) continue;
			Emit("prey", name, best);
			Engage(name, best);
		}
	}

	// An Entity fighter walks straight at its foe (to within reach) or away from it, on the ground of its entity.
	void CombatSystem::MoveEntity(Combatant& c, float dt)
	{
		if (!m_Entities) return;
		const EntityId id = m_Entities->FindByName(c.Name);
		if (id == NullEntity) return;
		Entity& e = *m_Entities->Find(id);
		glm::vec2 pos(e.Transform.Position.x, e.Transform.Position.z), foe;
		const AbilitySystemComponent* asc = m_Gameplay->Find(c.Name);
		// running tires: a spent animal runs at half its speed (so a hunter that keeps after its prey catches it)
		const float wind = asc ? 0.5f + 0.5f * std::clamp(asc->Attribute(kStamina) / std::max(1.0f, Rules.Hits.MaxStamina), 0.0f, 1.0f) : 1.0f;
		const float vigor = (asc && asc->Attributes().Has(NeedName::Vigor) ? std::max(0.3f, asc->Attribute(NeedName::Vigor)) : 1.0f) * wind;
		if (!c.Target.empty() && Locate(c.Target, foe) && !(asc && asc->HasTag("State.Attacking")))
		{
			glm::vec2 to = foe - pos;
			const float distance = glm::length(to);
			float step = 0.0f;
			if (c.Mode == FightMode::Engage) step = std::min(c.Speed * vigor * dt, std::max(0.0f, distance - Rules.Weapon(c.Weapon).Reach * 0.8f));
			else if (c.Mode == FightMode::Flee) { step = c.Speed * vigor * dt; to = -to; }
			if (distance > 1e-4f && step > 0.0f)
			{
				if (AbilitySystemComponent* tired = m_Gameplay->Find(c.Name)) tired->Attributes().AddBase(kStamina, -Rules.Hits.RunStamina * dt);
				pos += to / distance * step;
				e.Transform.Rotation.y = glm::degrees(std::atan2(to.x, to.y));
			}
		}
		e.Transform.Position.x = pos.x;
		e.Transform.Position.z = pos.y;
		c.Pos = pos;
	}

	void CombatSystem::Update(float dt, float daySeconds)
	{
		if (!m_Gameplay || !m_Entities) return;
		m_Time += static_cast<double>(dt);
		m_Scan += dt;
		if (m_Scan >= 0.5f)
		{
			m_Scan = 0.0f;
			if (!m_HasRegistered || !(m_Registered == Rules)) Refresh();
			StepPredators();
		}
		std::vector<std::string> gone;
		for (auto& [name, c] : m_Fighters)
		{
			AbilitySystemComponent* asc = m_Gameplay->Find(name);
			if (!asc) { gone.push_back(name); continue; }
			Arm(c);
			if (c.Mode == FightMode::Dead) continue;
			if (::GB::IsDead(*asc)) { c.Mode = FightMode::Dead; c.Target.clear(); continue; }   // (died of something else: hunger, old age)
			// stamina comes back; morale too, out of a fight
			asc->Attributes().AddBase(kStamina, Rules.Hits.StaminaRegen * dt * (c.Mode == FightMode::Engage ? 0.5f : c.Mode == FightMode::Flee && c.Entity ? 0.0f : 1.0f));

			if (c.Mode == FightMode::None) c.Morale = std::min(1.0f, c.Morale + Rules.Morale.Recover * dt);
			StepWounds(c, *asc, dt, daySeconds);
			if (c.Mode == FightMode::Dead) continue;
			// an animal out of the fight goes back to its life (the ecology moves it again); a wounded one stays to heal or bleed
			if (c.Animal && c.Mode == FightMode::None && c.Wounds.empty() && AttackersOf(name).empty()) { gone.push_back(name); continue; }
			if (c.Entity && (c.Mode == FightMode::Engage || c.Mode == FightMode::Flee)) MoveEntity(c, dt);
			else if (c.Entity) Locate(name, c.Pos);
			c.Swing = std::max(0.0f, c.Swing - dt);
		}
		for (const std::string& n : gone) Leave(n);

		// the fights: a fighter whose foe is gone stops; an Entity fighter that breaks flees; whoever is within reach swings
		for (auto& [name, c] : m_Fighters)
		{
			if (c.Mode == FightMode::Flee && c.Entity)
			{
				c.Fled += dt;
				glm::vec2 foe;
				const bool far = c.Target.empty() || !Locate(c.Target, foe) || glm::distance(foe, c.Pos) > Rules.Threats.SafeDistance;
				if (c.Fled > 12.0f || far) { Release(name); if (c.Animal) Emit("gives up", name, c.Target); }
				continue;
			}
			if (c.Mode != FightMode::Engage) continue;
			const Combatant* foe = Find(c.Target);
			if (!foe || foe->Mode == FightMode::Dead || IsDead(c.Target)) { c.Mode = FightMode::None; c.Target.clear(); if (c.Animal) Release(name); continue; }
			const float distance = glm::distance(c.Pos, foe->Pos);
			if (foe->Entity && foe->Animal && foe->Mode == FightMode::None && distance <= Rules.Threats.GroupRadius) Answer(c.Target, name);   // it sees its hunter now
			AbilitySystemComponent* asc = m_Gameplay->Find(name);

			if (c.Entity)
			{
				const AbilitySystemComponent* self = asc;
				const PredatorRule* p = self ? Rules.PredatorFor(*self) : nullptr;
				const bool broken = c.Morale < Rules.Morale.FleeBelow || (self && self->Attribute(kHealth) < Rules.Morale.FleeHealth && c.Boldness < 0.8f);
				if (broken) { Flee(name, c.Target); continue; }
				if (p && distance > p->GiveUp) { Emit("gives up", name, c.Target); Release(name); continue; }
			}
			const WeaponDef& w = Rules.Weapon(c.Weapon);
			if (distance > w.Reach || c.Swing > 0.0f || !asc) continue;
			AbilitySystemComponent* target = m_Gameplay->Find(c.Target);
			if (!target) continue;
			if (!asc->Activate("Attack." + c.Weapon, target, nullptr, c.Target)) continue;   // (no stamina, staggered, mid-swing: next frame)
			c.Swing = w.Seconds;
			// the foe sees it coming: it may block (a fighter that is not swinging itself, with the stamina, by its dexterity)
			Combatant* d = Find(c.Target);
			if (d && d->Mode == FightMode::Engage && !target->HasTag("State.Attacking"))
			{
				const double roll = CombatRoll(Rules.Seed + 3u, c.Target, name, static_cast<uint64_t>(d->Rolls++));
				if (roll < static_cast<double>(Rules.Hits.BlockChance * (0.5f + d->Dexterity))) target->Activate("Block", nullptr);
			}
		}
	}

	std::string CombatSystem::Status(const std::string& name) const
	{
		const Combatant* c = Find(name);
		if (!c) return {};
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(name) : nullptr;
		char b[256];
		std::snprintf(b, sizeof b, "%s (%s) %s%s%s, health %.0f, stamina %.0f, morale %.2f, %s", name.c_str(), c->Side.c_str(), FightModeName(c->Mode),
			c->Target.empty() ? "" : " ", c->Target.c_str(), static_cast<double>(asc ? asc->Attribute(kHealth) : 0.0f), static_cast<double>(asc ? asc->Attribute(kStamina) : 0.0f),
			static_cast<double>(c->Morale), c->Weapon.c_str());
		std::string s = b;
		if (!c->Wounds.empty())
		{
			std::snprintf(b, sizeof b, ", %zu wound%s (severity %.2f%s)", c->Wounds.size(), c->Wounds.size() == 1 ? "" : "s", static_cast<double>(c->Severity()),
				std::any_of(c->Wounds.begin(), c->Wounds.end(), [](const Wound& w) { return w.Bleeding > 0.0f; }) ? ", bleeding" : "");
			s += b;
		}
		if (c->Mode == FightMode::Dead && !c->KilledBy.empty()) s += ", killed by " + c->KilledBy;
		return s;
	}

	std::string CombatSystem::Report() const
	{
		std::string s;
		for (const auto& [name, c] : m_Fighters)
			if (c.Mode != FightMode::None || !c.Wounds.empty()) s += Status(name) + "\n";
		return s.empty() ? "no fights, nobody wounded\n" : s;
	}

	std::string CombatSystem::ToJson() const
	{
		Json fighters = Json::array();
		for (const auto& [name, c] : m_Fighters)
		{
			Json wounds = Json::array();
			for (const Wound& w : c.Wounds) wounds.push_back(Json::array({ w.Kind, w.Severity, w.Bleeding }));
			fighters.push_back(Json{ { "name", c.Name }, { "side", c.Side }, { "entity", c.Entity }, { "animal", c.Animal }, { "child", c.Child }, { "speed", c.Speed },
				{ "weapon", c.Weapon }, { "power", c.Power }, { "stats", Json::array({ c.Strength, c.Dexterity, c.Endurance, c.Boldness }) },
				{ "mode", static_cast<int>(c.Mode) }, { "target", c.Target }, { "pos", Json::array({ c.Pos.x, c.Pos.y }) }, { "home", Json::array({ c.Home.x, c.Home.y }) },
				{ "swing", c.Swing }, { "morale", c.Morale }, { "fled", c.Fled }, { "resting", c.Resting }, { "cared", c.Cared }, { "wounds", wounds },
				{ "rolls", c.Rolls }, { "counts", Json::array({ c.Landed, c.Missed, c.Blocked, c.Taken, c.Kills }) }, { "killedBy", c.KilledBy }, { "lastHitBy", c.LastHitBy }, { "lost", c.Lost } });

		}
		return Json{ { "time", m_Time }, { "seq", m_Seq }, { "scan", m_Scan }, { "fighters", fighters } }.dump();
	}

	bool CombatSystem::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("fighters") || !j["fighters"].is_array()) return false;
		std::map<std::string, Combatant> fighters;
		try
		{
			for (const Json& f : j["fighters"])
			{
				Combatant c;
				c.Name = f.at("name").get<std::string>();
				c.Side = f.value("side", std::string());
				c.Entity = f.value("entity", false);
				c.Animal = f.value("animal", false);
				c.Child = f.value("child", false);
				c.Speed = f.value("speed", 3.0f);
				c.Weapon = f.value("weapon", Rules.Unarmed);
				c.Power = f.value("power", 0.0f);
				if (f.contains("stats") && f["stats"].size() == 4)
				{
					c.Strength = f["stats"][0].get<float>(); c.Dexterity = f["stats"][1].get<float>();
					c.Endurance = f["stats"][2].get<float>(); c.Boldness = f["stats"][3].get<float>();
				}
				const int mode = f.value("mode", 0);
				if (mode < 0 || mode > 3) return false;
				c.Mode = static_cast<FightMode>(mode);
				c.Target = f.value("target", std::string());
				if (f.contains("pos")) c.Pos = { f["pos"][0].get<float>(), f["pos"][1].get<float>() };
				if (f.contains("home")) c.Home = { f["home"][0].get<float>(), f["home"][1].get<float>() };
				c.Swing = f.value("swing", 0.0f);
				c.Morale = f.value("morale", 1.0f);
				c.Fled = f.value("fled", 0.0f);
				c.Resting = f.value("resting", false);
				c.Cared = f.value("cared", false);
				if (f.contains("wounds"))
					for (const Json& w : f["wounds"]) c.Wounds.push_back({ w.at(0).get<std::string>(), w.at(1).get<float>(), w.at(2).get<float>() });
				c.Rolls = f.value("rolls", 0);
				if (f.contains("counts") && f["counts"].size() == 5)
				{
					c.Landed = f["counts"][0].get<int>(); c.Missed = f["counts"][1].get<int>(); c.Blocked = f["counts"][2].get<int>();
					c.Taken = f["counts"][3].get<int>(); c.Kills = f["counts"][4].get<int>();
				}
				c.KilledBy = f.value("killedBy", std::string());
				c.LastHitBy = f.value("lastHitBy", std::string());
				c.Lost = f.value("lost", 0.0f);
				if (c.Name.empty()) return false;
				fighters[c.Name] = std::move(c);
			}
		}
		catch (const Json::exception&) { return false; }
		if (m_Gameplay)
			for (const auto& [name, armed] : m_Armed)
				if (AbilitySystemComponent* asc = m_Gameplay->Find(name); asc && asc == armed.first) asc->RemoveListener(armed.second);
		m_Armed.clear();
		m_Fighters = std::move(fighters);
		m_Time = j.value("time", 0.0);
		m_Seq = j.value("seq", static_cast<uint64_t>(0));
		m_Scan = j.value("scan", 0.0f);
		m_Events.clear();
		for (auto& [name, c] : m_Fighters)
		{
			Arm(c);
			if (c.Animal && (c.Mode == FightMode::Engage || c.Mode == FightMode::Flee))
				if (AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(name) : nullptr; asc && !asc->Tags().HasExact("State.Held")) asc->Tags().Add("State.Held");
			if (AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(name) : nullptr) SetWoundEffect(*asc, c.Severity());
		}
		return true;
	}
}
