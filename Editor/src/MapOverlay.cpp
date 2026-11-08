// U1 (docs/PLAN_SIMULACION_NATURAL.md): the map of areas, painted on the ground of the viewport with filters: the land each clan holds (its
// territory, region tile by tile, in the clan's colour), the resource zones of the world (where copper, flint, clay... grow), each clan's home
// ground (HomeRadius and its edge) and the productivity map of an item for a clan (N0, Tribe::ProductivityMap). Only what lies near what the
// camera looks at is drawn. `areas` in the console, or the "Map areas" window.
#include "EditorApp.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/World/Regions.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <set>

namespace GB::Editor
{
	namespace
	{
		ImU32 Rgba(const glm::vec3& c, float a)
		{
			auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
			return IM_COL32(b(c.r), b(c.g), b(c.b), b(a));
		}
	}

	std::vector<std::pair<const Tribe*, ClanId>> EditorApp::MapClans() const
	{
		std::vector<std::pair<const Tribe*, ClanId>> out;
		for (const auto& c : m_Clans.All()) if (c->Folk) out.push_back({ c->Folk, c->Id });
		if (out.empty() && !m_Tribe.Members().empty()) out.push_back({ &m_Tribe, -1 });
		return out;
	}

	void EditorApp::DrawMapOverlay(float ox, float oy, float w, float h)
	{
		MapAreas& m = m_MapAreas;
		if (!m.On || !m_World.IsBuilt()) return;
		const GeneratedWorld& world = m_World.World();
		if (!world.TerrainRef) return;
		const Terrain& terrain = *world.TerrainRef;
		const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
		const glm::vec2 focus(m_Camera.Target.x, m_Camera.Target.z);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		auto screen = [&](const glm::vec2& p, ImVec2& out)
		{
			glm::vec2 s;
			if (!ProjectToScreen(viewProj, glm::vec3(p.x, terrain.GetHeight(p.x, p.y) + 0.15f, p.y), glm::vec2(w, h), s)) return false;
			out = ImVec2(ox + s.x, oy + s.y);
			return true;
		};
		auto quad = [&](const glm::vec2& a, float side, ImU32 colour)
		{
			ImVec2 p[4];
			if (!screen(a, p[0]) || !screen(a + glm::vec2(side, 0.0f), p[1]) || !screen(a + glm::vec2(side, side), p[2]) || !screen(a + glm::vec2(0.0f, side), p[3])) return;
			dl->AddQuadFilled(p[0], p[1], p[2], p[3], colour);
		};
		auto ring = [&](const glm::vec2& c, float r, ImU32 colour, float thick)
		{
			std::vector<ImVec2> points;
			const int n = std::clamp(static_cast<int>(r * 0.5f), 24, 96);
			for (int i = 0; i < n; ++i)
			{
				const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
				ImVec2 s;
				if (!screen(c + r * glm::vec2(std::cos(a), std::sin(a)), s)) { if (points.size() > 1) dl->AddPolyline(points.data(), static_cast<int>(points.size()), colour, 0, thick); points.clear(); continue; }
				points.push_back(s);
			}
			if (points.size() > 1) dl->AddPolyline(points.data(), static_cast<int>(points.size()), colour, points.size() == static_cast<size_t>(n) ? ImDrawFlags_Closed : 0, thick);
		};
		auto label = [&](const glm::vec2& p, const std::string& text, ImU32 colour)
		{
			ImVec2 s;
			if (!screen(p, s)) return;
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());
			dl->AddRectFilled(ImVec2(s.x - size.x * 0.5f - 3.0f, s.y - 2.0f), ImVec2(s.x + size.x * 0.5f + 3.0f, s.y + size.y + 2.0f), IM_COL32(0, 0, 0, 150), 3.0f);
			dl->AddText(ImVec2(s.x - size.x * 0.5f, s.y), colour, text.c_str());
		};

		// the land each clan holds: the region of each tile, its owner's colour
		if (m.Territory)
			if (const RegionMap* regions = m_Clans.Regions(); regions && !regions->Empty())
			{
				const float tile = regions->Rules().Tile;
				const int n = static_cast<int>(std::ceil(m.Reach / tile));
				const glm::vec2 base = glm::floor(focus / tile) * tile;
				for (int j = -n; j <= n; ++j)
					for (int i = -n; i <= n; ++i)
					{
						const glm::vec2 a = base + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * tile;
						const int region = regions->RegionAt(a + glm::vec2(0.5f * tile));
						if (region < 0) continue;
						const std::vector<ClanId> claimants = m_Clans.Territory().Claimants(region, m_Clans.Rules.Territory);
						if (claimants.empty()) continue;
						// a contested region is painted by its strongest claimant, darker
						quad(a, tile, Rgba(m_Clans.ColorOf(claimants.front()) * (claimants.size() > 1 ? 0.6f : 1.0f), 0.28f));
					}
			}

		// each clan's home ground: where it gathers first (HomeRadius) and its edge
		if (m.Home)
			for (const auto& [tribe, id] : MapClans())
			{
				const glm::vec3 c = m_Clans.ColorOf(id);
				const glm::vec2 camp = tribe->Config.Economy.Camp;
				ring(camp, tribe->Config.Exploration.HomeRadius, Rgba(c, 0.95f), 2.5f);
				ring(camp, tribe->Config.Exploration.HomeRadius + tribe->Config.Exploration.HomeFalloff, Rgba(c, 0.5f), 1.5f);
				const Clan* clan = m_Clans.Find(id);
				label(camp + glm::vec2(0.0f, -tribe->Config.Exploration.HomeRadius), clan ? clan->Name : std::string("clan"), Rgba(c, 1.0f));
			}

