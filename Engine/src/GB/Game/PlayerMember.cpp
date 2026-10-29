// H15: the data of the player inside the society (the "player" and "controls" sections) and the state a clan keeps of its players. The rules a
// tribe applies to them are in TribePlayer.cpp.
#include "GB/Game/PlayerMember.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		double Round(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }   // 0.9f is written as 0.9

		// A table of the keys of one object: its name, where it goes and how it is read. Reading checks the type and the unknown keys.
		struct Field
		{
			const char* Name;
			float* F = nullptr;
			int* I = nullptr;
			bool* B = nullptr;
			std::string* S = nullptr;
			std::map<std::string, float>* Map = nullptr;
			std::vector<std::string>* List = nullptr;
		};
		Field F(const char* n, float& v) { Field f{ n }; f.F = &v; return f; }
		Field I(const char* n, int& v) { Field f{ n }; f.I = &v; return f; }
		Field B(const char* n, bool& v) { Field f{ n }; f.B = &v; return f; }
		Field S(const char* n, std::string& v) { Field f{ n }; f.S = &v; return f; }
		Field M(const char* n, std::map<std::string, float>& v) { Field f{ n }; f.Map = &v; return f; }
		Field L(const char* n, std::vector<std::string>& v) { Field f{ n }; f.List = &v; return f; }

		// Reads the fields of `o` (the nested objects named in `nested` are left to the caller). "" or what is wrong, with `where` in front.
		std::string Read(const Json& o, const std::vector<Field>& fields, const std::string& where, const std::vector<std::string>& nested = {})
		{
			if (!o.is_object()) return where + ": expected an object";
			for (const auto& [key, v] : o.items())
			{
				if (std::find(nested.begin(), nested.end(), key) != nested.end()) continue;
				const auto f = std::find_if(fields.begin(), fields.end(), [&](const Field& x) { return key == x.Name; });
				if (f == fields.end()) return where + ": unknown key '" + key + "'";
				const std::string at = where + "." + key;
				if (f->F) { if (!v.is_number()) return at + ": expected a number"; *f->F = v.get<float>(); }
				else if (f->I) { if (!v.is_number_integer()) return at + ": expected a whole number"; *f->I = v.get<int>(); }
				else if (f->B) { if (!v.is_boolean()) return at + ": expected true or false"; *f->B = v.get<bool>(); }
				else if (f->S) { if (!v.is_string()) return at + ": expected a string"; *f->S = v.get<std::string>(); }
				else if (f->Map)
				{
					if (!v.is_object()) return at + ": expected an object of numbers";
					std::map<std::string, float> m;
					for (const auto& [k, x] : v.items()) { if (!x.is_number()) return at + "." + k + ": expected a number"; m[k] = x.get<float>(); }
					*f->Map = std::move(m);
				}
				else if (f->List)
				{
					if (!v.is_array()) return at + ": expected a list of strings";
					std::vector<std::string> l;
					for (const Json& x : v) { if (!x.is_string()) return at + ": expected a list of strings"; l.push_back(x.get<std::string>()); }
					*f->List = std::move(l);
				}
			}
			return {};
		}

		Json Write(const std::vector<Field>& fields)
		{
			Json o = Json::object();
			for (const Field& f : fields)
			{
				if (f.F) o[f.Name] = Round(*f.F);
				else if (f.I) o[f.Name] = *f.I;
				else if (f.B) o[f.Name] = *f.B;
				else if (f.S) o[f.Name] = *f.S;
				else if (f.Map) { Json m = Json::object(); for (const auto& [k, v] : *f.Map) m[k] = Round(v); o[f.Name] = m; }
				else if (f.List) o[f.Name] = *f.List;
			}
			return o;
		}

		std::vector<Field> PlayerFields(PlayerRules& r)
		{
			return { S("role", r.Role), B("mortal", r.Mortal), F("floorHealth", r.FloorHealth), F("reach", r.Reach), F("campReach", r.CampReach), F("giftBond", r.GiftBond),
				F("talkBond", r.TalkBond), F("talkSeconds", r.TalkSeconds), F("teachAmount", r.TeachAmount), F("marryBond", r.MarryBond), F("visitRadius", r.VisitRadius) };
		}
		std::vector<Field> StandingFields(PlayerRules& r)
		{
			return { F("start", r.StandingStart), F("max", r.StandingMax), F("leader", r.Leader), F("decayPerDay", r.DecayPerDay), M("credit", r.Credit) };
		}
		std::vector<Field> ProposalFields(ProposalRules& p)
		{
			return { F("base", p.Base), F("affinity", p.Affinity), F("standing", p.Standing), F("leader", p.Leader), M("culture", p.Culture), F("majority", p.Majority),
				F("unitCost", p.UnitCost), F("buildCost", p.BuildCost), F("cooldown", p.Cooldown), I("maxUnits", p.MaxUnits) };
		}
		std::vector<Field> HudFields(HudRules& h)
		{
			return { I("notifications", h.Notifications), F("notifySeconds", h.NotifySeconds), L("needs", h.Needs), B("clock", h.Clock), B("prompt", h.Prompt) };
		}
		std::vector<Field> GameFields(GameSetup& g)
		{
			return { S("world", g.World), I("seed", g.Seed), F("resources", g.Resources), I("members", g.Members), I("clans", g.Clans), S("model", g.Model),
				S("fallback", g.Fallback), S("name", g.Name), S("quality", g.Quality), F("autoMs", g.AutoMs), I("timeScale", g.TimeScale) };
		}
	}

	PlayerRules DefaultPlayerRules()
	{
		PlayerRules r;
		r.Credit = { { "deposit", 0.5f }, { "job", 1.0f }, { "gift", 1.0f }, { "teach", 6.0f }, { "craft", 3.0f }, { "stoke", 2.0f }, { "build", 1.5f },
			{ "trade", 5.0f }, { "proposal", 2.0f }, { "take", -0.3f }, { "hunt", 3.0f }, { "defend", 5.0f }, { "fall", -5.0f } };
		r.Proposals.Culture = { { "sharing", 0.3f }, { "industry", 0.1f } };
		return r;
	}

	ControlRules DefaultControls()
	{
		ControlRules c;
		c.Keys = { { "interact", "E" }, { "eat", "F" }, { "drink", "R" }, { "deposit", "G" }, { "sleep", "Z" }, { "clan", "Tab" }, { "jobs", "J" }, { "claim", "C" },
			{ "pause", "P" }, { "faster", "Equal" }, { "slower", "Minus" }, { "save", "F5" }, { "load", "F9" }, { "help", "H" } };
		return c;
	}

	std::string LoadPlayerRules(const std::string& json, PlayerRules& rules)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("player");
		if (section == j.end()) return {};
		PlayerRules read = rules;
		if (std::string e = Read(*section, PlayerFields(read), "player", { "standing", "proposals" }); !e.empty()) return e;
		if (const auto s = section->find("standing"); s != section->end())
			if (std::string e = Read(*s, StandingFields(read), "player.standing"); !e.empty()) return e;
		if (const auto p = section->find("proposals"); p != section->end())
			if (std::string e = Read(*p, ProposalFields(read.Proposals), "player.proposals"); !e.empty()) return e;
		if (read.StandingMax <= 0.0f) return "player.standing.max: must be above 0";
		if (read.Proposals.Majority < 0.0f || read.Proposals.Majority >= 1.0f) return "player.proposals.majority: must be in 0..1";
		if (read.Proposals.MaxUnits < 1) return "player.proposals.maxUnits: must be at least 1";
		rules = std::move(read);
		return {};
	}

	std::string PlayerRulesToJson(const PlayerRules& rules)
	{
		PlayerRules r = rules;   // the field tables take non-const pointers; nothing is written through them here
		Json p = Write(PlayerFields(r));
		p["standing"] = Write(StandingFields(r));
		p["proposals"] = Write(ProposalFields(r.Proposals));
		Json out = Json::object();
		out["player"] = p;
		return out.dump(2);
	}

	std::string LoadControls(const std::string& json, ControlRules& controls)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("controls");
		if (section == j.end()) return {};
		if (!section->is_object()) return "controls: expected an object";
		ControlRules read = controls;
		for (const auto& [key, v] : section->items())
		{
			if (key == "keys")
			{
				if (!v.is_object()) return "controls.keys: expected an object of key names";
				for (const auto& [action, k] : v.items())
				{
					if (!k.is_string()) return "controls.keys." + action + ": expected a key name";
					if (k.get<std::string>().empty()) read.Keys.erase(action);
					else read.Keys[action] = k.get<std::string>();
				}
			}
			else if (key == "hud") { if (std::string e = Read(v, HudFields(read.Hud), "controls.hud"); !e.empty()) return e; }
			else if (key == "game") { if (std::string e = Read(v, GameFields(read.Game), "controls.game"); !e.empty()) return e; }
			else return "controls: unknown key '" + key + "'";
		}
		if (read.Hud.Notifications < 0) return "controls.hud.notifications: must not be negative";
		if (read.Game.Members < 0 || read.Game.Clans < 1) return "controls.game: members must not be negative and there is at least one clan";
		controls = std::move(read);
		return {};
	}

	std::string ControlsToJson(const ControlRules& controls)
	{
		ControlRules c = controls;
		Json o = Json::object();
		Json keys = Json::object();
		for (const auto& [action, key] : c.Keys) keys[action] = key;
		o["keys"] = keys;
		o["hud"] = Write(HudFields(c.Hud));
		o["game"] = Write(GameFields(c.Game));
		Json out = Json::object();
		out["controls"] = o;
		return out.dump(2);
	}

	float ProposalVote(const ProposalRules& rules, float bond, float standing, float standingMax, bool leader, const std::map<std::string, float>& norms, float cost)
	{
		float v = rules.Base + rules.Affinity * bond + rules.Standing * (standingMax > 0.0f ? standing / standingMax : 0.0f) + (leader ? rules.Leader : 0.0f) - cost;
		for (const auto& [norm, w] : rules.Culture)
			if (const auto it = norms.find(norm); it != norms.end()) v += w * (2.0f * it->second - 1.0f);
		return v;
	}

	// ------------------------------------------------------------------ saving (the tribe's "ai" section, key "player")
	std::string PlayerSociety::ToJson() const
	{
		Json j = Json::object();
		Json players = Json::object();
		for (const auto& [name, r] : Players)
		{
			Json done = Json::object();
			for (const auto& [k, v] : r.Done) done[k] = v;
			Json talked = Json::object();
			for (const auto& [k, v] : r.TalkedAt) talked[k] = v;
			players[name] = Json{ { "standing", r.Standing }, { "done", done }, { "talked", talked }, { "lastProposal", r.LastProposal }, { "leader", r.Leader ? 1 : 0 } };
		}
		j["players"] = players;
		Json proposals = Json::array();
		for (const Proposal& p : Proposals)
			proposals.push_back(Json{ { "id", p.Id }, { "by", p.By }, { "kind", p.Kind }, { "target", p.Target }, { "units", p.Units }, { "at", Json::array({ p.At.x, p.At.y }) },
				{ "status", p.Status }, { "yes", p.Yes }, { "voters", p.Voters }, { "why", p.Why }, { "site", p.Site }, { "start", p.Start }, { "posted", p.Posted } });
		j["proposals"] = proposals;
		j["next"] = NextProposal;
		j["dayTimer"] = DayTimer;
		return j.dump();
	}

	bool PlayerSociety::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		PlayerSociety read;
		try
		{
			if (j.contains("players"))
				for (const auto& [name, o] : j.at("players").items())
				{
					PlayerRecord r;
					r.Standing = o.value("standing", 0.0f);
					if (o.contains("done")) for (const auto& [k, v] : o.at("done").items()) r.Done[k] = v.get<float>();
					if (o.contains("talked")) for (const auto& [k, v] : o.at("talked").items()) r.TalkedAt[k] = v.get<double>();
					r.LastProposal = o.value("lastProposal", -1e9);
					r.Leader = o.value("leader", 0) != 0;
					read.Players[name] = std::move(r);
				}
			if (j.contains("proposals"))
				for (const Json& o : j.at("proposals"))
				{
					Proposal p;
					p.Id = o.at("id").get<int>();
					p.By = o.value("by", std::string());
					p.Kind = o.value("kind", std::string());
					p.Target = o.value("target", std::string());
					p.Units = o.value("units", 0);
					if (o.contains("at")) p.At = { o.at("at").at(0).get<float>(), o.at("at").at(1).get<float>() };
					p.Status = o.value("status", std::string());
					p.Yes = o.value("yes", 0);
					p.Voters = o.value("voters", 0);
					p.Why = o.value("why", std::string());
					p.Site = o.value("site", std::string());
					p.Start = o.value("start", 0);
					p.Posted = o.value("posted", 0.0);
					read.Proposals.push_back(std::move(p));
				}
			read.NextProposal = j.value("next", 1);
			read.DayTimer = j.value("dayTimer", 0.0);
		}
		catch (const Json::exception&) { return false; }
		*this = std::move(read);
		return true;
	}
}
