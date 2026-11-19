// T325: moving a resource node (see Transplant.h). Each step is a walk and a wait of the order's own (jobs "uproot" and "plant"), given through
// the tribe's Order, so the body, the abstract agent and the log see it as any other work.
#include "GB/Game/Transplant.h"
#include "GB/Game/GroundFootprint.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/Techs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <vector>

namespace GB
{
	namespace
	{
		constexpr const char* kUsage = "usage: transplant <node|x z> to <x> <z>";

		std::string Fixed1(float v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
			return b;
		}
		std::string GoTo(const glm::vec2& at, float wait)
		{
			char b[96];
			std::snprintf(b, sizeof b, "goto %.2f %.2f | wait %.2f", static_cast<double>(at.x), static_cast<double>(at.y), static_cast<double>(std::max(0.1f, wait)));
			return b;
		}
		bool Number(const std::string& s, float& out)
		{
			if (s.empty()) return false;
			char* end = nullptr;
			out = std::strtof(s.c_str(), &end);
			return end && *end == '\0' && std::isfinite(out);
		}
		std::vector<std::string> Words(const std::string& text)
		{
			std::vector<std::string> out;
			std::stringstream ss(text);
			for (std::string w; ss >> w;) out.push_back(w);
			return out;
		}
		// Where to stand to work at `spot`, coming from `from`: kStandOff short of it (the spot itself when the member is on it).
		glm::vec2 StandAt(const glm::vec2& from, const glm::vec2& spot)
		{
			const glm::vec2 d = from - spot;
			const float len = std::sqrt(d.x * d.x + d.y * d.y);
			return len > Transplant::kStandOff ? spot + d / len * Transplant::kStandOff : from;
		}
	}

	const ResourceNodeDef* Transplant::DefOf(const Tribe& tribe, const std::string& node)
	{
		const AbilitySystemComponent* a = tribe.m_Gameplay ? tribe.m_Gameplay->Find(node) : nullptr;
		if (!a || !a->HasTag("Resource.Node")) return nullptr;
		for (const auto& [id, def] : tribe.m_Defs)
			if (a->HasTag("Resource.Node." + id)) return def.Transplant.Item.empty() ? nullptr : &def;
		return nullptr;
	}

	glm::vec2 Transplant::NodePos(const Tribe& tribe, const std::string& node, bool* found)
	{
		if (found) *found = false;
		if (!tribe.m_Entities) return glm::vec2(0.0f);
		const EntityId id = tribe.m_Entities->FindByName(node);
		if (id == NullEntity) return glm::vec2(0.0f);
		if (found) *found = true;
		const glm::vec4 p = tribe.m_Entities->WorldMatrix(id)[3];
		return { p.x, p.z };
	}

	std::string Transplant::NodeNear(const Tribe& tribe, const glm::vec2& at)
	{
		if (!tribe.m_Gameplay) return {};
		std::string best;
		float bestD = kPick;
		for (const GameplayWorld::Tagged& t : tribe.m_Gameplay->WithTag("Resource.Node"))
		{
			if (!DefOf(tribe, *t.Name)) continue;
			bool found = false;
			const glm::vec2 p = NodePos(tribe, *t.Name, &found);
			const float d = glm::distance(p, at);
			if (found && d <= bestD) { bestD = d; best = *t.Name; }   // (name order breaks a tie: the first)
		}
		return best;
	}

	std::string Transplant::Parse(const Tribe& tribe, const std::string& text, std::string& node, glm::vec2& to)
	{
		const std::vector<std::string> w = Words(text);
		size_t sep = w.size();
		for (size_t i = w.size(); i-- > 0;) if (w[i] == "to") { sep = i; break; }
		if (sep == 0 || sep + 3 != w.size() || !Number(w[sep + 1], to.x) || !Number(w[sep + 2], to.y)) return kUsage;
		glm::vec2 at;
		if (sep == 2 && Number(w[0], at.x) && Number(w[1], at.y))
		{
			node = NodeNear(tribe, at);
			if (node.empty()) return "there is no plant that can be moved within " + Fixed1(kPick) + " m of " + Fixed1(at.x) + ", " + Fixed1(at.y);
			return {};
		}
		node.clear();
		for (size_t i = 0; i < sep; ++i) node += (i ? " " : "") + w[i];
		return {};
	}

