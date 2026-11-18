#include "GB/Game/FarmZones.h"
#include "GB/Game/FieldPlacement.h"
#include "GB/Game/GroundFootprint.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/Tribe.h"
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

	FarmZones::Reply FarmZones::PaintOutline(Tribe& tribe, const std::vector<glm::vec2>& outline, const std::string& crop)
	{
		const FarmRules& f = tribe.Config.Econ.Farming;
		if (outline.size() < 3) return { false, "a zone needs 3 points or more" };
		if (!crop.empty() && !tribe.Config.Econ.Crops.count(crop)) return { false, "no crop called " + crop + " in the loaded content" };
		const float sp = std::max(0.5f, f.Spacing);
		glm::vec2 lo = outline[0], hi = outline[0];
		for (const glm::vec2& p : outline) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
		const int columns = static_cast<int>(std::floor((hi.x - lo.x) / sp + 0.5f)) + 1;
		const int rows = static_cast<int>(std::floor((hi.y - lo.y) / sp + 0.5f)) + 1;
		if (static_cast<long long>(columns) * rows > 4LL * kMostPlots) return { false, "the zone is too big: at most " + std::to_string(kMostPlots) + " plots" };
		std::vector<int> cells;
		for (int c = 0; c < columns * rows; ++c)
			if (Inside(outline, lo + glm::vec2(static_cast<float>(c % columns), static_cast<float>(c / columns)) * sp)) cells.push_back(c);
		const int plots = static_cast<int>(cells.size());
		if (plots == 0) return { false, "no plot falls inside the zone" };
		if (plots > kMostPlots) return { false, "the zone would have " + std::to_string(plots) + " plots: at most " + std::to_string(kMostPlots) };
		// the ground: nothing standing there (any clan's site, building or field: its bounding grid), and every plot dry ground a member can stand on
		const GroundRect want = FieldGround(lo, cells.back() + 1, columns, sp);
		if (const std::string why = GroundFree(want, TakenGround(tribe), 0.0f); !why.empty()) return { false, "cannot paint there: " + why };
		const ITribeBody* body = nullptr;
		for (const TribeMember& m : tribe.m_Members) if (!m.Dead && m.Body) { body = m.Body; break; }
		if (body)
			for (const int cell : cells)
			{
				const glm::vec2 p = lo + glm::vec2(static_cast<float>(cell % columns), static_cast<float>(cell / columns)) * sp;
				char at[48];
				std::snprintf(at, sizeof at, "%.0f, %.0f", static_cast<double>(p.x), static_cast<double>(p.y));
				if (body->WaterDepthAt(p) > kWetDepth) return { false, std::string("cannot paint there: the plot at ") + at + " is in the water" };
				bool stand = body->CanStandAt(p);   // the plot's middle and its four quarters (a trunk between two middles)
				for (const glm::vec2 d : { glm::vec2(0.5f, 0.5f), glm::vec2(-0.5f, 0.5f), glm::vec2(0.5f, -0.5f), glm::vec2(-0.5f, -0.5f) }) stand = stand && body->CanStandAt(p + d * sp * 0.5f);
				if (!stand) return { false, std::string("cannot paint there: the plot at ") + at + " is on a rock or a trunk" };
			}
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
			tribe.AddStockpile(name + " store", lo + glm::vec2(-sp, 0.0f), tribe.Config.Econ.Logistics.FieldStore);
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
		return { true, text };
	}

	FarmZones::Reply FarmZones::SetCrop(Tribe& tribe, const std::string& zone, const std::string& crop)
	{
		const std::string c = crop == "any" || crop == "-" ? std::string() : crop;
		if (!c.empty() && !tribe.Config.Econ.Crops.count(c)) return { false, "no crop called " + c + " in the loaded content" };
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

	std::string FarmZones::AutoCrop(const Tribe& tribe)
	{
		const EconomyRules& econ = tribe.Config.Econ;
		const Season season = tribe.CurrentSeason();
		std::string best;
		int bestStock = 0, bestYield = 0;
		for (const auto& [id, crop] : econ.Crops)
		{
			if (!crop.SowsIn(season) || tribe.m_Store.Count(crop.Seed) <= 0) continue;
			std::string produce;
			int most = 0, yield = 0;
			for (const auto& [item, n] : crop.Yield)
			{
				yield += n;
				if (item != crop.Seed && n > most) { most = n; produce = item; }
			}
			const int stock = produce.empty() ? 0 : tribe.Stored(produce);
			if (best.empty() || stock < bestStock || (stock == bestStock && yield > bestYield)) { best = id; bestStock = stock; bestYield = yield; }
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
			return c != econ.Crops.end() && c->second.SowsIn(season) && tribe.m_Store.Count(c->second.Seed) > 0 ? z->Crop : std::string();
		}
		for (const auto& [id, crop] : econ.Crops) if (crop.SowsIn(season) && tribe.m_Store.Count(crop.Seed) > 0) return id;   // the first by id (as before)
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
			plots = action == "till" ? field.Find("grass", most) : action == "sow" ? field.Find("dug", most) : action == "tend" ? field.Dry(most) : field.Find("grown", most);
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
		if (!m_Player && static_cast<int>(state.Fields.size()) < tribe.FieldsWanted() && season != Season::Winter)
		{
			bool seeds = false, can = false;
			for (const auto& [id, crop] : econ.Crops) seeds = seeds || tribe.Stored(crop.Seed) > 0;
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
		if (state.Fields.empty()) return;
		int grass = 0, dug = 0, dry = 0, grown = 0;
		std::string sowable;
		for (const Field& field : state.Fields)
		{
			if (Works(field, "till")) grass += field.Count("grass");
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
