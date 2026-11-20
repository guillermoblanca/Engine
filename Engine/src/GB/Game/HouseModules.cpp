// PROTOTYPE of TALLERES.md section 10 (see HouseModules.h).
#include "GB/Game/HouseModules.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/GroundFootprint.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		std::string FenceRoot(const std::string& host) { return host + " Yard"; }

		float HostYaw(const Tribe& tribe, const EntityWorld& w, const std::string& host)
		{
			(void)tribe;
			const Entity* e = w.Find(w.FindByName(host));
			return e ? e->Transform.Rotation.y : 0.0f;
		}

		std::string Stamp(double t)
		{
			char b[48];
			std::snprintf(b, sizeof b, "%6.1f ", t);
			return b;
		}

		std::vector<std::string> TechsOf(const Tribe& tribe, const BuildingDef& d)
		{
			std::vector<std::string> out;
			if (!d.Tech.empty()) out.push_back(d.Tech);
			if (const TechTree* tree = tribe.Knowledge().Techs())
				for (const auto& [id, t] : tree->Techs)
					if (std::find(t.Buildings.begin(), t.Buildings.end(), d.Id) != t.Buildings.end() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
			return out;
		}
	}

	YardShape HouseModules::YardOf(const Tribe& tribe, const std::string& host)
	{
		if (!tribe.m_Entities) return {};
		for (const Tribe::Placed& p : tribe.Placements())
			if (p.Name == host)
			{
				if (!p.State.Done || !p.Def->Yard.Any()) return {};
				return ComputeYard(*p.Def, p.Pos, HostYaw(tribe, *tribe.m_Entities, host), DefaultYardRules());
			}
		return {};
	}

	int HouseModules::Count(const std::string& host) const
	{
		int n = 0;
		for (const auto& [site, e] : m_Sites) if (e.Host == host) ++n;
		return n;
	}

	const HouseModules::Entry* HouseModules::Of(const std::string& site) const
	{
		const auto it = m_Sites.find(site);
		return it == m_Sites.end() ? nullptr : &it->second;
	}

	std::vector<HouseModules::SlotView> HouseModules::Slots(const Tribe& tribe, const std::string& host) const
	{
		std::vector<SlotView> out;
		const YardShape yard = YardOf(tribe, host);
		if (!yard.Valid) return out;
		for (const YardSlot& s : yard.Slots)
		{
			SlotView v;
			v.Slot = s;
			for (const auto& [site, e] : m_Sites)
				if (e.Host == host && e.Slot == s.Id)
				{
					v.Site = site;
					for (const Tribe::Placed& p : tribe.Placements()) if (p.Name == site) { v.Module = p.Def->Id; v.Done = p.State.Done; }
				}
			out.push_back(v);
		}
		return out;
	}

	std::vector<HouseModules::Offer> HouseModules::Offers(const Tribe& tribe, const std::string& host) const
	{
		std::vector<Offer> out;
		const auto* defs = tribe.Buildings();
		if (!defs) return out;
		std::string hostId;
		for (const Tribe::Placed& p : tribe.Placements()) if (p.Name == host) hostId = p.Def->Id;
		const std::vector<SlotView> slots = Slots(tribe, host);
		for (const auto& [id, d] : *defs)
		{
			if (!d.Module.Any() || (!d.Module.Hosts.empty() && std::find(d.Module.Hosts.begin(), d.Module.Hosts.end(), hostId) == d.Module.Hosts.end())) continue;
			Offer o;
			o.Id = id;
			o.Known = tribe.CanBuild(id);
			if (!o.Known) o.Techs = TechsOf(tribe, d);
			for (const SlotView& s : slots)
				if (s.Site.empty() && std::find(d.Module.Slots.begin(), d.Module.Slots.end(), s.Slot.Kind) != d.Module.Slots.end()) o.Room = true;
			out.push_back(o);
		}
		return out;
	}

	bool HouseModules::UseSpot(const Tribe& tribe, const std::string& site, glm::vec2& at) const
	{
		const Entry* e = Of(site);
		if (!e) return false;
		for (const YardSlot& s : YardOf(tribe, e->Host).Slots) if (s.Id == e->Slot) { at = s.Use; return true; }
		return false;
	}

	HouseModules::Reply HouseModules::Add(Tribe& tribe, const std::string& host, const std::string& module, bool now)
	{
		const auto* defs = tribe.Buildings();
		if (!defs || !tribe.m_Entities || !tribe.m_Gameplay) return { false, "the clan has no buildings" };
		const auto def = defs->find(module);
		if (def == defs->end() || !def->second.Module.Any()) return { false, "no module '" + module + "'" };
		const BuildingDef& d = def->second;
		const YardShape yard = YardOf(tribe, host);
		if (!yard.Valid) return { false, "no standing house of the clan with a yard called '" + host + "'" };
		std::string hostId;
		for (const Tribe::Placed& p : tribe.Placements()) if (p.Name == host) hostId = p.Def->Id;
		if (!d.Module.Hosts.empty() && std::find(d.Module.Hosts.begin(), d.Module.Hosts.end(), hostId) == d.Module.Hosts.end()) return { false, "a " + d.Name + " does not go in the yard of a " + hostId };
		if (!tribe.CanBuild(module))
		{
			std::string techs;
			for (const std::string& t : TechsOf(tribe, d)) techs += (techs.empty() ? "" : " or ") + t;
			return { false, "the clan does not know how to raise a " + d.Name + " yet" + (techs.empty() ? std::string() : ": it needs " + techs) };
		}
		const int max = Max(tribe, host);
		if (Count(host) >= max) return { false, host + " has " + std::to_string(max) + " modules already, its most" };
		std::string why = "no free slot of its kind (" , kinds;
		for (const std::string& k : d.Module.Slots) kinds += (kinds.empty() ? "" : ", ") + k;
		why += kinds + ")";
		std::vector<const YardSlot*> order;   // (the kinds in the module's order of preference, each kind's slots from the gate back)
		for (const std::string& k : d.Module.Slots) for (const YardSlot& s : yard.Slots) if (s.Kind == k) order.push_back(&s);
		for (const YardSlot* slotOf : order)
		{
			const YardSlot& s = *slotOf;
			if (std::any_of(m_Sites.begin(), m_Sites.end(), [&](const auto& kv) { return kv.second.Host == host && kv.second.Slot == s.Id; })) continue;
			std::string site;
			const float yaw = std::fabs(s.Yaw) < 0.01f ? 360.0f : s.Yaw;   // (StartSite reads 0 as "not turned" and keeps the planner's gap round it)
			m_Placing = host;   // (T367, Y-2: its own yard is not in the way of its module)
			const std::string error = tribe.StartSite(module, s.Pos, &site, yaw);
			m_Placing.clear();
			if (!error.empty() || site.empty()) { why = s.Id + ": " + error; continue; }
			m_Sites[site] = { host, s.Id };
			if (!now && !m_Planning) tribe.Designations().QueueSite(tribe, site);   // (the player's plans are worked in the order of its queue; T367: not the planner's)
			std::string text = site + " in " + s.Id + " of " + host;
			if (now)   // (as Tribe::RaiseBuilding: each phase its materials, then its work)
			{
				AbilitySystemComponent* a = tribe.m_Gameplay->Find(site);
				for (size_t phase = 0; a && phase <= d.Phases.size(); ++phase)
				{
					const SiteState st = ReadSite(*a, d);
					if (st.Done) break;
					for (const auto& [item, k] : st.Missing) DeliverToSite(*a, d, item, k);
					WorkOnSite(*a, d, std::max(st.WorkNeeded, 0.0f) + 1.0f);
				}
				if (a) if (Entity* e = tribe.m_Entities->Find(tribe.m_Entities->FindByName(site))) e->Gameplay = a->ToComponent();
				if (a) SyncSiteVisuals(*tribe.m_Entities, site, d, ReadSite(*a, d));
				tribe.m_PlacedAt = -1.0;
				tribe.ForgetPlacements();
				text += ", standing";
			}
			else text += ": the clan will bring its materials and build it";
			m_Next = 0.0;   // (the fence at the next step)
			Step(tribe);
			return { true, "module " + text };
		}
		return { false, "cannot: " + why };
	}

	HouseModules::Reply HouseModules::Remove(Tribe& tribe, const std::string& host, const std::string& what)
	{
		std::string site;
		for (const auto& [s, e] : m_Sites)
			if (e.Host == host && (s == what || e.Slot == what)) site = s;
		if (site.empty())
			for (const Tribe::Placed& p : tribe.Placements())
				if (p.Def->Id == what) if (const Entry* e = Of(p.Name); e && e->Host == host) site = p.Name;
		if (site.empty()) return { false, "no module '" + what + "' in the yard of " + host };
		std::string text;
		bool done = false;
		for (const Tribe::Placed& p : tribe.Placements()) if (p.Name == site) done = p.State.Done;
		if (!done)
		{
			std::map<std::string, int> back;
			if (tribe.m_Gameplay->Find(site))
				if (const std::string error = tribe.CancelSite(site, &back); !error.empty()) return { false, "cannot: " + error };
			for (const auto& [item, n] : back) text += (text.empty() ? "" : ", ") + std::to_string(n) + " " + item;
		}
		else
		{
			// a module that stands: all it cost goes back to the store (TALLERES 10.3, refundOnRemove 1.0) and it is gone
			const BuildingDef* d = nullptr;
			for (const Tribe::Placed& p : tribe.Placements()) if (p.Name == site) d = p.Def;
			if (d && tribe.m_Items)
				for (const auto& [item, n] : d->TotalNeeds())
					if (tribe.m_Items->Find(item)) { const int in = tribe.m_Store.Add(*tribe.m_Items, item, n); if (in > 0) text += (text.empty() ? "" : ", ") + std::to_string(in) + " " + item; }
			if (const EntityId e = tribe.m_Entities->FindByName(site); e != NullEntity) tribe.m_Entities->Destroy(e);
			tribe.m_Gameplay->Remove(site);
			tribe.m_Designations.SiteGone(site);
			tribe.m_PlacedAt = -1.0;
			tribe.ForgetPlacements();
			tribe.m_Log.push_back(Stamp(tribe.m_Time) + "the clan pulls down " + site + " (back to the store: " + (text.empty() ? std::string("nothing") : text) + ")");
		}
		m_Sites.erase(site);
		tribe.m_RecipeChoices.Forget(tribe, site);
		m_Next = 0.0;   // (T367: the fence and the reserved ground at the next step)
		return { true, "module " + site + " of " + host + " pulled down; back to the store: " + (text.empty() ? std::string("nothing") : text) };
	}

	std::string HouseModules::Status(const Tribe& tribe, const std::string& host) const
	{
		const YardShape yard = YardOf(tribe, host);
		if (!yard.Valid) return "no standing house of the clan with a yard called '" + host + "'";
		char b[64];
		std::snprintf(b, sizeof b, "%.1f", static_cast<double>(yard.Length));
		const YardClosure* closure = DefaultYardRules().Closure(ClosureOf(host));
		std::string s = host + ": yard, fence " + b + " m" + (closure ? " (" + closure->Id + ")" : std::string(" (no closure)"));
		s += "; modules " + std::to_string(Count(host)) + "/" + std::to_string(Max(tribe, host));
		std::string used, free;
		int nfree = 0;
		for (const SlotView& v : Slots(tribe, host))
		{
			if (!v.Site.empty()) used += (used.empty() ? "" : ", ") + v.Slot.Id + " " + v.Site + " (" + v.Module + (v.Done ? ", standing" : ", site") + ")";
			else { free += (free.empty() ? "" : " ") + v.Slot.Id; ++nfree; }
		}
		if (!used.empty()) s += " (" + used + ")";
		s += "; slots " + std::to_string(yard.Slots.size()) + ", free " + std::to_string(nfree) + ": " + free;
		return s;
	}

	void HouseModules::Step(Tribe& tribe)
	{
		if (m_Sites.empty() && m_Fenced.empty() && m_Houses.empty()) return;
		if (tribe.m_Time < m_Next) return;
		m_Next = tribe.m_Time + 1.0;
		if (!tribe.m_Entities || !tribe.m_Gameplay) return;
		// the modules whose site is gone (called off, pulled down by another way)
		for (auto it = m_Sites.begin(); it != m_Sites.end();)
		{
			if (!tribe.m_Gameplay->Find(it->first) && tribe.m_Entities->FindByName(it->first) == NullEntity) { tribe.m_RecipeChoices.Forget(tribe, it->first); it = m_Sites.erase(it); }
			else ++it;
		}
		// T367: the houses that are gone (pulled down, or turned into another building that is no house of a line) forget their size and work
		for (auto it = m_Houses.begin(); it != m_Houses.end();)
		{
			if (!tribe.m_Gameplay->Find(it->first) && tribe.m_Entities->FindByName(it->first) == NullEntity) it = m_Houses.erase(it);
			else ++it;
		}
		// T367 (Y-3): a house of a size of its own building (M) is drawn as its model scaled to the size's footprint, until A51 models it
		for (const Tribe::Placed& p : tribe.Placements())
			if (const auto h = m_Houses.find(p.Name); h != m_Houses.end() || p.Def->Yard.Any())
				if (Entity* e = tribe.m_Entities->Find(tribe.m_Entities->FindByName(p.Name)))
				{
					const BuildingDef* base = tribe.m_Buildings ? &tribe.m_Buildings->at(p.Def->Id) : nullptr;
					glm::vec3 scale(1.0f);
					if (base && base != p.Def && base->Footprint.x > 0.0f && base->Footprint.y > 0.0f) scale = { p.Def->Footprint.x / base->Footprint.x, 1.0f, p.Def->Footprint.y / base->Footprint.y };
					if (h != m_Houses.end() || scale != glm::vec3(1.0f))
						if (glm::any(glm::greaterThan(glm::abs(e->Transform.Scale - scale), glm::vec3(1e-4f)))) e->Transform.Scale = scale;
				}
		// the fences (T367f): one round each house with a closure, of its closure's look; none round the others
		std::map<std::string, int> hosts;
		for (const auto& [house, h] : m_Houses) if (h.Closure > 0) hosts[house] = h.Closure;
		for (const auto& [site, e] : m_Sites)   // (the prototype's free fence of a yard with modules, brought back by an older save: gone)
			if (!hosts.count(e.Host) && !m_Fenced.count(e.Host)) TakeDownFence(tribe, e.Host);
		for (auto it = m_Fenced.begin(); it != m_Fenced.end();)
		{
			if (!hosts.count(it->first)) { TakeDownFence(tribe, it->first); it = m_Fenced.erase(it); }
			else ++it;
		}
		for (const auto& [host, level] : hosts)
		{
			if (m_Fenced.count(host)) continue;
			TakeDownFence(tribe, host);   // (a level changed, or the house grew: the fence is made again)
			const YardShape yard = YardOf(tribe, host);
			if (!yard.Valid) continue;   // (the house is gone or not standing: its modules stay where they are)
			if (tribe.m_Entities->FindByName(FenceRoot(host)) == NullEntity) BuildFence(tribe, host);
			m_Fenced[host] = yard.Posts.size();
		}
	}

	void HouseModules::BuildFence(Tribe& tribe, const std::string& host) const
	{
		EntityWorld& w = *tribe.m_Entities;
		const YardShape yard = YardOf(tribe, host);
		if (!yard.Valid || yard.Posts.size() < 2) return;
		const YardClosure* closure = DefaultYardRules().Closure(ClosureOf(host));
		const YardFenceLook& look = closure ? closure->Look : DefaultYardRules().Fence;
		auto ground = [&](const glm::vec2& p) { return tribe.m_Ground ? tribe.m_Ground(p.x, p.y) : tribe.Config.Economy.CampY; };
		const EntityId root = w.Create(FenceRoot(host));
		auto piece = [&](const std::string& name, const glm::vec3& pos, const glm::vec3& scale, float yaw, const glm::vec3& colour)
		{
			const EntityId id = w.Create(name, root);
			Entity* e = w.Find(id);
			e->Transform.Position = pos;
			e->Transform.Rotation = glm::vec3(0.0f, yaw, 0.0f);
			e->Transform.Scale = scale;
			e->Mesh.emplace();
			e->Mesh->Shape = MeshShape::Cube;
			e->Mesh->Size = 1.0f;
			e->Mesh->Albedo = colour;
			e->Mesh->Roughness = 0.95f;
		};
		const size_t n = yard.Posts.size();
		for (size_t i = 0; i < n; ++i)
		{
			const glm::vec2 p = yard.Posts[i];
			const bool gate = i == 0 || i + 1 == n;
			const float h = gate ? look.GatePostHeight : look.PostHeight, wdt = gate ? look.GatePostWidth : look.PostWidth;
			const glm::vec2 along = glm::normalize(yard.Posts[i + 1 < n ? i + 1 : i] - yard.Posts[i + 1 < n ? i : i - 1]);
			piece(host + " Yard Post " + std::to_string(i + 1), { p.x, ground(p) + 0.5f * h - 0.08f, p.y }, { wdt, h, wdt }, glm::degrees(std::atan2(-along.y, along.x)), look.PostColor);
		}
		for (size_t i = 0; i + 1 < n; ++i)
		{
			const glm::vec2 a = yard.Posts[i], b = yard.Posts[i + 1], mid = 0.5f * (a + b), d = b - a;
			const float len = glm::length(d);
			if (len < 0.05f) continue;
			const float base = 0.5f * (ground(a) + ground(b));
			if (look.Solid)   // (a wall: one block a bay, from the ground up)
			{
				piece(host + " Yard Wall " + std::to_string(i + 1), { mid.x, base + 0.5f * look.PostHeight - 0.08f, mid.y }, { len + 0.5f * look.PostWidth, look.PostHeight, look.PostWidth },
					glm::degrees(std::atan2(-d.y, d.x)), look.RailColor);
				continue;
			}
			for (size_t k = 0; k < look.Rails.size(); ++k)
				piece(host + " Yard Rail " + std::to_string(i + 1) + "." + std::to_string(k + 1), { mid.x, base + look.Rails[k], mid.y }, { len + 0.04f, look.RailWidth, look.RailWidth },
					glm::degrees(std::atan2(-d.y, d.x)), look.RailColor);
		}
	}

	void HouseModules::TakeDownFence(Tribe& tribe, const std::string& host)
	{
		if (!tribe.m_Entities) return;
		if (const EntityId root = tribe.m_Entities->FindByName(FenceRoot(host)); root != NullEntity) tribe.m_Entities->Destroy(root);
	}

	std::string HouseModules::ToJson() const
	{
		Json j = Json::object();
		for (const auto& [site, e] : m_Sites) j[site] = { { "host", e.Host }, { "slot", e.Slot } };
		return j.dump();
	}

	bool HouseModules::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		std::map<std::string, Entry> sites;
		for (const auto& [site, e] : j.items())
		{
			if (!e.is_object() || !e.contains("host") || !e["host"].is_string() || !e.contains("slot") || !e["slot"].is_string()) return false;
			sites[site] = { e["host"].get<std::string>(), e["slot"].get<std::string>() };
		}
		m_Sites = std::move(sites);
		m_Fenced.clear();
		m_Next = 0.0;
		return true;
	}
}
