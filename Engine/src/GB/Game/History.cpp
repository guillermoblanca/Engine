#include "GB/Game/History.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <set>

namespace GB
{
	using Json = nlohmann::json;

	HistoryRules DefaultHistoryRules()
	{
		HistoryRules r;
		// kind -> weight, then how it feels to whom did it, to whom suffered it and to whom saw it
		r.Kinds = {
			{ "death", { 5, 0.0f, -1.0f, -0.6f } },
			{ "killed", { 5, -0.2f, -1.0f, -0.8f } },
			{ "birth", { 4, 0.8f, 0.8f, 0.5f } },
			{ "pair", { 4, 0.8f, 0.8f, 0.3f } },
			{ "discovery", { 4, 0.9f, 0.0f, 0.5f } },
			{ "learnt", { 2, 0.6f, 0.0f, 0.2f } },
			{ "built", { 3, 0.7f, 0.0f, 0.4f } },
			{ "era", { 5, 0.0f, 0.0f, 0.6f } },
			{ "raid", { 4, 0.3f, -0.9f, -0.5f } },
			{ "wounded", { 3, 0.0f, -0.7f, -0.4f } },
			{ "hunt", { 2, 0.6f, 0.0f, 0.3f } },
			{ "first_kill", { 3, 0.9f, 0.0f, 0.5f } },
			{ "shaman", { 3, 0.7f, 0.0f, 0.3f } },
			{ "tradition", { 3, 0.0f, 0.0f, 0.3f } },
			{ "norm", { 2, 0.0f, 0.0f, 0.1f } },
			{ "joined", { 2, 0.4f, 0.0f, 0.2f } },
			{ "left", { 2, 0.0f, 0.0f, -0.2f } },
			{ "fall", { 2, 0.0f, -0.5f, -0.2f } },
			{ "camp_moved", { 4, 0.0f, 0.0f, 0.0f } },
			{ "contact", { 3, 0.0f, 0.0f, 0.2f } },
			{ "trade", { 2, 0.4f, 0.4f, 0.2f } },
			{ "gift", { 2, 0.5f, 0.6f, 0.3f } },
			{ "pact", { 3, 0.5f, 0.5f, 0.4f } },
			{ "alliance", { 4, 0.6f, 0.6f, 0.5f } },
			{ "truce", { 3, 0.3f, 0.3f, 0.3f } },
			{ "break", { 3, -0.4f, -0.6f, -0.4f } },
			{ "theft", { 3, 0.2f, -0.8f, -0.5f } },
			{ "marriage", { 4, 0.8f, 0.8f, 0.4f } },
			{ "move", { 2, 0.2f, 0.0f, 0.0f } },
			{ "found", { 5, 0.4f, 0.0f, 0.1f } },
			{ "leap", { 5, 0.0f, 0.0f, 0.1f } },   // V6: the years between two lives
			{ "rank", { 4, 0.6f, 0.0f, 0.4f } },   // V7: a settlement becomes a village, a town, a city
			{ "polity", { 5, 0.5f, 0.5f, 0.4f } },   // V7: clans bound by alliances
			{ "stance", { 2, 0.0f, 0.0f, 0.0f } },
			{ "help", { 2, 0.6f, 0.7f, 0.3f } },
		};
		return r;
	}

	void WorldHistory::Clear()
	{
		m_Events.clear();
		m_Memories.clear();
		m_Next = 1;
		Crossings.clear();
	}

	int WorldHistory::WeightOf(const std::string& kind) const
	{
		const auto it = Rules.Kinds.find(kind);
		return it != Rules.Kinds.end() ? it->second.Weight : 1;
	}

	float WorldHistory::FeelingOf(const std::string& kind, const std::string& how) const
	{
		const auto it = Rules.Kinds.find(kind);
		if (it == Rules.Kinds.end()) return 0.0f;
		if (how == "did") return it->second.Doer;
		if (how == "suffered") return it->second.Sufferer;
		return it->second.Witness * (how == "heard" ? 0.5f : 1.0f);
	}

	int WorldHistory::Add(HistoryEvent e)
	{
		e.Id = m_Next++;
		if (e.Weight <= 0) e.Weight = WeightOf(e.Kind);
		m_Events.push_back(std::move(e));
		if (m_Events.size() > Rules.MaxEvents + Rules.MaxEvents / 8) Prune();
		return m_Events.back().Id;
	}

	void WorldHistory::Remember(const std::string& person, int event, const std::string& how, float feeling, const std::string& from, double since)
	{
		if (person.empty() || !Find(event)) return;
		std::vector<Recollection>& mem = m_Memories[person];
		static const std::map<std::string, int> closeness = { { "did", 0 }, { "suffered", 1 }, { "saw", 2 }, { "heard", 3 } };
		auto rank = [&](const std::string& h) { const auto it = closeness.find(h); return it != closeness.end() ? it->second : 4; };
		for (Recollection& r : mem)
			if (r.Event == event)
			{
				if (rank(how) < rank(r.How)) { r.How = how; r.Feeling = feeling; r.From = from; }
				return;
			}
		mem.push_back({ event, how, feeling, from, since });
		if (mem.size() > Rules.MaxMemories)
		{
			// the least weighty, then the oldest, goes
			auto worst = mem.begin();
			int worstWeight = 1 << 30;
			for (auto it = mem.begin(); it != mem.end(); ++it)
			{
				const HistoryEvent* e = Find(it->Event);
				const int w = e ? e->Weight : 0;
				if (w < worstWeight) { worstWeight = w; worst = it; }
			}
			mem.erase(worst);
		}
	}

