#pragma once
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/TgHash.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace GB
{
	// A memo of a graph's evaluation, node by node (docs/GRAFO_TERRENO.md, section 10.5): change one parameter and only that node and what
	// depends on it run again. EvaluateTerrainGraph(..., cache) gives each node a key made of what decides its result -- its type, its Source,
	// the exact bits of its parameters, the keys of the nodes and ports that feed it, and the context (seed, footprint, and the content of the
	// fields brought in through TgContext::Inputs) -- and reuses the outputs stored under that node's id when the key is the same. A change
	// therefore propagates downstream by itself (a node's key includes its inputs' keys); a node's label and position in the editor are not part
	// of it. Nodes are pure (TgContext), so a reused output is the output the node would give again: an evaluation with a cache is bit for bit an
	// evaluation without one (tests/terrain_graph_cache_test.cpp).
	//
	// Thread safety: an evaluation holds the cache's lock for as long as it runs; the editor's preview reads it from another thread with
	// TryCopyOutput, which gives up instead of waiting while a world is being built. It costs a copy of every node's outputs (the same as
	// TgResult::Nodes).
	class TerrainGraphCache
	{
	public:
		struct Stats { int Ran = 0, Reused = 0; };

		void Clear();
		Stats LastStats() const;                    // of the last evaluation that used this cache
		size_t Size() const;                       // nodes stored
		// A copy of the output `port` of node `nodeId` ("" = its first output) as the last evaluation left it. False when there is none or the
		// cache is busy (an evaluation is running).
		bool TryCopyOutput(const std::string& nodeId, const std::string& port, TgValue& out) const;
		std::vector<std::string> OutputPorts(const std::string& nodeId) const;   // empty when busy or unknown
		uint64_t Generation() const;               // goes up after every evaluation (a preview knows when to refresh)
		// The footprint the last evaluation was made over (TgContext::Size/Center); false before any.
		bool Footprint(float& size, glm::vec2& center) const;

	private:
		friend bool EvaluateTerrainGraph(const CompiledTerrainGraph&, const TerrainNodeRegistry&, const TgContext&, TgResult&, std::string&, TerrainGraphCache*);
		struct Entry
		{
			uint64_t Key = 0;
			std::map<std::string, TgValue> Outputs;
		};
		mutable std::mutex m_Mutex;
		std::map<std::string, Entry> m_Entries;   // by node id
		Stats m_Stats;
		uint64_t m_Generation = 0;
		bool m_HasFootprint = false;
		float m_Size = 0.0f;
		glm::vec2 m_Center{ 0.0f };
	};
}
