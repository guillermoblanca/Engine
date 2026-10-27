#pragma once
#include "Commands.h"
#include "Properties.h"
#include "GB/Game/Entity.h"
#include "GB/Renderer/Camera.h"
#include <string>
#include <vector>

namespace GB::Editor
{
	struct Ray
	{
		glm::vec3 Origin{ 0.0f };
		glm::vec3 Dir{ 0.0f, 0.0f, -1.0f };
	};

	// Ray through a point of the viewport (`mouse` in 0..1, origin top-left).
	Ray ScreenRay(const Camera& camera, glm::vec2 mouse, float aspect);
	// Nearest hit with the entity's bounds (primitives: their exact shape; models and lights: a sphere). `t` is in units of ray.Dir.
	bool RayHitEntity(const EntityWorld& world, EntityId id, const Ray& ray, float& t);
	// Parameter along the axis (origin + t * dir) closest to the ray: the basis of dragging along a gizmo axis.
	float RayAxisParam(const Ray& ray, const glm::vec3& axisOrigin, const glm::vec3& axisDir);
	// Screen position (pixels, origin top-left) of a world point; false when it is behind the camera.
	bool ProjectToScreen(const glm::mat4& viewProj, const glm::vec3& p, glm::vec2 viewportSize, glm::vec2& out);

	// The scene being edited: an EntityWorld plus selection, undo/redo and the commands that change it. No GL and no
	// ImGui, so scripts, agents and tests drive exactly what the panels do.
	class SceneEditor
	{
	public:
		SceneEditor();

		EntityWorld& World() { return m_World; }
		const EntityWorld& World() const { return m_World; }

		// Where new entities appear (the editor keeps it at the camera's focus point).
		void SetSpawnPoint(const glm::vec3& p) { m_Spawn = p; }
		EntityId Selected() const { return m_Selected; }
		void Select(EntityId id);
		EntityId Pick(const Ray& ray) const;
		EntityId Resolve(const std::string& token) const;   // "12", a name, or "sel"

		// Undo works on whole-scene snapshots (JSON). Commit records the current state when it changed.
		void Commit();
		void MarkDirty() { m_Dirty = true; }                 // a property was edited; committed once nothing is being dragged
		void Tick(bool interacting) { if (m_Dirty && !interacting) { m_Dirty = false; Commit(); } }
		bool Undo();
		bool Redo();
		size_t UndoDepth() const { return m_Undo.size(); }
		size_t RedoDepth() const { return m_Redo.size(); }

		// Exposes the selected entity as "sel.*" properties (rebuilt whenever the selection or its components change).
		void Bind(PropertyRegistry& props);
		void Register(CommandRunner& commands);

		CommandResult NewScene();
		CommandResult Save(const std::string& path);
		CommandResult Load(const std::string& path);
		std::string Describe(EntityId id) const;

	private:
		CommandResult EntityCommand(const std::vector<std::string>& args);
		void Rebind();
		void Restore(const std::string& snapshot);

		EntityWorld m_World;
		EntityId m_Selected = NullEntity;
		PropertyRegistry* m_Props = nullptr;
		std::string m_Current;
		std::vector<std::string> m_Undo, m_Redo;
		bool m_Dirty = false;
		glm::vec3 m_Spawn{ 0.0f };
	};
}