		// the resource zones of the world (R1): where a type of node grows, with its name
		if (m.Zones)
		{
			if (m.ZonesBuild != m_World.BuildCount() + 1)
			{
				m.ZoneList = PlanResourceZones(ResourceGroundOf(world), m_NodeDefs, world.Desc.Seed);
				m.ZonesBuild = m_World.BuildCount() + 1;
			}
			for (const ResourceZone& z : m.ZoneList)
			{
				if (glm::distance(z.Centre, focus) > m.Reach + z.Radius) continue;
				const auto def = m_NodeDefs.find(z.Node);
				const std::string item = def != m_NodeDefs.end() ? def->second.Item : z.Node;
				const uint32_t hue = static_cast<uint32_t>(std::hash<std::string>{}(z.Node));
				const glm::vec3 c(0.45f + 0.55f * static_cast<float>(hue & 0xFF) / 255.0f, 0.45f + 0.55f * static_cast<float>((hue >> 8) & 0xFF) / 255.0f, 0.45f + 0.55f * static_cast<float>((hue >> 16) & 0xFF) / 255.0f);
				ring(z.Centre, z.Radius, Rgba(c, 0.9f), 2.5f);
				label(z.Centre, item + " zone", Rgba(c, 1.0f));
			}
		}

		// the productivity of an item for each clan (N0): a heat map round its camp
		if (m.Productivity && !m.Item.empty())
			for (const auto& [tribe, id] : MapClans())
			{
				const float cell = 15.0f;
				const std::vector<Tribe::ProductivityCell> cells = tribe->ProductivityMap(m.Item, cell, 200.0f, 30.0f);
				float top = 0.0f;
				for (const Tribe::ProductivityCell& c : cells) top = std::max(top, c.Value);
				if (top <= 0.0f) continue;
				for (const Tribe::ProductivityCell& c : cells)
				{
					if (glm::distance(c.Pos, focus) > m.Reach) continue;
					const float t = c.Value / top;
					if (t < 0.15f) continue;   // (the faint tail of far nodes: left out, so the rich spots stand out)
					quad(c.Pos - glm::vec2(0.5f * cell), cell, Rgba(glm::mix(glm::vec3(1.0f, 0.95f, 0.3f), glm::vec3(0.95f, 0.15f, 0.1f), t), 0.08f + 0.5f * t * t));
				}
			}
	}

	void EditorApp::DrawMapPanel()
	{
		MapAreas& m = m_MapAreas;
		if (!m.On) return;
		ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_Appearing);
		if (ImGui::Begin("Map areas", &m.On))
		{
			ImGui::Checkbox("territory (clan colours)", &m.Territory);
			ImGui::Checkbox("home ground", &m.Home);
			ImGui::Checkbox("resource zones", &m.Zones);
			ImGui::Checkbox("productivity of", &m.Productivity);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##item", m.Item.empty() ? "-" : m.Item.c_str()))
			{
				std::set<std::string> items;
				for (const auto& [id, d] : m_NodeDefs) { items.insert(d.Item); if (!d.ExtraItem.empty()) items.insert(d.ExtraItem); }
				for (const std::string& it : items) if (ImGui::Selectable(it.c_str(), it == m.Item)) m.Item = it;
				ImGui::EndCombo();
			}
			ImGui::SliderFloat("reach (m)", &m.Reach, 50.0f, 800.0f, "%.0f");
			for (const auto& [tribe, id] : MapClans())
			{
				const Clan* clan = m_Clans.Find(id);
				const glm::vec3 c = m_Clans.ColorOf(id);
				ImGui::ColorButton(("##c" + std::to_string(id)).c_str(), ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12.0f, 12.0f));
				ImGui::SameLine();
				ImGui::TextUnformatted(clan ? clan->Name.c_str() : "clan");
			}
		}
		ImGui::End();
	}

	CommandResult EditorApp::MapAreasCommand(const std::vector<std::string>& a)
	{
		MapAreas& m = m_MapAreas;
		if (a.empty()) { m.On = !m.On; return { true, m.On ? "map areas on" : "map areas off" }; }
		const std::string& what = a[0];
		if (what == "off") { m.On = false; return { true, "map areas off" }; }
		m.On = true;
		if (what == "on") return { true, "map areas on" };
		if (what == "territory") m.Territory = !m.Territory;
		else if (what == "home") m.Home = !m.Home;
		else if (what == "zones") m.Zones = !m.Zones;
		else if (what == "reach" && a.size() > 1) m.Reach = std::clamp(std::stof(a[1]), 50.0f, 800.0f);
		else if (what == "productivity")
		{
			if (a.size() > 1) { m.Item = a[1]; m.Productivity = true; }
			else m.Productivity = !m.Productivity;
		}
		else if (what == "zonelist")
		{
			if (!m_World.IsBuilt()) return { false, "no world" };
			const std::vector<ResourceZone> zones = PlanResourceZones(ResourceGroundOf(m_World.World()), m_NodeDefs, m_World.World().Desc.Seed);
			std::string s = std::to_string(zones.size()) + " resource zones:";
			for (const ResourceZone& z : zones)
			{
				char line[160];
				std::snprintf(line, sizeof line, "\n  %s at (%.0f, %.0f), radius %.0f m", z.Node.c_str(), static_cast<double>(z.Centre.x), static_cast<double>(z.Centre.y), static_cast<double>(z.Radius));
				s += line;
			}
			return { true, s };
		}
		else return { false, "areas [on|off|territory|home|zones|productivity [item]|reach <m>|zonelist]" };
		return { true, std::string("map areas: territory ") + (m.Territory ? "on" : "off") + ", home " + (m.Home ? "on" : "off") + ", zones " + (m.Zones ? "on" : "off")
			+ ", productivity " + (m.Productivity ? m.Item : std::string("off")) };
	}
}
