#pragma once
// T343: the trees and rocks of the scenery as the clans see them. They are the world's own placements (GeneratedWorld::Trees and Rocks), not
// resource nodes: the editor has them as entities "Tree N" and "Rock N" (SceneEditor::PopulateEnvironment, in the order of the placements) and
// GBSim only as the walls of its ground (SimRun: one circle per placement, in the same order). Nobody gathers from them; a farm zone painted over
// one turns it into a node of the data where it stood (FarmZones: the zone's first work, then cleared away as any node in it).
//
// The host gives the tribe (Tribe::SetScenery) two functions over its own copy:
//   Around  the props whose foot is inside a box (x, z), with the name both hosts give them ("Tree 12" is the 12th tree placement)
//   Remove  takes one away for good: from the scene and from the walls of the navigation (the editor's grid follows the scene within a second,
//           the save keeps it as a change of the world's baseline; GBSim redoes the cells around it at once). False: there is none of that name.
// With no host (a test, a scene without a world) there is no scenery: nothing changes. CPU only, deterministic (Around's order is the names').
#include "GB/Scene/Entity.h"
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

namespace GB
{
	struct SceneryProp
	{
		std::string Name;             // "Tree 12", "Rock 3"
		PropKind Kind = PropKind::Tree;
		int Index = 0;                // its number in the name (1 = the first placement of its kind)
		int Variant = 0;              // which procedural tree or rock of the world draws it
		glm::vec3 Position{ 0.0f };   // its foot
		float Yaw = 0.0f;             // degrees
		float Scale = 1.0f;
		float Radius = 0.0f;          // the wall it makes for the walkers (0: the host does not know)
	};

	struct SceneryHost
	{
		std::function<void(const glm::vec2& lo, const glm::vec2& hi, std::vector<SceneryProp>& out)> Around;
		std::function<bool(const std::string& name)> Remove;
		explicit operator bool() const { return Around && Remove; }
	};

	// "Tree 12" -> PropKind::Tree and 12; false for any other name.
	bool ParseSceneryName(const std::string& name, PropKind& kind, int& index);
	// The scenery of an entity world as the editor fills it: the entities "Tree N" and "Rock N" with a prop and no ability system nor parts (a
	// resource node drawn with a prop has both). `radius` gives the wall of a prop (PlayerMode::MakeHost's), or none. The world must outlive it.
	SceneryHost EntityScenery(EntityWorld& world, std::function<float(const Entity&, float scale)> radius = {});
}
