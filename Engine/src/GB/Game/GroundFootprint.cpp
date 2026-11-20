#include "GB/Game/GroundFootprint.h"
#include "GB/Game/Clans.h"
#include "GB/Game/Entity.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		// the rectangle's own axes on the ground (x, z): its x turned by the yaw, and its z
		void Axes(const GroundRect& r, glm::vec2& u, glm::vec2& v)
		{
			const float a = glm::radians(r.Yaw);
			u = { std::cos(a), -std::sin(a) };
			v = { std::sin(a), std::cos(a) };
		}

		const BuildingDef* DefByName(const std::map<std::string, BuildingDef>& defs, const std::string& name)
		{
			if (const auto it = defs.find(name); it != defs.end()) return &it->second;
			for (const auto& [id, def] : defs) if (def.Name == name) return &def;
			return nullptr;
		}
	}

	GroundRect BuildingGround(const BuildingDef& def, const glm::vec2& at, float yawDegrees)
	{
		return { at, 0.5f * def.Footprint, yawDegrees, def.Name };
	}

	GroundRect FieldGround(const glm::vec2& origin, int plots, int columns, float spacing)
	{
		const int n = std::max(1, plots);
		const int cols = std::max(1, std::min(columns, n));
		const int rows = (n + cols - 1) / cols;
		// x: the store one plot before the first, to the last column; z: the first row to the last (each plot `spacing` wide round its middle)
		const glm::vec2 lo = origin + glm::vec2(-1.5f * spacing, -0.5f * spacing);
		const glm::vec2 hi = origin + glm::vec2((static_cast<float>(cols) - 0.5f) * spacing, (static_cast<float>(rows) - 0.5f) * spacing);
		return { 0.5f * (lo + hi), 0.5f * (hi - lo), 0.0f, "a field" };
	}

	GroundRect FieldGround(const Field& field)
	{
		return FieldGround(field.Origin, field.GridCells(), field.Columns, field.Spacing);   // T280: a shaped field takes its bounding grid
	}

	glm::vec2 FieldStoreSpot(const glm::vec2& origin, int columns, float spacing, const std::vector<int>& cells)
	{
		const int cols = std::max(1, columns);
		const int first = cells.empty() ? 0 : cells.front();
		return origin + glm::vec2(static_cast<float>(first % cols) - 1.0f, static_cast<float>(first / cols)) * spacing;
	}

	void FieldGrounds(const glm::vec2& origin, int columns, float spacing, const std::vector<int>& cells, std::vector<GroundRect>& out)
	{
		const int cols = std::max(1, columns);
		bool whole = !cells.empty() && cells.back() + 1 == static_cast<int>(cells.size()) && static_cast<int>(cells.size()) % cols == 0;
		if (cells.empty() || whole) { out.push_back(FieldGround(origin, cells.empty() ? 1 : static_cast<int>(cells.size()), cols, spacing)); return; }
		const glm::vec2 store = FieldStoreSpot(origin, cols, spacing, cells);
		out.push_back({ store, glm::vec2(0.5f * spacing), 0.0f, "a field" });
		for (size_t i = 0; i < cells.size();)
		{
			const int row = cells[i] / cols, from = cells[i] % cols;
			size_t j = i + 1;
			while (j < cells.size() && cells[j] == cells[j - 1] + 1 && cells[j] / cols == row) ++j;
			const int to = cells[j - 1] % cols;
			const glm::vec2 lo = origin + glm::vec2((static_cast<float>(from) - 0.5f) * spacing, (static_cast<float>(row) - 0.5f) * spacing);
			const glm::vec2 hi = origin + glm::vec2((static_cast<float>(to) + 0.5f) * spacing, (static_cast<float>(row) + 0.5f) * spacing);
			out.push_back({ 0.5f * (lo + hi), 0.5f * (hi - lo), 0.0f, "a field" });
			i = j;
		}
	}

	void FieldGrounds(const Field& field, std::vector<GroundRect>& out)
	{
		if (field.Cells.empty()) { out.push_back(FieldGround(field)); return; }
		FieldGrounds(field.Origin, field.Columns, field.Spacing, field.Cells, out);
	}

	bool GroundOverlaps(const GroundRect& a, const GroundRect& b, float gap)
	{
		glm::vec2 au, av, bu, bv;
		Axes(a, au, av);
		Axes(b, bu, bv);
		const glm::vec2 ha = a.Half + glm::vec2(0.5f * gap), hb = b.Half + glm::vec2(0.5f * gap);
		const glm::vec2 d = b.Centre - a.Centre;
		for (const glm::vec2& n : { au, av, bu, bv })
		{
			const float ra = ha.x * std::abs(glm::dot(au, n)) + ha.y * std::abs(glm::dot(av, n));
			const float rb = hb.x * std::abs(glm::dot(bu, n)) + hb.y * std::abs(glm::dot(bv, n));
			if (ra + rb - std::abs(glm::dot(d, n)) <= kGroundTouch) return false;   // a separating axis (or they only touch)
		}
		return true;
	}

	std::string GroundFree(const GroundRect& want, const std::vector<GroundRect>& taken, float gap)
	{
		for (const GroundRect& t : taken)
			if (GroundOverlaps(want, t, gap)) return "it would stand on " + (t.What.empty() ? std::string("something") : t.What);
		return {};
	}

	std::vector<GroundRect> TakenGround(const Tribe& tribe, bool fields)
	{
		std::vector<GroundRect> out;
		if (const auto* defs = tribe.Buildings())
			for (const Tribe::SettlementSite& s : tribe.SettlementSites())   // (SettlementSite::Building is the def's name)
				if (const BuildingDef* def = DefByName(*defs, s.Building)) out.push_back(BuildingGround(*def, s.Pos));
		tribe.Modules().Ground(tribe, out);   // T367 (Y-2): the reserved yards, the sizes bigger than their building, the ground of a growth
		if (!fields) return out;
		for (const Field& f : tribe.Economy().Fields) FieldGrounds(f, out);   // T337: a shaped field plot by plot
		if (tribe.Outside().Ground) tribe.Outside().Ground(tribe, out);
		return out;
	}

	std::string GroundFreeFor(const Tribe& tribe, const BuildingDef& def, const glm::vec2& at)
	{
		return GroundFree(BuildingGround(def, at), TakenGround(tribe));
	}

	glm::vec2 FreeSiteSpot(const Tribe& tribe, const std::string& building, const glm::vec2& wanted)
	{
		const auto* defs = tribe.Buildings();
		if (!defs || !defs->count(building)) return wanted;
		const BuildingDef& def = defs->at(building);
		if (GroundFreeFor(tribe, def, wanted).empty()) return wanted;
		return PlaceSite(tribe.SitePlacementFor(def)).value_or(wanted);
	}

	std::vector<GroundRect> WorldGround(const EntityWorld& entities, const GameplayWorld& gameplay, const std::map<std::string, BuildingDef>& defs, const ClanWorld* clans, const Tribe* alone)
	{
		std::vector<GroundRect> out;
		for (const GameplayWorld::Tagged& building : gameplay.WithTag("Building"))
		{
			const AbilitySystemComponent* a = gameplay.Find(*building.Name);
			const BuildingDef* def = a ? BuildingOf(*a, defs) : nullptr;
			const EntityId id = entities.FindByName(*building.Name);
			if (!def || id == NullEntity) continue;
			const glm::mat4 m = entities.WorldMatrix(id);
			GroundRect r = BuildingGround(*def, { m[3].x, m[3].z }, glm::degrees(std::atan2(-m[0].z, m[0].x)));
			r.What = *building.Name;
			out.push_back(r);
		}
		auto fieldsOf = [&](const Tribe& t, const std::string& clan)
		{
			for (const Field& f : t.Economy().Fields)
			{
				const size_t from = out.size();
				FieldGrounds(f, out);   // T337 (a rectangle field: its one rectangle, as before)
				for (size_t i = from; i < out.size(); ++i) out[i].What = clan + " " + f.Name;
			}
		};
		if (clans && clans->Count() > 0)
			for (const auto& c : clans->All()) { if (c->Folk) fieldsOf(*c->Folk, c->Name); }
		else if (alone) fieldsOf(*alone, "clan");
		return out;
	}

	std::vector<std::pair<size_t, size_t>> OverlappingPairs(const std::vector<GroundRect>& ground)
	{
		std::vector<std::pair<size_t, size_t>> out;
		for (size_t i = 0; i < ground.size(); ++i)
			for (size_t j = i + 1; j < ground.size(); ++j)
				if (GroundOverlaps(ground[i], ground[j])) out.emplace_back(i, j);
		return out;
	}
}
