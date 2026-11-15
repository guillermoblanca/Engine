#include "GB/Game/WorldDirector.h"
#include "GB/Game/Clans.h"
#include "GB/Game/Tribe.h"
#include "GB/World/Regions.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// T236 (the vote of a clan, MODO_DIOS_DECISIONES.md §3.0) and T237 (the sign, §3.a): the god marks a spot, someone of the clan sees it, the clan
// talks it over at the fire and, if it agrees, opens the site there (Tribe::StartSite, as an accepted proposal of the player): its people bring
// the material and do the work as for any other site. Only the public face of the Tribe is used; nothing here runs while there is no sign.
namespace GB
{
	namespace
	{
		std::string Num(double v, int decimals = 1)
		{
			char b[48];
			std::snprintf(b, sizeof b, "%.*f", decimals, v);
			return b;
		}
		const Clan* ClanOf(const ClanWorld* clans, int id)
		{
			if (!clans) return nullptr;
			const Clan* c = clans->Find(id);
			return c && c->Folk ? c : nullptr;
		}
		std::string NameOf(const Clan& c) { return c.Name.empty() && c.Folk ? c.Folk->ClanName() : c.Name; }
		bool Adult(const Tribe& t, const TribeMember& m) { return !m.Dead && !m.Player && !t.People().IsChild(t.Config.Society, m.Name); }
		// the share of the building's material the clan does not have in its store (0: all of it there)
		float MissingShare(const Tribe& t, const BuildingDef& def, std::string* what)
		{
			int need = 0, lack = 0;
			for (const auto& [item, n] : def.TotalNeeds())
			{
				const int short_ = std::max(0, n - t.Stored(item));
				need += n;
				lack += short_;
				if (short_ > 0 && what) *what += (what->empty() ? "" : ", ") + std::to_string(short_) + " " + item;
			}
			return need > 0 ? static_cast<float>(lack) / static_cast<float>(need) : 0.0f;
		}
	}

	// ------------------------------------------------------------------ T236: the vote
	DirectorBallot WorldDirector::BallotFor(int clan, const std::string& messenger, const std::map<std::string, float>& traits, float cost) const
	{
		DirectorBallot b;
		b.Messenger = messenger;
		b.Cost = cost;
		b.Traits = traits;
		const Clan* c = ClanOf(m_To.Clans, clan);
		if (!c) return b;
		const Tribe& t = *c->Folk;
		b.MessengerIsShaman = !messenger.empty() && t.Social().Shaman == messenger;
		for (const auto& [norm, w] : Rules.Vote.Culture) b.Norms[norm] = t.People().Norm(t.Config.Society, norm);
		for (const TribeMember& m : t.Members())
		{
			if (!Adult(t, m) || m.Name == messenger) continue;   // the messenger does not vote on its own idea
			DirectorVoter v;
			v.Name = m.Name;
			v.Devotion = DevotionOf(clan, m.Name);
			v.Bond = messenger.empty() ? 0.0f : t.People().Affinity(t.Config.Society, m.Name, messenger);
			if (const Person* p = t.People().Find(m.Name)) v.Traits = p->Traits;
			b.Voters.push_back(std::move(v));
		}
		return b;
	}

	// ------------------------------------------------------------------ T237: the sign
	const DirectorSign* WorldDirector::OpenSign(int clan) const
	{
		for (const DirectorSign& s : m_Signs) if (s.Clan == clan && s.Open()) return &s;
		return nullptr;
	}

	bool WorldDirector::CampOffset(int clan, float dx, float dz, float& x, float& z) const
	{
		const Clan* c = ClanOf(m_To.Clans, clan);
		if (!c) return false;
		x = c->Folk->Config.Economy.Camp.x + dx;
		z = c->Folk->Config.Economy.Camp.y + dz;
		return true;
	}

	// The messenger (§3.0): the most devout awake adult within MessengerRadius of the sign (ties by name); with nobody there (and !nearOnly),
	// the shaman, or else the most devout adult of the clan.
	std::string WorldDirector::PickMessenger(int clan, float x, float z, bool nearOnly) const
	{
		const Clan* c = ClanOf(m_To.Clans, clan);
		if (!c) return {};
		const Tribe& t = *c->Folk;
		const glm::vec2 at(x, z);
		std::string best, any;
		float top = -1.0f, topAny = -1.0f;
		std::vector<const TribeMember*> members;
		for (const TribeMember& m : t.Members()) if (Adult(t, m)) members.push_back(&m);
		std::sort(members.begin(), members.end(), [](const TribeMember* a, const TribeMember* b) { return a->Name < b->Name; });
		for (const TribeMember* m : members)
		{
			const float d = DevotionOf(clan, m->Name);
			if (d > topAny) { topAny = d; any = m->Name; }
			if (!m->Body || m->Job == "sleep" || glm::distance(m->Body->Position(), at) > Rules.MessengerRadius) continue;
			if (d > top) { top = d; best = m->Name; }
		}
		if (!best.empty() || nearOnly) return best;
		const std::string& shaman = t.Social().Shaman;
		if (!shaman.empty())
			if (const TribeMember* m = t.FindMember(shaman); m && Adult(t, *m)) return shaman;
		return any;
	}

