// H14: the tribe's side of conflict, combat and violence. The fights themselves are the CombatSystem's (GB/Gameplay/Combat.h: blows, wounds,
// morale, the animals); here the clan puts its members in them (their place, genes, personality and weapon), sees and hears threats, decides
// for each member whether to fight or to flee (utility from its boldness, health, the odds, its kin and the camp), steers it with orders (which
// walk with navigation), rests and tends its wounded, hunts for meat, and raids or is raided through IHostility (H13 plugs the real attitudes
// of the clans in). Tribe.cpp only calls these hooks: StepConflict in Update, ConflictTurn in a member's turn, ConflictOptions in Decide,
// ExecuteConflict and FinishConflict, PostConflictJobs and ConflictJobScore on the job board. Deterministic like the rest of the tribe.
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		constexpr float kNearCamp = 5.0f;   // (Tribe.cpp's: within this of the camp a member is "at the camp")
		constexpr float kScan = 0.5f;       // seconds between two refreshes of the stats in the fights and of the animals near

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

		float Value(const std::map<std::string, float>& values, const char* key, float fallback = 0.5f)
		{
			const auto it = values.find(key);
			return it == values.end() ? fallback : it->second;
		}

		glm::vec2 Rotate(const glm::vec2& v, float radians)
		{
			const float c = std::cos(radians), s = std::sin(radians);
			return { v.x * c - v.y * s, v.x * s + v.y * c };
		}
	}

	// ------------------------------------------------------------------ the clock
	// A member in the fights: its side, the stats its genes and personality give, the best weapon in its bag, whether it is a child.
	void Tribe::JoinFight(TribeMember& m)
	{
		const bool fresh = !m_Combat->Find(m.Name);
		Combatant* c = m_Combat->Join(m.Name, m_ClanName);
		if (!c) return;
		const Person* p = m_Society.Find(m.Name);
		static const std::map<std::string, float> kNone;
		const std::map<std::string, float>& genes = p ? p->Genes : kNone;
		const std::map<std::string, float>& traits = p ? p->Traits : kNone;
		const float growth = m_Society.Growth(Config.Society, m.Name);   // a child hits as hard as it is grown
		c->Strength = Value(genes, "strength") * growth;
		c->Endurance = Value(genes, "endurance") * (0.5f + 0.5f * growth);
		c->Dexterity = Value(genes, "dexterity");
		c->Boldness = Value(traits, "boldness");
		c->Child = IsChild(m);
		float power = 0.0f;
		c->Weapon = BestWeapon(m_Combat->Rules, *m_Items, m.Body->Bag(), &power);
		c->Power = power;
		if (fresh) c->Morale = std::clamp(m_Combat->Rules.Morale.Start + m_Combat->Rules.Morale.Boldness * c->Boldness, 0.0f, 1.0f);
	}

	void Tribe::StepConflict(float dt)
	{
		if (!m_Combat || !m_Gameplay || !m_Items) return;
		CombatSystem& cs = *m_Combat;
		m_Conflict.Timer += dt;
		const bool scan = m_Conflict.Timer >= kScan;
		if (scan) m_Conflict.Timer = 0.0f;
		// H16: the members tending someone, gathered once (it was members x members every frame: 3.600 tests a frame for a clan of 60)
		std::vector<std::pair<const std::string*, glm::vec2>> carers;
		for (const TribeMember& o : m_Members)
			if (o.Job == "care" && !IsBusy(o)) carers.emplace_back(&o.Detail, Where(o));
		for (TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			if (scan || !cs.Find(m.Name)) JoinFight(m);
			Combatant* c = cs.Find(m.Name);
			if (!c) continue;
			cs.Place(m.Name, Where(m));
			c->Resting = m.Job == "sleep" || m.Job == "recover";
			c->Heal = HealingAt(Where(m));   // a healer's hut near it (H8)
			const auto mc = m_Conflict.Members.find(m.Name);
			c->Cared = mc != m_Conflict.Members.end() && mc->second.CareUntil > m_Time;
			if (!c->Cared)   // someone at its side tending it right now
				for (const auto& [detail, at] : carers)
					if (*detail == m.Name && glm::distance(at, Where(m)) < 3.0f) c->Cared = true;
			// attacked and not answering yet: it looks up now, not at its next turn
			if (c->Mode == FightMode::None && !cs.AttackersOf(m.Name).empty()) m.Mind.NextThink = std::min(m.Mind.NextThink, m_Time);
		}
		if (scan)
		{
			// the animals of the rules (prey to hunt, predators to fear) and the clan's camp for the raids of others
			m_Animals.clear();
			for (const std::string& name : m_Gameplay->Names())
			{
				const AbilitySystemComponent* asc = m_Gameplay->Find(name);
				if (!asc || !asc->HasTag("Animal") || ::GB::IsDead(*asc)) continue;
				const PredatorRule* p = cs.Rules.PredatorFor(*asc);
				const HuntRule* h = p ? nullptr : cs.Rules.HuntFor(*asc);
				if (!p && !h) continue;
				const EntityId id = m_Entities->FindByName(name);
				if (id == NullEntity) continue;
				const glm::vec4 at = m_Entities->WorldMatrix(id)[3];
				m_Animals.push_back({ name, p ? p->Tag : h->Tag, { at.x, at.z }, p != nullptr });
			}
			if (m_Hostility) m_Hostility->Settled(m_ClanName, Config.Camp, &m_Store, Defense());
			// strangers of a hostile clan at the store, not fighting: a raid (the clan notes it once, for its grievances and its culture)
			for (const auto& [name, f] : cs.All())
			{
				if (f.Animal || f.Side == m_ClanName || f.Mode != FightMode::None || glm::distance(f.Pos, Config.Camp) > cs.Rules.Raids.Reach + 1.0f) continue;
				if (m_Hostility && m_Hostility->Hostility(m_ClanName, f.Side) <= 0.0f && m_Hostility->Hostility(f.Side, m_ClanName) <= 0.0f) continue;
				double& seen = m_Conflict.RaidedBy[f.Side];
				if (seen > 0.0 && m_Time - seen < 300.0) continue;
				seen = std::max(m_Time, 0.001);
				++m_Conflict.Raided;
				TellClan("sees " + name + " of " + f.Side + " at its store: a raid");
				ClanEvent("raided");
			}
		}
		ConflictEvents();
		StepRaid(dt);
	}

	// What happened in the fights since the clan last looked: who of its own is attacked (it cries out, the others hear it), killed or wounded,
	// and what its hunters killed (the meat).
	void Tribe::ConflictEvents()
	{
		CombatSystem& cs = *m_Combat;
		auto member = [&](const std::string& name) -> TribeMember*
		{
			for (TribeMember& m : m_Members) if (m.Name == name) return &m;
			return nullptr;
		};
		for (const CombatEvent& e : cs.EventsSince(m_Conflict.Seq))
		{
			m_Conflict.Seq = e.Seq;
			TribeMember* who = member(e.Who);
			TribeMember* other = member(e.Other);
			if (e.Kind == "engage" && other && !other->Dead && !who)
			{
				// one of ours is attacked from outside: it cries out, and whoever hears it knows of the threat
				Shout(other->Name, Where(*other), cs.Rules.Threats.Alarm);
				Say(*other, "is attacked by " + e.Who);
				for (TribeMember& o : m_Members)
				{
					if (o.Dead || !o.Body || glm::distance(Where(o), Where(*other)) > cs.Rules.Threats.Alarm) continue;
					MemberConflict& mc = m_Conflict.Members[o.Name];
					mc.Threat = e.Who;
					mc.SeenAt = m_Time;
					o.Mind.NextThink = std::min(o.Mind.NextThink, m_Time);
				}
			}
			else if (e.Kind == "death" && other)
			{
				if (other->Dead) continue;
				++m_Conflict.Deaths;
				const Combatant* killer = cs.Find(e.Who);
				Say(*other, "is killed" + (e.Who.empty() ? std::string() : " by " + e.Who) + (e.Text == "Bleeding" ? " (it bled to death)" : ""));
				Die(*other, "Wounds");
				if (killer && m_Hostility && !killer->Animal && killer->Side != m_ClanName)
					m_Hostility->Wronged(m_ClanName, killer->Side, "killing", 2.0f * cs.Rules.Raids.Wrong);
			}
			else if (e.Kind == "death" && who && !other)
			{
				++m_Conflict.Kills;
				const HuntRule* h = nullptr;   // (by the side the event keeps: the ecology may have taken the body away already)
				for (const auto& [id, rule] : cs.Rules.Hunts) if (rule.Tag == e.Side) h = &rule;
				if (h && who->Body)
				{
					const int got = m_Items->Find(h->Item) ? who->Body->Bag().Add(*m_Items, h->Item, h->Units) : 0;
					const int also = !h->Byproduct.empty() && m_Items->Find(h->Byproduct) ? who->Body->Bag().Add(*m_Items, h->Byproduct, h->ByproductUnits) : 0;
					std::string yields;   // the butchering (social lane: bone, sinew, fat, feathers...)
					for (const auto& [item, n] : h->Yields)
						if (m_Items->Find(item)) if (const int k = who->Body->Bag().Add(*m_Items, item, n); k > 0) yields += ", " + std::to_string(k) + " " + item;
					++m_Conflict.Hunted[h->Tag];
					Say(*who, "kills " + e.Other + ": " + std::to_string(got) + " " + h->Item + (also > 0 ? " and " + std::to_string(also) + " " + h->Byproduct : std::string()) + yields);
					Hunted(*who, *h, e.Other);   // social lane: the first kill (a rite), the shared kill (a taboo), a great kill (an omen) (TribeSocial.cpp)
					if (const Job* held = m_Board.ClaimOf(who->Name); held && held->Kind == "hunt") m_Board.Progress(who->Name, static_cast<float>(got), m_Time);
				}
				else Say(*who, "kills " + e.Other);
			}
			else if (e.Kind == "wound" && other && !other->Dead) Say(*other, "is wounded (" + e.Text + (e.Who.empty() ? std::string() : ", by " + e.Who) + ")");
		}
	}

	// ------------------------------------------------------------------ seeing a threat, fighting or fleeing
	// The worth of the member's side near it over the worth of the threat's side near the threat (CombatSystem::Strength).
	float Tribe::Odds(const std::string& member, const std::string& threat) const
	{
		const CombatSystem& cs = *m_Combat;
		const Combatant* me = cs.Find(member);
		const Combatant* foe = cs.Find(threat);
		if (!me) return 1.0f;
		const float radius = cs.Rules.Threats.GroupRadius;
		float ours = 0.0f, theirs = 0.0f;
		for (const auto& [name, f] : cs.All())
		{
			if (f.Mode == FightMode::Dead || f.Mode == FightMode::Flee) continue;
			if (f.Side == me->Side && glm::distance(f.Pos, me->Pos) <= radius) ours += cs.Strength(name);
			else if (foe && f.Side == foe->Side && glm::distance(f.Pos, foe->Pos) <= radius) theirs += cs.Strength(name);
		}
		if (!foe)
		{
			// a predator that has not joined a fight: as strong as its rule makes it
			for (const Animal& a : m_Animals)
				if (a.Name == threat)
					for (const auto& [id, p] : cs.Rules.Predators)
						if (p.Tag == a.Tag) { const WeaponDef& w = cs.Rules.Weapon(p.Weapon); theirs = p.Health / 100.0f * w.Damage / w.Seconds; }
		}
		return ours / std::max(theirs, 0.01f);
	}

	// What it sees and hears that is a threat: its safety drops and it remembers it; the one it must answer now (an attacker of its own, the foe of
	// a clanmate near it, a stranger of a hostile clan at the camp or close by, a predator a child meets), or "".
	std::string Tribe::SeeThreat(TribeMember& m)
	{
		CombatSystem& cs = *m_Combat;
		const ThreatRules& t = cs.Rules.Threats;
		const glm::vec2 pos = Where(m);
		MemberConflict& mc = m_Conflict.Members[m.Name];
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		const RaidState& raid = m_Conflict.Raid;
		const bool raiding = raid.Active() && std::find(raid.Party.begin(), raid.Party.end(), m.Name) != raid.Party.end();
		const bool child = IsChild(m);
		std::string answer;
		float best = 1e30f;
		auto notice = [&](const std::string& name, const glm::vec2& at)
		{
			if (mc.Threat != name || m_Time - mc.SeenAt > static_cast<double>(t.ForgetSeconds))
			{
				if (asc) SatisfyNeed(*asc, NeedName::Safety, -t.Safety);
				m.Mind.Memory.Observe({ name, "agent", at, -1, m_Time, false });
			}
			mc.Threat = name;
			mc.SeenAt = m_Time;
		};
		auto isOurs = [&](const std::string& name) { return std::any_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& o) { return o.Name == name && !o.Dead; }); };
		for (const std::string& a : cs.AttackersOf(m.Name))
		{
			const Combatant* f = cs.Find(a);
			if (!f) continue;
			notice(a, f->Pos);
			const float d = glm::distance(pos, f->Pos);
			if (d < best) { best = d; answer = a; }
		}
		if (answer.empty())
			for (const auto& [name, f] : cs.All())
			{
				if (name == m.Name || f.Mode == FightMode::Dead || f.Side == m_ClanName) continue;
				const float d = glm::distance(pos, f.Pos);
				if (d > std::max(t.Sight, t.Alarm)) continue;
				const bool engagedOurs = f.Mode == FightMode::Engage && isOurs(f.Target);
				const float hostility = m_Hostility ? std::max(m_Hostility->Hostility(f.Side, m_ClanName), m_Hostility->Hostility(m_ClanName, f.Side)) : 0.0f;
				if (!engagedOurs && hostility < t.Hostile) continue;
				const bool heard = mc.Threat == name && m_Time - mc.SeenAt < 3.0;   // a clanmate's cry about it
				const bool seen = d <= t.Sight && (d <= Config.Vision.NearRange || CanSeeIn(Config.Vision, pos, m.Mind.Facing, f.Pos, m_OccGrid, name));
				if (!seen && !heard) continue;
				notice(name, f.Pos);
				bool must = false;
				if (engagedOurs) must = d < t.Alarm * 0.5f;   // a clanmate is attacked near it: it answers (to help or to run)
				else if (!f.Animal && !raiding) must = glm::distance(f.Pos, Config.Camp) < kNearCamp * 2.0f || (hostility >= 0.9f && d < t.GroupRadius);
				if (must && d < best) { best = d; answer = name; }
			}
		// a predator that is not attacking anyone yet: fear, and a child runs from one close by
		for (const Animal& a : m_Animals)
		{
			if (!a.Predator || cs.InFight(a.Name)) continue;
			const float d = glm::distance(pos, a.Pos);
			if (d > Config.Vision.NearRange * 3.0f) continue;
			notice(a.Name, a.Pos);
			if (child && answer.empty() && d < Config.Vision.NearRange * 2.0f) { answer = a.Name; best = d; }
		}
		if (!answer.empty() && m_Time - mc.AlarmedAt > static_cast<double>(t.ForgetSeconds))
		{
			mc.AlarmedAt = m_Time;
			Shout(m.Name, pos, t.Alarm);   // the alarm cry (the others hear it: GB/AI's "scream")
		}
		return answer;
	}

	void Tribe::LeaveFight(TribeMember& m, const std::string& why)
	{
		m_Combat->Release(m.Name);
		MemberConflict& mc = m_Conflict.Members[m.Name];
		mc.Ordered = false;
		mc.Threat.clear();
		m.Job.clear();
		m.Detail.clear();
		std::string error;
		if (Run(m, "wait 0.2", error)) m.Mind.Orders = "wait 0.2";
		if (!why.empty()) Say(m, why);
	}

	// A member in a fight or a flight: orders that bring it within reach of its foe (the combat swings for it) or take it away from the threat
	// (home when the camp is the other way, else straight away; round what it cannot cross). False when it is out of it.
	bool Tribe::SteerFight(TribeMember& m)
	{
		CombatSystem& cs = *m_Combat;
		Combatant* c = cs.Find(m.Name);
		if (!c) return false;
		const ThreatRules& t = cs.Rules.Threats;
		MemberConflict& mc = m_Conflict.Members[m.Name];
		const glm::vec2 pos = Where(m);
		auto locate = [&](const std::string& name, glm::vec2& at)
		{
			if (const Combatant* f = cs.Find(name); f && f->Mode != FightMode::Dead) { at = f->Pos; return true; }
			for (const Animal& a : m_Animals) if (a.Name == name) { at = a.Pos; return true; }
			return false;
		};
		const std::string target = c->Target;
		glm::vec2 at;
		if (c->Mode == FightMode::Engage)
		{
			if (!locate(target, at) || cs.IsDead(target)) { LeaveFight(m, std::string()); return false; }
			const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
			const float health = asc ? asc->Attribute(NeedName::Health) : 100.0f;
			if (c->Morale < cs.Rules.Morale.FleeBelow || (health < cs.Rules.Morale.FleeHealth && c->Boldness < 0.8f))
			{
				cs.Flee(m.Name, target);
				++m_Conflict.Flights;
				mc.Ordered = false;
				char b[96];
				std::snprintf(b, sizeof b, "breaks and flees from %s (morale %.2f, health %.0f)", target.c_str(), static_cast<double>(c->Morale), static_cast<double>(health));
				Say(m, b);
				return SteerFight(m);
			}
			const float d = glm::distance(pos, at);
			if (d > std::max(t.Sight, 2.0f * t.SafeDistance)) { LeaveFight(m, "loses " + target); return false; }
			if (m.Job != "hunt") m.Job = "fight";
			m.Detail = target;
			const float reach = cs.Rules.Weapon(c->Weapon).Reach;
			if (d > reach * 0.9f)
			{
				const glm::vec2 away = d > 1e-3f ? (pos - at) / d : glm::vec2(1.0f, 0.0f);
				const glm::vec2 goal = at + away * reach * 0.6f;
				if (!mc.Ordered || !IsBusy(m) || glm::distance(goal, mc.Goal) > 1.0f)
				{
					if (Order(m, GoTo(goal, "wait 0.2"))) { mc.Goal = goal; mc.Ordered = true; }
				}
			}
			else if (mc.Ordered || !IsBusy(m))
			{
				if (Order(m, "wait 0.5")) mc.Ordered = false;   // within reach: it stands and swings
			}
			return true;
		}
		if (c->Mode != FightMode::Flee) return false;
		const bool known = !target.empty() && locate(target, at) && !cs.IsDead(target);
		const float d = known ? glm::distance(pos, at) : 1e30f;
		const bool chased = !cs.AttackersOf(m.Name).empty();
		if (!known || d > t.SafeDistance || (!IsBusy(m) && mc.Ordered && !chased && d > t.GroupRadius))
		{
			LeaveFight(m, known ? "is safe from " + target : std::string());
			return false;
		}
		m.Job = "flee";
		m.Detail = target;
		if (!mc.Ordered || !IsBusy(m))
		{
			// home if the camp is away from the threat, else straight away from it (turning when that way cannot be walked)
			glm::vec2 goal = CampSpot(m);
			const bool homeIsSafe = glm::distance(Config.Camp, at) > d + 2.0f && glm::distance(Config.Camp, at) > t.GroupRadius;
			if (!homeIsSafe || glm::distance(pos, Config.Camp) < kNearCamp)
			{
				const glm::vec2 away = d > 1e-3f ? (pos - at) / d : glm::vec2(1.0f, 0.0f);
				goal = pos + away * t.FleeDistance;
				for (const float turn : { 0.0f, 0.8f, -0.8f, 1.6f, -1.6f })
				{
					const glm::vec2 g = pos + Rotate(away, turn) * t.FleeDistance;
					if (m.Body->CanReach(g)) { goal = g; break; }
				}
			}
			if (Order(m, GoTo(goal, "wait 0.5"))) { mc.Goal = goal; mc.Ordered = true; }
		}
		return true;
	}

	// A member's turn while conflict has a say: a fight or a flight it is in, a raid it is on, or a threat it must answer now.
	bool Tribe::ConflictTurn(TribeMember& m)
	{
		if (!m_Combat || m.Dead || !m.Body) return false;
		CombatSystem& cs = *m_Combat;
		Combatant* c = cs.Find(m.Name);
		if (!c || c->Mode == FightMode::Dead) return false;
		const std::string threat = SeeThreat(m);
		if (c->Mode == FightMode::Engage || c->Mode == FightMode::Flee)
		{
			if (SteerFight(m)) return true;
			if (threat.empty()) return false;   // (out of the fight: it may face another threat at once)
		}
		MemberConflict& mc = m_Conflict.Members[m.Name];
		if (threat.empty())
		{
			// a raid it is on
			RaidState& raid = m_Conflict.Raid;
			if (!raid.Active() || std::find(raid.Party.begin(), raid.Party.end(), m.Name) == raid.Party.end() || raid.Home[m.Name]) return false;
			const glm::vec2 pos = Where(m);
			const RaidRules& rules = cs.Rules.Raids;
			m.Job = "raid";
			m.Detail = raid.Target;
			const bool took = raid.Took.count(m.Name) > 0;
			if (raid.Phase == "retreat" || took)
			{
				if (glm::distance(pos, Config.Camp) > kNearCamp)
				{
					if (!mc.Ordered || !IsBusy(m)) { const glm::vec2 home = CampSpot(m); if (Order(m, GoTo(home, "wait 0.5"))) { mc.Goal = home; mc.Ordered = true; } }
					return true;
				}
				// home: what it took goes into the store
				int brought = 0;
				if (const auto t = raid.Took.find(m.Name); t != raid.Took.end())
					for (const auto& [item, units] : t->second)
					{
						const int moved = m.Body->Bag().Transfer(*m_Items, m_Store, item, units);
						raid.Loot[item] += moved;
						brought += moved;
					}
				raid.Stolen += brought;
				raid.Home[m.Name] = true;
				mc.Ordered = false;
				m.Job.clear();
				m.Detail.clear();
				Say(m, "is back from the raid on " + raid.Target + (brought > 0 ? " with " + std::to_string(brought) + " things for the store" : std::string(" empty-handed")));
				return false;
			}
			const size_t index = static_cast<size_t>(std::find(raid.Party.begin(), raid.Party.end(), m.Name) - raid.Party.begin());
			const glm::vec2 spot = raid.TargetCamp + Rotate(glm::vec2(rules.Reach * 0.5f, 0.0f), static_cast<float>(index) * 1.3f);
			if (glm::distance(pos, raid.TargetCamp) > rules.Reach)
			{
				if (!mc.Ordered || !IsBusy(m)) { if (Order(m, GoTo(spot, "wait 0.3"))) { mc.Goal = spot; mc.Ordered = true; } }
				return true;
			}
			if (IsBusy(m) && mc.Ordered) return true;
			// at the store: it takes what it can carry, food first
			Inventory* store = nullptr;
			if (m_Hostility)
				for (const RaidTarget& rt : m_Hostility->RaidTargets(m_ClanName)) if (rt.Clan == raid.Target) store = rt.Store;
			std::map<std::string, int>& took2 = raid.Took[m.Name];
			int left = rules.Carry;
			if (store)
			{
				std::vector<std::string> order;
				for (const std::string& tag : rules.Steal)
					for (const ItemSlot& s : store->Stacks())
						if (const ItemDef* d = m_Items->Find(s.Id); d && std::any_of(d->Tags.begin(), d->Tags.end(), [&](const std::string& x) { return x == tag || x.rfind(tag + ".", 0) == 0; })
							&& std::find(order.begin(), order.end(), s.Id) == order.end()) order.push_back(s.Id);
				for (const ItemSlot& s : store->Stacks()) if (std::find(order.begin(), order.end(), s.Id) == order.end()) order.push_back(s.Id);
				for (const std::string& item : order)
				{
					if (left <= 0) break;
					const int moved = store->Transfer(*m_Items, m.Body->Bag(), item, left);
					if (moved > 0) { took2[item] += moved; left -= moved; }
				}
			}
			const int n = rules.Carry - left;
			if (raid.Phase == "march")
			{
				raid.Phase = "steal";
				if (m_Hostility) m_Hostility->Wronged(raid.Target, m_ClanName, "raid", rules.Wrong);
			}
			Say(m, n > 0 ? "takes " + std::to_string(n) + " things from the store of " + raid.Target : "finds nothing to take in the store of " + raid.Target);
			if (Order(m, WaitFor(rules.StealSeconds))) mc.Ordered = true;
			return true;
		}

		// a threat it must answer: fight or flee
		const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		ThreatView view;
		view.Boldness = c->Boldness;
		view.Health = asc ? asc->Attribute(NeedName::Health) : 100.0f;
		view.Safety = asc && asc->Attributes().Has(NeedName::Safety) ? asc->Attribute(NeedName::Safety) : 100.0f;
		view.Odds = Odds(m.Name, threat);
		view.Child = IsChild(m);
		glm::vec2 at = Where(m);
		if (const Combatant* f = cs.Find(threat))
		{
			at = f->Pos;
			if (f->Mode == FightMode::Engage && f->Target != m.Name)
			{
				const std::string kin = m_Society.Kin(m.Name, f->Target);
				const bool childOfClan = std::any_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& o) { return o.Name == f->Target && IsChild(o); });
				view.Kin = !kin.empty() || childOfClan;
			}
		}
		else for (const Animal& a : m_Animals) if (a.Name == threat) at = a.Pos;
		view.Camp = glm::distance(at, Config.Camp) < kNearCamp * 2.0f;
		view.Defense = Defense();
		view.FightCulture = m_Society.OptionFactor(Config.Society, m.Name, "fight", "").Total();
		view.FleeCulture = m_Society.OptionFactor(Config.Society, m.Name, "flee", "").Total();
		const FightChoice choice = FightOrFlight(cs.Rules.Threats, cs.Rules.Morale, view);
		if (m.Job == "gather") m_Claims.erase(m.Detail);   // it drops what it was doing
		m.Job.clear();
		mc.Ordered = false;
		if (choice.Fights() && cs.Find(threat) && cs.Engage(m.Name, threat))
		{
			++m_Conflict.Fights;
			Say(m, "stands up to " + threat + ": " + choice.Why());
		}
		else
		{
			cs.Flee(m.Name, threat);
			++m_Conflict.Flights;
			Say(m, "flees from " + threat + ": " + choice.Why());
		}
		return SteerFight(m);
	}

	// ------------------------------------------------------------------ hunting, resting and caring
	void Tribe::PostConflictJobs()
	{
		if (!m_Combat || m_Combat->Rules.Hunts.empty()) return;
		int alive = 0, food = FoodIn(m_Store);
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			++alive;
			food += FoodIn(m.Body->Bag());
		}
		const float want = static_cast<float>(std::max(1, alive)) * 3.0f;   // three meals a member in hand: below it, meat is worth going after
		const float short_ = std::clamp(1.0f - static_cast<float>(food) / want, 0.0f, 1.0f);
		if (short_ <= 0.0f) return;
		const JobRules& rules = Config.Work.Jobs;
		for (const auto& [id, h] : m_Combat->Rules.Hunts)
		{
			const Animal* nearest = nullptr;
			float best = h.Range;
			for (const Animal& a : m_Animals)
				if (a.Tag == h.Tag && !a.Predator && HuntAllowed(a.Pos)) { const float d = glm::distance(a.Pos, Config.Camp); if (d < best) { best = d; nearest = &a; } }   // (not where a taboo forbids it)
			if (!nearest) continue;
			char why[128];
			std::snprintf(why, sizeof why, "%d food for %d; %s %.0f m from the camp", food, alive, nearest->Name.c_str(), static_cast<double>(best));
			m_Board.Post("hunt", h.Tag, "hunting", 1.0f, h.Priority * (0.5f + 2.0f * short_), h.Hunters, why, m_Time, rules);
		}
	}

	float Tribe::ConflictJobScore(const TribeMember& m, const Job& job) const
	{
		if (!m_Combat || job.Kind != "hunt" || !m.Body || IsChild(m)) return 0.0f;
		const HuntRule* rule = nullptr;
		for (const auto& [id, h] : m_Combat->Rules.Hunts) if (h.Tag == job.Target) rule = &h;
		if (!rule || (!rule->ToolTag.empty() && m.Body->Bag().CountTagged(*m_Items, rule->ToolTag) == 0)) return 0.0f;
		const glm::vec2 pos = Where(m);
		float best = 1e30f;
		for (const Animal& a : m_Animals) if (a.Tag == job.Target) best = std::min(best, glm::distance(pos, a.Pos));
		if (best > 1e29f) return 0.0f;
		const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		const float vigor = asc && asc->Attributes().Has(NeedName::Vigor) ? asc->Attribute(NeedName::Vigor) : 1.0f;
		const Combatant* c = m_Combat->Find(m.Name);
		const float boldness = c ? c->Boldness : 0.5f;
		return job.Priority * Falloff(best, 40.0f) * vigor * (0.6f + 0.8f * boldness);
	}

	void Tribe::ConflictOptions(TribeMember& m, std::vector<UtilityOption>& options)
	{
		if (!m_Combat) return;
		const CombatSystem& cs = *m_Combat;
		const glm::vec2 pos = Where(m);
		const Combatant* c = cs.Find(m.Name);
		const InjuryRules& in = cs.Rules.Injuries;
		char b[128];
		// the hunt it holds: the nearest animal of the job's kind
		if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "hunt" && ConflictJobScore(m, *held) > 0.0f)
		{
			// a group hunt (HuntRule::Group) waits until enough hunters hold the job; the same animal for all of them
			const HuntRule* rule = nullptr;
			for (const auto& [id, h] : cs.Rules.Hunts) if (h.Tag == held->Target) rule = &h;
			const int group = rule ? rule->Group : 1;
			std::string chased;   // (in a group hunt: the animal the others already chase)
			for (const TribeMember& o : m_Members)
				if (group > 1 && &o != &m && o.Job == "hunt" && !o.Dead)
					if (const AbilitySystemComponent* a = m_Gameplay->Find(o.Detail); a && a->HasTag(held->Target) && !::GB::IsDead(*a)) chased = o.Detail;
			const bool enough = static_cast<int>(held->Claims.size()) >= group || !chased.empty();
			const Animal* prey = nullptr;
			float best = 1e30f;
			for (const Animal& a : m_Animals)
				if (enough && a.Tag == held->Target && (!cs.InFight(a.Name) || a.Name == chased) && HuntAllowed(a.Pos) && (chased.empty() || a.Name == chased))
					{ const float d = glm::distance(pos, a.Pos); if (d < best) { best = d; prey = &a; } }
			if (prey)
			{
				UtilityOption o;
				o.Kind = "hunt";
				o.Target = prey->Name;
				o.Where = prey->Pos;
				const float reach = Falloff(best, 60.0f);
				o.Score = 0.9f * reach * std::max(1.0f, held->Priority);   // (the board's priority carries: a clan short of food hunts before it tops up stock)
				o.Commit = 5.0f;
				o.Factors.push_back({ "hunt", 0.9f });
				o.Factors.push_back({ "distance", reach });
				o.Note = held->Why;
				options.push_back(o);
			}
		}
		// its wounds: rest at the camp (they heal faster)
		const float severity = c ? c->Severity() : 0.0f;
		if (severity > in.RestAbove)
		{
			UtilityOption o;
			o.Kind = "recover";
			o.Where = RecoverSpot(m);   // the healer's hut when there is one (H8), the camp otherwise
			const float u = std::min(1.0f, severity / std::max(0.1f, in.Lethal * 0.5f));
			o.Score = 1.4f * u + 0.3f;
			o.Commit = 20.0f;
			o.Factors.push_back({ "wounds", u });
			std::snprintf(b, sizeof b, "wounded (severity %.2f)", static_cast<double>(severity));
			o.Note = b;
			options.push_back(o);
		}
		// a wounded clanmate nobody tends: it goes to care for it (more for kin and friends)
		if (!IsChild(m) && severity <= in.RestAbove)
		{
			const TribeMember* who = nullptr;
			float bestScore = 0.0f;
			for (const TribeMember& o : m_Members)
			{
				if (&o == &m || o.Dead || !o.Body) continue;
				const Combatant* oc = cs.Find(o.Name);
				const float s = oc ? oc->Severity() : 0.0f;
				const bool bleeding = oc && std::any_of(oc->Wounds.begin(), oc->Wounds.end(), [](const Wound& w) { return w.Bleeding > 0.0f; });
				if (s <= in.RestAbove && !bleeding) continue;
				if (oc->Mode == FightMode::Engage || oc->Mode == FightMode::Flee) continue;
				const auto mc = m_Conflict.Members.find(o.Name);
				if (mc != m_Conflict.Members.end() && mc->second.CareUntil > m_Time) continue;
				if (std::any_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return &x != &m && x.Job == "care" && x.Detail == o.Name; })) continue;
				const float d = glm::distance(pos, Where(o));
				if (d > 50.0f) continue;
				const float score = std::min(1.0f, s + (bleeding ? 0.3f : 0.0f)) * (1.0f + 0.5f * m_Society.Affinity(Config.Society, m.Name, o.Name)) * Falloff(d, 40.0f);
				if (score > bestScore) { bestScore = score; who = &o; }
			}
			if (who)
			{
				UtilityOption o;
				o.Kind = "care";
				o.Target = who->Name;
				o.Where = Where(*who);
				o.Score = 1.1f * bestScore;
				o.Commit = 10.0f;
				o.Factors.push_back({ "wounded", bestScore });
				o.Note = who->Name + " is wounded";
				options.push_back(o);
			}
		}
	}

	bool Tribe::ExecuteConflict(TribeMember& m, const UtilityOption& o)
	{
		CombatSystem& cs = *m_Combat;
		if (o.Kind == "hunt")
		{
			if (!cs.JoinAnimal(o.Target)) return false;
			if (!cs.Engage(m.Name, o.Target)) return false;
			// a group hunt: the first to go drives the animal, the others wait for it with the spear
			const bool driver = std::none_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return &x != &m && x.Job == "hunt" && x.Detail == o.Target; });
			int group = 1;
			for (const auto& [id, h] : cs.Rules.Hunts) if (const AbilitySystemComponent* a = m_Gameplay->Find(o.Target); a && a->HasTag(h.Tag)) group = h.Group;
			m.Job = "hunt";
			m.Detail = o.Target;
			m_Conflict.Members[m.Name].Ordered = false;
			Say(m, group > 1 ? (driver ? "drives " + o.Target + " (a group hunt)" : "waits for " + o.Target + " with the spear (a group hunt)") : "goes hunting " + o.Target);
			if (group > 1 && driver)   // the others who hold the hunt come now (they leave what they were doing)
				for (TribeMember& x : m_Members)
				{
					if (&x == &m || x.Dead || !x.Body || x.Player || x.Job == "hunt" || x.Job == "fight" || x.Job == "flee") continue;
					const Job* held = m_Board.ClaimOf(x.Name);
					const AbilitySystemComponent* prey = m_Gameplay->Find(o.Target);
					if (!held || held->Kind != "hunt" || !prey || !prey->HasTag(held->Target)) continue;
					if (IsBusy(x) && x.Job != "gather" && x.Job != "explore" && !x.Job.empty() && x.Job != "rest") continue;   // (a scout after a food node leaves it for the hunt that feeds the clan)   // (it ends what it cannot drop first)
					if (IsBusy(x)) Order(x, "wait 0.1");
					x.Mind.NextThink = std::min(x.Mind.NextThink, m_Time);
				}
			SteerFight(m);
			return true;
		}
		if (o.Kind == "recover")
		{
			if (!Order(m, GoTo(o.Where, WaitFor(cs.Rules.Injuries.CareSeconds)))) return false;
			m.Job = "recover";
			m.Detail.clear();
			Say(m, HealingAt(o.Where) > 1.0f ? "rests its wounds at the healer's hut" : "rests its wounds at the camp");
			return true;
		}
		// care: beside the wounded one, for a while
		const glm::vec2 beside = o.Where + glm::vec2(0.8f, 0.0f);
		if (!Order(m, GoTo(beside, WaitFor(cs.Rules.Injuries.CareSeconds)))) return false;
		m.Job = "care";
		m.Detail = o.Target;
		Say(m, "goes to tend " + o.Target);
		return true;
	}

	bool Tribe::FinishConflict(TribeMember& m, const std::string& job)
	{
		if (job == "fight" || job == "flee" || job == "raid" || job == "hunt" || job == "recover") return true;
		if (job != "care") return false;
		for (const TribeMember& o : m_Members)
		{
			if (o.Name != m.Detail || o.Dead || !o.Body) continue;
			if (glm::distance(Where(o), Where(m)) > 3.0f) { Say(m, "could not reach " + o.Name + " to tend it"); break; }
			m_Conflict.Members[o.Name].CareUntil = m_Time + static_cast<double>(m_Combat ? m_Combat->Rules.Injuries.CareSeconds * 3.0f : 60.0f);
			++m_Conflict.Cares;
			Relate(m.Name, o.Name, "care");
			Say(m, "tends " + o.Name + "'s wounds");
			break;
		}
		m.Detail.clear();
		return true;
	}

	// ------------------------------------------------------------------ raids
	bool Tribe::StartRaid(const std::string& clan, std::string* why)
	{
		auto fail = [&](const std::string& w) { if (why) *why = w; return false; };
		if (!m_Combat || !m_Hostility) return fail("the clan has no combat or no hostility (SetCombat)");
		RaidState& raid = m_Conflict.Raid;
		if (raid.Active()) return fail("it is raiding " + raid.Target + " already");
		m_Hostility->Settled(m_ClanName, Config.Camp, &m_Store, Defense());
		const RaidTarget* target = nullptr;
		const std::vector<RaidTarget> targets = m_Hostility->RaidTargets(m_ClanName);
		for (const RaidTarget& t : targets) if (t.Clan == clan) target = &t;
		if (!target) return fail("no camp of " + clan + " to raid (it must be hostile and have told where it lives)");
		std::vector<RaiderCandidate> candidates;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body || m.Player) continue;   // (a player joins a raid by itself: H15)
			const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
			const Combatant* c = m_Combat->Find(m.Name);
			candidates.push_back({ m.Name, asc ? asc->Attribute(NeedName::Health) : 100.0f, c ? c->Boldness : 0.5f, c ? c->Strength : 0.5f, IsChild(m),
				m_Combat->InFight(m.Name) || m.Job == "expedition" || m.Job == "migrate" });
		}
		const std::vector<std::string> party = ChooseRaidParty(m_Combat->Rules.Raids, candidates);
		if (party.empty()) return fail("too few fit adults (" + std::to_string(m_Combat->Rules.Raids.Party) + " at least)");
		const RaidState before = raid;
		raid = RaidState();
		raid.Raids = before.Raids;
		raid.Stolen = before.Stolen;
		raid.NextAllowed = before.NextAllowed;
		raid.Phase = "march";
		raid.Target = clan;
		raid.TargetCamp = target->Camp;
		raid.Party = party;
		raid.Started = m_Time;
		raid.Why = why && !why->empty() ? *why : "ordered";
		std::string names;
		for (TribeMember& m : m_Members)
		{
			if (std::find(party.begin(), party.end(), m.Name) == party.end()) continue;
			if (m.Job == "gather") m_Claims.erase(m.Detail);
			m_Board.Release(m.Name);
			m.Job = "raid";
			m.Detail = clan;
			m_Conflict.Members[m.Name].Ordered = false;
			m.Mind.NextThink = m_Time;
			names += (names.empty() ? "" : ", ") + m.Name;
		}
		TellClan("raids " + clan + ": " + names + " (" + raid.Why + ")");
		return true;
	}

	void Tribe::StepRaid(float dt)
	{
		CombatSystem& cs = *m_Combat;
		const RaidRules& rules = cs.Rules.Raids;
		RaidState& raid = m_Conflict.Raid;
		if (raid.Active())
		{
			int out = 0;
			for (const std::string& name : raid.Party)
			{
				const TribeMember* m = nullptr;
				for (const TribeMember& x : m_Members) if (x.Name == name) m = &x;
				if (!m || m->Dead) raid.Home[name] = true;
				const Combatant* c = cs.Find(name);
				if (raid.Home[name] || (c && c->Mode == FightMode::Flee)) ++out;
			}
			const int party = static_cast<int>(raid.Party.size());
			// a pact made meanwhile (a truce, an alliance: the hostility no longer lets it raid them) calls the raid off
			m_Conflict.RaidTimer += dt;
			if (raid.Phase != "retreat" && m_Hostility && m_Conflict.RaidTimer >= kScan)
			{
				m_Conflict.RaidTimer = 0.0f;
				const std::vector<RaidTarget> targets = m_Hostility->RaidTargets(m_ClanName);
				if (std::none_of(targets.begin(), targets.end(), [&](const RaidTarget& t) { return t.Clan == raid.Target; }))
				{
					raid.Phase = "retreat";
					TellClan("calls off the raid on " + raid.Target + ": it may not raid them now (a pact, or the ill will is gone)");
				}
			}
			if (raid.Phase != "retreat" && (m_Time - raid.Started > static_cast<double>(rules.MaxSeconds) || static_cast<float>(out) >= rules.Retreat * static_cast<float>(party)))
			{
				raid.Phase = "retreat";
				TellClan("turns back from the raid on " + raid.Target);
			}
			if (std::all_of(raid.Party.begin(), raid.Party.end(), [&](const std::string& n) { return raid.Home[n]; }))
			{
				std::string loot;
				for (const auto& [item, units] : raid.Loot) loot += (loot.empty() ? "" : ", ") + std::to_string(units) + " " + item;
				TellClan("is back from the raid on " + raid.Target + (loot.empty() ? std::string(": nothing taken") : ": " + loot));
				++raid.Raids;
				raid.NextAllowed = m_Time + static_cast<double>(rules.Cooldown);
				raid.Phase.clear();
				ClanEvent("raid");
				for (const std::string& n : raid.Party) m_Conflict.Members[n].Ordered = false;
			}
			return;
		}
		// weigh a raid now and then: on the clan whose camp the motive is strongest for
		m_Conflict.RaidTimer += dt;
		if (m_Conflict.RaidTimer < rules.Every || !m_Hostility) return;
		m_Conflict.RaidTimer = 0.0f;
		if (m_Time < raid.NextAllowed) return;
		int adults = 0, alive = 0;
		float boldness = 0.0f;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			++alive;
			if (IsChild(m)) continue;
			++adults;
			if (const Combatant* c = cs.Find(m.Name)) boldness += c->Boldness;
		}
		if (adults < rules.Party) return;
		boldness /= static_cast<float>(adults);
		const float hunger = std::clamp(1.0f - static_cast<float>(FoodIn(m_Store)) / (static_cast<float>(alive) * 3.0f), 0.0f, 1.0f);
		std::string best;
		RaidMotive bestMotive;
		for (const RaidTarget& t : m_Hostility->RaidTargets(m_ClanName))
		{
			const RaidMotive motive = WeighRaid(rules, boldness, hunger, m_Hostility->Grievance(m_ClanName, t.Clan), m_Hostility->Hostility(m_ClanName, t.Clan), t.Defense);
			if (motive.Score > bestMotive.Score || best.empty()) { best = t.Clan; bestMotive = motive; }
		}
		if (best.empty() || bestMotive.Score < rules.Want) return;
		std::string why = bestMotive.Why();
		StartRaid(best, &why);
	}

	// ------------------------------------------------------------------ for people
	std::string Tribe::ConflictReport() const
	{
		if (!m_Combat) return "no combat system (the clan cannot fight)\n";
		const CombatSystem& cs = *m_Combat;
		char b[256];
		std::snprintf(b, sizeof b, "%s: fights %d, flights %d, kills %d, deaths in fights %d, wounds tended %d, raided %d\n", m_ClanName.c_str(), m_Conflict.Fights,
			m_Conflict.Flights, m_Conflict.Kills, m_Conflict.Deaths, m_Conflict.Cares, m_Conflict.Raided);
		std::string s = b;
		for (const auto& [tag, n] : m_Conflict.Hunted) s += "hunted " + std::to_string(n) + " " + tag + "\n";
		for (const TribeMember& m : m_Members)
		{
			const Combatant* c = cs.Find(m.Name);
			const auto mc = m_Conflict.Members.find(m.Name);
			const bool threat = mc != m_Conflict.Members.end() && !mc->second.Threat.empty() && m_Time - mc->second.SeenAt < static_cast<double>(cs.Rules.Threats.ForgetSeconds);
			if (!c || (c->Mode == FightMode::None && c->Wounds.empty() && !threat)) continue;
			s += cs.Status(m.Name);
			if (threat && c->Mode == FightMode::None) s += ", wary of " + mc->second.Threat;
			s += "\n";
		}
		const RaidState& r = m_Conflict.Raid;
		if (r.Active())
		{
			std::string party;
			for (const std::string& n : r.Party)
			{
				const auto home = r.Home.find(n);
				party += (party.empty() ? "" : ", ") + n + (home != r.Home.end() && home->second ? " (back)" : "");
			}
			s += "raid on " + r.Target + " (" + r.Phase + "): " + party + "; " + r.Why + "\n";
		}
		else if (r.Raids > 0)
		{
			std::snprintf(b, sizeof b, "raids made %d, things brought home %d\n", r.Raids, r.Stolen);
			s += b;
		}
		return s;
	}
}
