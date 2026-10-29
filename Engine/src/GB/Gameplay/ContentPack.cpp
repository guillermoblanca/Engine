#include "GB/Gameplay/ContentPack.h"
#include "GB/Gameplay/GameplayTag.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	void ContentReport::Append(const ContentReport& other)
	{
		Problems.insert(Problems.end(), other.Problems.begin(), other.Problems.end());
		Notes.insert(Notes.end(), other.Notes.begin(), other.Notes.end());
	}

	std::string ContentReport::Text() const
	{
		std::string out;
		for (const std::string& p : Problems) out += "problem: " + p + "\n";
		for (const std::string& n : Notes) out += "note: " + n + "\n";
		return out;
	}

	void PackRules::Merge(const PackRules& other)
	{
		for (const auto& [id, why] : other.Given) Given[id] = why;
		Stations.insert(other.Stations.begin(), other.Stations.end());
		for (const auto& [id, why] : other.Deferred) Deferred[id] = why;
	}

	namespace
	{
		bool ReadText(const std::string& path, std::string& out)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in) return false;
			std::stringstream ss;
			ss << in.rdbuf();
			out = ss.str();
			return true;
		}

		// A map of id -> text ("given", "deferred"); "" or what is wrong.
		std::string ReadReasons(const Json& j, const char* key, std::map<std::string, std::string>& out)
		{
			const auto it = j.find(key);
			if (it == j.end()) return {};
			if (!it->is_object()) return std::string(key) + " must be an object of id -> reason";
			for (const auto& [id, why] : it->items())
			{
				if (id.empty() || !why.is_string()) return std::string(key) + ": '" + id + "' needs a text that says why";
				out[id] = why.get<std::string>();
			}
			return {};
		}

		std::string ReadNames(const Json& j, const char* key, std::vector<std::string>& out)
		{
			const auto it = j.find(key);
			if (it == j.end()) return {};
			if (!it->is_array()) return std::string(key) + " must be a list of names";
			for (const Json& x : *it)
			{
				if (!x.is_string() || x.get<std::string>().empty()) return std::string(key) + " must be a list of names";
				out.push_back(x.get<std::string>());
			}
			return {};
		}
	}

	std::string ParsePackManifest(const std::string& text, PackManifest& out)
	{
		const Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the manifest is not a JSON object";
		PackManifest m;
		if (const auto it = j.find("id"); it != j.end() && it->is_string()) m.Id = it->get<std::string>();
		if (m.Id.empty() || m.Id.find_first_of("/\\ ") != std::string::npos) return "the manifest needs an id (a name without spaces or slashes)";
		m.Name = m.Id;
		if (const auto it = j.find("name"); it != j.end() && it->is_string()) m.Name = it->get<std::string>();
		if (const auto it = j.find("version"); it != j.end() && it->is_string()) m.Version = it->get<std::string>();
		if (const auto it = j.find("era"); it != j.end())
		{
			if (!it->is_number_integer() || it->get<long long>() < 0 || it->get<long long>() > 100) return "pack '" + m.Id + "': era must be a whole number from 0 to 100";
			m.Era = it->get<int>();
		}
		std::vector<std::string> stations;
		for (std::string error : { ReadNames(j, "depends", m.Depends), ReadNames(j, "files", m.Files), ReadNames(j, "stations", stations),
			ReadReasons(j, "given", m.Rules.Given), ReadReasons(j, "deferred", m.Rules.Deferred) })
			if (!error.empty()) return "pack '" + m.Id + "': " + error;
		m.Rules.Stations.insert(stations.begin(), stations.end());
		if (std::find(m.Depends.begin(), m.Depends.end(), m.Id) != m.Depends.end()) return "pack '" + m.Id + "' depends on itself";
		out = std::move(m);
		return {};
	}

	ContentReport CheckReachability(const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes, const PackRules& rules)
	{
		return CheckReachability(items, recipes, nodes, rules, nullptr);
	}

	ContentReport CheckReachability(const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes, const PackRules& rules,
		const TechTree* techs, const std::map<std::string, BuildingDef>* buildings)
	{
		ContentReport report;
		std::set<std::string> have, made, worked, found, built, stations;   // items had, recipes made, nodes worked, technologies discovered, buildings that can be built (H8) and the stations they give
		for (const auto& [id, why] : rules.Given)
		{
			if (items.Find(id)) have.insert(id);
			else report.Problems.push_back("given item '" + id + "' is not an item");
		}
		// A tool requirement is met by any item that can be had and carries the tag or a descendant of it ("Item.Tool.Axe" by a flint axe).
		auto toolOk = [&](const std::string& tag)
		{
			if (tag.empty()) return true;
			for (const std::string& id : have)
				if (const ItemDef* def = items.Find(id))
					for (const std::string& t : def->Tags)
						if (GameplayTag::TagMatches(tag, t)) return true;
			return false;
		};
		auto stationOk = [&](const std::string& s) { return s.empty() || rules.Stations.count(s) > 0 || stations.count(s) > 0; };
		// Technologies (H11): what unlocks a recipe, a node or a building (nothing: common knowledge), and whether one of them is discovered already.
		std::map<std::string, std::vector<std::string>> lockRecipe, lockNode, lockBuilding;
		if (techs)
			for (const auto& [id, t] : techs->Techs)
			{
				for (const std::string& r : t.Recipes) lockRecipe[r].push_back(id);
				for (const std::string& n : t.Nodes) lockNode[n].push_back(id);
				for (const std::string& b : t.Buildings) lockBuilding[b].push_back(id);
				if (t.Known) found.insert(id);
			}
		if (techs && buildings)
			for (const auto& [id, b] : *buildings)
				if (!b.Tech.empty()) lockBuilding[id].push_back(b.Tech);   // a building's own `tech` locks it too
		auto unlocked = [&](const std::map<std::string, std::vector<std::string>>& locks, const std::string& id)
		{
			const auto it = locks.find(id);
			return it == locks.end() || std::any_of(it->second.begin(), it->second.end(), [&](const std::string& t) { return found.count(t) > 0; });
		};
		auto inputsOk = [&](const Recipe& r) { return std::all_of(r.Inputs.begin(), r.Inputs.end(), [&](const auto& in) { return have.count(in.first) > 0; }); };

		// Fixed point: whatever a node or a recipe gives can feed the next round, until a round adds nothing.
		const std::vector<std::string> recipeIds = recipes.Ids();
		for (bool changed = true; changed;)
		{
			changed = false;
			for (const auto& [id, n] : nodes)
				if (!worked.count(id) && items.Find(n.Item) && toolOk(n.RequiresTag) && unlocked(lockNode, id))
				{
					worked.insert(id);
					have.insert(n.Item);
					changed = true;
				}
			if (buildings)
				for (const auto& [id, b] : *buildings)
				{
					if (built.count(id) || !unlocked(lockBuilding, id)) continue;
					const std::map<std::string, int> needs = b.TotalNeeds();
					if (!std::all_of(needs.begin(), needs.end(), [&](const auto& in) { return have.count(in.first) > 0; })) continue;
					built.insert(id);
					if (!b.Station.empty()) stations.insert(b.Station);
					changed = true;
				}
			for (const std::string& id : recipeIds)
			{
				const Recipe& r = *recipes.Find(id);
				if (made.count(id) || !stationOk(r.Station) || !toolOk(r.ToolTag) || !unlocked(lockRecipe, id)) continue;
				if (!inputsOk(r) || !items.Find(r.Output)) continue;
				made.insert(id);
				have.insert(r.Output);
				changed = true;
			}
			// a technology is found by trying one of its recipes with materials that can be had (or by an idea, when it unlocks no recipe)
			if (techs)
				for (const auto& [id, t] : techs->Techs)
				{
					if (found.count(id) || t.Discovery.Experiment <= 0.0f) continue;
					if (!std::all_of(t.Requires.begin(), t.Requires.end(), [&](const std::string& q) { return found.count(q) > 0; })) continue;
					// (a station that a building of this very technology gives does not stop it: a try needs only the materials, and once found the
					// clan can raise the building; that is how cooking and its hearth, or smelting and its forge, are found)
					auto ownStation = [&](const std::string& s)
					{
						if (!buildings || s.empty()) return false;
						for (const auto& [bid, b] : *buildings)
						{
							if (b.Station != s) continue;
							const bool mine = b.Tech == id || std::find(t.Buildings.begin(), t.Buildings.end(), bid) != t.Buildings.end();
							const std::map<std::string, int> needs = b.TotalNeeds();
							if (mine && std::all_of(needs.begin(), needs.end(), [&](const auto& in) { return have.count(in.first) > 0; })) return true;
						}
						return false;
					};
					const bool tryable = t.Recipes.empty() || std::any_of(t.Recipes.begin(), t.Recipes.end(), [&](const std::string& rid)
					{
						const Recipe* r = recipes.Find(rid);
						return r && (stationOk(r->Station) || ownStation(r->Station)) && toolOk(r->ToolTag) && inputsOk(*r);
					});
					if (!tryable) continue;
					found.insert(id);
					changed = true;
				}
		}

		auto deferred = [&](const std::string& id) { return rules.Deferred.count(id) > 0; };
		auto file = [&](bool known, const std::string& id, const std::string& text)
		{
			if (known) report.Notes.push_back(text + " (deferred: " + rules.Deferred.at(id) + ")");
			else report.Problems.push_back(text);
		};

		// Recipes that can never be made, with the first thing that stops each.
		for (const std::string& id : recipeIds)
		{
			if (made.count(id)) continue;
			const Recipe& r = *recipes.Find(id);
			std::string why;
			if (!unlocked(lockRecipe, id))
			{
				why = "needs one of the technologies";
				for (const std::string& t : lockRecipe.at(id)) why += " '" + t + "'";
				why += ", none of which can be discovered";
			}
			else if (!stationOk(r.Station)) why = "needs the station '" + r.Station + "', which no pack provides" + std::string(buildings ? " and no building gives" : "");
			else if (!toolOk(r.ToolTag)) why = "needs a tool tagged " + r.ToolTag + " that cannot be had";
			else
			{
				std::string missing;
				for (const auto& in : r.Inputs)
					if (!have.count(in.first)) missing += (missing.empty() ? "" : ", ") + in.first;
				why = "needs " + missing + ", which cannot be had";
			}
			const std::string& keyed = deferred(id) ? id : r.Output;
			file(deferred(id) || deferred(r.Output), keyed, "recipe '" + id + "' can never be made: " + why);
		}
		// Nodes whose tool cannot be had (their item may still come from elsewhere: the node is dead content all the same).
		for (const auto& [id, n] : nodes)
			if (!worked.count(id))
				file(deferred(id) || deferred(n.Item), deferred(id) ? id : n.Item, "node '" + id + "' can never be worked: " +
					(unlocked(lockNode, id) ? "it needs a tool tagged " + n.RequiresTag + " that cannot be had" : std::string("no technology that unlocks it can be discovered")));
		// Technologies that point at nothing, and those that can never be discovered (with the first thing that stops each).
		if (techs)
			for (const auto& [id, t] : techs->Techs)
			{
				for (const std::string& q : t.Requires)
					if (!techs->Find(q)) report.Problems.push_back("tech '" + id + "' requires '" + q + "', which is not a technology");
				for (const std::string& r : t.Recipes)
					if (!recipes.Find(r)) report.Problems.push_back("tech '" + id + "' unlocks the recipe '" + r + "', which does not exist");
				for (const std::string& n : t.Nodes)
					if (!nodes.count(n)) report.Problems.push_back("tech '" + id + "' unlocks the node '" + n + "', which does not exist");
				if (buildings)
					for (const std::string& b : t.Buildings)
						if (!buildings->count(b)) report.Problems.push_back("tech '" + id + "' unlocks the building '" + b + "', which does not exist");
				if (found.count(id)) continue;
				std::string why;
				for (const std::string& q : t.Requires)
					if (!found.count(q)) { why = "it requires '" + q + "', which cannot be discovered"; break; }
				if (why.empty()) why = t.Discovery.Experiment <= 0.0f ? "it cannot be found by experimenting, and nobody knows it to show or teach it"
					: "none of its recipes can be tried: their materials, tool or station cannot be had";
				file(deferred(id), id, "tech '" + id + "' can never be discovered: " + why);
			}
		// Buildings (H8) whose materials cannot be had, or that no technology that can be discovered unlocks. Without technologies a building's `tech` is only noted.
		if (buildings)
			for (const auto& [id, b] : *buildings)
			{
				if (!techs && !b.Tech.empty()) report.Notes.push_back("building '" + id + "' needs the tech '" + b.Tech + "' (not checked: no technologies loaded)");
				if (built.count(id)) continue;
				std::string missing;
				for (const auto& [item, n] : b.TotalNeeds())
					if (!have.count(item)) missing += (missing.empty() ? "" : ", ") + item;
				file(deferred(id), id, "building '" + id + "' can never be built: " + (missing.empty() ? std::string("no technology that unlocks it can be discovered") : "needs " + missing + ", which cannot be had"));
			}
		// Items that cannot be had, and where they would have come from.
		for (const std::string& id : items.Ids())
		{
			if (have.count(id)) continue;
			std::vector<std::string> from;
			for (const auto& [nid, n] : nodes) if (n.Item == id) from.push_back("node " + nid);
			for (const std::string& rid : recipeIds) if (recipes.Find(rid)->Output == id) from.push_back("recipe " + rid);
			std::string why = "no node, recipe or given entry makes it";
			if (!from.empty())
			{
				why = "only from ";
				for (size_t i = 0; i < from.size(); ++i) why += (i ? ", " : "") + from[i];
				why += from.size() == 1 ? ", which never yields" : ", none of which yields";
			}
			file(deferred(id), id, "item '" + id + "' cannot be had: " + why);
		}

		// Recipe cycles (an input that is, a few recipes later, made from the output): strongly connected parts of the graph input -> output (Tarjan).
		std::map<std::string, std::set<std::string>> edges;
		for (const std::string& id : recipeIds)
		{
			const Recipe& r = *recipes.Find(id);
			for (const auto& in : r.Inputs) edges[in.first].insert(r.Output);
		}
		std::map<std::string, int> index, low;
		std::vector<std::string> stack;
		std::set<std::string> onStack;
		int counter = 0;
		std::function<void(const std::string&)> visit = [&](const std::string& v)
		{
			index[v] = low[v] = counter++;
			stack.push_back(v);
			onStack.insert(v);
			for (const std::string& w : edges[v])
			{
				if (!index.count(w)) { visit(w); low[v] = std::min(low[v], low[w]); }
				else if (onStack.count(w)) low[v] = std::min(low[v], index[w]);
			}
			if (low[v] != index[v]) return;
			std::vector<std::string> part;
			for (std::string w; w != v;)
			{
				w = stack.back();
				stack.pop_back();
				onStack.erase(w);
				part.push_back(w);
			}
			if (part.size() == 1 && !edges[v].count(v)) return;   // a single item that is not made from itself is no cycle
			std::sort(part.begin(), part.end());
			std::string names;
			bool fed = true, known = false;
			for (const std::string& p : part)
			{
				names += (names.empty() ? "" : ", ") + p;
				if (!have.count(p)) { fed = false; known = known || deferred(p); }
			}
			if (fed) report.Notes.push_back("recipe cycle among " + names + ": fine, something outside it feeds it");
			else if (known) report.Notes.push_back("recipe cycle among " + names + " that nothing outside it feeds (deferred)");
			else report.Problems.push_back("recipe cycle among " + names + " that nothing outside it feeds");
		};
		std::vector<std::string> roots;
		for (const auto& kv : edges) roots.push_back(kv.first);
		for (const std::string& v : roots) if (!index.count(v)) visit(v);

		// Deferred entries that are no longer gaps, or name nothing.
		for (const auto& [id, why] : rules.Deferred)
		{
			const bool isItem = items.Find(id) != nullptr, isRecipe = recipes.Find(id) != nullptr, isNode = nodes.count(id) > 0, isTech = techs && techs->Find(id),
				isBuilding = buildings && buildings->count(id) > 0;
			if (!isItem && !isRecipe && !isNode && !isTech && !isBuilding) report.Notes.push_back("deferred '" + id + "' names no item, recipe, node, technology or building loaded");
			else if ((!isItem || have.count(id)) && (!isRecipe || made.count(id)) && (!isNode || worked.count(id)) && (!isTech || found.count(id)) && (!isBuilding || built.count(id)))
				report.Notes.push_back("'" + id + "' is deferred but can be had now: drop it from the pack");
		}

		std::sort(report.Problems.begin(), report.Problems.end());
		std::sort(report.Notes.begin(), report.Notes.end());
		return report;
	}

	ContentSection ItemContentSection(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes)
	{
		return StagedSection("items", { "items", "recipes", "nodes" },
			[](const std::string& json, ItemLibrary& i, RecipeBook& r, std::map<std::string, ResourceNodeDef>& n) { return LoadContent(json, i, r, n); },
			[](const PackRules& rules, ContentReport& report, const ItemLibrary& i, const RecipeBook& r, const std::map<std::string, ResourceNodeDef>& n)
			{
				report.Append(CheckReachability(i, r, n, rules));
			},
			items, recipes, nodes);
	}

	ContentSection ItemContentSection(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes, TechTree& techs)
	{
		return StagedSection("items", { "items", "recipes", "nodes", "techs", "knowledge" },
			[](const std::string& json, ItemLibrary& i, RecipeBook& r, std::map<std::string, ResourceNodeDef>& n, TechTree& t)
			{
				// both readers are atomic; the tree is read on a copy first, so a mistake in either leaves both as they were
				TechTree tree = t;
				if (std::string error = LoadTechs(json, tree); !error.empty()) return error;
				if (std::string error = LoadContent(json, i, r, n); !error.empty()) return error;
				t = std::move(tree);
				return std::string();
			},
			[](const PackRules& rules, ContentReport& report, const ItemLibrary& i, const RecipeBook& r, const std::map<std::string, ResourceNodeDef>& n, const TechTree& t)
			{
				report.Append(CheckReachability(i, r, n, rules, &t));
			},
			items, recipes, nodes, techs);
	}

	ContentSection ItemContentSection(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes, TechTree* techs, std::map<std::string, BuildingDef>* buildings)
	{
		if (!buildings) return techs ? ItemContentSection(items, recipes, nodes, *techs) : ItemContentSection(items, recipes, nodes);
		// H8: the buildings travel here too (and, when given, the technologies), for the same reason: the check needs all of them from the same load.
		// Without technologies an empty tree stands in for them (and is not written back: there is no live tree).
		auto noTechs = std::make_shared<TechTree>();
		TechTree& tree = techs ? *techs : *noTechs;
		const bool withTechs = techs != nullptr;
		std::vector<std::string> keys = { "items", "recipes", "nodes", "buildings" };
		if (withTechs) { keys.push_back("techs"); keys.push_back("knowledge"); }
		return StagedSection("items", keys,
			[withTechs, noTechs](const std::string& json, ItemLibrary& i, RecipeBook& r, std::map<std::string, ResourceNodeDef>& n, TechTree& t, std::map<std::string, BuildingDef>& b) -> std::string
			{
				TechTree read = t;
				if (withTechs)
					if (std::string error = LoadTechs(json, read); !error.empty()) return error;
				if (std::string error = LoadContent(json, i, r, n); !error.empty()) return error;
				if (std::string error = LoadBuildings(json, b); !error.empty()) return error;   // (the whole staging copy is dropped on a mistake: the items above do not stay)
				t = std::move(read);
				return std::string();
			},
			[withTechs, noTechs](const PackRules& rules, ContentReport& report, const ItemLibrary& i, const RecipeBook& r, const std::map<std::string, ResourceNodeDef>& n, const TechTree& t,
				const std::map<std::string, BuildingDef>& b)
			{
				report.Append(CheckReachability(i, r, n, rules, withTechs ? &t : nullptr, &b));
			},
			items, recipes, nodes, tree, *buildings);
	}

	void ContentPacks::AddSection(ContentSection section)
	{
		for (ContentSection& s : m_Sections)
			if (s.Name == section.Name) { s = std::move(section); return; }
		m_Sections.push_back(std::move(section));
	}

	bool ContentPacks::IsLoaded(const std::string& id) const
	{
		return std::any_of(m_Loaded.begin(), m_Loaded.end(), [&](const PackManifest& m) { return m.Id == id; });
	}

	std::string ContentPacks::Apply(const std::vector<Document>& docs, const PackRules& rules, ContentReport* report, Options options)
	{
		// Every section gets a staging copy; End drops them whatever happens, so a refused load leaves nothing behind.
		for (ContentSection& s : m_Sections) s.Begin();
		const std::string error = Stage(docs, rules, report, options);
		for (ContentSection& s : m_Sections) if (s.End) s.End();
		return error;
	}

	std::string ContentPacks::Stage(const std::vector<Document>& docs, const PackRules& rules, ContentReport* report, Options options)
	{
		ContentReport found;
		for (const Document& d : docs)
		{
			const auto where = [&](const std::string& what) { return d.Name.empty() ? what : d.Name + ": " + what; };
			const Json j = Json::parse(d.Text, nullptr, false);
			if (j.is_discarded() || !j.is_object()) return where("the content is not a JSON object");
			for (auto it = j.begin(); it != j.end(); ++it)
			{
				const std::string& key = it.key();
				const bool owned = std::any_of(m_Sections.begin(), m_Sections.end(), [&](const ContentSection& s) { return std::find(s.Keys.begin(), s.Keys.end(), key) != s.Keys.end(); });
				if (!owned) found.Notes.push_back(where("section '" + key + "' has no loader here (ignored)"));
			}
			for (ContentSection& s : m_Sections)
				if (const std::string error = s.Load(d.Text); !error.empty()) return where(error);
		}
		ContentReport checked;
		for (ContentSection& s : m_Sections) if (s.Check) s.Check(rules, checked);
		std::sort(checked.Problems.begin(), checked.Problems.end());
		std::sort(checked.Notes.begin(), checked.Notes.end());
		found.Append(checked);
		if (report) *report = found;
		if (options.Strict && !found.Clean())
			return std::to_string(found.Problems.size()) + " problem(s) in the content, the first: " + found.Problems.front();
		for (ContentSection& s : m_Sections) s.Commit();
		return {};
	}

	std::string ContentPacks::Resolve(const std::string& idOrPath, const std::string& dir, bool top, std::vector<PackManifest>& order, std::vector<std::string>& visiting) const
	{
		const bool isPath = idOrPath.size() > 5 && idOrPath.compare(idOrPath.size() - 5, 5, ".json") == 0;
		const std::string path = isPath ? idOrPath : (std::filesystem::path(dir) / (idOrPath + ".pack.json")).generic_string();
		std::string text;
		if (!ReadText(path, text)) return isPath ? "cannot open " + path : "no pack '" + idOrPath + "' (" + path + " not found)";
		PackManifest m;
		if (const std::string error = ParsePackManifest(text, m); !error.empty()) return path + ": " + error;
		if (!isPath && m.Id != idOrPath) return path + ": the manifest says it is '" + m.Id + "', not '" + idOrPath + "'";
		m.Path = path;
		if (std::find(visiting.begin(), visiting.end(), m.Id) != visiting.end())
		{
			std::string cycle;
			for (const std::string& v : visiting) cycle += v + " -> ";
			return "the packs depend on each other in a circle: " + cycle + m.Id;
		}
		if (std::any_of(order.begin(), order.end(), [&](const PackManifest& o) { return o.Id == m.Id; })) return {};   // already in, through another path
		if (!top && IsLoaded(m.Id)) return {};
		visiting.push_back(m.Id);
		const std::string base = std::filesystem::path(path).parent_path().generic_string();
		for (const std::string& dep : m.Depends)
			if (const std::string error = Resolve(dep, base, false, order, visiting); !error.empty()) return error;
		visiting.pop_back();
		order.push_back(std::move(m));
		return {};
	}

	std::string ContentPacks::Load(const std::string& idOrPath, const std::string& dir, ContentReport* report, Options options)
	{
		std::vector<PackManifest> order;
		std::vector<std::string> visiting;
		if (const std::string error = Resolve(idOrPath, dir, true, order, visiting); !error.empty()) return error;

		std::vector<Document> docs;
		for (const PackManifest& m : order)
		{
			const std::filesystem::path base = std::filesystem::path(m.Path).parent_path();
			for (const std::string& f : m.Files)
			{
				Document d;
				d.Name = (base / f).generic_string();
				if (!ReadText(d.Name, d.Text)) return "pack '" + m.Id + "': cannot open " + d.Name;
				docs.push_back(std::move(d));
			}
		}
		// The packs as they will be: a reloaded pack keeps its place in the order, a new one goes at the end. The rules are rebuilt from them,
		// so a reloaded manifest that dropped a station or a deferral really drops it.
		std::vector<PackManifest> loaded = m_Loaded;
		for (PackManifest& m : order)
		{
			auto same = std::find_if(loaded.begin(), loaded.end(), [&](const PackManifest& o) { return o.Id == m.Id; });
			if (same != loaded.end()) *same = m;
			else loaded.push_back(m);
		}
		PackRules rules;
		for (const PackManifest& m : loaded) rules.Merge(m.Rules);

		if (const std::string error = Apply(docs, rules, report, options); !error.empty()) return error;
		m_Loaded = std::move(loaded);
		m_Rules = std::move(rules);
		return {};
	}

	std::string ContentPacks::LoadFile(const std::string& path, ContentReport* report, Options options)
	{
		Document d;
		d.Name = path;
		if (!ReadText(path, d.Text)) return "cannot open " + path;
		return Apply({ d }, m_Rules, report, options);
	}

	std::string ContentPacks::LoadText(const std::string& json, ContentReport* report, Options options)
	{
		return Apply({ Document{ "", json } }, m_Rules, report, options);
	}

	ContentReport ContentPacks::Check() const
	{
		ContentReport report;
		for (const ContentSection& s : m_Sections) s.Begin();
		for (const ContentSection& s : m_Sections) if (s.Check) s.Check(m_Rules, report);
		for (const ContentSection& s : m_Sections) if (s.End) s.End();
		std::sort(report.Problems.begin(), report.Problems.end());
		std::sort(report.Notes.begin(), report.Notes.end());
		return report;
	}

	std::vector<PackManifest> ContentPacks::Available(const std::string& dir)
	{
		std::vector<PackManifest> out;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			const std::string name = entry.path().filename().string();
			if (!entry.is_regular_file() || name.size() <= 10 || name.compare(name.size() - 10, 10, ".pack.json") != 0) continue;
			std::string text;
			PackManifest m;
			if (!ReadText(entry.path().string(), text) || !ParsePackManifest(text, m).empty()) continue;
			m.Path = entry.path().generic_string();
			out.push_back(std::move(m));
		}
		std::sort(out.begin(), out.end(), [](const PackManifest& a, const PackManifest& b) { return a.Era != b.Era ? a.Era < b.Era : a.Id < b.Id; });
		return out;
	}
}
