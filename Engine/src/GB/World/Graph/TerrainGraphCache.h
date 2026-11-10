#pragma once
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/TgHash.h"
#include <cstddef>
#include <atomic>
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
	// One entry per node id: the resolution is in the key, so a cache that alternates between two resolutions recomputes everything each time.
	// The editor keeps one cache per resolution (the full world and the quick preview, T84).
	//
	// Memory (T84): every output of every node is kept, a few hundred MB for the 3 km valley at 1025 x 1025. SetByteBudget caps it: after each
	// evaluation the entries that give back the least time per byte are dropped until the rest fit (a dropped node only runs again when it is
	// next evaluated; the results never change). Clear frees everything (the editor does when its panel closes).
	//
	// Thread safety: an evaluation holds the cache's lock for as long as it runs; the editor's preview reads it from another thread with
	// TryCopyOutput, which gives up instead of waiting while a world is being built. It costs a copy of every node's outputs (the same as
	// TgResult::Nodes).
	class TerrainGraphCache
	{
	public:
		struct Stats
		{
			int Ran = 0, Reused = 0;
			int Evicted = 0;              // entries dropped after the evaluation to fit the byte budget
			double Ms = 0.0;              // the whole evaluation
			std::map<std::string, double> RunMs;   // milliseconds of each node that ran, by id
		};

		void Clear();
		Stats LastStats() const;                    // of the last evaluation that used this cache
		// Size, Bytes and Generation never wait for an evaluation that is running (they read what the last one left), so the editor can show them.
		size_t Size() const;                       // nodes stored
		size_t Bytes() const;                      // what the stored outputs hold (their samples, placements, points), in bytes
		// 0 = no limit (the default). Applied after every evaluation, and at once to what is stored now (unless an evaluation is running: then
		// when it ends). Neither this nor SetPinned waits for a running evaluation.
		void SetByteBudget(size_t bytes);
		size_t ByteBudget() const;
		// A node whose outputs the budget never drops (the editor's previewed port); "" = none.
		void SetPinned(const std::string& nodeId);
		// A copy of the output `port` of node `nodeId` ("" = its first output) as the last evaluation left it. False when there is none or the
		// cache is busy (an evaluation is running).
		bool TryCopyOutput(const std::string& nodeId, const std::string& port, TgValue& out) const;
		std::vector<std::string> OutputPorts(const std::string& nodeId) const;   // empty when busy or unknown
		uint64_t Generation() const;               // goes up after every evaluation (a preview knows when to refresh)
		// The footprint the last evaluation was made over (TgContext::Size/Center); false before any.
		bool Footprint(float& size, glm::vec2& center) const;

		static size_t BytesOf(const TgValue& value);   // the estimate Bytes() adds up

	private:
		friend bool EvaluateTerrainGraph(const CompiledTerrainGraph&, const TerrainNodeRegistry&, const TgContext&, TgResult&, std::string&, TerrainGraphCache*);
		struct Entry
		{
			uint64_t Key = 0;
			std::map<std::string, TgValue> Outputs;
			double Ms = 0.0;      // what running the node cost the last time it ran
			size_t Bytes = 0;     // BytesOf its outputs
		};
		void FitBudget();     // (with the lock held)
		void Tally();         // m_Count and m_Bytes from the entries (with the lock held)
		mutable std::mutex m_Mutex;
		std::map<std::string, Entry> m_Entries;   // by node id
		Stats m_Stats;
		std::atomic<uint64_t> m_Generation{ 0 };
		std::atomic<size_t> m_Count{ 0 }, m_Bytes{ 0 };
		bool m_HasFootprint = false;
		float m_Size = 0.0f;
		glm::vec2 m_Center{ 0.0f };
		std::atomic<size_t> m_Budget{ 0 };
		mutable std::mutex m_PinMutex;   // (apart from m_Mutex, so the editor can pin while a world is being built)
		std::string m_Pinned;
	};
}
