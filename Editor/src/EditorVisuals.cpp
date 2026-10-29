// The editor's side of the visual layer (docs/VISUALES.md): the looks of resource nodes, buildings and creatures drawn by the EntityRenderer's hook
// from the "visuals" section of the packs, the tribe director for every clan (props in the hands, on the hips and the back, loads in the arms with
// the carrying sets, the activity montages, the hands on what they work, the fields and the store's piles), and the `visuals` and `building`
// commands (the second is how H8's construction sites are shown until they arrive: BuildingVisuals::Build).
#include "EditorApp.h"
#include "GB/Log.h"
#include "GB/Visual/BuildingVisuals.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace GB::Editor
{
	void EditorApp::BindVisuals()
	{
		m_Visuals.SetAssetDir(m_AssetRoot);
		m_Visuals.SetNodes(&m_NodeDefs);
		m_Visuals.SetBuildings(&m_Buildings.Defs);   // H8: the sites and buildings are drawn by phase and work
		// the live amount of a node (and phase of a building) is the gameplay world's; the entity's own component is only what was loaded
		m_Visuals.SetAttributes([this](const std::string& entity, const std::string& attribute, float& value)
		{
			const AbilitySystemComponent* a = m_Gameplay.Find(entity);
			if (!a || !a->Attributes().Has(attribute)) return false;
			value = a->Attributes().Current(attribute);
			return true;
		});
		m_EntityRenderer->SetEntityVisual([this](const EntityWorld& w, EntityId id, const glm::mat4& m, Scene& s) { return m_Visuals.Draw(w, id, m, s); });
	}

	void EditorApp::UpdateVisuals(float dt)
	{
		m_Visuals.Update(dt);
		TribeVisuals::Context ctx;
		ctx.Actors = &m_Actors;
		ctx.Gameplay = &m_Gameplay;
		ctx.Items = &m_Items;
		ctx.Entities = &m_SceneEditor.World();
		ctx.Visuals = &m_Visuals;
		for (const auto& clan : m_Clans.All())   // every clan (H13), not only the first tribe
			if (clan->Folk) m_TribeVisuals.Update(*clan->Folk, ctx, dt);
	}

	void EditorApp::AddVisualsToScene()
	{
		TribeVisuals::Context ctx;
		ctx.Actors = &m_Actors;
		ctx.Items = &m_Items;
		ctx.Visuals = &m_Visuals;
		if (m_World.IsBuilt()) ctx.Ground = [this](float x, float z) { return m_World.GetTerrain().GetHeight(x, z); };
		ctx.Season = SeasonClock::Name(SeasonClock().At(static_cast<double>(m_SimTick) / 60.0));
		for (const auto& clan : m_Clans.All())
			if (clan->Folk) m_TribeVisuals.AddToScene(*clan->Folk, ctx, m_Scene);
	}

	void EditorApp::RegisterVisualCommands()
	{
		CommandRunner& c = m_Commands;
		c.Register("visuals", "visuals [on|off|status|props on|off|activities on|off|reach on|off|nodes [x z]]: the procedural looks of nodes, buildings, creatures and the tribe's props (assets/data/visuals.json); `nodes` lays out every node model full, half and empty",
			[this](const std::vector<std::string>& a) -> CommandResult
		{
			auto flag = [](const std::string& v) { return v == "on" || v == "1" || v == "true"; };
			if (a.empty() || a[0] == "status")
			{
				std::ostringstream out;
				out << "visuals " << (m_Visuals.Enabled ? "on" : "off") << ": " << m_Visuals.Library.Models.size() << " models, " << m_Visuals.Library.Buildings.size() << " buildings, "
					<< m_Visuals.Library.Materials.size() << " materials; cache " << m_Visuals.Cache().CachedMeshes() << " meshes, " << m_Visuals.Cache().CachedTextures() << " textures"
					<< (m_Visuals.Cache().LowRes() ? " (low-res)" : "") << "; tribe props " << (m_TribeVisuals.Props ? "on" : "off") << ", activities " << (m_TribeVisuals.Activities ? "on" : "off")
					<< ", reach " << (m_TribeVisuals.Reach ? "on" : "off");
				for (const auto& clan : m_Clans.All())
					if (clan->Folk)
						for (const TribeMember& m : clan->Folk->Members())
							if (const TribeVisuals::MemberState* st = m_TribeVisuals.State(m.Name))
							{
								out << "\n  " << m.Name << " [" << st->Activity << (st->Ability.empty() ? "" : " / " + st->Ability) << "]";
								for (const PropPlacement& p : st->Applied.Props) out << " " << p.Look << "@" << p.Socket;
								if (!st->Set.empty()) out << " set " << st->Set;
								if (!st->Montage.empty()) out << " montage " << st->Montage;
								if (st->ReachWeight > 0.0f) out << " reach " << st->ReachWeight;
							}
				return { true, out.str() };
			}
			if (a[0] == "on" || a[0] == "off") { m_Visuals.Enabled = m_TribeVisuals.Enabled = a[0] == "on"; return { true, "visuals " + a[0] }; }
			if (a.size() >= 2 && a[0] == "props") { m_TribeVisuals.Props = flag(a[1]); return { true, "props " + a[1] }; }
			if (a.size() >= 2 && a[0] == "activities") { m_TribeVisuals.Activities = flag(a[1]); return { true, "activities " + a[1] }; }
			if (a.size() >= 2 && a[0] == "reach") { m_TribeVisuals.Reach = flag(a[1]); return { true, "reach " + a[1] }; }
			if (a[0] == "nodes")
			{
				// a showcase: every node kind in a row, full, half and empty (entities with the node's tags and an Amount; not in the gameplay world)
				const float x0 = a.size() >= 3 ? static_cast<float>(std::atof(a[1].c_str())) : 0.0f, z0 = a.size() >= 3 ? static_cast<float>(std::atof(a[2].c_str())) : 0.0f;
				EntityWorld& w = m_SceneEditor.World();
				if (const EntityId old = w.FindByName("Visual nodes"); old != NullEntity) w.Destroy(old);
				const EntityId root = w.Create("Visual nodes");
				int col = 0;
				for (const auto& [id, def] : m_NodeDefs)
				{
					const std::string model = def.Model.empty() ? id : def.Model;
					if (!m_Visuals.Library.Models.count(model)) continue;
					const float amounts[] = { 1.0f, 0.5f, 0.0f };
					for (int row = 0; row < 3; ++row)
					{
						const float x = x0 + static_cast<float>(col) * 5.0f, z = z0 + static_cast<float>(row) * 5.0f;
						const EntityId e = w.Create("visual " + id + " " + std::to_string(row), root);
						Entity& ent = *w.Find(e);
						ent.Transform.Position = { x, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f, z };
						ent.Tags = { "visual-demo" };
						ent.Gameplay.emplace();
						ent.Gameplay->Tags = { "Resource.Node." + id };   // (the kind only: the hook reads it; `ability sync` would make them real nodes)
						ent.Gameplay->Attributes = { { "Amount", std::round(amounts[row] * static_cast<float>(def.Capacity)), 0.0f, static_cast<float>(def.Capacity) } };
					}
					++col;
				}
				return { true, std::to_string(col) + " node kinds laid out at " + std::to_string(static_cast<int>(x0)) + ", " + std::to_string(static_cast<int>(z0)) + " (rows: full, half, empty)" };
			}
			return { false, "usage: visuals [on|off|status|props on|off|activities on|off|reach on|off|nodes [x z]]" };
		});

		c.Register("building", "building <id> [phase] [progress] [x z] [name] | building <id> phases [x z]: a building site as H8 will make it (BuildingVisuals::Build); `phases` lays out every phase of it",
			[this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty())
			{
				std::string ids;
				for (const auto& [id, b] : m_Visuals.Library.Buildings) ids += (ids.empty() ? "" : ", ") + id + " (" + std::to_string(b.Phases.size()) + ")";
				return { true, "buildings: " + ids };
			}
			const std::string& id = a[0];
			const int phases = BuildingVisuals::PhaseCount(m_Visuals.Library, id);
			if (phases == 0) return { false, "no building '" + id + "' in the visuals" };
			EntityWorld& w = m_SceneEditor.World();
			auto ground = [this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; };
			if (a.size() >= 2 && a[1] == "phases")
			{
				const float x0 = a.size() >= 4 ? static_cast<float>(std::atof(a[2].c_str())) : 0.0f, z0 = a.size() >= 4 ? static_cast<float>(std::atof(a[3].c_str())) : 0.0f;
				const float step = std::max(m_Visuals.Library.Buildings.at(id).Footprint.x, 2.0f) + 1.5f;
				int n = 0;
				for (int phase = 0; phase <= phases; ++phase)
					for (const float progress : { 0.5f, 1.0f })
					{
						if (phase == phases && progress < 1.0f) continue;
						const float x = x0 + static_cast<float>(n++) * step;
						BuildingVisuals::Build(w, id, phase, progress, id + " phase " + std::to_string(phase) + (progress < 1.0f ? " half" : ""), NullEntity, { x, ground(x, z0), z0 });
					}
				return { true, std::to_string(n) + " sites of " + id + " from the first stakes to the finished building" };
			}
			const int phase = a.size() >= 2 ? std::atoi(a[1].c_str()) : phases;
			const float progress = a.size() >= 3 ? static_cast<float>(std::atof(a[2].c_str())) : 1.0f;
			const float x = a.size() >= 5 ? static_cast<float>(std::atof(a[3].c_str())) : 0.0f, z = a.size() >= 5 ? static_cast<float>(std::atof(a[4].c_str())) : 0.0f;
			const std::string name = a.size() >= 6 ? a[5] : "";
			const EntityId e = BuildingVisuals::Build(w, id, phase, progress, name, NullEntity, { x, ground(x, z), z });
			return { true, w.Find(e)->Name + ": " + (phase >= phases ? std::string("finished") : BuildingVisuals::PhaseName(m_Visuals.Library, id, phase) + " " + std::to_string(static_cast<int>(progress * 100.0f)) + "%") };
		});
	}
}