	const Recollection* WorldHistory::MemoryOf(const std::string& person, int event) const
	{
		const std::vector<Recollection>* mem = MemoriesOf(person);
		if (!mem) return nullptr;
		const auto it = std::find_if(mem->begin(), mem->end(), [&](const Recollection& r) { return r.Event == event; });
		return it != mem->end() ? &*it : nullptr;
	}

	int WorldHistory::Topic(const std::string& teller, const std::string& listener, const std::string& about, double until) const
	{
		const std::vector<Recollection>* mem = MemoriesOf(teller);
		if (!mem) return 0;
		const HistoryEvent* best = nullptr;
		for (const Recollection& r : *mem)
		{
			const HistoryEvent* e = Find(r.Event);
			if (!e || e->At > until || e->Who == listener || e->Whom == listener || MemoryOf(listener, e->Id)) continue;
			if (!about.empty() && e->Who != about && e->Whom != about) continue;
			if (!best || e->Weight > best->Weight || (e->Weight == best->Weight && e->Id > best->Id)) best = e;
		}
		return best ? best->Id : 0;
	}

	bool WorldHistory::Tell(const std::string& teller, const std::string& listener, int event, double now)
	{
		const Recollection* told = MemoryOf(teller, event);
		if (!told || teller == listener || MemoryOf(listener, event)) return false;
		Remember(listener, event, "heard", told->Feeling * Rules.Retold, teller, now);
		return MemoryOf(listener, event) != nullptr;
	}

	WorldHistory::Reputation WorldHistory::ReputationOf(const std::string& person) const
	{
		Reputation r;
		float sum = 0.0f;
		for (const auto& [who, mem] : m_Memories)
		{
			if (who == person) continue;
			bool knows = false;
			for (const Recollection& m : mem)
			{
				const HistoryEvent* e = Find(m.Event);
				if (!e || (e->Who != person && e->Whom != person)) continue;
				knows = true;
				if (e->Who == person) { sum += m.Feeling; ++r.Deeds; }
			}
			if (knows) ++r.Known;
		}
		r.Score = r.Deeds > 0 ? sum / static_cast<float>(r.Deeds) : 0.0f;
		return r;
	}

	int WorldHistory::KnownBy(int event) const
	{
		int n = 0;
		for (const auto& [who, mem] : m_Memories)
			for (const Recollection& m : mem)
				if (m.Event == event) { ++n; break; }
		return n;
	}

	const HistoryEvent* WorldHistory::Find(int id) const
	{
		// the ids grow with the vector: a binary search
		const auto it = std::lower_bound(m_Events.begin(), m_Events.end(), id, [](const HistoryEvent& e, int v) { return e.Id < v; });
		return it != m_Events.end() && it->Id == id ? &*it : nullptr;
	}

	const std::vector<Recollection>* WorldHistory::MemoriesOf(const std::string& person) const
	{
		const auto it = m_Memories.find(person);
		return it != m_Memories.end() ? &it->second : nullptr;
	}

	std::vector<const HistoryEvent*> WorldHistory::Recall(const std::string& person, size_t count) const
	{
		std::vector<const HistoryEvent*> out;
		if (const std::vector<Recollection>* mem = MemoriesOf(person))
			for (const Recollection& r : *mem)
				if (const HistoryEvent* e = Find(r.Event)) out.push_back(e);
		std::stable_sort(out.begin(), out.end(), [](const HistoryEvent* a, const HistoryEvent* b) { return a->Weight != b->Weight ? a->Weight > b->Weight : a->Id > b->Id; });
		if (out.size() > count) out.resize(count);
		return out;
	}

	std::vector<const HistoryEvent*> WorldHistory::OfClan(int clan, const std::string& kind) const
	{
		std::vector<const HistoryEvent*> out;
		for (const HistoryEvent& e : m_Events)
			if ((clan < 0 || e.Clan == clan || e.Other == clan) && (kind.empty() || e.Kind == kind)) out.push_back(&e);
		return out;
	}

	size_t WorldHistory::MemoryCount() const
	{
		size_t n = 0;
		for (const auto& [who, mem] : m_Memories) n += mem.size();
		return n;
	}

