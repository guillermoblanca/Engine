#pragma once
#include "GB/Game/Entity.h"
#include <string>

namespace GB
{
	struct SceneIOResult
	{
		bool Ok = true;
		std::string Error;
	};

	// Scenes as JSON text ("version": 1): readable, hand/agent-editable and friendly to diffs. Ids are kept.
	// Exact: a float is written as the shortest text that reads back as the same float, so a scene comes back bit for bit (what the fingerprints of a replay need).
	// Short7: 7 significant digits, as every file was written before; only the identity of a generated world (GB/Save) still uses it, so old saves keep matching.
	enum class SceneFloats { Exact, Short7 };
	std::string SaveSceneToString(const EntityWorld& world, SceneFloats floats = SceneFloats::Exact);
	SceneIOResult LoadSceneFromString(EntityWorld& world, const std::string& json);   // replaces the world's content on success
	SceneIOResult SaveSceneToFile(const EntityWorld& world, const std::string& path);
	SceneIOResult LoadSceneFromFile(EntityWorld& world, const std::string& path);
}
