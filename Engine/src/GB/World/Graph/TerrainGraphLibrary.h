#pragma once
#include "GB/World/Graph/TerrainGraph.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GB
{
	// The library of terrain graphs (docs/GRAFO_TERRENO.md, section 10.5), modelled on PriorityLibrary (GB/AI/PriorityLibrary.h): the presets
	// that ship with the engine (TerrainGraphPresets.h) plus the graphs read from a folder, one JSON per graph (assets/data/terrain_graphs,
	// TerrainGraphIO.h). Headless: the editor's panel and commands and the tests call the same functions. Every function that can fail returns why
	// ("" = done) and changes nothing on a failure.
	class TerrainGraphLibrary
	{
	public:
		TerrainGraphLibrary();   // with every preset (TerrainGraphPresetNames), validated against the default node registry
		explicit TerrainGraphLibrary(TerrainNodeRegistry registry);

		const TerrainNodeRegistry& Registry() const { return m_Registry; }

		// ---- graphs
		std::vector<std::string> Ids() const;
		const TerrainGraph* Find(const std::string& id) const;
		bool IsPreset(const std::string& id) const { return m_Presets.count(id) > 0; }   // its id is a preset's (it may have been replaced since)
		std::string Add(TerrainGraph graph, bool overwrite = false);   // refused when it has errors (warnings do not stop it)
		std::string Remove(const std::string& id);
		std::string Duplicate(const std::string& id, const std::string& newId);
		std::string ExportJson(const std::string& id, std::string& json) const;
		std::string ImportJson(const std::string& json, bool overwrite = false, std::string* importedId = nullptr);
		std::vector<TgIssue> Validate(const std::string& id) const;

		// ---- files: one <id>.json per graph in a folder
		std::string LoadDirectory(const std::string& dir);            // adds/replaces the graphs it reads; a bad file is reported and skipped
		std::string LoadFile(const std::string& path, std::string* loadedId = nullptr);   // one file (any name), added or replaced
		std::string SaveGraph(const std::string& dir, const std::string& id) const;     // <dir>/<id>.json
		std::string SaveFile(const std::string& path, const std::string& id) const;

		static bool ValidId(const std::string& id);   // letters, digits, '_', '-' and '.': it is also the file name

	private:
		TerrainNodeRegistry m_Registry;
		std::map<std::string, TerrainGraph> m_Graphs;
		std::set<std::string> m_Presets;
	};
}
