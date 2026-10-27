#pragma once
#include "GB/World/Terrain.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

namespace GB
{
	enum class ShapeType { Sphere, Box, Capsule };   // dynamic bodies are Sphere or Box; static colliders may be any

	struct BodyDesc
	{
		ShapeType Shape = ShapeType::Box;
		float Radius = 0.5f;                       // Sphere
		glm::vec3 HalfExtents{ 0.5f };             // Box
		float Density = 600.0f;                    // kg/m^3 (wood 600, stone 2500): mass = density * volume, < 1000 floats
		float Restitution = 0.3f;
		float Friction = 0.6f;
		float LinearDamping = 0.02f;               // 1/s
		float AngularDamping = 0.08f;
		void* UserData = nullptr;
	};

	class RigidBody
	{
	public:
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1, 0, 0, 0 };
		glm::vec3 Velocity{ 0.0f };
		glm::vec3 AngularVelocity{ 0.0f };         // rad/s, world space
		BodyDesc Desc;
		float Mass = 1.0f;
		bool Sleeping = false;
		float Submersion = 0.0f;                   // 0..1 of the body under water (last step)

		glm::mat4 GetTransform() const;            // translation * rotation (unit scale)
		float GetSpeed() const { return glm::length(Velocity); }
		void AddImpulse(const glm::vec3& impulse, const glm::vec3& worldPoint);
		void Wake() { Sleeping = false; m_Still = 0.0f; }
		float GetBoundingRadius() const;

	private:
		friend class PhysicsWorld;
		float m_Still = 0.0f;
		glm::vec3 m_InvInertia{ 1.0f };            // body space
		float m_InvMass = 1.0f;
		bool m_WasWet = false;
	};

	struct StaticCollider
	{
		ShapeType Shape = ShapeType::Box;
		glm::vec3 Center{ 0.0f };                  // Capsule: the middle of the axis
		float Radius = 0.5f;                       // Sphere, Capsule
		float HalfHeight = 1.0f;                   // Capsule: half the length of the axis segment (vertical)
		glm::vec3 HalfExtents{ 0.5f };             // Box
		glm::quat Rotation{ 1, 0, 0, 0 };          // Box
	};

	// A rotating column of air that carries light bodies (tornado, whirlwind). Accelerations come from drag against
	// the wind it generates, so heavy things resist and light things fly.
	struct Vortex
	{
		glm::vec3 Position{ 0.0f };                // base, on the ground
		float Radius = 10.0f;                      // core radius: peak tangential speed
		float Height = 60.0f;
		float TangentialSpeed = 38.0f;             // m/s at the core
		float InwardSpeed = 14.0f;                 // m/s toward the axis (pulls things in)
		float UpdraftSpeed = 26.0f;                // m/s upward inside the core
		float Grip = 5.0f;                         // multiplies the air drag inside the vortex (turbulence loft debris far better than still air)
	};

	struct ContactEvent
	{
		enum class Kind { Ground, Static, Body } What = Kind::Ground;
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0, 1, 0 };
		float Impulse = 0.0f;                      // N s along the normal
		int Body = -1;
	};

	struct SplashEvent
	{
		glm::vec3 Position{ 0.0f };                // on the water surface
		float Speed = 0.0f;                        // m/s through the surface (negative = rising out of it)
		float Radius = 0.0f;                       // body size
		int Body = -1;
	};

	struct RayHit
	{
		bool Hit = false;
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0, 1, 0 };
		float Distance = 0.0f;
		int Body = -1;                             // dynamic body index, -1 for terrain/static
	};

	// Rigid bodies (spheres and boxes) with terrain, static colliders, buoyancy, wind and vortices.
	// Deterministic fixed-step simulation; runs headless.
	class PhysicsWorld
	{
	public:
		PhysicsWorld() = default;

		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
		glm::vec3 Wind{ 0.0f };                    // m/s, air drag pushes bodies toward it
		float FixedStep = 1.0f / 120.0f;
		float AirDensity = 1.2f;
		float SleepSpeed = 0.12f;
		float SleepTime = 0.6f;
		std::vector<Vortex> Vortices;              // set every frame by the owner of the tornado

		// Ground: a heightmap (not owned, must outlive the world), or any height/normal functions.
		void SetTerrain(const Heightmap* heightmap);
		void SetGround(std::function<float(float, float)> height, std::function<glm::vec3(float, float)> normal);
		// Water: surface height at (x, z), or -infinity where there is none. Enables buoyancy.
		void SetWater(std::function<float(float, float)> surfaceHeight) { m_Water = std::move(surfaceHeight); }

		int AddBody(const BodyDesc& desc, const glm::vec3& position, const glm::quat& rotation = glm::quat(1, 0, 0, 0));
		RigidBody& GetBody(int index) { return *m_Bodies[static_cast<size_t>(index)]; }
		const RigidBody& GetBody(int index) const { return *m_Bodies[static_cast<size_t>(index)]; }
		size_t GetBodyCount() const { return m_Bodies.size(); }
		void RemoveBody(int index);                // leaves a hole (the index stays valid for the others); see IsAlive
		bool IsAlive(int index) const { return index >= 0 && static_cast<size_t>(index) < m_Bodies.size() && m_Bodies[static_cast<size_t>(index)]; }
		void AddStatic(const StaticCollider& collider) { m_Statics.push_back(collider); }
		size_t GetStaticCount() const { return m_Statics.size(); }

		void Step(float dt);                       // accumulates and runs fixed sub-steps

		RayHit Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;
		float GroundHeight(float x, float z) const { return m_GroundHeight ? m_GroundHeight(x, z) : 0.0f; }

		// Events since the last call (hard impacts, bodies entering/leaving water).
		std::vector<ContactEvent> TakeContacts() { return std::move(m_ContactEvents); }
		std::vector<SplashEvent> TakeSplashes() { return std::move(m_Splashes); }
		float MinImpactImpulse = 2.0f;             // contact events below this are not reported

	private:
		struct Contact
		{
			int A = -1, B = -1;                    // dynamic indices; B = -1 means static/ground
			glm::vec3 Point{ 0.0f }, Normal{ 0, 1, 0 };   // normal pushes A away from B
			float Penetration = 0.0f;
			ContactEvent::Kind Kind = ContactEvent::Kind::Ground;
		};
		void SubStep(float dt);
		void ApplyForces(RigidBody& b, int index, float dt);
		void GenerateContacts();
		void Solve(float dt);

		std::vector<std::unique_ptr<RigidBody>> m_Bodies;
		std::vector<StaticCollider> m_Statics;
		std::function<float(float, float)> m_GroundHeight;
		std::function<glm::vec3(float, float)> m_GroundNormal;
		std::function<float(float, float)> m_Water;
		std::vector<Contact> m_Contacts;
		std::vector<ContactEvent> m_ContactEvents;
		std::vector<SplashEvent> m_Splashes;
		float m_Accumulator = 0.0f;
	};
}
