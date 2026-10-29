#pragma once
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/Techs.h"
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace GB
{
	// Eras as data packs (H17). A pack is a manifest, `<id>.pack.json`, beside the content files it lists:
	//   { "id": "era1", "name": "...", "era": 1, "version": "1.0", "depends": [ "<pack id>" ], "files": [ "era1.json" ],
	//     "given":    { "<item>": "where it comes from" },          // obtained outside recipes and nodes (the starting stock, a well...)
	//     "stations": [ "workbench" ],                             // stations a recipe can be made at once this pack is in
	//     "deferred": { "<item, recipe or node>": "why not yet" } } // known gaps: reported as notes, not as problems
	// Loading a pack loads the packs it depends on first (those not loaded yet), then its files in order, through every registered content section,
	// and checks the result. It is atomic: a mistake in any file of any pack (or, under Strict, a problem the check finds) changes nothing at all.
	// The content files themselves are the same documents as before (sections "items", "recipes", "nodes", "roles", ...): a file does not know its pack.

	// What a check found. Problems are things that are wrong (an item nothing can make); notes are worth knowing but fine (a deferred gap,
	// a section of a file no loader reads). Both are sorted and stable, so a test can compare them.
	struct ContentReport
	{
		std::vector<std::string> Problems;
		std::vector<std::string> Notes;
		bool Clean() const { return Problems.empty(); }
		void Append(const ContentReport& other);
		std::string Text() const;   // one line per entry, "problem: ..." then "note: ..."
	};

	// What the packs loaded so far say about the content as a whole: what is given, at which stations one can work and which gaps are known.
	struct PackRules
	{
		std::map<std::string, std::string> Given;      // item -> where it comes from
		std::set<std::string> Stations;
		std::map<std::string, std::string> Deferred;   // item, recipe or node id -> why it cannot be had yet
		void Merge(const PackRules& other);            // later packs add to (and, for the same id, replace) what earlier ones say
	};

	struct PackManifest
	{
		std::string Id;
		std::string Name;
		int Era = 0;
		std::string Version;
		std::vector<std::string> Depends;   // pack ids, looked up beside this manifest
		std::vector<std::string> Files;     // content files, relative to this manifest
		PackRules Rules;
		std::string Path;                   // the manifest file it was read from ("" = read from text)
	};

	// Reads a manifest; "" or what is wrong. The id is required; everything else is optional.
	std::string ParsePackManifest(const std::string& json, PackManifest& out);

	// Can everything be had? Starting from the given items, a node gives its item once its tool (RequiresTag) can be had, and a recipe gives its output once
	// its inputs, its tool and its station can (a station is there when a pack names it). Problems: items nothing reachable makes, recipes that can never
	// be made, nodes whose tool cannot be had, given items that are not items, and recipe cycles that nothing outside feeds. Anything named in
	// rules.Deferred is a note instead. A cycle that is fed from outside (wood -> plank -> wood) is a note.
	ContentReport CheckReachability(const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes, const PackRules& rules);
	// The same aware of technologies (H11): a recipe or a node a technology unlocks is reachable only once one of its technologies can be discovered.
	// A technology can be discovered when it is known from the start, or when everything it requires can and it can be found by experimenting
	// (Discovery.Experiment above 0) with one of its recipes whose inputs, tool and station can be had (or it unlocks no recipe: an idea needs no
	// materials); a station that a building of that very technology gives counts (a try needs only the materials: cooking and its hearth). Watching and being taught need someone who knows it, so on their own they discover nothing. More problems: technologies that require
	// or unlock ids that do not exist, and technologies that can never be discovered (deferred ids, as always, are notes). nullptr = no technologies.
	// With buildings (H8, E9): a building can be built once every material of its phases can be had and, when a technology unlocks it (TechDef::Buildings
	// or its own `tech`), once one of those can be discovered; then its station is there too (a drying rack gives "drying_rack"). A building that can
	// never be built is a problem (a note when deferred). Without technologies a building's `tech` is only noted.
	ContentReport CheckReachability(const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes, const PackRules& rules,
		const TechTree* techs, const std::map<std::string, BuildingDef>* buildings = nullptr);

	// A section loader: the part of the content one system owns ("items" reads items, recipes and nodes; "roles" the tribe roles; later interactives,
	// buildings, techs...). Items does not know the other systems: each registers its own section with ContentPacks. Loading goes Begin (copy the live
	// state into a staging copy), Load for each document (into the copy), Check (over the copy, optional), then Commit (copy -> live) or nothing; End always.
	struct ContentSection
	{
		std::string Name;
		std::vector<std::string> Keys;                                           // top-level keys of a content file it reads
		std::function<void()> Begin;
		std::function<std::string(const std::string& json)> Load;               // "" or what is wrong; a document without its keys must change nothing
		std::function<void(const PackRules&, ContentReport&)> Check;            // may be empty
		std::function<void()> Commit;
		std::function<void()> End;
	};

	// A section over live objects of any types: the staging copy is a tuple of copies of them. `load(json, T&...)` reads one document into the copy
	// (it may be a function that is atomic by itself, like LoadContent); `check(rules, report, const T&...)` may be nullptr. The live objects must
	// outlive the ContentPacks the section is added to.
	namespace Detail
	{
		template <class Copies, class Targets, std::size_t... I>
		void MoveInto(Copies& copies, const Targets& targets, std::index_sequence<I...>) { ((*std::get<I>(targets) = std::move(std::get<I>(copies))), ...); }
	}

	template <class LoadFn, class CheckFn, class... T>
	ContentSection StagedSection(std::string name, std::vector<std::string> keys, LoadFn load, CheckFn check, T&... live)
	{
		auto stage = std::make_shared<std::optional<std::tuple<T...>>>();
		const std::tuple<T*...> targets(&live...);
		ContentSection s;
		s.Name = std::move(name);
		s.Keys = std::move(keys);
		s.Begin = [stage, targets] { stage->emplace(std::apply([](T*... p) { return std::tuple<T...>(*p...); }, targets)); };
		s.Load = [stage, load](const std::string& json) -> std::string { return std::apply([&](T&... t) -> std::string { return load(json, t...); }, **stage); };
		if constexpr (!std::is_same_v<CheckFn, std::nullptr_t>)
			s.Check = [stage, check](const PackRules& rules, ContentReport& report) { std::apply([&](const T&... t) { check(rules, report, t...); }, **stage); };
		s.Commit = [stage, targets] { Detail::MoveInto(**stage, targets, std::index_sequence_for<T...>{}); };
		s.End = [stage] { stage->reset(); };
		return s;
	}

	// The "items" section: items, recipes and resource nodes through LoadContent, checked with CheckReachability.
	ContentSection ItemContentSection(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes);
	// The same with the technologies (H11): the section also reads "techs" and "knowledge" (LoadTechs) and checks with them. They travel in the items
	// section, not in one of their own, because the check needs the staged items, recipes, nodes and technologies of the same load at once.
	ContentSection ItemContentSection(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes, TechTree& techs);
	// The same with the buildings too (H8, "buildings" through LoadBuildings) and, when `techs` is given, the technologies: what a building needs, the
	// station it gives and the technology that unlocks it are part of what can be had. Either may be null.
	ContentSection ItemContentSection(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes, TechTree* techs, std::map<std::string, BuildingDef>* buildings);

	// The packs loaded and the sections that read them.
	class ContentPacks
	{
	public:
		struct Options
		{
			bool Strict = false;   // the problems of the check refuse the load (otherwise they are only reported)
		};

		// Adds a section, or replaces the one of the same name.
		void AddSection(ContentSection section);
		const std::vector<ContentSection>& Sections() const { return m_Sections; }

		// Loads a pack: `idOrPath` is a pack id, looked up as `<dir>/<id>.pack.json`, or the path of a manifest (anything ending in .json).
		// The packs it depends on are loaded first, unless already loaded; the pack itself is loaded again if it was (to pick up edits).
		// Returns "" or what is wrong (and then nothing changed). The report gets the check of the content as it would be after the load, and notes
		// on sections no loader reads.
		std::string Load(const std::string& idOrPath, const std::string& dir, ContentReport* report = nullptr, Options options = {});
		// Loads one content file with no manifest (`content load`): all sections, atomic, checked with the rules of the packs already loaded.
		std::string LoadFile(const std::string& path, ContentReport* report = nullptr, Options options = {});
		// The same for a document in memory ("<text>" in messages).
		std::string LoadText(const std::string& json, ContentReport* report = nullptr, Options options = {});
		// Checks the live content with the rules of the packs loaded (nothing changes).
		ContentReport Check() const;

		const std::vector<PackManifest>& Loaded() const { return m_Loaded; }
		bool IsLoaded(const std::string& id) const;
		const PackRules& Rules() const { return m_Rules; }

		// The manifests in a directory (`*.pack.json`), sorted by era then id. Unreadable ones are skipped.
		static std::vector<PackManifest> Available(const std::string& dir);

	private:
		struct Document { std::string Name; std::string Text; };
		std::string Apply(const std::vector<Document>& docs, const PackRules& rules, ContentReport* report, Options options);
		std::string Stage(const std::vector<Document>& docs, const PackRules& rules, ContentReport* report, Options options);   // Apply between Begin and End
		std::string Resolve(const std::string& idOrPath, const std::string& dir, bool top, std::vector<PackManifest>& order, std::vector<std::string>& visiting) const;

		std::vector<ContentSection> m_Sections;
		std::vector<PackManifest> m_Loaded;   // in load order
		PackRules m_Rules;
	};
}
