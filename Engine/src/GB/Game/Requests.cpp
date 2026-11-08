#include "GB/Game/Requests.h"
#include "GB/Game/Clans.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	Request RequestBoard::ProblemOf(const ClanWorld& world, const Tribe& clan, int clanId, const TribeMember& npc, const std::string& player, double now, double daySeconds) const
	{
		Request r;
		r.Giver = npc.Name;
		r.GiverClan = clanId;
		r.Since = now;
		r.Until = now + static_cast<double>(Rules.Days) * daySeconds;
		const bool mine = clan.FindMember(player) != nullptr;   // the player is of the asker's clan
		// a problem already asked by someone else of the clan is not asked again: each tells its own (the next one down the list)
		auto taken = [&]()
		{
			for (const Request& o : m_All)
				if (o.State == "open" && o.GiverClan == r.GiverClan && o.Kind == r.Kind && o.Item == r.Item && o.Target == r.Target) return true;
			return false;
		};
		auto fresh = [&]() { r.Kind.clear(); r.Item.clear(); r.Target.clear(); r.TargetClan = -1; r.Count = 0; r.Why.clear(); };
		// a site of the clan lacks a material: what it lacks most
		std::string item;
		int lack = 0;
		for (const auto& [what, n] : clan.SiteDemand())
			if (n > lack) { item = what; lack = n; }
		if (lack > 0)
		{
			std::string site;
			for (const Tribe::SettlementSite& s : clan.SettlementSites())
				if (!s.State.Done) { site = s.Name; break; }
			r.Kind = "bring";
			r.Item = item;
			r.Count = std::min(lack, Rules.MaxBring);
			r.Target = site;
			r.Why = "the " + (site.empty() ? std::string("building") : site) + " lacks " + std::to_string(lack) + " " + item;
			if (!taken()) return r;
			fresh();
		}
		// the clan is short of food
		if (clan.FoodDays() < Rules.FoodDays)
		{
			r.Kind = "food";
			r.Item = "food";
			r.Count = 4;
			r.Why = "the clan has food for less than " + std::to_string(static_cast<int>(Rules.FoodDays + 0.5f)) + " days";
			if (!taken()) return r;
			fresh();
		}
		// a relative is far from home: a child, the partner, a parent
		if (const Person* p = clan.People().Find(npc.Name))
		{
			std::vector<std::string> kin = { p->Partner, p->Mother, p->Father };
			for (const auto& [name, other] : clan.People().People())
				if (!other.Dead && (other.Mother == npc.Name || other.Father == npc.Name)) kin.push_back(name);
			for (const std::string& k : kin)
			{
				if (k.empty() || k == player) continue;
				const TribeMember* m = clan.FindMember(k);
				if (!m || m->Dead || !m->Body) continue;
				const float far = glm::distance(clan.Position(*m), clan.Config.Economy.Camp);
				if (far < Rules.FarFromHome) continue;
				r.Kind = "find";
				r.Target = k;
				r.Why = k + " has been away from the camp (" + std::to_string(static_cast<int>(far)) + " m) and " + npc.Name + " worries";
				if (!taken()) return r;
				fresh();
			}
		}
		// the player knows something the asker would learn
		if (mine && clan.Knowledge().Gated() && clan.Knowledge().Techs())
			for (const auto& [id, def] : clan.Knowledge().Techs()->Techs)
				if (clan.Knowledge().Knows(player, id) && !clan.Knowledge().Knows(npc.Name, id) && clan.Knowledge().CanLearn(npc.Name, id))
				{
					r.Kind = "teach";
					r.Item = id;
					r.Why = npc.Name + " would learn " + id;
					if (!taken()) return r;
					fresh();
				}
		// the clan's next step needs what it does not have: another clan may have it
		const std::map<std::string, int> needs = clan.NextStepNeeds();
		if (!needs.empty())
			for (const auto& c : world.All())
				if (c->Id != clanId && c->Folk && !c->Folk->Members().empty())
				{
					r.Kind = "message";
					r.Item = needs.begin()->first;
					r.Target = c->Name;
					r.TargetClan = c->Id;
					r.Why = "the clan needs " + r.Item + " and the " + c->Name + " may have it";
					if (!taken()) return r;
					fresh();
				}
		r.Kind.clear();
		return r;
	}

	Request& RequestBoard::Add(Request r)
	{
		r.Id = m_Next++;
		m_All.push_back(std::move(r));
		if (m_All.size() > 200) m_All.erase(m_All.begin(), m_All.begin() + 50);   // (the oldest closed ones go first in time)
		return m_All.back();
	}

	Request* RequestBoard::Find(int id)
	{
		for (Request& r : m_All) if (r.Id == id) return &r;
		return nullptr;
	}

	const Request* RequestBoard::OpenBy(const std::string& giver) const
	{
		for (const Request& r : m_All) if (r.Giver == giver && r.State == "open") return &r;
		return nullptr;
	}

	std::vector<const Request*> RequestBoard::OpenFor(const std::string& player) const
	{
		std::vector<const Request*> out;
		for (const Request& r : m_All) if (r.Taker == player && r.State == "open") out.push_back(&r);
		return out;
	}

	std::vector<int> RequestBoard::Expire(double now)
	{
		std::vector<int> out;
		for (Request& r : m_All)
			if (r.State == "open" && now > r.Until) { r.State = "dropped"; out.push_back(r.Id); }
		return out;
	}

	std::vector<int> RequestBoard::Gave(const std::string& player, const std::string& to, int toClan, const std::string& item, int count, bool food)
	{
		std::vector<int> out;
		for (Request& r : m_All)
		{
			if (r.State != "open" || r.Taker != player || count <= 0) continue;
			const bool bring = (r.Kind == "bring" && r.Giver == to && r.Item == item) || (r.Kind == "food" && r.GiverClan == toClan && food);
			if (!bring) continue;
			const int used = std::min(count, r.Count - r.Given);
			r.Given += used;
			count -= used;
			if (r.Given >= r.Count) { r.State = "done"; out.push_back(r.Id); }
		}
		return out;
	}

	std::vector<int> RequestBoard::Talked(const std::string& player, const std::string& to, int toClan)
	{
		std::vector<int> out;
		for (Request& r : m_All)
			if (r.State == "open" && r.Taker == player && ((r.Kind == "message" && r.TargetClan == toClan) || (r.Kind == "find" && r.Target == to)))
			{
				r.State = "done";
				out.push_back(r.Id);
			}
		return out;
	}

	std::vector<int> RequestBoard::Taught(const std::string& player, const std::string& to, const std::string& tech)
	{
		std::vector<int> out;
		for (Request& r : m_All)
			if (r.State == "open" && r.Taker == player && r.Kind == "teach" && r.Giver == to && (tech.empty() || r.Item == tech))
			{
				r.State = "done";
				out.push_back(r.Id);
			}
		return out;
	}

	std::vector<int> RequestBoard::Met(const std::string& player, const std::string& person)
	{
		return Talked(player, person, -2);
	}

	std::string RequestBoard::ToJson() const
	{
		Json all = Json::array();
		for (const Request& r : m_All)
			all.push_back({ { "id", r.Id }, { "kind", r.Kind }, { "giver", r.Giver }, { "giverClan", r.GiverClan }, { "item", r.Item }, { "count", r.Count }, { "given", r.Given },
				{ "target", r.Target }, { "targetClan", r.TargetClan }, { "since", r.Since }, { "until", r.Until }, { "why", r.Why }, { "state", r.State }, { "taker", r.Taker } });
		return Json{ { "next", m_Next }, { "requests", all } }.dump();
	}

	bool RequestBoard::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		std::vector<Request> all;
		try
		{
			for (const Json& o : j.value("requests", Json::array()))
			{
				Request r;
				r.Id = o.at("id").get<int>();
				r.Kind = o.value("kind", std::string());
				r.Giver = o.value("giver", std::string());
				r.GiverClan = o.value("giverClan", -1);
				r.Item = o.value("item", std::string());
				r.Count = o.value("count", 0);
				r.Given = o.value("given", 0);
				r.Target = o.value("target", std::string());
				r.TargetClan = o.value("targetClan", -1);
				r.Since = o.value("since", 0.0);
				r.Until = o.value("until", 0.0);
				r.Why = o.value("why", std::string());
				r.State = o.value("state", std::string("open"));
				r.Taker = o.value("taker", std::string());
				all.push_back(std::move(r));
			}
		}
		catch (const std::exception&)
		{
			return false;
		}
		m_All = std::move(all);
		m_Next = j.value("next", 1);
		return true;
	}

	std::string RequestBoard::Report(const std::string& player) const
	{
		std::string s;
		int open = 0, done = 0, dropped = 0;
		std::vector<std::string> kinds;
		for (const Request& r : m_All)
		{
			if (!player.empty() && r.Taker != player) continue;
			(r.State == "open" ? open : r.State == "done" ? done : dropped)++;
			if (std::find(kinds.begin(), kinds.end(), r.Kind) == kinds.end()) kinds.push_back(r.Kind);
			s += "#" + std::to_string(r.Id) + " " + r.State + " " + r.Kind + " from " + r.Giver + (r.Item.empty() ? std::string() : " " + r.Item)
				+ (r.Count > 0 ? " " + std::to_string(r.Given) + "/" + std::to_string(r.Count) : std::string()) + (r.Target.empty() ? std::string() : " -> " + r.Target) + ": " + r.Why + "\n";
		}
		return s + std::to_string(open) + " open, " + std::to_string(done) + " done, " + std::to_string(dropped) + " dropped; " + std::to_string(kinds.size()) + " kinds";
	}
}
