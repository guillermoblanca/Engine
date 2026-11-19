#include "GB/Game/FarmZones.h"
#include "GB/Game/FieldPlacement.h"
#include "GB/Game/GroundFootprint.h"
#include "GB/Game/MealLog.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		bool IsPlan(const std::string& p) { return p == "sow" || p == "harvest" || p == "fallow"; }
		std::string Spaced(std::string s) { std::replace(s.begin(), s.end(), '_', ' '); return s; }
		// A point inside a polygon, or on its edge (within 1 cm): the even-odd rule
		bool Inside(const std::vector<glm::vec2>& poly, const glm::vec2& p)
		{
			bool in = false;
			for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
			{
				const glm::vec2 a = poly[j], b = poly[i];
				const glm::vec2 ab = b - a;
				const float len2 = glm::dot(ab, ab);
				const float t = len2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
				if (glm::distance(p, a + t * ab) < 0.01f) return true;
				if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y)) in = !in;
			}
			return in;
		}
	}

	void FarmZones::Logged(Tribe& tribe, const std::string& text)
	{
		char t[48];
		std::snprintf(t, sizeof t, "%6.1f ", tribe.m_Time);
		tribe.m_Log.push_back(std::string(t) + text);
	}

	const FarmZones::Zone* FarmZones::Of(const std::string& field) const
	{
		const auto it = m_Zones.find(field);
		return it != m_Zones.end() ? &it->second : nullptr;
	}

	const Field* FarmZones::Find(const Tribe& tribe, const std::string& zone, size_t* index)
	{
		if (zone.empty()) return nullptr;
		const bool number = zone.find_first_not_of("0123456789") == std::string::npos;
		const std::string name = number ? kPrefix + zone : Spaced(zone);
		const std::vector<Field>& fields = tribe.m_Econ.Fields;
		for (size_t i = 0; i < fields.size(); ++i)
			if (fields[i].Name == name)
			{
				if (index) *index = i;
				return &fields[i];
			}
		return nullptr;
	}

	FarmZones::Zone* FarmZones::Edit(const Tribe& tribe, const std::string& zone, std::string& name)
	{
		const Field* f = Find(tribe, zone);
		if (!f) return nullptr;
		name = f->Name;
		return &m_Zones[name];   // a field of the clan's own becomes the player's when it is given a crop or a plan
	}

	// ------------------------------------------------------------------ the player's zones
	FarmZones::Reply FarmZones::Paint(Tribe& tribe, const glm::vec2& a, const glm::vec2& b, const std::string& crop)
	{
		const glm::vec2 lo = glm::min(a, b), hi = glm::max(a, b);
		return PaintOutline(tribe, { lo, { hi.x, lo.y }, hi, { lo.x, hi.y } }, crop);
	}

	namespace
	{
		// the grid of a perimeter: its cells (2 m, from the lowest x and z of the points) whose middle falls inside it
		struct OutlineGrid
		{
			glm::vec2 Lo{ 0.0f };
			int Columns = 0, Rows = 0;
			float Spacing = 2.0f;
			std::vector<int> Cells;
		};

		FarmZones::Reply CheckOutline(const Tribe& tribe, const std::vector<glm::vec2>& outline, const std::string& crop, OutlineGrid& grid)
		{
			const FarmRules& f = tribe.Config.Econ.Farming;
			if (outline.size() < 3) return { false, "a zone needs 3 points or more" };
			if (const std::string why = TechVerbs::WhyNot(tribe, "farm", "till"); !why.empty()) return { false, "cannot paint a farm zone: the clan " + why };   // T322
			if (!crop.empty() && !tribe.Config.Econ.Crops.count(crop)) return { false, "no crop called " + crop + " in the loaded content" };
			if (!crop.empty() && !FarmZones::Knows(tribe, crop)) return { false, "the clan does not know how to grow " + crop };   // T344
			const float sp = std::max(0.5f, f.Spacing);
			glm::vec2 lo = outline[0], hi = outline[0];
			for (const glm::vec2& p : outline) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
			const int columns = static_cast<int>(std::floor((hi.x - lo.x) / sp + 0.5f)) + 1;
			const int rows = static_cast<int>(std::floor((hi.y - lo.y) / sp + 0.5f)) + 1;
			if (static_cast<long long>(columns) * rows > 4LL * FarmZones::kMostPlots) return { false, "the zone is too big: at most " + std::to_string(FarmZones::kMostPlots) + " plots" };
			std::vector<int> cells;
			for (int c = 0; c < columns * rows; ++c)
				if (Inside(outline, lo + glm::vec2(static_cast<float>(c % columns), static_cast<float>(c / columns)) * sp)) cells.push_back(c);
			const int plots = static_cast<int>(cells.size());
			if (plots == 0) return { false, "no plot falls inside the zone" };
			if (plots > FarmZones::kMostPlots) return { false, "the zone would have " + std::to_string(plots) + " plots: at most " + std::to_string(FarmZones::kMostPlots) };
			grid.Lo = lo;
			grid.Columns = columns;
			grid.Rows = rows;
			grid.Spacing = sp;
			grid.Cells = cells;
			// the ground: nothing standing there (any clan's site, building or field: T337 plot by plot, so a zone in the notch of an L is not on
			// it), and every plot dry ground (what stands in a plot, a tree or a rock, is the zone's first work: FarmZones::Blockers)
			std::vector<GroundRect> want;
			FieldGrounds(lo, columns, sp, cells, want);
			const std::vector<GroundRect> taken = TakenGround(tribe);
			for (const GroundRect& r : want)
				if (const std::string why = GroundFree(r, taken, 0.0f); !why.empty()) return { false, "cannot paint there: " + why };
			const ITribeBody* body = nullptr;
			for (const TribeMember& m : tribe.Members()) if (!m.Dead && m.Body) { body = m.Body; break; }
			if (body)
				for (const int cell : cells)
				{
					const glm::vec2 p = lo + glm::vec2(static_cast<float>(cell % columns), static_cast<float>(cell / columns)) * sp;
					char at[48];
					std::snprintf(at, sizeof at, "%.0f, %.0f", static_cast<double>(p.x), static_cast<double>(p.y));
					if (body->WaterDepthAt(p) > kWetDepth) return { false, std::string("cannot paint there: the plot at ") + at + " is in the water" };
				}
			return { true, std::string() };
		}
	}

	// ------------------------------------------------------------------ T337: what stands in a zone
	FarmZones::Reply FarmZones::Blockers(const Tribe& tribe, const glm::vec2& lo, int columns, float sp, const std::vector<int>& cells, std::vector<std::vector<std::string>>& perCell,
		std::vector<size_t>* stuck)
	{
		perCell.assign(cells.size(), {});
		if (cells.empty()) return { true, std::string() };
		const int cols = std::max(1, columns);
		const float reach = std::max(0.0f, tribe.Config.Econ.Farming.ClearReach);
		auto at = [&](int cell) { return lo + glm::vec2(static_cast<float>(cell % cols), static_cast<float>(cell / cols)) * sp; };
		// the nodes round the zone's grid (its plots and a reach more), in the scene's order of names
		glm::vec2 from = at(cells.front()), to = from;
		for (const int c : cells) { from = glm::min(from, at(c)); to = glm::max(to, at(c)); }
		from -= glm::vec2(std::max(sp, reach));
		to += glm::vec2(std::max(sp, reach));
		std::vector<std::pair<std::string, glm::vec2>> nodes;
		if (tribe.m_Gameplay && tribe.m_Entities)
			for (const GameplayWorld::Tagged& t : tribe.m_Gameplay->WithTag("Resource.Node"))
			{
				const EntityId id = tribe.m_Entities->FindByName(*t.Name);
				if (id == NullEntity) continue;
				const glm::vec4 p = tribe.m_Entities->WorldMatrix(id)[3];
				if (p.x >= from.x && p.x <= to.x && p.z >= from.y && p.z <= to.y) nodes.push_back({ *t.Name, { p.x, p.z } });
			}
		const ITribeBody* body = nullptr;
		for (const TribeMember& m : tribe.Members()) if (!m.Dead && m.Body) { body = m.Body; break; }
		const float half = 0.5f * sp;
		for (size_t i = 0; i < cells.size(); ++i)
		{
			const glm::vec2 p = at(cells[i]);
			// a node whose foot is in the plot's square
			for (const auto& [name, q] : nodes) if (std::abs(q.x - p.x) <= half && std::abs(q.y - p.y) <= half) perCell[i].push_back(name);
			if (!body) continue;
			bool stand = body->CanStandAt(p);   // the plot's middle and its four quarters (a trunk between two middles)
			for (const glm::vec2 d : { glm::vec2(0.5f, 0.5f), glm::vec2(-0.5f, 0.5f), glm::vec2(0.5f, -0.5f), glm::vec2(-0.5f, -0.5f) }) stand = stand && body->CanStandAt(p + d * half);
			if (stand) continue;
			if (!perCell[i].empty()) continue;
			// nobody can stand on it and no node stands in it: the nearest node within the reach is in the way (a trunk, a big rock whose wall
			// reaches over; once it is gone the next one is, if the plot is still walled); with none, a tree or a rock of the scenery or a cliff,
			// which nobody can clear
			const std::string* nearest = nullptr;
			float best = reach;
			for (const auto& [name, q] : nodes) if (const float d = glm::distance(q, p); d <= best) { best = d; nearest = &name; }
			if (nearest) { perCell[i].push_back(*nearest); continue; }
			if (stuck) { stuck->push_back(i); continue; }
			char where[48];
			std::snprintf(where, sizeof where, "%.0f, %.0f", static_cast<double>(p.x), static_cast<double>(p.y));
			return { false, std::string("cannot paint there: the plot at ") + where + " is on a rock or a trunk" };
		}
		return { true, std::string() };
	}

	// T337: the zone's plots without the walled ones (a tree or a rock of the scenery, a cliff: nobody can clear them, so the zone leaves them
	// out, a hole in it) and what stands in each of the others. Refused when nothing is left.
	FarmZones::Reply FarmZones::Plots(const Tribe& tribe, std::vector<int>& cells, const glm::vec2& lo, int columns, float sp, std::vector<std::vector<std::string>>& perCell,
		std::vector<glm::vec2>* walled)
	{
		std::vector<size_t> stuck;
		if (const Reply r = Blockers(tribe, lo, columns, sp, cells, perCell, &stuck); !r.Ok) return r;
		if (walled) walled->clear();
		if (stuck.empty()) return { true, std::string() };
		std::vector<int> kept;
		std::vector<std::vector<std::string>> keptNodes;
		size_t s = 0;
		for (size_t i = 0; i < cells.size(); ++i)
		{
			if (s < stuck.size() && stuck[s] == i)
			{
				++s;
				if (walled) walled->push_back(lo + glm::vec2(static_cast<float>(cells[i] % columns), static_cast<float>(cells[i] / columns)) * sp);
				continue;
			}
			kept.push_back(cells[i]);
			keptNodes.push_back(std::move(perCell[i]));
		}
		if (kept.empty()) return { false, "cannot paint there: every plot is on a rock or a trunk that cannot be cleared" };
		cells = std::move(kept);
		perCell = std::move(keptNodes);
		// with holes its store may stand elsewhere (beside its first plot left): the ground again, as it will be
		std::vector<GroundRect> want;
		FieldGrounds(lo, columns, sp, cells, want);
		const std::vector<GroundRect> taken = TakenGround(tribe);
		for (const GroundRect& r : want)
			if (const std::string why = GroundFree(r, taken, 0.0f); !why.empty()) return { false, "cannot paint there: " + why };
		return { true, std::string() };
	}

	FarmZones::Reply FarmZones::Check(const Tribe& tribe, const std::vector<glm::vec2>& outline, const std::string& crop, int* plots, std::vector<glm::vec2>* clear,
		std::vector<glm::vec2>* walled)
	{
		OutlineGrid grid;
		Reply r = CheckOutline(tribe, outline, crop, grid);
		std::vector<std::vector<std::string>> perCell;
		if (r.Ok) r = Plots(tribe, grid.Cells, grid.Lo, grid.Columns, grid.Spacing, perCell, walled);
		if (plots) *plots = r.Ok ? static_cast<int>(grid.Cells.size()) : 0;
		if (clear)
		{
			clear->clear();
			if (r.Ok)
				for (size_t i = 0; i < grid.Cells.size(); ++i)
					if (!perCell[i].empty()) clear->push_back(grid.Lo + glm::vec2(static_cast<float>(grid.Cells[i] % grid.Columns), static_cast<float>(grid.Cells[i] / grid.Columns)) * grid.Spacing);
		}
		return r;
	}

	FarmZones::Reply FarmZones::PaintOutline(Tribe& tribe, const std::vector<glm::vec2>& outline, const std::string& crop)
	{
		OutlineGrid grid;
		if (const Reply r = CheckOutline(tribe, outline, crop, grid); !r.Ok) return r;
		std::vector<std::vector<std::string>> perCell;
		std::vector<glm::vec2> walled;
		if (const Reply r = Plots(tribe, grid.Cells, grid.Lo, grid.Columns, grid.Spacing, perCell, &walled); !r.Ok) return r;
		const glm::vec2 lo = grid.Lo;
		const int columns = grid.Columns, rows = grid.Rows;
		const float sp = grid.Spacing;
		const std::vector<int>& cells = grid.Cells;
		const int plots = static_cast<int>(cells.size());
		// a name of its own: "zone N", the first N no field of the clan has
		std::set<std::string> taken;
		for (const Field& x : tribe.m_Econ.Fields) taken.insert(x.Name);
		int n = 1;
		while (taken.count(kPrefix + std::to_string(n))) ++n;
		const std::string name = kPrefix + std::to_string(n);
		Field field(name, lo, plots, columns, sp);
		if (plots != columns * rows) field.Cells = cells;   // (a rectangle keeps the plain grid)
		tribe.m_Econ.Fields.push_back(std::move(field));
		if (!tribe.Config.Econ.Logistics.FieldStore.empty())   // the harvest is left beside the zone and hauled to the camp (as Tribe::MarkField)
			tribe.AddStockpile(name + " store", FieldStoreSpot(lo, columns, sp, plots != columns * rows ? cells : std::vector<int>()), tribe.Config.Econ.Logistics.FieldStore);   // (T337: beside its first plot)
		Zone zone{ crop, "sow", crop.empty() };   // with no crop the clan chooses it
		zone.Outline = outline;
		m_Zones[name] = zone;
		char text[200];
		if (plots == columns * rows)
			std::snprintf(text, sizeof text, "%s painted at %.0f, %.0f: %d x %d = %d plots, crop %s", name.c_str(), static_cast<double>(lo.x), static_cast<double>(lo.y), columns, rows, plots,
				crop.empty() ? "any" : crop.c_str());
		else
			std::snprintf(text, sizeof text, "%s painted at %.0f, %.0f: %d points, %d plots, crop %s", name.c_str(), static_cast<double>(lo.x), static_cast<double>(lo.y), static_cast<int>(outline.size()), plots,
				crop.empty() ? "any" : crop.c_str());
		Logged(tribe, std::string("the player paints ") + text);
		// T337: what stands in it is its first work (its nodes are marked to clear now, and its plots wait for them)
		int waiting = 0;
		for (const auto& c : perCell) waiting += c.empty() ? 0 : 1;
		std::string reply = text;
		if (waiting > 0)
		{
			RefreshClearing(tribe);
			reply += "; " + std::to_string(waiting) + (waiting == 1 ? " plot" : " plots") + " to clear first";
			if (const std::string c = Clearing(name); !c.empty()) Logged(tribe, name + ": " + c);
		}
		if (!walled.empty()) reply += "; " + std::to_string(walled.size()) + (walled.size() == 1 ? " plot" : " plots") + " left out (a rock or a tree that cannot be cleared)";
		return { true, reply };
	}

	FarmZones::Reply FarmZones::SetCrop(Tribe& tribe, const std::string& zone, const std::string& crop)
	{
		const std::string c = crop == "any" || crop == "-" ? std::string() : crop;
		if (!c.empty() && !tribe.Config.Econ.Crops.count(c)) return { false, "no crop called " + c + " in the loaded content" };
		if (!c.empty() && !Knows(tribe, c)) return { false, "the clan does not know how to grow " + c };   // T344
		std::string name;
		Zone* z = Edit(tribe, zone, name);
		if (!z) return { false, "the clan has no zone " + zone };
		z->Crop = c;
		z->Auto = c.empty();
		Logged(tribe, "the player sows " + name + " with " + (c.empty() ? std::string("any crop") : c));
		return { true, name + " crop " + (c.empty() ? std::string("any") : c) };
	}

	FarmZones::Reply FarmZones::SetPlan(Tribe& tribe, const std::string& zone, const std::string& plan)
	{
		if (!IsPlan(plan)) return { false, "a plan is sow, harvest or fallow" };
		std::string name;
		Zone* z = Edit(tribe, zone, name);
		if (!z) return { false, "the clan has no zone " + zone };
		z->Plan = plan;
		Logged(tribe, "the player sets the plan of " + name + " to " + plan);
		return { true, name + " plan " + plan };
	}

	FarmZones::Reply FarmZones::SetAuto(Tribe& tribe, const std::string& zone, bool on)
	{
		std::string name;
		Zone* z = Edit(tribe, zone, name);
		if (!z) return { false, "the clan has no zone " + zone };
		z->Auto = on;
		Logged(tribe, "the player sets " + name + " to " + (on ? "auto" : "its own crop"));
		return { true, name + " auto " + (on ? "on" : "off") };
	}

	FarmZones::Reply FarmZones::Remove(Tribe& tribe, const std::string& zone)
	{
		size_t index = 0;
		const Field* f = Find(tribe, zone, &index);
		if (!f) return { false, "the clan has no zone " + zone };
		const std::string name = f->Name;
		std::vector<Field>& fields = tribe.m_Econ.Fields;
		fields.erase(fields.begin() + static_cast<std::ptrdiff_t>(index));
		m_Zones.erase(name);
		// its store goes with it when it is empty; one that holds something stays (and is hauled) under another name, so that a zone painted
		// again with this name gets a store of its own beside it
		std::vector<Stockpile>& piles = tribe.m_Econ.Stockpiles;
		for (auto p = piles.begin(); p != piles.end(); ++p)
			if (p->Name == name + " store")
			{
				if (p->Store.Stacks().empty()) piles.erase(p);
				else
				{
					int k = 1;
					while (tribe.FindStockpile(name + " store (left " + std::to_string(k) + ")")) ++k;
					p->Name = name + " store (left " + std::to_string(k) + ")";
				}
				break;
			}
		// a farmer on its way works the field by its index ("<action> <field> <crop> <plot>...": Tribe::FinishEconomy): the ones after it move down
		// one, and a trip to the zone that went comes to nothing
		for (TribeMember& m : tribe.m_Members)
		{
			if (m.Job != "farm" || m.Detail.empty()) continue;
			std::istringstream in(m.Detail);
			std::string action, crop, rest;
			size_t at = 0;
			if (!(in >> action >> at >> crop)) continue;
			std::getline(in, rest);
			if (at == index) { m.Detail = action + " " + std::to_string(fields.size() + 1000) + " " + crop; continue; }
			if (at > index) m.Detail = action + " " + std::to_string(at - 1) + " " + crop + rest;
		}
		// T337: the nodes it asked to clear are no more its work (what was cleared stays cleared)
		std::vector<std::string> marked;
		for (const auto& [node, mark] : tribe.m_Designations.Marks()) if (mark.Field == name) marked.push_back(node);
		for (const std::string& node : marked) tribe.m_Designations.Undesignate(tribe, node);
		m_Blocked.erase(name);
		m_ClearText.erase(name);
		m_Clean.erase(name);
		Logged(tribe, "the player removes " + name);
		return { true, name + " removed" };
	}

	std::string FarmZones::List(const Tribe& tribe) const
	{
		std::string s;
		for (const Field& f : tribe.m_Econ.Fields)
		{
			const Zone* z = Of(f.Name);
			std::string head = z ? " [crop " + (z->Auto ? std::string("auto") : z->Crop.empty() ? std::string("any") : z->Crop) + ", plan " + z->Plan + "]" : std::string(" [the clan's, auto]");
			if (const std::string c = Clearing(f.Name); !c.empty()) head += "; " + c;   // T337 (only while something stands in it)
			s += (s.empty() ? "" : "\n") + f.Report() + head;
		}
		return s.empty() ? std::string("no zones") : s;
	}

	// ------------------------------------------------------------------ the rule of the AI
	int FarmZones::FieldsWanted(const Tribe& tribe) const
	{
		const FarmRules& f = tribe.Config.Econ.Farming;
		int alive = 0;
		for (const TribeMember& m : tribe.m_Members) if (!m.Dead) ++alive;
		if (f.PlotsPerMember <= 0) return f.FieldsFor(alive);   // (the old rule)
		int have = 0;
		for (const Field& x : tribe.m_Econ.Fields) have += static_cast<int>(x.Plots.size());
		const int shortBy = alive * f.PlotsPerMember - have;
		return static_cast<int>(tribe.m_Econ.Fields.size()) + (shortBy >= std::max(1, f.ZoneMin() / 2) ? 1 : 0);
	}

	int FarmZones::NextPlots(const Tribe& tribe)
	{
		const FarmRules& f = tribe.Config.Econ.Farming;
		if (f.PlotsPerMember <= 0) return f.Plots;
		int alive = 0, have = 0;
		for (const TribeMember& m : tribe.m_Members) if (!m.Dead) ++alive;
		for (const Field& x : tribe.m_Econ.Fields) have += static_cast<int>(x.Plots.size());
		return std::clamp(alive * f.PlotsPerMember - have, f.ZoneMin(), f.ZoneMax());
	}

	bool FarmZones::Knows(const Tribe& tribe, const std::string& crop)
	{
		const auto c = tribe.Config.Econ.Crops.find(crop);
		return c != tribe.Config.Econ.Crops.end() && (c->second.Tech.empty() || tribe.ClanKnowsTech(c->second.Tech));
	}

	std::string FarmZones::AutoCrop(const Tribe& tribe)
	{
		const EconomyRules& econ = tribe.Config.Econ;
		const Season season = tribe.CurrentSeason();
		// the item a crop yields most of (not its seed), and how many of it a plot gives
		auto produceOf = [](const CropDef& crop, int& total, int& most)
		{
			std::string produce;
			for (const auto& [item, n] : crop.Yield)
			{
				total += n;
				if (item != crop.Seed && n > most) { most = n; produce = item; }
			}
			return produce;
		};
		const float foodDays = econ.Farming.FoodDays;
		if (foodDays <= 0.0f)   // the old rule: the produce the store has least units of
		{
			std::string best;
			int bestStock = 0, bestYield = 0;
			for (const auto& [id, crop] : econ.Crops)
			{
				if (!crop.SowsIn(season) || tribe.m_Store.Count(crop.Seed) <= 0 || !Knows(tribe, id)) continue;
				int yield = 0, most = 0;
				const std::string produce = produceOf(crop, yield, most);
				const int stock = produce.empty() ? 0 : tribe.Stored(produce);
				if (best.empty() || stock < bestStock || (stock == bestStock && yield > bestYield)) { best = id; bestStock = stock; bestYield = yield; }
			}
			return best;
		}
		// T344 (P-F15): food first. What the clan has of an item: the store, the bags of the living, and what its sown plots of a crop will give.
		std::map<std::string, int> growing;   // crop -> plots sown and growing
		for (const Field& f : tribe.m_Econ.Fields)
			for (const FieldPlot& p : f.Plots)
				if (!p.Crop.empty() && p.State != "grass" && p.State != "dug") ++growing[p.Crop];
		auto have = [&](const std::string& item)
		{
			int n = tribe.Stored(item);
			for (const TribeMember& m : tribe.m_Members) if (!m.Dead && m.Body) n += m.Body->Bag().Count(item);
			for (const auto& [id, plots] : growing)
				if (const auto c = econ.Crops.find(id); c != econ.Crops.end() && item != c->second.Seed)
					if (const auto y = c->second.Yield.find(item); y != c->second.Yield.end()) n += plots * y->second;
			return n;
		};
		const bool fed = tribe.FoodDays() >= foodDays;
		std::string best;
		bool bestFood = true;
		float bestHave = 0.0f, bestFeeds = 0.0f;
		for (const auto& [id, crop] : econ.Crops)
		{
			if (!crop.SowsIn(season) || tribe.m_Store.Count(crop.Seed) <= 0 || !Knows(tribe, id)) continue;
			int yield = 0, most = 0;
			const std::string produce = produceOf(crop, yield, most);
			if (produce.empty()) continue;
			const int units = have(produce);
			if (tribe.IsFood(produce))
			{
				const float nutrition = MealLog::Nutrition(tribe.Config.Social.Diet, tribe.Config.Social.On, tribe.Config.Needs.FoodHunger, produce);
				const float had = static_cast<float>(units) * nutrition, feeds = static_cast<float>(most) * nutrition;
				// among the food crops: the least nutrition had, then the most a plot feeds, then by id (a crop that is no food and lacks goes first)
				if (best.empty() || (bestFood && (had < bestHave || (had == bestHave && feeds > bestFeeds)))) { best = id; bestFood = true; bestHave = had; bestFeeds = feeds; }
				continue;
			}
			// a crop that is no food: only with the food covered and its produce lacking (a stock goal, the production stock, the plan's shortage)
			if (!fed) continue;
			int wanted = 0;
			if (const auto g = tribe.Goals().All().find(produce); g != tribe.Goals().All().end()) wanted = std::max(wanted, g->second);
			if (const auto s = econ.Production.Stock.find(produce); s != econ.Production.Stock.end()) wanted = std::max(wanted, s->second);
			if (const auto r = tribe.Plan().Raw.find(produce); r != tribe.Plan().Raw.end()) wanted = std::max(wanted, tribe.Stored(produce) + r->second);
			if (units >= wanted) continue;
			const float lack = static_cast<float>(units - wanted);   // (negative: the one lacking most first)
			if (best.empty() || bestFood || lack < bestHave) { best = id; bestFood = false; bestHave = lack; bestFeeds = 0.0f; }
		}
		return best;
	}

	// ------------------------------------------------------------------ the tribe's hooks
	std::string FarmZones::CropFor(const Tribe& tribe, const Field& field) const
	{
		const EconomyRules& econ = tribe.Config.Econ;
		const Season season = tribe.CurrentSeason();
		const Zone* z = Of(field.Name);
		if (z && z->Plan != "sow") return {};
		if (!z || z->Auto) return AutoCrop(tribe);   // the clan's own fields and the zones in auto
		if (!z->Crop.empty())
		{
			const auto c = econ.Crops.find(z->Crop);
			return c != econ.Crops.end() && c->second.SowsIn(season) && tribe.m_Store.Count(c->second.Seed) > 0 && Knows(tribe, z->Crop) ? z->Crop : std::string();
		}
		for (const auto& [id, crop] : econ.Crops) if (crop.SowsIn(season) && tribe.m_Store.Count(crop.Seed) > 0 && Knows(tribe, id)) return id;   // the first by id (as before)
		return {};
	}

	bool FarmZones::Works(const Field& field, const std::string& action) const
	{
		const Zone* z = Of(field.Name);
		if (!z || z->Plan == "sow") return true;
		if (z->Plan == "harvest") return action == "tend" || action == "harvest";
		return action == "harvest";   // fallow
	}

	bool FarmZones::Pick(const Tribe& tribe, const std::string& action, size_t most, size_t& fieldIndex, std::vector<size_t>& plots, std::string& crop) const
	{
		const std::vector<Field>& fields = tribe.m_Econ.Fields;
		for (size_t i = 0; i < fields.size(); ++i)
		{
			const Field& field = fields[i];
			if (!Works(field, action)) continue;
			std::string c;
			if (action == "sow" && (c = CropFor(tribe, field)).empty()) continue;
			if (action == "till" && Blocked(field.Name))   // T337: a plot with something standing in it waits to be cleared
			{
				const std::set<size_t>& wait = *Blocked(field.Name);
				plots.clear();
				for (const size_t p : field.Find("grass", field.Plots.size())) if (!wait.count(p) && plots.size() < most) plots.push_back(p);
			}
			else plots = action == "till" ? field.Find("grass", most) : action == "sow" ? field.Find("dug", most) : action == "tend" ? field.Dry(most) : field.Find("grown", most);
			if (plots.empty()) continue;
			fieldIndex = i;
			crop = action == "sow" ? c : std::string("-");
			return true;
		}
		plots.clear();
		return false;
	}

	glm::vec2 FarmZones::WorkPlace(const Tribe& tribe, const std::string& action) const
	{
		const std::vector<Field>& fields = tribe.m_Econ.Fields;
		if (fields.empty()) return tribe.Config.Economy.Camp;
		if (m_Zones.empty()) return fields.front().Centre();   // (as before the zones: the first field)
		size_t index = 0;
		std::vector<size_t> plots;
		std::string crop;
		return Pick(tribe, action, 1, index, plots, crop) ? fields[index].Centre() : fields.front().Centre();
	}

	void FarmZones::Post(Tribe& tribe) const
	{
		const JobRules& rules = tribe.Config.Work.Jobs;
		const EconomyRules& econ = tribe.Config.Econ;
		EconomyState& state = tribe.m_Econ;
		// farm: mark out a field once the clan knows how and has seeds (not the player's clan in the colony: the player paints its zones); then
		// till, sow (in its season), water and harvest
		const FarmRules& f = econ.Farming;
		const Season season = tribe.CurrentSeason();
		if (!m_Player && static_cast<int>(state.Fields.size()) < tribe.FieldsWanted() && season != Season::Winter && TechVerbs::Allows(tribe, "farm", "till"))   // (T322)
		{
			bool seeds = false, can = false;
			for (const auto& [id, crop] : econ.Crops) seeds = seeds || (tribe.Stored(crop.Seed) > 0 && Knows(tribe, id));   // (T344: of a crop it may sow)
			for (const TribeMember& m : tribe.m_Members) can = can || (!m.Dead && m.Body && tribe.CanFarm(m));
			if (seeds && can)
			{
				// each new field one row of plots (and a path) further from the camp than the ones before
				float along = static_cast<float>(state.Fields.size()) * (static_cast<float>((f.Plots + f.Columns - 1) / f.Columns) + 1.0f) * f.Spacing;   // (the old rule: all alike)
				if (f.PlotsPerMember > 0)
				{
					along = 0.0f;
					for (const Field& x : state.Fields) along += (static_cast<float>((static_cast<int>(x.Plots.size()) + x.Columns - 1) / std::max(1, x.Columns)) + 1.0f) * x.Spacing;
				}
				const int plots = NextPlots(tribe);
				if (const auto at = FieldOriginFor(tribe, tribe.Config.Economy.Camp + f.Offset + glm::vec2(0.0f, along), plots)) tribe.MarkField(*at, plots);   // T215/T260: on free ground (FieldPlacement.h)
			}
		}
		if (!m_Zones.empty()) RefreshClearing(tribe);   // T337: clear what stands in the player's zones first (nothing for a clan of the AI)
		if (state.Fields.empty()) return;
		int grass = 0, dug = 0, dry = 0, grown = 0;
		std::string sowable;
		for (const Field& field : state.Fields)
		{
			if (Works(field, "till"))
			{
				grass += field.Count("grass");
				if (const std::set<size_t>* wait = Blocked(field.Name))
					for (const size_t p : *wait) grass -= p < field.Plots.size() && field.Plots[p].State == "grass" ? 1 : 0;
			}
			if (Works(field, "sow"))
				if (const std::string c = CropFor(tribe, field); !c.empty())
				{
					const int n = field.Count("dug");
					dug += n;
					if (sowable.empty() || (n > 0 && dug == n)) sowable = c;   // the crop named: the first field's with tilled plots
				}
			if (Works(field, "tend")) dry += static_cast<int>(field.Dry(field.Plots.size()).size());
			if (Works(field, "harvest")) grown += field.Count("grown");
		}
		int hoes = f.ToolTag.empty() ? 1 : tribe.m_Store.CountTagged(*tribe.m_Items, f.ToolTag);
		for (const TribeMember& m : tribe.m_Members) if (!m.Dead && m.Body && !f.ToolTag.empty()) hoes += m.Body->Bag().CountTagged(*tribe.m_Items, f.ToolTag);
		const int trip = std::max(1, f.PlotsPerTrip);
		auto slots = [&](int plots) { return std::clamp((plots + trip - 1) / trip, 1, 2); };
		JobBoard& board = tribe.m_Board;
		const double now = tribe.m_Time;
		if (grass > 0 && hoes > 0 && season != Season::Winter)
			board.Post("farm", "till", "", static_cast<float>(grass), f.Priority, slots(grass), std::to_string(grass) + " plots of the field are still grass", now, rules);
		if (dug > 0 && !sowable.empty())
			board.Post("farm", "sow", "", static_cast<float>(dug), f.Priority * 1.1f, slots(dug), std::to_string(dug) + " plots are tilled and it is the season to sow " + sowable, now, rules);
		if (dry > 0)
			board.Post("farm", "tend", "", static_cast<float>(dry), f.Priority, slots(dry), std::to_string(dry) + " plots of growing crops are dry", now, rules);
		if (grown > 0)
			board.Post("farm", "harvest", "", static_cast<float>(grown), f.Priority * 1.3f, slots(grown), std::to_string(grown) + " plots are ripe", now, rules);
	}

	// ------------------------------------------------------------------ T337: clearing a zone
	const std::set<size_t>* FarmZones::Blocked(const std::string& field) const
	{
		const auto it = m_Blocked.find(field);
		return it != m_Blocked.end() && !it->second.empty() ? &it->second : nullptr;
	}

	std::string FarmZones::Clearing(const std::string& field) const
	{
		const auto it = m_ClearText.find(field);
		return it != m_ClearText.end() ? it->second : std::string();
	}

	void FarmZones::ClearAway(Tribe& tribe, const std::string& node, const std::string& zone)
	{
		if (tribe.m_Entities)
			if (const EntityId id = tribe.m_Entities->FindByName(node); id != NullEntity) tribe.m_Entities->Destroy(id);   // (its parts with it)
		if (tribe.m_Gameplay) tribe.m_Gameplay->Remove(node);
		tribe.m_Claims.Release(node);
		tribe.m_PosCache.erase(node);
		tribe.m_Clan.Forget(node);
		for (TribeMember& m : tribe.m_Members) { m.Mind.Memory.Forget(node); m.Unreachable.erase(node); }
		Logged(tribe, zone + ": " + node + " is cleared away");
	}

	void FarmZones::RefreshClearing(Tribe& tribe) const
	{
		const int priority = std::clamp(tribe.Config.Econ.Farming.ClearPriority, 1, 10);
		std::map<std::string, std::set<size_t>> blocked;
		std::map<std::string, std::string> texts;
		for (const Field& field : tribe.m_Econ.Fields)
		{
			if (!m_Zones.count(field.Name) || m_Clean.count(field.Name)) continue;
			std::vector<int> cells = field.Cells;
			if (cells.empty()) for (int i = 0; i < static_cast<int>(field.Plots.size()); ++i) cells.push_back(i);
			std::vector<std::vector<std::string>> perCell;
			std::vector<size_t> stuck;   // (a plot walled by the scenery or a cliff: nobody can till it, so it is not asked to)
			(void)Blockers(tribe, field.Origin, field.Columns, field.Spacing, cells, perCell, &stuck);
			std::set<std::string> nodes;
			for (const auto& c : perCell) nodes.insert(c.begin(), c.end());
			if (nodes.empty() && stuck.empty()) { m_Clean.insert(field.Name); continue; }   // (nothing grows back in it: it is not looked at again)
			std::set<std::string> gone;
			std::map<std::string, int> kinds;          // node type -> how many still stand
			std::map<std::string, std::string> tools;  // a tool tag the clan has none of -> the node type that asks for it
			std::string cannot;
			for (const std::string& node : nodes)
			{
				const AbilitySystemComponent* a = tribe.m_Gameplay ? tribe.m_Gameplay->Find(node) : nullptr;
				if (!a) { gone.insert(node); continue; }
				const int amount = ResourceSystem::Amount(*a);
				if (amount <= 0)
				{
					// spent: it goes for good (it would grow again in the field), once nobody is still at work on it
					bool busy = false;
					for (const TribeMember& m : tribe.m_Members) busy = busy || (!m.Dead && m.Detail == node && (m.Job == "gather" || m.Job == "designate"));
					if (!busy) { ClearAway(tribe, node, field.Name); gone.insert(node); continue; }
				}
				const std::string kind = ColonyDesignations::KindOf(tribe, node);
				++kinds[kind.empty() ? std::string("node") : kind];
				if (const auto def = tribe.m_Defs.find(kind); def != tribe.m_Defs.end() && !def->second.RequiresTag.empty() && tribe.m_Items)
				{
					int have = tribe.m_Store.CountTagged(*tribe.m_Items, def->second.RequiresTag);
					for (const TribeMember& m : tribe.m_Members) if (!m.Dead && m.Body) have += m.Body->Bag().CountTagged(*tribe.m_Items, def->second.RequiresTag);
					if (have == 0 && !tools.count(def->second.RequiresTag)) tools[def->second.RequiresTag] = kind;
				}
				if (amount > 0)
					if (const ColonyDesignations::Reply r = tribe.m_Designations.Clear(tribe, node, priority, field.Name); !r.Ok && cannot.empty()) cannot = r.Text;
			}
			std::set<size_t> wait;
			for (size_t i = 0; i < perCell.size(); ++i)
				for (const std::string& node : perCell[i]) if (!gone.count(node)) { wait.insert(i); break; }
			const size_t clearing = wait.size();
			wait.insert(stuck.begin(), stuck.end());
			if (wait.empty()) continue;
			// "to clear first: 3 tree, 1 rock (6 plots wait); no axe for tree"
			std::string text;
			if (clearing > 0)
			{
				text = "to clear first: ";
				bool first = true;
				for (const auto& [kind, n] : kinds) { text += (first ? "" : ", ") + std::to_string(n) + " " + Spaced(kind); first = false; }
				text += " (" + std::to_string(clearing) + (clearing == 1 ? " plot waits)" : " plots wait)");
			}
			if (!stuck.empty()) text += (text.empty() ? "" : "; ") + std::to_string(stuck.size()) + (stuck.size() == 1 ? " plot is" : " plots are") + " walled by what nobody can clear";
			for (const auto& [tag, kind] : tools)
			{
				std::string tool = tag.substr(tag.rfind('.') + 1);
				std::transform(tool.begin(), tool.end(), tool.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				text += "; no " + tool + " for " + Spaced(kind);
			}
			if (!cannot.empty()) text += "; " + cannot;
			blocked[field.Name] = std::move(wait);
			texts[field.Name] = text;
		}
		m_Blocked = std::move(blocked);
		m_ClearText = std::move(texts);
	}

	// ------------------------------------------------------------------ saving
	std::string FarmZones::ToJson() const
	{
		Json zones = Json::object();
		for (const auto& [name, z] : m_Zones)
		{
			zones[name] = Json{ { "crop", z.Crop }, { "plan", z.Plan }, { "auto", z.Auto } };
			if (!z.Outline.empty())
			{
				Json outline = Json::array();
				for (const glm::vec2& p : z.Outline) outline.push_back(Json::array({ p.x, p.y }));
				zones[name]["outline"] = outline;
			}
		}
		return Json{ { "zones", zones } }.dump();
	}

	bool FarmZones::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		std::map<std::string, Zone> zones;
		if (const auto z = j.find("zones"); z != j.end())
		{
			if (!z->is_object()) return false;
			for (const auto& [name, o] : z->items())
			{
				if (!o.is_object() || name.empty()) return false;
				Zone zone;
				if (o.contains("crop")) { if (!o["crop"].is_string()) return false; zone.Crop = o["crop"].get<std::string>(); }
				if (o.contains("plan")) { if (!o["plan"].is_string() || !IsPlan(o["plan"].get<std::string>())) return false; zone.Plan = o["plan"].get<std::string>(); }
				if (o.contains("auto")) { if (!o["auto"].is_boolean()) return false; zone.Auto = o["auto"].get<bool>(); }
				if (o.contains("outline"))
				{
					if (!o["outline"].is_array()) return false;
					for (const Json& p : o["outline"])
					{
						if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) return false;
						zone.Outline.push_back({ p[0].get<float>(), p[1].get<float>() });
					}
				}
				zones[name] = zone;
			}
		}
		m_Zones = std::move(zones);
		return true;
	}
}
