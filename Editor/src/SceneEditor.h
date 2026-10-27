#pragma once
#include "Commands.h"
#include "GB/Game/Entity.h"
#include "GB/World/WorldGen.h"
#include <functional>
#include <string>
#include <vector>

namespace GB::Editor
{
	// The editable content of the editor scene: an EntityWorld plus the selection and the operations on it (add,
	// delete, move, pick...). CPU only (no GL), so the text commands and the logic run headless and are unit tested.
	// The generated environment is part of it: every tree and rock is an entity with a PropComponent.
	class SceneEditor
	{
	public:
		// `assetRoot`: model paths are stored relative to it.
		explicit SceneEditor(std::string assetRoot = "") : m_AssetRoot(std::move(assetRoot)) {}

		EntityWorld& World() { return m_World; }
		const EntityWorld& World() const { return m_World; }
		const std::string& AssetRoot() const { return m_AssetRoot; }

		EntityId Selected() const { return m_Selected; }
		void Select(EntityId id) { m_Selected = m_World.Find(id) ? id : NullEntity; }

		// Creation. New entities become the selection.
		EntityId AddPrimitive(MeshShape shape, const glm::vec3& position);
		EntityId AddLight(const glm::vec3& position);
		// `path` is a .glb/.gltf relative to the asset root, or absolute (stored relative when inside it).
		EntityId AddModel(const std::string& path, const glm::vec3& position, std::string* error = nullptr);
		EntityId Duplicate(EntityId id);
		bool Delete(EntityId id);

		// Wipes every entity (and the selection).
		void Clear();
		// Removes the generated environment (entities tagged "world"), keeping what the user added.
		void RemoveEnvironment();
		// One entity per tree / rock, grouped under "Trees" and "Rocks". Returns how many were created.
		size_t PopulateEnvironment(const std::vector<TreePlacement>& trees, const std::vector<RockPlacement>& rocks);

		// "12" -> id 12, otherwise an exact entity name; empty -> the selection. NullEntity when nothing matches.
		EntityId Resolve(const std::string& token) const;
		// Entities (id order) whose name contains `filter` (case-insensitive).
		std::vector<EntityId> Find(const std::string& filter) const;

		// Bounding sphere of an entity in world space (false when it has nothing to pick).
		struct Sphere { glm::vec3 Center{ 0.0f }; float Radius = 0.0f; };
		// Local-space bounds of things only the renderer knows (models, trees, rocks): center and radius.
		using BoundsProvider = std::function<bool(EntityId, const Entity&, glm::vec3& center, float& radius)>;
		void SetBoundsProvider(BoundsProvider p) { m_Bounds = std::move(p); }
		bool WorldBounds(EntityId id, Sphere& out) const;
		// The nearest active entity hit by the ray (NullEntity when none). Groups without geometry are skipped.
		EntityId Pick(const glm::vec3& origin, const glm::vec3& direction) const;

		static std::string Describe(const Entity& e);

	private:
		EntityId Group(const std::string& name);

		std::string m_AssetRoot;
		EntityWorld m_World;
		EntityId m_Selected = NullEntity;
		BoundsProvider m_Bounds;
	};

	// What the scene commands need from the application.
	struct SceneHooks
	{
		std::function<void()> ClearWorld;                           // new_scene: drops the generated world and resets the sky
		std::function<void()> SceneReplaced;                        // entities were replaced wholesale (new scene / load)
		std::function<glm::vec3()> SpawnPoint;                      // where new things appear
		std::function<float(float, float)> Ground;                  // terrain height, may be empty
		std::function<void(const std::string&)> RunScript;          // runs commands before the rest of the queue
		std::function<CommandResult(const std::string&)> RunExternal;   // opens a Sandbox scene in its own window
	};

	// new_scene, add, objects, select, delete, duplicate, move, rotate, scale, color, drop, scene_save, scene_load,
	// tests, test.
	void RegisterSceneCommands(CommandRunner& commands, SceneEditor& editor, SceneHooks hooks);
}
