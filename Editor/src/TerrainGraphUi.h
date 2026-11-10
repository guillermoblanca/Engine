#pragma once
#include "NodeGraphEditor.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/Nodes/BuiltinNodes.h"
#include "GB/World/Graph/TerrainGraphCache.h"
#include "GB/World/Graph/TerrainGraphLibrary.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace GB { class Terrain; class Texture2D; }

namespace GB::Editor
{
	// The terrain graph editor (docs/GRAFO_TERRENO.md). Everything it shows about a node -- its ports, their kinds, its parameters with their
	// ranges, its category and colour -- comes from the node's own TgNodeSchema in the registry, so a node added to the engine (or by a plugin)
	// appears here without touching this file.
	class TerrainGraphUi final : public NodeGraphEditor
	{
	public:
		TerrainGraphUi();
		~TerrainGraphUi();
		void Draw(bool* open);
		std::shared_ptr<const TerrainGraph> Graph() const { return m_Graph; }
		// Replaces the edited graph (a preset, a loaded file).
		void LoadGraph(const TerrainGraph& graph);
		bool ConsumeApplyRequest();
		// The node-by-node memo of the panel's graph (WorldDesc::GraphCache): a world built from the panel reuses every node that did not change.
		std::shared_ptr<TerrainGraphCache> Cache() const { return m_Cache; }

		// The library of graphs (TerrainGraphLibrary): the presets plus the files of assets/data/terrain_graphs, read the first time it is asked for.
		TerrainGraphLibrary& Library();
		const std::string& Directory() const { return m_Dir; }
		// Saves the graph in the panel as <id>.json in the library's folder ("" = under its own id); "" when done, else why not.
		std::string SaveCurrent(const std::string& id);
		// Loads into the panel a graph of the library by id, or a .json file by path; "" when done, else why not.
		std::string LoadNamed(const std::string& idOrPath);
		// `terrain_graph library | load <id|file.json> | save [id] | export <id> <file.json> | reload`: false when it failed; `message` says what happened.
		bool Command(const std::vector<std::string>& args, std::string& message);

		// The preview of one port (docs/GRAFO_TERRENO.md 10.5): the value a node gave in the last world built from the panel, read from the cache
		// (Cache()), as a map in the inspector and/or painted over the terrain in the viewport. A field is coloured from its lowest to its highest
		// value; placements are dots, paths lines, water the lake's shore and the river. `mode`: map, overlay, both or off.
		std::string SetPreview(const std::string& node, const std::string& port, const std::string& mode);
		// Paints the previewed port over the ground (ImGui quads projected with the camera, like the editor's map overlay): `draw` is the viewport
		// window's draw list, (ox, oy, w, h) its rectangle.
		void DrawViewportOverlay(ImDrawList* draw, const glm::mat4& viewProj, float ox, float oy, float w, float h, const Terrain& terrain);

	private:
		void AddNode(const std::string& type, const ImVec2& canvasPosition);
		std::string UniqueId(const std::string& type) const;
		const TgNodeSchema& SchemaOf(const std::string& type) const;
		int OutputIndex(const TgNode& node, const std::string& port) const;   // the output an edge leaves from ("out" = the first)
		void Connect(const std::string& from, const std::string& fromPort, TgNode& to, size_t input);
		void DrawInspector(const std::vector<TgIssue>& issues);
		void DrawLibraryBar();
		void DrawPreview();          // the map and the port picker, in the inspector
		bool RefreshPreview();       // copies the previewed port out of the cache when a newer evaluation left it; false while there is none
		glm::vec4 PreviewColour(float value) const;   // rgba 0..1 for a field value
		std::shared_ptr<TerrainGraph> m_Graph;
		TerrainNodeRegistry m_Registry;
		TgNodeSchema m_Unknown;
		std::string m_ConnectPort;   // the output of ConnectFrom being dragged
		std::string m_Message;
		bool m_MessageError = false;
		bool m_ApplyRequested = false;
		TerrainGraphLibrary m_Library;
		std::string m_Dir;             // where the library reads and writes its files
		bool m_LibraryLoaded = false;
		char m_SaveId[64] = {};
		std::shared_ptr<TerrainGraphCache> m_Cache = std::make_shared<TerrainGraphCache>();
		struct Preview
		{
			std::string Node, Port;
			bool Map = true, Overlay = false;
			uint64_t Generation = ~0ull;     // of the cache, when Value was copied
			bool Valid = false;
			TgValue Value;
			float Min = 0.0f, Max = 1.0f;    // a field's range
			float Size = 0.0f;               // the footprint of the evaluation (placements, paths and water are drawn in it)
			glm::vec2 Center{ 0.0f };
			std::unique_ptr<Texture2D> Texture;   // the field as a 256 x 256 image
		} m_Preview;
	};
}
