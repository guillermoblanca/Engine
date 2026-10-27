#include "Destruction.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	void DestructionSystem::Add(int body, const DestructibleDesc& desc)
	{
		if (m_World.IsAlive(body))
			m_Destructibles[body] = { desc, 0.0f };
	}

	float DestructionSystem::GetVisualScale(int body) const
	{
		const auto it = m_Debris.find(body);
		if (it == m_Debris.end()) return 1.0f;
		return glm::clamp(it->second.Lifetime - it->second.Age, 0.0f, 1.0f);
	}

	void DestructionSystem::Damage(int body, float impulse, const glm::vec3&)
	{
		auto it = m_Destructibles.find(body);
		if (it == m_Destructibles.end() || !m_World.IsAlive(body)) return;
		Info& info = it->second;
		if (impulse >= info.Desc.Toughness)
			m_PendingBreak.push_back(body), m_PendingImpulse.push_back(impulse);
		else if (info.Desc.Accumulate)
		{
			info.Damage += impulse;
			if (info.Damage >= info.Desc.Toughness) m_PendingBreak.push_back(body), m_PendingImpulse.push_back(info.Damage);
		}
	}

	void DestructionSystem::Explode(const glm::vec3& center, float radius, float speedAtCenter)
	{
		for (const auto& hit : m_World.Explode(center, radius, speedAtCenter))
			Damage(hit.Body, hit.Impulse, center);
	}

	void DestructionSystem::Break(int body, float impulse)
	{
		auto it = m_Destructibles.find(body);
		if (it == m_Destructibles.end() || !m_World.IsAlive(body)) return;
		const DestructibleDesc desc = it->second.Desc;
		const RigidBody parent = m_World.GetBody(body);   // copy: the body is removed below
		m_Destructibles.erase(it);

		std::vector<std::pair<glm::vec3, BodyDesc>> pieces;   // local offset + description
		if (parent.Desc.Shape == ShapeType::Box)
		{
			const glm::ivec3 n = glm::max(desc.Split, glm::ivec3(1));
			const glm::vec3 he = parent.Desc.HalfExtents / glm::vec3(n);
			for (int x = 0; x < n.x; ++x)
				for (int y = 0; y < n.y; ++y)
					for (int z = 0; z < n.z; ++z)
					{
						BodyDesc d = parent.Desc;
						d.HalfExtents = he * 0.96f;   // a hair of space so the pieces do not start interpenetrating
						pieces.push_back({ (glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) + 0.5f) * 2.0f * he - parent.Desc.HalfExtents, d });
					}
		}
		else
		{
			const float r = parent.Desc.Radius * 0.55f;
			const glm::vec3 offsets[4] = { { 1, 1, 1 }, { 1, -1, -1 }, { -1, 1, -1 }, { -1, -1, 1 } };
			for (const glm::vec3& o : offsets)
			{
				BodyDesc d = parent.Desc;
				d.Radius = r;
				pieces.push_back({ glm::normalize(o) * parent.Desc.Radius * 0.45f, d });
			}
		}

		BreakEvent ev;
		ev.Position = parent.Position;
		ev.Size = parent.GetBoundingRadius();
		ev.Impulse = impulse;
		ev.FromBody = body;
		ev.Fragments = static_cast<int>(pieces.size());
		m_World.RemoveBody(body);
		for (const auto& [offset, d] : pieces)
		{
			const glm::vec3 world = parent.Position + parent.Rotation * offset;
			const int id = m_World.AddBody(d, world, parent.Rotation);
			RigidBody& frag = m_World.GetBody(id);
			const glm::vec3 r = world - parent.Position;
			const glm::vec3 out = glm::length(r) > 1e-4f ? glm::normalize(r) : glm::vec3(0, 1, 0);
			frag.Velocity = parent.Velocity + glm::cross(parent.AngularVelocity, r) + out * desc.BlastSpeed * (0.5f + Rand()) + glm::vec3(0.0f, 1.0f + 2.0f * Rand(), 0.0f);
			frag.AngularVelocity = parent.AngularVelocity + glm::vec3(Rand() - 0.5f, Rand() - 0.5f, Rand() - 0.5f) * 8.0f;
			m_Debris[id] = { 0.0f, desc.DebrisLifetime * (0.8f + 0.4f * Rand()) };
			if (desc.FragmentsBreakAgain)
			{
				DestructibleDesc again = desc;
				again.Toughness = desc.FragmentToughness;
				again.FragmentsBreakAgain = false;
				m_Destructibles[id] = { again, 0.0f };
			}
			if (OnFragment)
			{
				const glm::vec3 size = d.Shape == ShapeType::Sphere ? glm::vec3(d.Radius * 2.0f) : d.HalfExtents * 2.0f;
				OnFragment(id, body, size);
			}
		}
		m_Breaks.push_back(ev);
		++m_BrokenTotal;
	}

	void DestructionSystem::Update(float dt, const std::vector<ContactEvent>& contacts)
	{
		for (const ContactEvent& c : contacts)
			if (c.Body >= 0) Damage(c.Body, c.Impulse, c.Position);
		// break after collecting, so one frame's hits never invalidate each other's body indices
		std::vector<int> bodies; bodies.swap(m_PendingBreak);
		std::vector<float> impulses; impulses.swap(m_PendingImpulse);
		for (size_t i = 0; i < bodies.size(); ++i)
			Break(bodies[i], impulses[i]);
		// debris ages and disappears
		for (auto it = m_Debris.begin(); it != m_Debris.end();)
		{
			it->second.Age += dt;
			if (it->second.Age >= it->second.Lifetime)
			{
				m_World.RemoveBody(it->first);
				m_Destructibles.erase(it->first);
				it = m_Debris.erase(it);
			}
			else
				++it;
		}
	}
}
