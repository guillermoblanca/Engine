#pragma once
#include "NodeGraphEditor.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/Nodes/BuiltinNodes.h"
#include <memory>

namespace GB::Editor
{
	// The terrain graph editor (docs/GRAFO_TERRENO.md). Everything it shows about a node -- its ports, their kinds, its parameters with their
	// ranges, its category and colour -- comes from the node's own TgNodeSchema in the registry, so a node added to the engine (or by a plugin)
	// appears here without touching this file.
	class TerrainGraphUi final : public NodeGraphEditor
	{
	public:
		TerrainGraphUi();
		void Draw(bool* open);
		std::shared_ptr<const TerrainGraph> Graph() const { return m_Graph; }
		// Replaces the edited graph (a preset, a loaded file).
		void LoadGraph(const TerrainGraph& graph);
		bool ConsumeApplyRequest();

	private:
		void AddNode(const std::string& type, const ImVec2& canvasPosition);
		std::string UniqueId(const std::string& type) const;
		const TgNodeSchema& SchemaOf(const std::string& type) const;
		int OutputIndex(const TgNode& node, const std::string& port) const;   // the output an edge leaves from ("out" = the first)
		void Connect(const std::string& from, const std::string& fromPort, TgNode& to, size_t input);
		void DrawInspector(const std::vector<TgIssue>& issues);
		std::shared_ptr<TerrainGraph> m_Graph;
		TerrainNodeRegistry m_Registry;
		TgNodeSchema m_Unknown;
		std::string m_ConnectPort;   // the output of ConnectFrom being dragged
		std::string m_Message;
		bool m_MessageError = false;
		bool m_ApplyRequested = false;
	};
}
