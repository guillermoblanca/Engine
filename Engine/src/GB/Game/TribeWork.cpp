// H7 · roles and the work of the clan (E3 aptitudes by practice, E8 job board): the part of the tribe that decides WHO does WHAT. It is kept apart
// from Tribe.cpp (the senses, the needs and the decision of each member) so the two grow without stepping on each other; the hooks there are few:
// Decide calls ClaimJob and JobBonus, Execute reads the aptitude for the gathers of a trip, Finish calls Practise, and Update runs PostJobs and
// ReviewRoles on their clocks. Deterministic like the rest: maps in id order, ties to the first, no randomness.
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		// Wood and what is under it burns (the same rule as Stoke and Tribe.cpp's IsFuel).
		bool IsFuel(const ItemDef& d)
		{
			return std::any_of(d.Tags.begin(), d.Tags.end(), [](const std::string& t) { return t == "Item.Resource.Wood" || t.rfind("Item.Resource.Wood.", 0) == 0; });
		}

		bool HasFuel(const ItemLibrary& items, const Inventory& inv)
		{
			for (const ItemSlot& s : inv.Stacks())
				if (const ItemDef* d = items.Find(s.Id); d && s.Count > 0 && IsFuel(*d)) return true;
			return false;
		}
	}

	// ------------------------------------------------------------------ aptitudes
	float Tribe::MemberWeight(const TribeMember& m, const std::string& kind) const
	{
		const float base = RoleWeight(m.Role, kind);
		const std::string family = SkillFamilyOf(kind);
		if (family.empty()) return base;
		const SkillRules& rules = Config.Work.Skills;
		return base + rules.Weight * std::clamp(SkillLevel(m.Skills, family) / std::max(rules.Max, 1e-3f), 0.0f, 1.0f);   // what it is good at, it likes doing
	}

	bool Tribe::IsProfession(const TribeMember& m, const std::string& family) const
	{
		const auto role = Config.Roles.find(m.Role);
		return !family.empty() && role != Config.Roles.end() && role->second.Skill == family;
	}

	// A finished piece of work trains the family of what was worked: `activity` is "gather", "craft", "stoke" or "fire" (how many units of practice
	// one piece is: SkillRules::Practice) and `key` the node kind or the option it was ("craft").
	void Tribe::Practise(TribeMember& m, const std::string& activity, const std::string& key, float units)
	{
		const SkillRules& rules = Config.Work.Skills;
		const std::string family = SkillFamilyOf(key);
		if (family.empty()) return;
		const auto per = rules.Practice.find(activity);
		// H9: how fast it learns is inherited (genes) and a child learns faster; the skill itself is not inherited
		const float practice = units * (per != rules.Practice.end() ? per->second : 1.0f) * m_Society.LearnRate(Config.Society, m.Name, family);
		const float before = SkillLevel(m.Skills, family);
		::GB::Practise(m.Skills, rules, family, practice, IsProfession(m, family));
		const float after = SkillLevel(m.Skills, family);
		// the log says when a member reaches a new tenth of a skill (not every point: it would fill the log)
		if (static_cast<int>(after / 10.0f) > static_cast<int>(before / 10.0f)) Say(m, "is better at " + family + " (" + std::to_string(static_cast<int>(after)) + ")");
	}

	// ------------------------------------------------------------------ the trades
	std::vector<std::string> Tribe::PlanRoles(int count, bool current) const
	{
		std::vector<std::string> out;
		std::vector<std::string> names;
		for (const auto& [name, r] : Config.Roles) if (!r.Fixed) names.push_back(name);
		if (names.empty() || count <= 0) return out;
		const size_t members = static_cast<size_t>(count);

		// What each role is the best at: every item the nodes give is wanted (its stock target for a clan of this size) and that want goes to the roles
		// with the highest weight for one of its nodes (split when they tie). With `current`, an item the store already has is wanted less (a quarter
		// of its target stays: the clan always needs some of everything), and one it has more than enough of less still, down to a tenth of that
		// quarter at twice its target: a surplus frees hands (a clan of 20 kept 15 foragers while 844 fibre piled up).
		std::map<std::string, std::vector<std::string>> kindsOf;
		for (const auto& [kind, def] : m_Defs) kindsOf[def.Item].push_back(kind);
		std::map<std::string, float> demand;
		for (const auto& [item, kinds] : kindsOf)
		{
			float want = StockFor(item, members);
			if (current)
			{
				float have = static_cast<float>(m_Store.Count(item));
				for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) have += static_cast<float>(m.Body->Bag().Count(item));
				const float ratio = have / std::max(want, 1e-3f);
				want *= ratio < 1.0f ? 0.25f + 0.75f * (1.0f - ratio) : 0.25f * std::clamp(2.0f - ratio, 0.1f, 1.0f);
			}
			float best = 0.0f;
			std::vector<std::string> who;
			for (const std::string& role : names)
			{
				float w = 0.0f;
				for (const std::string& kind : kinds) w = std::max(w, RoleWeight(role, kind));
				if (w > best + 1e-6f) { best = w; who.assign(1, role); }
				else if (w > 0.0f && std::fabs(w - best) <= 1e-6f) who.push_back(role);
			}
			for (const std::string& role : who) demand[role] += want / static_cast<float>(who.size());
		}
		{
			// the campfire is kept by the roles that like it most
			float best = 0.0f;
			std::vector<std::string> who;
			for (const std::string& role : names)
			{
				const float w = RoleWeight(role, "fire");
				if (w > best + 1e-6f) { best = w; who.assign(1, role); }
				else if (w > 0.0f && std::fabs(w - best) <= 1e-6f) who.push_back(role);
			}
			for (const std::string& role : who) demand[role] += Config.Work.Roles.FireDemand / static_cast<float>(who.size());
		}

		// Shared out one member at a time: each goes to the role with the most demand per member already in it (ties: the first by name).
		std::map<std::string, int> taken;
		for (size_t i = 0; i < members; ++i)
		{
			std::string pick;
			float value = -1.0f;
			for (const std::string& role : names)
			{
				const float v = demand[role] / static_cast<float>(1 + taken[role]);
				if (v > value + 1e-6f) { value = v; pick = role; }
			}
			if (value <= 0.0f) pick = names[i % names.size()];   // nothing to gather at all: round the roles
			++taken[pick];
			out.push_back(pick);
		}
		// The hands every clan keeps in a trade (TribeRole::Min, by name): a trade short of them takes the last member given to the trade with the
		// most hands above its own minimum (a clan of three whose stores were full of wood had no woodcutter left: no axe, no wood, no fire)
		for (const std::string& role : names)
			while (taken[role] < Config.Roles.at(role).Min)
			{
				std::string from;
				int spare = 0;
				for (const std::string& other : names)
					if (other != role && taken[other] - Config.Roles.at(other).Min > spare) { spare = taken[other] - Config.Roles.at(other).Min; from = other; }
				if (from.empty()) break;   // (fewer members than the minimums ask for)
				*std::find(out.rbegin(), out.rend(), from) = role;
				--taken[from];
				++taken[role];
			}
		return out;
	}

	// Now and then the clan looks at who does what: if a trade has fewer hands than the needs ask for, one member of a trade with too many takes it
	// (the one best at it, its skill in its own trade counting against the move); else a member far better at another trade than at its own
	// takes that one. At most one change per review, a clan below MinClan does not specialise, and nobody takes a trade whose tool it cannot get.
	bool Tribe::ReviewRoles()
	{
		const RoleRules& rules = Config.Work.Roles;
		std::vector<TribeMember*> flexible;
		int alive = 0;
		for (TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			++alive;
			const auto role = Config.Roles.find(m.Role);
			if ((role == Config.Roles.end() || !role->second.Fixed) && !IsChild(m) && !m.Player) flexible.push_back(&m);   // (a child keeps its child's trade, H9; a player chooses its own, H15)
		}
		if (alive < rules.MinClan || flexible.empty()) return false;

		std::map<std::string, int> ideal, actual;
		for (const std::string& r : PlanRoles(static_cast<int>(flexible.size()), true)) ++ideal[r];
		for (const TribeMember* m : flexible) ++actual[m->Role];
		auto skillOf = [&](const TribeMember& m, const std::string& role)
		{
			const auto r = Config.Roles.find(role);
			return r == Config.Roles.end() || r->second.Skill.empty() ? 0.0f : SkillLevel(m.Skills, r->second.Skill);
		};
		auto canTake = [&](const TribeMember& m, const std::string& role)
		{
			const auto r = Config.Roles.find(role);
			if (r == Config.Roles.end() || r->second.Fixed) return false;
			const TribeRole& t = r->second;
			return t.Tool.empty() || m.Body->Bag().CountTagged(*m_Items, t.ToolTag) > 0 || (m_Knowledge.CanMake(m.Name, t.Tool) && m_Recipes->WhyNot(*m_Items, m_Store, t.Tool, CraftStation(t.Tool)).empty());   // (H11: and it knows how)
		};
		auto change = [&](TribeMember& m, const std::string& role, const std::string& why)
		{
			Say(m, "takes up the trade of " + role + " (" + why + ")");
			m.Role = role;
		};

		// 1) a trade short of hands
		std::string shortRole;
		int gap = 0;
		for (const auto& [role, n] : ideal)
			if (n - actual[role] > gap) { gap = n - actual[role]; shortRole = role; }
		if (gap >= 1)
		{
			TribeMember* best = nullptr;
			float value = -1e9f;
			for (TribeMember* m : flexible)
			{
				if (m->Role == shortRole || actual[m->Role] <= ideal[m->Role] || !canTake(*m, shortRole)) continue;
				const float v = skillOf(*m, shortRole) - rules.Inertia * skillOf(*m, m->Role);
				if (v > value) { value = v; best = m; }
			}
			if (best)
			{
				change(*best, shortRole, "the clan needs more hands there: " + std::to_string(actual[shortRole]) + " of " + std::to_string(ideal[shortRole]));
				return true;
			}
		}

		// 2) a member much better at another trade than at its own
		TribeMember* who = nullptr;
		std::string to;
		float margin = 0.0f;
		auto minOf = [&](const std::string& role) { const auto r = Config.Roles.find(role); return r == Config.Roles.end() ? 0 : r->second.Min; };
		for (TribeMember* m : flexible)
		{
			if (actual[m->Role] <= minOf(m->Role)) continue;   // the last hands of an essential trade stay in it
			const float own = skillOf(*m, m->Role);
			for (const auto& [role, r] : Config.Roles)
			{
				if (role == m->Role || r.Fixed || r.Skill.empty() || actual[role] >= ideal[role] + 1 || !canTake(*m, role)) continue;
				const float d = skillOf(*m, role) - own;
				if (d >= rules.SkillSwitch && d > margin) { margin = d; who = m; to = role; }
			}
		}
		if (who)
		{
			char why[96];
			std::snprintf(why, sizeof why, "it is %.0f points better at it than at its own", static_cast<double>(margin));
			change(*who, to, why);
			return true;
		}
		return false;
	}

	// ------------------------------------------------------------------ the job board
	void Tribe::PostJobs()
	{
		if (!m_Items || !m_Recipes) return;
		const JobRules& rules = Config.Work.Jobs;
		auto priority = [&](const char* kind) { const auto p = rules.Priority.find(kind); return p != rules.Priority.end() ? p->second : 1.0f; };
		int alive = 0;
		std::map<std::string, int> carried;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			++alive;
			for (const ItemSlot& s : m.Body->Bag().Stacks()) carried[s.Id] += s.Count;
		}
		PlanEconomy();   // H18: the production plan of now (its raw shortages are wanted by the gather jobs below)
		m_Board.Begin();
		if (alive > 0)
		{
			// gather: every item a node gives that the store holds less of than it wants (with the upkeep of the camp and the wood for the campfire)
			std::map<std::string, std::string> kindOf;   // item -> the first node (by id) that gives it: the family of the job
			for (const auto& [kind, def] : m_Defs) kindOf.emplace(def.Item, kind);
			std::string fuel;   // the fuel the campfire burns best: it wants FireWood more of it while there is a fire
			int heat = 0;
			for (const auto& [item, kind] : kindOf)
				if (const ItemDef* d = m_Items->Find(item); d && IsFuel(*d) && (d->Fuel > 0 ? d->Fuel : 2) > heat) { heat = d->Fuel > 0 ? d->Fuel : 2; fuel = item; }
			for (const auto& [item, kind] : kindOf)
			{
				if (m_Store.Count(item) >= StoreCap()) continue;
				float wanted = StockFor(item, m_Members.size());
				std::string extra;
				if (const auto raw = m_Plan.Raw.find(item); raw != m_Plan.Raw.end() && static_cast<float>(m_Store.Count(item) + raw->second) > wanted)
				{
					wanted = static_cast<float>(m_Store.Count(item) + raw->second);   // H18: what the production chains need of it
					extra = " (with what the clan wants to make)";
				}
				if (const auto u = rules.Upkeep.find(item); u != rules.Upkeep.end() && u->second > 0.0f) { wanted += u->second * static_cast<float>(alive); extra = " (with the upkeep of the camp)"; }
				if (item == fuel && HasFire()) { wanted += rules.FireWood; extra += " (and the campfire)"; }
				// the pyramid: a clan wants the days of food it lives on in hand, whatever the stock table says of it
				if (IsFood(item))
				{
					const NeedsConfig& nc = Config.Needs;
					const float perDay = (nc.DaySeconds / std::max(nc.HungerSeconds, 1.0f)) * (100.0f / static_cast<float>(std::max(nc.FoodHunger, 1)));
					const float days = std::min(rules.SurvivalDays * perDay * static_cast<float>(alive), static_cast<float>(StoreCap()));
					if (days > wanted) { wanted = days; extra += " (the days of food the clan lives on)"; }
				}
				const float have = static_cast<float>(Stored(item) + carried[item]);   // (H18: the stockpiles count too)
				if (have >= wanted) continue;
				const float need = std::clamp(1.0f - have / wanted, 0.0f, 1.0f);
				const int slots = std::clamp(static_cast<int>(std::ceil((wanted - have) / rules.SlotUnits)), 1, std::max(1, rules.MaxSlots));
				char why[128];
				std::snprintf(why, sizeof why, "the store has %d of %.0f %s", m_Store.Count(item), static_cast<double>(wanted), item.c_str());
				m_Board.Post("gather", item, SkillFamilyOf(kind), wanted - have, priority("gather") * (0.3f + 0.7f * need) * (IsFood(item) ? 1.0f + rules.SurvivalBoost * FoodShortage() : 1.0f), slots, why + extra, m_Time, rules);
			}
			// craft: the tool of a trade that a member lacks
			std::map<std::string, int> lacking;
			for (const TribeMember& m : m_Members)
			{
				if (m.Dead || !m.Body) continue;
				const auto role = Config.Roles.find(m.Role);
				if (role == Config.Roles.end() || role->second.Tool.empty()) continue;
				if (m.Body->Bag().CountTagged(*m_Items, role->second.ToolTag) == 0) ++lacking[role->second.Tool];
			}
			for (const auto& [tool, n] : lacking)
				m_Board.Post("craft", tool, SkillFamilyOf("craft"), static_cast<float>(n), priority("craft"), n, std::to_string(n) + " of the clan lack a " + tool, m_Time, rules);
			// the campfire: to light once the tools are made and the store can pay for it, to feed when it runs low
			if (!HasFire() && ToolsReady() && m_Store.Count("wood") >= 4 && m_Store.Count("stone") >= 3)
				m_Board.Post("fire", Config.FireName, SkillFamilyOf("fire"), 1.0f, priority("fire"), 1, "the tools are ready and the store can pay for the campfire", m_Time, rules);
			if (HasFire() && FireFuel() <= Config.StokeBelow)
			{
				bool fuelAt = HasFuel(*m_Items, m_Store);
				for (const TribeMember& m : m_Members) fuelAt = fuelAt || (!m.Dead && m.Body && HasFuel(*m_Items, m.Body->Bag()));
				char why[96];
				std::snprintf(why, sizeof why, "the campfire has %.1f fuel%s", static_cast<double>(FireFuel()), FireBurning() ? "" : " and is out");
				if (fuelAt) m_Board.Post("stoke", Config.FireName, SkillFamilyOf("stoke"), 1.0f, priority("stoke") * (FireBurning() ? 1.0f : 1.3f), 1, why, m_Time, rules);
			}
			PostSettlementJobs();   // H8: carry materials to the construction sites and work their phases (Settlement.cpp)
		}
		if (alive > 0) PostEconomyJobs();   // H18: make, farm and haul (TribeEconomy.cpp)
		PostExpedition();   // H12: a party forming or out (TribeExplore.cpp)
		if (alive > 0) PostConflictJobs();   // H14: hunts (TribeConflict.cpp)
		for (const std::string& gone : m_Board.End(m_Time, rules)) (void)gone;   // (met needs and lapsed claims: the member chooses again when it next decides)
	}

	// What a job is worth to a member now: its priority, the member's liking for that work (role and aptitude), how far it is and how fit the member is.
	// 0 = it cannot do it (it knows no node of the item, lacks the tool the node asks for, it is not its tool, it does not keep fires...).
	float Tribe::JobScore(const TribeMember& m, const Job& job, const std::map<std::string, Nearest>& known) const
	{
		if (!m.Body) return 0.0f;
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(m.Name) : nullptr;
		const float vigor = asc && asc->Attributes().Has(NeedName::Vigor) ? asc->Attribute(NeedName::Vigor) : 1.0f;
		const glm::vec2 pos = Where(m);
		Inventory& bag = m.Body->Bag();
		if (job.Kind == "gather")
		{
			if (m_Store.Count(job.Target) >= StoreCap()) return 0.0f;
			float liking = 0.0f, distance = 1e30f;
			bool any = false;
			for (const auto& [kind, def] : m_Defs)
			{
				if (def.Item != job.Target) continue;
				if (!def.RequiresTag.empty() && bag.CountTagged(*m_Items, def.RequiresTag) == 0) continue;
				const auto it = known.find(kind);
				if (it == known.end() || !it->second.Any) continue;   // a job is only for whoever knows where to do it
				any = true;
				liking = std::max(liking, MemberWeight(m, kind));
				if (!it->second.Name.empty()) distance = std::min(distance, it->second.Distance);
			}
			if (!any) return 0.0f;
			if (distance > 1e29f) distance = 60.0f;   // it knows of some, all taken or not trusted now: as if far
			return job.Priority * liking * Falloff(distance, 40.0f) * vigor;
		}
		if (job.Kind == "expedition") return ExpeditionJobScore(m, job);   // H12
		if (job.Kind == "hunt") return ConflictJobScore(m, job);   // H14

		if (job.Kind == "make" || job.Kind == "farm" || job.Kind == "haul") return EconomyJobScore(m, job);   // H18
		if (job.Kind == "deliver" || job.Kind == "build") return SettlementJobScore(m, job) * vigor;   // H8 (Settlement.cpp)
		const float home = Falloff(glm::distance(pos, Config.Camp), 100.0f);
		if (job.Kind == "craft")
		{
			const auto role = Config.Roles.find(m.Role);
			if (role == Config.Roles.end() || role->second.Tool != job.Target || bag.CountTagged(*m_Items, role->second.ToolTag) > 0 || !CanCraftTool(m)) return 0.0f;
			return job.Priority * home * vigor;   // its own tool: nobody else can make it for it
		}
		if (job.Kind == "fire" || job.Kind == "stoke")
		{
			const float liking = MemberWeight(m, "fire");
			if (liking <= 0.0f) return 0.0f;
			if (job.Kind == "stoke" && !(asc && std::find(asc->Granted().begin(), asc->Granted().end(), "Stoke") != asc->Granted().end())) return 0.0f;
			if (job.Kind == "fire")
			{
				const auto role = Config.Roles.find(m.Role);
				if (role == Config.Roles.end() || role->second.Tool.empty() || bag.CountTagged(*m_Items, role->second.ToolTag) == 0) return 0.0f;   // as the "fire" option asks
			}
			return job.Priority * liking * home * vigor;
		}
		return 0.0f;
	}

	// Called as the member decides: it keeps its claim unless it can no longer do that work or another job is worth clearly more (JobRules::Switch:
	// hysteresis on top of the chooser's), and with none it takes the best one with room, if it is worth MinScore.
	void Tribe::ClaimJob(TribeMember& m, const std::map<std::string, Nearest>& known)
	{
		const JobRules& rules = Config.Work.Jobs;
		const Job* held = m_Board.ClaimOf(m.Name);
		float heldScore = held ? JobScore(m, *held, known) * SurvivalFactor(*held) : 0.0f;
		if (held && heldScore <= 0.0f)
		{
			m_Board.Release(m.Name);
			held = nullptr;
		}
		const Job* best = nullptr;
		float bestScore = 0.0f;
		for (const auto& [id, job] : m_Board.Jobs())
		{
			if (&job == held || !m_Board.HasRoom(job)) continue;
			const float s = JobScore(m, job, known) * SurvivalFactor(job);
			if (s > bestScore) { bestScore = s; best = &job; }
		}
		if (!best || bestScore < rules.MinScore || (held && bestScore <= heldScore + rules.Switch)) return;
		const std::string id = best->Id;
		if (m_Board.Claim(id, m.Name, m_Time)) Say(m, "takes the job " + id + " from the board");
	}

	// ------------------------------------------------------------------ the pyramid of needs (Maslow)
	// The lowest tier is staying alive: food, water, warmth. While the clan is short of food nothing above it is worth doing, and the food jobs are
	// worth more; once the clan has the days of food it wants, the pyramid steps aside and the rest of the board (tools, buildings, knowledge) goes on.
	float Tribe::FoodDays() const
	{
		int units = FoodIn(m_Store), alive = 0;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead) continue;
			++alive;
			if (m.Body) units += FoodIn(m.Body->Bag());
		}
		if (alive == 0) return 0.0f;
		const NeedsConfig& n = Config.Needs;
		// what a member eats in a day: it empties in HungerSeconds, a unit of food gives back FoodHunger of 100
		const float perDay = (n.DaySeconds / std::max(n.HungerSeconds, 1.0f)) * (100.0f / static_cast<float>(std::max(n.FoodHunger, 1)));
		return static_cast<float>(units) / (perDay * static_cast<float>(alive));
	}

	float Tribe::FoodShortage() const
	{
		const float wanted = Config.Work.Jobs.SurvivalDays;
		if (wanted <= 0.0f) return 0.0f;
		return std::clamp(1.0f - FoodDays() / wanted, 0.0f, 1.0f);
	}

	std::string Tribe::MaslowTier() const
	{
		if (FoodShortage() > 0.25f) return "survival";
		bool afraid = false;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead) continue;
			const std::map<std::string, float> needs = NeedLevels(m);
			if (const auto it = needs.find(NeedName::Safety); it != needs.end() && it->second < 40.0f) afraid = true;
		}
		if (!HasFire() && Beds() == 0) return "warmth";
		if (afraid) return "safety";
		return "growth";
	}

	void Tribe::RecordRow()
	{
		ChronicleRow row;
		row.Time = m_Time;
		row.Hour = Hour();
		row.Day = static_cast<int>((m_Time / std::max(1.0, static_cast<double>(Config.Needs.DaySeconds))) + static_cast<double>(Config.Needs.StartHour) / 24.0) + 1;
		row.Tier = MaslowTier();
		row.FoodDays = FoodDays();
		row.Fire = FireBurning();
		row.Beds = Beds();
		row.Buildings = BuildingCount();
		for (const std::string& id : m_Items->Ids())
			if (const int n = m_Store.Count(id)) row.Store[id] = n;
		for (const TribeMember& m : m_Members)
		{
			ChronicleMember c;
			c.Name = m.Name;
			c.Role = m.Role;
			c.Job = m.Job;
			c.Detail = m.Detail;
			c.Dead = m.Dead;
			(m.Dead ? row.Dead : row.Alive) += 1;
			const std::map<std::string, float> needs = NeedLevels(m);
			auto level = [&](const char* n) { const auto it = needs.find(n); return it == needs.end() ? -1.0f : it->second; };
			c.Hunger = level(NeedName::Hunger);
			c.Thirst = level(NeedName::Thirst);
			c.Rest = level(NeedName::Rest);
			c.Warmth = level(NeedName::Warmth);
			c.Health = level(NeedName::Health);
			row.Members.push_back(std::move(c));
		}
		m_Chronicle.Add(std::move(row));
	}

	float Tribe::SurvivalFactor(const Job& job) const
	{
		const float shortage = FoodShortage();
		if (shortage <= 0.0f) return 1.0f;
		const JobRules& r = Config.Work.Jobs;
		if ((job.Kind == "gather" || job.Kind == "expedition") && IsFood(job.Target)) return 1.0f + r.SurvivalBoost * shortage;
		if (job.Kind == "stoke" || job.Kind == "fire" || job.Kind == "farm" || job.Kind == "haul" || job.Kind == "hunt") return 1.0f;   // warmth, fields and meat are staying alive too
		return 1.0f + (r.SurvivalDamp - 1.0f) * shortage;
	}

	void Tribe::MaslowOptions(const TribeMember& m, float hunger, int foodInBag, bool foodHeld, std::vector<UtilityOption>& options) const
	{
		const JobRules& r = Config.Work.Jobs;
		float severity = FoodShortage();
		if (!foodHeld && hunger < r.CriticalHunger)   // its own stomach, whatever the clan's days say
			severity = std::max(severity, std::clamp(1.0f - hunger / std::max(r.CriticalHunger, 1.0f), 0.0f, 1.0f));
		if (severity <= 0.0f) return;
		const float boost = 1.0f + r.SurvivalBoost * severity;
		const float damp = 1.0f + (r.SurvivalDamp - 1.0f) * severity;
		for (UtilityOption& o : options)
		{
			bool food = false, neutral = false;
			if (o.Kind == "eat" || o.Kind == "drink") food = true;
			else if (o.Kind == "gather")
			{
				const MemoryEntry* e = m.Mind.Memory.Find(o.Target);
				const auto def = e ? m_Defs.find(e->Kind) : m_Defs.end();
				food = def != m_Defs.end() && IsFood(def->second.Item);
			}
			else if (o.Kind == "explore")
			{
				const auto def = m_Defs.find(o.Target);
				food = def != m_Defs.end() && IsFood(def->second.Item);
			}
			else if (o.Kind == "deposit") food = foodInBag > 0;   // food in the bag goes to the store, where the clan eats it
			else if (o.Kind == "expedition") { const Job* held = m_Board.ClaimOf(m.Name); food = held && IsFood(held->Target); }
			else if (IsOutside(o.Kind)) food = true;   // the clans around: a meeting or a trade is how a hungry clan eats too, so it weighs like food
			else neutral = o.Kind == "sleep" || o.Kind == "warm" || o.Kind == "regroup" || o.Kind == "rest" || o.Kind == "stoke" || o.Kind == "fire" || o.Kind == "farm"
				|| o.Kind == "haul" || o.Kind == "hunt" || o.Kind == "recover" || o.Kind == "care" || o.Kind == "migrate";
			if (neutral) continue;
			// eating and drinking when it can come first of all: the stomach is not a job to weigh against the others
			const float f = (o.Kind == "eat" || o.Kind == "drink") ? boost * 2.0f : food ? boost : damp;
			o.Score *= f;
			o.Factors.push_back({ food ? "survival" : "survival (not now)", f });
		}
	}

	void Tribe::JobBonus(const TribeMember& m, std::vector<UtilityOption>& options) const
	{
		const Job* held = m_Board.ClaimOf(m.Name);
		if (!held) return;
		const float factor = 1.0f + Config.Work.Jobs.Bonus;
		for (UtilityOption& o : options)
		{
			bool match = false;
			if (held->Kind == "gather")
			{
				if (o.Kind == "gather")
					if (const MemoryEntry* e = m.Mind.Memory.Find(o.Target))
						if (const auto def = m_Defs.find(e->Kind); def != m_Defs.end()) match = def->second.Item == held->Target;
			}
			else if (held->Kind == "craft") match = o.Kind == "tool" && o.Target == held->Target;
			else if (held->Kind == "deliver" || held->Kind == "build") match = o.Kind == held->Kind && o.Target == held->Target;   // that site (H8)
			else match = o.Kind == held->Kind;   // "fire", "stoke"
			if (!match) continue;
			o.Score *= factor;
			o.Factors.push_back({ "job", factor });
			o.Note += "; job: " + held->Id;
		}
	}

	std::string Tribe::JobsReport() const
	{
		std::string s = m_Board.Report();
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead) continue;
			const Job* held = m_Board.ClaimOf(m.Name);
			const std::string best = BestSkill(m.Skills);
			s += m.Name + " (" + m.Role + "): " + (held ? held->Id : std::string("no job"));
			if (!best.empty()) s += ", best at " + best + " " + std::to_string(static_cast<int>(SkillLevel(m.Skills, best)));
			s += "\n";
		}
		return s + ProposalsReport();   // H15: what the players proposed and what the clan said
	}
}
