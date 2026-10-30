#include "GB/AI/PriorityLibrary.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>

namespace GB
{
	using Json = nlohmann::json;
	namespace fs = std::filesystem;

	namespace
	{
		bool ValidId(const std::string& id)
		{
			if (id.empty()) return false;
			return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; });
		}

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
			fs::create_directories(p.parent_path(), ec);
			std::ofstream f(p, std::ios::binary);
			if (!f) return "cannot write " + p.string();
			f << text;
			return f ? std::string() : "cannot write " + p.string();
		}
	}

	PriorityLibrary::PriorityLibrary()
	{
		for (PriorityGraph g : { MakeBuiltinPriorityGraph(), MakeFormulasPriorityGraph(), MakeRawPriorityGraph(), MakeNeedUrgencyFragment(), MakeCautiousPriorityGraph() })
			m_Graphs[g.Id] = std::move(g);
	}

	std::vector<std::string> PriorityLibrary::Ids() const
	{
		std::vector<std::string> ids;
		for (const auto& [id, g] : m_Graphs) ids.push_back(id);
		return ids;
	}

	const PriorityGraph* PriorityLibrary::Find(const std::string& id) const
	{
		const auto it = m_Graphs.find(id);
		return it == m_Graphs.end() ? nullptr : &it->second;
	}

	PriorityGraph* PriorityLibrary::Edit(const std::string& id)
	{
		const auto it = m_Graphs.find(id);
		if (it == m_Graphs.end()) return nullptr;
		m_Compiled.clear();
		return &it->second;
	}

	void PriorityLibrary::Touch() { m_Compiled.clear(); }

	PgResolver PriorityLibrary::Resolver() const
	{
		return [this](const std::string& id) { return Find(id); };
	}

	std::vector<PgIssue> PriorityLibrary::Validate(const std::string& id) const
	{
		const PriorityGraph* g = Find(id);
		if (!g) return { { true, "", "no graph '" + id + "'" } };
		return ValidatePriorityGraph(*g, Resolver());
	}

	std::string PriorityLibrary::ValidateAll() const
	{
		std::string out;
		for (const auto& [id, g] : m_Graphs)
		{
			const std::string text = PgIssuesText(ValidatePriorityGraph(g, Resolver()));
			if (!text.empty()) out += "[" + id + "]\n" + text;
		}
		return out;
	}

	std::string PriorityLibrary::Add(PriorityGraph graph, bool overwrite)
	{
		if (!ValidId(graph.Id)) return "the id '" + graph.Id + "' is not valid (letters, digits, '_', '-' and '.')";
		if (!overwrite && m_Graphs.count(graph.Id)) return "a graph '" + graph.Id + "' already exists";
		// it must not make a cycle of includes with the library once it is in
		const std::string id = graph.Id;
		const auto old = m_Graphs.find(id);
		std::optional<PriorityGraph> previous;
		if (old != m_Graphs.end()) previous = old->second;
		m_Graphs[id] = std::move(graph);
		const std::vector<PgIssue> issues = ValidatePriorityGraph(m_Graphs[id], Resolver());
		if (HasPgErrors(issues))
		{
			if (previous) m_Graphs[id] = std::move(*previous); else m_Graphs.erase(id);
			return "the graph '" + id + "' has errors:\n" + PgIssuesText(issues);
		}
		m_Compiled.clear();
		return {};
	}

	std::vector<std::string> PriorityLibrary::UsedBy(const std::string& id) const
	{
		std::vector<std::string> by;
		for (const auto& [gid, g] : m_Graphs)
			for (const PgNode& n : g.Nodes)
				if (n.Type == "subgraph" && n.Source == id) { by.push_back("graph " + gid); break; }
		if (DefaultGraph == id) by.push_back("default");
		for (const auto& [role, gid] : Roles) if (gid == id) by.push_back("role " + role);
		for (const auto& [member, gid] : Members) if (gid == id) by.push_back("member " + member);
		return by;
	}

	std::string PriorityLibrary::Remove(const std::string& id)
	{
		if (!Find(id)) return "no graph '" + id + "'";
		const std::vector<std::string> by = UsedBy(id);
		if (!by.empty())
		{
			std::string list;
			for (const std::string& s : by) list += (list.empty() ? "" : ", ") + s;
			return "'" + id + "' is in use by " + list;
		}
		m_Graphs.erase(id);
		m_Compiled.clear();
		return {};
	}

	std::string PriorityLibrary::Rename(const std::string& id, const std::string& newId)
	{
		if (!Find(id)) return "no graph '" + id + "'";
		if (!ValidId(newId)) return "the id '" + newId + "' is not valid";
		if (newId == id) return {};
		if (Find(newId)) return "a graph '" + newId + "' already exists";
		PriorityGraph g = m_Graphs[id];
		g.Id = newId;
		m_Graphs.erase(id);
		m_Graphs[newId] = std::move(g);
		for (auto& [gid, other] : m_Graphs) for (PgNode& n : other.Nodes) if (n.Type == "subgraph" && n.Source == id) n.Source = newId;
		if (DefaultGraph == id) DefaultGraph = newId;
		for (auto& [k, v] : Roles) if (v == id) v = newId;
		for (auto& [k, v] : Members) if (v == id) v = newId;
		m_Compiled.clear();
		return {};
	}

	std::string PriorityLibrary::Duplicate(const std::string& id, const std::string& newId)
	{
		const PriorityGraph* g = Find(id);
		if (!g) return "no graph '" + id + "'";
		PriorityGraph copy = *g;
		copy.Id = newId;
		copy.Name = g->Name.empty() ? newId : g->Name + " (copy)";
		copy.Revision = 1;
		return Add(std::move(copy));
	}

	std::string PriorityLibrary::SaveAsTemplate(const std::string& id, const std::string& newId, const std::string& name)
	{
		const PriorityGraph* g = Find(id);
		if (!g) return "no graph '" + id + "'";
		PriorityGraph copy = *g;
		copy.Id = newId;
		copy.Template = true;
		copy.Revision = 1;
		if (!name.empty()) copy.Name = name;
		return Add(std::move(copy));
	}

	std::string PriorityLibrary::Instantiate(const std::string& templateId, const std::string& newId)
	{
		const PriorityGraph* g = Find(templateId);
		if (!g) return "no graph '" + templateId + "'";
		if (!g->Template) return "'" + templateId + "' is not a template";
		PriorityGraph copy = *g;
		copy.Id = newId;
		copy.Template = false;
		copy.Revision = 1;
		copy.Tags.erase(std::remove(copy.Tags.begin(), copy.Tags.end(), "template"), copy.Tags.end());
		return Add(std::move(copy));
	}

	std::string PriorityLibrary::ExportJson(const std::string& id, std::string& json) const
	{
		const PriorityGraph* g = Find(id);
		if (!g) return "no graph '" + id + "'";
		json = PriorityGraphToJson(*g);
		return {};
	}

	std::string PriorityLibrary::ImportJson(const std::string& json, bool overwrite, std::string* importedId)
	{
		PriorityGraph g;
		if (const std::string e = PriorityGraphFromJson(json, g); !e.empty()) return e;
		if (importedId) *importedId = g.Id;
		return Add(std::move(g), overwrite);
	}

	// ------------------------------------------------------------------ files
	std::string PriorityLibrary::LoadDirectory(const std::string& dir)
	{
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) return "no folder " + dir;
		std::vector<fs::path> files;
		for (const auto& entry : fs::directory_iterator(dir, ec))
			if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
		std::sort(files.begin(), files.end());
		std::string errors;
		std::vector<PriorityGraph> read;
		for (const fs::path& p : files)
		{
			if (p.filename() == "assignments.json") continue;
			std::string text;
			if (std::string e = ReadFile(p, text); !e.empty()) { errors += e + "\n"; continue; }
			PriorityGraph g;
			if (std::string e = PriorityGraphFromJson(text, g); !e.empty()) { errors += p.filename().string() + ": " + e + "\n"; continue; }
			read.push_back(std::move(g));
		}
		// all of them go in first, then they are checked together (a graph may include one that comes later in the list)
		std::vector<std::string> added;
		std::map<std::string, std::optional<PriorityGraph>> before;
		for (PriorityGraph& g : read)
		{
			const std::string id = g.Id;
			if (!ValidId(id)) { errors += "graph '" + id + "': the id is not valid\n"; continue; }
			if (!before.count(id)) before[id] = Find(id) ? std::optional<PriorityGraph>(*Find(id)) : std::nullopt;
			m_Graphs[id] = std::move(g);
			added.push_back(id);
		}
		for (const std::string& id : added)
		{
			const std::vector<PgIssue> issues = ValidatePriorityGraph(m_Graphs[id], Resolver());
			if (!HasPgErrors(issues)) continue;
			errors += "graph '" + id + "' skipped:\n" + PgIssuesText(issues);
			if (before[id]) m_Graphs[id] = *before[id]; else m_Graphs.erase(id);
		}
		if (const fs::path a = fs::path(dir) / "assignments.json"; fs::exists(a, ec))
		{
			std::string text;
			if (std::string e = ReadFile(a, text); !e.empty()) errors += e + "\n";
			else if (std::string e2 = LoadAssignments(text); !e2.empty()) errors += "assignments.json: " + e2 + "\n";
		}
		m_Compiled.clear();
		return errors;
	}

	std::string PriorityLibrary::SaveGraph(const std::string& dir, const std::string& id) const
	{
		const PriorityGraph* g = Find(id);
		if (!g) return "no graph '" + id + "'";
		return WriteFile(fs::path(dir) / (id + ".json"), PriorityGraphToJson(*g));
	}

	std::string PriorityLibrary::SaveDirectory(const std::string& dir) const
	{
		for (const auto& [id, g] : m_Graphs)
			if (std::string e = SaveGraph(dir, id); !e.empty()) return e;
		return WriteFile(fs::path(dir) / "assignments.json", AssignmentsToJson());
	}

	std::string PriorityLibrary::AssignmentsToJson() const
	{
		Json j;
		j["kind"] = "priority_assignments";
		j["version"] = 1;
		j["default"] = DefaultGraph;
		j["roles"] = Json::object();
		for (const auto& [k, v] : Roles) j["roles"][k] = v;
		j["members"] = Json::object();
		for (const auto& [k, v] : Members) j["members"][k] = v;
		return j.dump(2) + "\n";
	}

	std::string PriorityLibrary::LoadAssignments(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		if (const auto k = j.find("kind"); k != j.end() && (!k->is_string() || k->get<std::string>() != "priority_assignments")) return "not an assignments file";
		if (const auto v = j.find("version"); v != j.end() && (!v->is_number_integer() || v->get<int>() > 1)) return "unsupported version of the assignments";
		std::string def = DefaultGraph;
		std::map<std::string, std::string> roles, members;
		if (const auto d = j.find("default"); d != j.end())
		{
			if (!d->is_string()) return "default must be text";
			def = d->get<std::string>();
		}
		for (const char* key : { "roles", "members" })
		{
			const auto s = j.find(key);
			if (s == j.end()) continue;
			if (!s->is_object()) return std::string(key) + " must be an object of name -> graph id";
			for (const auto& [name, v] : s->items())
			{
				if (!v.is_string()) return std::string(key) + " '" + name + "': the graph id must be text";
				(std::string(key) == "roles" ? roles : members)[name] = v.get<std::string>();
			}
		}
		for (const std::string& id : [&] { std::vector<std::string> all = { def }; for (auto& [k, v] : roles) all.push_back(v); for (auto& [k, v] : members) all.push_back(v); return all; }())
			if (const std::string e = CheckAssignable(id); !e.empty()) return e;
		DefaultGraph = def;
		Roles = roles;
		Members = members;
		return {};
	}

	// ------------------------------------------------------------------ assignments
	std::string PriorityLibrary::CheckAssignable(const std::string& id) const
	{
		if (id.empty()) return {};
		const PriorityGraph* g = Find(id);
		if (!g) return "no graph '" + id + "'";
		if (HasPgErrors(ValidatePriorityGraph(*g, Resolver()))) return "the graph '" + id + "' has errors:\n" + PgIssuesText(ValidatePriorityGraph(*g, Resolver()));
		if (std::any_of(g->Nodes.begin(), g->Nodes.end(), [](const PgNode& n) { return n.Type == "return"; })) return "'" + id + "' is a fragment (it has a 'return'): it can only be included in another graph";
		return {};
	}

	std::string PriorityLibrary::AssignRole(const std::string& role, const std::string& id)
	{
		if (const std::string e = CheckAssignable(id); !e.empty()) return e;
		if (id.empty()) Roles.erase(role); else Roles[role] = id;
		return {};
	}

	std::string PriorityLibrary::AssignMember(const std::string& member, const std::string& id)
	{
		if (const std::string e = CheckAssignable(id); !e.empty()) return e;
		if (id.empty()) Members.erase(member); else Members[member] = id;
		return {};
	}

	std::string PriorityLibrary::AssignDefault(const std::string& id)
	{
		if (const std::string e = CheckAssignable(id); !e.empty()) return e;
		DefaultGraph = id;
		return {};
	}

	std::string PriorityLibrary::IdFor(const std::string& role, const std::string& member) const
	{
		if (const auto m = Members.find(member); m != Members.end()) return m->second;
		if (const auto r = Roles.find(role); r != Roles.end()) return r->second;
		return DefaultGraph;
	}

	const CompiledPriorityGraph* PriorityLibrary::Compiled(const std::string& id) const
	{
		if (id.empty()) return nullptr;
		const auto it = m_Compiled.find(id);
		if (it != m_Compiled.end()) return it->second.get();
		const PriorityGraph* g = Find(id);
		std::shared_ptr<CompiledPriorityGraph> c;
		if (g)
		{
			c = std::make_shared<CompiledPriorityGraph>();
			if (!CompilePriorityGraph(*g, Resolver(), *c)) c.reset();
		}
		m_Compiled[id] = c;
		return c.get();
	}

	const CompiledPriorityGraph* PriorityLibrary::CompiledFor(const std::string& role, const std::string& member) const
	{
		return Compiled(IdFor(role, member));
	}
}
