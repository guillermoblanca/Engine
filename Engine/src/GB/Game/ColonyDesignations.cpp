// T270: the work the player designates to the clan and the order of its sites (see ColonyDesignations.h).
#include "GB/Game/ColonyDesignations.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <set>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr float kOptionWeight = 1.6f;   // the gather a mark offers its holder (x the job's own bonus): above the usual work of a trade
		constexpr float kDesignateBase = 2.2f;  // the board's priority of a mark of priority 1 (JobRules::Priority "designate" says otherwise)
	}

	bool ColonyDesignations::NodePos(const Tribe& tribe, const std::string& node, glm::vec2& out)
	{
		if (!tribe.m_Entities) return false;
		const EntityId id = tribe.m_Entities->FindByName(node);
		if (id == NullEntity) return false;
		const glm::vec4 p = tribe.m_Entities->WorldMatrix(id)[3];
		out = { p.x, p.z };
		return true;
	}

	int ColonyDesignations::AmountOf(const Tribe& tribe, const std::string& node)
	{
		const AbilitySystemComponent* a = tribe.m_Gameplay ? tribe.m_Gameplay->Find(node) : nullptr;
		return a && a->HasTag("Resource.Node") ? ResourceSystem::Amount(*a) : -1;
	}

	void ColonyDesignations::Logged(Tribe& tribe, const std::string& text)
	{
		char t[48];
		std::snprintf(t, sizeof t, "%6.1f ", tribe.m_Time);
		tribe.m_Log.push_back(std::string(t) + text);
	}

	float ColonyDesignations::MemberPriority(const Tribe& tribe, const TribeMember& m, const std::string& node)
	{
		return tribe.WorkTable().MarkFactor(tribe, m, node);   // T306
	}

	float ColonyDesignations::PriorityFactor(int priority)
	{
		return std::clamp(1.1f - 0.1f * static_cast<float>(priority), 0.1f, 1.0f);
	}

	std::string ColonyDesignations::KindOf(const Tribe& tribe, const std::string& node)
	{
		const AbilitySystemComponent* a = tribe.m_Gameplay ? tribe.m_Gameplay->Find(node) : nullptr;
		if (!a || !a->HasTag("Resource.Node")) return {};
		for (const auto& [id, def] : tribe.m_Defs) if (a->HasTag("Resource.Node." + id)) return id;
		return {};
	}

	const ColonyDesignations::Mark* ColonyDesignations::MarkOf(const std::string& node) const
	{
		const auto it = m_Marks.find(node);
		return it == m_Marks.end() ? nullptr : &it->second;
	}

	ColonyDesignations::Reply ColonyDesignations::Designate(Tribe& tribe, const std::string& node, int priority)
	{
		if (priority < 1 || priority > 10) return { false, "the priority goes from 1 (first) to 10 (last)" };
		if (!tribe.m_Gameplay || !tribe.m_Gameplay->Find(node) || !tribe.m_Gameplay->Find(node)->HasTag("Resource.Node")) return { false, "there is no node called " + node };
		if (KindOf(tribe, node).empty()) return { false, node + " is not a node the clan can work" };
		if (const std::string why = TechVerbs::WhyNot(tribe, "designate", KindOf(tribe, node)); !why.empty()) return { false, "the clan " + why };   // T322
		if (AmountOf(tribe, node) <= 0) return { false, node + " is empty" };
		const bool again = m_Marks.count(node) != 0;
		Mark& mark = m_Marks[node];
		if (!again) mark.Since = tribe.m_Time;
		mark.Priority = priority;
		Logged(tribe, "the clan is asked to gather " + node + " first (priority " + std::to_string(priority) + ")");
		return { true, node + (again ? " stays marked, now with priority " : " is marked to gather (priority ") + std::to_string(priority) + (again ? "" : ")") };
	}

	ColonyDesignations::Reply ColonyDesignations::Clear(Tribe& tribe, const std::string& node, int priority, const std::string& field)
	{
		if (const auto it = m_Marks.find(node); it != m_Marks.end())
		{
			if (!it->second.Field.empty()) it->second.Priority = std::clamp(priority, 1, 10);   // (the player's own mark stays the player's)
			return { true, node + " is marked already" };
		}
		if (priority < 1 || priority > 10) return { false, "the priority goes from 1 (first) to 10 (last)" };
		if (!tribe.m_Gameplay || !tribe.m_Gameplay->Find(node) || !tribe.m_Gameplay->Find(node)->HasTag("Resource.Node")) return { false, "there is no node called " + node };
		const std::string kind = KindOf(tribe, node);
		if (kind.empty()) return { false, node + " is not a node the clan can work" };
		if (const std::string why = TechVerbs::WhyNot(tribe, "designate", kind); !why.empty()) return { false, "the clan " + why };
		if (AmountOf(tribe, node) <= 0) return { false, node + " is empty" };
		Mark& mark = m_Marks[node];
		mark.Since = tribe.m_Time;
		mark.Priority = priority;
		mark.Field = field;
		Logged(tribe, "the clan is to clear " + node + " out of " + field + " (priority " + std::to_string(priority) + ")");
		return { true, node + " is marked to clear out of " + field };
	}

	std::vector<std::string> ColonyDesignations::Stand(const Tribe& tribe, const std::string& node)
	{
		const std::string kind = KindOf(tribe, node);
		glm::vec2 origin;
		if (kind.empty() || !NodePos(tribe, node, origin)) return {};
		// the nodes of the kind near it, by name (the flood below walks them in that order: the same stand for the same world)
		std::map<std::string, glm::vec2> near;
		for (const GameplayWorld::Tagged& t : tribe.m_Gameplay->WithTag("Resource.Node." + kind))
		{
			glm::vec2 p;
			if (*t.Name == node || !NodePos(tribe, *t.Name, p) || glm::distance(p, origin) > kAllRadius) continue;
			if (ResourceSystem::Amount(*t.System) <= 0) continue;
			near.emplace(*t.Name, p);
		}
		std::vector<std::string> out{ node };
		std::deque<glm::vec2> front{ origin };
		while (!front.empty() && static_cast<int>(out.size()) < kAllMost)
		{
			const glm::vec2 at = front.front();
			front.pop_front();
			for (auto it = near.begin(); it != near.end() && static_cast<int>(out.size()) < kAllMost;)
			{
				if (glm::distance(it->second, at) > kLink) { ++it; continue; }
				out.push_back(it->first);
				front.push_back(it->second);
				it = near.erase(it);
			}
		}
		std::sort(out.begin() + 1, out.end());
		return out;
	}

	ColonyDesignations::Reply ColonyDesignations::DesignateAll(Tribe& tribe, const std::string& node, int priority)
	{
		if (priority < 1 || priority > 10) return { false, "the priority goes from 1 (first) to 10 (last)" };
		const Reply first = Designate(tribe, node, priority);
		if (!first.Ok) return first;
		int n = 0;
		for (const std::string& other : Stand(tribe, node))
		{
			Mark& mark = m_Marks[other];
			if (mark.Since == 0.0 && other != node) mark.Since = tribe.m_Time;
			mark.Priority = priority;
			++n;
		}
		Logged(tribe, "the clan is asked to gather the stand of " + node + " first (" + std::to_string(n) + " " + KindOf(tribe, node) + ", priority " + std::to_string(priority) + ")");
		return { true, std::to_string(n) + " " + KindOf(tribe, node) + " of the stand of " + node + " are marked to gather (priority " + std::to_string(priority) + ")" };
	}

	ColonyDesignations::Reply ColonyDesignations::Undesignate(Tribe& tribe, const std::string& node, bool all)
	{
		std::vector<std::string> lift{ node };
		if (all) lift = Stand(tribe, node);
		int n = 0;
		for (const std::string& x : lift) n += static_cast<int>(m_Marks.erase(x));
		if (n == 0) return { false, node + " is not marked" };
		Logged(tribe, "the mark on " + node + (n > 1 ? " and its stand" : std::string()) + " is lifted");
		return { true, n == 1 ? "the mark on " + node + " is lifted" : std::to_string(n) + " marks lifted (the stand of " + node + ")" };
	}

	// ------------------------------------------------------------------ the site queue
	std::vector<std::string> ColonyDesignations::SiteOrder(const Tribe& tribe) const
	{
		std::vector<std::string> open;
		for (const Tribe::Placed& p : tribe.Placements()) if (!p.State.Done) open.push_back(p.Name);
		std::vector<std::string> out;
		for (const std::string& s : m_Sites) if (std::find(open.begin(), open.end(), s) != open.end()) out.push_back(s);
		for (const std::string& s : open) if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
		return out;
	}

	bool ColonyDesignations::QueueSite(const Tribe& tribe, const std::string& site)
	{
		if (std::find(m_Sites.begin(), m_Sites.end(), site) != m_Sites.end()) return false;
		bool open = false;
		for (const Tribe::Placed& p : tribe.Placements()) open = open || (p.Name == site && !p.State.Done);
		if (!open) return false;
		// what was queued and is no open site any more goes (stood up, called off)
		const std::vector<std::string> order = SiteOrder(tribe);
		m_Sites.erase(std::remove_if(m_Sites.begin(), m_Sites.end(), [&](const std::string& s) { return std::find(order.begin(), order.end(), s) == order.end(); }), m_Sites.end());
		m_Sites.push_back(site);
		return true;
	}

	ColonyDesignations::Reply ColonyDesignations::MoveSite(const Tribe& tribe, const std::string& site, const std::string& how)
	{
		if (how != "up" && how != "down" && how != "top" && how != "bottom") return { false, "usage: game site <id> up|down|top|bottom" };
		std::vector<std::string> order = SiteOrder(tribe);
		const auto it = std::find(order.begin(), order.end(), site);
		if (it == order.end()) return { false, "no open site of the clan called '" + site + "'" };
		size_t i = static_cast<size_t>(it - order.begin());
		const std::string moved = order[i];
		order.erase(order.begin() + static_cast<std::ptrdiff_t>(i));
		if (how == "top") i = 0;
		else if (how == "bottom") i = order.size();
		else if (how == "up") i = i > 0 ? i - 1 : 0;
		else i = std::min(order.size(), i + 1);
		order.insert(order.begin() + static_cast<std::ptrdiff_t>(i), moved);
		m_Sites = order;
		return { true, "site " + site + " is now " + std::to_string(i + 1) + " of " + std::to_string(order.size()) + " in the queue" };
	}

	void ColonyDesignations::SiteGone(const std::string& site)
	{
		m_Sites.erase(std::remove(m_Sites.begin(), m_Sites.end(), site), m_Sites.end());
	}

	bool ColonyDesignations::OpenFor(const Tribe& tribe, const std::string& site, const std::string& kind, const TribeMember* asking) const
	{
		for (const Tribe::Placed& p : tribe.Placements())
		{
			if (p.Name != site) continue;
			if (p.State.Done) return false;
			if (kind == "build")
			{
				if (!p.State.Ready) return false;
				int builders = 0;
				for (const TribeMember& o : tribe.m_Members) if (&o != asking && !o.Dead && o.Job == "build" && o.Detail == site) ++builders;
				return builders < tribe.Config.Builders;
			}
			// deliver: something the phase lacks that nobody is bringing yet and the store has (or what stands in for it)
			for (const auto& [item, n] : p.State.Missing)
			{
				if (n - tribe.InFlight(site, item) <= 0) continue;
				int have = tribe.m_Store.Count(item);
				if (const auto sub = p.State.Substitutes.find(item); sub != p.State.Substitutes.end()) for (const std::string& other : sub->second) have += tribe.m_Store.Count(other);
				if (have > 0) return true;
			}
			return false;
		}
		return false;
	}

	float ColonyDesignations::SiteFactor(const Tribe& tribe, const std::string& site, const std::string& kind, const TribeMember* asking) const
	{
		if (m_Sites.empty()) return 1.0f;
		for (const std::string& s : SiteOrder(tribe))
		{
			if (s == site) return 1.0f;   // nothing before it can take this work now: it is the first
			if (OpenFor(tribe, s, kind, asking)) return kLater;
		}
		return 1.0f;
	}

	// ------------------------------------------------------------------ the tribe's hooks
	void ColonyDesignations::Post(Tribe& tribe)
	{
		if (!m_Sites.empty())   // the sites that stood up or were called off leave the queue
		{
			const std::vector<std::string> order = SiteOrder(tribe);
			m_Sites.erase(std::remove_if(m_Sites.begin(), m_Sites.end(), [&](const std::string& s) { return std::find(order.begin(), order.end(), s) == order.end(); }), m_Sites.end());
		}
		if (m_Marks.empty()) return;
		const JobRules& rules = tribe.Config.Work.Jobs;
		const auto base = rules.Priority.find("designate");
		const float top = base != rules.Priority.end() ? base->second : kDesignateBase;
		for (auto it = m_Marks.begin(); it != m_Marks.end();)
		{
			const std::string& node = it->first;
			const int amount = AmountOf(tribe, node);
			const std::string kind = KindOf(tribe, node);
			if (amount <= 0 || kind.empty())
			{
				Logged(tribe, "the mark on " + node + " is lifted: " + (amount < 0 || kind.empty() ? "it is gone" : "it is spent"));
				it = m_Marks.erase(it);
				continue;
			}
			tribe.m_Board.Post("designate", node, tribe.SkillFamilyOf(kind), static_cast<float>(amount), top * PriorityFactor(it->second.Priority), 1,
				"marked by you to gather (priority " + std::to_string(it->second.Priority) + ")", tribe.m_Time, rules);
			++it;
		}
	}

	float ColonyDesignations::JobScore(const Tribe& tribe, const TribeMember& m, const Job& job) const
	{
		if (!m.Body || !tribe.m_Items) return 0.0f;
		const std::string kind = KindOf(tribe, job.Target);
		const auto def = tribe.m_Defs.find(kind);
		if (def == tribe.m_Defs.end() || AmountOf(tribe, job.Target) <= 0) return 0.0f;
		Inventory& bag = m.Body->Bag();
		if (!def->second.RequiresTag.empty() && bag.CountTagged(*tribe.m_Items, def->second.RequiresTag) == 0) return 0.0f;   // the tool the node asks for
		if (!tribe.m_Knowledge.CanGather(m.Name, kind)) return 0.0f;   // a node only those who know a technology work (H11)
		if (tribe.m_Claims.HeldByOther(job.Target, m.Name)) return 0.0f;
		glm::vec2 at;
		if (!NodePos(tribe, job.Target, at) || !m.Body->CanReach(at)) return 0.0f;
		const AbilitySystemComponent* asc = tribe.m_Gameplay->Find(m.Name);
		const float vigor = asc && asc->Attributes().Has(NeedName::Vigor) ? asc->Attribute(NeedName::Vigor) : 1.0f;
		const float liking = std::max(0.6f, tribe.MemberWeight(m, kind));   // anyone helps; its trade, more
		return job.Priority * liking * Falloff(glm::distance(tribe.Where(m), at), 80.0f) * vigor * MemberPriority(tribe, m, job.Target);
	}

	void ColonyDesignations::Options(Tribe& tribe, TribeMember& m, std::vector<UtilityOption>& options) const
	{
		if (m_Marks.empty() || !m.Body || !tribe.m_Items) return;
		const Job* held = tribe.m_Board.ClaimOf(m.Name);
		if (!held || held->Kind != "designate") return;
		const auto mark = m_Marks.find(held->Target);
		const std::string kind = KindOf(tribe, held->Target);
		const auto def = tribe.m_Defs.find(kind);
		const int amount = AmountOf(tribe, held->Target);
		glm::vec2 at;
		if (mark == m_Marks.end() || def == tribe.m_Defs.end() || amount <= 0 || !NodePos(tribe, held->Target, at)) return;
		Inventory probe = m.Body->Bag();
		if (probe.Add(*tribe.m_Items, def->second.Item, std::max(1, def->second.PerGather)) <= 0) return;   // its bag is full: the store first
		// the player showed the clan where it is: it knows the node as it is now
		if (!m.Mind.Memory.Find(held->Target)) m.Mind.Memory.Observe({ held->Target, kind, at, amount, tribe.m_Time, false });
		UtilityOption o;
		o.Kind = "gather";
		o.Target = held->Target;
		o.Where = at;
		const float prio = PriorityFactor(mark->second.Priority);
		const float reach = Falloff(glm::distance(tribe.Where(m), at), 120.0f);
		const float agenda = tribe.AgendaFactor(m.Role, "gather", tribe.Hour(), tribe.CurrentSeason());
		o.Score = kOptionWeight * prio * reach * agenda;
		o.Factors.push_back({ "marked by you", kOptionWeight * prio });
		o.Factors.push_back({ "distance", reach });
		if (agenda != 1.0f) o.Factors.push_back({ "agenda", agenda });
		o.Note = held->Target + ": marked by you to gather (priority " + std::to_string(mark->second.Priority) + ")";
		options.push_back(o);
	}

	void ColonyDesignations::Gathered(Tribe& tribe, const TribeMember& m) const
	{
		if (m_Marks.empty()) return;
		if (const Job* held = tribe.m_Board.ClaimOf(m.Name); held && held->Kind == "designate" && held->Target == m.Detail) tribe.m_Board.Progress(m.Name, 1.0f, tribe.m_Time);
	}

	// ------------------------------------------------------------------ for people and saving
	std::string ColonyDesignations::Line() const
	{
		std::string s;
		if (!m_Marks.empty())
		{
			std::string list;
			int shown = 0, own = 0;
			for (const auto& [node, mark] : m_Marks)
			{
				if (!mark.Field.empty()) continue;   // (T337: a farm zone's, told by `game zone`)
				++own;
				if (shown++ < 6) list += (list.empty() ? "" : ", ") + node + " p" + std::to_string(mark.Priority);
			}
			if (own > 6) list += ", ...";
			if (own > 0) s = std::to_string(own) + (own == 1 ? " node" : " nodes") + " marked (" + list + ")";
		}
		if (!m_Sites.empty())
		{
			std::string list;
			for (const std::string& site : m_Sites) list += (list.empty() ? "" : ", ") + site;
			s += (s.empty() ? "" : "; ") + std::string("sites in order: ") + list;
		}
		return s;
	}

	std::string ColonyDesignations::ToJson() const
	{
		Json j;
		j["version"] = 1;
		Json marks = Json::object();
		for (const auto& [node, mark] : m_Marks)
		{
			marks[node] = Json{ { "priority", mark.Priority }, { "since", mark.Since } };
			if (!mark.Field.empty()) marks[node]["field"] = mark.Field;   // T337
		}
		j["marks"] = marks;
		j["sites"] = m_Sites;
		return j.dump();
	}

	bool ColonyDesignations::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || j.value("version", 0) != 1) return false;
		std::map<std::string, Mark> marks;
		if (j.contains("marks"))
		{
			if (!j["marks"].is_object()) return false;
			for (const auto& [node, jm] : j["marks"].items())
			{
				if (!jm.is_object() || !jm.contains("priority") || !jm["priority"].is_number_integer()) return false;
				Mark mark;
				mark.Priority = std::clamp(jm["priority"].get<int>(), 1, 10);
				mark.Since = jm.value("since", 0.0);
				if (jm.contains("field")) { if (!jm["field"].is_string()) return false; mark.Field = jm["field"].get<std::string>(); }   // T337
				marks[node] = mark;
			}
		}
		std::vector<std::string> sites;
		if (j.contains("sites"))
		{
			if (!j["sites"].is_array()) return false;
			for (const Json& s : j["sites"])
			{
				if (!s.is_string()) return false;
				sites.push_back(s.get<std::string>());
			}
		}
		m_Marks = std::move(marks);
		m_Sites = std::move(sites);
		return true;
	}
}
