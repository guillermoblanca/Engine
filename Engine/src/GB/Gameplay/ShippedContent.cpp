#include "GB/Gameplay/ShippedContent.h"
#include "GB/Gameplay/ContentPack.h"
#include "GB/Log.h"
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef GB_CONTENT_DIR
#define GB_CONTENT_DIR "assets/data"
#endif

namespace GB
{
	namespace
	{
		std::string& Dir()
		{
			static std::string dir = std::filesystem::exists(GB_CONTENT_DIR) ? std::string(GB_CONTENT_DIR) : std::string("assets/data");
			return dir;
		}

		ShippedCatalog Load()
		{
			ShippedCatalog c;
			ContentPacks packs;
			packs.AddSection(ItemContentSection(c.Items, c.Recipes, c.Nodes, &c.Techs, &c.Buildings));
			packs.AddSection(StagedSection("interactives", { "interactives" },
				[](const std::string& json, std::map<std::string, InteractiveDef>& defs) { return LoadInteractives(json, defs); }, nullptr, c.Interactives));
			packs.AddSection(StagedSection("species", { "species" },
				[](const std::string& json, std::map<std::string, SpeciesDef>& species) { return LoadSpecies(json, species); }, nullptr, c.Species));
			packs.AddSection(StagedSection("nature", { "nature" },
				[](const std::string& json, NatureConfig& nature) { return LoadNatureSection(json, nature); }, nullptr, c.Nature));
			ContentReport report;
			c.Error = packs.Load(GamePack(), Dir(), &report);
			if (!c.Error.empty()) GB_CORE_ERROR("the game's content ({} in {}) did not load: {}", GamePack(), Dir(), c.Error);
			c.Problems = report.Problems;
			for (std::string& p : UnplacedNodes(c.Nodes, c.Nature)) c.Problems.push_back(std::move(p));
			return c;
		}
	}

	void SetContentDir(const std::string& dir) { Dir() = dir; }
	const std::string& ContentDir() { return Dir(); }
	const char* GamePack() { return "era1"; }

	const std::vector<std::string>& ShippedDocuments()
	{
		static const std::vector<std::string> docs = []
		{
			std::vector<std::string> out;
			const std::filesystem::path manifest = std::filesystem::path(Dir()) / (std::string(GamePack()) + ".pack.json");
			std::ifstream in(manifest, std::ios::binary);
			std::stringstream text;
			text << in.rdbuf();
			PackManifest m;
			if (!in || !ParsePackManifest(text.str(), m).empty()) { GB_CORE_ERROR("the game's pack manifest {} cannot be read", manifest.generic_string()); return out; }
			for (const std::string& f : m.Files)
			{
				std::ifstream file(manifest.parent_path() / f, std::ios::binary);
				std::stringstream body;
				body << file.rdbuf();
				out.push_back(body.str());
			}
			return out;
		}();
		return docs;
	}

	const ShippedCatalog& Shipped()
	{
		static const ShippedCatalog catalog = Load();   // (the first caller loads it; a static is initialised once even when two threads ask together)
		return catalog;
	}
}