	void WorldHistory::Prune()
	{
		// keep MaxEvents: the lightest old events go first (the newest eighth always stays, so the latest small things can still be told)
		const size_t keep = Rules.MaxEvents, fresh = Rules.MaxEvents / 8;
		if (m_Events.size() <= keep) return;
		std::vector<size_t> order;
		for (size_t i = 0; i + fresh < m_Events.size(); ++i) order.push_back(i);
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return m_Events[a].Weight != m_Events[b].Weight ? m_Events[a].Weight < m_Events[b].Weight : a < b; });
		std::set<int> gone;
		for (size_t i = 0; i < order.size() && m_Events.size() - gone.size() > keep; ++i) gone.insert(m_Events[order[i]].Id);
		m_Events.erase(std::remove_if(m_Events.begin(), m_Events.end(), [&](const HistoryEvent& e) { return gone.count(e.Id) > 0; }), m_Events.end());
		for (auto& [who, mem] : m_Memories)
			mem.erase(std::remove_if(mem.begin(), mem.end(), [&](const Recollection& r) { return gone.count(r.Event) > 0; }), mem.end());
	}

	std::string WorldHistory::ToJson() const
	{
		Json events = Json::array();
		for (const HistoryEvent& e : m_Events)
		{
			Json o = { { "id", e.Id }, { "at", e.At }, { "kind", e.Kind }, { "text", e.Text }, { "w", e.Weight } };
			if (!e.Who.empty()) o["who"] = e.Who;
			if (!e.Whom.empty()) o["whom"] = e.Whom;
			if (e.Clan >= 0) o["clan"] = e.Clan;
			if (e.Other >= 0) o["other"] = e.Other;
			if (e.HasWhere) o["where"] = { e.Where.x, e.Where.y };
			events.push_back(std::move(o));
		}
		Json memories = Json::object();
		for (const auto& [who, mem] : m_Memories)
		{
			Json list = Json::array();
			for (const Recollection& r : mem)
			{
				Json one = { r.Event, r.How, std::round(static_cast<double>(r.Feeling) * 100.0) / 100.0 };
				if (!r.From.empty() || r.Since > 0.0) { one.push_back(r.From); one.push_back(r.Since); }   // V2: a rumour says who told it and when
				list.push_back(std::move(one));
			}
			memories[who] = std::move(list);
		}
		return Json{ { "next", m_Next }, { "events", events }, { "memories", memories } }.dump();
	}

	bool WorldHistory::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		WorldHistory h;
		h.Rules = Rules;
		try
		{
			h.m_Next = j.value("next", 1);
			for (const Json& o : j.value("events", Json::array()))
			{
				HistoryEvent e;
				e.Id = o.at("id").get<int>();
				e.At = o.value("at", 0.0);
				e.Kind = o.value("kind", std::string());
				e.Text = o.value("text", std::string());
				e.Weight = o.value("w", 1);
				e.Who = o.value("who", std::string());
				e.Whom = o.value("whom", std::string());
				e.Clan = o.value("clan", -1);
				e.Other = o.value("other", -1);
				if (o.contains("where")) { e.HasWhere = true; e.Where = { o["where"][0].get<float>(), o["where"][1].get<float>() }; }
				if (!h.m_Events.empty() && e.Id <= h.m_Events.back().Id) return false;
				h.m_Events.push_back(std::move(e));
			}
			const Json memories = j.value("memories", Json::object());   // (kept: items() of a temporary would dangle)
			for (const auto& [who, list] : memories.items())
				for (const Json& r : list)
					h.m_Memories[who].push_back({ r[0].get<int>(), r[1].get<std::string>(), r[2].get<float>(), r.size() > 3 ? r[3].get<std::string>() : std::string(), r.size() > 4 ? r[4].get<double>() : 0.0 });
		}
		catch (const std::exception&)
		{
			return false;
		}
		*this = std::move(h);
		return true;
	}

	namespace
	{
		std::string DayOf(double at, double daySeconds)
		{
			char b[32];
			std::snprintf(b, sizeof b, "day %d", static_cast<int>(at / std::max(1.0, daySeconds)) + 1);
			return b;
		}
	}

	std::string WorldHistory::Report(int clan, size_t count, double daySeconds) const
	{
		const std::vector<const HistoryEvent*> all = OfClan(clan);
		std::string s;
		for (size_t i = all.size() > count ? all.size() - count : 0; i < all.size(); ++i)
		{
			const HistoryEvent& e = *all[i];
			s += DayOf(e.At, daySeconds) + " [" + e.Kind + "] " + e.Text + "\n";
		}
		return s + std::to_string(m_Events.size()) + " events, " + std::to_string(MemoryCount()) + " memories";   // (last: a long report is read from its end)
	}

	std::string WorldHistory::MemoryReport(const std::string& person, double daySeconds, const std::string& kind) const
	{
		std::string s = person + " remembers:";
		const std::vector<Recollection>* mem = MemoriesOf(person);
		if (!mem || mem->empty()) return s + " nothing yet";
		for (const HistoryEvent* e : Recall(person, kind.empty() ? 20 : 1000))
		{
			if (!kind.empty() && e->Kind != kind) continue;
			const auto r = std::find_if(mem->begin(), mem->end(), [&](const Recollection& x) { return x.Event == e->Id; });
			char f[16];
			std::snprintf(f, sizeof f, "%+.1f", static_cast<double>(r->Feeling));
			s += "\n" + DayOf(e->At, daySeconds) + " (" + r->How + (r->From.empty() ? std::string() : " from " + r->From) + ", " + f + ") ["+ e->Kind + "] " + e->Text;
		}
		return s;
	}
}
