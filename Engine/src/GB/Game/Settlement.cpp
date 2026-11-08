// The settlement of the tribe (H8, E9): what the clan builds, where, and how its members carry the materials and work the sites. These are members
// of Tribe (declared in Tribe.h) kept apart from Tribe.cpp, which only calls them from a few hooks: the planner from Update, the options from Decide,
// the orders from Execute, what a finished job leaves from Finish and the shelter from the needs. Without buildings (Tribe::SetBuildings) every one of
// them does nothing, so a tribe without them behaves exactly as before. See docs/ASENTAMIENTOS.md.
#include "GB/Game/Tribe.h"
#include "GB/World/Regions.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		// What `inv` holds of a site's missing material, its substitutes included (SiteState::Substitutes)
		int WithSubstitutes(const Inventory& inv, const SiteState& site, const std::string& item)
		{
			int n = inv.Count(item);
			if (const auto sub = site.Substitutes.find(item); sub != site.Substitutes.end()) for (const std::string& other : sub->second) n += inv.Count(other);
			return n;
		}

		std::string Underscored(std::string s)
		{
			std::replace(s.begin(), s.end(), ' ', '_');
			return s;
		}

		void Factor(UtilityOption& o, const char* name, float value) { o.Factors.push_back({ name, value }); }

		// What a building gives, for the log and the reports: "4 beds, station hearth, defense 1"...
		std::string Provides(const BuildingDef& d)
		{
			std::string s;
			char b[64];
			auto add = [&](const std::string& part) { s += (s.empty() ? "" : ", ") + part; };
			if (d.Shelter) add(std::to_string(d.Shelter) + " beds");
			if (!d.Store.empty()) add("a " + d.Store + " of the economy");
			if (!d.Station.empty()) add("station " + d.Station);
			if (d.StoreCap) add("+" + std::to_string(d.StoreCap) + " store cap");
			if (d.Keep < 1.0f) { std::snprintf(b, sizeof b, "food keeps x%.2f", static_cast<double>(1.0f / d.Keep)); add(b); }
			if (d.Defense > 0.0f) { std::snprintf(b, sizeof b, "defense %.1f", static_cast<double>(d.Defense)); add(b); }
			if (d.Healing > 0.0f) { std::snprintf(b, sizeof b, "wounds heal x%.1f", static_cast<double>(1.0f + d.Healing)); add(b); }
			if (d.Culture > 0.0f) add("a place to meet");
			if (d.Shelter == 0 && d.Warmth > 0.0f && d.Aura > 0.0f) add("warmth around it");
			if (d.Well) add("a well");
			return s.empty() ? std::string("nothing yet") : s;
		}

		// Where a member stands to work a site: in front of it (the "work" point of BuildSite), a little to the side for each builder.
		glm::vec2 FrontOf(const glm::vec2& pos, const BuildingDef& def, float side = 0.0f) { return SiteFront(pos, def, side); }
	}

	SiteWeights ClanSiteWeights(const Tribe& tribe, const std::vector<std::string>& roles)
	{
		SiteWeights w;
		if (roles.empty()) return w;
		for (const auto& [kind, def] : tribe.NodeDefs())
		{
			float sum = 0.0f;
			for (const std::string& role : roles) sum += tribe.RoleWeight(role, kind);
			float want = sum / static_cast<float>(roles.size());
			if (tribe.IsFood(def.Item)) want *= 1.5f;   // the first thing a camp needs near
			if (want > 0.0f) w.Want[kind] = want;
		}
		return w;
	}

	CampSiteChooser SettlementSiteChooser(const Tribe& tribe, ResourceGround ground, std::function<std::vector<SiteResource>()> resources)
	{
		return [&tribe, ground = std::move(ground), resources](const CampSiteRequest& request, CampSite& site)
		{
			// what the clan knows: the nodes of its shared memory that still give (or grow back); a new clan: every node it is given
			std::vector<SiteResource> known;
			if (resources) known = resources();
			else
				for (const auto& [name, e] : tribe.ClanMemory().All())
				{
					const auto def = tribe.NodeDefs().find(e.Kind);
					if (def != tribe.NodeDefs().end() && (e.Amount != 0 || def->second.RegenSeconds > 0.0f)) known.push_back({ e.Kind, e.Pos });
				}
			std::vector<std::string> roles;
			const ITribeBody* walker = nullptr;
			for (const TribeMember& m : tribe.Members())
				if (!m.Dead && m.Body) { roles.push_back(m.Role); if (!walker) walker = m.Body; }
			if ((!walker && !resources) || known.empty()) return false;
			if (roles.empty()) roles = tribe.PlanRoles(4);   // (a probe with no members: the trades a new clan of four would have)
			const SiteWeights weights = ClanSiteWeights(tribe, roles);
			const float gain = tribe.Config.Explore.Migration.MinGain;
			for (const SiteRating& r : RankSites(ground, known, weights))
			{
				if (glm::distance(r.At, request.From) < request.HomeRadius || r.Near.empty()) continue;   // a move, not a shuffle; and somewhere it has been
				const float worth = resources ? r.Total : tribe.SiteScore(r.At);   // H12's measure decides whether a move is worth it (a new clan: H8's own)
				if ((!resources && worth < request.HomeScore + gain) || (walker && !resources && !walker->CanReach(r.At))) continue;
				site.At = r.At;
				site.Score = worth;
				if (const RegionMap* regions = tribe.Regions()) site.Region = regions->RegionAt(r.At);
				char buf[160];
				std::snprintf(buf, sizeof buf, "the best place it knows (site %.2f: water %.2f, resources %.2f, flat %.2f), worth %.2f against %.2f at home", static_cast<double>(r.Total),
					static_cast<double>(r.Water), static_cast<double>(r.Resources), static_cast<double>(r.Flat), static_cast<double>(worth), static_cast<double>(request.HomeScore));
				site.Why = buf;
				return true;
			}
			return false;
		};
	}

	Tribe::PlacedList Tribe::Placements() const
	{
		if (m_Updating && m_PlacedOut) return { m_PlacedOut };
		auto list = std::make_shared<std::vector<Placed>>();
		std::vector<Placed>& out = *list;
		if (!m_Buildings || !m_Gameplay || !m_Entities) return { list };
		// The names and places are looked up again when the scene's systems change in number, when a site was started (StartSite) and every few
		// seconds besides (a world of kilometres has tens of thousands of systems, and a site does not move); the state is read each time.
		if (m_PlacedAt < 0.0 || m_Time - m_PlacedAt >= 5.0 || m_Time < m_PlacedAt || m_PlacedSystems != m_Gameplay->Count())
		{
			m_Placed.clear();
			for (const GameplayWorld::Tagged& building : m_Gameplay->WithTag("Building"))   // (the sites and buildings only: not the thousands of nodes)
			{
				const std::string& name = *building.Name;
				const EntityId id = m_Entities->FindByName(name);
				if (id == NullEntity) continue;
				const glm::vec4 p = m_Entities->WorldMatrix(id)[3];
				m_Placed.emplace_back(name, glm::vec2(p.x, p.z));
			}
			m_PlacedAt = m_Time;
			m_PlacedSystems = m_Gameplay->Count();
		}
		for (const auto& [name, pos] : m_Placed)
		{
			if (glm::distance(pos, Config.Economy.Camp) > Config.SettlementRadius) continue;   // left behind by a move of the camp (H12): not the clan's any more
			const AbilitySystemComponent* a = m_Gameplay->Find(name);
			const BuildingDef* def = a ? BuildingOf(*a, *m_Buildings) : nullptr;
			if (!def) continue;
			out.push_back({ name, def, pos, ReadSite(*a, *def) });
		}
		if (m_Updating) m_PlacedOut = list;
		return { list };
	}

	std::vector<Tribe::SettlementSite> Tribe::SettlementSites() const
	{
		std::vector<SettlementSite> out;
		for (const Placed& p : Placements())
			out.push_back({ p.Name, p.Def->Name, p.Pos, p.State, p.Def->Shelter, p.Def->Station, p.Def->Store });
		return out;
	}

	void Tribe::SetBuildings(const std::map<std::string, BuildingDef>* defs)
	{
		m_Buildings = defs;
		if (!defs) { m_Stations = nullptr; return; }
		// the economy's stations (H18): a finished building's, in front of it (the camp's own come first: EconomyRules::Production.CampStations)
		m_Stations = [this](const std::string& station, glm::vec2& where)
		{
			for (const Placed& p : Placements())
				if (p.State.Done && p.Def->Station == station) { where = FrontOf(p.Pos, *p.Def); return true; }
			return false;
		};
	}

	int Tribe::BuildingCount() const
	{
		int n = 0;
		for (const Placed& p : Placements()) if (p.State.Done) ++n;
		return n;
	}

	int Tribe::Beds() const
	{
		int beds = 0;
		for (const Placed& p : Placements()) if (p.State.Done) beds += p.Def->Shelter;
		return beds;
	}

	bool Tribe::StationAvailable(const std::string& station) const
	{
		if (station.empty()) return true;
		const std::vector<std::string>& camp = Config.Econ.Production.CampStations;
		if (std::find(camp.begin(), camp.end(), station) != camp.end()) return true;
		for (const Placed& p : Placements()) if (p.State.Done && p.Def->Station == station) return true;
		return false;
	}

	std::string Tribe::CraftStation(const std::string& recipe) const
	{
		const Recipe* r = m_Recipes ? m_Recipes->Find(recipe) : nullptr;
		return r && StationAvailable(r->Station) ? r->Station : std::string();
	}

	glm::vec2 Tribe::StationSpot(const TribeMember& m, const std::string& station) const
	{
		if (!station.empty())
			for (const Placed& p : Placements())
				if (p.State.Done && p.Def->Station == station)
					return FrontOf(p.Pos, *p.Def, 0.6f * (static_cast<float>(IndexOf(m) % 3) - 1.0f));
		return CampSpot(m);   // the camp's improvised station (Config.Econ.Production.CampStations)
	}

	const Tribe::Household* Tribe::HouseholdOf(const std::string& member) const
	{
		for (const Household& h : m_Households)
			if (std::find(h.Members.begin(), h.Members.end(), member) != h.Members.end()) return &h;
		return nullptr;
	}

	const Tribe::Household* Tribe::HouseholdAt(const std::string& building) const
	{
		for (const Household& h : m_Households) if (!h.Home.empty() && h.Home == building) return &h;
		return nullptr;
	}

	void Tribe::UpdateHouseholds()
	{
		std::set<std::string> alive;
		for (const TribeMember& m : m_Members) if (!m.Dead) alive.insert(m.Name);
		// the key of a member's household: its couple's first name; else its mother's (or father's) household; else its own name
		std::function<std::string(const std::string&, int)> key = [&](const std::string& name, int depth) -> std::string
		{
			const Person* p = m_Society.Find(name);
			if (!p || depth > 6) return name;
			if (!p->Partner.empty() && alive.count(p->Partner)) return std::min(name, p->Partner);
			if (!p->Mother.empty() && alive.count(p->Mother)) return key(p->Mother, depth + 1);
			if (!p->Father.empty() && alive.count(p->Father)) return key(p->Father, depth + 1);
			return name;
		};
		std::map<std::string, Household> by;
		for (const std::string& name : alive)
		{
			Household& h = by[key(name, 0)];
			h.Id = key(name, 0);
			h.Members.push_back(name);
		}
		// the houses: kept by their household while it lives and the house stands; a free one goes to the biggest household without one
		std::set<std::string> shelters;
		for (const Placed& p : Placements()) if (p.Def->Shelter > 0) shelters.insert(p.Name);   // (a house being built is already its household's)
		// N1c: a better house that stands replaces the one it was built for: the household moves, the old house comes down
		for (auto it = m_Upgrades.begin(); it != m_Upgrades.end();)
		{
			bool standing = false, gone = true;
			for (const Placed& p : Placements())
			{
				if (p.Name == it->first) { gone = false; standing = p.State.Done; }
			}
			if (gone) { it = m_Upgrades.erase(it); continue; }
			if (!standing) { ++it; continue; }
			const std::string old = it->second;
			if (m_Entities)
				if (const EntityId e = m_Entities->FindByName(old); e != NullEntity) m_Entities->Destroy(e);
			if (m_Gameplay) m_Gameplay->Remove(old);
			m_Homes.erase(old);
			shelters.erase(old);
			ForgetPlacements();
			char t[48];
			std::snprintf(t, sizeof t, "%6.1f ", m_Time);
			m_Log.push_back(std::string(t) + (m_Homes.count(it->first) ? m_Homes[it->first] + "'s household" : std::string("a household")) + " moves into " + it->first + " and pulls down " + old);
			it = m_Upgrades.erase(it);
		}
		for (auto it = m_Homes.begin(); it != m_Homes.end();)
			it = !shelters.count(it->first) || !by.count(it->second) ? m_Homes.erase(it) : std::next(it);
		for (const auto& [building, id] : m_Homes)   // (while its better house is being built, a household lives in the one that stands)
			if (by[id].Home.empty() || !m_Upgrades.count(building)) by[id].Home = building;
		std::vector<Household*> homeless;
		for (auto& [id, h] : by) if (h.Home.empty()) homeless.push_back(&h);
		std::stable_sort(homeless.begin(), homeless.end(), [](const Household* a, const Household* b) { return a->Members.size() > b->Members.size(); });
		size_t next = 0;
		for (const std::string& building : shelters)
		{
			if (m_Homes.count(building) || next >= homeless.size()) continue;
			m_Homes[building] = homeless[next]->Id;
			homeless[next]->Home = building;
			++next;
		}
		m_Households.clear();
		for (auto& [id, h] : by) m_Households.push_back(std::move(h));
		for (auto it = m_Econ.Pantries.begin(); it != m_Econ.Pantries.end();)   // N1b: a household that is gone leaves its pantry to the clan
		{
			if (by.count(it->first)) { ++it; continue; }
			if (m_Items) for (const ItemSlot& s : std::vector<ItemSlot>(it->second.Stacks())) it->second.Transfer(*m_Items, m_Store, s.Id, s.Count);
			it = m_Econ.Pantries.erase(it);
		}
	}

	float Tribe::HouseholdWork(const Household& h, const glm::vec2& at) const
	{
		float v = 0.0f;
		for (const auto& [name, e] : m_Clan.All())
		{
			const auto def = m_Defs.find(e.Kind);
			if (def == m_Defs.end() || glm::distance(e.Pos, at) > 90.0f) continue;
			float like = 0.0f;
			for (const std::string& who : h.Members) if (const TribeMember* m = FindMember(who)) like += MemberWeight(*m, e.Kind);
			like /= static_cast<float>(std::max<size_t>(1, h.Members.size()));
			const float amount = static_cast<float>(e.Amount >= 0 ? e.Amount : def->second.Capacity) * (def->second.RegenSeconds > 0.0f ? 2.0f : 1.0f);
			v += like * amount * Falloff(glm::distance(e.Pos, at), 25.0f);
		}
		return v;
	}

	glm::vec2 Tribe::SleepSpot(const TribeMember& m) const
	{
		auto bed = [](const Placed& p, int slot)
		{
			const float x = (-0.5f + (static_cast<float>(slot) + 0.5f) / static_cast<float>(p.Def->Shelter)) * 0.7f * p.Def->Footprint.x;
			return p.Pos + glm::vec2(x, 0.0f);
		};
		// N1: its bed in its household's house
		if (const Household* h = HouseholdOf(m.Name); h && !h->Home.empty())
			for (const Placed& p : Placements())
				if (p.Name == h->Home && p.State.Done && p.Def->Shelter > 0)
				{
					const int slot = static_cast<int>(std::find(h->Members.begin(), h->Members.end(), m.Name) - h->Members.begin());
					if (slot < p.Def->Shelter) return bed(p, slot);
				}
		// The beds nobody owns go to the living members in order; who has none sleeps at its place at the camp.
		int slot = 0;
		for (const TribeMember& o : m_Members)
		{
			if (&o == &m) break;
			if (!o.Dead) ++slot;
		}
		for (const Placed& p : Placements())
		{
			if (!p.State.Done || p.Def->Shelter <= 0 || m_Homes.count(p.Name)) continue;
			if (slot < p.Def->Shelter)
			{
				const float x = (-0.5f + (static_cast<float>(slot) + 0.5f) / static_cast<float>(p.Def->Shelter)) * 0.7f * p.Def->Footprint.x;
				return p.Pos + glm::vec2(x, 0.0f);
			}
			slot -= p.Def->Shelter;
		}
		return CampSpot(m);
	}

	// What the finished buildings give the people in and around them: rest and warmth to who sleeps under a roof (one roof), and within a building's
	// aura its warmth (a building without beds: the communal hearth) and its company (the hearth, the totem: a meeting place, H10).
	void Tribe::ShelterNeeds(TribeMember& m, float dt)
	{
		if (!m_Buildings) return;
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		if (!asc) return;
		const glm::vec2 pos = Where(m);
		bool roof = m.Job != "sleep";
		for (const Placed& p : Placements())
		{
			if (!p.State.Done) continue;
			const BuildingDef& d = *p.Def;
			const float distance = glm::distance(pos, p.Pos);
			if (!roof && d.Shelter > 0 && distance < d.Radius() + 0.6f)
			{
				roof = true;
				if (d.Rest > 0.0f) SatisfyNeed(*asc, NeedName::Rest, d.Rest * dt);
				if (d.Warmth > 0.0f) SatisfyNeed(*asc, NeedName::Warmth, d.Warmth * dt);
				continue;
			}
			if (d.Aura <= 0.0f || distance > d.Reach()) continue;
			if (d.Shelter == 0 && d.Warmth > 0.0f) SatisfyNeed(*asc, NeedName::Warmth, d.Warmth * dt);
			if (d.Culture > 0.0f) SatisfyNeed(*asc, NeedName::Company, d.Culture * dt);
		}
	}

	float Tribe::Defense() const
	{
		float d = 0.0f;
		for (const Placed& p : Placements()) if (p.State.Done) d += p.Def->Defense;
		return d;
	}

	float Tribe::HealingAt(const glm::vec2& at) const
	{
		float h = 1.0f;
		if (!m_Buildings) return h;
		for (const Placed& p : Placements())
			if (p.State.Done && p.Def->Healing > 0.0f && glm::distance(at, p.Pos) <= p.Def->Reach()) h += p.Def->Healing;
		return h;
	}

	glm::vec2 Tribe::RecoverSpot(const TribeMember& m) const
	{
		if (m_Buildings)
			for (const Placed& p : Placements())
				if (p.State.Done && p.Def->Healing > 0.0f) return FrontOf(p.Pos, *p.Def, 0.7f * (static_cast<float>(IndexOf(m) % 3) - 1.0f));
		return CampSpot(m);
	}

	int Tribe::StoreBonus() const
	{
		int n = 0;
		if (!m_Buildings) return n;
		for (const Placed& p : Placements()) if (p.State.Done) n += p.Def->StoreCap;
		return n;
	}

	float Tribe::StoreKeep() const
	{
		float k = 1.0f;
		if (!m_Buildings) return k;
		for (const Placed& p : Placements()) if (p.State.Done) k = std::min(k, p.Def->Keep);
		return k;
	}

	int Tribe::InFlight(const std::string& site, const std::string& item) const
	{
		const SiteState* state = nullptr;   // (what is carried for it may be a substitute)
		for (const Placed& p : Placements()) if (p.Name == site) state = &p.State;
		int n = 0;
		for (const TribeMember& o : m_Members)
			if (o.Job == "deliver" && o.Detail == site && o.Body) n += state ? WithSubstitutes(o.Body->Bag(), *state, item) : o.Body->Bag().Count(item);
		return n;
	}

	std::map<std::string, int> Tribe::SiteDemand() const
	{
		std::map<std::string, int> out;
		for (const Placed& p : Placements())
		{
			if (p.State.Done) continue;
			for (const auto& [item, n] : p.State.Missing)
				if (const int left = n - InFlight(p.Name, item); left > 0)
				{
					out[item] += left;
					if (const auto sub = p.State.Substitutes.find(item); sub != p.State.Substitutes.end())   // (what can stand in for it is wanted too)
						for (const std::string& other : sub->second) out[other] += left;
				}
		}
		return out;
	}

	std::vector<BuildingNeed> Tribe::BuildingNeeds(bool anyTech) const
	{
		std::vector<BuildingNeed> none;
		if (!m_Buildings || !m_Items || !m_Recipes || m_Members.empty()) return none;
		// first the tools and the campfire, as before there were buildings; but a roof is wanted from the first day (its stone, branches and reeds
		// are gathered by hand): without them only the buildings that give beds are planned
		const bool sheltersOnly = !HasFire() || !ToolsReady();
		SettlementFacts facts;
		facts.Buildings = m_Buildings;
		for (const Placed& p : Placements()) facts.Sites.push_back({ p.Def, p.Pos, p.State.Done });
		int open = 0;
		for (const PlannerSite& p : facts.Sites) if (!p.Done) ++open;
		for (const TribeMember& m : m_Members) if (!m.Dead) ++facts.Alive;
		facts.MaxOpenSites = Config.Econ.Construction.SitesFor(facts.Alive);
		if (anyTech) facts.MaxOpenSites = open + 1;   // (what it would want next, whatever is open now)
		if (facts.Alive == 0 || open >= facts.MaxOpenSites) return none;
		if (m_Combat)
			for (const TribeMember& m : m_Members)   // H14: members with wounds
				if (const Combatant* c = m.Dead ? nullptr : m_Combat->Find(m.Name); c && !c->Wounds.empty()) ++facts.Wounded;
		for (const auto& [item, n] : m_Econ.Spoiled) if (IsFood(item)) facts.Spoiled += n;   // H18: food the store lost to spoilage
		facts.Raided = m_Conflict.Raided;
		facts.StoreCap = StoreCap();
		facts.MembersPerWell = m_DigWell ? Config.Economy.MembersPerWell : 0;
		if (facts.MembersPerWell > 0 && m_Gameplay && m_Entities)
			for (const auto& w : m_Gameplay->WithTag("Interactive.Well"))
				if (const EntityId id = m_Entities->FindByName(*w.Name); id != NullEntity)   // (where it stands in the world: the scene's first well is a child)
					if (const glm::vec3 at = m_Entities->WorldMatrix(id)[3]; glm::distance(glm::vec2(at.x, at.z), Config.Economy.Camp) <= VillageReach()) ++facts.Wells;
		for (const ItemSlot& s : m_Store.Stacks())
			if (m_Store.Count(s.Id) >= facts.StoreCap - 2 && (facts.FullItem.empty() || s.Id < facts.FullItem)) facts.FullItem = s.Id;
		if (facts.FullItem.empty() && m_Store.UsedSlots() >= m_Store.Slots() - 2)   // every slot taken: as full as an item at its cap (the planner wants a storage)
			for (const ItemSlot& s : m_Store.Stacks())
				if (facts.FullItem.empty() || m_Store.Count(s.Id) > m_Store.Count(facts.FullItem)) facts.FullItem = s.Id;
		if (!facts.FullItem.empty()) facts.FullCount = m_Store.Count(facts.FullItem);
		for (const NeedDef& n : Config.DataNeeds.Needs)   // V5: how short the living are of what a need of the packs asks of a building
		{
			if (n.Provision.empty() || !DataNeedActive(n)) continue;
			float shortBy = 0.0f;
			int living = 0;
			for (const TribeMember& m : m_Members)
				if (!m.Dead && m.Body) { ++living; shortBy += std::max(0.0f, n.Threshold + 20.0f - DataNeedLevel(m, n)) / std::max(1.0f, n.Threshold + 20.0f); }
			if (living > 0) facts.Wanting[n.Provision] = std::max(facts.Wanting[n.Provision], shortBy / static_cast<float>(living));
		}
		facts.Known = [this, anyTech](const BuildingDef& def) { return anyTech ? CultureAllows(def) : ClanKnowsBuilding(def); };   // (a culture is not a technology to research)
		// only what the clan can pay: every material is in the store, some node of the land gives it or a recipe the clan knows makes it where it can
		facts.Payable = [this](const BuildingDef& def)
		{
			for (const auto& [item, n] : def.TotalNeeds())
			{
				bool from = m_Store.Count(item) > 0;
				for (const auto& [kind, node] : m_Defs) from = from || node.Item == item;
				for (const std::string& rid : m_Recipes->Ids())
				{
					if (from) break;
					const Recipe& r = *m_Recipes->Find(rid);
					from = r.Output == item && ClanKnows(r) && StationAvailable(r.Station);
				}
				if (!from) return false;
			}
			return true;
		};
		facts.StationThere = [this](const std::string& station) { return StationAvailable(station); };
		facts.StationIsImprovised = [this](const std::string& station)
		{
			const std::vector<std::string>& camp = Config.Econ.Production.CampStations;
			return std::find(camp.begin(), camp.end(), station) != camp.end();
		};
		facts.RecipeNeeding = [this](const std::string& station)
		{
			// a recipe needs the station: the tool of a role here, or one whose inputs the store holds
			for (const std::string& rid : m_Recipes->Ids())
			{
				const Recipe& r = *m_Recipes->Find(rid);
				if (r.Station != station) continue;
				bool tool = false;
				for (const TribeMember& m : m_Members)
					if (const auto role = Config.Roles.find(m.Role); role != Config.Roles.end() && role->second.Tool == r.Output) tool = true;
				const bool inputs = !r.Inputs.empty() && std::all_of(r.Inputs.begin(), r.Inputs.end(), [&](const auto& in) { return m_Store.Count(in.first) >= in.second; });
				if (tool || inputs) return rid;
			}
			return std::string();
		};
		std::vector<BuildingNeed> needs = PlanBuildingNeeds(facts);
		if (sheltersOnly)
			needs.erase(std::remove_if(needs.begin(), needs.end(), [&](const BuildingNeed& n) { return m_Buildings->at(n.Building).Shelter <= 0; }), needs.end());
		return needs;
	}

	std::string Tribe::StartSite(const std::string& building, const glm::vec2& at, std::string* name)
	{
		if (!m_Buildings || !m_Entities || !m_Gameplay) return "the tribe has no buildings";
		const auto def = m_Buildings->find(building);
		if (def == m_Buildings->end()) return "no building '" + building + "'";
		std::string n;
		for (int i = 1; n.empty(); ++i)
		{
			const std::string candidate = def->second.Name + " " + std::to_string(i);
			if (m_Entities->FindByName(candidate) == NullEntity && !m_Gameplay->Find(candidate)) n = candidate;
		}
		const float y = m_Ground ? m_Ground(at.x, at.y) : Config.Economy.CampY;
		const EntityId id = BuildSite(*m_Entities, def->second, n, { at.x, y, at.y });
		AbilitySystemComponent* a = m_Gameplay->Create(n);
		a->FromComponent(*m_Entities->Find(id)->Gameplay);
		m_PlacedAt = -1.0;   // look again
		ForgetPlacements();
		std::string needs;
		for (const auto& [item, k] : def->second.TotalNeeds()) needs += (needs.empty() ? "" : ", ") + std::to_string(k) + " " + item;
		char t[48];
		std::snprintf(t, sizeof t, "%6.1f ", m_Time);
		m_Log.push_back(std::string(t) + "the clan starts " + n + " at " + std::to_string(static_cast<int>(at.x)) + ", " + std::to_string(static_cast<int>(at.y)) + " (" + needs + ")");
		if (name) *name = n;
		return {};
	}

	std::string Tribe::RaiseBuilding(const std::string& building, const glm::vec2& at, float yawDegrees, std::string* name)
	{
		std::string n;
		if (const std::string error = StartSite(building, at, &n); !error.empty()) return error;
		const BuildingDef& def = m_Buildings->at(building);
		if (Entity* e = m_Entities->Find(m_Entities->FindByName(n))) e->Transform.Rotation.y = yawDegrees;
		AbilitySystemComponent* a = m_Gameplay->Find(n);
		for (size_t phase = 0; a && phase <= def.Phases.size(); ++phase)   // each phase: its materials, then its work
		{
			const SiteState s = ReadSite(*a, def);
			if (s.Done) break;
			for (const auto& [item, k] : s.Missing) DeliverToSite(*a, def, item, k);
			WorkOnSite(*a, def, std::max(s.WorkNeeded, 0.0f) + 1.0f);
		}
		if (!a || !ReadSite(*a, def).Done) return n + " could not be finished";
		if (Entity* e = m_Entities->Find(m_Entities->FindByName(n))) e->Gameplay = a->ToComponent();   // (the entity's copy too: a host that syncs from it keeps the finished state)
		SyncSiteVisuals(*m_Entities, n, def, ReadSite(*a, def));
		if (!m_Log.empty()) m_Log.back() += " (it already stands)";
		if (name) *name = n;
		return {};
	}

	// A building is locked by the technologies that list it (TechDef::Buildings, by id or by name) and by its own `tech`. With technologies (SetTechs) the clan
	// can build it when a living member knows one of them; without, the check of SetTechCheck decides (none: a locked building is never planned).
	bool Tribe::CultureAllows(const BuildingDef& def) const
	{
		if (!def.Rank.empty() && !m_RanksReached.count(def.Rank)) return false;   // V7: a building of a rank (the town hall of a town)
		// V4: a building of a culture: only where the clan's culture holds it (a positive threshold asks at least that much, a negative one at most)
		for (const auto& [value, threshold] : def.Values)
		{
			const auto it = m_Culture.find(value);
			const int have = it != m_Culture.end() ? it->second : 0;
			if (it == m_Culture.end() || (threshold > 0 ? have < threshold : have > threshold)) return false;
		}
		return true;
	}

	bool Tribe::ClanKnowsBuilding(const BuildingDef& def) const
	{
		if (!CultureAllows(def)) return false;
		std::vector<std::string> locks;
		if (!def.Tech.empty()) locks.push_back(def.Tech);
		if (const TechTree* tree = m_Knowledge.Techs())
			for (const auto& [id, t] : tree->Techs)
				if (std::find(t.Buildings.begin(), t.Buildings.end(), def.Id) != t.Buildings.end() || std::find(t.Buildings.begin(), t.Buildings.end(), def.Name) != t.Buildings.end())
					locks.push_back(id);
		if (locks.empty()) return true;
		for (const std::string& tech : locks)
		{
			if (m_Knowledge.Gated())
			{
				for (const TribeMember& m : m_Members)
					if (!m.Dead && m.Body && m_Knowledge.Knows(m.Name, tech)) return true;
			}
			else if (m_KnowsTech && m_KnowsTech(tech)) return true;
		}
		return false;
	}

	bool Tribe::SiteWorkable(const std::string& site) const
	{
		const AbilitySystemComponent* a = m_Gameplay && m_Buildings ? m_Gameplay->Find(site) : nullptr;
		return a && a->HasTag("Building.Site") && a->HasTag("Building.Ready");
	}

	void Tribe::PlanSettlement(float dt)
	{
		if (!m_Buildings) return;
		m_PlanTimer += dt;
		if (m_PlanTimer < Config.PlanSeconds) return;
		m_PlanTimer = 0.0f;
		// a finished store becomes a stockpile of the economy (H18; once: the economy saves its stockpiles)
		for (const Placed& p : Placements())
			if (p.State.Done && !p.Def->Store.empty() && !FindStockpile(p.Name)) AddStockpile(p.Name, FrontOf(p.Pos, *p.Def), p.Def->Store);
		// what the open sites still lack goes to the economy as a demand (its plan makes what it can, its gather jobs ask for the rest)
		{
			std::map<std::string, std::map<std::string, int>> now;
			for (const Placed& p : Placements())
				if (!p.State.Done)
					for (const auto& [item, n] : p.State.Missing)
						if (const int left = n - InFlight(p.Name, item); left > 0) now["site " + p.Name][item] = left;
			for (const auto& [source, items] : m_SiteDemandSet)
				for (const auto& [item, n] : items)
					if (!now.count(source) || !now[source].count(item)) SetDemand(source, item, 0);
			for (const auto& [source, items] : now)
				for (const auto& [item, n] : items) SetDemand(source, item, n);
			m_SiteDemandSet = std::move(now);
		}
		// F4: since when each open phase waits for materials (a phase that has them, or a new phase, starts again)
		{
			std::map<std::string, double> waiting;
			for (const Placed& p : Placements())
				if (!p.State.Done && !p.State.Missing.empty())
				{
					const auto was = m_SiteWaiting.find(p.Name + "/" + p.State.PhaseName);
					waiting[p.Name + "/" + p.State.PhaseName] = was != m_SiteWaiting.end() ? was->second : m_Time;
				}
			m_SiteWaiting = std::move(waiting);
		}
		UpdateGoals();   // F5
		UpdateHouseholds();   // N1
		// the log tells once when a building stands (whoever gave it its last Build)
		for (const Placed& p : Placements())
			if (p.State.Done && m_Announced.insert(p.Name).second)
			{
				char t[48];
				std::snprintf(t, sizeof t, "%6.1f ", m_Time);
				m_Log.push_back(std::string(t) + p.Name + " stands: " + Provides(*p.Def));
			}
		if (m_DigWell && m_Entities)   // a dug well that stands gets its well (once: it is found by name, a save keeps it)
			for (const Placed& p : Placements())
				if (p.State.Done && p.Def->Well && m_Entities->FindByName("Well of " + p.Name) == NullEntity) m_DigWell("Well of " + p.Name, p.Pos);
		const std::vector<BuildingNeed> needs = BuildingNeeds();
		if (needs.empty()) { UpgradeHouse(); return; }

		// Where: rings round the camp, each building at its own angle, off the campfire, the other buildings and the resource nodes, on ground a member
		// can stand on. The same scene gives the same place (SettlementPlanner.h).
		const ITribeBody* body = nullptr;
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) { body = m.Body; break; }
		SitePlacement where;
		where.Camp = Config.Economy.Camp;
		where.Fire = FirePos();
		where.SiteRadius = Config.SiteRadius;
		for (const Placed& p : Placements()) where.Others.push_back({ p.Def, p.Pos, p.State.Done });
		if (m_Sights.empty()) RefreshSights();
		for (const Sight& sight : m_Sights) where.Obstacles.push_back(sight.Pos);
		if (body) where.CanStand = [body](const glm::vec2& p) { return body->CanStandAt(p); };
		// F6: the most urgent that has a place (a long house with no room left every other building waiting); "no place" is told once a building
		for (const BuildingNeed& need : needs)
		{
			const BuildingDef& def = m_Buildings->at(need.Building);
			where.Def = &def;
			where.Prefer = nullptr;
			where.Rings = 5;
			// N2: a building with an anchor (a node kind it works, the field) or a zone stands by them, on the home ground
			if (def.Shelter <= 0 && (!def.Near.empty() || !def.Zone.empty()))
			{
				std::vector<glm::vec2> anchors, mates;
				if (def.Near == "field")
					for (const Field& f : m_Econ.Fields) anchors.push_back(f.Centre());
				else if (!def.Near.empty())
					for (const auto& [name, e] : m_Clan.All())
						if (e.Kind == def.Near && HomeFactor(e.Pos) >= 1.0f) anchors.push_back(e.Pos);
				if (!def.Zone.empty())
					for (const Placed& p : Placements()) if (p.Def->Zone == def.Zone) mates.push_back(p.Pos);
				if (!anchors.empty() || !mates.empty())
				{
					const float reach = std::max(Config.Exploration.HomeRadius, 20.0f);
					where.Rings = std::max(5, static_cast<int>((VillageReach() - where.SiteRadius) / 3.0f));
					where.Prefer = [this, anchors, mates, reach](const glm::vec2& p)
					{
						float v = 0.0f;
						for (const glm::vec2& a : anchors) v = std::max(v, 2.0f * Falloff(glm::distance(p, a), 8.0f));
						for (const glm::vec2& a : mates) v = std::max(v, 1.5f * Falloff(glm::distance(p, a), 6.0f));
						return v - 0.5f * glm::distance(p, Config.Economy.Camp) / reach;
					};
				}
			}
			// N1: a house goes up for the biggest household without one, where its work is near, by its kin and not far from the fire
			std::string forWhom;
			if (def.Shelter > 0)
			{
				const Household* h = nullptr;
				for (const Household& o : m_Households) if (o.Home.empty() && (!h || o.Members.size() > h->Members.size())) h = &o;
				if (h)
				{
					forWhom = h->Id;
					std::vector<glm::vec2> kin;
					for (const Household& o : m_Households)
					{
						if (o.Home.empty() || &o == h) continue;
						bool related = false;
						for (const std::string& a : h->Members)
							for (const std::string& b : o.Members)
							{
								const Person* pa = m_Society.Find(a);
								const Person* pb = m_Society.Find(b);
								related = related || (pa && (pa->Mother == b || pa->Father == b)) || (pb && (pb->Mother == a || pb->Father == a))
									|| (pa && pb && !pa->Mother.empty() && pa->Mother == pb->Mother);
							}
						if (!related) continue;
						for (const Placed& p : Placements()) if (p.Name == o.Home) kin.push_back(p.Pos);
					}
					const Household home = *h;
					const float reach = std::max(Config.Exploration.HomeRadius, 20.0f);
					where.Rings = std::max(5, static_cast<int>((VillageReach() - where.SiteRadius) / 3.0f));
					where.Prefer = [this, home, kin, reach](const glm::vec2& p)
					{
						float near = 0.0f;
						for (const glm::vec2& k : kin) near = std::max(near, Falloff(glm::distance(p, k), 10.0f));
						return std::log1p(HouseholdWork(home, p)) + 1.5f * near - 2.0f * glm::distance(p, Config.Economy.Camp) / reach;
					};
				}
			}
			if (const std::optional<glm::vec2> spot = PlaceSite(where))
			{
				std::string site;
				StartSite(def.Id, *spot, &site);
				if (!forWhom.empty() && !site.empty()) m_Homes[site] = forWhom;
				m_Log.back() += ": " + need.Why + (forWhom.empty() ? std::string() : " (the house of " + forWhom + "'s household)");
				m_NoPlace.erase(def.Id);
				return;
			}
			if (m_NoPlace.insert(def.Id).second)
			{
				char t[48];
				std::snprintf(t, sizeof t, "%6.1f ", m_Time);
				m_Log.push_back(std::string(t) + "the clan wants a " + def.Name + " (" + need.Why + ") but finds no place for it near the camp");
			}
		}
	}

	// N1c: one household at a time gets a better house (more beds, the data's "supersedes": the hut over the shelter, the long house over both), the
	// clan knows it and can pay it, when nothing is more pressing and a site is free: built beside its house, which it pulls down when it stands.
	bool Tribe::UpgradeHouse()
	{
		if (!m_Buildings || !m_Upgrades.empty() || !HasFire() || !ToolsReady()) return false;
		int open = 0, alive = 0;
		for (const Placed& p : Placements()) if (!p.State.Done) ++open;
		for (const TribeMember& m : m_Members) if (!m.Dead) ++alive;
		if (open >= std::max(1, Config.Econ.Construction.SitesFor(alive))) return false;
		auto payable = [&](const BuildingDef& def)
		{
			for (const auto& [item, n] : def.TotalNeeds())
			{
				bool from = m_Store.Count(item) > 0;
				for (const auto& [kind, node] : m_Defs) from = from || node.Item == item;
				if (!from) return false;
			}
			return true;
		};
		// first a household with no house at all gets one (the beds may be enough, but they are someone else's): the smallest shelter it fits in
		{
			const Household* homeless = nullptr;
			for (const Household& h : m_Households)
				if (h.Home.empty() && h.Members.size() >= 2 && (!homeless || h.Members.size() > homeless->Members.size())) homeless = &h;   // (one alone sleeps in a free bed)
			if (homeless)
			{
				const BuildingDef* pick = nullptr;
				for (const auto& [id, def] : *m_Buildings)
				{
					if (def.Shelter <= 0 || !ClanKnowsBuilding(def) || !payable(def)) continue;
					const bool fits = def.Shelter >= static_cast<int>(homeless->Members.size());
					const bool pickFits = pick && pick->Shelter >= static_cast<int>(homeless->Members.size());
					if (!pick || (fits && (!pickFits || def.Shelter < pick->Shelter)) || (!fits && !pickFits && def.Shelter > pick->Shelter)) pick = &def;
				}
				if (pick)
				{
					SitePlacement where;
					where.Camp = Config.Economy.Camp;
					where.Fire = FirePos();
					where.SiteRadius = Config.SiteRadius;
					where.Def = pick;
					for (const Placed& p : Placements()) where.Others.push_back({ p.Def, p.Pos, p.State.Done });
					if (m_Sights.empty()) RefreshSights();
					for (const Sight& sight : m_Sights) where.Obstacles.push_back(sight.Pos);
					for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) { const ITribeBody* body = m.Body; where.CanStand = [body](const glm::vec2& p) { return body->CanStandAt(p); }; break; }
					const Household home = *homeless;
					const float reach = std::max(Config.Exploration.HomeRadius, 20.0f);
					where.Rings = std::max(5, static_cast<int>((VillageReach() - where.SiteRadius) / 3.0f));
					where.Prefer = [this, home, reach](const glm::vec2& p) { return std::log1p(HouseholdWork(home, p)) - 2.0f * glm::distance(p, Config.Economy.Camp) / reach; };
					std::string site;
					if (const std::optional<glm::vec2> spot = PlaceSite(where); spot && StartSite(pick->Id, *spot, &site).empty() && !site.empty())
					{
						m_Homes[site] = home.Id;
						m_Log.back() += ": the house of " + home.Id + "'s household, which has none";
						return true;
					}
				}
			}
		}
		const Household* who = nullptr;
		const Placed* house = nullptr;
		const BuildingDef* better = nullptr;
		for (const Household& h : m_Households)
		{
			if (h.Home.empty() || (who && who->Members.size() >= h.Members.size())) continue;
			const Placed* home = nullptr;
			for (const Placed& p : Placements()) if (p.Name == h.Home && p.State.Done) home = &p;
			if (!home) continue;
			const BuildingDef* best = nullptr;
			for (const auto& [id, def] : *m_Buildings)
			{
				if (def.Shelter <= home->Def->Shelter || std::find(def.Supersedes.begin(), def.Supersedes.end(), home->Def->Id) == def.Supersedes.end()) continue;
				if (!ClanKnowsBuilding(def)) continue;
				if (payable(def) && (!best || def.Shelter > best->Shelter)) best = &def;
			}
			if (best) { who = &h; house = home; better = best; }
		}
		if (!who) return false;
		SitePlacement where;
		where.Camp = Config.Economy.Camp;
		where.Fire = FirePos();
		where.SiteRadius = Config.SiteRadius;
		where.Def = better;
		for (const Placed& p : Placements()) where.Others.push_back({ p.Def, p.Pos, p.State.Done });
		if (m_Sights.empty()) RefreshSights();
		for (const Sight& sight : m_Sights) where.Obstacles.push_back(sight.Pos);
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) { const ITribeBody* body = m.Body; where.CanStand = [body](const glm::vec2& p) { return body->CanStandAt(p); }; break; }
		const glm::vec2 old = house->Pos;
		where.Rings = std::max(5, static_cast<int>((VillageReach() - where.SiteRadius) / 3.0f));
		where.Prefer = [old](const glm::vec2& p) { return -glm::distance(p, old); };   // as near the old house as it fits
		const std::optional<glm::vec2> spot = PlaceSite(where);
		if (!spot) return false;
		std::string site;
		if (!StartSite(better->Id, *spot, &site).empty() || site.empty()) return false;
		m_Homes[site] = who->Id;
		m_Upgrades[site] = house->Name;
		m_Log.back() += ": a better house for " + who->Id + "'s household (" + std::to_string(better->Shelter) + " beds instead of " + std::to_string(house->Def->Shelter) + ")";
		return true;
	}

	// How far from the fire the clan builds now: a small clan keeps close, a growing one spreads its houses and workshops out (within
	// SettlementRadius, past which a building is not the clan's any more).
	int Tribe::FieldsWanted() const
	{
		int alive = 0;
		for (const TribeMember& m : m_Members) if (!m.Dead) ++alive;
		return Config.Econ.Farming.FieldsFor(alive);
	}

	float Tribe::VillageReach() const
	{
		int alive = 0;
		for (const TribeMember& m : m_Members) if (!m.Dead) ++alive;
		const float reach = 20.0f + 2.5f * static_cast<float>(alive) + 1.0f * static_cast<float>(BuildingCount());
		return std::clamp(reach, 30.0f, std::max(30.0f, Config.SettlementRadius - 4.0f));
	}

	std::string Tribe::Era() const
	{
		const TechTree* tree = m_Knowledge.Techs();
		if (!tree) return {};
		std::string era;
		for (const EraDef& e : tree->Rules.Eras)
			if (std::all_of(e.Key.begin(), e.Key.end(), [&](const std::string& t) { return m_Knowledge.Clan().count(t) > 0; })) era = e.Id;
		return era;
	}

	std::string Tribe::EraName() const
	{
		const TechTree* tree = m_Knowledge.Techs();
		const std::string era = Era();
		if (tree) for (const EraDef& e : tree->Rules.Eras) if (e.Id == era) return e.Name;
		return era;
	}

	float Tribe::SiteAge(const std::string& site) const
	{
		const ConstructionRules& c = Config.Econ.Construction;
		double since = m_Time;
		for (const auto& [key, at] : m_SiteWaiting)
			if (key.size() > site.size() && key.compare(0, site.size(), site) == 0 && key[site.size()] == '/') since = std::min(since, at);
		return 1.0f + std::min(c.AgeMax, static_cast<float>(m_Time - since) / std::max(1.0f, c.AgeSeconds));
	}

	float Tribe::ItemAge(const std::string& item) const
	{
		float age = 1.0f;
		for (const Placed& p : Placements())
			if (!p.State.Done && p.State.Missing.count(item)) age = std::max(age, SiteAge(p.Name));
		return age;
	}

	// The clan's next steps (F5): the technologies that would let it raise what it needs (its needs as if it knew everything, the ones it does not
	// know), and the one of its fields while it has none; each after what it requires. Its members experiment with these first.
	void Tribe::UpdateGoals()
	{
		const TechTree* tree = m_Knowledge.Techs();
		if (!tree || !m_Buildings) return;
		const std::set<std::string>& known = m_Knowledge.Clan();
		std::vector<std::string> goals;
		std::function<void(const std::string&, int)> add = [&](const std::string& id, int depth)
		{
			const TechDef* t = tree->Find(id);
			if (!t || known.count(id) || depth > 8 || std::find(goals.begin(), goals.end(), id) != goals.end()) return;
			for (const std::string& r : t->Requires) add(r, depth + 1);   // what it requires first
			goals.push_back(id);
		};
		// the player's aim first (and what it requires before it); once known the aim is spent
		if (!m_Players.Aim.empty())
		{
			if (known.count(m_Players.Aim) || !tree->Find(m_Players.Aim))
			{
				if (const TechDef* t = tree->Find(m_Players.Aim); t && known.count(m_Players.Aim)) NoteClan("has learnt " + t->Name + ", as " + m_Players.AimBy + " asked");
				m_Players.Aim.clear();
				m_Players.AimBy.clear();
			}
			else add(m_Players.Aim, 0);
		}
		for (const BuildingNeed& need : BuildingNeeds(true))
		{
			const BuildingDef& def = m_Buildings->at(need.Building);
			if (ClanKnowsBuilding(def)) continue;
			if (!def.Tech.empty()) add(def.Tech, 0);
			for (const auto& [id, t] : tree->Techs)
				if (std::find(t.Buildings.begin(), t.Buildings.end(), def.Id) != t.Buildings.end() || std::find(t.Buildings.begin(), t.Buildings.end(), def.Name) != t.Buildings.end()) add(id, 0);
		}
		const FarmRules& farm = Config.Econ.Farming;
		if (!farm.Tech.empty() && static_cast<int>(m_Econ.Fields.size()) < FieldsWanted()) add(farm.Tech, 0);   // the way to the fields
		// P1/N6: the next rung of a ladder the clan climbs on its own. Without eras, knowledge.progression; with them, the ladder of its era (pull 1)
		// or of an era it can go on to, pulled by what of that era's interest the clan has in the store and knows of round it (the nearer, the
		// more) and by the era's bias: a clan with copper near goes for smelting, one with grain and clay for the fields
		const std::vector<EraDef>& eras = tree->Rules.Eras;
		const std::string era = Era();
		if (era != m_Era)
		{
			if (!m_Era.empty())
			{
				char t[48];
				std::snprintf(t, sizeof t, "%6.1f ", m_Time);
				m_Log.push_back(std::string(t) + "the clan enters the " + EraName());
				TellClan("enters the " + EraName());
			}
			m_Era = era;
		}
		auto firstUnknown = [&](const std::vector<std::string>& ladder) -> std::string
		{
			for (const std::string& id : ladder) if (!known.count(id) && tree->Find(id)) return id;
			return {};
		};
		std::string rung = firstUnknown(tree->Rules.Progression);
		if (!eras.empty())
		{
			float best = -1.0f;
			rung.clear();
			for (const EraDef& e : eras)
			{
				const bool current = e.Id == era;
				const bool next = !current && std::find(e.From.begin(), e.From.end(), era) != e.From.end();
				if (!current && !next) continue;
				const std::string id = firstUnknown(e.Progression);
				if (id.empty()) continue;
				float pull = current ? 1.0f : e.Pull;
				if (next && !e.Interest.empty())
				{
					float interest = 0.0f;
					for (const std::string& item : e.Interest)
					{
						float have = static_cast<float>(m_Store.Count(item));
						for (const auto& [name, mem] : m_Clan.All())
							if (const auto d = m_Defs.find(mem.Kind); d != m_Defs.end() && d->second.Item == item)
								have += static_cast<float>(mem.Amount >= 0 ? mem.Amount : d->second.Capacity) * Falloff(glm::distance(mem.Pos, Config.Economy.Camp), 300.0f);
						interest += std::min(1.0f, have / 20.0f);
					}
					pull += 2.0f * interest / static_cast<float>(e.Interest.size());
				}
				if (pull > best) { best = pull; rung = id; }
			}
		}
		if (!rung.empty()) add(rung, 0);
		m_Knowledge.SetGoals(std::move(goals));
	}

	void Tribe::SettlementOptions(TribeMember& m, std::vector<UtilityOption>& opts) const
	{
		if (!m_Buildings) return;
		const glm::vec2 pos = Where(m);
		const float bw = std::max(0.6f, MemberWeight(m, "build"));   // anyone helps; a role that names "build" (a builder), or a member good at building, more
		const float day = AgendaFactor(m.Role, "gather", Hour(), CurrentSeason());   // building is work: the agenda of work
		const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		const bool canBuild = asc && std::find(asc->Granted().begin(), asc->Granted().end(), "Build") != asc->Granted().end();
		char buffer[160];
		for (const Placed& p : Placements())
		{
			if (p.State.Done) continue;
			const glm::vec2 front = FrontOf(p.Pos, *p.Def, 0.7f * (static_cast<float>(IndexOf(m) % 3) - 1.0f));   // (each its own place at the front: one point for all was a queue)
			const float reach = Falloff(glm::distance(pos, front), 120.0f);
			// carry what the phase lacks from the store
			int bring = 0;
			std::string what;
			for (const auto& [item, n] : p.State.Missing)
				if (const int k = std::min(n - InFlight(p.Name, item), WithSubstitutes(m_Store, p.State, item)); k > 0) { bring += k; what += (what.empty() ? "" : ", ") + item; }
			if (bring > 0)
			{
				UtilityOption o;
				o.Kind = "deliver";
				o.Target = p.Name;
				o.Where = front;
				const float age = SiteAge(p.Name);   // F4: it has waited for this long
				o.Score = 0.85f * bw * reach * day * age;
				if (age > 1.0f) Factor(o, "waiting", age);
				Factor(o, "role", bw);
				Factor(o, "distance", reach);
				if (day != 1.0f) Factor(o, "agenda", day);
				o.Note = p.Name + " lacks " + what + " and the store has it";
				opts.push_back(o);
			}
			// work it once the phase has its materials
			int builders = 0;
			for (const TribeMember& o : m_Members) if (&o != &m && o.Job == "build" && o.Detail == p.Name) ++builders;
			if (p.State.Ready && canBuild && builders < Config.Builders)
			{
				UtilityOption o;
				o.Kind = "build";
				o.Target = p.Name;
				o.Where = front;
				o.Score = 1.0f * bw * reach * day;
				Factor(o, "role", bw);
				Factor(o, "distance", reach);
				if (day != 1.0f) Factor(o, "agenda", day);
				std::snprintf(buffer, sizeof buffer, "%s: the %s has its materials (%.0f/%.0f s of work)", p.Name.c_str(), p.State.PhaseName.c_str(), static_cast<double>(p.State.Work), static_cast<double>(p.State.WorkNeeded));
				o.Note = buffer;
				opts.push_back(o);
			}
		}
	}

	int Tribe::ExecuteSettlement(TribeMember& m, const UtilityOption& o)
	{
		if (o.Kind != "deliver" && o.Kind != "build") return -1;
		if (!m_Buildings) return 0;
		const AbilitySystemComponent* site = m_Gameplay->Find(o.Target);
		const BuildingDef* def = site ? BuildingOf(*site, *m_Buildings) : nullptr;
		if (!def) return 0;
		const SiteState state = ReadSite(*site, *def);
		char go[128];
		if (o.Kind == "deliver")
		{
			// takes from the store what the phase lacks (and nobody is bringing yet), as much as one trip carries
			Inventory& bag = m.Body->Bag();
			std::string what;
			int left = Config.HaulAt;
			std::vector<std::string> taken;   // (to give back if the trip cannot start)
			for (const auto& [item, n] : state.Missing)
			{
				int k = std::min({ n - InFlight(o.Target, item), WithSubstitutes(m_Store, state, item), left });
				std::vector<std::string> choices{ item };   // the material itself first, then what stands in for it (a roof of branches)
				if (const auto sub = state.Substitutes.find(item); sub != state.Substitutes.end()) choices.insert(choices.end(), sub->second.begin(), sub->second.end());
				for (const std::string& what_ : choices)
				{
					if (k <= 0) break;
					const int moved = m_Store.Transfer(*m_Items, bag, what_, std::min(k, m_Store.Count(what_)));
					if (moved <= 0) continue;
					k -= moved;
					left -= moved;
					taken.push_back(what_);
					what += (what.empty() ? "" : ", ") + std::to_string(moved) + " " + what_ + (what_ != item ? " (for " + item + ")" : std::string());
				}
			}
			if (what.empty()) return 0;
			std::snprintf(go, sizeof go, "goto %.2f %.2f | wait 1", static_cast<double>(o.Where.x), static_cast<double>(o.Where.y));
			if (!Order(m, go))
			{
				for (const std::string& item : taken) bag.Transfer(*m_Items, m_Store, item, bag.Count(item));   // gives them back
				return 0;
			}
			m.Job = "deliver";
			m.Detail = o.Target;
			Say(m, "carries " + what + " to " + o.Target);
			return 1;
		}
		// build: a few uses of Build, like the swings at a node (not more than the phase still needs, counting 2 s of work a use)
		const int uses = std::clamp(static_cast<int>(std::ceil((state.WorkNeeded - state.Work) / 2.0f)), 1, 3);
		std::string text;
		for (int i = 0; i < uses; ++i) text += std::string(i ? " | " : "") + "ability Build " + Underscored(o.Target) + " | wait 0.5";
		if (!Order(m, text)) return 0;
		m.Job = "build";
		m.Detail = o.Target;
		if (m.Mind.Current.Kind != "build" || m.Mind.Current.Target != o.Target) Say(m, "builds the " + state.PhaseName + " of " + o.Target);
		return 1;
	}

	bool Tribe::FinishSettlement(TribeMember& m, const std::string& job)
	{
		if (job != "deliver" && job != "build") return false;
		if (!m_Buildings) return true;
		AbilitySystemComponent* site = m_Gameplay->Find(m.Detail);
		const BuildingDef* def = site ? BuildingOf(*site, *m_Buildings) : nullptr;
		if (!def) return true;
		if (job == "deliver")
		{
			// it hands over what the site lacks, if it got there; the rest stays in the bag (it goes to the store with the next deposit)
			for (const Placed& p : Placements())
				if (p.Name == m.Detail && glm::distance(Where(m), p.Pos) > def->Radius() + 3.0f) { Say(m, "could not get to " + m.Detail); return true; }
			Inventory& bag = m.Body->Bag();
			int given = 0;
			const SiteState before = ReadSite(*site, *def);
			for (const auto& [item, n] : before.Missing)
			{
				std::vector<std::string> choices{ item };   // the material, then what stands in for it
				if (const auto sub = before.Substitutes.find(item); sub != before.Substitutes.end()) choices.insert(choices.end(), sub->second.begin(), sub->second.end());
				for (const std::string& what : choices)
					if (const int k = std::min(n, bag.Count(what)); k > 0) given += bag.Remove(what, DeliverToSite(*site, *def, what, k));
			}
			if (given > 0) ForgetPlacements();
			if (given > 0)
			{
				Say(m, "brings " + std::to_string(given) + " to " + m.Detail + (site->HasTag("Building.Ready") ? ": the " + ReadSite(*site, *def).PhaseName + " can be built" : ""));
				Practise(m, "deliver", "deliver", static_cast<float>(given));   // H7: carrying to a site trains building (a little)
				if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "deliver" && held->Target == m.Detail) m_Board.Progress(m.Name, static_cast<float>(given), m_Time);
			}
			return true;
		}
		const SiteState s = ReadSite(*site, *def);
		if (!HasFailed(m))
		{
			Practise(m, "build", "build", 1.0f);
			if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "build" && held->Target == m.Detail) m_Board.Progress(m.Name, 1.0f, m_Time);
		}
		if (s.Done && m_Announced.insert(m.Detail).second)
		{
			++m.Made;
			Say(m, "finishes " + m.Detail + ": it stands");
		}
		return true;
	}

	// The construction sites on the job board (H7): "haul <site>" while its phase lacks materials the store has (one slot per HaulAt units), and
	// "build <site>" while the phase has them (Builders slots). Their family is the aptitude of "build" (building), so who builds most likes it most.
	void Tribe::PostSettlementJobs()
	{
		if (!m_Buildings) return;
		const JobRules& rules = Config.Work.Jobs;
		auto priority = [&](const char* kind) { const auto p = rules.Priority.find(kind); return p != rules.Priority.end() ? p->second : 1.0f; };
		for (const Placed& p : Placements())
		{
			if (p.State.Done) continue;
			int bring = 0;
			std::string what;
			for (const auto& [item, n] : p.State.Missing)
				if (const int k = std::min(n - InFlight(p.Name, item), WithSubstitutes(m_Store, p.State, item)); k > 0) { bring += k; what += (what.empty() ? "" : ", ") + std::to_string(k) + " " + item; }
			if (bring > 0)
			{
				const int slots = std::clamp((bring + Config.HaulAt - 1) / std::max(1, Config.HaulAt), 1, std::max(1, rules.MaxSlots));
				m_Board.Post("deliver", p.Name, SkillFamilyOf("deliver"), static_cast<float>(bring), priority("deliver"), slots, p.Name + " lacks " + what + " and the store has it", m_Time, rules);
			}
			if (p.State.Ready)
			{
				char why[128];
				std::snprintf(why, sizeof why, "the %s of %s has its materials (%.0f/%.0f s of work)", p.State.PhaseName.c_str(), p.Name.c_str(), static_cast<double>(p.State.Work), static_cast<double>(p.State.WorkNeeded));
				m_Board.Post("build", p.Name, SkillFamilyOf("build"), std::max(0.0f, p.State.WorkNeeded - p.State.Work), priority("build"), std::max(1, Config.Builders), why, m_Time, rules);
			}
		}
	}

	float Tribe::SettlementJobScore(const TribeMember& m, const Job& job) const
	{
		if (!m_Buildings || !m.Body) return 0.0f;
		for (const Placed& p : Placements())
		{
			if (p.Name != job.Target || p.State.Done) continue;
			const float distance = Falloff(glm::distance(Where(m), FrontOf(p.Pos, *p.Def)), 100.0f);
			const float liking = std::max(0.6f, MemberWeight(m, job.Kind));
			if (job.Kind == "build")
			{
				const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
				const bool can = asc && std::find(asc->Granted().begin(), asc->Granted().end(), "Build") != asc->Granted().end();
				return can && p.State.Ready ? job.Priority * liking * distance : 0.0f;
			}
			bool any = false;
			for (const auto& [item, n] : p.State.Missing) any = any || (n - InFlight(p.Name, item) > 0 && WithSubstitutes(m_Store, p.State, item) > 0);
			return any ? job.Priority * liking * distance : 0.0f;
		}
		return 0.0f;
	}

	float Tribe::Provision(const std::string& what) const
	{
		float sum = 0.0f;
		for (const Placed& p : Placements())
			if (p.State.Done && what == "trade") sum += p.Def->Trade;
		return sum;
	}

	bool Tribe::CanBuild(const std::string& building) const
	{
		const auto def = m_Buildings ? m_Buildings->find(building) : std::map<std::string, BuildingDef>::const_iterator();
		return m_Buildings && def != m_Buildings->end() && ClanKnowsBuilding(def->second);
	}

	std::string Tribe::SettlementReport() const
	{
		std::string s;
		for (const Placed& p : Placements())
		{
			if (p.State.Done)
			{
				s += p.Name + " (" + p.Def->Id + ") stands: " + Provides(*p.Def) + "\n";
				continue;
			}
			char line[200];
			std::snprintf(line, sizeof line, "%s (%s): building the %s (phase %d/%d), work %.0f/%.0f s, %.0f%% done", p.Name.c_str(), p.Def->Id.c_str(), p.State.PhaseName.c_str(),
				p.State.Phase + 1, p.State.Phases, static_cast<double>(p.State.Work), static_cast<double>(p.State.WorkNeeded), static_cast<double>(p.State.Progress * 100.0f));
			s += line;
			if (!p.State.Missing.empty())
			{
				s += ", lacks";
				for (const auto& [item, n] : p.State.Missing) s += " " + item + " x" + std::to_string(n);
			}
			s += "\n";
		}
		const std::vector<BuildingNeed> needs = BuildingNeeds();
		if (!needs.empty()) s += "wants a " + needs.front().Building + ": " + needs.front().Why + "\n";
		s += "store cap " + std::to_string(StoreCap()) + "\n";
		return s;
	}
}
