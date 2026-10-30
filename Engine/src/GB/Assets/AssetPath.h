#pragma once
#include <filesystem>
#include <string>

namespace GB
{
	// The path as given when it exists, else the one under `assetRoot`, else as given (the loader then reports it). One rule for every caller.
	inline std::string ResolveAssetPath(const std::string& path, const std::string& assetRoot)
	{
		namespace fs = std::filesystem;
		if (!fs::exists(path) && fs::exists(assetRoot + "/" + path)) return assetRoot + "/" + path;
		return path;
	}
}
