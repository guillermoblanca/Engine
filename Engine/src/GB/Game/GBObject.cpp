#include "GB/Game/GBObject.h"

namespace GB
{
	const char* ObjectStateName(ObjectState state)
	{
		switch (state)
		{
		case ObjectState::Constructing: return "constructing";
		case ObjectState::Active: return "active";
		case ObjectState::Paused: return "paused";
		case ObjectState::Ending: return "ending";
		case ObjectState::Destroyed: return "destroyed";
		}
		return "?";
	}

	GBObject::~GBObject()
	{
		while (!m_Components.empty()) m_Components.pop_back();
	}

	void GBObject::OnSpawnStart(GBObjectContext& context)
	{
		if (m_State != ObjectState::Constructing || m_SpawnStarted) return;
		m_SpawnStarted = true;
		for (size_t i = 0; i < m_Components.size(); ++i) m_Components[i]->OnSpawnStart(*this, context);   // (index: a component may add another)
	}

	void GBObject::OnSpawnEnd()
	{
		if (m_State != ObjectState::Constructing || !m_SpawnStarted || m_SpawnEnded) return;
		m_SpawnEnded = true;
		for (size_t i = 0; i < m_Components.size(); ++i) m_Components[i]->OnSpawnEnd(*this);
	}

	void GBObject::BeginPlay()
	{
		if (m_State != ObjectState::Constructing || !m_SpawnEnded) return;
		m_State = ObjectState::Active;
		for (size_t i = 0; i < m_Components.size(); ++i) m_Components[i]->BeginPlay(*this);
	}

	void GBObject::Update(float dt)
	{
		if (m_State != ObjectState::Active) return;
		for (size_t i = 0; i < m_Components.size(); ++i) m_Components[i]->Update(*this, dt);
	}

	void GBObject::OnPause()
	{
		if (m_State != ObjectState::Active) return;
		m_State = ObjectState::Paused;
		for (size_t i = 0; i < m_Components.size(); ++i) m_Components[i]->OnPause(*this);
	}

	void GBObject::OnResume()
	{
		if (m_State != ObjectState::Paused) return;
		m_State = ObjectState::Active;
		for (size_t i = 0; i < m_Components.size(); ++i) m_Components[i]->OnResume(*this);
	}

	void GBObject::EndPlay()
	{
		if (!Alive()) return;
		m_State = ObjectState::Ending;
		for (size_t i = m_Components.size(); i-- > 0;) m_Components[i]->EndPlay(*this);
	}

	void GBObject::OnDestroy()
	{
		if (m_State == ObjectState::Destroyed) return;
		if (Alive()) EndPlay();   // never destroyed while still playing
		m_State = ObjectState::Destroyed;
		for (size_t i = m_Components.size(); i-- > 0;) m_Components[i]->OnDestroy(*this);
		while (!m_Components.empty()) m_Components.pop_back();   // reverse order of creation
	}
}
