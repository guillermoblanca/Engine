// H18 · the economy of the clan (E6 spoilage and preservation, E7 agriculture, E11 logistics, and the production chains): the part of the tribe
// that decides WHAT is made from what, what goes off, what is grown and where things are stored. It is kept apart from Tribe.cpp (the senses, the
// needs and the decision of each member) and TribeWork.cpp (who does which job) so they grow without stepping on each other. The hooks are few:
// Update calls StepEconomy, PostJobs calls PlanEconomy and PostEconomyJobs, JobScore asks EconomyJobScore, Decide calls EconomyOptions, Execute
// and Finish hand "make", "farm" and "haul" here, and the deposit goes through DepositPlace / DepositAt. Deterministic like the rest: maps in id
// order, ties to the first, no randomness (the rain on the fields comes from the season, not from dice).
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/GameplayTag.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/Spoilage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace GB
{
	namespace
	{
		// Each member's own place around a stockpile or a station: one point for all of them was a queue, and the ones behind waited for the crowd
		// (20 s) and gave up (the golden angle spreads any number of them round the circle)
		constexpr float kReachStore = 4.0f;   // what a stockpile holds is picked up within this of it
		const std::string kCampPile = "the pile by the fire";   // T255: the target of a haul from the camp to the store buildings

		// T214: with a body, a place it can stand on: a station by the river put some members' places in the water, where the way failed trip
		// after trip ("no way to 34.2 17.7", temperate seed 77, 18 times). The next places round the circle are tried; none: its own, as before.
		glm::vec2 Around(const glm::vec2& at, size_t index, float radius, const ITribeBody* body = nullptr)
		{
			for (size_t k = 0; k < 8; ++k)
			{
				const float a = static_cast<float>(index + k) * 2.3999632f;
				const glm::vec2 p = at + glm::vec2(std::cos(a), std::sin(a)) * radius;
				if (!body || body->CanStandAt(p)) return p;
			}
			const float a = static_cast<float>(index) * 2.3999632f;
			return at + glm::vec2(std::cos(a), std::sin(a)) * radius;
		}

		bool IsTool(const ItemLibrary& items, const std::string& id)
		{
			const ItemDef* d = items.Find(id);
			return d && d->Category == "tool";
		}

		bool Tagged(const ItemDef& d, const std::string& tag)
		{
			if (tag.empty()) return false;
			for (const std::string& t : d.Tags) if (GameplayTag::TagMatches(tag, t)) return true;
			return false;
		}

		// What a member keeps when it empties its bag at a store: its tools and its container (a basket is carried, not stored).
		bool Kept(const ItemLibrary& items, const std::string& id)
		{
			const ItemDef* d = items.Find(id);
			return d && (d->Category == "tool" || Tagged(*d, "Item.Container"));
		}

		int Carried(const ItemLibrary& items, const Inventory& bag)
		{
			int n = 0;
			for (const ItemSlot& s : bag.Stacks()) if (!Kept(items, s.Id)) n += s.Count;
			return n;
		}

		std::string Go(const glm::vec2& at, const std::string& tail)
		{
			char b[64];
			std::snprintf(b, sizeof b, "goto %.2f %.2f | ", static_cast<double>(at.x), static_cast<double>(at.y));
			return b + tail;
		}

		std::string Seconds(float s)
		{
			char b[32];
			std::snprintf(b, sizeof b, "wait %.2f", static_cast<double>(std::max(0.1f, s)));
			return b;
		}

		bool Takes(const StoreKind& kind, const ItemDef* d)
		{
			if (kind.Accepts.empty()) return true;
			if (!d) return false;
			for (const std::string& tag : kind.Accepts) if (Tagged(*d, tag)) return true;
			return false;
		}
	}

	// ------------------------------------------------------------------ stations, demands, stores
	bool Tribe::StationAt(const std::string& station, glm::vec2* where) const
	{
		const std::vector<std::string>& camp = Config.Econ.Production.CampStations;
		if (station.empty() || std::find(camp.begin(), camp.end(), station) != camp.end()) { if (where) *where = Config.Economy.Camp; return true; }
		glm::vec2 at = Config.Economy.Camp;
		if (m_Stations && m_Stations(station, at)) { if (where) *where = at; return true; }   // the buildings (H8)
		return false;
	}

	bool Tribe::BuiltStationAt(const std::string& station, glm::vec2* where) const
	{
		glm::vec2 at = Config.Economy.Camp;
		if (station.empty() || !m_Stations || !m_Stations(station, at)) return false;
		if (where) *where = at;
		return true;
	}

	void Tribe::SetDemand(const std::string& source, const std::string& item, int units)
	{
		if (units > 0) m_Demand[source][item] = units;
		else if (const auto it = m_Demand.find(source); it != m_Demand.end())
		{
			it->second.erase(item);
			if (it->second.empty()) m_Demand.erase(it);
		}
	}

	int Tribe::StoreCap(const std::string& item) const { return Config.Econ.Main().Cap + StoreBonus(!item.empty() && IsFood(item)); }   // (a cache pit or a granary adds to it, H8; to food more, W5)

	int Tribe::Stored(const std::string& item) const
	{
		int n = m_Store.Count(item);
		for (const Stockpile& p : m_Econ.Stockpiles) n += p.Store.Count(item);
		return n;
	}

	int Tribe::CarryLimit(const TribeMember& m) const
	{
		int slots = 0;
		if (m.Body && m_Items)
			if (const ItemDef* c = m.Body->Bag().BestContainer(*m_Items)) slots = c->BagSlots;
		return Config.Economy.DepositAt + static_cast<int>(std::floor(Config.Econ.Logistics.PerBagSlot * static_cast<float>(slots) + 1e-4f));
	}

	Stockpile* Tribe::FindStockpile(const std::string& name)
	{
		for (Stockpile& p : m_Econ.Stockpiles) if (p.Name == name) return &p;
		return nullptr;
	}

	Stockpile* Tribe::AddStockpile(const std::string& name, const glm::vec2& pos, const std::string& kind)
	{
		const auto k = Config.Econ.Logistics.Stores.find(kind);
		if (k == Config.Econ.Logistics.Stores.end() || kind == "main" || name.empty() || FindStockpile(name)) return nullptr;
		Stockpile p;
		p.Name = name;
		p.Kind = kind;
		p.Pos = pos;
		p.Store = Inventory(k->second.Slots, 0.0f);
		m_Econ.Stockpiles.push_back(std::move(p));
		return &m_Econ.Stockpiles.back();
	}

	Field& Tribe::MarkField(const glm::vec2& origin)
	{
		const FarmRules& f = Config.Econ.Farming;
		const std::string name = "field " + std::to_string(m_Econ.Fields.size() + 1);
		m_Econ.Fields.emplace_back(name, origin, f.Plots, f.Columns, f.Spacing);
		Field& field = m_Econ.Fields.back();
		if (!Config.Econ.Logistics.FieldStore.empty())   // the harvest is left beside the field and hauled to the camp (E11)
			AddStockpile(name + " store", field.Origin + glm::vec2(-f.Spacing, 0.0f), Config.Econ.Logistics.FieldStore);
		char at[64];
		std::snprintf(at, sizeof at, " at %.0f, %.0f", static_cast<double>(origin.x), static_cast<double>(origin.y));
		TellClan("marks out " + name + at + " (" + std::to_string(f.Plots) + " plots)");
		return field;
	}

	// ------------------------------------------------------------------ what the clan knows how to make, and who can farm
	bool Tribe::ClanKnows(const Recipe& r) const
	{
		if (!m_Knowledge.Gated()) return true;
		for (const TribeMember& m : m_Members)
			if (!m.Dead && m.Body && m_Knowledge.CanMake(m.Name, r.Id)) return true;
		return false;
	}

	bool Tribe::CanFarm(const TribeMember& m) const
	{
		const std::string& tech = Config.Econ.Farming.Tech;
		return tech.empty() || !m_Knowledge.Gated() || m_Knowledge.Knows(m.Name, tech);
	}

	// The food eaten first from a bag or a store: what goes off soonest (the least life left; what never spoils last), then the lowest id.
	std::string Tribe::FoodToEat(const Inventory& inv) const
	{
		std::string best;
		float left = 0.0f;
		for (const ItemSlot& s : inv.Stacks())
		{
			if (s.Count <= 0 || !IsFood(s.Id)) continue;
			const float l = LifeLeft(*m_Items, s);
			if (best.empty() || l < left || (l == left && s.Id < best)) { best = s.Id; left = l; }
		}
		return best;
	}

	// ------------------------------------------------------------------ the plan
	std::vector<ProductionWant> Tribe::Wants() const
	{
		std::vector<ProductionWant> out;
		if (!m_Items || !m_Recipes) return out;
		const ProductionRules& p = Config.Econ.Production;
		const std::vector<std::string> ids = m_Recipes->Ids();
		// The best recipe (by power of its output, then speed, then id) whose output carries `tag`: known first, else one to try (E5).
		auto bestMaking = [&](const std::string& tag, int above, bool& known) -> const Recipe*
		{
			const Recipe* best = nullptr;
			const ItemDef* bestDef = nullptr;
			known = false;
			for (int pass = 0; pass < 2 && !best; ++pass)
				for (const std::string& id : ids)
				{
					const Recipe* r = m_Recipes->Find(id);
					const ItemDef* d = r ? m_Items->Find(r->Output) : nullptr;
					if (!d || !Tagged(*d, tag) || d->Power <= above || !StationAt(r->Station)) continue;
					if (pass == 0 && !ClanKnows(*r)) continue;
					if (!best || d->Power > bestDef->Power || (d->Power == bestDef->Power && d->Speed > bestDef->Speed)) { best = r; bestDef = d; }
				}
			known = best && ClanKnows(*best);
			return best;
		};

		// 1) the tool of a trade a member lacks: the member makes it itself ("craft" job), but what it is made of is planned here
		std::map<std::string, int> lacking;
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			const auto role = Config.Roles.find(m.Role);
			if (role == Config.Roles.end() || role->second.Tool.empty()) continue;
			if (m.Body->Bag().CountTagged(*m_Items, role->second.ToolTag) == 0) ++lacking[role->second.Tool];
		}
		for (const auto& [tool, n] : lacking) out.push_back({ tool, n, std::to_string(n) + " of the clan lack a " + tool, false });

		// 2) a better tool of a member's trade, once the clan knows how to make one (a flint axe for a woodcutter with a stone one)
		if (p.Upgrade)
		{
			std::map<std::string, int> better;
			for (const TribeMember& m : m_Members)
			{
				if (m.Dead || !m.Body) continue;
				const auto role = Config.Roles.find(m.Role);
				if (role == Config.Roles.end() || role->second.ToolTag.empty()) continue;
				const ItemDef* mine = m.Body->Bag().BestTool(*m_Items, role->second.ToolTag);
				if (!mine) continue;   // (it lacks one: that is 1)
				bool known = false;
				if (const Recipe* r = bestMaking(role->second.ToolTag, mine->Power, known); r && known) ++better[r->Output];
			}
			for (const auto& [tool, n] : better) out.push_back({ tool, n, std::to_string(n) + " of the clan could use a " + tool });
		}

		// 3) a container (a basket) for each member that carries none: more is carried per trip (E11)
		if (p.ContainersPerMember > 0.0f)
		{
			int without = 0;
			for (const TribeMember& m : m_Members) if (!m.Dead && m.Body && !m.Body->Bag().BestContainer(*m_Items)) ++without;
			const ItemDef* best = nullptr;
			const Recipe* recipe = nullptr;
			// an input the clan can come by: in the store, from a node, or made where it can make it (leather wants a tannery: without one, the basket)
			auto obtainable = [&](const std::string& item, int pass)
			{
				if (m_Store.Count(item) > 0) return true;
				for (const auto& [kind, node] : m_Defs) if (node.Item == item) return true;
				for (const std::string& rid : ids)
					if (const Recipe* q = m_Recipes->Find(rid); q && q->Output == item && StationAt(q->Station) && (pass == 1 || ClanKnows(*q))) return true;
				return false;
			};
			for (int pass = 0; pass < 2 && !best; ++pass)
				for (const std::string& id : ids)
				{
					const Recipe* r = m_Recipes->Find(id);
					const ItemDef* d = r ? m_Items->Find(r->Output) : nullptr;
					if (!d || !Tagged(*d, "Item.Container") || !StationAt(r->Station) || (pass == 0 && !ClanKnows(*r))) continue;
					if (!std::all_of(r->Inputs.begin(), r->Inputs.end(), [&](const auto& in) { return obtainable(in.first, pass); })) continue;
					if (!best || d->BagSlots > best->BagSlots) { best = d; recipe = r; }
				}
			const int units = static_cast<int>(std::ceil(static_cast<float>(without) * p.ContainersPerMember - 1e-4f));
			if (best && recipe && units > 0) out.push_back({ best->Id, units, std::to_string(without) + " of the clan carry no container" });
		}

		// 4) the farm tool (a hoe) once there is a field to till, or the clan could mark one out (it knows how and has seeds)
		const FarmRules& farm = Config.Econ.Farming;
		if (!farm.ToolTag.empty())
		{
			bool farming = !m_Econ.Fields.empty();
			if (!farming && static_cast<int>(m_Econ.Fields.size()) < FieldsWanted())
			{
				bool seeds = false, can = false;
				for (const auto& [id, crop] : Config.Econ.Crops) seeds = seeds || Stored(crop.Seed) > 0;
				for (const TribeMember& m : m_Members) can = can || (!m.Dead && m.Body && CanFarm(m));
				farming = seeds && can;
			}
			if (farming)
			{
				int hoes = m_Store.CountTagged(*m_Items, farm.ToolTag);
				for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) hoes += m.Body->Bag().CountTagged(*m_Items, farm.ToolTag);
				bool known = false;
				if (const Recipe* r = bestMaking(farm.ToolTag, -1, known); r && hoes == 0) out.push_back({ r->Output, 1, "the field needs a " + r->Output });
			}
		}

		// 5) what others ask for (a building site's materials, H8)
		for (const auto& [source, items] : m_Demand)
			for (const auto& [item, n] : items) out.push_back({ item, n, source + " needs " + std::to_string(n) + " " + item });

		// 6) the intermediates the store keeps (cordage: so that what needs it can be made, or tried, at once)
		for (const auto& [item, n] : p.Stock)
			if (n > 0) out.push_back({ item, n, "the store keeps " + std::to_string(n) + " " + item });

		// 7) preserving (E6): perishable food above PreserveAbove x its stock target is turned into something that keeps longer, when a known recipe
		// and its station allow (berries -> dried berries at the drying rack); of several, the one whose output keeps longest (meat: smoked before cooked)
		if (p.PreserveAbove > 0.0f)
		{
			std::vector<std::string> foods;
			for (const ItemSlot& s : m_Store.Stacks()) if (std::find(foods.begin(), foods.end(), s.Id) == foods.end()) foods.push_back(s.Id);
			std::sort(foods.begin(), foods.end());
			for (const std::string& food : foods)
			{
				const ItemDef* d = m_Items->Find(food);
				if (!d || !d->Perishable() || !IsFood(food)) continue;
				const int surplus = m_Store.Count(food) - static_cast<int>(std::ceil(p.PreserveAbove * StockWanted(food)));
				if (surplus <= 0) continue;
				const Recipe* best = nullptr;
				float bestLife = 0.0f;
				for (const std::string& id : ids)
				{
					const Recipe* r = m_Recipes->Find(id);
					const ItemDef* out_ = r ? m_Items->Find(r->Output) : nullptr;
					const auto in = r ? r->Inputs.find(food) : std::map<std::string, int>::const_iterator();
					if (!out_ || in == r->Inputs.end() || (out_->Perishable() && out_->ShelfLife <= d->ShelfLife) || !ClanKnows(*r) || !StationAt(r->Station)) continue;
					const float life = out_->Perishable() ? out_->ShelfLife : 1e30f;   // (what never goes off keeps longest)
					if (!best || life > bestLife) { best = r; bestLife = life; }
				}
				// (a store full of what keeps: no more of it; 2000 dried berries in twelve days filled every slot). The same test the maker makes (a
				// batch past the cap is not made): with "below the cap" a store at 79 of 80 planned a batch the maker refused, 2800 times in 12 days
				if (best && m_Store.Count(best->Output) + std::max(1, best->OutputCount) <= StoreCap(best->Output))
				{
					const int batches = surplus / std::max(1, best->Inputs.at(food));
					if (batches >= 1)
						out.push_back({ best->Output, m_Store.Count(best->Output) + batches * std::max(1, best->OutputCount), "keeps " + std::to_string(surplus) + " " + food + " from spoiling" });
				}
			}
		}
		return out;
	}

	ProductionPlan Tribe::PlanNow() const
	{
		if (!m_Items || !m_Recipes) return {};
		std::map<std::string, int> stock;
		for (const ItemSlot& s : m_Store.Stacks()) stock[s.Id] += s.Count;
		const ProductionRules& p = Config.Econ.Production;
		ProductionQuery q;
		q.Depth = p.Depth;
		q.Experiment = p.Experiment;
		q.MaxBatches = p.MaxBatches;
		q.Known = [this](const Recipe& r) { return ClanKnows(r); };
		q.Learnable = [this](const Recipe& r)
		{
			const TechTree* tree = m_Knowledge.Techs();
			if (!tree) return true;
			for (const std::string& tech : tree->ForRecipe(r.Id))
				if (tree->Find(tech)->Discovery.Experiment > 0.0f)
					for (const TribeMember& m : m_Members)
						if (!m.Dead && m.Body && m_Knowledge.CanLearn(m.Name, tech)) return true;
			return false;
		};
		q.Station = [this](const std::string& s) { return StationAt(s); };
		return PlanProduction(Wants(), stock, *m_Recipes, q);
	}

	void Tribe::PlanEconomy() { m_Plan = PlanNow(); }

	// ------------------------------------------------------------------ the jobs
	void Tribe::PostEconomyJobs()
	{
		const JobRules& rules = Config.Work.Jobs;
		const EconomyRules& econ = Config.Econ;
		// make: every step of the plan that can start now (its inputs are in the store); a deeper step (an intermediate) is a little more pressing
		for (const ProductionStep& st : m_Plan.Steps)
		{
			if (!st.Ready) continue;
			m_Board.Post("make", st.Recipe, SkillFamilyOf("craft"), static_cast<float>(st.Batches), econ.Production.Priority * (st.Depth > 0 ? 1.1f : 1.0f), 1,
				"make " + st.Output + ": " + st.Why, m_Time, rules);
		}

		// farm: mark out a field once the clan knows how and has seeds; then till, sow (in its season), water and harvest
		const FarmRules& f = econ.Farming;
		const Season season = CurrentSeason();
		if (static_cast<int>(m_Econ.Fields.size()) < FieldsWanted() && season != Season::Winter)
		{
			bool seeds = false, can = false;
			for (const auto& [id, crop] : econ.Crops) seeds = seeds || Stored(crop.Seed) > 0;
			for (const TribeMember& m : m_Members) can = can || (!m.Dead && m.Body && CanFarm(m));
			if (seeds && can)
				MarkField(Config.Economy.Camp + f.Offset + glm::vec2(0.0f, static_cast<float>(m_Econ.Fields.size()) * (static_cast<float>((f.Plots + f.Columns - 1) / f.Columns) + 1.0f) * f.Spacing));
		}
		if (!m_Econ.Fields.empty())
		{
			int grass = 0, dug = 0, dry = 0, grown = 0;
			for (const Field& field : m_Econ.Fields)
			{
				grass += field.Count("grass");
				dug += field.Count("dug");
				dry += static_cast<int>(field.Dry(field.Plots.size()).size());
				grown += field.Count("grown");
			}
			int hoes = f.ToolTag.empty() ? 1 : m_Store.CountTagged(*m_Items, f.ToolTag);
			for (const TribeMember& m : m_Members) if (!m.Dead && m.Body && !f.ToolTag.empty()) hoes += m.Body->Bag().CountTagged(*m_Items, f.ToolTag);
			std::string sowable;
			for (const auto& [id, crop] : econ.Crops) if (sowable.empty() && crop.SowsIn(season) && m_Store.Count(crop.Seed) > 0) sowable = id;
			const int trip = std::max(1, f.PlotsPerTrip);
			auto slots = [&](int plots) { return std::clamp((plots + trip - 1) / trip, 1, 2); };
			if (grass > 0 && hoes > 0 && season != Season::Winter)
				m_Board.Post("farm", "till", "", static_cast<float>(grass), f.Priority, slots(grass), std::to_string(grass) + " plots of the field are still grass", m_Time, rules);
			if (dug > 0 && !sowable.empty())
				m_Board.Post("farm", "sow", "", static_cast<float>(dug), f.Priority * 1.1f, slots(dug), std::to_string(dug) + " plots are tilled and it is the season to sow " + sowable, m_Time, rules);
			if (dry > 0)
				m_Board.Post("farm", "tend", "", static_cast<float>(dry), f.Priority, slots(dry), std::to_string(dry) + " plots of growing crops are dry", m_Time, rules);
			if (grown > 0)
				m_Board.Post("farm", "harvest", "", static_cast<float>(grown), f.Priority * 1.3f, slots(grown), std::to_string(grown) + " plots are ripe", m_Time, rules);
		}

		// haul: a stockpile whose kind is hauled holds things: someone carries them to the main store
		for (const Stockpile& p : m_Econ.Stockpiles)
		{
			const auto kind = econ.Logistics.Stores.find(p.Kind);
			if (kind == econ.Logistics.Stores.end() || !kind->second.Haul) continue;
			int units = 0;
			bool fits = false;   // L2: no haul while the main store has no room for any of it (the hauler would carry it back and forth)
			for (const ItemSlot& s : p.Store.Stacks())
			{
				if (m_Store.Count(s.Id) >= StoreCap(s.Id)) continue;   // T248: past the main store's cap it stays where it is (it would be thrown away there)
				units += s.Count;
				if (!fits) { Inventory probe = m_Store; fits = probe.Add(*m_Items, s.Id, 1) > 0; }
			}
			if (fits && units >= econ.Logistics.HaulAt)
				m_Board.Post("haul", p.Name, "", static_cast<float>(units), econ.Logistics.Priority, 1, p.Name + " holds " + std::to_string(units) + " things for the store", m_Time, rules);
		}
		// T255: the pile by the fire past its reserve is carried to the store buildings with room for it (the first one built empties the old pile)
		if (ClanStores::Active(*this))
		{
			int units = 0;
			std::set<std::string> seen;
			for (const ItemSlot& s : m_Store.Stacks())
				if (seen.insert(s.Id).second)
					if (const int n = m_Stores.Surplus(*this, s.Id); n > 0 && !m_Stores.Destination(*this, s.Id, Config.Economy.Camp).empty()) units += n;
			if (units >= econ.Logistics.HaulAt)
				m_Board.Post("haul", kCampPile, "", static_cast<float>(units), econ.Logistics.Priority, units >= 4 * econ.Logistics.HaulAt ? 2 : 1,
					"the pile by the fire holds " + std::to_string(units) + " things for the store buildings", m_Time, rules);
			m_Stores.PostRestock(*this);   // T256: and the fire's reserve is filled again from the buildings
		}
	}

	float Tribe::EconomyJobScore(const TribeMember& m, const Job& job) const
	{
		if (!m.Body || !m_Items || !m_Recipes) return 0.0f;
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(m.Name) : nullptr;
		const float vigor = asc && asc->Attributes().Has(NeedName::Vigor) ? asc->Attribute(NeedName::Vigor) : 1.0f;
		const glm::vec2 pos = Where(m);
		Inventory& bag = m.Body->Bag();
		auto liking = [&](const char* kind) { return 0.5f + 0.5f * std::clamp(MemberWeight(m, kind), 0.0f, 1.5f); };
		if (job.Kind == "make")
		{
			const Recipe* r = m_Recipes->Find(job.Target);
			glm::vec2 at;
			if (!r || !StationAt(r->Station, &at)) return 0.0f;
			if (m_Knowledge.Gated() && !m_Knowledge.CanMake(m.Name, r->Id)) return 0.0f;   // only who knows how (H11)
			if (!m_Recipes->WhyNot(*m_Items, m_Store, r->Id, r->Station).empty()) return 0.0f;   // the inputs are not (or no longer) in the store
			return job.Priority * liking("craft") * Falloff(glm::distance(pos, at), 100.0f) * vigor;
		}
		if (job.Kind == "farm")
		{
			if (m_Econ.Fields.empty() || !CanFarm(m)) return 0.0f;
			const std::string& tag = Config.Econ.Farming.ToolTag;
			if (job.Target == "till" && !tag.empty() && bag.CountTagged(*m_Items, tag) == 0 && m_Store.CountTagged(*m_Items, tag) == 0) return 0.0f;
			if (job.Target == "harvest" && Carried(*m_Items, bag) >= CarryLimit(m)) return 0.0f;   // no room for the harvest
			return job.Priority * liking("farm") * Falloff(glm::distance(pos, m_Econ.Fields.front().Centre()), 60.0f) * vigor;
		}
		if (job.Kind == "haul")
		{
			const Stockpile* pile = nullptr;
			for (const Stockpile& p : m_Econ.Stockpiles) if (p.Name == job.Target) pile = &p;
			const bool camp = job.Target == kCampPile || job.Target == ClanStores::kRestock;   // T255, T256
			if ((!pile && !camp) || Carried(*m_Items, bag) > 0) return 0.0f;   // it goes with an empty bag
			const float load = std::clamp(static_cast<float>(CarryLimit(m)) / static_cast<float>(std::max(1, Config.Economy.DepositAt)), 1.0f, 3.0f) / 3.0f;   // L3: who carries more (a basket) hauls
			return job.Priority * liking("haul") * Falloff(glm::distance(pos, camp ? Config.Economy.Camp : pile->Pos), 60.0f) * vigor * (0.5f + 0.5f * load);
		}
		return 0.0f;
	}

	// The option that does the economy job a member holds: one, where the work is (JobBonus then adds the "job" factor).
	void Tribe::EconomyOptions(TribeMember& m, std::vector<UtilityOption>& options)
	{
		const Job* held = m_Board.ClaimOf(m.Name);
		if (!held || (held->Kind != "make" && held->Kind != "farm" && held->Kind != "haul")) return;
		if (EconomyJobScore(m, *held) <= 0.0f) return;
		UtilityOption o;
		o.Kind = held->Kind;
		o.Target = held->Target;
		o.Where = Config.Economy.Camp;
		if (held->Kind == "make") { if (const Recipe* r = m_Recipes->Find(held->Target)) StationAt(r->Station, &o.Where); }
		else if (held->Kind == "farm") o.Where = m_Econ.Fields.front().Centre();
		else for (const Stockpile& p : m_Econ.Stockpiles) if (p.Name == held->Target) o.Where = p.Pos;
		const float reach = Falloff(glm::distance(Where(m), o.Where), 100.0f);
		o.Score = 0.9f * reach;
		o.Commit = 5.0f;
		o.Factors.push_back({ "work", 0.9f });
		o.Factors.push_back({ "distance", reach });
		o.Note = held->Why;
		options.push_back(o);
	}

	bool Tribe::ExecuteEconomy(TribeMember& m, const UtilityOption& o)
	{
		Inventory& bag = m.Body->Bag();
		const EconomyRules& econ = Config.Econ;
		if (o.Kind == "make")
		{
			const Recipe* r = m_Recipes->Find(o.Target);
			glm::vec2 at;
			if (!r || !StationAt(r->Station, &at)) return false;
			const glm::vec2 spot = at == Config.Economy.Camp ? CampSpot(m) : Around(at, IndexOf(m), 1.2f, m.Body);
			// T255: what the fire lacks of the inputs it fetches on the way from the store building that holds them (they are made with by the fire's share)
			std::string fetch;
			if (ClanStores::Active(*this))
				for (const auto& [id, n] : r->Inputs)
				{
					const int lack = n - m_Stores.AtCamp(*this, id);
					StoreLayout::Place from;
					if (lack <= 0) continue;
					if (const std::string source = m_Stores.Source(*this, id, lack, Where(m), &from); !source.empty() && source != "-" && m_Stores.Take(source, id, lack) > 0 && fetch.empty())
						fetch = Go(Around(from.Door, IndexOf(m), 0.9f, m.Body), "wait 1") + " | ";
				}
			if (!Order(m, fetch + Go(spot, Seconds(econ.Production.Seconds)))) return false;
			m.Job = "make";
			m.Detail = r->Id;
			Say(m, "goes to make " + r->Output + (r->Station.empty() ? std::string() : " at the " + r->Station));
			return true;
		}
		if (o.Kind == "haul")
		{
			if (o.Target == ClanStores::kRestock) return m_Stores.StartRestock(*this, m);   // T256: to a store building for the fire's reserve
			if (o.Target == kCampPile)   // T255: to the pile by the fire
			{
				if (!Order(m, Go(CampSpot(m), "wait 1"))) return false;
				m.Job = "haul";
				m.Detail = kCampPile;
				Say(m, "goes to carry the pile by the fire to the store");
				return true;
			}
			const Stockpile* pile = FindStockpile(o.Target);
			if (!pile) return false;
			if (!Order(m, Go(Around(pile->Pos, IndexOf(m), 0.9f, m.Body), "wait 1"))) return false;
			m.Job = "haul";
			m.Detail = pile->Name;
			Say(m, "goes to fetch what " + pile->Name + " holds");
			return true;
		}
		// farm: up to PlotsPerTrip plots of the first field with that work
		const FarmRules& f = econ.Farming;
		const std::string& action = o.Target;
		const size_t most = static_cast<size_t>(std::max(1, f.PlotsPerTrip));
		size_t fieldIndex = 0;
		std::vector<size_t> plots;
		for (size_t i = 0; i < m_Econ.Fields.size() && plots.empty(); ++i)
		{
			const Field& field = m_Econ.Fields[i];
			plots = action == "till" ? field.Find("grass", most) : action == "sow" ? field.Find("dug", most) : action == "tend" ? field.Dry(most) : field.Find("grown", most);
			fieldIndex = i;
		}
		if (plots.empty()) return false;
		std::string crop = "-";
		if (action == "till" && !f.ToolTag.empty() && bag.CountTagged(*m_Items, f.ToolTag) == 0)
		{
			// it takes the hoe from the store (and keeps it: the farmer's tool)
			const ItemDef* hoe = m_Store.BestTool(*m_Items, f.ToolTag);
			if (!hoe || m_Store.Transfer(*m_Items, bag, hoe->Id, 1) != 1) return false;
			Say(m, "takes the " + hoe->Id + " from the store");
		}
		if (action == "sow")
		{
			const Season season = CurrentSeason();
			for (const auto& [id, c] : econ.Crops) if (crop == "-" && c.SowsIn(season) && m_Store.Count(c.Seed) > 0) crop = id;
			if (crop == "-") return false;
			const std::string& seed = econ.Crops.at(crop).Seed;
			const int have = bag.Count(seed);
			if (have < static_cast<int>(plots.size())) m_Store.Transfer(*m_Items, bag, seed, static_cast<int>(plots.size()) - have);
			plots.resize(std::min(plots.size(), static_cast<size_t>(bag.Count(seed))));
			if (plots.empty()) return false;
		}
		const Field& field = m_Econ.Fields[fieldIndex];
		std::string orders, detail = action + " " + std::to_string(fieldIndex) + " " + crop;
		for (size_t i : plots)
		{
			orders += (orders.empty() ? "" : " | ") + Go(field.PlotPos(i), Seconds(f.WorkSeconds));
			detail += " " + std::to_string(i);
		}
		if (!Order(m, orders)) return false;
		m.Job = "farm";
		m.Detail = detail;
		Say(m, "goes to " + action + " " + std::to_string(plots.size()) + " plots of " + field.Name);
		return true;
	}

	bool Tribe::FinishEconomy(TribeMember& m, const std::string& job)
	{
		if (job != "make" && job != "farm" && job != "haul") return false;
		Inventory& bag = m.Body->Bag();
		const EconomyRules& econ = Config.Econ;
		if (HasFailed(m)) return true;   // (said above, in Finish; a farmer keeps the seeds it took and leaves them at the store)
		if (job == "make")
		{
			const Recipe* r = m_Recipes->Find(m.Detail);
			if (!r) return true;
			const std::set<std::string> known = m_Knowledge.KnownRecipes(m.Name, *m_Recipes);
			const std::set<std::string>* gate = m_Knowledge.Gated() ? &known : nullptr;
			if (m_Store.Count(r->Output) > 0 && m_Store.Count(r->Output) + r->OutputCount > StoreCap(r->Output))   // (a batch past the cap: 102 dried berries in a store of 100)
			{
				Say(m, "does not make " + r->Output + ": the store is full of it");
				return true;
			}
			if (StationAt(r->Station) && m_Recipes->Craft(*m_Items, m_Store, r->Id, r->Station, gate))
			{
				Witnessed(m, r->Id, std::string());
				m_Econ.Made[r->Output] += r->OutputCount;
				++m.Made;
				Say(m, "makes " + std::to_string(r->OutputCount) + " " + r->Output + " for the store");
				Practise(m, "craft", "craft", 1.0f);
				if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "make") m_Board.Progress(m.Name, 1.0f, m_Time);
				if (glm::distance(Where(m), Config.Economy.Camp) < 6.0f) HandOut(m);
			}
			else Say(m, "cannot make " + r->Output + ": " + (StationAt(r->Station) ? m_Recipes->WhyNot(*m_Items, m_Store, r->Id, r->Station, gate) : "there is no " + r->Station));
			Share(m);
			return true;
		}
		// T255: where a hauler carries what it picked up: the store building the first of it goes to ("hauled@<building>"), else the fire
		auto carryOn = [&]()
		{
			StoreLayout::Place to;
			std::string shed;
			if (ClanStores::Active(*this))
				for (const ItemSlot& s : bag.Stacks())
					if (shed.empty() && !Kept(*m_Items, s.Id)) shed = m_Stores.Destination(*this, s.Id, Where(m), &to);
			const glm::vec2 spot = shed.empty() ? CampSpot(m) : Around(to.Door, IndexOf(m), 0.9f, m.Body);
			if (Order(m, Go(spot, "wait 1"))) { m.Job = "deposit"; m.Detail = shed.empty() ? std::string("hauled") : "hauled@" + shed; }
		};
		if (job == "haul" && m.Detail.rfind(ClanStores::kRestock, 0) == 0) return m_Stores.FinishRestock(*this, m);   // T256
		if (job == "haul" && m.Detail == kCampPile)
		{
			if (glm::distance(Where(m), Config.Economy.Camp) > 9.0f) { Say(m, "could not get to the pile by the fire"); return true; }
			// the most of one building's things, past the fire's reserve
			int room = CarryLimit(m), moved = 0;
			std::string shed;
			std::set<std::string> seen;
			for (const ItemSlot& s : std::vector<ItemSlot>(m_Store.Stacks()))
			{
				if (room <= 0 || !seen.insert(s.Id).second) continue;
				const int surplus = m_Stores.Surplus(*this, s.Id);
				StoreLayout::Place place;
				const std::string to = surplus > 0 ? m_Stores.Destination(*this, s.Id, Config.Economy.Camp, &place) : std::string();
				if (to.empty() || (!shed.empty() && to != shed)) continue;
				shed = to;
				const int n = m_Store.Transfer(*m_Items, bag, s.Id, std::min({ room, surplus, StoreLayout::Free(*this, place, s.Id) }));
				room -= n;
				moved += n;
			}
			if (moved == 0) return true;
			m_Econ.Hauled += moved;
			if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "haul") m_Board.Progress(m.Name, static_cast<float>(moved), m_Time);
			Say(m, "picks up " + std::to_string(moved) + " things from the pile by the fire for the store at " + shed);
			carryOn();
			return true;
		}
		if (job == "haul")
		{
			Stockpile* pile = FindStockpile(m.Detail);
			if (!pile) return true;
			if (glm::distance(Where(m), pile->Pos) > kReachStore) { Say(m, "could not get to " + pile->Name); return true; }   // (what it holds is not picked up from afar)
			int room = CarryLimit(m), moved = 0;
			for (const ItemSlot& s : std::vector<ItemSlot>(pile->Store.Stacks()))
			{
				if (room <= 0) break;
				const int wanted = std::min(s.Count, StoreCap(s.Id) - m_Store.Count(s.Id) - bag.Count(s.Id));   // T248: only what the main store still takes
				if (wanted <= 0) continue;
				const int n = pile->Store.Transfer(*m_Items, bag, s.Id, std::min(room, wanted));
				room -= n;
				moved += n;
			}
			if (moved == 0) return true;
			m_Econ.Hauled += moved;
			if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "haul") m_Board.Progress(m.Name, static_cast<float>(moved), m_Time);
			Say(m, "picks up " + std::to_string(moved) + " things at " + pile->Name + " for the store");
			// and carries them straight to the main store (a deposit whose place is not a stockpile: the main store; T255: its store building)
			carryOn();
			return true;
		}
		// farm: "<action> <field> <crop|-> <plot> <plot>..."
		std::istringstream in(m.Detail);
		std::string action, crop;
		size_t fieldIndex = 0;
		in >> action >> fieldIndex >> crop;
		if (fieldIndex >= m_Econ.Fields.size()) return true;
		Field& field = m_Econ.Fields[fieldIndex];
		const FarmRules& f = econ.Farming;
		int done = 0;
		std::map<std::string, int> got;
		// a better hoe reaps more: its Power is added to what each plot gives of its crop (not of its seed)
		const ItemDef* hoe = f.ToolTag.empty() ? nullptr : bag.BestTool(*m_Items, f.ToolTag);
		const int reap = hoe ? std::max(0, hoe->Power) : 0;
		for (size_t i; in >> i;)
		{
			if (i >= field.Plots.size()) continue;
			FieldPlot& p = field.Plots[i];
			if (action == "till" && p.State == "grass") { p.State = "dug"; ++done; }
			else if (action == "sow" && p.State == "dug" && econ.Crops.count(crop) && bag.Remove(econ.Crops.at(crop).Seed, 1) == 1)
			{
				p = FieldPlot{ "sown", crop, 0.0f, f.MoistureSeconds };   // sown and watered
				++done;
			}
			else if (action == "tend" && (p.State == "sown" || p.State == "sprout")) { p.Moisture = f.MoistureSeconds; ++done; }
			else if (action == "harvest" && p.State == "grown")
			{
				if (const auto c = econ.Crops.find(p.Crop); c != econ.Crops.end())
				{
					// what the plot gives must fit in the bag: else the rest stays ripe for the next trip (it used to go straight into the store past
					// its cap: 870 grain in a store of 100 after twelve days of five fields)
					std::map<std::string, int> yield = c->second.Yield;
					for (auto& [item, n] : yield) if (item != c->second.Seed) n += reap;
					Inventory probe = bag;
					bool fits = true;
					for (const auto& [item, n] : yield) fits = fits && probe.Add(*m_Items, item, n) == n;
					if (!fits) break;
					for (const auto& [item, n] : yield)
					{
						bag.Add(*m_Items, item, n);
						got[item] += n;
						m_Econ.Harvested[item] += n;
					}
				}
				p = FieldPlot{ "dug", "", 0.0f, 0.0f };   // the soil stays tilled for the next sowing
				++done;
			}
		}
		if (done > 0)
		{
			if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "farm") m_Board.Progress(m.Name, static_cast<float>(done), m_Time);
			std::string what;
			for (const auto& [item, n] : got) what += (what.empty() ? ": " : ", ") + std::to_string(n) + " " + item;
			const char* verb = action == "till" ? "tills" : action == "sow" ? "sows" : action == "tend" ? "waters" : "harvests";
			Say(m, std::string(verb) + " " + std::to_string(done) + " plots of " + field.Name + (action == "sow" ? " with " + crop : std::string()) + what);
			Practise(m, "farm", "farm", static_cast<float>(done));   // N3: the farmer's trade
		}
		return true;
	}

	// At the camp: a better tool of its trade than the one it carries, and a container if it has none, are taken from the store (the old tool goes back).
	void Tribe::HandOut(TribeMember& m, const std::string& at)
	{
		if (!m.Body || !m_Items) return;
		Inventory& bag = m.Body->Bag();
		bool changed = false;
		// T255: only what lies where it stands: the fire's share at the camp, a building's in it (the whole store while the clan has none)
		const Inventory here = m_Stores.At(*this, at);
		auto take = [&](const std::string& id)
		{
			if (here.Count(id) <= 0 || m_Store.Transfer(*m_Items, bag, id, 1) != 1) return false;
			m_Stores.Take(at, id, 1);
			return true;
		};
		auto leave = [&](const std::string& id) { if (bag.Transfer(*m_Items, m_Store, id, 1) == 1) m_Stores.Put(at, id, 1); };
		if (const auto role = Config.Roles.find(m.Role); role != Config.Roles.end() && !role->second.ToolTag.empty())
		{
			const std::string& tag = role->second.ToolTag;
			const ItemDef* best = here.BestTool(*m_Items, tag);
			const ItemDef* mine = bag.BestTool(*m_Items, tag);
			if (best && (!mine || best->Power > mine->Power || (best->Power == mine->Power && best->Speed > mine->Speed)) && take(best->Id))
			{
				const std::string old = mine ? mine->Id : std::string();
				if (!old.empty()) leave(old);
				Say(m, "takes the " + best->Id + " from the store" + (old.empty() ? std::string() : " and leaves its " + old));
				changed = true;
			}
		}
		if (!bag.BestContainer(*m_Items))
			if (const ItemDef* c = here.BestContainer(*m_Items); c && take(c->Id))
			{
				Say(m, "takes a " + c->Id + " from the store: it can carry more");
				changed = true;
			}
		if (changed)
		{
			bag.SyncCapacity(*m_Items);
			if (AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(m.Name) : nullptr) ResourceSystem::SyncTools(*asc, bag, *m_Items);
		}
	}

	// ------------------------------------------------------------------ where things are left
	// Where it leaves what it carries: the nearest store that has room for some of it (the main store at the camp, "" ; a stockpile, by name;
	// "-" when none has: then it does not go). A stockpile must be clearly nearer than the camp, unless the main store is full: 1399 trips in a
	// row left 0 in a full store while three storages stood empty.
	std::string Tribe::DepositPlace(const TribeMember& m, glm::vec2& where) const
	{
		if (!m.Body || !m_Items) return {};
		const glm::vec2 pos = Where(m);
		auto room = [&](const Inventory& to, const StoreKind* kind, bool capped = true)   // a copy is tried: the slots, the stacks and the caps decide
		{
			Inventory probe = to;
			for (const ItemSlot& s : m.Body->Bag().Stacks())
			{
				if (Kept(*m_Items, s.Id)) continue;
				if (kind && (!Takes(*kind, m_Items->Find(s.Id)) || to.Count(s.Id) >= kind->Cap)) continue;
				if (!kind && capped && to.Count(s.Id) >= StoreCap(s.Id)) continue;   // T248: the main store's cap too (else 539 units thrown away in 3 days, the storage empty)
				if (probe.Add(*m_Items, s.Id, 1) > 0) return true;
			}
			return false;
		};
		const bool main = room(m_Store, nullptr);
		float best = main ? glm::distance(pos, Config.Economy.Camp) - 1.0f : 1e30f;
		std::string place = main ? std::string() : std::string("-");
		// T255: with store buildings the main store is them: each thing goes to its building (ClanStores::Destination), or to the fire while the
		// fire is short of its reserve or no building has room for it; the trip is to the nearest of those places ("@<building>" or "")
		if (main && ClanStores::Active(*this))
		{
			best = 1e30f;
			for (const ItemSlot& s : m.Body->Bag().Stacks())
			{
				if (Kept(*m_Items, s.Id) || m_Store.Count(s.Id) >= StoreCap(s.Id)) continue;
				StoreLayout::Place building;
				const std::string to = m_Stores.ReserveShort(*this, s.Id) > 0 ? std::string() : m_Stores.Destination(*this, s.Id, pos, &building);
				const float d = to.empty() ? glm::distance(pos, Config.Economy.Camp) - 1.0f : glm::distance(pos, building.Door);
				if (d >= best) continue;
				best = d;
				place = to.empty() ? std::string() : "@" + to;
				where = to.empty() ? CampSpot(m) : Around(building.Door, IndexOf(m), 0.9f, m.Body);
			}
		}
		for (const Stockpile& p : m_Econ.Stockpiles)
		{
			const auto kind = Config.Econ.Logistics.Stores.find(p.Kind);
			if (kind == Config.Econ.Logistics.Stores.end() || !room(p.Store, &kind->second)) continue;
			if (main && kind->second.Haul) continue;   // L1: what is left at a hauled stockpile is carried again to the main store: only when that is full
			const float d = glm::distance(pos, p.Pos);
			if (d < best) { best = d; place = p.Name; where = Around(p.Pos, IndexOf(m), 0.9f, m.Body); }
		}
		if (place == "-" && room(m_Store, nullptr, false)) place.clear();   // no store has room under its cap: to the main store all the same (it leaves what is past the cap; a bag never clogs)
		return place;
	}

	int Tribe::ToPantry(TribeMember& m, const std::string& item, int count)
	{
		const std::string& property = Config.Economy.Property;
		if (!m.Body || !m_Items || count <= 0 || property == "communal" || property.empty()) return 0;
		const Household* h = HouseholdOf(m.Name);
		if (!h) return 0;
		Inventory& pantry = m_Econ.Pantries.try_emplace(h->Id, Inventory(12, 0.0f)).first->second;
		const int room = std::max(0, Config.Economy.PantryPerMember * static_cast<int>(h->Members.size()) - FoodIn(pantry));
		const int share = property == "mixed" ? (count + 1) / 2 : count;
		return m.Body->Bag().Transfer(*m_Items, pantry, item, std::min(share, room));
	}

	int Tribe::DepositAt(TribeMember& m, const std::string& place)
	{
		Inventory& bag = m.Body->Bag();
		// T255: "" or "hauled" the fire; "@<building>" or "hauled@<building>" a store building of the main store; else a stockpile
		const size_t at = place.find('@');
		const std::string building = at == std::string::npos ? std::string() : place.substr(at + 1);
		const bool hauled = place.rfind("hauled", 0) == 0;
		Stockpile* pile = place.empty() || hauled || !building.empty() ? nullptr : FindStockpile(place);
		const StoreKind* kind = nullptr;
		if (pile)
			if (const auto k = Config.Econ.Logistics.Stores.find(pile->Kind); k != Config.Econ.Logistics.Stores.end()) kind = &k->second;
		Inventory& to = pile && kind ? pile->Store : m_Store;
		StoreLayout::Place shed;   // the store building it leaves things in (T255)
		const bool inShed = !building.empty() && ClanStores::Find(*this, building, shed);
		const bool sorted = !pile && !m.Player && ClanStores::Active(*this);   // the fire takes only its reserve and what no building has room for (a player leaves all)
		auto mainTakes = [&](const std::string& id, int count)
		{
			if (inShed)
			{
				const auto held = m_Stores.Ledger().find(building);
				shed.Held = held != m_Stores.Ledger().end() ? held->second : ClanStores::Items();
				return std::min(count, StoreLayout::Free(*this, shed, id));
			}
			if (!sorted) return count;
			if (const int reserve = m_Stores.ReserveShort(*this, id); reserve > 0) return std::min(count, reserve);
			return m_Stores.Destination(*this, id, Where(m)).empty() ? count : 0;
		};
		int moved = 0;
		const Job* held = m_Board.ClaimOf(m.Name);
		const std::string jobItem = held && held->Kind == "gather" ? held->Target : std::string();
		auto PileTakes = [&](const std::string& id)   // T248: a stockpile with room for it (the main store's surplus is kept there, not thrown away)
		{
			for (const Stockpile& p : m_Econ.Stockpiles)
				if (const auto k = Config.Econ.Logistics.Stores.find(p.Kind); k != Config.Econ.Logistics.Stores.end() && Takes(k->second, m_Items->Find(id)) && p.Store.Count(id) < k->second.Cap)
					if (Inventory probe = p.Store; probe.Add(*m_Items, id, 1) > 0) return true;
			return false;
		};
		for (const ItemSlot& s : std::vector<ItemSlot>(bag.Stacks()))
		{
			if (Kept(*m_Items, s.Id)) continue;
			int count = s.Count;
			if (pile && kind)
			{
				if (!Takes(*kind, m_Items->Find(s.Id))) continue;   // what this stockpile does not take stays in the bag (for the main store)
				count = std::min(count, std::max(0, kind->Cap - to.Count(s.Id)));
			}
			if (!pile && !hauled && IsFood(s.Id))   // N1b: the household's share goes home
				if (const int home = ToPantry(m, s.Id, count); home > 0) { moved += home; count -= home; m_Econ.FoodToday += home; m_FoodBy = m.Name; }
			if (!(pile && kind))
			{
				// the main store takes each thing up to its cap; what is past it is left (else 900 grain in a store of 100 filled every slot, and
				// the stone of the building sites and the wood of the fire found no room: the clans stopped building and having children)
				const int room = std::max(0, StoreCap(s.Id) - to.Count(s.Id));
				if (count > room)
				{
					const int over = count - room;
					if (!PileTakes(s.Id)) { bag.Remove(s.Id, over); Say(m, "leaves " + std::to_string(over) + " " + s.Id + ": the store has all it can keep of it"); }
					count = room;   // T248: what a stockpile (a storage) still takes stays in the bag and goes there on the next trip
				}
				count = mainTakes(s.Id, count);   // T255: what this building (or the fire) does not take stays in the bag for the next trip
			}
			const int n = count > 0 ? bag.Transfer(*m_Items, to, s.Id, count) : 0;
			if (n > 0 && inShed) m_Stores.Put(building, s.Id, n);
			moved += n;
			if (n > 0 && s.Id == jobItem) m_Board.Progress(m.Name, static_cast<float>(n), m_Time);   // delivered: the claim is alive
			if (n > 0 && !hauled && IsFood(s.Id)) { m_Econ.FoodToday += n; m_FoodBy = m.Name; }   // (hauled food was counted where it was first left; who feeds the others, H10)
		}
		return moved;
	}

	// ------------------------------------------------------------------ the clock: spoilage, the fields and the day's food
	void Tribe::StepEconomy(float dt)
	{
		if (!m_Items) return;
		m_Stores.Reconcile(*this);   // T255: the ledger of the store buildings follows the store (GB/Game/ClanStores.h)
		const EconomyRules& econ = Config.Econ;
		// the day's food (the clan's carrying capacity: how many it feeds)
		const double now = WorldSeconds();
		const double day = static_cast<double>(std::max(Config.Needs.DaySeconds, 1.0f));
		if (now < m_Econ.DayStart) m_Econ.DayStart = now;
		if (now - m_Econ.DayStart >= day)
		{
			m_Econ.FoodYesterday = m_Econ.FoodToday;
			m_Econ.FoodToday = 0;
			m_Econ.DayStart += day * std::floor((now - m_Econ.DayStart) / day);
		}
		m_Econ.SpoilTimer += dt;
		if (m_Econ.SpoilTimer < econ.Spoilage.Step) return;
		const float step = m_Econ.SpoilTimer;
		m_Econ.SpoilTimer = 0.0f;
		const Season season = CurrentSeason();
		const auto sf = econ.Spoilage.Seasons.find(SeasonClock::Name(season));
		const float cold = sf != econ.Spoilage.Seasons.end() ? sf->second : 1.0f;

		// E6: everything perishable ages where it is; what goes off is gone (or turns into what it spoils into)
		auto tell = [&](const std::vector<SpoilEvent>& events, const std::string& where)
		{
			for (const SpoilEvent& e : events)
			{
				m_Econ.Spoiled[e.Item] += e.Count;
				if (IsFood(e.Item)) m_Stores.NoteSpoiled(e.Count, m_Time, econ.Logistics.SpoilMemory);   // T256: the planner answers it
				TellClan("loses " + std::to_string(e.Count) + " " + e.Item + " in " + where + ": they went off" + (e.Into.empty() ? std::string() : " (now " + e.Into + ")"));
			}
		};
		tell(AgeInventory(m_Store, *m_Items, step, econ.Main().SpoilRate * m_Stores.Keep(*this) * cold), "the store");   // T255: as well as where its food lies keeps it
		for (Stockpile& p : m_Econ.Stockpiles)
		{
			const auto k = econ.Logistics.Stores.find(p.Kind);
			tell(AgeInventory(p.Store, *m_Items, step, (k != econ.Logistics.Stores.end() ? k->second.SpoilRate : 1.0f) * cold), p.Name);
		}
		for (TribeMember& m : m_Members)
			if (!m.Dead && m.Body) tell(AgeInventory(m.Body->Bag(), *m_Items, step, econ.Spoilage.Bag * cold), m.Name + "'s bag");
		for (auto& [id, pantry] : m_Econ.Pantries) tell(AgeInventory(pantry, *m_Items, step, econ.Main().SpoilRate * cold), id + "'s pantry");   // N1b: a house keeps as the store does

		// E7: the fields grow with the season and the rain of the day (T14: SeasonClock::WeatherAt, from the game's seed), and the frost ruins what winter finds in them
		const bool rain = SeasonClock::Rains(Config.Seasons.WeatherAt(now));
		for (Field& field : m_Econ.Fields)
		{
			const FieldEvents ev = field.Update(step, season, rain, econ.Farming.MoistureSeconds, econ.Crops);
			if (ev.Ripened) TellClan("sees " + std::to_string(ev.Ripened) + " plots of " + field.Name + " ripen");
			if (ev.Ruined)
			{
				m_Econ.Ruined += ev.Ruined;
				TellClan("loses " + std::to_string(ev.Ruined) + " plots of " + field.Name + " to the frost");
			}
		}
	}

	// ------------------------------------------------------------------ for people
	std::string Tribe::EconomyReport() const
	{
		std::string s;
		auto list = [](const std::map<std::string, int>& m)
		{
			std::string out;
			for (const auto& [k, n] : m) out += " " + k + " " + std::to_string(n);
			return out.empty() ? std::string(" none") : out;
		};
		s += "main store (cap " + std::to_string(StoreCap()) + " per item):";
		for (const ItemSlot& st : m_Store.Stacks())
		{
			s += " " + st.Id + " x" + std::to_string(st.Count);
			if (const ItemDef* d = m_Items ? m_Items->Find(st.Id) : nullptr; d && d->Perishable())
			{
				char b[48];
				std::snprintf(b, sizeof b, " (%.0f s left)", static_cast<double>(LifeLeft(*m_Items, st)));
				s += b;
			}
		}
		s += "\n";
		for (const Stockpile& p : m_Econ.Stockpiles)
		{
			char at[48];
			std::snprintf(at, sizeof at, " at %.0f, %.0f:", static_cast<double>(p.Pos.x), static_cast<double>(p.Pos.y));
			s += p.Name + " (" + p.Kind + ")" + at;
			for (const ItemSlot& st : p.Store.Stacks()) s += " " + st.Id + " x" + std::to_string(st.Count);
			if (p.Store.Stacks().empty()) s += " empty";
			s += "\n";
		}
		int alive = 0, carry = 0;
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) { ++alive; carry += CarryLimit(m); }
		char b[192];
		std::snprintf(b, sizeof b, "food brought in: %d today, %d yesterday, for %d members; they carry %d things per trip in all\n", m_Econ.FoodToday, m_Econ.FoodYesterday, alive, carry);
		s += b;
		s += "made:" + list(m_Econ.Made) + "; spoiled:" + list(m_Econ.Spoiled) + "; harvested:" + list(m_Econ.Harvested) + "; hauled " + std::to_string(m_Econ.Hauled)
			+ "; plots lost to the frost " + std::to_string(m_Econ.Ruined) + "\n";
		s += "plan:\n" + PlanReport(m_Plan);
		return s;
	}

	std::string Tribe::FieldReport() const
	{
		if (m_Econ.Fields.empty())
		{
			const FarmRules& f = Config.Econ.Farming;
			std::string s = "no field yet: the clan marks one out when a member knows " + (f.Tech.empty() ? std::string("how") : f.Tech) + " and the store has seeds of";
			if (Config.Econ.Crops.empty()) return s + " a crop (no crops in the loaded content)";
			for (const auto& [id, c] : Config.Econ.Crops) s += " " + c.Seed + " (" + std::to_string(Stored(c.Seed)) + ")";
			return s;
		}
		std::string s;
		for (const Field& f : m_Econ.Fields) s += f.Report() + "\n";
		s += "season: ";
		s += SeasonClock::Name(CurrentSeason());
		for (const auto& [id, c] : Config.Econ.Crops) s += "; " + id + (c.SowsIn(CurrentSeason()) ? " can be sown now" : " cannot be sown now") + " (seed " + c.Seed + ": " + std::to_string(Stored(c.Seed)) + ")";
		return s;
	}
}
