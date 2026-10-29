#include "GB/Game/SimQA.h"
#include "GB/AI/Needs.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/World/Terrain.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace GB
{
	namespace
	{
		std::string Fixed(double v, int decimals = 1)
		{
			char b[48];
			std::snprintf(b, sizeof b, "%.*f", decimals, v);
			return b;
		}

		bool Finite(const glm::vec2& p) { return std::isfinite(p.x) && std::isfinite(p.y); }

		// The jobs that hold a claim on the node of their Detail: two members may never be on their way to the same one (the tribe's m_Claims).
		bool Claims(const std::string& job) { return job == "gather" || job == "approach"; }

		// A member has nothing to do: what the "idle" metric counts. Sleeping, eating or walking home is not idle.
		bool Idle(const TribeMember& m) { return m.Job.empty() || m.Job == "rest"; }

		// ---- the built-in invariants. Each looks at the view and reports what is wrong; none changes anything.

		// No node, store or bag holds a negative or empty count, and no node more than its capacity.
		void CheckCounts(const SimView& v, SimTrack&, SimReport& r)
		{
			if (v.Gameplay)
				for (const std::string& n : v.Gameplay->Names())
				{
					const AbilitySystemComponent* a = v.Gameplay->Find(n);
					if (!a || !a->HasTag("Resource.Node")) continue;
					const int amount = ResourceSystem::Amount(*a);
					if (amount < 0) r.Add(n, "holds " + std::to_string(amount));
					const float max = a->Attributes().Max("Amount");
					if (max > 0.0f && a->Attributes().Current("Amount") > max + 0.5f) r.Add(n, "holds " + std::to_string(amount) + ", over its capacity " + Fixed(max, 0));
				}
			if (!v.Clan) return;
			for (const ItemSlot& s : v.Clan->Store().Stacks()) if (s.Count <= 0) r.Add("store", s.Id + " x" + std::to_string(s.Count));
			for (const TribeMember& m : v.Clan->Members())
				if (m.Body)
					for (const ItemSlot& s : m.Body->Bag().Stacks()) if (s.Count <= 0) r.Add(m.Name, "carries " + s.Id + " x" + std::to_string(s.Count));
		}

		// Nobody walks to a node another member has claimed (the same units would be counted twice), and one member at most stokes the fire.
		void CheckClaims(const SimView& v, SimTrack&, SimReport& r)
		{
			if (!v.Clan) return;
			std::map<std::string, std::string> owner;
			std::string stoker;
			for (const TribeMember& m : v.Clan->Members())
			{
				if (m.Dead) continue;
				if (Claims(m.Job) && !m.Detail.empty())
				{
					auto [it, fresh] = owner.emplace(m.Detail, m.Name);
					if (!fresh) r.Add(m.Detail, "claimed by " + it->second + " and " + m.Name);
				}
				if (m.Job == "stoke")
				{
					if (!stoker.empty()) r.Add(v.Clan->Config.FireName, "stoked by " + stoker + " and " + m.Name + " at once");
					stoker = m.Name;
				}
			}
		}

		// Nobody is stuck: failing trip after trip (FailLimit failures within FailWindow), or standing on the same spot with the same busy job for
		// StillLimit seconds (resting and sleeping are allowed to last).
		void CheckStuck(const SimView& v, SimTrack& t, SimReport& r)
		{
			if (!v.Clan) return;
			for (const TribeMember& m : v.Clan->Members())
			{
				SimTrack::Member& k = t.Members[m.Name];
				const glm::vec2 pos = SimMemberPosition(m);
				const std::string doing = m.Job + " " + m.Detail;
				if (!k.Seen) { k.Seen = true; k.Failures = m.Failures; k.Pos = pos; k.Doing = doing; k.StillSince = v.Clock; continue; }
				if (m.Dead) continue;
				for (int i = k.Failures; i < m.Failures; ++i) k.FailedAt.push_back(v.Clock);
				k.Failures = m.Failures;
				while (!k.FailedAt.empty() && k.FailedAt.front() < v.Clock - t.FailWindow) k.FailedAt.pop_front();
				if (static_cast<int>(k.FailedAt.size()) >= t.FailLimit)
					r.Add(m.Name, std::to_string(k.FailedAt.size()) + " failed trips in " + Fixed(t.FailWindow, 0) + " s (now: " + doing + ")");
				const bool lasting = m.Job.empty() || m.Job == "rest" || m.Job == "sleep" || m.Job == "dead";
				if (glm::distance(pos, k.Pos) > 0.5f || doing != k.Doing || lasting) { k.Pos = pos; k.Doing = doing; k.StillSince = v.Clock; }
				else if (v.Clock - k.StillSince > t.StillLimit)
					r.Add(m.Name, "has not moved for " + Fixed(v.Clock - k.StillSince, 0) + " s doing " + doing);
			}
		}

		// The population is alive, or each death is explained by a need (hunger, thirst, cold) and agrees with its ability system.
		void CheckDeaths(const SimView& v, SimTrack&, SimReport& r)
		{
			if (!v.Clan || !v.Gameplay) return;
			for (const TribeMember& m : v.Clan->Members())
			{
				const AbilitySystemComponent* a = v.Gameplay->Find(m.Name);
				if (!a) { if (!m.Dead) r.Add(m.Name, "has no ability system"); continue; }
				if (m.Dead)
				{
					// a need that ran out (its body agrees), or a death of the society (old age, H9: the society agrees)
					const bool need = m.Cause == "Hunger" || m.Cause == "Thirst" || m.Cause == "Warmth" || m.Cause == "Wounds";   // (Wounds: killed in a fight, H14)
					const Person* p = v.Clan->People().Find(m.Name);
					if (need) { if (!IsDead(*a) || CauseOfDeath(*a) != m.Cause) r.Add(m.Name, "the tribe says it died of " + m.Cause + ", its body says '" + CauseOfDeath(*a) + "'"); }
					else if (m.Cause.empty() || m.Cause == "Unknown" || !p || !p->Dead || p->Cause != m.Cause) r.Add(m.Name, "died of '" + m.Cause + "', which neither a need nor the society explains");
				}
				else if (IsDead(*a)) r.Add(m.Name, "is dead (" + CauseOfDeath(*a) + ") but the tribe still counts it alive");
			}
		}

		// H14: the fights. Health and stamina stay in their ranges and are numbers; the dead never fight, flee or are fought; morale is 0..1 and the
		// wounds 0..1 each, below the lethal total; and a wound that no longer bleeds never gets worse (they heal: the total of a member only grows
		// with a new wound).
		void CheckFights(const SimView& v, SimTrack& t, SimReport& r)
		{
			if (!v.Clan || !v.Gameplay || !v.Clan->Combat()) return;
			const CombatSystem& cs = *v.Clan->Combat();
			for (const TribeMember& m : v.Clan->Members())
			{
				const AbilitySystemComponent* a = v.Gameplay->Find(m.Name);
				if (a)
					for (const char* attr : { "Health", "Stamina" })
					{
						if (!a->Attributes().Has(attr)) continue;
						const float x = a->Attribute(attr);
						if (!std::isfinite(x) || x < a->Attributes().Min(attr) - 0.01f || x > a->Attributes().Max(attr) + 0.01f) r.Add(m.Name, std::string(attr) + " " + Fixed(x) + " is out of its range");
					}
				const Combatant* c = cs.Find(m.Name);
				if (!c) continue;
				if (m.Dead && (c->Mode == FightMode::Engage || c->Mode == FightMode::Flee)) r.Add(m.Name, "is dead but still " + std::string(FightModeName(c->Mode)));
				if (m.Dead && !cs.AttackersOf(m.Name).empty()) r.Add(m.Name, "is dead but " + cs.AttackersOf(m.Name).front() + " still fights it");
				if (!(c->Morale >= 0.0f && c->Morale <= 1.0f)) r.Add(m.Name, "morale " + Fixed(c->Morale, 2) + " is out of 0..1");
				float total = 0.0f;
				bool bleeding = false;
				for (const Wound& w : c->Wounds)
				{
					if (!(w.Severity >= 0.0f && w.Severity <= 1.0f)) r.Add(m.Name, "a wound of severity " + Fixed(w.Severity, 2));
					total += w.Severity;
					bleeding = bleeding || w.Bleeding > 0.0f;
				}
				if (!m.Dead && total >= cs.Rules.Injuries.Lethal) r.Add(m.Name, "lives with wounds of " + Fixed(total, 2) + ", past the lethal " + Fixed(cs.Rules.Injuries.Lethal, 2));
				auto& last = t.Wounds[m.Name];
				if (!m.Dead && !bleeding && c->Wounds.size() <= last.first && total > last.second + 1e-4f) r.Add(m.Name, "its wounds got worse with no new blow (" + Fixed(last.second, 2) + " -> " + Fixed(total, 2) + ")");
				last = { c->Wounds.size(), total };
			}
		}

		// Every store has room for what it holds and no item piles up far over the cap of its kind at which gathering into it stops (H18: the
		// main store's cap is EconomyRules::Main().Cap, a stockpile's its kind's; a member who started before the store was full may still bring a
		// bagful: the cap plus one full bag per living member is the bound).
		void CheckStore(const SimView& v, SimTrack&, SimReport& r)
		{
			if (!v.Clan) return;
			int bags = 0;
			for (const TribeMember& m : v.Clan->Members()) if (m.Body && !m.Dead) bags += m.Body->Bag().Slots();
			auto check = [&](const std::string& name, const Inventory& store, int cap)
			{
				if (store.UsedSlots() > store.Slots()) r.Add(name, std::to_string(store.UsedSlots()) + " stacks in " + std::to_string(store.Slots()) + " slots");
				std::map<std::string, int> units;
				for (const ItemSlot& s : store.Stacks()) units[s.Id] += s.Count;
				for (const auto& [id, n] : units) if (n > cap + bags) r.Add(name, id + " " + std::to_string(n) + " over the bound " + std::to_string(cap + bags) + " (cap " + std::to_string(cap) + ")");
			};
			check("store", v.Clan->Store(), v.Clan->StoreCap());
			const auto& kinds = v.Clan->Config.Econ.Logistics.Stores;
			for (const Stockpile& pile : v.Clan->Economy().Stockpiles)
			{
				const auto kind = kinds.find(pile.Kind);
				if (kind == kinds.end()) { r.Add(pile.Name, "is a stockpile of an unknown kind '" + pile.Kind + "'"); continue; }
				for (const ItemSlot& s : pile.Store.Stacks()) if (s.Count <= 0) r.Add(pile.Name, s.Id + " x" + std::to_string(s.Count));
				check(pile.Name, pile.Store, kind->second.Cap);
			}
		}

		// The tribe's fire is the interactive it says it is: the entity is there, its fuel is within limits, it is lit exactly when it has fuel to burn
		// and its flame shows exactly then.
		void CheckFire(const SimView& v, SimTrack&, SimReport& r)
		{
			if (!v.Clan || !v.Gameplay || !v.Clan->HasFire()) return;
			const std::string& name = v.Clan->Config.FireName;
			const AbilitySystemComponent* a = v.Gameplay->Find(name);
			if (!a) { r.Add(name, "the tribe has a fire but there is no interactive"); return; }
			const float fuel = a->Attributes().Current("Fuel"), max = a->Attributes().Max("Fuel");
			if (fuel < 0.0f || (max > 0.0f && fuel > max + 1e-3f)) r.Add(name, "fuel " + Fixed(fuel) + " outside 0.." + Fixed(max));
			const bool lit = v.Clan->FireBurning();
			if (lit != (fuel >= 1.0f)) r.Add(name, std::string(lit ? "lit" : "not lit") + " with fuel " + Fixed(fuel));
			if (v.Entities)
			{
				if (v.Entities->FindByName(name) == NullEntity) r.Add(name, "the fire's entity is gone");
				const EntityId flame = v.Entities->FindByName(name + " Flame");
				if (flame != NullEntity && v.Entities->IsActive(flame) != lit) r.Add(name, std::string("the flame shows ") + (lit ? "out" : "lit") + " while the fire is " + (lit ? "lit" : "out"));
			}
		}

		// H7: the job board keeps its word: no job has more claims than slots, no claim outlives ClaimSeconds without progress (they lapse at the
		// board's refresh: Every seconds of slack), a member holds one job at most, and only living members of the clan hold any.
		void CheckBoard(const SimView& v, SimTrack&, SimReport& r)
		{
			if (!v.Clan) return;
			const JobRules& rules = v.Clan->Config.Work.Jobs;
			const double now = v.Clan->Time();
			std::map<std::string, const TribeMember*> members;
			for (const TribeMember& m : v.Clan->Members()) members[m.Name] = &m;
			std::map<std::string, std::string> held;
			for (const auto& [id, job] : v.Clan->Board().Jobs())
			{
				if (static_cast<int>(job.Claims.size()) > job.Slots) r.Add(id, std::to_string(job.Claims.size()) + " claims for " + std::to_string(job.Slots) + " slot(s)");
				for (const JobClaim& c : job.Claims)
				{
					const double idle = now - std::max(c.Progress, c.Since);
					if (idle > static_cast<double>(rules.ClaimSeconds + rules.Every) + 1.0) r.Add(id, c.Member + "'s claim has had no progress for " + Fixed(idle, 0) + " s (it lapses at " + Fixed(rules.ClaimSeconds, 0) + ")");
					const auto it = members.find(c.Member);
					if (it == members.end()) r.Add(id, "claimed by " + c.Member + ", who is not in the clan");
					else if (it->second->Dead) r.Add(id, "still claimed by " + c.Member + ", who is dead");
					auto [h, fresh] = held.emplace(c.Member, id);
					if (!fresh) r.Add(c.Member, "holds two jobs: " + h->second + " and " + id);
				}
			}
		}

		// H7: every aptitude is a level in 0..Max (finite).
		void CheckSkills(const SimView& v, SimTrack&, SimReport& r)
		{
			if (!v.Clan) return;
			const float max = v.Clan->Config.Work.Skills.Max;
			for (const TribeMember& m : v.Clan->Members())
				for (const auto& [family, level] : m.Skills)
					if (!std::isfinite(level) || level < 0.0f || level > max + 1e-3f) r.Add(m.Name, family + " at " + Fixed(level, 2) + " (0.." + Fixed(max, 0) + ")");
		}

		// H11: nobody makes a recipe it does not know (the tribe gates crafting by knowledge; this reads what the log says was made and asks the
		// knowledge), and what experiments spoil is accounted: at most one unit per failed try, of an item that exists.
		void CheckKnowledge(const SimView& v, SimTrack& t, SimReport& r)
		{
			if (!v.Clan) return;
			const ClanKnowledge& k = v.Clan->Knowledge();
			for (const SimTrack::Line& line : t.NewLines)
			{
				const size_t made = line.Text.rfind("makes a ", 0);
				if (made == 0 && k.Gated())
				{
					const std::string recipe = line.Text.substr(8, line.Text.find(' ', 8) - 8);
					if (!k.CanMake(line.Who, recipe)) r.Add(line.Who, "made " + recipe + " at " + Fixed(line.At, 0) + " s without knowing how (" + k.Status(line.Who) + ")");
				}
			}
			int spoiled = 0;
			for (const auto& [item, n] : t.Spoiled) spoiled += n;
			if (spoiled > t.FailedTries) r.Add("experiments", std::to_string(spoiled) + " units spoilt by " + std::to_string(t.FailedTries) + " failed tries (one at most each)");
		}

		// H12: an expedition's party comes home or is accounted for. While it is out: its members are members of the clan and it is called home
		// by MaxSeconds (plus a look of slack). Once it ends: each living member of the party stands within the home radius of the camp again within
		// ReturnGrace (or dies: accounted).
		void CheckExpeditions(const SimView& v, SimTrack& t, SimReport& r)
		{
			if (!v.Clan) return;
			const Tribe& clan = *v.Clan;
			const ExplorationState& x = clan.Exploration();
			const ExplorationRules& rules = clan.Config.Explore;
			std::map<std::string, const TribeMember*> members;
			for (const TribeMember& m : clan.Members()) members[m.Name] = &m;
			if (x.Current.Stage == "out")
			{
				for (const std::string& who : x.Current.Party)
					if (!members.count(who)) r.Add("expedition " + std::to_string(x.Current.Id), who + " is in the party but not in the clan");
				const double out = clan.Time() - x.Current.Started;
				if (out > static_cast<double>(rules.Expeditions.MaxSeconds + rules.Expeditions.Every) + 1.0)
					r.Add("expedition " + std::to_string(x.Current.Id), "out for " + Fixed(out, 0) + " s (it comes home at " + Fixed(rules.Expeditions.MaxSeconds, 0) + ")");
			}
			for (const ExpeditionRecord& rec : x.History)
				if (t.SeenExpeditions.insert(rec.Id).second)
					for (const std::string& who : rec.Party) t.Away[rec.Id][who] = v.Clock;
			const float home = rules.Migration.HomeRadius;
			for (auto it = t.Away.begin(); it != t.Away.end();)
			{
				for (auto w = it->second.begin(); w != it->second.end();)
				{
					const auto m = members.find(w->first);
					const bool accounted = m == members.end() || m->second->Dead || glm::distance(SimMemberPosition(*m->second), clan.Config.Camp) < home;
					if (accounted) { w = it->second.erase(w); continue; }
					if (v.Clock - w->second > t.ReturnGrace)
						r.Add(w->first, "has not come home " + Fixed(v.Clock - w->second, 0) + " s after expedition " + std::to_string(it->first) + " ended (" + Fixed(glm::distance(SimMemberPosition(*m->second), clan.Config.Camp), 0) + " m from the camp)");
					++w;
				}
				it = it->second.empty() ? t.Away.erase(it) : std::next(it);
			}
		}

		// H12: after the camp moves, the store, the fire and the members are at the new camp: the camp is where the last move says, the campfire
		// stands by it, and every living member reaches it within ArriveGrace.
		void CheckMigration(const SimView& v, SimTrack& t, SimReport& r)
		{
			if (!v.Clan) return;
			const Tribe& clan = *v.Clan;
			const ExplorationState& x = clan.Exploration();
			if (!x.Moves.empty())
			{
				const MigrationRecord& last = x.Moves.back();
				if (last.At != t.LastMoveAt)
				{
					t.LastMoveAt = last.At;
					t.NotArrived.clear();
					for (const TribeMember& m : clan.Members()) if (!m.Dead) t.NotArrived[m.Name] = v.Clock;
				}
				if (glm::distance(last.To, clan.Config.Camp) > 0.01f)
					r.Add("camp", "the camp is at (" + Fixed(clan.Config.Camp.x) + ", " + Fixed(clan.Config.Camp.y) + "), the last move went to (" + Fixed(last.To.x) + ", " + Fixed(last.To.y) + ")");
				if (clan.HasFire() && v.Entities)
					if (const EntityId fire = v.Entities->FindByName(clan.Config.FireName); fire != NullEntity)
					{
						const glm::vec4 p = v.Entities->WorldMatrix(fire)[3];
						if (glm::distance(glm::vec2(p.x, p.z), clan.Config.Camp) > 4.0f) r.Add(clan.Config.FireName, "stayed at (" + Fixed(p.x) + ", " + Fixed(p.z) + ") when the camp moved");
					}
			}
			const float arrive = std::max(clan.Config.Explore.Migration.Arrive, 10.0f);
			for (auto it = t.NotArrived.begin(); it != t.NotArrived.end();)
			{
				const TribeMember* m = nullptr;
				for (const TribeMember& c : clan.Members()) if (c.Name == it->first) m = &c;
				if (!m || m->Dead || glm::distance(SimMemberPosition(*m), clan.Config.Camp) < arrive) { it = t.NotArrived.erase(it); continue; }
				if (v.Clock - it->second > t.ArriveGrace) r.Add(it->first, "has not reached the new camp " + Fixed(v.Clock - it->second, 0) + " s after the move");
				++it;
			}
		}

		// H9 + H10: the dead stay in the list but never act again (no job but "dead", no ability running, no step taken, no counter moving); a
		// member keeps at most MaxPerMember bonds; every gene and trait is in 0..1.
		void CheckSociety(const SimView& v, SimTrack& t, SimReport& r)
		{
			if (!v.Clan) return;
			const Tribe& clan = *v.Clan;
			for (const TribeMember& m : clan.Members())
			{
				if (!m.Dead) continue;
				SimTrack::Member& k = t.Members[m.Name];
				const glm::vec2 pos = SimMemberPosition(m);
				const int count = m.Deposited + m.Made + m.Meals;
				if (!k.DeadSeen) { k.DeadSeen = true; k.DeadPos = pos; k.DeadCount = count; continue; }
				if (m.Job != "dead") r.Add(m.Name, "is dead but its job is '" + m.Job + "'");
				if (glm::distance(pos, k.DeadPos) > 0.05f) r.Add(m.Name, "is dead but moved " + Fixed(glm::distance(pos, k.DeadPos), 2) + " m");
				if (count != k.DeadCount) r.Add(m.Name, "is dead but still deposits, makes or eats");
				if (v.Gameplay) if (const AbilitySystemComponent* a = v.Gameplay->Find(m.Name); a && a->AnyActive()) r.Add(m.Name, "is dead but an ability of it runs");
			}
			const Society& people = clan.People();
			const int maxBonds = clan.Config.Society.Relations.MaxPerMember;
			for (const auto& [name, p] : people.People())
			{
				const size_t bonds = people.BondsOf(name).size();
				if (static_cast<int>(bonds) > maxBonds) r.Add(name, std::to_string(bonds) + " bonds (at most " + std::to_string(maxBonds) + ")");
				for (const auto& [gene, value] : p.Genes)
					if (!std::isfinite(value) || value < 0.0f || value > 1.0f) r.Add(name, "gene " + gene + " at " + Fixed(value, 3));
				for (const auto& [trait, value] : p.Traits)
					if (!std::isfinite(value) || value < 0.0f || value > 1.0f) r.Add(name, "trait " + trait + " at " + Fixed(value, 3));
			}
		}

		// Every living member is somewhere: no NaN or infinite position (its body's and, when abstract, its agent's).
		void CheckPositions(const SimView& v, SimTrack&, SimReport& r)
		{
			if (!v.Clan) return;
			for (const TribeMember& m : v.Clan->Members())
			{
				if (m.Dead || !m.Body) continue;
				if (!Finite(m.Body->Position())) r.Add(m.Name, "its body is at a NaN position");
				if (m.Abstract && !Finite(m.Agent.Position())) r.Add(m.Name, "its abstract agent is at a NaN position");
			}
		}

		// H8: every construction site and building of the tribe's data is consistent: its phase within its phases, its work within the phase's, nothing
		// delivered beyond what the phase needs, the tags as the state says (Site/Done, Ready), its progress never going back, and none in deep water.
		void CheckBuildings(const SimView& v, SimTrack& t, SimReport& r)
		{
			if (!v.Clan || !v.Gameplay || !v.Clan->Buildings()) return;
			for (const std::string& name : v.Gameplay->Names())
			{
				const AbilitySystemComponent* a = v.Gameplay->Find(name);
				if (!a || !a->HasTag("Building")) continue;
				const BuildingDef* def = BuildingOf(*a, *v.Clan->Buildings());
				if (!def) { r.Add(name, "a building of a kind the data does not have"); continue; }
				const int phase = static_cast<int>(std::lround(a->Attributes().Base("Phase")));
				const int phases = static_cast<int>(def->Phases.size());
				if (phase < 0 || phase > phases) { r.Add(name, "phase " + std::to_string(phase) + " of " + std::to_string(phases)); continue; }
				const SiteState st = ReadSite(*a, *def);
				if (st.Done != a->HasTag("Building.Done") || st.Done == a->HasTag("Building.Site")) r.Add(name, "its Site/Done tags do not match its phase");
				if (!st.Done)
				{
					const BuildingPhase& p = def->Phases[static_cast<size_t>(phase)];
					if (a->Attributes().Base("Work") > p.Work + 1e-3f) r.Add(name, "more work than its phase needs (" + Fixed(a->Attributes().Base("Work")) + " of " + Fixed(p.Work) + " s)");
					for (const auto& [item, n] : p.Needs)
						if (a->Attributes().Base("Delivered." + item) > static_cast<float>(n) + 1e-3f) r.Add(name, "more " + item + " delivered than its phase needs");
					if (st.Ready != a->HasTag("Building.Ready")) r.Add(name, "its Ready tag does not match what it has been brought");
				}
				float& last = t.SiteProgress[name];
				if (st.Progress + 1e-4f < last) r.Add(name, "its progress went back from " + Fixed(last) + " to " + Fixed(st.Progress));
				last = std::max(last, st.Progress);
				if (v.Ground && v.Entities)
					if (const EntityId id = v.Entities->FindByName(name); id != NullEntity)
					{
						const glm::vec4 at = v.Entities->WorldMatrix(id)[3];
						const std::string wrong = v.Ground({ at.x, at.z });
						if (wrong.find("water") != std::string::npos) r.Add(name, "it stands " + wrong);
					}
			}
			// the progression (docs/PROGRESION.md): never more sites open than the construction rules allow, and the provisions of what stands in range
			int open = 0;
			for (const Tribe::SettlementSite& s : v.Clan->SettlementSites()) if (!s.State.Done) ++open;
			if (open > std::max(1, v.Clan->Config.Econ.Construction.MaxSites)) r.Add("settlement", std::to_string(open) + " sites open, more than the rules allow");
			const float keep = v.Clan->StoreKeep();
			if (!(keep > 0.0f && keep <= 1.0f) || v.Clan->StoreBonus() < 0 || v.Clan->Defense() < 0.0f) r.Add("settlement", "the provisions of its buildings are out of range");
		}

		// Nobody stands in water too deep to wade, inside a trunk or a rock, or off the map.
		void CheckGround(const SimView& v, SimTrack&, SimReport& r)
		{
			if (!v.Clan || !v.Ground) return;
			for (const TribeMember& m : v.Clan->Members())
			{
				if (m.Dead || !m.Body) continue;
				const glm::vec2 p = SimMemberPosition(m);
				if (!Finite(p)) continue;   // (CheckPositions says so)
				const std::string wrong = v.Ground(p);
				if (!wrong.empty()) r.Add(m.Name, wrong + " at (" + Fixed(p.x) + ", " + Fixed(p.y) + ") doing " + m.Job + " " + m.Detail);
			}
		}
	}

	glm::vec2 SimMemberPosition(const TribeMember& m)
	{
		if (m.Abstract) return m.Agent.Position();
		return m.Body ? m.Body->Position() : glm::vec2(0.0f);
	}

	std::vector<SimInvariant>& SimInvariants()
	{
		static std::vector<SimInvariant> table = {
			{ "counts", "no node, store or bag holds a negative or empty count, and no node more than its capacity", CheckCounts },
			{ "claims", "no two members walk to the same node, and one at most stokes the fire", CheckClaims },
			{ "stuck", "nobody fails trip after trip, nor stands still on a busy job for long", CheckStuck },
			{ "deaths", "everyone is alive, or died of a need (hunger, thirst, cold) its body agrees with", CheckDeaths },
			{ "store", "every store (the main one and each stockpile) has room for what it holds and nothing piles far over its kind's cap", CheckStore },
			{ "fire", "the campfire is the interactive: fuel within limits, lit exactly when it has fuel, the flame shows it", CheckFire },
			{ "positions", "no member is at a NaN position (body or abstract agent)", CheckPositions },
			{ "ground", "nobody stands in deep water, inside a trunk or a rock, or off the map", CheckGround },
			{ "board", "the job board: claims within slots, none kept past its lapse, one job per member, held only by the living", CheckBoard },
			{ "skills", "every aptitude is a level in 0..100", CheckSkills },
			{ "knowledge", "nobody makes a recipe it does not know, and an experiment spoils one unit at most per failed try", CheckKnowledge },
			{ "expeditions", "an expedition's party is of the clan, is called home in time, and every member comes home (or died)", CheckExpeditions },
			{ "migration", "after a move the camp is where it went, the campfire stands by it and every member gets there", CheckMigration },
			{ "society", "the dead never act again; nobody keeps more than 12 bonds; genes and traits stay in 0..1", CheckSociety },
			// H13 (the checks are ClanWorld's own, GB/Game/Clans.cpp)
			{ "clans", "every member is in exactly one clan, and relations and meetings name clans that exist and envoys of their own clan",
				[](const SimView& v, SimTrack&, SimReport& r) { if (v.Clans) for (const auto& [who, what] : v.Clans->CheckMembers()) r.Add(who, what); } },
			{ "territory", "claims are finite, on land regions, of clans that exist, and deterministic (the same step twice gives the same map)",
				[](const SimView& v, SimTrack&, SimReport& r) { if (v.Clans) for (const auto& [who, what] : v.Clans->CheckTerritory()) r.Add(who, what); } },
			{ "buildings", "every site and building: phase and work within its data, nothing delivered beyond its phase, tags as its state, progress never back, none in water; no more sites open than the rules allow", CheckBuildings },
			{ "combat", "health and stamina in range; the dead neither fight nor are fought; morale and wounds in range; wounds heal (they only grow with a new blow)", CheckFights },
		};
		return table;
	}

	std::vector<SimMetricDef>& SimMetricTable()
	{
		static std::vector<SimMetricDef> table = {
			{ "berries in store", [](const SimView& v, SimMetrics& m) { if (v.Clan) m.Extra["store berry"] = v.Clan->Store().Count("berry"); } },
			// what the clan holds of what is not food: a surplus that piles up (fibre while the foragers pull it forever) shows here
			{ "fibre in store", [](const SimView& v, SimMetrics& m) { if (v.Clan) m.Extra["stored fiber"] = v.Clan->Stored("fiber"); } },
			{ "known things", [](const SimView& v, SimMetrics& m) { if (v.Clan) m.Extra["clan memory"] = static_cast<double>(v.Clan->ClanMemory().Size()); } },
			// H7: how far the aptitudes went (the mean of each member's best, and the highest), how the trades are shared out at the end, and the board
			{ "skills", [](const SimView& v, SimMetrics& m)
			{
				if (!v.Clan || v.Clan->Members().empty()) return;
				double sum = 0.0, top = 0.0;
				for (const TribeMember& t : v.Clan->Members())
				{
					float best = 0.0f;
					for (const auto& [family, level] : t.Skills) best = std::max(best, level);
					sum += static_cast<double>(best);
					top = std::max(top, static_cast<double>(best));
				}
				m.Extra["skill best avg"] = sum / static_cast<double>(v.Clan->Members().size());
				m.Extra["skill top"] = top;
			} },
			{ "trades", [](const SimView& v, SimMetrics& m) { if (v.Clan) for (const TribeMember& t : v.Clan->Members()) if (!t.Dead) m.Extra["role " + t.Role] += 1.0; } },
			{ "board", [](const SimView& v, SimMetrics& m)
			{
				if (!v.Clan) return;
				double unheld = 0.0;
				for (const auto& [id, job] : v.Clan->Board().Jobs()) if (job.Claims.empty()) unheld += 1.0;
				m.Extra["jobs"] = static_cast<double>(v.Clan->Board().Jobs().size());
				m.Extra["jobs unheld"] = unheld;
			} },
		};
		return table;
	}

	std::string SimMetrics::Line() const
	{
		std::string s = "alive " + std::to_string(Alive) + "/" + std::to_string(Members) + " | gathered";
		if (Gathered.empty()) s += " nothing";
		bool first = true;
		for (const auto& [item, n] : Gathered) { s += (first ? " " : ", ") + item + " " + std::to_string(n); first = false; }
		s += " | idle " + Fixed(IdlePercent(), 0) + "% | failures " + std::to_string(Failures) + " | deaths " + std::to_string(Deaths);
		for (const auto& [cause, n] : DeathsBy) s += " " + cause + ":" + std::to_string(n);
		s += " | meals " + std::to_string(Meals) + " | made " + std::to_string(Made);
		if (!MadeBy.empty()) { s += " ("; bool f = true; for (const auto& [item, n] : MadeBy) { s += (f ? "" : ", ") + item + " " + std::to_string(n); f = false; } s += ")"; }
		s += std::string() + " | deposited " + std::to_string(Deposited) + " | trade changes " + std::to_string(RoleChanges) + " | fire " + (Fire ? (FireLit ? "lit" : "out") : "none");
		s += " | experiments " + std::to_string(Experiments) + " (failed " + std::to_string(FailedTries) + ")";
		for (const auto& [item, n] : Spoiled) s += " spoilt " + item + ":" + std::to_string(n);
		s += " | births " + std::to_string(Births) + " | population " + std::to_string(Population) + " (peak " + std::to_string(PeakPopulation) + ")";
		auto list = [](const std::map<std::string, int>& m) { std::string t; for (const auto& [k, n] : m) t += (t.empty() ? " " : ", ") + k + " " + std::to_string(n); return t.empty() ? std::string(" none") : t; };
		s += " | spoiled" + list(SpoiledFood) + " | intermediates" + list(Intermediates) + " | harvests" + list(Harvests) + " | hauled " + std::to_string(Hauled);
		s += " | expeditions " + std::to_string(Expeditions) + " (found " + std::to_string(ExpeditionsFound) + ") | regions known " + std::to_string(RegionsKnown) + " | migrations " + std::to_string(Migrations);
		s += " | techs found";
		if (ClanGains.empty() && Discoveries.empty()) s += " none";
		// the first member to find or learn each technology, and how many came to know it in all
		std::vector<std::pair<std::string, int>> firsts;
		for (const std::string& d : Discoveries)
		{
			const size_t mark = d.find(" s ("), time = mark == std::string::npos || mark == 0 ? std::string::npos : d.rfind(' ', mark - 1);
			const std::string tech = d.substr(0, time);
			auto it = std::find_if(firsts.begin(), firsts.end(), [&](const auto& f) { return f.first.compare(0, tech.size() + 1, tech + " ") == 0; });
			if (it == firsts.end()) firsts.emplace_back(d, 1);
			else ++it->second;
		}
		for (const auto& [head, n] : firsts) s += " [" + head + (n > 1 ? ", " + std::to_string(n) + " in all" : std::string()) + "]";
		if (!ClanGains.empty()) { s += " | clan learnt"; for (const std::string& g : ClanGains) s += " [" + g + "]"; }
		for (const auto& [name, value] : Extra) s += " | " + name + " " + Fixed(value, 0);
		return s;
	}

	void SimMonitor::Reset()
	{
		m_Track = SimTrack();
		m_Findings.clear();
		m_Index.clear();
		m_Metrics = SimMetrics();
		m_Steps = 0;
	}

	void SimMonitor::Sample(const SimView& view)
	{
		if (!view.Clan) return;
		int alive = 0;
		for (const TribeMember& m : view.Clan->Members())
		{
			if (!m.Dead) ++alive;
			std::string& role = m_Track.Members[m.Name].Role;
			if (!role.empty() && role != m.Role) ++m_Metrics.RoleChanges;
			role = m.Role;
			if (m.Dead) continue;
			++m_Metrics.Samples;
			if (Idle(m)) ++m_Metrics.IdleSamples;
		}
		m_Metrics.PeakPopulation = std::max(m_Metrics.PeakPopulation, alive);
	}

	// The lines the tribe said since the last look. The log is a ring (its oldest quarter goes at once), so the place of the last line read is
	// found from the end; a line is "<time> <name> (<role>) <text>".
	void SimMonitor::ReadLog(const SimView& view)
	{
		m_Track.NewLines.clear();
		if (!view.Clan) return;
		const std::vector<std::string>& log = view.Clan->Log();
		size_t from = 0;
		if (!m_Track.LastLog.empty())
			for (size_t i = log.size(); i-- > 0;)
				if (log[i] == m_Track.LastLog) { from = i + 1; break; }
		for (size_t i = from; i < log.size(); ++i)
		{
			const std::string& text = log[i];
			SimTrack::Line line;
			line.At = std::atof(text.c_str());
			const size_t open = text.find(" ("), close = text.find(") ", open == std::string::npos ? 0 : open);
			if (open == std::string::npos || close == std::string::npos) continue;
			size_t start = text.find_first_not_of(' ');
			start = text.find(' ', start);   // past the time
			line.Who = text.substr(start + 1, open - start - 1);
			line.Text = text.substr(close + 2);
			if (line.Text.rfind("makes a ", 0) == 0) ++m_Track.MadeBy[line.Text.substr(8, line.Text.find(' ', 8) - 8)];
			if (line.Text.rfind("tries to work out ", 0) == 0)
			{
				++m_Track.FailedTries;
				const size_t one = line.Text.find("; one ");
				if (one != std::string::npos) ++m_Track.Spoiled[line.Text.substr(one + 6, line.Text.find(' ', one + 6) - one - 6)];
			}
			const bool found = line.Text.rfind("discovers ", 0) == 0, learnt = line.Text.rfind("learns ", 0) == 0;
			if (found || learnt)
			{
				// "discovers <Tech> by experimenting...", "learns <Tech> by watching <who>" / "learns <Tech> from <who> at the camp"
				size_t end = found ? line.Text.find(" by experimenting") : std::min(line.Text.find(" by watching "), line.Text.find(" from "));
				const size_t begin = found ? 10 : 7;
				const std::string tech = line.Text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
				const std::string how = found ? "by experimenting" : line.Text.find(" by watching ") != std::string::npos ? "by watching" : "taught";
				m_Track.Discoveries.push_back(tech + " " + Fixed(line.At, 0) + " s (" + line.Who + ", " + how + ")");
			}
			m_Track.NewLines.push_back(line);
		}
		if (!log.empty()) m_Track.LastLog = log.back();
		// what the clan knows, against the last look: when it came to know each technology
		const std::set<std::string>& clan = view.Clan->Knowledge().Clan();
		if (m_Steps > 1)
			for (const std::string& tech : clan) if (!m_Track.ClanTechs.count(tech)) m_Track.ClanGains.push_back(tech + " " + Fixed(view.Clock, 0) + " s");
		m_Track.ClanTechs = clan;
	}

	int SimMonitor::Step(const SimView& view)
	{
		++m_Steps;
		ReadLog(view);
		SimReport report;
		for (const SimInvariant& inv : SimInvariants())
		{
			report.Begin(inv.Name);
			if (inv.Check) inv.Check(view, m_Track, report);
		}
		for (SimFinding& f : report.Items)
		{
			// one finding per check and subject: a member stuck in the lake for an hour is one problem seen many times, not thousands
			const std::string key = f.Check + "\n" + f.Subject;
			const auto it = m_Index.find(key);
			if (it == m_Index.end())
			{
				f.First = f.Last = view.Clock;
				m_Index[key] = m_Findings.size();
				m_Findings.push_back(f);
			}
			else
			{
				SimFinding& old = m_Findings[it->second];
				old.Last = view.Clock;
				old.What = f.What;
				++old.Count;
			}
		}
		return static_cast<int>(report.Items.size());
	}

	void SimMonitor::Finish(const SimView& view)
	{
		m_Metrics.Seconds = view.Clock;
		m_Metrics.Extra.clear();
		if (view.Clan)
		{
			const Tribe& t = *view.Clan;
			m_Metrics.Members = static_cast<int>(t.Members().size());
			m_Metrics.Alive = m_Metrics.Deaths = m_Metrics.Failures = m_Metrics.Meals = m_Metrics.Made = m_Metrics.Deposited = 0;
			m_Metrics.DeathsBy.clear();
			for (const TribeMember& m : t.Members())
			{
				if (m.Dead) { ++m_Metrics.Deaths; ++m_Metrics.DeathsBy[m.Cause]; }
				else ++m_Metrics.Alive;
				m_Metrics.Failures += m.Failures;
				m_Metrics.Meals += m.Meals;
				m_Metrics.Made += m.Made;
				m_Metrics.Deposited += m.Deposited;
			}
			m_Metrics.Fire = t.HasFire();
			m_Metrics.FireLit = t.FireBurning();
		}
		m_Metrics.MadeBy = m_Track.MadeBy;
		m_Metrics.Spoiled = m_Track.Spoiled;
		m_Metrics.FailedTries = m_Track.FailedTries;
		m_Metrics.Experiments = m_Track.FailedTries + static_cast<int>(std::count_if(m_Track.Discoveries.begin(), m_Track.Discoveries.end(), [](const std::string& d) { return d.find("by experimenting") != std::string::npos; }));
		m_Metrics.Discoveries = m_Track.Discoveries;
		m_Metrics.ClanGains = m_Track.ClanGains;
		if (view.Clan)
		{
			const ExplorationState& x = view.Clan->Exploration();
			m_Metrics.Expeditions = static_cast<int>(m_Track.SeenExpeditions.size()) + (x.Current.Stage == "out" ? 1 : 0);
			m_Metrics.ExpeditionsFound = 0;
			for (const ExpeditionRecord& rec : x.History) if (rec.Found) ++m_Metrics.ExpeditionsFound;
			m_Metrics.Migrations = static_cast<int>(x.Moves.size());
			m_Metrics.RegionsKnown = static_cast<int>(x.Atlas.Lore.size());
			m_Metrics.Births = view.Clan->People().Births();
			const EconomyState& econ = view.Clan->Economy();
			m_Metrics.SpoiledFood = econ.Spoiled;
			m_Metrics.Intermediates = econ.Made;
			m_Metrics.Harvests = econ.Harvested;
			m_Metrics.Hauled = econ.Hauled;
			m_Metrics.Population = m_Metrics.Alive;
		}
		for (const SimMetricDef& def : SimMetricTable()) if (def.Measure) def.Measure(view, m_Metrics);
	}

	std::map<std::string, int> SimMonitor::CountByCheck() const
	{
		std::map<std::string, int> out;
		for (const SimInvariant& inv : SimInvariants()) out[inv.Name] = 0;
		for (const SimFinding& f : m_Findings) out[f.Check] += f.Count;
		return out;
	}

	std::string SimMonitor::Describe(size_t max) const
	{
		std::string s;
		for (size_t i = 0; i < m_Findings.size() && i < max; ++i)
		{
			const SimFinding& f = m_Findings[i];
			s += (s.empty() ? "" : "\n") + std::string("[") + f.Check + "] " + f.Subject + ": " + f.What + " (x" + std::to_string(f.Count) + ", first at " + Fixed(f.First, 0) + " s)";
		}
		if (m_Findings.size() > max) s += "\n... and " + std::to_string(m_Findings.size() - max) + " more";
		return s;
	}

	std::string SimGroundAt(const CharacterWorld& world, const glm::vec2& p, float maxWade)
	{
		if (p.x < world.BoundsMin.x - 0.5f || p.y < world.BoundsMin.y - 0.5f || p.x > world.BoundsMax.x + 0.5f || p.y > world.BoundsMax.y + 0.5f) return "off the map";
		if (world.WaterLevel && world.GroundHeight)
		{
			const float depth = world.WaterLevel(p.x, p.y) - world.GroundHeight(p.x, p.y);
			if (depth > maxWade + 0.05f) return "in water " + Fixed(depth) + " m deep";
		}
		const size_t statics = std::min(world.Obstacles.size(), world.DynamicFrom);
		for (size_t i = 0; i < statics; ++i)
		{
			const CircleObstacle& o = world.Obstacles[i];
			if (glm::distance(o.Center, p) < o.Radius - 0.05f) return "inside an obstacle (r " + Fixed(o.Radius, 2) + " at " + Fixed(o.Center.x) + ", " + Fixed(o.Center.y) + ")";
		}
		return {};
	}

	// ---------------------------------------------------------------- SimBody

	SimBody::SimBody(std::string name, GameplayWorld& gameplay, const EntityWorld& entities, const NavGrid& nav, const ItemLibrary& items, glm::vec2 start)
		: m_Name(std::move(name)), m_Gameplay(gameplay), m_Entities(entities), m_Nav(nav), m_Items(items)
	{
		m_Agent.Place(start);
	}

	bool SimBody::CanReach(const glm::vec2& to) const
	{
		if (!m_Nav.Ready()) return true;
		NavPath path;
		return m_Nav.Walkable(to, m_Profile) && m_Nav.FindPath(m_Profile, m_Agent.Position(), to, path);
	}

	// The path the actor would plan (the editor's ActorBody::Route): a goal inside a wall (a trunk) is fine when the path ends within reach of it.
	bool SimBody::Route(const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points) const
	{
		if (!m_Nav.Ready()) { points.assign(1, to); return true; }
		NavPath path;
		const bool found = m_Nav.FindPath(m_Profile, from, to, path);
		if (path.Points.empty()) return false;
		if (!found && glm::distance(path.Points.back(), to) > reach + m_Nav.CellSize()) return false;
		points = std::move(path.Points);
		return true;
	}

	void SimBody::Update(float dt)
	{
		if (m_Suspended) return;   // the tribe moves it as an abstract agent
		AbilitySystemComponent* me = m_Gameplay.Find(m_Name);
		if (me) ResourceSystem::SyncTools(*me, m_Bag, m_Items);
		const float vigor = me && me->Attributes().Has(NeedName::Vigor) ? me->Attribute(NeedName::Vigor) : 1.0f;   // the tired and hungry walk slower
		const EntityWorld* entities = &m_Entities;
		m_Agent.Update(dt, me, m_Gameplay, [entities](const std::string& entity, glm::vec2& pos)
		{
			const EntityId id = entities->FindByName(entity);
			if (id == NullEntity) return false;
			const glm::vec4 p = entities->WorldMatrix(id)[3];
			pos = { p.x, p.z };
			return true;
		}, 3.0f * std::max(0.3f, vigor), [this](const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points) { return Route(from, to, reach, points); },
			[this]() { return MayPlan(); });
	}

	// ---------------------------------------------------------------- SimRun

	SimRun::SimRun(const SimSetup& setup) : m_Setup(setup)
	{
		if (!setup.World || !setup.World->TerrainRef) { m_Error = "no world"; return; }
		const GeneratedWorld& world = *setup.World;
		if (setup.Items && setup.Recipes && setup.Nodes) { m_Items = *setup.Items; m_Recipes = *setup.Recipes; m_Defs = *setup.Nodes; }
		else RegisterEraOneItems(m_Items, m_Recipes, m_Defs);
		m_Techs = setup.Techs ? *setup.Techs : DefaultTechs();
		RegisterResourceContent(m_Gameplay.Library, m_Defs);
		RegisterInteractiveContent(m_Gameplay.Library);
		if (setup.Buildings) RegisterBuildingContent(m_Gameplay.Library);   // H8: the ability Build

		// The ground the members walk: the terrain's height and water, the world's trunks and rocks as walls (as the editor's actors see them:
		// PlayerMode::MakeHost), and the undergrowth of each biome.
		m_Ground = MakeTerrainCharacterWorld(*world.TerrainRef);
		for (const TreePlacement& tree : world.Trees) m_Ground.Obstacles.push_back({ { tree.Position.x, tree.Position.z }, 0.3f * tree.Scale });
		for (const RockPlacement& rock : world.Rocks)
		{
			const float size = rock.Variant >= 0 && rock.Variant < static_cast<int>(world.RockRecipes.size()) ? world.RockRecipes[static_cast<size_t>(rock.Variant)].Size : 1.0f;
			m_Ground.Obstacles.push_back({ { rock.Position.x, rock.Position.z }, 0.8f * size * rock.Scale });
		}
		NavConfig nav;
		if (setup.PlanBudget > 0) nav.MaxPlansPerFrame = setup.PlanBudget;
		nav.PathCache = std::max(setup.PathCache, 0);
		nav.MaxExpandedPerFrame = std::max(setup.PlanCells, 0);
		const GeneratedWorld* w = &world;
		nav.Vegetation = [w](float x, float z)
		{
			switch (w->BiomeAt(x, z))
			{
			case Biome::Forest: return 1.0f;
			case Biome::Shore: return 0.3f;
			case Biome::Meadow: return 0.15f;
			default: return 0.0f;
			}
		};
		if (!m_Nav.Build(m_Ground, nav, &m_Error)) return;

		// The camp: the one given, or the flattest dry place near the middle of the map (the editor's spawn), clear of trunks and rocks.
		const glm::vec2 camp = setup.HasCamp ? setup.Camp : FindFreeSpot(m_Ground, world.Spawn, 2.0f);
		if (setup.Scene)
		{
			m_Entities = *setup.Scene;
			if (const EntityId fire = m_Entities.FindByName("Campfire"); fire != NullEntity) m_Entities.Destroy(fire);   // the new tribe lights its own
		}
		else
		{
			m_Spots = PlanWorldResources(ResourceGroundOf(world), m_Defs, world.Desc.Seed, setup.ResourceDensity);
			BuildWorldResources(m_Entities, m_Spots, m_Defs);
		}
		if (m_Entities.FindByName("Well") == NullEntity)   // a well near the camp (the members get thirsty), as `tribe build` puts one
		{
			const glm::vec2 well = FindFreeSpot(m_Ground, camp + glm::vec2(4.0f, -6.0f), 1.5f);
			const InteractiveDef& def = m_Rules.Defs.at("well");
			const glm::vec2 origin = well - glm::vec2(def.At.x, def.At.z);
			BuildInteractives(m_Entities, glm::vec3(origin.x, world.TerrainRef->GetHeight(well.x, well.y), origin.y), m_Rules.Defs);
			for (const char* extra : { "Fire Pit", "Gate" }) if (const EntityId id = m_Entities.FindByName(extra); id != NullEntity) m_Entities.Destroy(id);
		}

		if (setup.Config)
		{
			m_Tribe.Config.Roles = setup.Config->Roles;
			m_Tribe.Config.Work = setup.Config->Work;
			m_Tribe.Config.Agenda = setup.Config->Agenda;
			m_Tribe.Config.Economy = setup.Config->Economy;
		}
		m_Tribe.Bind(m_Entities, m_Gameplay, m_Items, m_Recipes, m_Defs);   // (bound again below, once the members' ability systems exist)
		std::vector<std::pair<std::string, std::string>> roster = setup.Roster;
		if (roster.empty())
		{
			// as `tribe build` (H7): the names from the data and the trades from the clan's needs
			const int clans = std::max(1, setup.Clans);
			const int n = std::max(1, setup.Members / clans + setup.Members % clans);   // (H16: with several clans the first takes its share and the remainder)
			const std::vector<std::string> roles = m_Tribe.PlanRoles(n);
			for (int i = 0; i < n; ++i)
				roster.emplace_back(MemberName(m_Tribe.Config.Work, static_cast<size_t>(i)), static_cast<size_t>(i) < roles.size() ? roles[static_cast<size_t>(i)] : std::string("forager"));
		}
		for (const auto& member : roster)
		{
			EntityId id = m_Entities.FindByName(member.first);
			if (id == NullEntity) id = m_Entities.Create(member.first);
			Entity& e = *m_Entities.Find(id);
			if (!e.Gameplay) e.Gameplay.emplace();
		}
		m_Gameplay.Load(m_Entities);
		for (const auto& member : roster)
			if (AbilitySystemComponent* asc = m_Gameplay.Find(member.first))
				for (const char* work : { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "Build" }) asc->Grant(work);   // what every actor can do (Actor.cpp; Build only with buildings)
		for (const std::string& n : m_Gameplay.Names())
			if (const AbilitySystemComponent* a = m_Gameplay.Find(n); a && a->HasTag("Resource.Node")) ++m_NodeCount;

		for (size_t i = 0; i < roster.size(); ++i)
		{
			// rows of up to 8, as `tribe build` stands them
			const size_t col = i % 8, row = i / 8, inRow = std::min<size_t>(8, roster.size() - row * 8);
			glm::vec2 start = camp + glm::vec2((static_cast<float>(col) - static_cast<float>(inRow - 1) * 0.5f) * 1.5f, 1.5f * static_cast<float>(row));
			if (!m_Nav.Walkable(start)) start = FindFreeSpot(m_Ground, start, 0.6f);
			m_Bodies.push_back(std::make_unique<SimBody>(roster[i].first, m_Gameplay, m_Entities, m_Nav, m_Items, start));
			m_Index[roster[i].first] = i;
			if (const auto bag = setup.Bags.find(roster[i].first); bag != setup.Bags.end()) m_Bodies.back()->Bag().FromJson(bag->second);
		}
		auto bag = [this](const std::string& who) -> Inventory* { const auto it = m_Index.find(who); return it == m_Index.end() ? nullptr : &m_Bodies[it->second]->Bag(); };
		m_Resources.Bind(m_Gameplay, m_Defs, [this, bag](const std::string& who, const std::string& item, int count)
		{
			if (Inventory* b = bag(who)) m_Monitor.NoteYield(item, b->Add(m_Items, item, count));
		});
		m_Rules.Items = &m_Items;
		m_Rules.Bind(m_Gameplay,
			[bag](const std::string& who, const std::string& item, int count) { Inventory* b = bag(who); return b ? b->Remove(item, count) : 0; },
			[this, bag](const std::string& who, const std::string& item, int count) { Inventory* b = bag(who); return b ? b->Add(m_Items, item, count) : 0; });

		m_Tribe.Bind(m_Entities, m_Gameplay, m_Items, m_Recipes, m_Defs);
		m_Tribe.SetInteractives(&m_Rules.Defs);
		m_Tribe.SetTechs(&m_Techs);   // H11: the members know the technologies of the start and find, watch and teach the rest (as the editor's tribe)
		if (setup.Buildings)
		{
			// H8: the clan plans and builds (the sites stand on the terrain; the system adds the work of each Build)
			m_Builds.Defs = *setup.Buildings;
			m_Builds.Bind(m_Gameplay);
			m_Tribe.SetBuildings(&m_Builds.Defs);
			m_Tribe.SetGround([terrain = world.TerrainRef](float x, float z) { return terrain->GetHeight(x, z); });
		}
		// H12: the regions of the world (the editor's, or cut here with the clan's rules and the nodes planned on it), so expeditions and moves can happen
		if (setup.Config) { m_Tribe.Config.Explore = setup.Config->Explore; m_Tribe.Config.Society = setup.Config->Society; m_Tribe.Config.Econ = setup.Config->Econ; }
		if (!setup.Regions) m_Regions = RegionMap::Build(world, m_Tribe.Config.Explore.Regions, RegionThingsOf(m_Spots));
		const Terrain* terrain = world.TerrainRef;
		m_Tribe.SetRegions(setup.Regions ? setup.Regions : &m_Regions, [terrain](float x, float z) { return terrain->GetHeight(x, z); });
		m_Tribe.Config.Camp = camp;
		m_Tribe.Config.CampY = world.TerrainRef->GetHeight(camp.x, camp.y);
		if (setup.Store.empty() || !m_Tribe.Store().FromJson(setup.Store))
		{
			const int n = static_cast<int>(roster.size());
			m_Tribe.Store().Add(m_Items, "wood", 2 + 2 * n);   // what they found lying around on the first day (as `tribe build`)
			m_Tribe.Store().Add(m_Items, "stone", 3 * n);
		}
		for (size_t i = 0; i < roster.size(); ++i) m_Tribe.AddMember(roster[i].first, roster[i].second, m_Bodies[i].get());
		// H9: a newborn gets a stand-in body like the founders' (its ability system, its place in the bags' index)
		m_Tribe.SetSpawner([this](const std::string& name, const glm::vec2& at, float) -> ITribeBody*
		{
			AbilitySystemComponent* asc = m_Gameplay.Create(name);
			for (const char* work : { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "Build" }) asc->Grant(work);
			m_Bodies.push_back(std::make_unique<SimBody>(name, m_Gameplay, m_Entities, m_Nav, m_Items, at));
			m_Index[name] = m_Bodies.size() - 1;
			return m_Bodies.back().get();
		});
		if (setup.Abstract) m_Tribe.SetViewer(camp + glm::vec2(1e5f, 1e5f));   // nobody near the viewer: every member is an abstract agent
		if (setup.ViewerAtCamp) m_Tribe.SetViewer(camp);

		// H16: more clans (the scale tests). The first is the tribe above; each further one settles where the clan world says, with its own well,
		// store and stand-in bodies, and its members are named on from the first clan's (a name is unique in the world).
		if (setup.Clans > 1)
		{
			m_Clans.SetRegions(setup.Regions ? setup.Regions : &m_Regions);
			m_Clans.Add("Clan 1", &m_Tribe);
			if (setup.ThinkPerFrame >= 0) m_Clans.ThinkPerFrame = setup.ThinkPerFrame;
			size_t named = roster.size();
			const int each = std::max(1, setup.Members / setup.Clans);
			const NavGrid* grid = &m_Nav;
			for (int c = 1; c < setup.Clans; ++c)
			{
				CampSite site;
				if (!m_Clans.ChooseHome(setup.ClanApart, [grid](const glm::vec2& p) { return grid->Walkable(p); }, site))
				{
					m_Error = "no home for clan " + std::to_string(c + 1) + " at least " + Fixed(setup.ClanApart, 0) + " m from the others";
					return;
				}
				const std::string name = "Clan " + std::to_string(c + 1);
				Clan& clan = m_Clans.Add(name, nullptr, -1, &m_Tribe.Config);
				Tribe& t = *clan.Folk;
				const glm::vec2 home = FindFreeSpot(m_Ground, site.At, 2.0f);
				const glm::vec2 well = FindFreeSpot(m_Ground, home + glm::vec2(4.0f, -6.0f), 1.5f);
				BuildInteractive(m_Entities, m_Rules.Defs.at("well"), "Well " + name, NullEntity, { well.x, terrain->GetHeight(well.x, well.y), well.y });
				t.Bind(m_Entities, m_Gameplay, m_Items, m_Recipes, m_Defs);
				t.SetInteractives(&m_Rules.Defs);
				t.SetTechs(&m_Techs);
				if (setup.Buildings) { t.SetBuildings(&m_Builds.Defs); t.SetGround([terrain](float x, float z) { return terrain->GetHeight(x, z); }); }   // (H8, as the first clan)
				t.SetRegions(setup.Regions ? setup.Regions : &m_Regions, [terrain](float x, float z) { return terrain->GetHeight(x, z); });
				t.Config.Camp = home;
				t.Config.CampY = terrain->GetHeight(home.x, home.y);
				t.Store().Add(m_Items, "wood", 2 + 2 * each);
				t.Store().Add(m_Items, "stone", 3 * each);
				const std::vector<std::string> roles = t.PlanRoles(each);
				for (int i = 0; i < each; ++i)
				{
					const std::string who = MemberName(t.Config.Work, named++);
					AbilitySystemComponent* asc = m_Gameplay.Create(who);
					for (const char* work : { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "Build" }) asc->Grant(work);
					const size_t col = static_cast<size_t>(i) % 8, row = static_cast<size_t>(i) / 8;
					glm::vec2 start = home + glm::vec2((static_cast<float>(col) - 3.5f) * 1.5f, 1.5f * static_cast<float>(row));
					if (!m_Nav.Walkable(start)) start = FindFreeSpot(m_Ground, start, 0.6f);
					m_Bodies.push_back(std::make_unique<SimBody>(who, m_Gameplay, m_Entities, m_Nav, m_Items, start));
					m_Index[who] = m_Bodies.size() - 1;
					t.AddMember(who, static_cast<size_t>(i) < roles.size() ? roles[static_cast<size_t>(i)] : std::string("forager"), m_Bodies.back().get());
				}
				t.SetSpawner([this](const std::string& who, const glm::vec2& at, float) -> ITribeBody*
				{
					AbilitySystemComponent* asc = m_Gameplay.Create(who);
					for (const char* work : { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "Build" }) asc->Grant(work);
					m_Bodies.push_back(std::make_unique<SimBody>(who, m_Gameplay, m_Entities, m_Nav, m_Items, at));
					m_Index[who] = m_Bodies.size() - 1;
					return m_Bodies.back().get();
				});
				if (setup.Abstract) t.SetViewer(camp + glm::vec2(1e5f, 1e5f));
				if (setup.ViewerAtCamp) t.SetViewer(camp);
			}
		}
		// H14: one combat system for every clan of the run (as the editor's StepCombat): threats, fights and raids, with the hostility of the clans
		if (setup.Combat)
		{
			m_Combat.Bind(m_Entities, m_Gameplay);
			m_Hostility.FromRules(m_Combat.Rules);
			IHostility* hostility = &m_Hostility;
			if (m_Clans.Count() > 1) { m_ClanHostility.Bind(&m_Clans, &m_Hostility); hostility = &m_ClanHostility; }
			const auto& all = m_Clans.All();
			if (all.empty()) m_Tribe.SetCombat(&m_Combat, hostility);
			for (const auto& c : all) { c->Folk->SetClanName(c->Name); c->Folk->SetCombat(&m_Combat, hostility); }
			if (setup.HostileClans)
				for (size_t a = 0; a < all.size(); ++a)
					for (size_t b = a + 1; b < all.size(); ++b) { m_Hostility.Set(all[a]->Name, all[b]->Name, 1.0f); m_Clans.Relate(all[a]->Id, all[b]->Id, "raid", 3.0f, "the scale run starts them hostile", all[a]->Id); }
		}
	}

	SimRun::~SimRun() = default;

	SimView SimRun::View() const
	{
		SimView v;
		v.Clan = &m_Tribe;
		if (m_Clans.Count() > 1) v.Clans = &m_Clans;   // (H16: the "clans" and "territory" checks of a run with several)
		v.Gameplay = &m_Gameplay;
		v.Entities = &m_Entities;
		v.Clock = m_Clock;
		const CharacterWorld* ground = &m_Ground;
		v.Ground = [ground](const glm::vec2& p) { return SimGroundAt(*ground, p); };
		return v;
	}

	// One step, in the order the editor runs its fixed tick (EditorSaveHost::Step): abilities, regrowth, bodies, the interactives, the tribe.
	void SimRun::Frame()
	{
		using Clock = std::chrono::steady_clock;
		const auto t0 = Clock::now();
		const float dt = m_Setup.Step;
		m_Clock += static_cast<double>(dt);
		if (m_Setup.PlanBudget > 0) m_Nav.NewFrame();   // H16: the searches of this step are shared fairly by every body (as ActorWorld::Update does)
		m_Gameplay.Update(dt);
		m_Resources.Update(m_Gameplay, dt, SeasonClock::GrowthRate(m_Tribe.Config.Seasons.At(m_Clock)));
		const auto t1 = Clock::now();
		for (auto& b : m_Bodies) b->Update(dt);
		const auto t2 = Clock::now();
		m_Resources.SyncVisuals(m_Gameplay, m_Entities);
		m_Rules.Update(dt, &m_Entities);
		if (m_Setup.Buildings) m_Builds.Update(dt, &m_Entities);   // H8
		if (m_Setup.Combat)   // H14: the fights of every clan, before their brains read them (as the editor)
		{
			const auto c0 = Clock::now();
			m_Combat.Update(dt, m_Tribe.Config.Needs.DaySeconds);
			m_Cost.CombatMs = std::chrono::duration<double, std::milli>(Clock::now() - c0).count();
		}
		if (m_Clans.Count() > 1)
		{
			m_Clans.SetWorldClock(m_Clock);
			m_Clans.Update(dt);
			m_Cost.TribesMs = m_Clans.LastCost().TribesMs;
			m_Cost.ThinkMs = m_Clans.LastCost().ThinkMs;
			m_Cost.DiplomacyMs = m_Clans.LastCost().DiplomacyMs;
			m_Cost.Thinks = m_Clans.LastCost().Thinks;
			m_Cost.Due = m_Clans.LastCost().Due;
		}
		else
		{
			m_Tribe.SetWorldClock(m_Clock);
			m_Tribe.Update(dt);
			m_Cost.TribesMs = m_Tribe.LastCost().Ms;
			m_Cost.ThinkMs = m_Tribe.LastCost().ThinkMs;
			m_Cost.DiplomacyMs = 0.0;
			m_Cost.Thinks = m_Tribe.LastCost().Thinks;
			m_Cost.Due = m_Tribe.LastCost().Due;
		}
		m_Cost.BodiesMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
		m_Cost.Plans = m_Nav.FrameStats();
		m_Cost.Ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
		++m_Frames;
		const SimView v = View();
		if (m_Frames % std::max(1, m_Setup.CheckEvery) == 0) m_Monitor.Step(v);
		m_Monitor.Sample(v);
	}

	void SimRun::Run(double seconds)
	{
		if (!Ok()) return;
		const long frames = static_cast<long>(seconds / static_cast<double>(m_Setup.Step) + 0.5);
		for (long i = 0; i < frames; ++i) Frame();
	}

	std::string SimRun::Fingerprint() const
	{
		std::string f = m_Tribe.ToJson() + "\n" + m_Tribe.Store().ToJson();
		for (const auto& b : m_Bodies)
		{
			const glm::vec2 p = b->Position();
			f += "\n" + std::to_string(p.x) + "," + std::to_string(p.y) + " " + b->Carried().ToJson();
		}
		return f + "\n" + m_Monitor.Metrics().Line() + "\n" + m_Monitor.Describe(1000);
	}
}
