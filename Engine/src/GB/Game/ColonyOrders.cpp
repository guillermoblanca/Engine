// T268: direct orders, their mandate and what cuts them (see ColonyOrders.h). Everything a step does is one of the tribe's own jobs ("gather",
// "deposit", "build", "deliver", "talk") or a walk of the order's own ("goto", "follow"), given through the tribe's Order, Execute and
// ExecuteSettlement, so the body, the abstract agent, the log and the telemetry see an order as they see any other work.
#include "GB/Game/ColonyOrders.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Transplant.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr float kArrive = 1.2f;       // goto: there within this (the acceptance asks 1.5 m)
		constexpr float kFollowGap = 1.8f;    // follow: it stays this far behind its leader ...
		constexpr float kFollowSlack = 3.0f;  // ... and sets off again when the leader is farther than this
		constexpr int kMostFails = 3;         // steps in a row that came to nothing: the order is cut
		constexpr int kMostWaits = 10;        // build: turns waiting for materials someone else is bringing (3 s each)
		constexpr float kRestFloor = 20.0f;   // rest under this cuts an order (the raid's own "in need": TribeConflict.cpp)
		constexpr size_t kNotices = 32;

		std::string Underscored(std::string s)
		{
			std::replace(s.begin(), s.end(), ' ', '_');
			return s;
		}
		std::string Fixed1(float v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
			return b;
		}
		std::string GoTo(const glm::vec2& at, const char* tail)
		{
			char b[96];
			std::snprintf(b, sizeof b, "goto %.2f %.2f | %s", static_cast<double>(at.x), static_cast<double>(at.y), tail);
			return b;
		}
		bool OwnJob(const std::string& job) { return job == "goto" || job == "follow" || Transplant::OwnJob(job); }   // the order's own walks (the tribe's Finish knows nothing of them)
	}

	const ColonyOrders::Mandate* ColonyOrders::Of(const std::string& member) const
	{
		const auto it = m_Mandates.find(member);
		return it == m_Mandates.end() ? nullptr : &it->second;
	}

	std::string ColonyOrders::Describe(const Mandate& d)
	{
		if (d.Verb == "goto") return "go to " + Fixed1(d.At.x) + ", " + Fixed1(d.At.y);
		if (d.Verb == "transplant") return Transplant::Describe(d);   // T325
		return d.Verb + " " + d.Target;
	}

	std::string ColonyOrders::Line() const
	{
		std::string s;
		for (const auto& [name, d] : m_Mandates)
			s += (s.empty() ? "" : ", ") + name + " " + (d.Verb == "goto" ? "goto " + Fixed1(d.At.x) + " " + Fixed1(d.At.y) : d.Verb == "transplant" ? Transplant::Describe(d) : d.Verb + " " + d.Target);
		return s;
	}

	std::string ColonyOrders::Unfit(const Tribe& tribe, const TribeMember& m)
	{
		const AbilitySystemComponent* asc = tribe.m_Gameplay ? tribe.m_Gameplay->Find(m.Name) : nullptr;
		if (asc)
		{
			const float weak = tribe.Config.Work.Jobs.CriticalHunger * 0.5f;   // Maslow's "too weak to leave" (TribeWork.cpp)
			auto level = [&](const char* need) { return asc->Attributes().Has(need) ? asc->Attribute(need) : 100.0f; };
			if (level(NeedName::Thirst) < weak) return "very thirsty";
			if (level(NeedName::Hunger) < weak) return "very hungry";
			if (level(NeedName::Rest) < kRestFloor) return "exhausted";
		}
		if (tribe.Mood(m.Name) < tribe.Config.Social.Mood.Low * 0.5f) return "in low spirits";
		return {};
	}

	ColonyOrders::Reply ColonyOrders::Cancel(Tribe& tribe, const std::string& member)
	{
		TribeMember* m = tribe.MemberNamed(member);
		if (!m || !Has(member)) return { false, member + " has no order" };
		const Mandate d = m_Mandates[member];
		CloseJob(tribe, *m);
		if (d.Verb == "transplant") Transplant::Abandon(tribe, *m, d);   // T325: what it carries goes back in the ground
		m_Mandates.erase(member);
		if (const auto held = tribe.m_Claims.All().find(d.Target); held != tribe.m_Claims.All().end() && held->second == member) tribe.m_Claims.Release(d.Target);
		tribe.Say(*m, "is let off the order to " + Describe(d));
		if (!m->Dead && m->Body) tribe.Decide(*m);
		return { true, member + " is let off the order to " + Describe(d) };
	}

	ColonyOrders::Reply ColonyOrders::Give(Tribe& tribe, const std::string& member, const std::string& verb, const std::vector<std::string>& args)
	{
		if (verb == "cancel") return Cancel(tribe, member);
		TribeMember* found = tribe.MemberNamed(member);
		if (!found) return { false, "there is nobody called " + member + " in the clan" };
		TribeMember& m = *found;
		if (m.Dead || !m.Body) return { false, member + " cannot: " + (m.Dead ? "dead" : "no body") };
		if (m.Player) return { false, member + " is your own character: use game use, approach or the keys" };
		if (m.Job == "fight" || m.Job == "flee" || m.Job == "raid" || m.Job == "expedition" || m.Job == "migrate")
			return { false, member + " cannot now: " + (m.Job == "expedition" || m.Job == "migrate" ? std::string("away from the camp") : "in a " + m.Job) };
		if (const std::string unfit = Unfit(tribe, m); !unfit.empty())
		{
			tribe.Say(m, "refuses the order: " + unfit);
			return { false, member + " refuses the order: " + unfit };
		}
		auto joined = [&]() { std::string s; for (const std::string& a : args) s += (s.empty() ? "" : " ") + a; return s; };
		Mandate d;
		d.Verb = verb;
		d.Since = tribe.m_Time;
		if (verb == "goto")
		{
			char* end = nullptr;
			if (args.size() < 2) return { false, "usage: goto <x> <z>" };
			const float x = std::strtof(args[0].c_str(), &end);
			const bool okx = end && *end == '\0';
			const float z = std::strtof(args[1].c_str(), &end);
			if (!okx || !end || *end != '\0') return { false, "usage: goto <x> <z>" };
			d.At = { x, z };
			if (!m.Body->CanReach(d.At)) return { false, member + " cannot get to " + Fixed1(x) + ", " + Fixed1(z) };
		}
		else if (verb == "use")
		{
			d.Target = joined();
			const AbilitySystemComponent* node = tribe.m_Gameplay->Find(d.Target);
			if (!node || !node->HasTag("Resource.Node")) return { false, "there is no node called " + d.Target };
			bool known = false;
			for (const auto& [id, def] : tribe.m_Defs) if (node->HasTag("Resource.Node." + id)) known = true;
			if (!known) return { false, d.Target + " is not a node the clan can work" };
			if (ResourceSystem::Amount(*node) <= 0) return { false, d.Target + " is empty" };
		}
		else if (verb == "build" || verb == "deliver")
		{
			d.Target = joined();
			const AbilitySystemComponent* site = tribe.m_Gameplay->Find(d.Target);
			if (!tribe.m_Buildings || !site || !site->HasTag("Building.Site") || !BuildingOf(*site, *tribe.m_Buildings)) return { false, "there is no open site called " + d.Target };
		}
		else if (verb == "talk" || verb == "follow")
		{
			d.Target = joined();
			const TribeMember* other = tribe.MemberNamed(d.Target);
			if (!other || other == &m) return { false, member + " cannot " + verb + " " + d.Target + ": not someone else of the clan" };
			if (other->Dead || !other->Body) return { false, d.Target + " is dead" };
			d.At = tribe.Where(*other);
		}
		else if (verb == "transplant")   // T325: the node and the place, and its own verb (transplant, transplant_tree) in the node's data
		{
			if (const std::string why = Transplant::Prepare(tribe, m, joined(), d); !why.empty())
				return { false, why.rfind("does not know", 0) == 0 || why.rfind("cannot get", 0) == 0 ? member + " " + why : why };
		}
		else return { false, "unknown order " + verb + " (goto, use, build, deliver, talk, follow, cancel)" };
		// T322: a verb some technology unlocks needs the clan to know one of them ("use" is matched as use:<kind of node>)
		std::string what;
		if (verb == "use")
			if (const AbilitySystemComponent* node = tribe.m_Gameplay->Find(d.Target))
				for (const auto& [id, def] : tribe.m_Defs) if (node->HasTag("Resource.Node." + id)) { what = id; break; }
		if (verb != "transplant")   // (transplant asked its own verb in Prepare: a tree's is transplant_tree)
			if (const std::string why = TechVerbs::WhyNot(tribe, verb, what); !why.empty()) return { false, member + " " + why };

		// it drops what it was doing: its claims and its job on the board go, the job it was on ends here (no Finish: it did not get there)
		if (const auto old = m_Mandates.find(member); old != m_Mandates.end())
		{
			if (old->second.Verb == "transplant") Transplant::Abandon(tribe, m, old->second);   // T325
			if (const auto held = tribe.m_Claims.All().find(old->second.Target); held != tribe.m_Claims.All().end() && held->second == member) tribe.m_Claims.Release(old->second.Target);
		}
		if (m.Job == "gather" || m.Job == "approach" || m.Job == "stoke") tribe.m_Claims.Release(m.Detail);
		tribe.m_Board.Release(member);
		m.Job.clear();
		m.Detail.clear();
		m.Why.clear();
		m.Mind.Orders.clear();
		if (AbilitySystemComponent* asc = tribe.m_Gameplay->Find(member)) asc->CancelAll();   // (an ability under way would block the order's first one)
		m_Mandates[member] = d;
		tribe.Say(m, "is ordered to " + Describe(d));
		Mandate& held = m_Mandates[member];
		std::string why;
		const Outcome first = Step(tribe, m, held, why);
		if (first == Outcome::Failed)
		{
			m_Mandates.erase(member);
			tribe.Say(m, "cannot carry out the order: " + why);
			tribe.Decide(m);
			return { false, member + " cannot " + Describe(d) + ": " + why };
		}
		if (first == Outcome::Done)
		{
			m_Mandates.erase(member);
			tribe.Say(m, "has done the order: " + Describe(d));
			tribe.Decide(m);
			return { true, member + " has already done it (" + Describe(d) + ")" };
		}
		return { true, member + " goes to " + Describe(d) };
	}

	void ColonyOrders::CloseJob(Tribe& tribe, TribeMember& m)
	{
		if (m.Job.empty()) return;
		if (OwnJob(m.Job))
		{
			m.Job.clear();
			m.Detail.clear();
			m.Mind.Orders.clear();
			return;
		}
		tribe.Finish(m);   // a job of the tribe's: its own accounting (what was gathered, delivered, said)
	}

	void ColonyOrders::End(Tribe& tribe, TribeMember& m, const std::string& reason, bool decide)
	{
		const auto it = m_Mandates.find(m.Name);
		if (it == m_Mandates.end()) return;
		const Mandate d = it->second;
		m_Mandates.erase(it);
		if (d.Verb == "transplant") Transplant::Abandon(tribe, m, d);   // T325: cut with the plant in the bag: it goes back in the ground
		if (const auto held = tribe.m_Claims.All().find(d.Target); held != tribe.m_Claims.All().end() && held->second == m.Name) tribe.m_Claims.Release(d.Target);
		if (reason.empty()) tribe.Say(m, "has done the order: " + Describe(d));
		else
		{
			tribe.Say(m, "drops the order: " + reason);
			m_Notices.push_back({ m.Name, d.Verb, d.Target, reason, tribe.m_Time });
			if (m_Notices.size() > kNotices) m_Notices.erase(m_Notices.begin());
		}
		if (!decide) return;
		CloseJob(tribe, m);
		tribe.Decide(m);   // its own life again at once: a hungry one goes to eat now
	}

	void ColonyOrders::Threat(Tribe& tribe, TribeMember& m)
	{
		if (m_Mandates.empty() || !Has(m.Name)) return;
		End(tribe, m, "a threat", false);   // (the fight or the flight already steers it)
	}

	bool ColonyOrders::Turn(Tribe& tribe, TribeMember& m)
	{
		if (m_Mandates.empty()) return false;
		const auto it = m_Mandates.find(m.Name);
		if (it == m_Mandates.end()) return false;
		if (m.Dead || !m.Body || m.Player) { m_Mandates.erase(it); return false; }
		if (const std::string unfit = Unfit(tribe, m); !unfit.empty())
		{
			End(tribe, m, unfit, true);
			return true;
		}
		Mandate& d = it->second;
		if (d.Verb == "follow")
		{
			std::string why;
			if (StepFollow(tribe, m, d, why) == Outcome::Failed) End(tribe, m, why, true);
			return true;
		}
		if (tribe.IsBusy(m)) return true;   // still at it
		std::string why;
		const Outcome next = Step(tribe, m, d, why);
		if (next == Outcome::Going) return true;
		End(tribe, m, next == Outcome::Done ? std::string() : why, true);
		return true;
	}

	ColonyOrders::Outcome ColonyOrders::Step(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why)
	{
		if (d.Verb == "goto") return StepGoto(tribe, m, d, why);
		if (d.Verb == "use") return StepUse(tribe, m, d, why);
		if (d.Verb == "build" || d.Verb == "deliver") return StepSite(tribe, m, d, why);
		if (d.Verb == "talk") return StepTalk(tribe, m, d, why);
		if (d.Verb == "follow") return StepFollow(tribe, m, d, why);
		if (d.Verb == "transplant") { const Transplant::Outcome o = Transplant::Step(tribe, m, d, why); return o == Transplant::Outcome::Going ? Outcome::Going : o == Transplant::Outcome::Done ? Outcome::Done : Outcome::Failed; }   // T325
		why = "unknown order";
		return Outcome::Failed;
	}

	ColonyOrders::Outcome ColonyOrders::StepGoto(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why)
	{
		if (m.Job == "goto")
		{
			if (tribe.HasFailed(m)) ++d.Fails;
			CloseJob(tribe, m);
		}
		if (glm::distance(tribe.Where(m), d.At) <= kArrive) return Outcome::Done;
		if (d.Fails >= kMostFails || d.Steps > kMostFails + 2) { why = "cannot get there"; return Outcome::Failed; }
		if (!tribe.Order(m, GoTo(d.At, "wait 0.1"))) { why = "cannot get there"; return Outcome::Failed; }
		++d.Steps;
		m.Job = "goto";
		m.Detail = Fixed1(d.At.x) + " " + Fixed1(d.At.y);
		return Outcome::Going;
	}

	ColonyOrders::Outcome ColonyOrders::StepUse(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why)
	{
		const AbilitySystemComponent* node = tribe.m_Gameplay->Find(d.Target);
		std::string kind;
		const ResourceNodeDef* def = nullptr;
		if (node)
			for (const auto& [id, nd] : tribe.m_Defs) if (node->HasTag("Resource.Node." + id)) { kind = id; def = &nd; break; }
		Inventory& bag = m.Body->Bag();
		const std::string item = def ? def->Item : std::string();
		// the step that ended: a gather (did it bring anything?) or a deposit
		const std::string ended = m.Job;
		bool settle = d.Steps == 0;   // (its first trip: whatever it was using a moment ago may still be running)
		if (ended == "gather")
		{
			const std::string reason = tribe.HasFailed(m) ? tribe.WhyFailed(m) : std::string();
			// its last ability was still running when the order came (the editor's bodies: "blocked by State.Busy"): that is no fault, it waits a moment
			if (reason.find("State.Busy") != std::string::npos || reason.find("already running") != std::string::npos) { settle = true; ++d.Waits; }
			const bool failed = !reason.empty() && reason.find("Depleted") == std::string::npos && !settle;
			const int got = item.empty() ? 0 : bag.Count(item) - m.WorkStart;
			if (got <= 0 && failed) ++d.Fails;
			else if (got > 0) d.Fails = 0;
		}
		else if (ended == "deposit" && !item.empty() && bag.Count(item) > 0) ++d.Fails;   // it could not leave it: the store is full or out of reach
		CloseJob(tribe, m);
		if (!node || !def) return d.Steps > 0 ? Outcome::Done : (why = "the node is gone", Outcome::Failed);   // (felled for good: done)
		const int amount = ResourceSystem::Amount(*node);
		if (amount <= 0) d.Spent = true;   // (a node that grows back is not worked for ever: empty once, the order is done)
		Inventory probe = bag;
		const bool room = probe.Add(*tribe.m_Items, item, std::max(1, def->PerGather)) > 0;
		if (d.Fails >= kMostFails || d.Waits > kMostWaits) { why = "cannot work " + d.Target; return Outcome::Failed; }
		// someone else of the clan is working the node now: it waits its turn, never two at one node (SimQA's claims). Two sent to the same node
		// take turns (one swings while the other carries to the store) for as long as the node lasts; behind a member's own gather it waits a little
		if (amount > 0 && room && !d.Spent)
			for (const TribeMember& o : tribe.m_Members)
				if (&o != &m && !o.Dead && (o.Job == "gather" || o.Job == "approach") && o.Detail == d.Target)
				{
					const auto other = m_Mandates.find(o.Name);
					const bool ordered = other != m_Mandates.end() && other->second.Verb == "use" && other->second.Target == d.Target;
					if (!ordered && ++d.Waits > kMostWaits) { why = o.Name + " works " + d.Target; return Outcome::Failed; }
					if (!tribe.Order(m, "wait 3")) { why = "cannot wait"; return Outcome::Failed; }
					m.Job = "goto";   // (a wait of the order's own: no claim on the node)
					m.Detail = d.Target;
					return Outcome::Going;
				}
		if (amount > 0 && room && !d.Spent)
		{
			// a trip of swings at the node, as the tribe's own gather (Tribe::Execute): its ability, its claim, what it held before
			const std::string ability = !def->Ability.empty() ? def->Ability : kind == "tree" ? "Chop" : kind == "rock" ? "Mine" : kind == "fiber_grass" ? "PullFiber" : "Gather";
			const int per = std::max(1, def->PerGather);
			const int swings = std::max(1, std::min(tribe.Config.Economy.Swings, (amount + per - 1) / per));
			std::string text = settle ? "wait 1 | " : "";
			for (int i = 0; i < swings; ++i) text += std::string(i ? " | " : "") + "ability " + ability + " " + Underscored(d.Target) + " | wait 1";
			if (!tribe.Order(m, text)) { why = "cannot work " + d.Target; return Outcome::Failed; }
			m.WorkKind = kind;
			m.WorkStart = bag.Count(item);
			tribe.m_Claims.Claim(d.Target, m.Name);
			m.Job = "gather";
			m.Detail = d.Target;
			if (d.Steps == 0) tribe.Say(m, "goes to " + d.Target + " (" + ability + ")");
			++d.Steps;
			return Outcome::Going;
		}
		// the bag is full or the node is spent: what it gathered goes to the store, then it goes on (or is done)
		if ((bag.Count(item) > 0 || !room) && ended != "deposit")
		{
			UtilityOption o;
			o.Kind = "deposit";
			o.Target = tribe.DepositPlace(m, o.Where);
			if (!tribe.Execute(m, o)) { why = "cannot carry it to the store"; return Outcome::Failed; }
			tribe.m_Claims.Claim(d.Target, m.Name);   // (the node stays its own while it is away)
			++d.Steps;
			return Outcome::Going;
		}
		if (d.Spent) return Outcome::Done;
		why = "its bag is full";
		return Outcome::Failed;
	}

	ColonyOrders::Outcome ColonyOrders::StepSite(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why)
	{
		const std::string ended = m.Job;
		if ((ended == "build" || ended == "deliver") && tribe.HasFailed(m) && tribe.SiteWorkable(d.Target)) ++d.Fails;
		else if (ended == "build" || ended == "deliver") d.Fails = 0;
		CloseJob(tribe, m);
		const AbilitySystemComponent* site = tribe.m_Buildings ? tribe.m_Gameplay->Find(d.Target) : nullptr;
		const BuildingDef* def = site ? BuildingOf(*site, *tribe.m_Buildings) : nullptr;
		if (!site || !def || !site->HasTag("Building.Site")) return Outcome::Done;   // it stands (or is gone)
		const SiteState state = ReadSite(*site, *def);
		if (state.Done) return Outcome::Done;
		if (d.Fails >= kMostFails) { why = "cannot work on " + d.Target; return Outcome::Failed; }
		// the tribe's own options for this site (where to stand, what to carry): build when its phase has its materials, else carry them
		std::vector<UtilityOption> opts;
		tribe.SettlementOptions(m, opts);
		const UtilityOption* build = nullptr;
		const UtilityOption* deliver = nullptr;
		for (const UtilityOption& o : opts)
		{
			if (o.Target != d.Target) continue;
			if (o.Kind == "build") build = &o;
			else if (o.Kind == "deliver") deliver = &o;
		}
		if (d.Verb == "build" && build && state.Ready)
		{
			if (tribe.ExecuteSettlement(m, *build) > 0) { ++d.Steps; d.Waits = 0; return Outcome::Going; }
			++d.Fails;
		}
		else if (deliver)
		{
			if (tribe.ExecuteSettlement(m, *deliver) > 0) { ++d.Steps; d.Waits = 0; return Outcome::Going; }
			++d.Fails;
		}
		else if (d.Verb == "deliver") return Outcome::Done;   // nothing more it can carry there
		if (d.Fails >= kMostFails) { why = "cannot work on " + d.Target; return Outcome::Failed; }
		// the phase waits for materials someone is bringing (or that nothing in the store can give): it waits a little, then gives up
		if (++d.Waits > kMostWaits) { why = d.Target + " lacks materials"; return Outcome::Failed; }
		if (!tribe.Order(m, "wait 3")) { why = "cannot wait"; return Outcome::Failed; }
		m.Job = "goto";   // (a wait of the order's own: no job of the tribe's)
		m.Detail = d.Target;
		return Outcome::Going;
	}

	ColonyOrders::Outcome ColonyOrders::StepTalk(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why)
	{
		if (d.Steps > 0) { CloseJob(tribe, m); return Outcome::Done; }   // the talk it went to is over
		CloseJob(tribe, m);
		TribeMember* other = tribe.MemberNamed(d.Target);
		if (!other || other->Dead || !other->Body) { why = d.Target + " is not there"; return Outcome::Failed; }
		UtilityOption o;
		o.Kind = "talk";
		o.Target = other->Name;
		o.Where = (tribe.Where(m) + tribe.Where(*other)) * 0.5f;
		if (!tribe.ExecuteSocial(m, o)) { why = "they have nothing to talk about now"; return Outcome::Failed; }
		++d.Steps;
		return Outcome::Going;
	}

	ColonyOrders::Outcome ColonyOrders::StepFollow(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why)
	{
		const TribeMember* leader = tribe.MemberNamed(d.Target);
		if (!leader || leader->Dead || !leader->Body) { why = d.Target + " is gone"; return Outcome::Failed; }
		const glm::vec2 at = tribe.Where(m), there = tribe.Where(*leader);
		const float gap = glm::distance(at, there);
		const bool busy = tribe.IsBusy(m);
		if (!busy && !m.Job.empty() && !OwnJob(m.Job)) CloseJob(tribe, m);   // (a job of the tribe's that ended under it)
		if (gap <= kFollowSlack)   // close enough: it stands and waits for the leader to move
		{
			if (!busy) { tribe.Order(m, "wait 1"); m.Job = "follow"; m.Detail = d.Target; }
			return Outcome::Going;
		}
		if (busy && m.Job == "follow" && glm::distance(d.At, there) < kFollowSlack) return Outcome::Going;   // on its way to where the leader is
		const glm::vec2 dir = (there - at) / gap;
		if (!tribe.Order(m, GoTo(there - dir * kFollowGap, "wait 0.5")))
		{
			if (++d.Fails >= kMostFails * 3) { why = "cannot keep up with " + d.Target; return Outcome::Failed; }
			return Outcome::Going;
		}
		d.At = there;
		d.Fails = 0;
		++d.Steps;
		m.Job = "follow";
		m.Detail = d.Target;
		return Outcome::Going;
	}

	std::string ColonyOrders::ToJson() const
	{
		Json j;
		j["version"] = 1;
		Json all = Json::object();
		for (const auto& [name, d] : m_Mandates)
		{
			all[name] = Json{ { "verb", d.Verb }, { "target", d.Target }, { "at", Json::array({ d.At.x, d.At.y }) }, { "since", d.Since }, { "steps", d.Steps }, { "fails", d.Fails }, { "waits", d.Waits }, { "spent", d.Spent ? 1 : 0 } };
			if (!d.Carry.empty()) all[name]["carry"] = d.Carry;   // T325 (only while something is carried: older saves read the same)
		}
		j["mandates"] = all;
		return j.dump();
	}

	bool ColonyOrders::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || j.value("version", 0) != 1 || !j.contains("mandates") || !j["mandates"].is_object()) return false;
		std::map<std::string, Mandate> read;
		for (const auto& [name, jm] : j["mandates"].items())
		{
			if (!jm.is_object() || !jm.contains("verb") || !jm["verb"].is_string()) return false;
			Mandate d;
			d.Verb = jm["verb"].get<std::string>();
			d.Target = jm.value("target", std::string());
			if (jm.contains("at") && jm["at"].is_array() && jm["at"].size() == 2) d.At = { jm["at"][0].get<float>(), jm["at"][1].get<float>() };
			d.Since = jm.value("since", 0.0);
			d.Steps = jm.value("steps", 0);
			d.Fails = jm.value("fails", 0);
			d.Waits = jm.value("waits", 0);
			d.Spent = jm.value("spent", 0) != 0;
			d.Carry = jm.value("carry", std::string());   // T325
			read[name] = d;
		}
		m_Mandates = std::move(read);
		return true;
	}
}
