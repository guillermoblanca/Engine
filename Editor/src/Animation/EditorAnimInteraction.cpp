#include "EditorApp.h"
#include "GB/Gameplay/Resources.h"
#include <cmath>

namespace GB::Editor
{
	bool EditorApp::TryOnNode(const std::string& nodeId, std::string& report)
	{
		Actor* actor = m_Player.Controlled();
		if (!actor) { report = "no controlled character (player <fbx>)"; return false; }
		const auto def = m_NodeDefs.find(nodeId);
		if (def == m_NodeDefs.end()) { report = "no resource node type '" + nodeId + "'"; return false; }
		if (def->second.Ability.empty()) { report = "'" + nodeId + "' is not worked by any ability"; return false; }
		EntityWorld& world = m_SceneEditor.World();
		std::string best;
		glm::vec3 where(0.0f);
		float bestDistance = 1e30f;
		const glm::vec3 me = actor->GetPosition();
		for (const std::string& name : m_Gameplay.Names())
		{
			const AbilitySystemComponent* asc = m_Gameplay.Find(name);
			if (!asc || !asc->HasTag("Resource.Node." + nodeId) || asc->HasTag("State.Depleted")) continue;
			const EntityId id = world.FindByName(name);
			if (id == NullEntity) continue;
			const glm::vec3 p = glm::vec3(world.WorldMatrix(id)[3]);
			const float d = (p.x - me.x) * (p.x - me.x) + (p.z - me.z) * (p.z - me.z);
			if (d < bestDistance) { bestDistance = d; best = name; where = p; }
		}
		if (!m_Gameplay.Find(m_Player.ControlledName()))
		{
			if (world.FindByName(m_Player.ControlledName()) == NullEntity) world.Find(world.Create(m_Player.ControlledName()))->Gameplay.emplace();
			m_Gameplay.Create(m_Player.ControlledName());
			actor->SetAbilities(&m_Gameplay, m_Player.ControlledName());
		}
		if (best.empty()) { report = "there is no '" + nodeId + "' node in the world (generate one with resources: generate temperate 5)"; return false; }
		if (!def->second.RequiresTag.empty())
			for (const std::string& item : m_Items.Ids())
			{
				const ItemDef* d = m_Items.Find(item);
				bool matches = false;
				if (d) for (const std::string& tag : d->Tags) matches = matches || GameplayTag(def->second.RequiresTag).Matches(tag);
				if (!matches) continue;
				if (actor->Bag().Count(item) == 0) actor->Bag().Add(m_Items, item, 1);
				break;
			}
		if (AbilitySystemComponent* asc = m_Gameplay.Find(m_Player.ControlledName())) ResourceSystem::SyncTools(*asc, actor->Bag(), m_Items);
		glm::vec2 away(me.x - where.x, me.z - where.z);
		away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec2(0.0f, 1.0f);
		const glm::vec2 stand = glm::vec2(where.x, where.z) + away * 2.2f;
		actor->Teleport(stand.x, stand.y, std::atan2(-away.x, -away.y));
		std::string error;
		if (!m_Player.UseAbility(def->second.Ability, best, error)) { report = def->second.Ability + " on '" + best + "' did not start: " + error; return false; }
		report = "the character uses " + def->second.Ability + " on '" + best + "'";
		return true;
	}
}
