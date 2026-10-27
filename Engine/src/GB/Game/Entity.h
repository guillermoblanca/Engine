#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace GB
{
	using EntityId = uint32_t;
	constexpr EntityId NullEntity = 0;

	// Local transform; rotation is Euler angles in degrees (pitch, yaw, roll).
	struct TransformComponent
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Rotation{ 0.0f };
		glm::vec3 Scale{ 1.0f };
		glm::mat4 ToMatrix() const;
		// Decomposes a matrix (no shear) into this transform.
		void FromMatrix(const glm::mat4& m);
	};

	enum class MeshShape { Cube, Sphere, Plane };

	// Something to draw: a primitive, or a glTF model by path (relative to the asset directory).
	struct MeshComponent
	{
		MeshShape Shape = MeshShape::Cube;
		float Size = 1.0f;               // edge / diameter / side, before the entity scale
		std::string Model;               // when not empty, the model replaces the primitive
		glm::vec3 Albedo{ 0.8f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		glm::vec3 Emissive{ 0.0f };
	};

	struct LightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 20.0f;
		float Radius = 0.0f;             // > 0: fades to zero at this distance
	};

	enum class BodyType { Static, Dynamic };
	enum class ColliderShape { Box, Sphere, Capsule };

	struct BodyComponent
	{
		BodyType Type = BodyType::Dynamic;
		ColliderShape Shape = ColliderShape::Box;
		glm::vec3 HalfExtents{ 0.5f };   // Box; x = radius for Sphere/Capsule, y = half height for Capsule
		float Mass = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.1f;
	};

	// A piece of generated world (tree or rock) the host draws itself: `Variant` indexes the host's recipes.
	// Lets the editor move, rotate, scale and delete the environment like any other entity.
	enum class PropKind { Tree, Rock };

	struct PropComponent
	{
		PropKind Kind = PropKind::Tree;
		int Variant = 0;
	};

	struct Entity
	{
		EntityId Id = NullEntity;
		std::string Name;
		EntityId Parent = NullEntity;
		std::vector<EntityId> Children;
		std::vector<std::string> Tags;
		bool Active = true;              // inactive entities (and their children) are not built into the scene
		TransformComponent Transform;
		std::optional<MeshComponent> Mesh;
		std::optional<LightComponent> Light;
		std::optional<BodyComponent> Body;
		std::optional<PropComponent> Prop;

		bool HasTag(const std::string& tag) const;
	};

	// The objects of a game scene: named entities in a hierarchy, each with a few optional components.
	// Pure data (no GL), so it runs headless; see SceneIO.h for the JSON form and EntityRender.h for drawing.
	class EntityWorld
	{
	public:
		// Names are unique: a clash gets a numeric suffix ("Cube" -> "Cube 2").
		EntityId Create(const std::string& name = "Entity", EntityId parent = NullEntity);
		// Destroys the entity and all its descendants.
		bool Destroy(EntityId id);
		// Deep copy (with descendants) under the same parent. Returns the new root, or NullEntity.
		EntityId Duplicate(EntityId id);
		void Clear();

		Entity* Find(EntityId id);
		const Entity* Find(EntityId id) const;
		EntityId FindByName(const std::string& name) const;
		std::vector<EntityId> FindByTag(const std::string& tag) const;

		bool Rename(EntityId id, const std::string& name);
		// Reparents; `keepWorld` preserves the world transform. Fails (false) for cycles and unknown ids.
		bool SetParent(EntityId id, EntityId parent, bool keepWorld = true);

		glm::mat4 WorldMatrix(EntityId id) const;
		// False when the entity or any ancestor is inactive.
		bool IsActive(EntityId id) const;

		std::vector<EntityId> Roots() const;
		size_t Count() const { return m_Entities.size(); }
		// Every entity, parents before children is NOT guaranteed; ordered by id.
		const std::map<EntityId, Entity>& All() const { return m_Entities; }

		// Used by loaders: inserts an entity with a given id (no hierarchy links; call Link afterwards).
		Entity* Insert(EntityId id, const std::string& name);
		bool Link(EntityId id, EntityId parent);

	private:
		std::string UniqueName(const std::string& wanted, EntityId ignore) const;
		bool IsDescendant(EntityId id, EntityId ancestor) const;
		EntityId CopyTree(EntityId src, EntityId parent);

		std::map<EntityId, Entity> m_Entities;
		EntityId m_NextId = 1;
	};
}