	std::string Transplant::WhyNot(const Tribe& tribe, const TribeMember& m, const std::string& node, const glm::vec2& to)
	{
		const AbilitySystemComponent* a = tribe.m_Gameplay ? tribe.m_Gameplay->Find(node) : nullptr;
		if (!a || !a->HasTag("Resource.Node")) return "there is no node called " + node;
		const ResourceNodeDef* def = DefOf(tribe, node);
		if (!def) return node + " cannot be moved";
		if (!TechVerbs::Knows(tribe, def->Transplant.Verb)) return "does not know how to " + VerbWord(def->Transplant.Verb);
		if (const auto held = tribe.m_Claims.All().find(node); held != tribe.m_Claims.All().end() && held->second != m.Name) return held->second + " works " + node;
		const glm::vec2 from = NodePos(tribe, node);
		if (const std::string held = Held(tribe, node, from); !held.empty()) return held;   // T333
		const std::string where = Fixed1(to.x) + ", " + Fixed1(to.y);
		if (glm::distance(from, to) < kRoom) return node + " is there already";
		// the ground: no site, building or field; dry; no other node too close
		GroundRect spot;
		spot.Centre = to;
		spot.Half = glm::vec2(kRoom * 0.5f);
		if (const std::string why = GroundFree(spot, TakenGround(tribe), 0.0f); !why.empty()) return "cannot plant at " + where + ": " + why;
		if (m.Body && m.Body->WaterDepthAt(to) > kWetDepth) return "cannot plant at " + where + ": it is in the water";
		for (const GameplayWorld::Tagged& t : tribe.m_Gameplay->WithTag("Resource.Node"))
		{
			if (*t.Name == node) continue;
			bool found = false;
			const glm::vec2 p = NodePos(tribe, *t.Name, &found);
			if (found && glm::distance(p, to) < kRoom) return "cannot plant at " + where + ": " + *t.Name + " grows there";
		}
		if (m.Body && !m.Body->CanReach(to)) return "cannot get to " + where;
		if (m.Body && !m.Body->CanReach(StandAt(m.Body->Position(), from))) return "cannot get to " + node;
		return {};
	}

	std::string Transplant::Prepare(const Tribe& tribe, const TribeMember& m, const std::string& text, ColonyOrders::Mandate& d)
	{
		std::string node;
		glm::vec2 to(0.0f);
		if (const std::string bad = Parse(tribe, text, node, to); !bad.empty()) return bad;
		if (const std::string why = WhyNot(tribe, m, node, to); !why.empty()) return why;
		d.Target = node;
		d.At = to;
		d.Carry.clear();
		return {};
	}

	ColonyOrders::Reply Transplant::Assign(Tribe& tribe, const std::string& text)
	{
		std::string node;
		glm::vec2 to(0.0f);
		if (const std::string bad = Parse(tribe, text, node, to); !bad.empty()) return { false, bad };
		const ResourceNodeDef* def = DefOf(tribe, node);
		if (!tribe.m_Gameplay || !tribe.m_Gameplay->Find(node)) return { false, "there is no node called " + node };
		if (!def) return { false, node + " cannot be moved" };
		if (!TechVerbs::Knows(tribe, def->Transplant.Verb)) return { false, "the clan does not know how to " + VerbWord(def->Transplant.Verb) };
		const glm::vec2 from = NodePos(tribe, node);
		if (const std::string held = Held(tribe, node, from); !held.empty()) return { false, "cannot: " + held };   // T333
		const TribeMember* best = nullptr;
		float bestD = std::numeric_limits<float>::max();
		std::string lastWhy;
		for (const TribeMember& m : tribe.m_Members)
		{
			if (m.Dead || !m.Body || m.Player || tribe.Colony().Has(m.Name)) continue;
			if (m.Job == "fight" || m.Job == "flee" || m.Job == "raid" || m.Job == "expedition" || m.Job == "migrate") continue;
			if (!ColonyOrders::Unfit(tribe, m).empty()) continue;
			if (const std::string why = WhyNot(tribe, m, node, to); !why.empty()) { lastWhy = why; continue; }
			const float dist = glm::distance(tribe.Where(m), from);
			if (dist < bestD) { bestD = dist; best = &m; }   // (member order breaks a tie)
		}
		if (!best) return { false, lastWhy.empty() ? std::string("nobody of the clan is free to transplant ") + node : "cannot: " + lastWhy };
		const std::string name = best->Name;
		return tribe.Colony().Give(tribe, name, "transplant", { node + " to " + Fixed1(to.x) + " " + Fixed1(to.y) });
	}

