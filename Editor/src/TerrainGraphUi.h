#pragma once
#include "NodeGraphEditor.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/Nodes/BuiltinNodes.h"
#include <memory>

namespace GB::Editor
{
	struct TerrainPortInfo
	{
		std::string Name;
		std::string Type;
	};

	class TerrainGraphUi final : public NodeGraphEditor
	{
	public:
		TerrainGraphUi();
		void Draw(bool* open);
		std::shared_ptr<const TerrainGraph> Graph() const { return m_Graph; }
		bool ConsumeApplyRequest();

	private:
		void AddNode(const std::string& type, const ImVec2& canvasPosition);
		std::string UniqueId(const std::string& type) const;
		std::vector<struct TerrainPortInfo> InputsFor(const std::string& type) const;
		std::vector<struct TerrainPortInfo> OutputsFor(const std::string& type) const;
		std::shared_ptr<TerrainGraph> m_Graph;
		TerrainNodeRegistry m_Registry;
		std::string m_Message;
		bool m_MessageError = false;
		bool m_ApplyRequested = false;
	};
}
