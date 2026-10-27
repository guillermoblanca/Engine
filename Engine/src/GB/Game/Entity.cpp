#include "GB/Game/Entity.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace GB
{
	glm::mat4 TransformComponent::ToMatrix() const
	{
		const glm::quat q(glm::radians(Rotation));
		return glm::translate(glm::mat4(1.0f), Position) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), Scale);
	}

	void TransformComponent::FromMatrix(const glm::mat4& m)
	{
		Position = glm::vec3(m[3]);
		glm::vec3 axes[3] = { glm::vec3(m[0]), glm::vec3(m[1]), glm::vec3(m[2]) };
		Scale = glm::vec3(glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]));
		glm::mat3 r;
		for (int i = 0; i < 3; ++i)
			r[i] = Scale[i] > 1e-8f ? axes[i] / Scale[i] : glm::vec3(0.0f);
		// R = Rz(roll) * Ry(yaw) * Rx(pitch), the same order as ToMatrix(); atan2 keeps it accurate near yaw = +-90.
		const float cosYaw = std::hypot(r[0][0], r[0][1]);
		float pitch, roll;
		if (cosYaw > 1e-6f)
		{
			pitch = std::atan2(r[1][2], r[2][2]);
			roll = std::atan2(r[0][1], r[0][0]);
		}
		else   // gimbal lock: only pitch -/+ roll is defined; put it all in pitch
		{
			pitch = std::atan2(-r[2][1], r[1][1]);
			roll = 0.0f;
		}
		Rotation = glm::degrees(glm::vec3(pitch, std::atan2(-r[0][2], cosYaw), roll));
	}

	bool Entity::HasTag(const std::string& tag) const
	{
		return std::find(Tags.begin(), Tags.end(), tag) != Tags.end();
	}

	std::string EntityWorld::UniqueName(const std::string& wanted, EntityId ignore) const
	{
		const std::string base = wanted.empty() ? "Entity" : wanted;
		auto taken = [&](const std::string& n)
		{
			for (const auto& [id, e] : m_Entities)
				if (id != ignore && e.Name == n) return true;
			return false;
		};
		if (!taken(base)) return base;
		for (int i = 2;; ++i)
		{
			std::string n = base + " " + std::to_string(i);
			if (!taken(n)) return n;
		}
	}

	Entity* EntityWorld::Insert(EntityId id, const std::string& name)
	{
		if (id == NullEntity || m_Entities.count(id)) return nullptr;
		Entity e;
		e.Id = id;
		e.Name = UniqueName(name, NullEntity);
		m_NextId = std::max(m_NextId, id + 1);
		return &m_Entities.emplace(id, std::move(e)).first->second;
	}

	bool EntityWorld::Link(EntityId id, EntityId parent)
	{
		Entity* e = Find(id);
		if (!e || e->Parent != NullEntity) return false;
		if (parent == NullEntity) return true;
		Entity* p = Find(parent);
		if (!p || parent == id || IsDescendant(parent, id)) return false;
		e->Parent = parent;
		p->Children.push_back(id);
		return true;
	}

	EntityId EntityWorld::Create(const std::string& name, EntityId parent)
	{
		if (parent != NullEntity && !Find(parent)) return NullEntity;
		const EntityId id = m_NextId;
		Insert(id, name);
		Link(id, parent);
		return id;
	}

	Entity* EntityWorld::Find(EntityId id)
	{
		auto it = m_Entities.find(id);
		return it == m_Entities.end() ? nullptr : &it->second;
	}

	const Entity* EntityWorld::Find(EntityId id) const
	{
		auto it = m_Entities.find(id);
		return it == m_Entities.end() ? nullptr : &it->second;
	}

	EntityId EntityWorld::FindByName(const std::string& name) const
	{
		// Systems look entities up by name every frame; the scan is linear in the scene (thousands of trees), so the last answer is kept and
		// verified (the entity must still exist and still carry the name).
		if (const auto hit = m_NameCache.find(name); hit != m_NameCache.end())
		{
			const auto e = m_Entities.find(hit->second);
			if (e != m_Entities.end() && e->second.Name == name) return hit->second;
		}
		for (const auto& [id, e] : m_Entities)
			if (e.Name == name)
			{
				if (m_NameCache.size() > 2 * m_Entities.size() + 64) m_NameCache.clear();
				m_NameCache[name] = id;
				return id;
			}
		return NullEntity;
	}

	std::vector<EntityId> EntityWorld::FindByTag(const std::string& tag) const
	{
		std::vector<EntityId> out;
		for (const auto& [id, e] : m_Entities)
			if (e.HasTag(tag)) out.push_back(id);
		return out;
	}

	bool EntityWorld::Destroy(EntityId id)
	{
		Entity* e = Find(id);
		if (!e) return false;
		const std::vector<EntityId> children = e->Children;   // the map entry goes away below
		for (EntityId c : children) Destroy(c);
		if (Entity* p = Find(e->Parent))
			p->Children.erase(std::remove(p->Children.begin(), p->Children.end(), id), p->Children.end());
		m_Entities.erase(id);
		return true;
	}

	EntityId EntityWorld::CopyTree(EntityId src, EntityId parent)
	{
		const Entity original = *Find(src);   // by value: Create() may rebalance the map
		const EntityId id = Create(original.Name, parent);
		Entity* e = Find(id);
		e->Tags = original.Tags;
		e->Active = original.Active;
		e->Transform = original.Transform;
		e->Mesh = original.Mesh;
		e->Light = original.Light;
		e->Body = original.Body;
		e->Prop = original.Prop;
		e->Interact = original.Interact;
		e->Gameplay = original.Gameplay;
		for (EntityId c : original.Children) CopyTree(c, id);
		return id;
	}

	EntityId EntityWorld::Duplicate(EntityId id)
	{
		const Entity* e = Find(id);
		return e ? CopyTree(id, e->Parent) : NullEntity;
	}

	void EntityWorld::Clear()
	{
		m_Entities.clear();
		m_NextId = 1;
	}

	bool EntityWorld::Rename(EntityId id, const std::string& name)
	{
		Entity* e = Find(id);
		if (!e) return false;
		e->Name = UniqueName(name, id);
		return true;
	}

	bool EntityWorld::IsDescendant(EntityId id, EntityId ancestor) const
	{
		for (const Entity* e = Find(id); e; e = Find(e->Parent))
			if (e->Id == ancestor) return true;
		return false;
	}

	bool EntityWorld::SetParent(EntityId id, EntityId parent, bool keepWorld)
	{
		Entity* e = Find(id);
		if (!e || (parent != NullEntity && !Find(parent))) return false;
		if (parent == id || (parent != NullEntity && IsDescendant(parent, id))) return false;
		const glm::mat4 world = WorldMatrix(id);
		if (Entity* old = Find(e->Parent))
			old->Children.erase(std::remove(old->Children.begin(), old->Children.end(), id), old->Children.end());
		e->Parent = parent;
		if (Entity* p = Find(parent)) p->Children.push_back(id);
		if (keepWorld)
			e->Transform.FromMatrix(glm::inverse(WorldMatrix(parent)) * world);
		return true;
	}

	glm::mat4 EntityWorld::WorldMatrix(EntityId id) const
	{
		glm::mat4 m(1.0f);
		for (const Entity* e = Find(id); e; e = Find(e->Parent))
			m = e->Transform.ToMatrix() * m;
		return m;
	}

	bool EntityWorld::IsActive(EntityId id) const
	{
		const Entity* e = Find(id);
		if (!e) return false;
		for (; e; e = Find(e->Parent))
			if (!e->Active) return false;
		return true;
	}

	std::vector<EntityId> EntityWorld::Roots() const
	{
		std::vector<EntityId> out;
		for (const auto& [id, e] : m_Entities)
			if (e.Parent == NullEntity) out.push_back(id);
		return out;
	}
}