	std::string Transplant::Describe(const ColonyOrders::Mandate& d)
	{
		return "transplant " + d.Target + " to " + Fixed1(d.At.x) + ", " + Fixed1(d.At.y);
	}

	bool Transplant::Uproot(Tribe& tribe, TribeMember& m, ColonyOrders::Mandate& d, std::string& why)
	{
		const ResourceNodeDef* def = DefOf(tribe, d.Target);
		AbilitySystemComponent* node = tribe.m_Gameplay ? tribe.m_Gameplay->Find(d.Target) : nullptr;
		if (!def || !node) { why = "the node is gone"; return false; }
		if (const std::string held = Held(tribe, d.Target, NodePos(tribe, d.Target)); !held.empty()) { why = held; return false; }   // T333 (on the way there)
		if (m.Body->Bag().Add(*tribe.m_Items, def->Transplant.Item, 1) < 1) { why = "its bag is full"; return false; }
		AttributeSet& at = node->Attributes();
		at.SetBase("Amount", 0.0f);   // (what grew on it is lost with the digging)
		if (at.Has("Regrow")) at.SetBase("Regrow", 0.0f);
		d.Carry = def->Id;
		tribe.m_Claims.Claim(d.Target, m.Name);   // (nobody gathers from it while it is on its way)
		tribe.Say(m, "digs up " + d.Target + " (" + def->Transplant.Item + ")");
		return true;
	}

	bool Transplant::Plant(Tribe& tribe, TribeMember& m, ColonyOrders::Mandate& d, std::string& why)
	{
		const ResourceNodeDef* def = DefOf(tribe, d.Target);
		AbilitySystemComponent* node = tribe.m_Gameplay ? tribe.m_Gameplay->Find(d.Target) : nullptr;
		const EntityId id = tribe.m_Entities ? tribe.m_Entities->FindByName(d.Target) : NullEntity;
		if (!def || !node || id == NullEntity) { why = "the node is gone"; return false; }
		if (m.Body->Bag().Remove(def->Transplant.Item, 1) < 1) { why = "it lost the " + def->Transplant.Item; return false; }
		// the node itself stands there now: its place in its parent's frame (the roots of the nodes are only moved, never turned), on the ground
		Entity& e = *tribe.m_Entities->Find(id);
		glm::vec3 origin(0.0f);
		if (e.Parent != NullEntity && tribe.m_Entities->Find(e.Parent)) origin = glm::vec3(tribe.m_Entities->WorldMatrix(e.Parent)[3]);
		const float ground = tribe.m_Ground ? tribe.m_Ground(d.At.x, d.At.y) : tribe.Config.Economy.CampY;
		e.Transform.Position = glm::vec3(d.At.x, ground, d.At.y) - origin;
		AttributeSet& at = node->Attributes();
		at.SetBase("Amount", 0.0f);   // it starts empty and gives after its usual regrowth (ResourceSystem::Update)
		if (at.Has("Regrow")) at.SetBase("Regrow", 0.0f);
		// the clan's senses: the nodes are listed once and their places kept (Tribe::RefreshSights): this one moved, so they are listed again,
		// and what the clan and its people remember of it is where it is now
		tribe.m_PosCache.erase(d.Target);
		tribe.m_SightsWorld = nullptr;
		tribe.m_Clan.Refresh(d.Target, d.At, 0, tribe.m_Time);
		for (TribeMember& o : tribe.m_Members) o.Mind.Memory.Refresh(d.Target, d.At, 0, tribe.m_Time);
		if (const auto held = tribe.m_Claims.All().find(d.Target); held != tribe.m_Claims.All().end() && held->second == m.Name) tribe.m_Claims.Release(d.Target);
		if (tribe.m_Outside.NodeMoved) tribe.m_Outside.NodeMoved(tribe, d.Target);   // T333: the other clans forget where it was
		tribe.Say(m, "plants " + d.Target + " at " + Fixed1(d.At.x) + ", " + Fixed1(d.At.y));
		d.Carry.clear();
		return true;
	}

