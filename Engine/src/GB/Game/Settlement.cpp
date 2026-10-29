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
			return s.empty() ? std::string("nothing yet") : s;
		}

		// Where a member stands to work a site: in front of it (the "work" point of BuildSite), a little to the side for each builder.
		glm::vec2 FrontOf(const glm::vec2& pos, const BuildingDef& def, float side = 0.0f) { return pos + glm::vec2(side, 0.5f * def.Footprint.y + 0.8f); }
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

	std::vector<Tribe::Placed> Tribe::Placements() const
	{
		std::vector<Placed> out;
		if (!m_Buildings || !m_Gameplay || !m_Entities) return out;
		// The names and places are looked up once per tick (the scene's systems are many; a site does not move); the state is read each time.
		if (m_PlacedAt != m_Time || m_PlacedSystems != m_Gameplay->Count())
		{
			m_Placed.clear();
			for (const std::string& name : m_Gameplay->Names())
			{
				const AbilitySystemComponent* a = m_Gameplay->Find(name);
				if (!a || !a->HasTag("Building")) continue;
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
			if (glm::distance(pos, Config.Camp) > Config.SettlementRadius) continue;   // left behind by a move of the camp (H12): not the clan's any more
			const AbilitySystemComponent* a = m_Gameplay->Find(name);
			const BuildingDef* def = a ? BuildingOf(*a, *m_Buildings) : nullptr;
			if (!def) continue;
			out.push_back({ name, def, pos, ReadSite(*a, *def) });
		}
		return out;
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

	glm::vec2 Tribe::SleepSpot(const TribeMember& m) const
	{
		// The beds go to the living members in order; who has none sleeps at its place at the camp.
		int slot = 0;
		for (const TribeMember& o : m_Members)
		{
			if (&o == &m) break;
			if (!o.Dead) ++slot;
		}
		for (const Placed& p : Placements())
		{
			if (!p.State.Done || p.Def->Shelter <= 0) continue;
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
		int n = 0;
		for (const TribeMember& o : m_Members)
			if (o.Job == "deliver" && o.Detail == site && o.Body) n += o.Body->Bag().Count(item);
		return n;
	}

	std::map<std::string, int> Tribe::SiteDemand() const
	{
		std::map<std::string, int> out;
		for (const Placed& p : Placements())
		{
			if (p.State.Done) continue;
			for (const auto& [item, n] : p.State.Missing)
				if (const int left = n - InFlight(p.Name, item); left > 0) out[item] += left;
		}
		return out;
	}

	std::vector<BuildingNeed> Tribe::BuildingNeeds() const
	{
		std::vector<BuildingNeed> out;
		if (!m_Buildings || !m_Items || !m_Recipes || m_Members.empty()) return out;
		if (!HasFire() || !ToolsReady()) return out;   // first the tools and the campfire, as before there were buildings
		const std::vector<Placed> placed = Placements();
		int open = 0, beds = 0, alive = 0;
		std::map<std::string, int> count;   // buildings of each kind, done or not
		for (const Placed& p : placed)
		{
			if (!p.State.Done) ++open;
			beds += p.Def->Shelter;   // a shelter being built already answers the need for beds
			++count[p.Def->Id];
		}
		for (const TribeMember& m : m_Members) if (!m.Dead) ++alive;
		if (alive == 0 || open >= Config.Econ.Construction.SitesFor(alive)) return out;
		int wounded = 0;   // H14: members with wounds
		if (m_Combat)
			for (const TribeMember& m : m_Members)
				if (const Combatant* c = m.Dead ? nullptr : m_Combat->Find(m.Name); c && !c->Wounds.empty()) ++wounded;
		int spoiled = 0;   // H18: food the store lost to spoilage
		for (const auto& [item, n] : m_Econ.Spoiled) if (IsFood(item)) spoiled += n;
		const int cap = StoreCap();
		std::string full;
		for (const ItemSlot& s : m_Store.Stacks())
			if (m_Store.Count(s.Id) >= cap - 2 && (full.empty() || s.Id < full)) full = s.Id;

		char why[160];
		// only what the clan can pay: every material is in the store, some node of the land gives it or a recipe the clan knows makes it where it can
		auto payable = [&](const BuildingDef& def)
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
		// a better home the clan can raise takes the place of the ones it supersedes (the hut over the shelter): those are not raised for beds any more
		std::set<std::string> superseded;
		for (const auto& [id, def] : *m_Buildings)
			if (!def.Supersedes.empty() && ClanKnowsBuilding(def) && payable(def)) superseded.insert(def.Supersedes.begin(), def.Supersedes.end());
		for (const auto& [id, def] : *m_Buildings)
		{
			if (!ClanKnowsBuilding(def) || !payable(def)) continue;   // H11 decides what is known
			BuildingNeed need;
			need.Building = id;
			auto want = [&](float urgency, const std::string& text) { if (urgency > need.Urgency) { need.Urgency = urgency; need.Why = text; } };
			if (def.Shelter > 0 && beds < alive && !superseded.count(id))
			{
				need.Urgency = std::max(need.Urgency, 3.0f * static_cast<float>(alive - beds) / static_cast<float>(alive));
				std::snprintf(why, sizeof why, "beds for %d of %d", beds, alive);
				need.Why = why;
			}
			if ((!def.Store.empty() || def.StoreCap > 0) && !full.empty() && count[id] == 0)
				want(2.0f, "the store is full of " + full + " (" + std::to_string(m_Store.Count(full)) + "/" + std::to_string(cap) + ")");
			// the generic provisions (docs/PROGRESION.md), each only for the first of its kind
			if (count[id] == 0)
			{
				if (def.Keep < 1.0f && spoiled > 0) want(1.2f, std::to_string(spoiled) + " food went off in the store");
				if (def.Healing > 0.0f) want(wounded > 0 ? 1.2f : 0.3f, wounded > 0 ? std::to_string(wounded) + " wounded to tend" : std::string("a place to tend the wounded"));
				if (def.Defense > 0.0f) want(m_Conflict.Raided > 0 ? 2.0f : 0.35f, m_Conflict.Raided > 0 ? "raided " + std::to_string(m_Conflict.Raided) + " times" : std::string("walls against raiders"));
				if (def.Culture > 0.0f && def.Aura > 0.0f) want(std::min(0.8f, 0.3f + 0.05f * static_cast<float>(alive)), "a place for the clan to meet");
				if (def.Shelter == 0 && def.Warmth > 0.0f && def.Aura > 0.0f) want(0.4f, "a fire to gather round");
			}
			if (!def.Station.empty() && count[id] == 0)
			{
				const bool there = StationAvailable(def.Station);
				// a recipe needs the station: the tool of a role here, or one whose inputs the store holds
				std::string recipe;
				for (const std::string& rid : m_Recipes->Ids())
				{
					const Recipe& r = *m_Recipes->Find(rid);
					if (r.Station != def.Station) continue;
					bool tool = false;
					for (const TribeMember& m : m_Members)
						if (const auto role = Config.Roles.find(m.Role); role != Config.Roles.end() && role->second.Tool == r.Output) tool = true;
					const bool inputs = !r.Inputs.empty() && std::all_of(r.Inputs.begin(), r.Inputs.end(), [&](const auto& in) { return m_Store.Count(in.first) >= in.second; });
					if (tool || inputs) { recipe = rid; break; }
				}
				if (!there && !recipe.empty() && 1.5f > need.Urgency) { need.Urgency = 1.5f; need.Why = "the recipe " + recipe + " needs a " + def.Station; }
				else if (there && 0.5f > need.Urgency && std::find(Config.Econ.Production.CampStations.begin(), Config.Econ.Production.CampStations.end(), def.Station) != Config.Econ.Production.CampStations.end())
				{
					need.Urgency = 0.5f;
					need.Why = "the camp only has an improvised " + def.Station;
				}
			}
			if (need.Urgency > 0.0f) out.push_back(need);
		}
		std::sort(out.begin(), out.end(), [](const BuildingNeed& a, const BuildingNeed& b) { return a.Urgency != b.Urgency ? a.Urgency > b.Urgency : a.Building < b.Building; });
		return out;
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
		const float y = m_Ground ? m_Ground(at.x, at.y) : Config.CampY;
		const EntityId id = BuildSite(*m_Entities, def->second, n, { at.x, y, at.y });
		AbilitySystemComponent* a = m_Gameplay->Create(n);
		a->FromComponent(*m_Entities->Find(id)->Gameplay);
		m_PlacedAt = -1.0;   // look again
		std::string needs;
		for (const auto& [item, k] : def->second.TotalNeeds()) needs += (needs.empty() ? "" : ", ") + std::to_string(k) + " " + item;
		char t[48];
		std::snprintf(t, sizeof t, "%6.1f ", m_Time);
		m_Log.push_back(std::string(t) + "the clan starts " + n + " at " + std::to_string(static_cast<int>(at.x)) + ", " + std::to_string(static_cast<int>(at.y)) + " (" + needs + ")");
		if (name) *name = n;
		return {};
	}

	// A building is locked by the technologies that list it (TechDef::Buildings, by id or by name) and by its own `tech`. With technologies (SetTechs) the clan
	// can build it when a living member knows one of them; without, the check of SetTechCheck decides (none: a locked building is never planned).
	bool Tribe::ClanKnowsBuilding(const BuildingDef& def) const
	{
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
		// the log tells once when a building stands (whoever gave it its last Build)
		for (const Placed& p : Placements())
			if (p.State.Done && m_Announced.insert(p.Name).second)
			{
				char t[48];
				std::snprintf(t, sizeof t, "%6.1f ", m_Time);
				m_Log.push_back(std::string(t) + p.Name + " stands: " + Provides(*p.Def));
			}
		const std::vector<BuildingNeed> needs = BuildingNeeds();
		if (needs.empty()) return;
		const BuildingDef& def = m_Buildings->at(needs.front().Building);

		// Where: rings round the camp, each building at its own angle, off the campfire, the other buildings and the resource nodes, on ground a member
		// can stand on. The same scene gives the same place.
		const ITribeBody* body = nullptr;
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) { body = m.Body; break; }
		const std::vector<Placed> placed = Placements();
		if (m_Sights.empty()) RefreshSights();
		const float start = 0.6f + 1.1f * static_cast<float>(placed.size());
		auto fits = [&](const glm::vec2& p)
		{
			const float r = def.Radius();
			if (glm::distance(p, Config.Camp) < r + 2.5f || glm::distance(p, FirePos()) < r + 2.5f) return false;
			for (const Placed& o : placed) if (glm::distance(p, o.Pos) < r + o.Def->Radius() + 1.5f) return false;
			for (const Sight& s : m_Sights) if (glm::distance(p, s.Pos) < r + 1.2f) return false;
			if (!body) return true;
			if (!body->CanStandAt(p)) return false;
			for (int k = 0; k < 4; ++k)   // the corners too: not half in the lake
			{
				const glm::vec2 c = p + glm::vec2((k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f) * def.Footprint;
				if (!body->CanStandAt(c)) return false;
			}
			return body->CanStandAt(FrontOf(p, def));
		};
		for (int ring = 0; ring < 5; ++ring)
			for (int k = 0; k < 12; ++k)
			{
				const float a = start + static_cast<float>(k) * 0.5236f;
				const glm::vec2 p = Config.Camp + glm::vec2(std::cos(a), std::sin(a)) * (Config.SiteRadius + 3.0f * static_cast<float>(ring));
				if (!fits(p)) continue;
				StartSite(def.Id, p);
				m_Log.back() += ": " + needs.front().Why;
				return;
			}
		char t[48];
		std::snprintf(t, sizeof t, "%6.1f ", m_Time);
		m_Log.push_back(std::string(t) + "the clan wants a " + def.Name + " (" + needs.front().Why + ") but finds no place for it near the camp");
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
			const glm::vec2 front = FrontOf(p.Pos, *p.Def);
			const float reach = Falloff(glm::distance(pos, front), 120.0f);
			// carry what the phase lacks from the store
			int bring = 0;
			std::string what;
			for (const auto& [item, n] : p.State.Missing)
				if (const int k = std::min(n - InFlight(p.Name, item), m_Store.Count(item)); k > 0) { bring += k; what += (what.empty() ? "" : ", ") + item; }
			if (bring > 0)
			{
				UtilityOption o;
				o.Kind = "deliver";
				o.Target = p.Name;
				o.Where = front;
				o.Score = 0.85f * bw * reach * day;
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
			for (const auto& [item, n] : state.Missing)
			{
				const int k = std::min({ n - InFlight(o.Target, item), m_Store.Count(item), left });
				if (k <= 0) continue;
				const int moved = m_Store.Transfer(*m_Items, bag, item, k);
				if (moved <= 0) continue;
				left -= moved;
				what += (what.empty() ? "" : ", ") + std::to_string(moved) + " " + item;
			}
			if (what.empty()) return 0;
			std::snprintf(go, sizeof go, "goto %.2f %.2f | wait 1", static_cast<double>(o.Where.x), static_cast<double>(o.Where.y));
			if (!Order(m, go))
			{
				for (const auto& [item, n] : state.Missing) bag.Transfer(*m_Items, m_Store, item, bag.Count(item));   // gives them back
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
			for (const auto& [item, n] : ReadSite(*site, *def).Missing)
				if (const int k = std::min(n, bag.Count(item)); k > 0) given += bag.Remove(item, DeliverToSite(*site, *def, item, k));
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
				if (const int k = std::min(n - InFlight(p.Name, item), m_Store.Count(item)); k > 0) { bring += k; what += (what.empty() ? "" : ", ") + std::to_string(k) + " " + item; }
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
			for (const auto& [item, n] : p.State.Missing) any = any || (n - InFlight(p.Name, item) > 0 && m_Store.Count(item) > 0);
			return any ? job.Priority * liking * distance : 0.0f;
		}
		return 0.0f;
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
