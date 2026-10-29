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
		if (station.empty() || std::find(camp.begin(), camp.end(), station) != camp.end()) { if (where) *where = Config.Camp; return true; }
		glm::vec2 at = Config.Camp;
		if (m_Stations && m_Stations(station, at)) { if (where) *where = at; return true; }   // the buildings (H8)
		return false;
	}

	bool Tribe::BuiltStationAt(const std::string& station, glm::vec2* where) const
	{
		glm::vec2 at = Config.Camp;
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

	int Tribe::StoreCap() const { return Config.Econ.Main().Cap; }

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
		return Config.DepositAt + static_cast<int>(std::floor(Config.Econ.Logistics.PerBagSlot * static_cast<float>(slots) + 1e-4f));
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
			for (int pass = 0; pass < 2 && !best; ++pass)
				for (const std::string& id : ids)
				{
					const Recipe* r = m_Recipes->Find(id);
					const ItemDef* d = r ? m_Items->Find(r->Output) : nullptr;
					if (!d || !Tagged(*d, "Item.Container") || !StationAt(r->Station) || (pass == 0 && !ClanKnows(*r))) continue;
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
			if (!farming && static_cast<int>(m_Econ.Fields.size()) < farm.Fields)
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
		// and its station allow (berries -> dried berries at the drying rack)
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
				for (const std::string& id : ids)
				{
					const Recipe* r = m_Recipes->Find(id);
					const ItemDef* out_ = r ? m_Items->Find(r->Output) : nullptr;
					const auto in = r ? r->Inputs.find(food) : std::map<std::string, int>::const_iterator();
					if (!out_ || in == r->Inputs.end() || (out_->Perishable() && out_->ShelfLife <= d->ShelfLife) || !ClanKnows(*r) || !StationAt(r->Station)) continue;
					const int batches = surplus / std::max(1, in->second);
					if (batches >= 1)
						out.push_back({ r->Output, m_Store.Count(r->Output) + batches * std::max(1, r->OutputCount), "keeps " + std::to_string(surplus) + " " + food + " from spoiling" });
					break;
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
		if (static_cast<int>(m_Econ.Fields.size()) < f.Fields && season != Season::Winter)
		{
			bool seeds = false, can = false;
			for (const auto& [id, crop] : econ.Crops) seeds = seeds || Stored(crop.Seed) > 0;
			for (const TribeMember& m : m_Members) can = can || (!m.Dead && m.Body && CanFarm(m));
			if (seeds && can)
				MarkField(Config.Camp + f.Offset + glm::vec2(0.0f, static_cast<float>(m_Econ.Fields.size()) * (static_cast<float>((f.Plots + f.Columns - 1) / f.Columns) + 1.0f) * f.Spacing));
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
			for (const ItemSlot& s : p.Store.Stacks()) units += s.Count;
			if (units >= econ.Logistics.HaulAt)
				m_Board.Post("haul", p.Name, "", static_cast<float>(units), econ.Logistics.Priority, 1, p.Name + " holds " + std::to_string(units) + " things for the store", m_Time, rules);
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
			if (!pile || Carried(*m_Items, bag) > 0) return 0.0f;   // it goes with an empty bag
			return job.Priority * liking("haul") * Falloff(glm::distance(pos, pile->Pos), 60.0f) * vigor;
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
		o.Where = Config.Camp;
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
			const glm::vec2 spot = at == Config.Camp ? CampSpot(m) : at + glm::vec2(1.2f, 0.0f);
			if (!Order(m, Go(spot, Seconds(econ.Production.Seconds)))) return false;
			m.Job = "make";
			m.Detail = r->Id;
			Say(m, "goes to make " + r->Output + (r->Station.empty() ? std::string() : " at the " + r->Station));
			return true;
		}
		if (o.Kind == "haul")
		{
			const Stockpile* pile = FindStockpile(o.Target);
			if (!pile) return false;
			if (!Order(m, Go(pile->Pos + glm::vec2(0.8f, 0.0f), "wait 1"))) return false;
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
			if (StationAt(r->Station) && m_Recipes->Craft(*m_Items, m_Store, r->Id, r->Station, gate))
			{
				Witnessed(m, r->Id, std::string());
				m_Econ.Made[r->Output] += r->OutputCount;
				++m.Made;
				Say(m, "makes " + std::to_string(r->OutputCount) + " " + r->Output + " for the store");
				Practise(m, "craft", "craft", 1.0f);
				if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "make") m_Board.Progress(m.Name, 1.0f, m_Time);
				if (glm::distance(Where(m), Config.Camp) < 6.0f) HandOut(m);
			}
			else Say(m, "cannot make " + r->Output + ": " + (StationAt(r->Station) ? m_Recipes->WhyNot(*m_Items, m_Store, r->Id, r->Station, gate) : "there is no " + r->Station));
			Share(m);
			return true;
		}
		if (job == "haul")
		{
			Stockpile* pile = FindStockpile(m.Detail);
			if (!pile) return true;
			int room = CarryLimit(m), moved = 0;
			for (const ItemSlot& s : std::vector<ItemSlot>(pile->Store.Stacks()))
			{
				if (room <= 0) break;
				const int n = pile->Store.Transfer(*m_Items, bag, s.Id, std::min(room, s.Count));
				room -= n;
				moved += n;
			}
			if (moved == 0) return true;
			m_Econ.Hauled += moved;
			if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "haul") m_Board.Progress(m.Name, static_cast<float>(moved), m_Time);
			Say(m, "picks up " + std::to_string(moved) + " things at " + pile->Name + " for the store");
			// and carries them straight to the main store (a deposit whose place is not a stockpile: the main store)
			char go[96];
			const glm::vec2 spot = CampSpot(m);
			std::snprintf(go, sizeof go, "goto %.2f %.2f | wait 1", static_cast<double>(spot.x), static_cast<double>(spot.y));
			if (Order(m, go)) { m.Job = "deposit"; m.Detail = "hauled"; }
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
					for (const auto& [item, n] : c->second.Yield)
					{
						const int put = bag.Add(*m_Items, item, n);
						if (put < n) m_Store.Add(*m_Items, item, n - put);   // (no room in the bag: it is as if it called someone over)
						got[item] += n;
						m_Econ.Harvested[item] += n;
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
		}
		return true;
	}

	// At the camp: a better tool of its trade than the one it carries, and a container if it has none, are taken from the store (the old tool goes back).
	void Tribe::HandOut(TribeMember& m)
	{
		if (!m.Body || !m_Items) return;
		Inventory& bag = m.Body->Bag();
		bool changed = false;
		if (const auto role = Config.Roles.find(m.Role); role != Config.Roles.end() && !role->second.ToolTag.empty())
		{
			const std::string& tag = role->second.ToolTag;
			const ItemDef* best = m_Store.BestTool(*m_Items, tag);
			const ItemDef* mine = bag.BestTool(*m_Items, tag);
			if (best && (!mine || best->Power > mine->Power || (best->Power == mine->Power && best->Speed > mine->Speed)) && m_Store.Transfer(*m_Items, bag, best->Id, 1) == 1)
			{
				const std::string old = mine ? mine->Id : std::string();
				if (!old.empty()) bag.Transfer(*m_Items, m_Store, old, 1);
				Say(m, "takes the " + best->Id + " from the store" + (old.empty() ? std::string() : " and leaves its " + old));
				changed = true;
			}
		}
		if (!bag.BestContainer(*m_Items))
			if (const ItemDef* c = m_Store.BestContainer(*m_Items); c && m_Store.Transfer(*m_Items, bag, c->Id, 1) == 1)
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
	std::string Tribe::DepositPlace(const TribeMember& m, glm::vec2& where) const
	{
		if (!m.Body || !m_Items) return {};
		const glm::vec2 pos = Where(m);
		float best = glm::distance(pos, Config.Camp) - 1.0f;   // a stockpile must be clearly nearer than the camp
		std::string place;
		for (const Stockpile& p : m_Econ.Stockpiles)
		{
			const auto kind = Config.Econ.Logistics.Stores.find(p.Kind);
			if (kind == Config.Econ.Logistics.Stores.end()) continue;
			bool takes = false;
			for (const ItemSlot& s : m.Body->Bag().Stacks())
				if (!Kept(*m_Items, s.Id) && Takes(kind->second, m_Items->Find(s.Id)) && p.Store.Count(s.Id) < kind->second.Cap) takes = true;
			const float d = glm::distance(pos, p.Pos);
			if (takes && d < best) { best = d; place = p.Name; where = p.Pos + glm::vec2(0.8f, 0.0f); }
		}
		return place;
	}

	int Tribe::DepositAt(TribeMember& m, const std::string& place)
	{
		Inventory& bag = m.Body->Bag();
		Stockpile* pile = place.empty() ? nullptr : FindStockpile(place);
		const StoreKind* kind = nullptr;
		if (pile)
			if (const auto k = Config.Econ.Logistics.Stores.find(pile->Kind); k != Config.Econ.Logistics.Stores.end()) kind = &k->second;
		Inventory& to = pile && kind ? pile->Store : m_Store;
		const bool hauled = place == "hauled";
		int moved = 0;
		const Job* held = m_Board.ClaimOf(m.Name);
		const std::string jobItem = held && held->Kind == "gather" ? held->Target : std::string();
		for (const ItemSlot& s : std::vector<ItemSlot>(bag.Stacks()))
		{
			if (Kept(*m_Items, s.Id)) continue;
			int count = s.Count;
			if (pile && kind)
			{
				if (!Takes(*kind, m_Items->Find(s.Id))) continue;   // what this stockpile does not take stays in the bag (for the main store)
				count = std::min(count, std::max(0, kind->Cap - to.Count(s.Id)));
			}
			const int n = bag.Transfer(*m_Items, to, s.Id, count);
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
				TellClan("loses " + std::to_string(e.Count) + " " + e.Item + " in " + where + ": they went off" + (e.Into.empty() ? std::string() : " (now " + e.Into + ")"));
			}
		};
		tell(AgeInventory(m_Store, *m_Items, step, econ.Main().SpoilRate * cold), "the store");
		for (Stockpile& p : m_Econ.Stockpiles)
		{
			const auto k = econ.Logistics.Stores.find(p.Kind);
			tell(AgeInventory(p.Store, *m_Items, step, (k != econ.Logistics.Stores.end() ? k->second.SpoilRate : 1.0f) * cold), p.Name);
		}
		for (TribeMember& m : m_Members)
			if (!m.Dead && m.Body) tell(AgeInventory(m.Body->Bag(), *m_Items, step, econ.Spoilage.Bag * cold), m.Name + "'s bag");

		// E7: the fields grow with the season and the rain of the season (SeasonClock::Weather), and the frost ruins what winter finds in them
		const std::string weather = SeasonClock::Weather(season, Config.Seasons.Progress(now));
		const bool rain = weather == "rain" || weather == "lightrain" || weather == "storm";
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