	Transplant::Outcome Transplant::Step(Tribe& tribe, TribeMember& m, ColonyOrders::Mandate& d, std::string& why)
	{
		const std::string ended = m.Job;
		if (OwnJob(ended))
		{
			const bool failed = tribe.HasFailed(m);
			m.Job.clear();
			m.Detail.clear();
			m.Mind.Orders.clear();
			if (failed) ++d.Fails;
			else if (ended == "uproot" && d.Carry.empty())
			{
				if (!Uproot(tribe, m, d, why)) return Outcome::Failed;
				d.Fails = 0;
			}
			else if (ended == "plant" && !d.Carry.empty()) return Plant(tribe, m, d, why) ? Outcome::Done : Outcome::Failed;
		}
		const ResourceNodeDef* def = DefOf(tribe, d.Target);
		bool found = false;
		const glm::vec2 from = NodePos(tribe, d.Target, &found);
		if (!def || !found) { why = "the node is gone"; return Outcome::Failed; }
		if (d.Fails >= kMostFails) { why = d.Carry.empty() ? "cannot get to " + d.Target : "cannot get to " + Fixed1(d.At.x) + ", " + Fixed1(d.At.y); return Outcome::Failed; }
		const glm::vec2 here = tribe.Where(m);
		if (d.Carry.empty())
		{
			if (const auto held = tribe.m_Claims.All().find(d.Target); held != tribe.m_Claims.All().end() && held->second != m.Name) { why = held->second + " works " + d.Target; return Outcome::Failed; }
			if (const std::string held = Held(tribe, d.Target, from); !held.empty()) { why = held; return Outcome::Failed; }   // T333
			if (!tribe.Order(m, GoTo(StandAt(here, from), def->Transplant.Work))) { why = "cannot get to " + d.Target; return Outcome::Failed; }
			tribe.m_Claims.Claim(d.Target, m.Name);
			m.Job = "uproot";
		}
		else
		{
			if (m.Body->Bag().Count(def->Transplant.Item) < 1) { why = "it lost the " + def->Transplant.Item; return Outcome::Failed; }
			if (!tribe.Order(m, GoTo(StandAt(here, d.At), def->Transplant.Work))) { why = "cannot get to " + Fixed1(d.At.x) + ", " + Fixed1(d.At.y); return Outcome::Failed; }
			m.Job = "plant";
		}
		m.Detail = d.Target;
		++d.Steps;
		return Outcome::Going;
	}

	void Transplant::Abandon(Tribe& tribe, TribeMember& m, const ColonyOrders::Mandate& d)
	{
		if (d.Carry.empty() || !m.Body || !tribe.m_Items) return;   // (T333: the dead too: the cutting does not stay in the body's bag)
		const auto it = tribe.m_Defs.find(d.Carry);
		if (it == tribe.m_Defs.end() || it->second.Transplant.Item.empty()) return;
		if (m.Body->Bag().Remove(it->second.Transplant.Item, 1) < 1) return;
		if (const auto held = tribe.m_Claims.All().find(d.Target); held != tribe.m_Claims.All().end() && held->second == m.Name) tribe.m_Claims.Release(d.Target);
		if (m.Dead) tribe.TellClan("finds the " + it->second.Transplant.Item + " " + m.Name + " carried and puts " + d.Target + " back in the ground where it was");
		else tribe.Say(m, "puts " + d.Target + " back in the ground where it was");
	}

	// ------------------------------------------------------------------ T333: the other clans
	bool Transplant::Moving(const Tribe& tribe, const std::string& node)
	{
		for (const auto& [who, d] : tribe.m_Orders.All())
			if (d.Verb == "transplant" && d.Target == node) return true;
		return false;
	}

	std::string Transplant::Held(const Tribe& tribe, const std::string& node, const glm::vec2& at)
	{
		return tribe.m_Outside.NodeHeld ? tribe.m_Outside.NodeHeld(tribe, node, at) : std::string();
	}

	void Transplant::Forget(Tribe& tribe, const std::string& node)
	{
		tribe.m_PosCache.erase(node);
		tribe.m_SightsWorld = nullptr;   // (its senses list the nodes again, with the place it has now)
		tribe.m_Clan.Forget(node);
		for (TribeMember& o : tribe.m_Members) { o.Mind.Memory.Forget(node); o.Unreachable.erase(node); }
	}
}
