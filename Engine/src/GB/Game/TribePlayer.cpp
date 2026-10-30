// H15: the player inside the society (see GB/Game/PlayerMember.h and docs/JUGAR.md). The member a person controls is a TribeMember with Player set:
// Tribe::Update gives it a turn like the others (it looks and listens, so it learns where things are, and tells the clan at the camp) but calls
// StepPlayer instead of Finish and Decide, never makes it abstract, and the review of the trades leaves it alone. What the person does reaches the
// clan through the calls below: the store, meals, water and the bed, gifts, the job board, the work its abilities did, crafts, proposals, talk,
// lessons and marriage. Each one is the same rule an NPC follows (Eat, DepositAt, Practise, Worked, Witnessed, Relate, Instruct...) plus what the
// player adds: STANDING, a score of contributions that fades a little every day and makes the clan follow the player's proposals.
// The hooks in Tribe.cpp are one line each.
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		std::string Fixed(float v, int decimals = 0)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.*f", decimals, static_cast<double>(v));
			return b;
		}
		// The jobs of the board a player can hold: those its own work (gathers, crafts, the campfire, a site's phases) moves on.
		bool PlayerJobKind(const std::string& kind)
		{
			return kind == "gather" || kind == "craft" || kind == "make" || kind == "stoke" || kind == "fire" || kind == "build" || kind == "hunt";
		}
		std::string Feeling(float affinity)
		{
			return affinity >= 0.5f ? "loves" : affinity >= 0.2f ? "likes" : affinity > 0.05f ? "is fond of" : affinity > -0.05f ? "hardly knows" : affinity > -0.25f ? "is wary of" : "dislikes";
		}
	}

	// ------------------------------------------------------------------ members
	TribeMember* Tribe::MemberNamed(const std::string& name)
	{
		for (TribeMember& m : m_Members) if (m.Name == name) return &m;
		return nullptr;
	}

	const TribeMember* Tribe::FindMember(const std::string& name) const
	{
		for (const TribeMember& m : m_Members) if (m.Name == name) return &m;
		return nullptr;
	}

	const TribeMember* Tribe::PlayerMember() const
	{
		for (const TribeMember& m : m_Members) if (m.Player && !m.Dead) return &m;
		return nullptr;
	}

	PlayerRecord& Tribe::RecordOf(const std::string& player)
	{
		const auto [it, fresh] = m_Players.Players.try_emplace(player);
		if (fresh) it->second.Standing = Config.Player.StandingStart;
		return it->second;
	}

	bool Tribe::AddPlayer(const std::string& name, ITribeBody* body, const std::string& role)
	{
		if (name.empty() || FindMember(name)) return false;
		AddMember(name, role.empty() ? Config.Player.Role : role, body);
		TribeMember& m = m_Members.back();
		m.Player = true;
		m.Why = "a person decides what it does";
		RecordOf(name);
		Say(m, "joins the clan");
		return true;
	}

	bool Tribe::SetPlayer(const std::string& name, bool on)
	{
		TribeMember* m = MemberNamed(name);
		if (!m) return false;
		if (m->Player == on) return true;
		m->Player = on;
		if (on)
		{
			if (m->Abstract) SetDetail(*m, true);
			for (auto it = m_Claims.begin(); it != m_Claims.end();) it = it->second == name ? m_Claims.erase(it) : std::next(it);
			m_Board.Release(name);
			m->Job.clear();
			m->Detail.clear();
			m->Mind.Orders.clear();
			m->Why = "a person decides what it does";
			RecordOf(name);
			Say(*m, "is taken over by a person");
		}
		else m->Why.clear();
		return true;
	}

	float Tribe::Standing(const std::string& player) const
	{
		const auto it = m_Players.Players.find(player);
		return it != m_Players.Players.end() ? it->second.Standing : 0.0f;
	}

	bool Tribe::Leads(const std::string& player) const
	{
		return m_Players.Players.count(player) && Standing(player) >= Config.Player.Leader;
	}

	void Tribe::Credit(const std::string& player, const std::string& kind, float units)
	{
		if (units == 0.0f || !FindMember(player)) return;
		PlayerRecord& r = RecordOf(player);
		r.Done[kind] += units;
		const auto c = Config.Player.Credit.find(kind);
		if (c == Config.Player.Credit.end()) return;
		r.Standing = std::clamp(r.Standing + c->second * units, 0.0f, Config.Player.StandingMax);
		const bool leads = r.Standing >= Config.Player.Leader;
		if (leads != r.Leader)
		{
			r.Leader = leads;
			if (const TribeMember* m = FindMember(player))
				Say(*m, leads ? "is now looked up to: the clan follows its proposals more readily (standing " + Fixed(r.Standing) + ")" : "no longer leads the clan (standing " + Fixed(r.Standing) + ")");
		}
	}

	bool Tribe::NearCampForPlayer(const TribeMember& m) const
	{
		return m.Body && glm::distance(Where(m), Config.Camp) <= Config.Player.CampReach;
	}

	// ------------------------------------------------------------------ the turn of a player in Update
	void Tribe::StepPlayer(TribeMember& m)
	{
		if (m.Job == "sleep" && m.Body && m.Body->Busy()) { m.Job.clear(); Say(m, "gets up"); }   // it walked off: awake (its orders are a person's)
		Share(m);   // at the camp it tells the clan what it has seen, like anyone (Share checks how near it is)
		m.Why = "a person decides what it does";
		// H14: what its blows did. A kill of an animal is a hunt (its meat came with the combat's event), of a person a defence of the clan.
		if (const Combatant* c = m_Combat ? m_Combat->Find(m.Name) : nullptr)
		{
			// an attack is a lunge: in a fight the player closes on its foe to within reach and keeps after it as it moves; an order that ended early is the
			// person taking the keys (it stands down for one turn)
			MemberConflict& mc = m_Conflict.Members[m.Name];
			if (c->Mode == FightMode::Engage && m.Body)
				if (const Combatant* foe = m_Combat->Find(c->Target); foe && foe->Mode != FightMode::Dead)
				{
					const glm::vec2 at = Where(m);
					const float reach = m_Combat->Rules.Weapon(c->Weapon).Reach;
					if (mc.Ordered && !IsBusy(m)) mc.Ordered = false;
					else if (glm::distance(at, foe->Pos) > reach * 0.9f && (!mc.Ordered || glm::distance(mc.Goal, foe->Pos) > 1.2f))
					{
						const glm::vec2 dir = glm::normalize(foe->Pos - at);
						const glm::vec2 stop = foe->Pos - dir * std::max(reach * 0.6f, 0.9f);   // (never inside the body of the foe)
						char go[64];
						std::snprintf(go, sizeof go, "goto %.2f %.2f run | wait 0.1", static_cast<double>(stop.x), static_cast<double>(stop.y));
						if (Order(m, go)) { mc.Goal = stop; mc.Ordered = true; }
					}
				}
			PlayerRecord& r = RecordOf(m.Name);
			const int known = r.Done.count("kills") ? static_cast<int>(r.Done.at("kills")) : 0;
			if (c->Kills > known)
			{
				const Combatant* last = m_Combat->Find(c->Target);
				const bool animal = !last || last->Animal;
				r.Done["kills"] = static_cast<float>(c->Kills);
				Credit(m.Name, animal ? "hunt" : "defend", static_cast<float>(c->Kills - known));
			}
		}
	}

	// A player that would die (wounds, or any cause while the rules keep it alive) falls and wakes at the camp: its wounds tended, its health and
	// hunger at a third, its foes let go; the clan thinks a little less of it (the "fall" credit). With PlayerRules::Mortal it dies like anyone.
	void Tribe::KnockOut(TribeMember& m, const std::string& cause)
	{
		if (AbilitySystemComponent* asc = m_Gameplay->Find(m.Name))
		{
			for (const char* tag : { "State.Dead", "Death.Hunger", "Death.Thirst", "Death.Warmth", "Death.Wounds" }) asc->Tags().RemoveAll(tag);
			asc->CancelAll();
			for (const char* need : { NeedName::Health, NeedName::Hunger, NeedName::Thirst, NeedName::Warmth })
				if (asc->Attributes().Has(need) && asc->Attribute(need) < 33.0f) SatisfyNeed(*asc, need, 33.0f - asc->Attribute(need));
		}
		if (m_Combat)
		{
			for (const std::string& foe : m_Combat->AttackersOf(m.Name)) m_Combat->Release(foe);
			if (Combatant* c = m_Combat->Find(m.Name)) { c->Mode = FightMode::None; c->Target.clear(); c->Wounds.clear(); c->Lost = 0.0f; c->Morale = 1.0f; }
			m_Combat->Release(m.Name);
		}
		m_Board.Release(m.Name);
		m.Job.clear();
		const glm::vec2 home = CampSpot(m);
		if (m.Body) m.Body->Teleport(home);
		Credit(m.Name, "fall", 1.0f);
		Say(m, "falls (" + (cause.empty() ? std::string("its wounds") : cause) + ") and wakes at the camp, tended by the clan");
	}

	// ------------------------------------------------------------------ fights (H14)
	Tribe::PlayerAct Tribe::PlayerAttack(const std::string& player, const std::string& target)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		if (!m_Combat) return { false, "there are no fights in this world" };
		JoinFight(*m);   // its side, its genes and the best weapon of its bag, now
		m_Combat->Place(player, Where(*m));
		const glm::vec2 at = Where(*m);
		const float range = Config.Player.Reach * 3.0f;
		std::string foe = target;
		if (foe.empty())
		{
			// whoever attacks it first, then the nearest foe in range: a predator or a hunted animal, a fighter of a clan hostile to this one
			const std::vector<std::string> attackers = m_Combat->AttackersOf(player);
			if (!attackers.empty()) foe = attackers.front();
			float best = range;
			if (foe.empty())
			{
				for (const Animal& a : m_Animals)
					if (const float d = glm::distance(a.Pos, at); d <= best) { best = d; foe = a.Name; }
				for (const auto& [name, f] : m_Combat->All())
				{
					if (name == player || f.Mode == FightMode::Dead || f.Side == m_ClanName || f.Animal) continue;
					const bool hostile = m_Hostility && (m_Hostility->Hostility(m_ClanName, f.Side) > 0.0f || m_Hostility->Hostility(f.Side, m_ClanName) > 0.0f);
					if (const float d = glm::distance(f.Pos, at); hostile && d <= best) { best = d; foe = name; }
				}
			}
		}
		if (foe.empty()) return { false, "no foe within " + Fixed(range) + " m" };
		if (!m_Combat->Find(foe) && !m_Combat->JoinAnimal(foe)) return { false, foe + " is not someone to fight" };
		const Combatant* f = m_Combat->Find(foe);
		if (f && f->Side == m_ClanName) return { false, foe + " is of your own clan" };
		if (!m_Combat->Engage(player, foe)) return { false, "you cannot fight " + foe + " now" };
		f = m_Combat->Find(foe);
		const bool raider = f && !f->Animal && glm::distance(f->Pos, Config.Camp) <= Config.Player.CampReach * 2.0f;
		const std::string weapon = m_Combat->Find(player)->Weapon;
		Say(*m, (raider ? "stands up to " : "attacks ") + foe + " with " + weapon);
		if (raider) Credit(player, "defend", 1.0f);
		return { true, "you attack " + foe + " (" + weapon + "; walk into reach to strike)" + (f ? ", " + Fixed(glm::distance(f->Pos, at)) + " m away" : std::string()) };
	}

	Tribe::PlayerAct Tribe::PlayerBlock(const std::string& player)
	{
		const TribeMember* m = FindMember(player);
		if (!m || !m->Player || m->Dead) return { false, "no player called " + player + " in the clan" };
		AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(player) : nullptr;
		if (!m_Combat || !asc) return { false, "there are no fights in this world" };
		std::string why;
		if (!asc->Activate("Block", nullptr, &why)) return { false, "you cannot block now" + (why.empty() ? std::string() : ": " + why) };
		return { true, "you raise your guard" };
	}

	bool Tribe::PlayerFighting(const std::string& player, float* gap) const
	{
		const Combatant* c = m_Combat ? m_Combat->Find(player) : nullptr;
		if (!c || c->Mode != FightMode::Engage) return false;
		const Combatant* foe = m_Combat->Find(c->Target);
		if (!foe || foe->Mode == FightMode::Dead) return false;
		if (gap) *gap = glm::distance(c->Pos, foe->Pos) - m_Combat->Rules.Weapon(c->Weapon).Reach;
		return true;
	}

	Tribe::PlayerAct Tribe::PlayerStop(const std::string& player)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || !m_Combat) return { false, "no player called " + player + " in the clan" };
		Combatant* c = m_Combat->Find(player);
		if (!c || c->Mode != FightMode::Engage) return { false, "you are not attacking anyone" };
		const std::string foe = c->Target;
		m_Combat->Release(player);
		m_Conflict.Members[player].Ordered = false;
		Say(*m, "breaks off from " + foe);
		return { true, "you stop attacking " + foe };
	}

	Tribe::PlayerAct Tribe::PlayerFlee(const std::string& player)
	{
		const TribeMember* m = FindMember(player);
		if (!m || !m->Player || m->Dead) return { false, "no player called " + player + " in the clan" };
		if (!m_Combat) return { false, "there are no fights in this world" };
		const Combatant* c = m_Combat->Find(player);
		std::string from = c && c->Mode == FightMode::Engage ? c->Target : std::string();
		if (const std::vector<std::string> attackers = m_Combat->AttackersOf(player); from.empty() && !attackers.empty()) from = attackers.front();
		if (from.empty()) return { false, "nobody to run from" };
		m_Combat->Flee(player, from);
		Say(*m, "runs from " + from);
		return { true, "you run from " + from + ": get away (the camp is safe)" };
	}

	std::string Tribe::PlayerCondition(const std::string& player) const
	{
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(player) : nullptr;
		if (!asc) return {};
		std::string s = "health " + Fixed(asc->Attributes().Has(NeedName::Health) ? asc->Attribute(NeedName::Health) : 100.0f);
		if (asc->Attributes().Has("Stamina")) s += ", stamina " + Fixed(asc->Attribute("Stamina"));
		if (const Combatant* c = m_Combat ? m_Combat->Find(player) : nullptr)
		{
			for (const Wound& w : c->Wounds) s += ", " + w.Kind + " " + Fixed(w.Severity * 100.0f) + "%" + (w.Bleeding > 0.0f ? " (bleeding)" : "");
			if (c->Mode == FightMode::Engage) s += "; fighting " + c->Target;
			else if (c->Mode == FightMode::Flee) s += "; fleeing " + c->Target;
			if (const std::vector<std::string> at = m_Combat->AttackersOf(player); !at.empty()) s += "; attacked by " + at.front();
		}
		return s;
	}

	void Tribe::GuardPlayer(const TribeMember& m, AbilitySystemComponent& asc) const
	{
		(void)m;
		if (Config.Player.Mortal || !asc.Attributes().Has(NeedName::Health)) return;
		const float health = asc.Attribute(NeedName::Health);
		if (health < Config.Player.FloorHealth) SatisfyNeed(asc, NeedName::Health, Config.Player.FloorHealth - health);
	}

	// The days of the players (their standing fades) and the proposals the clan took on: a want is met when the stores hold what it asked for, a
	// building when it is finished; then the want goes from the economy.
	void Tribe::StepPlayers(float dt)
	{
		if (m_Players.Players.empty() && m_Players.Proposals.empty()) return;
		m_Players.DayTimer += static_cast<double>(dt);
		const double day = static_cast<double>(std::max(Config.Needs.DaySeconds, 1.0f));
		while (m_Players.DayTimer >= day)
		{
			m_Players.DayTimer -= day;
			for (auto& [name, r] : m_Players.Players) r.Standing = std::max(0.0f, r.Standing - Config.Player.DecayPerDay);
		}
		for (Proposal& p : m_Players.Proposals)
		{
			if (p.Status != "accepted") continue;
			bool done = false;
			if (p.Kind == "build")
			{
				for (const Placed& placed : Placements())
					if (placed.Name == p.Site && placed.State.Done) done = true;
			}
			else if (Stored(p.Target) >= p.Start + p.Units) done = true;
			if (!done) continue;
			p.Status = "done";
			if (p.Kind != "build") SetDemand("proposal " + std::to_string(p.Id), p.Target, 0);
			NoteClan("has done what " + p.By + " proposed: " + p.Kind + " " + p.Target + (p.Units > 0 && p.Kind != "build" ? " x" + std::to_string(p.Units) : std::string()));
		}
	}

	// ------------------------------------------------------------------ the store, meals, water and the bed
	Tribe::PlayerAct Tribe::PlayerDeposit(const std::string& player, const std::string& item, int count)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		if (!NearCampForPlayer(*m)) return { false, "the store is at the camp (" + Fixed(glm::distance(Where(*m), Config.Camp)) + " m away)" };
		Inventory& bag = m->Body->Bag();
		const Job* held = m_Board.ClaimOf(player);
		const std::string jobItem = held && held->Kind == "gather" ? held->Target : std::string();
		const int jobBefore = jobItem.empty() ? 0 : bag.Count(jobItem);
		int moved = 0;
		if (item.empty()) moved = DepositAt(*m, std::string());   // everything that is not a tool, as a member does (with the board and the food counted)
		else
		{
			moved = bag.Transfer(*m_Items, m_Store, item, count > 0 ? count : bag.Count(item));
			if (moved > 0 && item == jobItem) m_Board.Progress(player, static_cast<float>(moved), m_Time);
			if (moved > 0 && IsFood(item)) { m_Econ.FoodToday += moved; m_FoodBy = player; }
		}
		if (moved <= 0) return { false, item.empty() ? "nothing to leave in the store (tools stay in the bag)" : "you carry no " + item };
		m->Deposited += moved;
		Credit(player, "deposit", static_cast<float>(moved));
		const int forJob = jobItem.empty() ? 0 : jobBefore - bag.Count(jobItem);
		if (forJob > 0) Credit(player, "job", static_cast<float>(forJob));
		Say(*m, "leaves " + std::to_string(moved) + " in the store" + (forJob > 0 ? " (" + std::to_string(forJob) + " for the job " + held->Id + ")" : std::string()));
		Share(*m);
		return { true, "left " + std::to_string(moved) + " in the store" + (forJob > 0 ? ", " + std::to_string(forJob) + " for your job" : std::string()) + "; standing " + Fixed(Standing(player)) };
	}

	Tribe::PlayerAct Tribe::PlayerTake(const std::string& player, const std::string& item, int count)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		if (!NearCampForPlayer(*m)) return { false, "the store is at the camp" };
		const int n = m_Store.Transfer(*m_Items, m->Body->Bag(), item, std::max(1, count));
		if (n <= 0) return { false, "the store has no " + item + " (or your bag is full)" };
		if (!IsFood(item)) Credit(player, "take", static_cast<float>(n));   // food is everyone's to eat; the rest the clan gathered for itself
		if (AbilitySystemComponent* asc = m_Gameplay->Find(player)) ResourceSystem::SyncTools(*asc, m->Body->Bag(), *m_Items);   // a tool counts at once
		Say(*m, "takes " + std::to_string(n) + " " + item + " from the store");
		return { true, "took " + std::to_string(n) + " " + item };
	}

	Tribe::PlayerAct Tribe::PlayerEat(const std::string& player)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		const bool carries = !FirstFood(m->Body->Bag()).empty();
		if (!carries && !NearCampForPlayer(*m)) return { false, "you carry nothing to eat and the store is at the camp" };
		if (!carries && FirstFood(m_Store).empty()) return { false, "the store has nothing to eat" };
		const int before = m->Meals;
		Eat(*m);   // what it carries first, else the store's oldest; the one who brought it is liked more (H10)
		if (m->Meals == before) return { false, "nothing to eat" };
		const AbilitySystemComponent* asc = m_Gameplay->Find(player);
		return { true, "you eat (hunger " + Fixed(asc ? asc->Attribute(NeedName::Hunger) : 100.0f) + ")" };
	}

	Tribe::PlayerAct Tribe::PlayerDrink(const std::string& player)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		if (m->Body->Bag().Count("water") == 0) return { false, "you carry no water (draw some at the well)" };
		const AbilitySystemComponent* asc = m_Gameplay->Find(player);
		const float before = asc ? asc->Attribute(NeedName::Thirst) : 0.0f;
		Drink(*m);
		const float after = asc ? asc->Attribute(NeedName::Thirst) : 0.0f;
		if (after <= before) return { false, "you are not thirsty" };
		return { true, "you drink (thirst " + Fixed(after) + ")" };
	}

	bool Tribe::SetPlayerActivity(const std::string& player, const std::string& job, const std::string& detail)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead) return false;
		if (job.empty() && m->Job == "sleep") return true;   // (only waking up ends the sleep)
		m->Job = job;
		m->Detail = detail;
		return true;
	}

	Tribe::PlayerAct Tribe::PlayerSleep(const std::string& player, bool on)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		if (!on)
		{
			if (m->Job != "sleep") return { false, "you are awake" };
			m->Job.clear();
			Say(*m, "gets up");
			return { true, "you get up" };
		}
		const glm::vec2 bed = SleepSpot(*m), spot = CampSpot(*m);
		const bool roof = glm::distance(bed, spot) > 0.01f;
		m->Job = "sleep";
		m->Detail = roof ? "bed" : "camp";
		Say(*m, "lies down to sleep");
		return { true, std::string("you sleep") + (roof ? " (your bed in a shelter is at " : " (no bed for you yet: your place at the camp is at ") + Fixed(bed.x) + " " + Fixed(bed.y) + ")" };
	}

	// ------------------------------------------------------------------ gifts and the job board
	Tribe::PlayerAct Tribe::PlayerGive(const std::string& player, const std::string& to, const std::string& item, int count)
	{
		TribeMember* m = MemberNamed(player);
		TribeMember* o = MemberNamed(to);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		if (!o || o == m || o->Dead || !o->Body) return { false, "nobody called " + to + " in the clan" };
		if (glm::distance(Where(*m), Where(*o)) > Config.Player.Reach) return { false, to + " is too far away" };
		const int n = m->Body->Bag().Transfer(*m_Items, o->Body->Bag(), item, std::max(1, count));
		if (n <= 0) return { false, "you carry no " + item + " (or " + to + " has no room)" };
		const ItemDef* d = m_Items->Find(item);
		const float worth = static_cast<float>(n) * ((IsFood(item) || (d && d->Category == "tool")) ? 2.0f : 1.0f);
		m_Society.Adjust(Config.Society, player, to, Config.Player.GiftBond * worth);
		Credit(player, "gift", worth);
		if (AbilitySystemComponent* asc = m_Gameplay->Find(to)) ResourceSystem::SyncTools(*asc, o->Body->Bag(), *m_Items);
		Say(*m, "gives " + std::to_string(n) + " " + item + " to " + to);
		return { true, "gave " + std::to_string(n) + " " + item + " to " + to + " (" + to + " " + Feeling(m_Society.Affinity(Config.Society, to, player)) + " you)" };
	}

	Tribe::PlayerAct Tribe::PlayerClaim(const std::string& player, const std::string& job)
	{
		const TribeMember* m = FindMember(player);
		if (!m || !m->Player || m->Dead) return { false, "no player called " + player + " in the clan" };
		std::string id = job;
		if (id.empty())   // the most pressing job with room that a player can do
			for (const Job* j : m_Board.Ordered())
				if (PlayerJobKind(j->Kind) && (m_Board.HasRoom(*j) || m_Board.ClaimOf(player) == j)) { id = j->Id; break; }
		const Job* found = m_Board.Find(id);
		if (!found) return { false, id.empty() ? "the board has nothing you can do now" : "no job '" + id + "' on the board (tribe jobs)" };
		if (!PlayerJobKind(found->Kind)) return { false, "a " + found->Kind + " job is for the clan's own members" };
		if (m_Board.ClaimOf(player) == found) return { true, "you already hold " + id };
		if (!m_Board.Claim(id, player, m_Time)) return { false, "nobody else can take " + id + " now (all its places are held)" };
		Say(*m, "takes the job " + id + " from the board");
		return { true, "you take the job " + id + ": " + found->Why };
	}

	Tribe::PlayerAct Tribe::PlayerRelease(const std::string& player)
	{
		const Job* held = m_Board.ClaimOf(player);
		if (!held) return { false, "you hold no job" };
		const std::string id = held->Id;
		m_Board.Release(player);
		if (const TribeMember* m = FindMember(player)) Say(*m, "leaves the job " + id);
		return { true, "you leave the job " + id };
	}

	// ------------------------------------------------------------------ the work of the player's abilities
	Tribe::PlayerAct Tribe::PlayerWorked(const std::string& player, const std::string& activity, const std::string& key, float units)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		if (units <= 0.0f) return { false, "nothing was done" };
		const Job* held = m_Board.ClaimOf(player);
		std::string text;
		if (activity == "gather")
		{
			// the node's kind (for the aptitude and the witnesses) and its item (for the job): `key` is either
			std::string node = m_Defs.count(key) ? key : std::string(), item = node.empty() ? key : m_Defs.at(node).Item;
			if (node.empty())
				for (const auto& [kind, def] : m_Defs) if (def.Item == key) { node = kind; break; }
			if (!node.empty()) Practise(*m, "gather", node, units);
			m->Job = "gather";   // (for an instant: who gathers beside it grows closer, as two members do)
			Worked(*m);
			m->Job.clear();
			if (!node.empty()) Witnessed(*m, std::string(), node);
			if (held && held->Kind == "gather" && held->Target == item) m_Board.Progress(player, 0.0f, m_Time);   // (the units count when they reach the store)
			text = "gathered " + Fixed(units) + " " + item;
		}
		else if (activity == "craft" || activity == "make")
		{
			Practise(*m, "craft", "craft", units);
			ClanEvent("craft");
			Witnessed(*m, key, std::string());
			Credit(player, "craft", units);
			if (held && (held->Kind == "craft" || held->Kind == "make") && held->Target == key) { m_Board.Progress(player, units, m_Time); Credit(player, "job", units); }
			text = "made " + key;
		}
		else if (activity == "stoke" || activity == "fire")
		{
			Practise(*m, activity, activity, units);
			ClanEvent(activity);
			Credit(player, "stoke", units);
			if (held && held->Kind == activity) { m_Board.Progress(player, units, m_Time); Credit(player, "job", units); }
			text = activity == "stoke" ? "fed the campfire" : "lit the campfire";
		}
		else if (activity == "build")
		{
			Practise(*m, "build", "build", units);
			Credit(player, "build", units);
			if (held && held->Kind == "build" && held->Target == key) { m_Board.Progress(player, units, m_Time); Credit(player, "job", units); }
			text = "worked on " + key;
		}
		else return { false, "unknown work '" + activity + "' (gather, craft, stoke, fire, build)" };
		Say(*m, text);
		return { true, text + "; standing " + Fixed(Standing(player)) };
	}

	Tribe::PlayerAct Tribe::PlayerCraft(const std::string& player, const std::string& recipe)
	{
		TribeMember* m = MemberNamed(player);
		if (!m || !m->Player || m->Dead || !m->Body) return { false, "no player called " + player + " in the clan" };
		const Recipe* r = m_Recipes->Find(recipe);
		if (!r) return { false, "no recipe '" + recipe + "'" };
		const std::set<std::string> known = m_Knowledge.KnownRecipes(player, *m_Recipes);   // H11: only what it knows how to make
		const std::set<std::string>* gate = m_Knowledge.Gated() ? &known : nullptr;
		const std::string station = r->Station.empty() ? std::string() : (NearCampForPlayer(*m) ? CraftStation(recipe) : std::string());
		Inventory& bag = m->Body->Bag();
		std::string why = m_Recipes->WhyNot(*m_Items, bag, recipe, station, gate);
		bool made = false;
		if (why.empty()) made = m_Recipes->Craft(*m_Items, bag, recipe, station, gate);
		else if (NearCampForPlayer(*m) && m_Recipes->WhyNot(*m_Items, m_Store, recipe, station, gate).empty())   // at the camp the store's materials will do
		{
			made = m_Recipes->Craft(*m_Items, m_Store, recipe, station, gate);
			if (made) m_Store.Transfer(*m_Items, bag, r->Output, r->OutputCount);
		}
		if (!made) return { false, "cannot make " + recipe + ": " + why };
		if (AbilitySystemComponent* asc = m_Gameplay->Find(player)) ResourceSystem::SyncTools(*asc, bag, *m_Items);   // a new tool counts at once
		++m->Made;
		return PlayerWorked(player, "craft", recipe, 1.0f);
	}

	// ------------------------------------------------------------------ proposals
	Tribe::PlayerAct Tribe::Propose(const std::string& player, const std::string& kind, const std::string& target, int units, glm::vec2 at)
	{
		const TribeMember* me = FindMember(player);
		if (!me || !me->Player || me->Dead) return { false, "no player called " + player + " in the clan" };
		const ProposalRules& rules = Config.Player.Proposals;
		if (kind != "gather" && kind != "make" && kind != "build") return { false, "propose gather|make <item> [units] | build <building>" };
		if (kind == "gather" && std::none_of(m_Defs.begin(), m_Defs.end(), [&](const auto& d) { return d.second.Item == target; })) return { false, "nothing in the world gives " + target };
		if (kind == "make" && !m_Recipes->Find(target)) return { false, "no recipe makes " + target };
		if (kind == "build" && (!m_Buildings || !m_Buildings->count(target))) return { false, "no building '" + target + "'" };
		PlayerRecord& rec = RecordOf(player);
		if (m_Time - rec.LastProposal < static_cast<double>(rules.Cooldown))
			return { false, "the clan is still talking over your last proposal (wait " + Fixed(static_cast<float>(static_cast<double>(rules.Cooldown) - (m_Time - rec.LastProposal))) + " s)" };
		rec.LastProposal = m_Time;

		Proposal p;
		p.Id = m_Players.NextProposal++;
		p.By = player;
		p.Kind = kind;
		p.Target = target;
		p.Units = kind == "build" ? 0 : std::clamp(units > 0 ? units : (kind == "gather" ? 10 : 1), 1, rules.MaxUnits);
		p.Posted = m_Time;
		const float cost = kind == "build" ? rules.BuildCost : rules.UnitCost * static_cast<float>(p.Units);
		std::map<std::string, float> norms;
		for (const auto& [norm, w] : rules.Culture) norms[norm] = m_Society.Norm(Config.Society, norm);
		const float standing = Standing(player);
		const bool leader = Leads(player);
		std::string against;
		for (const TribeMember& o : m_Members)
		{
			if (o.Dead || !o.Body || o.Player || IsChild(o)) continue;   // the adults of the clan vote (not the players, not the children)
			const float bond = m_Society.Affinity(Config.Society, o.Name, player);
			const float vote = ProposalVote(rules, bond, standing, Config.Player.StandingMax, leader, norms, cost);
			++p.Voters;
			if (vote > 0.0f) ++p.Yes;
			else if (against.size() < 60) against += (against.empty() ? "" : ", ") + o.Name;
		}
		const bool accepted = p.Voters > 0 && static_cast<float>(p.Yes) > rules.Majority * static_cast<float>(p.Voters);
		p.Why = std::to_string(p.Yes) + " of " + std::to_string(p.Voters) + " agree (standing " + Fixed(standing) + (leader ? ", you lead" : "") + ")" + (against.empty() ? std::string() : "; against: " + against);
		p.Status = accepted ? "accepted" : "rejected";
		if (accepted)
		{
			if (kind == "build")
			{
				p.At = at != glm::vec2(0.0f) ? at : Config.Camp + glm::vec2(-Config.SiteRadius, 2.0f * static_cast<float>(p.Id % 3));
				std::string site;
				if (const std::string error = StartSite(target, p.At, &site); !error.empty()) { p.Status = "dropped"; p.Why += "; but " + error; }
				else p.Site = site;
			}
			else
			{
				p.Start = Stored(target);
				SetDemand("proposal " + std::to_string(p.Id), target, p.Start + p.Units);   // the economy plans it and the board posts its gathers and makes
			}
			if (p.Status == "accepted") Credit(player, "proposal", 1.0f);
		}
		const std::string what = kind + " " + target + (kind == "build" ? std::string() : " x" + std::to_string(p.Units));
		Say(*me, "proposes to " + what + ": " + (p.Status == "accepted" ? "the clan agrees" : p.Status == "dropped" ? "the clan agrees but cannot" : "the clan says no") + " (" + p.Why + ")");
		m_Players.Proposals.push_back(p);
		if (m_Players.Proposals.size() > 40) m_Players.Proposals.erase(m_Players.Proposals.begin());
		if (accepted && kind != "build") PostJobs();   // the board shows it at once
		return { p.Status == "accepted", "proposal #" + std::to_string(p.Id) + " " + what + ": " + p.Status + " (" + p.Why + ")" + (p.Site.empty() ? std::string() : "; site " + p.Site) };
	}

	// ------------------------------------------------------------------ talk, lessons and marriage
	Tribe::PlayerAct Tribe::PlayerTalk(const std::string& player, const std::string& other)
	{
		const TribeMember* m = FindMember(player);
		const TribeMember* o = FindMember(other);
		if (!m || !m->Player || m->Dead) return { false, "no player called " + player + " in the clan" };
		if (!o || o == m) return { false, "nobody called " + other + " in the clan" };
		if (o->Dead) return { true, other + " died of " + o->Cause };
		if (m->Body && o->Body && glm::distance(Where(*m), Where(*o)) > Config.Player.Reach) return { false, other + " is too far away to talk" };
		PlayerRecord& rec = RecordOf(player);
		const auto last = rec.TalkedAt.find(other);
		if (last == rec.TalkedAt.end() || m_Time - last->second >= static_cast<double>(Config.Player.TalkSeconds))
		{
			m_Society.Adjust(Config.Society, player, other, Config.Player.TalkBond);
			rec.TalkedAt[other] = m_Time;
		}
		const Job* held = m_Board.ClaimOf(other);
		const std::string doing = o->Job.empty() ? std::string("thinking what to do next") : o->Job + (o->Detail.empty() ? std::string() : " " + o->Detail);
		std::string s = other + " (" + o->Role + ", " + Society::StageName(m_Society.StageOf(Config.Society, other)) + "): " + doing;
		if (const std::string why = Because(*o); !why.empty()) s += ", because " + why;
		s += held ? "; its job: " + held->Id : std::string("; no job of the board");
		if (const std::string best = BestSkill(o->Skills); !best.empty()) s += "; best at " + best + " " + std::to_string(static_cast<int>(SkillLevel(o->Skills, best)));
		const float affinity = m_Society.Affinity(Config.Society, other, player);
		s += "; it " + Feeling(affinity) + " you (" + Fixed(affinity, 2) + ")";
		if (const std::string kin = m_Society.Kin(player, other); !kin.empty()) s += ", your " + kin;
		return { true, s };
	}

	Tribe::PlayerAct Tribe::PlayerTeach(const std::string& player, const std::string& other, const std::string& tech)
	{
		const TribeMember* m = FindMember(player);
		const TribeMember* o = FindMember(other);
		if (!m || !m->Player || m->Dead) return { false, "no player called " + player + " in the clan" };
		if (!o || o == m || o->Dead) return { false, "nobody called " + other + " in the clan" };
		if (!m_Knowledge.Gated()) return { false, "without technologies there is nothing to teach" };
		if (m->Body && o->Body && glm::distance(Where(*m), Where(*o)) > Config.Player.Reach) return { false, other + " is too far away" };
		std::string t = tech;
		if (t.empty())
			if (const AgentKnowledge* k = m_Knowledge.Of(player))
				for (const std::string& known : k->Known) if (m_Knowledge.CanLearn(other, known)) { t = known; break; }
		if (t.empty()) return { false, "you know nothing " + other + " could learn now" };
		if (!m_Knowledge.Knows(player, t)) return { false, "you do not know " + m_Knowledge.Name(t) };
		if (!m_Knowledge.CanLearn(other, t)) return { false, other + " cannot learn " + m_Knowledge.Name(t) + " now (it knows it, or not what it needs first)" };
		Tell(m_Knowledge.Instruct(other, t, Config.Player.TeachAmount, "a lesson from " + player));
		Relate(player, other, "teach");
		Credit(player, "teach", 1.0f);
		const AgentKnowledge* k = m_Knowledge.Of(other);
		const bool knows = m_Knowledge.Knows(other, t);
		const float progress = k && k->Progress.count(t) ? k->Progress.at(t) : 0.0f;
		return { true, "you show " + other + " " + m_Knowledge.Name(t) + (knows ? ": now it knows it" : ": it understands " + Fixed(progress * 100.0f) + "%") };
	}

	Tribe::PlayerAct Tribe::PlayerLearn(const std::string& player, const std::string& other, const std::string& tech)
	{
		const TribeMember* m = FindMember(player);
		const TribeMember* o = FindMember(other);
		if (!m || !m->Player || m->Dead) return { false, "no player called " + player + " in the clan" };
		if (!o || o == m || o->Dead) return { false, "nobody called " + other + " in the clan" };
		if (!m_Knowledge.Gated()) return { false, "without technologies there is nothing to learn" };
		if (m->Body && o->Body && glm::distance(Where(*m), Where(*o)) > Config.Player.Reach) return { false, other + " is too far away" };
		std::string t = tech;
		if (t.empty())
			if (const AgentKnowledge* k = m_Knowledge.Of(other))
				for (const std::string& known : k->Known) if (m_Knowledge.CanLearn(player, known)) { t = known; break; }
		if (t.empty()) return { false, other + " knows nothing you could learn now" };
		if (!m_Knowledge.Knows(other, t)) return { false, other + " does not know " + m_Knowledge.Name(t) };
		if (!m_Knowledge.CanLearn(player, t)) return { false, "you cannot learn " + m_Knowledge.Name(t) + " now" };
		Tell(m_Knowledge.Instruct(player, t, Config.Player.TeachAmount, "a lesson from " + other));
		Relate(player, other, "teach");
		const bool knows = m_Knowledge.Knows(player, t);
		return { true, other + " shows you " + m_Knowledge.Name(t) + (knows ? ": now you know it" : ": keep at it") };
	}

	Tribe::PlayerAct Tribe::PlayerMarry(const std::string& player, const std::string& other)
	{
		const TribeMember* m = FindMember(player);
		const TribeMember* o = FindMember(other);
		if (!m || !m->Player || m->Dead) return { false, "no player called " + player + " in the clan" };
		if (!o || o == m || o->Dead || o->Player) return { false, "nobody called " + other + " in the clan" };
		const Person* p = m_Society.Find(other);
		if (!p || IsChild(*o)) return { false, other + " is a child" };
		if (!p->Partner.empty() && p->Partner != player) return { false, other + " is " + p->Partner + "'s partner" };
		const float affinity = m_Society.Affinity(Config.Society, other, player);
		if (affinity < Config.Player.MarryBond) return { false, other + " " + Feeling(affinity) + " you, not enough to share a life (" + Fixed(affinity, 2) + " of " + Fixed(Config.Player.MarryBond, 2) + ")" };
		if (!m_Society.Marry(Config.Society, player, other)) return { false, "the clan does not see it" };
		ClanEvent("pair");
		Say(*m, "and " + other + " become partners");
		return { true, "you and " + other + " are partners now" };
	}

	// ------------------------------------------------------------------ for people
	std::string Tribe::PlayerReport(const std::string& player) const
	{
		const TribeMember* m = FindMember(player);
		if (!m) return "no member called " + player;
		std::string s = player + " (" + m->Role + (m->Player ? ", player" : "") + "): standing " + Fixed(Standing(player)) + " of " + Fixed(Config.Player.StandingMax)
			+ (Leads(player) ? ", leads the clan" : ", leads at " + Fixed(Config.Player.Leader));
		if (const auto it = m_Players.Players.find(player); it != m_Players.Players.end() && !it->second.Done.empty())
		{
			s += "; done:";
			for (const auto& [kind, units] : it->second.Done) s += " " + kind + " " + Fixed(units);
		}
		const Job* held = m_Board.ClaimOf(player);
		s += held ? "; job " + held->Id : std::string("; no job");
		s += "; meals " + std::to_string(m->Meals) + ", deposited " + std::to_string(m->Deposited) + ", made " + std::to_string(m->Made);
		if (const Person* p = m_Society.Find(player); p && !p->Partner.empty()) s += "; partner " + p->Partner;
		return s;
	}

	std::string Tribe::ProposalsReport() const
	{
		std::string s;
		for (const Proposal& p : m_Players.Proposals)
			s += "proposal #" + std::to_string(p.Id) + " by " + p.By + ": " + p.Kind + " " + p.Target + (p.Kind == "build" ? std::string() : " x" + std::to_string(p.Units)) + ": " + p.Status
				+ " (" + p.Why + ")" + (p.Site.empty() ? std::string() : " " + p.Site) + "\n";
		return s;
	}
}
