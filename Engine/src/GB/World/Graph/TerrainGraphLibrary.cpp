#include "GB/World/Graph/TerrainGraphLibrary.h"
#include "GB/World/Graph/TerrainGraphIO.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace GB
{
	namespace fs = std::filesystem;

	namespace
	{
		std::string ReadFile(const fs::path& p, std::string& out)
		{
			std::ifstream f(p, std::ios::binary);
			if (!f) return "cannot read " + p.string();
			std::stringstream ss;
			ss << f.rdbuf();
			out = ss.str();
			return {};
		}

		std::string WriteFile(const fs::path& p, const std::string& text)
		{
			std::error_code ec;
			if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
			std::ofstream f(p, std::ios::binary);
			if (!f) return "cannot write " + p.string();
			f << text;
			return f ? std::string() : "cannot write " + p.string();
		}
	}

	bool TerrainGraphLibrary::ValidId(const std::string& id)
	{
		if (id.empty()) return false;
		return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; });
	}

	TerrainGraphLibrary::TerrainGraphLibrary() : TerrainGraphLibrary(MakeDefaultTerrainNodeRegistry()) {}

	TerrainGraphLibrary::TerrainGraphLibrary(TerrainNodeRegistry registry) : m_Registry(std::move(registry))
	{
		for (const std::string& name : TerrainGraphPresetNames())
		{
			TerrainGraph g;
			if (!MakeTerrainGraphPreset(name, g)) continue;
			if (g.Id.empty()) g.Id = name;
			m_Presets.insert(g.Id);
			m_Graphs[g.Id] = std::move(g);
		}
	}

	std::vector<std::string> TerrainGraphLibrary::Ids() const
	{
		std::vector<std::string> ids;
		for (const auto& [id, g] : m_Graphs) ids.push_back(id);
		return ids;
	}

	const TerrainGraph* TerrainGraphLibrary::Find(const std::string& id) const
	{
		const auto it = m_Graphs.find(id);
		return it == m_Graphs.end() ? nullptr : &it->second;
	}

	std::vector<TgIssue> TerrainGraphLibrary::Validate(const std::string& id) const
	{
		const TerrainGraph* g = Find(id);
		if (!g) return { { true, "", "no graph '" + id + "'" } };
		return ValidateTerrainGraph(*g, m_Registry);
	}

	std::string TerrainGraphLibrary::Add(TerrainGraph graph, bool overwrite)
	{
		if (!ValidId(graph.Id)) return "the id '" + graph.Id + "' is not valid (letters, digits, '_', '-' and '.')";
		if (!overwrite && m_Graphs.count(graph.Id)) return "a graph '" + graph.Id + "' already exists";
		const std::vector<TgIssue> issues = ValidateTerrainGraph(graph, m_Registry);
		if (HasTgErrors(issues)) return "the graph '" + graph.Id + "' has errors:\n" + TgIssuesText(issues);
		const std::string id = graph.Id;
		m_Graphs[id] = std::move(graph);
		return {};
	}

	std::string TerrainGraphLibrary::Remove(const std::string& id)
	{
		if (!m_Graphs.erase(id)) return "no graph '" + id + "'";
		return {};
	}

	std::string TerrainGraphLibrary::Duplicate(const std::string& id, const std::string& newId)
	{
		const TerrainGraph* g = Find(id);
		if (!g) return "no graph '" + id + "'";
		TerrainGraph copy = *g;
		copy.Id = newId;
		return Add(std::move(copy));
	}

	std::string TerrainGraphLibrary::ExportJson(const std::string& id, std::string& json) const
	{
		const TerrainGraph* g = Find(id);
		if (!g) return "no graph '" + id + "'";
		json = TerrainGraphToJson(*g);
		return {};
	}

	std::string TerrainGraphLibrary::ImportJson(const std::string& json, bool overwrite, std::string* importedId)
	{
		TerrainGraph g;
		if (std::string e = TerrainGraphFromJson(json, g); !e.empty()) return e;
		const std::string id = g.Id;
		if (std::string e = Add(std::move(g), overwrite); !e.empty()) return e;
		if (importedId) *importedId = id;
		return {};
	}

	std::string TerrainGraphLibrary::LoadFile(const std::string& path, std::string* loadedId)
	{
		std::string text;
		if (std::string e = ReadFile(path, text); !e.empty()) return e;
		if (std::string e = ImportJson(text, true, loadedId); !e.empty()) return fs::path(path).filename().string() + ": " + e;
		return {};
	}

	std::string TerrainGraphLibrary::LoadDirectory(const std::string& dir)
	{
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) return "no folder " + dir;
		std::vector<fs::path> files;
		for (const auto& entry : fs::directory_iterator(dir, ec))
			if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
		std::sort(files.begin(), files.end());   // (a fixed order: two files with one id, the later name wins every time)
		std::string errors;
		for (const fs::path& p : files)
			if (std::string e = LoadFile(p.string()); !e.empty()) errors += e + "\n";
		return errors;
	}

	std::string TerrainGraphLibrary::SaveFile(const std::string& path, const std::string& id) const
	{
		std::string json;
		if (std::string e = ExportJson(id, json); !e.empty()) return e;
		return WriteFile(path, json);
	}

	std::string TerrainGraphLibrary::SaveGraph(const std::string& dir, const std::string& id) const
	{
		if (!ValidId(id)) return "the id '" + id + "' is not valid";
		return SaveFile((fs::path(dir) / (id + ".json")).string(), id);
	}
}
