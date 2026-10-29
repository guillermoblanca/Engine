#pragma once
#include "GB/AI/PriorityGraph.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// The library of priority graphs and who uses which (docs/GRAFO_PRIORIDADES.md). Headless: the editor and the tests call the same functions.
	// Every function that can fail returns why ("" = done) and changes nothing on a failure.
	class PriorityLibrary
	{
	public:
		PriorityLibrary();                     // with the graphs that ship (builtin, builtin_formulas, builtin_raw, fragment_need_urgency, cautious); builtin is the default

		bool ForceEvaluate = false;            // evaluate even the identity graph (tests compare both ways)

		// ---- graphs
		std::vector<std::string> Ids() const;
		const PriorityGraph* Find(const std::string& id) const;
		// Mutable access for the editor: any change invalidates the compiled graphs. Call Touch() after changing it too.
		PriorityGraph* Edit(const std::string& id);
		void Touch();
		std::string Add(PriorityGraph graph, bool overwrite = false);   // validated before it enters (warnings do not stop it)
		std::string Remove(const std::string& id);                     // refused while an assignment or another graph uses it
		std::string Rename(const std::string& id, const std::string& newId);   // also updates assignments and the subgraphs that include it
		std::string Duplicate(const std::string& id, const std::string& newId);
		std::string SaveAsTemplate(const std::string& id, const std::string& newId, const std::string& name = {});   // a copy marked as template
		std::string Instantiate(const std::string& templateId, const std::string& newId);                          // a graph from a template
		std::string ExportJson(const std::string& id, std::string& json) const;
		std::string ImportJson(const std::string& json, bool overwrite = false, std::string* importedId = nullptr);
		std::vector<std::string> UsedBy(const std::string& id) const;   // the graphs that include it and the roles / members assigned to it
		std::vector<PgIssue> Validate(const std::string& id) const;
		std::string ValidateAll() const;                                // every problem of every graph, as text ("" = none)

		// ---- files: one JSON per graph in a folder (assets/data/priority_graphs) and assignments.json beside them
		std::string LoadDirectory(const std::string& dir);              // adds/replaces the graphs it reads; a bad file is reported and skipped
		std::string SaveGraph(const std::string& dir, const std::string& id) const;
		std::string SaveDirectory(const std::string& dir) const;        // every graph and the assignments
		std::string LoadAssignments(const std::string& json);
		std::string AssignmentsToJson() const;

		// ---- who uses which: the member beats the role, the role beats the default; "" = none (the tribe is left alone)
		std::string DefaultGraph = "builtin";
		std::map<std::string, std::string> Roles, Members;
		std::string AssignRole(const std::string& role, const std::string& id);     // id "" clears it
		std::string AssignMember(const std::string& member, const std::string& id);
		std::string AssignDefault(const std::string& id);
		std::string IdFor(const std::string& role, const std::string& member) const;
		// The compiled graph for a character (null: none assigned, or invalid and so ignored).
		const CompiledPriorityGraph* CompiledFor(const std::string& role, const std::string& member) const;
		const CompiledPriorityGraph* Compiled(const std::string& id) const;
		PgResolver Resolver() const;

	private:
		std::map<std::string, PriorityGraph> m_Graphs;
		mutable std::map<std::string, std::shared_ptr<CompiledPriorityGraph>> m_Compiled;   // null entry = invalid
		std::string CheckAssignable(const std::string& id) const;
	};
}