	DirectorSignPreview WorldDirector::PreviewSign(int clan, const std::string& building, float x, float z) const
	{
		DirectorSignPreview p;
		p.X = x;
		p.Z = z;
		const Clan* c = ClanOf(m_To.Clans, clan);
		if (!c) { p.Why = "no clan " + std::to_string(clan); return p; }
		const Tribe& t = *c->Folk;
		const std::string name = NameOf(*c);
		const auto* defs = t.Buildings();
		if (!defs || !defs->count(building)) { p.Why = "no building '" + building + "'"; return p; }
		const BuildingDef& d = defs->at(building);
		if (!t.CanBuild(building)) { p.Why = name + " does not know how to raise a " + d.Name + " (a vision first)"; return p; }
		if (const DirectorSign* open = OpenSign(clan)) { p.Why = "a sign already waits on " + name + " (#" + std::to_string(open->Id) + ")"; return p; }
		// the land: the camp's own ground, or a region the clan holds
		const glm::vec2 at(x, z);
		bool inside = glm::distance(at, t.Config.Economy.Camp) <= Rules.Sign.Reach;
		if (!inside && m_To.Clans->Regions())
			if (const int region = m_To.Clans->Regions()->RegionAt(at); region >= 0) inside = m_To.Clans->OwnerOf(region) == clan;
		if (!inside) { p.Why = "outside the land of " + name + " (" + Num(glm::distance(at, t.Config.Economy.Camp), 0) + " m from its camp)"; return p; }
		if (const std::string fits = SiteFits(t.SitePlacementFor(d), at); !fits.empty()) { p.Why = "a " + d.Name + " does not fit there: " + fits; return p; }
		for (const BuildingNeed& n : t.PublicBuildingNeeds()) if (n.Building == building) p.SiteOnly = true;   // the clan wanted it already
		const DirectorCheck paid = CanSpend(p.SiteOnly ? "sign_site" : "sign", clan, true);
		p.Cost = paid.Cost;
		if (!paid.Ok) { p.Why = paid.Why; return p; }
		std::string lacks;
		const float missing = MissingShare(t, d, &lacks);
		if (!lacks.empty()) p.Warning = "the store lacks " + lacks + ": the site will wait for it";
		p.Messenger = PickMessenger(clan, x, z, false);
		const float cost = Rules.Sign.BuildCost + Rules.Sign.Missing * missing + (t.MaslowTier() == "survival" ? Rules.Vote.Survival : 0.0f);
		const DirectorVoteResult vote = ForeseeVote(clan, p.Messenger, Rules.Sign.Traits, cost);
		p.Yes = vote.Yes;
		p.Voters = vote.Voters;
		p.Outcome = vote.Outcome;
		p.Ok = true;
		return p;
	}

	DirectorReply WorldDirector::PlaceSign(int clan, const std::string& building, float x, float z, float yaw)
	{
		const DirectorSignPreview p = PreviewSign(clan, building, x, z);
		if (!p.Ok) return { false, "cannot: " + p.Why };
		const Clan* c = ClanOf(m_To.Clans, clan);
		const std::string name = NameOf(*c);
		const BuildingDef& d = c->Folk->Buildings()->at(building);
		// the god insists: the same building on the same clan refused within InsistDays
		for (const DirectorSign& s : m_Signs)
			if (s.Clan == clan && s.Building == building && s.Status == "refused" && Now() - s.VotedAt < static_cast<double>(Rules.InsistDays * Rules.DaySeconds))
			{
				AddPressure(clan, Rules.PressureInsist, "the god insists on a " + d.Name + " after the clan said no");
				break;
			}
		const std::string where = Num(x, 0) + " " + Num(z, 0);
		const DirectorCheck paid = Spend(p.SiteOnly ? "sign_site" : "sign", clan, true, "sign on " + name + ": a " + d.Name + " at " + where);
		if (!paid.Ok) return { false, "cannot: " + paid.Why };
		DirectorSign s;
		s.Id = m_NextSign++;
		s.Clan = clan;
		s.Building = building;
		s.X = x;
		s.Z = z;
		s.Yaw = yaw;
		s.Posted = Now();
		s.Paid = paid.Cost;
		s.Messenger = p.Messenger;   // the probable one until someone sees it
		m_Signs.push_back(s);
		return { true, "sign " + std::to_string(s.Id) + " for " + name + ": " + building + " at " + where + "; messenger " + (p.Messenger.empty() ? std::string("nobody yet") : p.Messenger)
			+ " (forecast " + std::to_string(p.Yes) + "/" + std::to_string(p.Voters) + ", " + p.Outcome + "), cost " + Num(paid.Cost) + (p.SiteOnly ? " (the clan wanted it: only the place)" : "")
			+ (p.Warning.empty() ? std::string() : "; " + p.Warning) };
	}

