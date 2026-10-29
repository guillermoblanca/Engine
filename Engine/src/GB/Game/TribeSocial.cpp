// Social life of the tribe (docs/VIDA_SOCIAL.md; the rules and the state are in GB/Game/Social.h): mood and grief, conversations between
// members, rites and rites of passage gathered in a circle, beliefs (taboos, omens, the sacred place, the shaman), what each food sates and the
// variety of the diet, and the clan's snares and pit traps. Kept apart from Tribe.cpp like the other systems; the hooks there are one line each:
// Update runs StepSocial, Decide calls SocialOptions (its options and the mood on every option), Execute and Finish hand "talk", "rite" and
// "snare" here, Eat tells Ate what was eaten, Die tells SocialDied, the society's events go through SocialHears (TribeSociety.cpp), the hunts
// of H14 call Hunted and HuntAllowed (TribeConflict.cpp), and the save has a "social" key. Deterministic like the rest: every die is SocialRoll.
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265f;
		constexpr float kCircle = 2.0f;      // a rite's circle is this much more than its radius: who is within it attends
		constexpr float kMeet = 4.0f;        // two who talk must be this close at the end
		void Factor(UtilityOption& o, const char* name, float value) { o.Factors.push_back({ name, value }); }

		std::string GoTo(const glm::vec2& at, const std::string& tail)
		{
			char b[64];
			std::snprintf(b, sizeof b, "goto %.2f %.2f | ", static_cast<double>(at.x), static_cast<double>(at.y));
			return b + tail;
		}
		std::string WaitFor(float s)
		{
			char b[32];
			std::snprintf(b, sizeof b, "wait %.1f", static_cast<double>(std::max(0.1f, s)));
			return b;
		}
		std::string Word(const std::string& s, int n)   // the n-th word of a detail ("story Biel listening")
		{
			size_t from = 0;
			for (int i = 0; i < n; ++i) { from = s.find(' ', from); if (from == std::string::npos) return {}; ++from; }
			const size_t to = s.find(' ', from);
			return s.substr(from, to == std::string::npos ? std::string::npos : to - from);
		}
		std::string Num(float v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", static_cast<double>(v)); return b; }
		bool Idle(const TribeMember& m)
		{
			return m.Job.empty() || m.Job == "rest" || m.Job == "warm" || m.Job == "socialize" || m.Job == "regroup";
		}
		// jobs that keep a member from a rite (it fights, flees, raids, is away on an expedition or moving camp)
		bool Away(const TribeMember& m)
		{
			return m.Job == "fight" || m.Job == "flee" || m.Job == "raid" || m.Job == "expedition" || m.Job == "migrate" || m.Job == "hunt" || m.Job == "dead";
		}
	}

	// ------------------------------------------------------------------ members
	SocialMember& Tribe::SocialOf(const std::string& name)
	{
		const auto it = m_Social.Members.find(name);
		if (it != m_Social.Members.end()) return it->second;
		SocialMember& m = m_Social.Members[name];
		m.Mood = Config.Social.Mood.Start;
		return m;
	}

	void Tribe::MoveMood(const std::string& name, float delta)
	{
		SocialMember& s = SocialOf(name);
		const float before = s.Mood;
		s.Mood = std::clamp(s.Mood + delta, 0.0f, 1.0f);
		const MoodRules& r = Config.Social.Mood;
		const auto m = std::find_if(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return x.Name == name; });
		if (m == m_Members.end()) return;
		if (before >= r.Low && s.Mood < r.Low) Say(*m, "is downcast (mood " + Num(s.Mood) + ")");
		else if (before <= r.High && s.Mood > r.High) Say(*m, "is in high spirits (mood " + Num(s.Mood) + ")");
	}

	float Tribe::Mood(const std::string& member) const
	{
		const SocialMember* s = m_Social.Find(member);
		return s ? s->Mood : Config.Social.Mood.Start;
	}

	float Tribe::Grief(const std::string& member) const
	{
		const SocialMember* s = m_Social.Find(member);
		return s ? s->Grief : 0.0f;
	}

	bool Tribe::ClanKnowsTech(const std::string& tech) const
	{
		if (tech.empty()) return true;
		if (m_Knowledge.Gated())
		{
			for (const TribeMember& m : m_Members)
				if (!m.Dead && m.Body && m_Knowledge.Knows(m.Name, tech)) return true;
			return false;
		}
		return !m_KnowsTech || m_KnowsTech(tech);   // (a tribe without technologies: nothing is gated)
	}

	// ------------------------------------------------------------------ the clock
	void Tribe::StepSocial(float dt)
	{
		const SocialRules& R = Config.Social;
		if (!R.On) return;
		int alive = 0;
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) ++alive;
		if (alive == 0) return;
		// the clan's beliefs, drawn once from the seed and its name
		if (m_Social.Beliefs.empty())
			for (const auto& [id, sys] : R.Beliefs.Systems)
				m_Social.Beliefs[id] = std::clamp(sys.Start + sys.Spread * (2.0f * SocialRoll(R.Seed, m_ClanName + "/" + id, 0) - 1.0f), 0.0f, 1.0f);
		// the sacred place: its totem (a meeting place) if it has one, else a place it can reach SacredDistance from the camp
		if (!m_Social.HasSacred)
		{
			glm::vec2 at = Config.Camp;
			bool found = false;
			for (const SettlementSite& s : SettlementSites())
				if (s.State.Done && m_Buildings)
					if (const auto d = m_Buildings->find(s.Building); d != m_Buildings->end() && d->second.Culture > 0.0f && d->second.Warmth <= 0.0f) { at = s.Pos; found = true; break; }
			if (!found)
			{
				const float angle = SocialRoll(R.Seed, m_ClanName + "/sacred", 0) * 2.0f * kPi;
				const TribeMember* walker = nullptr;
				for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) { walker = &m; break; }
				at = Config.Camp + glm::vec2(std::cos(angle), std::sin(angle)) * R.Beliefs.SacredDistance;
				for (int k = 1; k < 12 && walker && !walker->Body->CanStandAt(at); ++k)
					at = Config.Camp + glm::vec2(std::cos(angle + 0.5f * static_cast<float>(k)), std::sin(angle + 0.5f * static_cast<float>(k))) * R.Beliefs.SacredDistance;
			}
			m_Social.Sacred = at;
			m_Social.HasSacred = true;
			char b[96];
			std::snprintf(b, sizeof b, "adopts a sacred place at (%.0f, %.0f)", static_cast<double>(at.x), static_cast<double>(at.y));
			TellClan(b);
		}
		// the days: grief fades, the mood returns to its rest, the rare omens, the shaman
		m_Social.DayTimer += static_cast<double>(dt);
		const double day = static_cast<double>(std::max(1.0f, Config.Needs.DaySeconds));
		if (m_Social.DayTimer >= day) { m_Social.DayTimer -= day; SocialDay(); }

		m_Social.StepTimer += dt;
		if (m_Social.StepTimer < R.StepSeconds) return;
		m_Social.StepTimer = 0.0f;
		const double now = m_Time;
		const Season season = CurrentSeason();
		const float progress = Config.Seasons.Progress(WorldSeconds());
		const int year = static_cast<int>(WorldSeconds() / static_cast<double>(std::max(1.0f, Config.Seasons.SeasonSeconds * 4.0f)));
		// the seasonal rites, once a year each
		for (const auto& [id, d] : R.Rites)
		{
			if (d.Season.empty() || d.Season != SeasonClock::Name(season) || progress < d.At) continue;
			int& last = m_Social.Years[id];
			if (last == year + 1) continue;
			last = year + 1;
			CallRite(id, std::string(), 0.0f);
		}
		// the traditions the clan keeps come back as rites
		for (const auto& [id, d] : R.Rites)
		{
			if (d.Event != "tradition" || d.Every <= 0.0f) continue;
			for (const std::string& t : m_Society.Traditions())
			{
				const auto last = m_Social.Recurring.find(id + ":" + t);
				if (last == m_Social.Recurring.end() || now - last->second < static_cast<double>(d.Every) * day) continue;
				const auto def = Config.Society.Culture.Traditions.find(t);
				if (CallRite(id, def != Config.Society.Culture.Traditions.end() ? def->second.Name : t, d.Delay)) m_Social.Recurring[id + ":" + t] = now;
			}
		}
		// the storm of the season, once per season it comes in
		if (std::string(SeasonClock::Weather(season, progress)) == "storm")
		{
			int& seen = m_Social.Years["omen.storm." + std::string(SeasonClock::Name(season))];
			if (seen != year + 1) { seen = year + 1; Omen("storm"); }
		}
		// a harvest of the fields (H18)
		int harvested = 0;
		for (const auto& [item, n] : m_Econ.Harvested) harvested += n;
		int& seenHarvest = m_Social.Counts["harvest.seen"];
		if (harvested > seenHarvest) { seenHarvest = harvested; Omen("harvest"); }
		// the gravely wounded, when a rite can heal them (H14)
		if (m_Combat)
			for (const TribeMember& m : m_Members)
			{
				if (m.Dead || !m.Body) continue;
				const Combatant* c = m_Combat->Find(m.Name);
				if (!c || c->Severity() < m_Combat->Rules.Injuries.RestAbove * 2.0f) continue;
				double& last = m_Social.Recurring["wounded:" + m.Name];
				if (last != 0.0 && now - last < day * 0.5) continue;
				last = std::max(now, 0.001);
				CallRites("wounded", m.Name);
			}
		StepRites();
		StepTraps();
		// the gear of the social lane the clan wants made (H18's plan makes it): the cost of the traps it may set, and the tool of a food node it
		// knows and nobody carries (the fish trap of a fishing spot)
		for (const auto& [id, t] : R.Hunting.Traps)
			for (const auto& [item, n] : t.Cost)
				if (m_Recipes && m_Items && m_Items->Find(item) && m_Recipes->Find(item)) SetDemand("social:" + id, item, ClanKnowsTech(t.Tech) ? n * std::max(1, t.Max) : 0);
		for (const auto& [kind, def] : m_Defs)
		{
			if (def.RequiresTag.empty() || !IsFood(def.Item) || !m_Items) continue;
			std::string tool;
			for (const std::string& rid : m_Recipes->Ids())
				if (const Recipe* rec = m_Recipes->Find(rid))
					if (const ItemDef* out = m_Items->Find(rec->Output); out && std::find(out->Tags.begin(), out->Tags.end(), def.RequiresTag) != out->Tags.end()) { tool = rec->Output; break; }
			if (tool.empty()) continue;
			bool carried = false;
			for (const TribeMember& m : m_Members) carried = carried || (!m.Dead && m.Body && m.Body->Bag().CountTagged(*m_Items, def.RequiresTag) > 0);
			SetDemand("social:gear", tool, !carried && !m_Clan.OfKind(kind).empty() ? 1 : 0);
		}
	}

	void Tribe::SocialDay()
	{
		const SocialRules& R = Config.Social;
		++m_Social.Day;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			SocialMember& s = SocialOf(m.Name);
			s.Grief = std::max(0.0f, s.Grief - R.Grief.DecayPerDay);
			const float rest = std::clamp(R.Mood.Rest - R.Mood.Grief * s.Grief, 0.0f, 1.0f);
			s.Mood = std::clamp(s.Mood + (rest - s.Mood) * R.Mood.Return, 0.0f, 1.0f);
		}
		if (m_Social.DeathsToday >= 2) Omen("two_deaths");
		m_Social.DeathsToday = 0;
		for (const auto& [id, o] : R.Beliefs.Omens)
			if (o.Chance > 0.0f && SocialRoll(R.Seed, m_ClanName + "/omen/" + id, m_Social.Day) < o.Chance) Omen(o.Event);
		// the shaman: the living adult with the best score of its traits, age and the rites it presided over
		std::string best;
		float top = -1.0f;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body || m.Player) continue;
			const Person* p = m_Society.Find(m.Name);
			const float age = p ? p->Age : 30.0f;
			if (age < R.Shaman.MinAge) continue;
			float score = R.Shaman.Age * age + R.Shaman.Rites * static_cast<float>(SocialOf(m.Name).Led);
			for (const auto& [trait, w] : R.Shaman.Traits)
				if (p) { const auto t = p->Traits.find(trait); score += w * (t == p->Traits.end() ? 0.5f : t->second); }
			if (score > top + 1e-6f) { top = score; best = m.Name; }
		}
		if (best != m_Social.Shaman && !best.empty())
		{
			m_Social.Shaman = best;
			if (TribeMember* m = MemberNamed(best)) Say(*m, "is the clan's shaman now: it presides over the rites");
		}
	}

	// ------------------------------------------------------------------ omens, events, deaths
	void Tribe::Omen(const std::string& event)
	{
		const SocialRules& R = Config.Social;
		if (!R.On) return;
		for (const auto& [id, o] : R.Beliefs.Omens)
		{
			if (o.Event != event) continue;
			++m_Social.Counts["omen." + id];
			for (const TribeMember& m : m_Members) if (!m.Dead && m.Body && o.Mood != 0.0f) MoveMood(m.Name, o.Mood);
			for (const auto& [sys, w] : o.Beliefs) if (m_Social.Beliefs.count(sys)) m_Social.Beliefs[sys] = std::clamp(m_Social.Beliefs[sys] + w, 0.0f, 1.0f);
			TellClan("reads an omen: " + o.Name + (o.Mood < 0.0f ? " (a bad sign)" : o.Mood > 0.0f ? " (a good sign)" : std::string()));
			if (!o.Rite.empty()) CallRite(o.Rite, o.Name, R.Rites.count(o.Rite) ? R.Rites.at(o.Rite).Delay : 0.0f);
		}
	}

	void Tribe::SocialDied(const TribeMember& dead)
	{
		const SocialRules& R = Config.Social;
		if (!R.On) return;
		++m_Social.DeathsToday;
		std::vector<std::string>& called = m_Social.Rite.Called;   // (the dead leave the circle at once)
		called.erase(std::remove(called.begin(), called.end(), dead.Name), called.end());
		for (const TribeMember& m : m_Members)
		{
			if (&m == &dead || m.Dead || !m.Body) continue;
			SocialMember& s = SocialOf(m.Name);
			float grief = 0.0f;
			if (!m_Society.Kin(m.Name, dead.Name).empty()) grief = R.Grief.Kin;
			else if (const float a = m_Society.Affinity(Config.Society, m.Name, dead.Name); a > 0.0f) grief = R.Grief.Friend * a;
			s.Grief = std::clamp(s.Grief + grief, 0.0f, 1.0f);
			MoveMood(m.Name, R.Grief.Mood - 0.2f * grief);
		}
		CallRites("death", dead.Name);
	}

	void Tribe::SocialHears(const SocietyEvent& e)
	{
		if (!Config.Social.On) return;
		if (e.Kind == "adult") CallRites("adult", e.Who);
		else if (e.Kind == "pair") CallRites("pair", e.Who + " & " + e.Other);
		else if (e.Kind == "birth") CallRites("birth", e.Who);
		else if (e.Kind == "tradition")
		{
			const auto def = Config.Society.Culture.Traditions.find(e.Who);
			const std::string name = def != Config.Society.Culture.Traditions.end() ? def->second.Name : e.Who;
			for (const auto& [id, d] : Config.Social.Rites)
				if (d.Event == "tradition" && CallRite(id, name, d.Delay)) m_Social.Recurring[id + ":" + e.Who] = m_Time;
		}
	}

	void Tribe::Hunted(TribeMember& hunter, const HuntRule& rule, const std::string& animal)
	{
		if (!Config.Social.On) return;
		SocialMember& s = SocialOf(hunter.Name);
		++s.Kills;
		if (s.Kills == 1)
		{
			CallRites("first_kill", hunter.Name);
			// the taboo of the shared kill: the first kill goes whole to the store, and everyone thanks the hunter
			for (const auto& [id, t] : Config.Social.Beliefs.Taboos)
			{
				if (t.Kind != "share_kill" || !m_Social.Holds(Config.Social, id) || !hunter.Body) continue;
				std::vector<std::string> items = { rule.Item };
				if (!rule.Byproduct.empty()) items.push_back(rule.Byproduct);
				for (const auto& [item, n] : rule.Yields) items.push_back(item);
				int moved = 0;
				for (const std::string& item : items)
				{
					const int have = hunter.Body->Bag().Count(item);
					if (have <= 0 || !m_Items->Find(item)) continue;
					const int n = hunter.Body->Bag().RemoveOldest(item, have);
					m_Store.Add(*m_Items, item, n);
					moved += n;
				}
				for (const TribeMember& o : m_Members) if (&o != &hunter && !o.Dead && o.Body) Relate(o.Name, hunter.Name, "food");
				++m_Social.Counts["taboo." + id];
				Say(hunter, "shares its first kill (" + animal + ") with the clan: " + std::to_string(moved) + " things to the store (" + t.Name + ")");
				break;
			}
		}
		if (rule.Units >= 3) Omen("big_kill");
	}

	bool Tribe::HuntAllowed(const glm::vec2& at) const
	{
		if (!Config.Social.On || !m_Social.HasSacred) return true;
		for (const auto& [id, t] : Config.Social.Beliefs.Taboos)
			if (t.Kind == "sacred_hunt" && m_Social.Holds(Config.Social, id) && glm::distance(at, m_Social.Sacred) < t.Radius) return false;
		return true;
	}

	// ------------------------------------------------------------------ rites
	void Tribe::CallRites(const std::string& event, const std::string& subject)
	{
		for (const auto& [id, d] : Config.Social.Rites) if (d.Event == event) CallRite(id, subject, d.Delay);
	}

	bool Tribe::CallRite(const std::string& rite, const std::string& subject, float delay, std::string* why)
	{
		const auto d = Config.Social.Rites.find(rite);
		auto fail = [&](const std::string& text) { if (why) *why = text; return false; };
		if (!Config.Social.On) return fail("the clan has no social rules (the social section of the content)");
		if (d == Config.Social.Rites.end()) return fail("no rite " + rite);
		if (!ClanKnowsTech(d->second.Tech)) return fail("the clan does not know " + m_Knowledge.Name(d->second.Tech));
		if (d->second.Shaman && m_Social.Shaman.empty()) return fail("the clan has no shaman");
		if (m_Social.Rite.Rite == rite && m_Social.Rite.Subject == subject) return fail("it is being held now");
		for (const PendingRite& p : m_Social.Agenda) if (p.Rite == rite && p.Subject == subject) return fail("it is on the agenda already");
		if (m_Social.Agenda.size() >= 16) return fail("the agenda is full");
		m_Social.Agenda.push_back({ rite, subject, m_Time + static_cast<double>(delay), 0 });
		return true;
	}

	bool Tribe::StartRite(const std::string& rite, const std::string& subject, std::string* why)
	{
		const auto d = Config.Social.Rites.find(rite);
		if (d == Config.Social.Rites.end()) { if (why) *why = "no rite " + rite; return false; }
		if (m_Social.Rite.Active()) { if (why) *why = "the clan is holding " + Config.Social.Rites.at(m_Social.Rite.Rite).Name + " now"; return false; }
		m_Social.Agenda.erase(std::remove_if(m_Social.Agenda.begin(), m_Social.Agenda.end(), [&](const PendingRite& p) { return p.Rite == rite && p.Subject == subject; }), m_Social.Agenda.end());
		if (!CallRite(rite, subject, 0.0f, why)) return false;
		std::rotate(m_Social.Agenda.begin(), m_Social.Agenda.end() - 1, m_Social.Agenda.end());   // (to the front)
		m_Social.StepTimer = Config.Social.StepSeconds;   // (it starts at the next step)
		return true;
	}

	glm::vec2 Tribe::RitePlace(const RiteDef& def) const
	{
		if (def.Place == "fire" && HasFire()) return FirePos();
		if (def.Place == "sacred" && m_Social.HasSacred) return m_Social.Sacred;
		if (def.Place == "totem" || def.Place == "fire")
		{
			// a finished meeting place (a building with culture: the totem; the hearth for a fire rite when there is no campfire)
			for (const SettlementSite& s : SettlementSites())
			{
				if (!s.State.Done || !m_Buildings) continue;
				const auto b = m_Buildings->find(s.Building);
				if (b == m_Buildings->end() || b->second.Culture <= 0.0f) continue;
				if (def.Place == "totem" ? b->second.Warmth <= 0.0f : b->second.Warmth > 0.0f) return s.Pos;
			}
			if (def.Place == "totem" && HasFire()) return FirePos();
		}
		return Config.Camp;
	}

	glm::vec2 Tribe::CircleSpot(const std::string& member) const
	{
		const ActiveRite& r = m_Social.Rite;
		const auto it = std::find(r.Called.begin(), r.Called.end(), member);
		const size_t n = std::max<size_t>(1, r.Called.size());
		const size_t i = it == r.Called.end() ? 0 : static_cast<size_t>(it - r.Called.begin());
		const float radius = std::max(2.5f, 0.45f * static_cast<float>(n));
		const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
		glm::vec2 at = r.Place + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
		if (const TribeMember* m = FindMember(member); m && m->Body)
			for (int k = 1; k < 6 && !m->Body->CanStandAt(at); ++k) at = r.Place + glm::vec2(std::cos(angle), std::sin(angle)) * (radius * (1.0f - 0.15f * static_cast<float>(k)));
		return at;
	}

	bool Tribe::BeginRite(PendingRite& p)
	{
		const RiteDef& d = Config.Social.Rites.at(p.Rite);
		if (d.Shaman && m_Social.Shaman.empty()) { TellClan("cannot hold " + d.Name + ": it has no shaman"); return true; }   // (dropped)
		// its cost, from the store ("food" = any food, the oldest first); after three tries it is held humbly, without it
		bool paid = true;
		for (const auto& [item, n] : d.Cost) paid = paid && (item == "food" ? FoodIn(m_Store) >= n : m_Store.Count(item) >= n);
		if (!paid && p.Tries < 2)
		{
			++p.Tries;
			p.Due = m_Time + 60.0;
			return false;
		}
		if (paid)
			for (const auto& [item, n] : d.Cost)
			{
				if (item != "food") { m_Store.RemoveOldest(item, n); continue; }
				for (int k = 0; k < n; ++k) { const std::string f = FirstFood(m_Store); if (f.empty() || m_Store.RemoveOldest(f, 1) != 1) break; }
			}
		ActiveRite& r = m_Social.Rite;
		r = ActiveRite();
		r.Rite = p.Rite;
		r.Subject = p.Subject;
		r.Humble = !paid;
		r.Place = RitePlace(d);
		r.Start = m_Time;
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body && !Away(m)) r.Called.push_back(m.Name);
		if (r.Called.empty()) { r = ActiveRite(); return true; }
		// who presides: the shaman if it comes, else the eldest
		if (std::find(r.Called.begin(), r.Called.end(), m_Social.Shaman) != r.Called.end()) r.Leader = m_Social.Shaman;
		else
		{
			float eldest = -1.0f;
			for (const std::string& n : r.Called) if (const Person* q = m_Society.Find(n); q && q->Age > eldest) { eldest = q->Age; r.Leader = n; }
			if (r.Leader.empty()) r.Leader = r.Called.front();
		}
		for (TribeMember& m : m_Members)
			if (std::find(r.Called.begin(), r.Called.end(), m.Name) != r.Called.end() && Idle(m)) m.Mind.NextThink = std::min(m.Mind.NextThink, m_Time);
		const char* where = d.Place == "sacred" ? "the sacred place" : d.Place == "totem" ? "the meeting place" : d.Place == "fire" ? "the fire" : "the camp";
		TellClan("calls everyone to " + d.Name + (p.Subject.empty() ? std::string() : " for " + p.Subject) + " at " + where + (r.Humble ? " (humbly: it could not pay for it)" : std::string()));
		return true;
	}

	void Tribe::StepRites()
	{
		ActiveRite& r = m_Social.Rite;
		if (!r.Active())
		{
			for (size_t i = 0; i < m_Social.Agenda.size(); ++i)
			{
				PendingRite& p = m_Social.Agenda[i];
				if (p.Due > m_Time) continue;
				if (!Config.Social.Rites.count(p.Rite)) { m_Social.Agenda.erase(m_Social.Agenda.begin() + static_cast<std::ptrdiff_t>(i)); return; }
				PendingRite copy = p;
				if (BeginRite(copy)) m_Social.Agenda.erase(m_Social.Agenda.begin() + static_cast<std::ptrdiff_t>(i));
				else m_Social.Agenda[i] = copy;
				return;
			}
			return;
		}
		const auto def = Config.Social.Rites.find(r.Rite);
		if (def == Config.Social.Rites.end()) { EndRite("its rule is gone"); return; }
		const RiteDef& d = def->second;
		// the dead leave the circle
		r.Called.erase(std::remove_if(r.Called.begin(), r.Called.end(), [&](const std::string& n) { const TribeMember* m = FindMember(n); return !m || m->Dead || !m->Body; }), r.Called.end());
		if (r.Called.empty()) { EndRite("nobody is left to hold it"); return; }
		const float radius = std::max(2.5f, 0.45f * static_cast<float>(r.Called.size())) + kCircle;
		int present = 0;
		for (const std::string& n : r.Called) if (const TribeMember* m = FindMember(n); m && glm::distance(Where(*m), r.Place) <= radius) ++present;
		if (r.Hold < 0.0)
		{
			const int quorum = static_cast<int>(std::ceil(d.Quorum * static_cast<float>(r.Called.size())));
			if (present >= std::max(1, quorum) || m_Time - r.Start >= static_cast<double>(d.GatherSeconds))
			{
				r.Hold = m_Time;
				TellClan(d.Name + " begins: " + std::to_string(present) + " of " + std::to_string(r.Called.size()) + " in the circle");
			}
		}
		else if (m_Time - r.Hold >= static_cast<double>(d.Seconds)) EndRite(std::string());
		if (r.Active() && m_Time - r.Start > static_cast<double>(d.GatherSeconds + d.Seconds) + 10.0) EndRite("it went on too long");
	}

	void Tribe::EndRite(const std::string& why)
	{
		ActiveRite r = m_Social.Rite;
		m_Social.Rite = ActiveRite();
		const auto def = Config.Social.Rites.find(r.Rite);
		// those who stood in the circle for it, and their orders end
		for (TribeMember& m : m_Members)
			if (m.Job == "rite" && !m.Dead && m.Body) Order(m, "wait 0.1");
		if (def == Config.Social.Rites.end() || !why.empty()) { TellClan("breaks off " + (def != Config.Social.Rites.end() ? def->second.Name : r.Rite) + ": " + why); return; }
		const RiteDef& d = def->second;
		const SocialRules& R = Config.Social;
		const float radius = std::max(2.5f, 0.45f * static_cast<float>(r.Called.size())) + kCircle;
		std::vector<std::string> there;
		for (const std::string& n : r.Called) if (const TribeMember* m = FindMember(n); m && !m->Dead && glm::distance(Where(*m), r.Place) <= radius) there.push_back(n);
		const bool presided = !r.Leader.empty() && r.Leader == m_Social.Shaman && std::find(there.begin(), there.end(), r.Leader) != there.end();
		const float scale = (r.Humble ? 0.5f : 1.0f) * (presided ? 1.0f + R.Shaman.Bonus : 1.0f);
		for (size_t i = 0; i < there.size(); ++i)
		{
			SocialMember& s = SocialOf(there[i]);
			++s.Rites;
			s.Grief = std::clamp(s.Grief + d.Grief * scale, 0.0f, 1.0f);
			MoveMood(there[i], d.Mood * scale);
			// cohesion: each with its neighbours in the circle (up to three either side), and with the one the rite is for
			for (size_t k = 1; k <= 3 && k < there.size(); ++k)
				if (i < (i + k) % there.size()) m_Society.Adjust(Config.Society, there[i], there[(i + k) % there.size()], d.Relation * scale);
			if (!r.Subject.empty() && r.Subject != there[i] && m_Society.Find(r.Subject) && !m_Society.Find(r.Subject)->Dead)
				m_Society.Adjust(Config.Society, there[i], r.Subject, 2.0f * d.Relation * scale);
		}
		for (const auto& [norm, delta] : d.Norms) m_Society.NudgeNorm(Config.Society, norm, delta * scale);
		for (const auto& [sys, w] : d.Beliefs)
			if (m_Social.Beliefs.count(sys)) m_Social.Beliefs[sys] = std::clamp(m_Social.Beliefs[sys] + R.Beliefs.Drift * w * scale, 0.0f, 1.0f);
		if (d.Heal > 0.0f && !r.Subject.empty()) m_Conflict.Members[r.Subject].CareUntil = m_Time + static_cast<double>(d.Heal * 3.0f);
		if (!r.Leader.empty()) ++SocialOf(r.Leader).Led;
		++m_Social.Counts["rite." + r.Rite];
		char t[32];
		std::snprintf(t, sizeof t, "%.1f ", m_Time);
		m_Social.History.push_back(t + d.Name + (r.Subject.empty() ? std::string() : " for " + r.Subject) + ": " + std::to_string(there.size()) + " of " + std::to_string(r.Called.size())
			+ (presided ? ", the shaman " + r.Leader + " presides" : std::string()));
		if (m_Social.History.size() > 12) m_Social.History.erase(m_Social.History.begin());
		TellClan("holds " + d.Name + (r.Subject.empty() ? std::string() : " for " + r.Subject) + ": " + std::to_string(there.size()) + " of " + std::to_string(r.Called.size()) + " attend"
			+ (presided ? " (the shaman " + r.Leader + " presides)" : r.Leader.empty() ? std::string() : " (" + r.Leader + " leads it)"));
	}

	// ------------------------------------------------------------------ traps
	void Tribe::StepTraps()
	{
		if (m_Social.Snares.empty()) return;
		for (Snare& s : m_Social.Snares)
		{
			if (!s.Caught.empty()) continue;
			const auto def = Config.Social.Hunting.Traps.find(s.Trap);
			if (def == Config.Social.Hunting.Traps.end() || m_Time - s.Set < static_cast<double>(def->second.Arm)) continue;
			for (const std::string& name : m_Gameplay->Names())
			{
				AbilitySystemComponent* a = m_Gameplay->Find(name);
				if (!a || !a->HasTag(def->second.Tag) || a->HasTag("State.Dead") || a->HasTag("State.Held")) continue;
				const EntityId id = m_Entities->FindByName(name);
				if (id == NullEntity) continue;
				const glm::vec4 p = m_Entities->WorldMatrix(id)[3];
				if (glm::distance(glm::vec2(p.x, p.z), s.Pos) > def->second.Radius) continue;
				a->Tags().Add("State.Dead");   // (the ecology takes it away and counts it as caught by hunters)
				s.Caught = name;
				++m_Social.Counts["trap." + s.Trap + ".caught"];
				TellClan("finds " + name + " in its " + def->second.Name + " (" + s.Name + ")");
				break;
			}
		}
	}

	// ------------------------------------------------------------------ talk
	// What two members would talk about, in the order of the rules (docs/VIDA_SOCIAL.md): the first that fits and that the data has. `about`: the
	// technology taught or the third one of the gossip.
	std::string Tribe::ChooseTalk(const TribeMember& m, const TribeMember& o, std::string& about) const
	{
		const TalkRules& T = Config.Social.Talk;
		const SocietyRules& S = Config.Society;
		about.clear();
		auto has = [&](const char* k) { const auto it = T.Kinds.find(k); return it != T.Kinds.end() && ClanKnowsTech(it->second.Tech); };
		const Person* pm = m_Society.Find(m.Name);
		const Person* po = m_Society.Find(o.Name);
		auto trait = [](const Person* p, const char* t) { if (!p) return 0.5f; const auto it = p->Traits.find(t); return it == p->Traits.end() ? 0.5f : it->second; };
		if (has("console") && Grief(o.Name) > T.ConsoleAbove) return "console";
		if (has("settle") && m_Society.Bond(m.Name, o.Name) < T.SettleBelow) return "settle";
		if (has("share") && FoodIn(m.Body->Bag()) > 0)
			if (const AbilitySystemComponent* a = m_Gameplay->Find(o.Name); a && a->Attributes().Has(NeedName::Hunger) && a->Attribute(NeedName::Hunger) < T.HungerBelow) return "share";
		const bool kids = pm && po && IsChild(m) && IsChild(o);
		if (has("play") && kids) return "play";
		// a technology it knows and the other can learn (H11)
		auto teachable = [&]() -> std::string
		{
			if (const TechTree* tree = m_Knowledge.Techs())
				for (const auto& [id, t] : tree->Techs)
					if (m_Knowledge.Knows(m.Name, id) && !m_Knowledge.Knows(o.Name, id) && m_Knowledge.CanLearn(o.Name, id)) return id;
			return {};
		};
		if (has("teach") && !IsChild(m)) if (std::string tech = teachable(); !tech.empty()) { about = tech; return "teach"; }
		const float hour = Hour();
		const bool evening = hour >= Config.Needs.NightFrom - Config.Agenda.DuskHours || hour < Config.Needs.NightTo;
		const bool teller = pm && (m_Society.StageOf(S, m.Name) == Society::Stage::Elder || pm->Age >= T.StoryAge || trait(pm, "sociability") >= T.StorySociability);
		if (has("story") && teller && evening) { about = teachable(); return "story"; }
		const auto talked = m_Social.Find(m.Name);
		const auto last = talked ? talked->Talked.find(o.Name) : std::map<std::string, double>::const_iterator();
		if (has("greet") && (!talked || last == talked->Talked.end() || m_Time - last->second >= static_cast<double>(T.GreetAfter))) return "greet";
		if (has("groom") && m_Society.Affinity(S, m.Name, o.Name) >= T.GroomAbove) return "groom";
		if (has("gossip") && trait(pm, "sociability") >= T.GossipSociability && trait(po, "sociability") >= T.GossipSociability)
		{
			for (const auto& [third, bond] : m_Society.BondsOf(m.Name))
				if (third != o.Name && std::abs(m_Society.Bond(o.Name, third)) > 0.0f)
					if (const TribeMember* x = FindMember(third); x && !x->Dead) { about = third; return "gossip"; }
		}
		return has("chat") ? "chat" : std::string();
	}

	// ------------------------------------------------------------------ the brain
	void Tribe::SocialOptions(TribeMember& m, std::vector<UtilityOption>& options)
	{
		const SocialRules& R = Config.Social;
		if (!R.On || m.Dead || !m.Body) return;
		SocialMember& me = SocialOf(m.Name);
		// the mood scales what it would do (a cheerful member works more and rests less)
		for (UtilityOption& o : options)
		{
			const float f = SocialFactor(R.Mood.Effects, o.Kind, me.Mood);
			if (f == 1.0f) continue;
			o.Score *= f;
			Factor(o, "mood", f);
		}
		const glm::vec2 pos = Where(m);
		// the rite the clan is holding: to its place in the circle
		if (m_Social.Rite.Active() && m.Job != "rite"
			&& std::find(m_Social.Rite.Called.begin(), m_Social.Rite.Called.end(), m.Name) != m_Social.Rite.Called.end())
		{
			const RiteDef& d = R.Rites.at(m_Social.Rite.Rite);
			UtilityOption o;
			o.Kind = "rite";
			o.Target = m_Social.Rite.Rite;
			o.Where = CircleSpot(m.Name);
			o.Score = 1.45f;
			o.Commit = d.Seconds;
			Factor(o, "rite", 1.45f);
			o.Note = "the clan holds " + d.Name + (m_Social.Rite.Subject.empty() ? std::string() : " for " + m_Social.Rite.Subject);
			options.push_back(o);
		}
		// a talk with someone near who is free
		const TalkRules& T = R.Talk;
		if (!T.Kinds.empty() && m_Time - me.LastTalk >= static_cast<double>(T.Cooldown))
		{
			const TribeMember* best = nullptr;
			float bestScore = -1e9f, bestDistance = 0.0f;
			for (const TribeMember& o : m_Members)
			{
				if (&o == &m || o.Dead || !o.Body || o.Player || !Idle(o) || o.Abstract != m.Abstract) continue;
				const float d = glm::distance(pos, Where(o));
				if (d > T.Radius) continue;
				const float s = 1.0f + m_Society.Affinity(Config.Society, m.Name, o.Name) + (Grief(o.Name) > T.ConsoleAbove ? 1.0f : 0.0f) - 0.5f * d / T.Radius;
				if (s > bestScore) { bestScore = s; best = &o; bestDistance = d; }
			}
			if (best)
			{
				std::string about;
				const std::string kind = ChooseTalk(m, *best, about);
				if (!kind.empty())
				{
					const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
					const float company = asc && asc->Attributes().Has(NeedName::Company) ? asc->Attribute(NeedName::Company) : 100.0f;
					const float urgency = std::clamp((T.Want - company) / std::max(1.0f, T.Want), 0.0f, 1.0f);
					const Person* p = m_Society.Find(m.Name);
					const auto soc = p ? p->Traits.find("sociability") : std::map<std::string, float>::const_iterator();
					const float sociable = 0.6f + 0.8f * (p && soc != p->Traits.end() ? soc->second : 0.5f);
					const float pressing = kind == "console" || kind == "settle" || kind == "share" ? 1.5f : 1.0f;
					const float agenda = AgendaFactor(m.Role, "socialize", Hour(), CurrentSeason());
					const float hour = Hour();
					const bool evening = (hour >= Config.Needs.NightFrom - Config.Agenda.DuskHours || hour < Config.Needs.NightTo) && glm::distance(pos, Config.Camp) < 15.0f;
					const float fire = evening ? T.Evening : 1.0f;   // (the evening at the camp is the time for talk)
					UtilityOption o;
					o.Kind = "talk";
					o.Target = best->Name;
					o.Where = 0.5f * (pos + Where(*best));
					o.Score = T.Score * (0.25f + 0.75f * urgency) * sociable * pressing * agenda * fire * (1.0f - 0.3f * bestDistance / T.Radius);
					o.Commit = T.Kinds.at(kind).Seconds;
					Factor(o, "company", 0.25f + 0.75f * urgency);
					Factor(o, "sociable", sociable);
					if (pressing != 1.0f) Factor(o, kind.c_str(), pressing);
					if (agenda != 1.0f) Factor(o, "agenda", agenda);
					if (fire != 1.0f) Factor(o, "evening", fire);
					o.Note = kind + " with " + best->Name + (about.empty() ? std::string() : " (" + about + ")");
					options.push_back(o);
				}
			}
		}
		// gear: the tool a food node it knows asks for (a fish trap for a fishing spot) is in the store and nobody carries one: it takes it
		if (!IsChild(m) && m_Items && glm::distance(pos, Config.Camp) < 40.0f)
			for (const auto& [kind, def] : m_Defs)
			{
				if (def.RequiresTag.empty() || !IsFood(def.Item) || m_Clan.OfKind(kind).empty() || m.Body->Bag().CountTagged(*m_Items, def.RequiresTag) > 0) continue;
				if (m_Store.CountTagged(*m_Items, def.RequiresTag) == 0) continue;
				if (std::any_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return !x.Dead && x.Body && (x.Job == "gear" || x.Body->Bag().CountTagged(*m_Items, def.RequiresTag) > 0); })) continue;
				std::string tool;
				for (const ItemSlot& slot : m_Store.Stacks())
					if (const ItemDef* d = m_Items->Find(slot.Id); d && slot.Count > 0 && std::find(d->Tags.begin(), d->Tags.end(), def.RequiresTag) != d->Tags.end()) { tool = slot.Id; break; }
				if (tool.empty()) continue;
				UtilityOption o;
				o.Kind = "gear";
				o.Target = tool;
				o.Where = CampSpotOf(m);
				o.Score = 0.45f;
				Factor(o, "gear", 0.45f);
				o.Note = "takes the " + tool + " for " + kind;
				options.push_back(o);
				break;
			}
		// traps: when food is short, set one near game of its kind (H14's hunts by other means), or bring home a catch
		if (!IsChild(m) && m_Items)
		{
			for (const Snare& s : m_Social.Snares)
			{
				if (s.Caught.empty()) continue;
				if (std::any_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return &x != &m && x.Job == "snare" && Word(x.Detail, 1) == s.Name; })) continue;
				UtilityOption o;
				o.Kind = "snare";
				o.Target = "collect " + s.Name;
				o.Where = s.Pos;
				const float reach = std::clamp(1.0f - glm::distance(pos, s.Pos) / 120.0f, 0.1f, 1.0f);
				o.Score = 1.3f * reach;   // (food waiting in a trap: before the fox finds it)
				Factor(o, "catch", 1.3f);
				o.Note = s.Name + " has caught " + s.Caught;
				options.push_back(o);
				break;
			}
			int alive = 0, food = FoodIn(m_Store);
			for (const TribeMember& x : m_Members) if (!x.Dead && x.Body) { ++alive; food += FoodIn(x.Body->Bag()); }
			const float want = static_cast<float>(std::max(1, alive)) * R.Hunting.Short;
			const float short_ = std::clamp(1.0f - static_cast<float>(food) / want, 0.0f, 1.0f);
			for (const auto& [id, t] : R.Hunting.Traps)
			{
				if (short_ <= 0.0f || !ClanKnowsTech(t.Tech)) continue;
				const int out = static_cast<int>(std::count_if(m_Social.Snares.begin(), m_Social.Snares.end(), [&](const Snare& s) { return s.Trap == id && s.Caught.empty(); }));
				if (out >= t.Max) continue;
				if (std::any_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return x.Job == "snare" && Word(x.Detail, 1) == id; })) continue;
				bool paid = true;
				for (const auto& [item, n] : t.Cost) paid = paid && m_Store.Count(item) + m.Body->Bag().Count(item) >= n;
				if (!paid) continue;
				// where the game is: the nearest animal of its tag to the camp, within 60 m
				glm::vec2 at{ 0.0f };
				float near = 60.0f;
				for (const std::string& name : m_Gameplay->Names())
				{
					const AbilitySystemComponent* a = m_Gameplay->Find(name);
					if (!a || !a->HasTag(t.Tag) || a->HasTag("State.Dead")) continue;
					const EntityId e = m_Entities->FindByName(name);
					if (e == NullEntity) continue;
					const glm::vec4 p = m_Entities->WorldMatrix(e)[3];
					const glm::vec2 q(p.x, p.z);
					if (const float d = glm::distance(q, Config.Camp); d < near && HuntAllowed(q)) { near = d; at = q; }
				}
				if (near >= 60.0f) continue;
				UtilityOption o;
				o.Kind = "snare";
				o.Target = "set " + id;
				o.Where = at;
				o.Score = t.Priority * (0.5f + short_);
				Factor(o, "short of food", 0.5f + short_);
				o.Note = "sets a " + t.Name + " where game was seen";
				options.push_back(o);
			}
		}
	}

	bool Tribe::ExecuteSocial(TribeMember& m, const UtilityOption& o)
	{
		const SocialRules& R = Config.Social;
		if (o.Kind == "rite")
		{
			if (!m_Social.Rite.Active()) return false;
			const RiteDef& d = R.Rites.at(m_Social.Rite.Rite);
			const float left = static_cast<float>(m_Social.Rite.Hold < 0.0 ? static_cast<double>(d.GatherSeconds + d.Seconds) - (m_Time - m_Social.Rite.Start)
				: static_cast<double>(d.Seconds) - (m_Time - m_Social.Rite.Hold)) + 5.0f;
			if (!Order(m, GoTo(o.Where, WaitFor(std::max(2.0f, left))))) return false;
			m.Job = "rite";
			m.Detail = m_Social.Rite.Rite + (m_Social.Rite.Leader == m.Name ? " leads" : "");
			return true;
		}
		if (o.Kind == "gear")
		{
			if (!Order(m, GoTo(o.Where, WaitFor(1.0f)))) return false;
			m.Job = "gear";
			m.Detail = o.Target;
			return true;
		}
		if (o.Kind == "snare")
		{
			const std::string what = Word(o.Target, 0), id = Word(o.Target, 1);
			float seconds = 3.0f;
			if (what == "set") { const auto t = R.Hunting.Traps.find(id); if (t == R.Hunting.Traps.end()) return false; seconds = t->second.Seconds; }
			if (!Order(m, GoTo(o.Where, WaitFor(seconds)))) return false;
			m.Job = "snare";
			m.Detail = o.Target;
			Say(m, what == "set" ? "goes to set a " + R.Hunting.Traps.at(id).Name : "goes to see to " + id);
			return true;
		}
		// talk: both go to a place between them and talk there
		TribeMember* other = MemberNamed(o.Target);
		if (!other || other->Dead || !other->Body) return false;
		std::string about;
		const std::string kind = ChooseTalk(m, *other, about);
		if (kind.empty()) return false;
		const TalkKind& k = R.Talk.Kinds.at(kind);
		const glm::vec2 a = Where(m), b = Where(*other);
		const glm::vec2 dir = glm::length(b - a) > 0.01f ? glm::normalize(b - a) : glm::vec2(1.0f, 0.0f);
		if (!Order(m, GoTo(o.Where - dir * 0.6f, WaitFor(k.Seconds)))) return false;
		m.Job = "talk";
		m.Detail = kind + " " + other->Name + (about.empty() ? std::string() : " " + about);
		if (Idle(*other)) Send(*other, GoTo(o.Where + dir * 0.6f, WaitFor(k.Seconds + 1.5f)), "talk", kind + " " + m.Name + " listening");
		return true;
	}

	bool Tribe::FinishSocial(TribeMember& m, const std::string& job)
	{
		if (job == "rite") return true;
		const SocialRules& R = Config.Social;
		if (job == "gear")
		{
			if (m_Items && m_Store.Transfer(*m_Items, m.Body->Bag(), m.Detail, 1) > 0)
			{
				if (AbilitySystemComponent* asc = m_Gameplay->Find(m.Name)) ResourceSystem::SyncTools(*asc, m.Body->Bag(), *m_Items);
				Say(m, "takes the " + m.Detail + " from the store");
			}
			return true;
		}
		if (job == "snare")
		{
			const std::string detail = m.Detail;
			const std::string what = Word(detail, 0), id = Word(detail, 1);
			if (what == "set")
			{
				const auto t = R.Hunting.Traps.find(id);
				if (t == R.Hunting.Traps.end() || !m_Items) return true;
				for (const auto& [item, n] : t->second.Cost)
				{
					const int fromBag = std::min(n, m.Body->Bag().Count(item));
					if (m.Body->Bag().Count(item) + m_Store.Count(item) < n) { Say(m, "has nothing to make the " + t->second.Name + " with"); return true; }
					m.Body->Bag().Remove(item, fromBag);
					m_Store.RemoveOldest(item, n - fromBag);
				}
				Snare s;
				s.Name = t->second.Name + " " + std::to_string(++m_Social.Serial);
				s.Trap = id;
				s.Owner = m.Name;
				s.Pos = Where(m);
				s.Set = m_Time;
				m_Social.Snares.push_back(s);
				++m_Social.Counts["trap." + id + ".set"];
				Say(m, "sets " + s.Name);
				return true;
			}
			const auto s = std::find_if(m_Social.Snares.begin(), m_Social.Snares.end(), [&](const Snare& x) { return x.Name == id; });
			if (s == m_Social.Snares.end() || s->Caught.empty()) return true;
			if (glm::distance(Where(m), s->Pos) > 3.0f) { Say(m, "could not reach " + id); return true; }
			const auto t = R.Hunting.Traps.find(s->Trap);
			std::string got;
			if (t != R.Hunting.Traps.end() && m_Items)
				for (const auto& [item, n] : t->second.Yields)
					if (m_Items->Find(item)) { const int k = m.Body->Bag().Add(*m_Items, item, n); if (k > 0) got += (got.empty() ? "" : ", ") + std::to_string(k) + " " + item; }
			Say(m, "brings home " + s->Caught + " from " + id + (got.empty() ? std::string() : ": " + got));
			HuntRule rule;   // (a trapped animal counts as the member's kill: its first one calls the rite)
			rule.Units = 0;
			const std::string caught = s->Caught;
			m_Social.Snares.erase(s);
			Hunted(m, rule, caught);
			return true;
		}
		if (job != "talk") return false;   // ("gear" and "snare" above)
		const std::string kind = Word(m.Detail, 0), otherName = Word(m.Detail, 1), about = Word(m.Detail, 2);
		if (about == "listening") return true;   // (the one who started it tells)
		const auto k = R.Talk.Kinds.find(kind);
		TribeMember* other = MemberNamed(otherName);
		if (k == R.Talk.Kinds.end() || !other || other->Dead || !other->Body) return true;
		if (glm::distance(Where(m), Where(*other)) > kMeet) { Say(m, "could not find " + otherName + " to talk"); return true; }
		const TalkKind& t = k->second;
		const SocietyRules& S = Config.Society;
		m_Society.Adjust(S, m.Name, other->Name, t.Relation);
		MoveMood(m.Name, t.Mood);
		MoveMood(other->Name, t.Mood);
		for (const TribeMember* who : { static_cast<const TribeMember*>(&m), static_cast<const TribeMember*>(other) })
			if (AbilitySystemComponent* a = m_Gameplay->Find(who->Name); a && t.Company > 0.0f) SatisfyNeed(*a, NeedName::Company, t.Company);
		if (t.Grief != 0.0f) { SocialMember& g = SocialOf(other->Name); g.Grief = std::clamp(g.Grief + t.Grief, 0.0f, 1.0f); }
		if (t.Teach > 0.0f && !about.empty() && m_Knowledge.Techs() && m_Knowledge.Techs()->Find(about))
			Tell(m_Knowledge.Instruct(other->Name, about, t.Teach, kind == "story" ? "a story of " + m.Name : "talking with " + m.Name));
		if (t.Third != 0.0f && !about.empty() && m_Society.Find(about))
		{
			m_Society.Adjust(S, m.Name, about, t.Third);
			m_Society.Adjust(S, other->Name, about, t.Third);
		}
		if (kind == "share" && m_Items)
			if (const std::string f = FirstFood(m.Body->Bag()); !f.empty() && m.Body->Bag().RemoveOldest(f, 1) == 1) other->Body->Bag().Add(*m_Items, f, 1);
		if (!t.Event.empty()) Relate(m.Name, other->Name, t.Event);
		SocialMember& a = SocialOf(m.Name);
		SocialMember& b = SocialOf(other->Name);
		a.LastTalk = b.LastTalk = m_Time;
		a.Talked[other->Name] = b.Talked[m.Name] = m_Time;
		++a.Talks;
		++b.Talks;
		++m_Social.Counts["talk." + kind];
		static const std::map<std::string, std::string> kSay = { { "chat", "chats with " }, { "story", "tells a story to " }, { "gossip", "gossips with " },
			{ "teach", "shows something to " }, { "console", "consoles " }, { "settle", "makes peace with " }, { "greet", "greets " }, { "share", "shares food with " },
			{ "groom", "grooms " }, { "play", "plays with " } };
		const auto said = kSay.find(kind);
		Say(m, (said != kSay.end() ? said->second : kind + " with ") + other->Name + (kind == "gossip" || about.empty() ? std::string() : " (" + about + ")"));
		return true;
	}

	// ------------------------------------------------------------------ meals
	// What to eat for variety: when what it would eat is of the group of its last meal and the store (at the camp) has food of a group it has not
	// eaten lately, that one (the soonest to go off of them). Otherwise what the economy chose (the soonest to go off, what it carries first).
	void Tribe::ChooseMeal(const TribeMember& m, std::string& carried, std::string& stored) const
	{
		if (!Config.Social.On || !m_Items || !m.Body) return;
		const SocialMember* s = m_Social.Find(m.Name);
		if (!s || s->Diet.empty()) return;
		const DietRules& D = Config.Social.Diet;
		auto group = [&](const std::string& item) { const auto f = D.Foods.find(item); return f != D.Foods.end() && !f->second.Group.empty() ? f->second.Group : item; };
		const std::string next = !carried.empty() ? carried : stored;
		if (next.empty() || group(next) != s->Diet.back()) return;
		if (!carried.empty() && glm::distance(Where(m), Config.Camp) > 8.0f) return;   // (away from the camp it eats what it carries)
		std::string best;
		float left = 1e30f;
		for (const ItemSlot& slot : m_Store.Stacks())
		{
			if (slot.Count <= 0 || !IsFood(slot.Id) || std::find(s->Diet.begin(), s->Diet.end(), group(slot.Id)) != s->Diet.end()) continue;
			const ItemDef* d = m_Items->Find(slot.Id);
			const float l = d && d->ShelfLife > 0.0f ? d->ShelfLife - slot.Age : 1e29f;
			if (l < left || (l == left && slot.Id < best)) { left = l; best = slot.Id; }
		}
		if (best.empty()) return;
		carried.clear();
		stored = best;
	}

	void Tribe::Ate(TribeMember& m, const std::string& item)
	{
		if (!Config.Social.On) return;
		const DietRules& D = Config.Social.Diet;
		const auto f = D.Foods.find(item);
		const float nutrition = f != D.Foods.end() ? f->second.Nutrition : static_cast<float>(Config.Needs.FoodHunger);
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		if (asc && nutrition != static_cast<float>(Config.Needs.FoodHunger)) SatisfyNeed(*asc, NeedName::Hunger, nutrition - static_cast<float>(Config.Needs.FoodHunger));
		SocialMember& s = SocialOf(m.Name);
		s.Diet.push_back(f != D.Foods.end() && !f->second.Group.empty() ? f->second.Group : item);
		while (static_cast<int>(s.Diet.size()) > D.Window) s.Diet.erase(s.Diet.begin());
		const std::set<std::string> groups(s.Diet.begin(), s.Diet.end());
		if (static_cast<int>(groups.size()) >= D.Varied)
		{
			MoveMood(m.Name, D.Bonus);
			if (asc && D.Health > 0.0f) SatisfyNeed(*asc, NeedName::Health, D.Health);
			++m_Social.Counts["meal.varied"];
		}
		else if (groups.size() == 1 && static_cast<int>(s.Diet.size()) >= D.Window)
		{
			MoveMood(m.Name, D.Monotony);
			++m_Social.Counts["meal.monotonous"];
		}
	}

	// ------------------------------------------------------------------ for people
	std::string Tribe::SocialReport(const std::string& member) const
	{
		char b[256];
		std::string s;
		if (!member.empty())
		{
			const SocialMember* m = m_Social.Find(member);
			if (!m) return member + ": nothing yet (no talk, no rite, no meal remembered)";
			std::snprintf(b, sizeof b, "%s: mood %.2f, grief %.2f, %d talks, %d rites (%d presided), %d kills%s\n", member.c_str(), static_cast<double>(m->Mood),
				static_cast<double>(m->Grief), m->Talks, m->Rites, m->Led, m->Kills, member == m_Social.Shaman ? ", the shaman" : "");
			s += b;
			std::string diet;
			for (const std::string& g : m->Diet) diet += (diet.empty() ? "" : ", ") + g;
			s += "  last meals: " + (diet.empty() ? std::string("none") : diet) + "\n";
			for (const auto& [o, when] : m->Talked)
			{
				std::snprintf(b, sizeof b, "  talked with %s %.0f s ago (bond %+.2f)\n", o.c_str(), m_Time - when, static_cast<double>(m_Society.Bond(member, o)));
				s += b;
			}
			return s;
		}
		float mood = 0.0f, grief = 0.0f;
		int n = 0;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			mood += Mood(m.Name);
			grief += Grief(m.Name);
			++n;
		}
		std::snprintf(b, sizeof b, "social life: %d alive, mood %.2f, grief %.2f; shaman %s\n", n, n ? static_cast<double>(mood / static_cast<float>(n)) : 0.0,
			n ? static_cast<double>(grief / static_cast<float>(n)) : 0.0, m_Social.Shaman.empty() ? "none" : m_Social.Shaman.c_str());
		s += b;
		std::string talks;
		for (const auto& [key, c] : m_Social.Counts) if (key.rfind("talk.", 0) == 0) talks += (talks.empty() ? "" : ", ") + key.substr(5) + " " + std::to_string(c);
		s += "talks: " + (talks.empty() ? std::string("none yet") : talks) + "\n";
		const int varied = m_Social.Counts.count("meal.varied") ? m_Social.Counts.at("meal.varied") : 0;
		const int dull = m_Social.Counts.count("meal.monotonous") ? m_Social.Counts.at("meal.monotonous") : 0;
		s += "meals: " + std::to_string(varied) + " varied, " + std::to_string(dull) + " monotonous\n";
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			std::snprintf(b, sizeof b, "  %s: mood %.2f%s%s\n", m.Name.c_str(), static_cast<double>(Mood(m.Name)), Grief(m.Name) > 0.05f ? (", grieving " + Num(Grief(m.Name))).c_str() : "",
				m.Job == "talk" ? (" (" + Word(m.Detail, 0) + " with " + Word(m.Detail, 1) + ")").c_str() : "");
			s += b;
		}
		return s;
	}

	std::string Tribe::RiteReport() const
	{
		std::string s;
		const ActiveRite& r = m_Social.Rite;
		if (r.Active())
		{
			const auto d = Config.Social.Rites.find(r.Rite);
			char b[160];
			std::snprintf(b, sizeof b, "now: %s%s at (%.0f, %.0f), %s for %.0f s, %zu called, led by %s\n", d != Config.Social.Rites.end() ? d->second.Name.c_str() : r.Rite.c_str(),
				r.Subject.empty() ? "" : (" for " + r.Subject).c_str(), static_cast<double>(r.Place.x), static_cast<double>(r.Place.y), r.Hold < 0.0 ? "gathering" : "under way",
				r.Hold < 0.0 ? m_Time - r.Start : m_Time - r.Hold, r.Called.size(), r.Leader.empty() ? "nobody" : r.Leader.c_str());
			s += b;
		}
		else s += "no rite under way\n";
		for (const PendingRite& p : m_Social.Agenda)
		{
			char b[128];
			std::snprintf(b, sizeof b, "  next: %s%s in %.0f s%s\n", p.Rite.c_str(), p.Subject.empty() ? "" : (" for " + p.Subject).c_str(), std::max(0.0, p.Due - m_Time),
				p.Tries ? " (could not pay for it yet)" : "");
			s += b;
		}
		for (const std::string& h : m_Social.History) s += "  held " + h + "\n";
		s += "rites of the rules:";
		for (const auto& [id, d] : Config.Social.Rites)
			s += " " + id + (d.Event.empty() ? " (" + d.Season + ")" : " (" + d.Event + ")") + (ClanKnowsTech(d.Tech) ? "" : " [needs " + d.Tech + "]");
		return s + "\n";
	}

	std::string Tribe::BeliefReport() const
	{
		std::string s = "beliefs of " + m_ClanName + ":";
		for (const auto& [id, v] : m_Social.Beliefs)
		{
			const auto def = Config.Social.Beliefs.Systems.find(id);
			s += " " + (def != Config.Social.Beliefs.Systems.end() ? def->second.Name : id) + " " + Num(v) + ";";
		}
		s += " dominant " + (m_Social.Dominant().empty() ? std::string("none") : m_Social.Dominant()) + "\n";
		char b[128];
		if (m_Social.HasSacred)
		{
			std::snprintf(b, sizeof b, "sacred place at (%.0f, %.0f)\n", static_cast<double>(m_Social.Sacred.x), static_cast<double>(m_Social.Sacred.y));
			s += b;
		}
		for (const auto& [id, t] : Config.Social.Beliefs.Taboos) s += "taboo " + t.Name + ": " + (m_Social.Holds(Config.Social, id) ? "kept" : "not kept") + "\n";
		std::string omens;
		for (const auto& [key, c] : m_Social.Counts) if (key.rfind("omen.", 0) == 0) omens += (omens.empty() ? "" : ", ") + key.substr(5) + " " + std::to_string(c);
		s += "omens read: " + (omens.empty() ? std::string("none") : omens) + "\n";
		s += "shaman: " + (m_Social.Shaman.empty() ? std::string("none") : m_Social.Shaman) + "\n";
		return s;
	}

	std::string Tribe::HuntReport() const
	{
		std::string s;
		for (const auto& [tag, n] : m_Conflict.Hunted) s += "hunted " + std::to_string(n) + " " + tag + "\n";
		std::map<std::string, int> near;
		for (const std::string& name : m_Gameplay ? m_Gameplay->Names() : std::vector<std::string>())
		{
			const AbilitySystemComponent* a = m_Gameplay->Find(name);
			if (!a || !a->HasTag("Animal") || a->HasTag("State.Dead")) continue;
			const EntityId e = m_Entities->FindByName(name);
			if (e == NullEntity) continue;
			const glm::vec4 p = m_Entities->WorldMatrix(e)[3];
			if (glm::distance(glm::vec2(p.x, p.z), Config.Camp) > 80.0f) continue;
			for (const std::string& tag : a->Tags().Names()) if (tag.rfind("Animal.", 0) == 0) { ++near[tag]; break; }
		}
		std::string game;
		for (const auto& [tag, n] : near) game += (game.empty() ? "" : ", ") + std::to_string(n) + " " + tag;
		s += "game within 80 m: " + (game.empty() ? std::string("none") : game) + "\n";
		for (const Snare& n : m_Social.Snares)
		{
			char b[160];
			std::snprintf(b, sizeof b, "%s (by %s) at (%.0f, %.0f): %s\n", n.Name.c_str(), n.Owner.c_str(), static_cast<double>(n.Pos.x), static_cast<double>(n.Pos.y),
				!n.Caught.empty() ? ("caught " + n.Caught).c_str() : m_Time - n.Set < 1e9 ? "waiting" : "");
			s += b;
		}
		for (const auto& [key, c] : m_Social.Counts) if (key.rfind("trap.", 0) == 0 || key.rfind("taboo.", 0) == 0) s += key + " " + std::to_string(c) + "\n";
		s += std::string("hunting near the sacred place: ") + (m_Social.HasSacred && !HuntAllowed(m_Social.Sacred) ? "taboo" : "allowed") + "\n";
		return s;
	}
}
