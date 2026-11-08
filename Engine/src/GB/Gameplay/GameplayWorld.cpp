#include "GB/Gameplay/GameplayWorld.h"
#include <cstdio>

namespace GB
{
	AbilitySystemComponent* GameplayWorld::Create(const std::string& name)
	{
		auto& slot = m_Systems[name];
		slot = std::make_unique<AbilitySystemComponent>(name, Library);
		m_Revision = NextRevision();
		return slot.get();
	}

	AbilitySystemComponent* GameplayWorld::Find(const std::string& name)
	{
		auto it = m_Systems.find(name);
		return it == m_Systems.end() ? nullptr : it->second.get();
	}

	const AbilitySystemComponent* GameplayWorld::Find(const std::string& name) const
	{
		auto it = m_Systems.find(name);
		return it == m_Systems.end() ? nullptr : it->second.get();
	}

	bool GameplayWorld::Remove(const std::string& name)
	{
		if (m_Systems.erase(name) == 0) return false;
		m_Revision = NextRevision();
		return true;
	}

	std::vector<std::string> GameplayWorld::Names() const
	{
		std::vector<std::string> out;
		for (const auto& [name, asc] : m_Systems) out.push_back(name);
		return out;
	}

	const std::vector<GameplayWorld::Tagged>& GameplayWorld::WithTag(const std::string& tag) const
	{
		auto& [revision, list] = m_Tagged[tag];
		if (revision != m_Revision)   // (0, never listed, is no revision)
		{
			list.clear();
			for (const auto& [name, system] : m_Systems)
				if (system && system->HasTag(tag)) list.push_back({ &name, system.get() });
			revision = m_Revision;
		}
		return list;
	}

	int GameplayWorld::Load(const EntityWorld& world)
	{
		m_Systems.clear();
		m_Revision = NextRevision();
		int n = 0;
		for (const auto& [id, e] : world.All())
		{
			if (!e.Gameplay) continue;
			Create(e.Name)->FromComponent(*e.Gameplay);
			++n;
		}
		return n;
	}

	int GameplayWorld::Store(EntityWorld& world) const
	{
		int n = 0;
		for (const auto& [name, asc] : m_Systems)
		{
			Entity* e = world.Find(world.FindByName(name));
			if (!e) continue;
			e->Gameplay = asc->ToComponent();
			++n;
		}
		return n;
	}

	void GameplayWorld::Update(float dt)
	{
		for (auto& [name, asc] : m_Systems) asc->Update(dt);
	}

	std::string GameplayWorld::Describe(const std::string& name) const
	{
		const AbilitySystemComponent* a = Find(name);
		if (!a) return "no ability system called '" + name + "'";
		std::string s = name + ":";
		for (const AttributeSet::Attribute& at : a->Attributes().All())
		{
			char b[80];
			std::snprintf(b, sizeof b, " %s %.4g/%.4g", at.Name.c_str(), static_cast<double>(at.Current), static_cast<double>(at.Max));
			s += b;
		}
		return s;
	}
}
