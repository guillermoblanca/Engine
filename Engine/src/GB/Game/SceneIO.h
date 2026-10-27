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
	std::string SaveSceneToString(const EntityWorld& world);
	SceneIOResult LoadSceneFromString(EntityWorld& world, const std::string& json);   // replaces the world's content on success
	SceneIOResult SaveSceneToFile(const EntityWorld& world, const std::string& path);
	SceneIOResult LoadSceneFromFile(EntityWorld& world, const std::string& path);
}