	void WorldDirector::Refund(int clan, float amount, const std::string& text)
	{
		if (amount <= 0.0f) return;
		m_Influence = std::min(Rules.Max, m_Influence + amount);
		const Clan* c = ClanOf(m_To.Clans, clan);
		Write("refund", c ? NameOf(*c) : std::string("world"), -amount, text, clan);
	}

	void WorldDirector::UpdateSigns()
	{
		if (m_Signs.empty()) return;
		const double now = Now();
		const double day = static_cast<double>(Rules.DaySeconds);
		for (DirectorSign& s : m_Signs)
		{
			const Clan* c = ClanOf(m_To.Clans, s.Clan);
			if (s.Status != "waiting" && s.Status != "seen" && s.Status != "accepted") continue;
			const BuildingDef* d = c && c->Folk->Buildings() && c->Folk->Buildings()->count(s.Building) ? &c->Folk->Buildings()->at(s.Building) : nullptr;
			if (!c || !d) { s.Status = "dropped"; s.Why = "the clan is gone"; continue; }
			Tribe& t = *c->Folk;
			const std::string name = NameOf(*c);
			if (s.Status == "waiting")
			{
				const double waited = now - s.Posted;
				std::string who = PickMessenger(s.Clan, s.X, s.Z, true);
				if (who.empty() && waited >= 0.5 * Rules.Sign.SignDays * day) who = PickMessenger(s.Clan, s.X, s.Z, false);   // the shaman dreams of it
				if (!who.empty())
				{
					s.Status = "seen";
					s.Messenger = who;
					s.SeenAt = now;
					if (const TribeMember* m = t.FindMember(who)) t.Note(*m, "sees a sign at " + Num(s.X, 0) + ", " + Num(s.Z, 0) + ": a " + s.Building + " should stand there");   // (the id: es.json names it)
					Write("sign seen", name, 0.0f, who + " sees the sign #" + std::to_string(s.Id) + " (" + d->Name + ")", s.Clan);
				}
				else if (waited >= Rules.Sign.SignDays * day)
				{
					s.Status = "unseen";
					s.Why = "nobody saw it";
					Refund(s.Clan, std::min(s.Paid, Rules.Sign.RefundUnseen), "nobody saw the sign #" + std::to_string(s.Id));
				}
				continue;
			}
			if (s.Status == "seen")
			{
				if (now - s.SeenAt < static_cast<double>(Rules.Sign.VoteDays) * day) continue;
				const float cost = Rules.Sign.BuildCost + Rules.Sign.Missing * MissingShare(t, *d, nullptr) + (t.MaslowTier() == "survival" ? Rules.Vote.Survival : 0.0f);
				const DirectorVoteResult vote = ForeseeVote(s.Clan, s.Messenger, Rules.Sign.Traits, cost);
				s.Yes = vote.Yes;
				s.Voters = vote.Voters;
				s.VotedAt = now;
				const TribeMember* m = t.FindMember(s.Messenger);
				if (vote.Outcome == "doubt" && s.Tries < Rules.Vote.Retries)
				{
					++s.Tries;
					s.SeenAt = now;   // the clan talks it over again at the next fire
					if (m) t.Note(*m, "speaks of the sign at the fire; the clan cannot decide (" + vote.Text() + ")");
					continue;
				}
				if (!vote.Accepted())
				{
					s.Status = "refused";
					s.Why = "the clan says no (" + vote.Text() + ")";
					if (m) t.Note(*m, "speaks of the sign at the fire; the clan will not raise a " + s.Building + " there (" + vote.Text() + ")");
					Refund(s.Clan, 0.5f * s.Paid, "the clan resists the sign #" + std::to_string(s.Id) + " (" + vote.Text() + ")");
					continue;
				}
				std::string site;
				if (const std::string error = t.StartSite(s.Building, { s.X, s.Z }, &site); !error.empty() || site.empty())
				{
					s.Status = "dropped";
					s.Why = "the clan agrees but cannot: " + error;
					Refund(s.Clan, 0.5f * s.Paid, "the sign #" + std::to_string(s.Id) + " could not be raised: " + error);
					continue;
				}
				s.Status = "accepted";
				s.Site = site;
				s.Why = "the clan agrees (" + vote.Text() + ")";
				if (m) t.Note(*m, "speaks of the sign at the fire; the clan agrees to raise a " + s.Building + " there (" + vote.Text() + ")");
				Write("sign accepted", name, 0.0f, "the clan agrees to the sign #" + std::to_string(s.Id) + ": " + site + " (" + vote.Text() + ")", s.Clan);
				continue;
			}
			// accepted: the site is the clan's now; the god gains when the building stands
			bool found = false;
			for (const Tribe::SettlementSite& site : t.SettlementSites())
				if (site.Name == s.Site)
				{
					found = true;
					if (site.State.Done)
					{
						s.Status = "built";
						Fulfilled(s.Clan, Rules.Sign.FulfilledGain, "the sign #" + std::to_string(s.Id) + " stands: " + s.Site);
					}
					break;
				}
			if (!found) { s.Status = "dropped"; s.Why = "the site is gone"; }
		}
		// keep the open ones and the last closed ones
		int closed = 0;
		for (const DirectorSign& s : m_Signs) if (s.Status != "waiting" && s.Status != "seen" && s.Status != "accepted") ++closed;
		for (auto it = m_Signs.begin(); it != m_Signs.end() && closed > Rules.Sign.Keep;)
			if (it->Status != "waiting" && it->Status != "seen" && it->Status != "accepted") { it = m_Signs.erase(it); --closed; }
			else ++it;
	}

