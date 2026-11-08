#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cstdlib>

// V1 (docs/VISION_MUNDO_VIVO.md): the clan writes what it lives through into the world's history (GB/Game/History.h), and its members remember
// what they did, suffered or saw. The events are read off the lines the clan already says (Say, TellClan, the society's events): one place
// that knows which lines are events, and no change to what anybody decides, so a run is the same with a history or without one.
namespace GB
{
	namespace
	{
		bool Starts(const std::string& s, const char* prefix, std::string* rest = nullptr)
		{
			const size_t n = std::char_traits<char>::length(prefix);
			if (s.compare(0, n, prefix) != 0) return false;
			if (rest) *rest = s.substr(n);
			return true;
		}

		// The word up to the first of `stops` (or the end).
		std::string UpTo(const std::string& s, const char* stops)
		{
			const size_t at = s.find_first_of(stops);
			return at == std::string::npos ? s : s.substr(0, at);
		}
	}

	int Tribe::Happen(const std::string& kind, const std::string& who, const std::string& whom, const std::string& text, const glm::vec2* where)
	{
		if (!m_History) return 0;
		HistoryEvent e;
		e.At = m_History->HasClock() ? m_History->Clock() : WorldSeconds();   // (one clock for the whole history)
		e.Kind = kind;
		e.Who = who;
		e.Whom = whom;
		e.Clan = m_HistoryClan;
		e.Text = text;
		const TribeMember* doer = who.empty() ? nullptr : FindMember(who);
		const TribeMember* sufferer = whom.empty() ? nullptr : FindMember(whom);
		if (where) { e.Where = *where; e.HasWhere = true; }
		else if (doer && doer->Body) { e.Where = Where(*doer); e.HasWhere = true; }
		else if (sufferer && sufferer->Body) { e.Where = Where(*sufferer); e.HasWhere = true; }
		const int id = m_History->Add(e);
		if (doer) m_History->Remember(who, id, "did", m_History->FeelingOf(kind, "did"));
		if (sufferer) m_History->Remember(whom, id, "suffered", m_History->FeelingOf(kind, "suffered"));
		// who saw it: the living members near where it happened; a clan's event (no place) is known by everyone of the clan
		const float r = m_History->Rules.WitnessRadius;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body || m.Name == who || m.Name == whom) continue;
			if (!e.HasWhere) m_History->Remember(m.Name, id, "heard", m_History->FeelingOf(kind, "heard"));
			else if (glm::distance(Where(m), e.Where) <= r) m_History->Remember(m.Name, id, "saw", m_History->FeelingOf(kind, "saw"));
		}
		return id;
	}

	void Tribe::Chronicled(const TribeMember& m, const std::string& text)
	{
		const std::string& me = m.Name;
		std::string rest;
		// the event keeps the whole line, as the log says it ("Ana (forager) dies of Hunger"): the story and the translations read it as a log line
		const std::string line = me.empty() ? "the clan " + text : me + " (" + m.Role + ") " + text;
		if (Starts(text, "dies of "))
		{
			// a death in a fight is already the "killed" event (who killed whom): one event, not two
			const std::vector<HistoryEvent>& all = m_History->Events();
			for (size_t i = all.size(), n = 0; i-- > 0 && n < 8; ++n)
				if (all[i].Kind == "killed" && all[i].Whom == me && all[i].Clan == m_HistoryClan) return;
			Happen("death", std::string(), me, line);
		}
		else if (Starts(text, "has a child: ", &rest)) Happen("birth", me, UpTo(rest, " ("), line);
		else if (Starts(text, "discovers ")) Happen("discovery", me, std::string(), line);
		else if (Starts(text, "learns ")) Happen("learnt", me, std::string(), line);
		else if (Starts(text, "finishes ", &rest) && rest.find(": it stands") != std::string::npos) Happen("built", me, std::string(), line);
		else if (Starts(text, "is killed", &rest)) Happen("killed", Starts(rest, " by ", &rest) ? UpTo(rest, " (") : std::string(), me, line);
		else if (Starts(text, "is wounded (", &rest)) { const size_t by = rest.find(", by "); Happen("wounded", by == std::string::npos ? std::string() : UpTo(rest.substr(by + 5), ")"), me, line); }
		else if (Starts(text, "is back from the raid on ")) Happen("raid", me, std::string(), line);
		else if (Starts(text, "joins the clan")) Happen("joined", me, std::string(), line);
		else if (Starts(text, "leaves the clan")) Happen("left", me, std::string(), line);
		else if (Starts(text, "is the clan's shaman")) Happen("shaman", me, std::string(), line);
		else if (Starts(text, "shares its first kill")) Happen("first_kill", me, std::string(), line);
		else if (Starts(text, "kills ", &rest) && rest.find(": ") != std::string::npos) Happen("hunt", me, UpTo(rest, ":"), line);
		else if (Starts(text, "falls (")) Happen("fall", std::string(), me, line);
		// what a player does for others (a person's deeds are what people talk about: V2): a gift, a good load for the store
		else if (m.Player && Starts(text, "gives ", &rest) && rest.find(" to ") != std::string::npos) Happen("gift", me, rest.substr(rest.rfind(" to ") + 4), line);
		else if (m.Player && Starts(text, "leaves ", &rest) && rest.find(" in the store") != std::string::npos && std::atoi(rest.c_str()) >= 5) Happen("help", me, std::string(), line);
		else if (me.empty() && Starts(text, "enters the ")) Happen("era", std::string(), std::string(), line);
		else if (me.empty() && Starts(text, "moves its camp from ")) Happen("camp_moved", std::string(), std::string(), line);
	}

	// V2: a talk passes memories: the one who talks tells the other the weightiest thing it knows that the other does not (two for a story, and
	// about the person they gossip about when there is one); in a chat or a gossip the other tells one back. The listener remembers it as heard.
	void Tribe::Gossip(const TribeMember& a, const TribeMember& b, const std::string& kind, const std::string& about)
	{
		const double now = m_History->HasClock() ? m_History->Clock() : WorldSeconds();
		const std::string person = kind == "gossip" && !about.empty() && m_Society.Find(about) ? about : std::string();
		const int tells = kind == "story" ? 2 : 1;
		for (int i = 0; i < tells; ++i)
			if (const int e = m_History->Topic(a.Name, b.Name, person)) m_History->Tell(a.Name, b.Name, e, now);
		if (kind == "chat" || kind == "gossip")
			if (const int e = m_History->Topic(b.Name, a.Name)) m_History->Tell(b.Name, a.Name, e, now);
	}
}
