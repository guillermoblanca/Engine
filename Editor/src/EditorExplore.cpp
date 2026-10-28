// The editor's side of H12: the regions of the generated world (kept in step with the seed, the generator and the wild nodes, and handed to the
// tribe) and the `region` command. The regions are never saved: a loaded game builds them again from its world.
#include "EditorApp.h"
#include "GB/Gameplay/WorldResources.h"
#include <cstdlib>

namespace GB::Editor
{
	void EditorApp::SyncRegions()
	{
		if (!m_World.IsBuilt())
		{
			if (m_Tribe.Regions()) m_Tribe.SetRegions(nullptr);
			m_RegionsKey.clear();
			m_Regions = RegionMap();
			return;
		}
		const WorldDesc& d = m_World.World().Desc;
		const std::string key = std::to_string(static_cast<int>(d.Type)) + " " + std::to_string(d.Seed) + " " + std::to_string(d.GeneratorVersion) + " " + std::to_string(m_WildResources)
			+ " " + ExplorationRulesToJson(m_Tribe.Config.Explore);
		if (key != m_RegionsKey)
		{
			std::vector<ResourceSpot> spots;
			if (m_WildResources > 0.0f) spots = PlanWorldResources(ResourceGroundOf(m_World.World()), m_NodeDefs, d.Seed, m_WildResources);
			m_Regions = RegionMap::Build(m_World.World(), m_Tribe.Config.Explore.Regions, RegionThingsOf(spots));
			m_RegionsKey = key;
			m_RegionTicks = 0;
			m_Tribe.SetRegions(nullptr);   // (the same address: set it again so the tribe checks its map against the new cut)
		}
		if (m_Tribe.Regions() != &m_Regions) m_Tribe.SetRegions(&m_Regions, [this](float x, float z) { return m_World.GetTerrain().GetHeight(x, z); });
		if (--m_RegionTicks > 0) return;
		m_RegionTicks = 600;   // the predators move: their count per region every ten simulated seconds
		std::vector<glm::vec2> predators;
		for (const auto& [id, s] : m_Ecology.Species)
		{
			if (!s.Hunts()) continue;
			for (const std::string& name : m_Gameplay.Names())
				if (const AbilitySystemComponent* a = m_Gameplay.Find(name); a && a->HasTag(s.Tag))
					if (const EntityId e = m_SceneEditor.World().FindByName(name); e != NullEntity)
					{
						const glm::vec4 p = m_SceneEditor.World().WorldMatrix(e)[3];
						predators.push_back({ p.x, p.z });
					}
		}
		m_Regions.SetPredators(predators);
	}

	CommandResult EditorApp::RegionCommand(const std::vector<std::string>& a)
	{
		if (!m_World.IsBuilt()) return { false, "the regions need a generated world (generate temperate|alpine|desert <seed>)" };
		m_RegionTicks = 0;
		SyncRegions();
		const std::string sub = a.empty() ? "list" : a[0];
		std::string s;
		if (sub == "list") s = m_Regions.List();
		else if (sub == "info" && a.size() > 1)
		{
			std::string key = a[1];
			for (size_t i = 2; i < a.size(); ++i) key += " " + a[i];
			const RegionSummary* g = m_Regions.Find(key);
			if (!g) return { false, "no region '" + key + "' (region list)" };
			s = m_Regions.Describe(g->Id);
			if (!m_Tribe.Members().empty()) s += "the clan:" + m_Tribe.RegionLoreReport(g->Id);
		}
		else if (sub == "explore") s = m_Tribe.Members().empty() ? std::string("no tribe (tribe build)") : m_Tribe.ExploreReport();
		else return { false, "usage: region list | info <id|name> | explore" };
		for (char& c : s) if (c == '\n') c = '|';   // one line: the console shows the first line of a message
		return { true, s };
	}
}