	std::string WorldDirector::SignText(const DirectorSign& s) const
	{
		const Clan* c = ClanOf(m_To.Clans, s.Clan);
		return "sign " + std::to_string(s.Id) + " " + (c ? NameOf(*c) : std::to_string(s.Clan)) + " " + s.Building + " at " + Num(s.X, 1) + " " + Num(s.Z, 1) + ": " + s.Status
			+ (s.Messenger.empty() ? std::string() : ", messenger " + s.Messenger) + (s.Voters > 0 ? ", vote " + std::to_string(s.Yes) + " of " + std::to_string(s.Voters) : std::string())
			+ (s.Site.empty() ? std::string() : ", site " + s.Site) + (s.Why.empty() ? std::string() : "; " + s.Why)
			+ ", day " + Num(s.Posted / static_cast<double>(Rules.DaySeconds), 2);
	}

	// director sign [preview] <clan> <building> <x> <z> [yaw] | sign [preview] <clan> <building> camp <dx> <dz> [yaw] | signs
	DirectorReply WorldDirector::SignCommand(const std::vector<std::string>& a)
	{
		if (a[0] == "signs")
		{
			std::string s = "director signs: " + std::to_string(m_Signs.size());
			for (const DirectorSign& sign : m_Signs) s += "\n" + SignText(sign);
			return { true, s };
		}
		const char* usage = "usage: director sign [preview] <clan> <building> <x> <z> [yaw] | sign [preview] <clan> <building> camp <dx> <dz> [yaw] | signs";
		size_t i = 1;
		const bool preview = a.size() > 1 && a[1] == "preview";
		if (preview) ++i;
		if (a.size() < i + 4 || !m_To.Clans) return { false, usage };
		const Clan* c = m_To.Clans->Find(a[i]);
		if (!c || !c->Folk) return { false, "cannot: no clan '" + a[i] + "'" };
		const std::string building = a[i + 1];
		const bool camp = a[i + 2] == "camp";
		if (camp && a.size() < i + 5) return { false, usage };
		const size_t n = camp ? i + 3 : i + 2;
		auto number = [](const std::string& w, float& out) { char* end = nullptr; out = std::strtof(w.c_str(), &end); return end != w.c_str() && *end == '\0' && std::isfinite(out); };
		float x = 0.0f, z = 0.0f, yaw = 0.0f;
		if (!number(a[n], x) || !number(a[n + 1], z) || (a.size() > n + 2 && !number(a[n + 2], yaw))) return { false, usage };
		if (camp) CampOffset(c->Id, x, z, x, z);
		if (!preview) return PlaceSign(c->Id, building, x, z, yaw);
		const DirectorSignPreview p = PreviewSign(c->Id, building, x, z);
		const std::string where = Num(x, 1) + " " + Num(z, 1);
		if (!p.Ok) return { true, "sign preview " + building + " at " + where + ": cannot: " + p.Why + (p.Cost > 0.0f ? " (cost " + Num(p.Cost) + ")" : std::string()) };
		return { true, "sign preview " + building + " at " + where + ": can, cost " + Num(p.Cost) + (p.SiteOnly ? " (only the place)" : "") + ", messenger "
			+ (p.Messenger.empty() ? std::string("nobody") : p.Messenger) + ", vote " + std::to_string(p.Yes) + " of " + std::to_string(p.Voters) + " (" + p.Outcome + ")"
			+ (p.Warning.empty() ? std::string() : "; " + p.Warning) };
	}
}
