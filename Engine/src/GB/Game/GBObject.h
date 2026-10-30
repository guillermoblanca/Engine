#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace GB
{
	// The minimum instance of a world: a name, a place and a life cycle. It has no model, no scene and no controller; whatever it does it does
	// through its components. Actor builds on it (gameplay), the geometry is one more optional component. See docs/CICLO_VIDA_ACTOR_ASSET.md.
	struct ObjectTransform
	{
		glm::vec3 Position{ 0.0f };
		float Yaw = 0.0f;       // radians around +Y
		float Scale = 1.0f;
	};

	enum class ObjectState : uint8_t
	{
		Constructing,   // made, not in the world yet (OnSpawnStart / OnSpawnEnd)
		Active,         // in the world and ticking (BeginPlay done)
		Paused,         // in the world, kept, not ticking (OnPause done)
		Ending,         // EndPlay done, about to be destroyed
		Destroyed,      // OnDestroy done
	};
	const char* ObjectStateName(ObjectState state);

	class GBObject;

	// What the world gives an object while it is built. Everything is optional and borrowed: it outlives the object or is null.
	struct GBObjectContext
	{
		void* World = nullptr;   // the owning world (ObjectWorld); typed by the world that fills it
	};

	// One piece of behaviour of an object. The phases are the object's (GBObject::BeginPlay calls each component's); a component never calls
	// them on itself. All are optional.
	class GBObjectComponent
	{
	public:
		virtual ~GBObjectComponent() = default;
		virtual void OnSpawnStart(GBObject&, GBObjectContext&) {}
		virtual void OnSpawnEnd(GBObject&) {}
		virtual void BeginPlay(GBObject&) {}
		virtual void Update(GBObject&, float /*dt*/) {}
		virtual void OnPause(GBObject&) {}
		virtual void OnResume(GBObject&) {}
		virtual void EndPlay(GBObject&) {}
		virtual void OnDestroy(GBObject&) {}
	};

	// Not copyable or movable: other systems keep its address while it lives. Phases are called by the world, in order, never from a
	// constructor or destructor; a call out of order (BeginPlay twice, Update while paused) is ignored. Components start in the order they
	// were added and end in the reverse one.
	class GBObject
	{
	public:
		explicit GBObject(std::string name) : m_Name(std::move(name)) {}
		GBObject(const GBObject&) = delete;
		GBObject& operator=(const GBObject&) = delete;
		virtual ~GBObject();   // frees the components in reverse order; emits no phase (the world calls OnDestroy first)

		const std::string& Name() const { return m_Name; }
		void Rename(std::string name) { if (m_State == ObjectState::Constructing) m_Name = std::move(name); }   // only while it is being built
		uint32_t Handle() const { return m_Handle; }          // 0 until the world publishes it; never reused by that world
		void SetHandle(uint32_t handle) { m_Handle = handle; }
		const std::string& Kind() const { return m_Kind; }    // what the caller asked for: saved so a load can make it again
		void SetKind(std::string kind) { m_Kind = std::move(kind); }
		ObjectState State() const { return m_State; }
		bool Alive() const { return m_State == ObjectState::Active || m_State == ObjectState::Paused; }

		ObjectTransform Transform;

		// Only while Constructing. Returns the component to configure it.
		template <class T, class... Args> T& AddComponent(Args&&... args)
		{
			auto c = std::make_unique<T>(std::forward<Args>(args)...);
			T& ref = *c;
			m_Components.push_back(std::move(c));
			return ref;
		}
		// A component built before it is known to work (a presentation whose model may fail) joins the object only once it is good.
		template <class T> T& Adopt(std::unique_ptr<T> component)
		{
			T& ref = *component;
			m_Components.push_back(std::move(component));
			return ref;
		}
		template <class T> T* Find() const
		{
			for (const auto& c : m_Components)
				if (T* t = dynamic_cast<T*>(c.get())) return t;
			return nullptr;
		}
		// Calls `fn` with every component that implements T (an interface such as IStateSaver), in the order they were added.
		template <class T, class F> void ForEach(F&& fn) const
		{
			for (const auto& c : m_Components)
				if (T* t = dynamic_cast<T*>(c.get())) fn(*t);
		}
		size_t ComponentCount() const { return m_Components.size(); }

		// ---- the life cycle (virtual: Actor adds its own steps around the components')
		virtual void OnSpawnStart(GBObjectContext& context);   // Constructing; components in order
		virtual void OnSpawnEnd();                             // Constructing; the world publishes the object right after
		virtual void BeginPlay();                              // Constructing -> Active
		virtual void Update(float dt);                         // Active only
		virtual void OnPause();                                // Active -> Paused, once
		virtual void OnResume();                               // Paused -> Active, once
		virtual void EndPlay();                                // Active/Paused -> Ending; components in reverse
		virtual void OnDestroy();                              // Ending -> Destroyed; components in reverse, then released. Idempotent.

	private:
		std::string m_Name, m_Kind;
		uint32_t m_Handle = 0;
		ObjectState m_State = ObjectState::Constructing;
		bool m_SpawnStarted = false, m_SpawnEnded = false;
		std::vector<std::unique_ptr<GBObjectComponent>> m_Components;
	};
}
