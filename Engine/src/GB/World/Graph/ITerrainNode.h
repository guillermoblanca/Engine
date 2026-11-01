#pragma once
#include "GB/World/Graph/TgField.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	struct TgNode;

	// Shared, read-only context a node needs beyond its inputs: the seed and footprint of the terrain being built.
	// Pure data, no engine/clock/randomness beyond the seed itself (docs/GRAFO_TERRENO.md).
	struct TgContext
	{
		uint32_t Seed = 1;
		uint32_t Resolution = 257;   // samples per side of newly created fields
		float Size = 256.0f;        // metres per side
		glm::vec2 Center{ 0.0f };
	};

	// Where a discrete object (a tree, a rock) ends up -- what GB::ScatterTrees (GB/World/Trees.h) already
	// returns as GB::TreePlacement, generalised past trees: a terrain graph node has no notion of a tree, only
	// of a position with a yaw, a scale and a variant index the caller gives meaning to.
	struct TgPlacement { glm::vec2 Position{ 0.0f }; float Yaw = 0.0f; float Scale = 1.0f; int Variant = 0; };
	struct TgPlacements { std::vector<TgPlacement> Items; };

	// A node that, besides (or instead of) a field, also places discrete objects -- "scatter" (Nodes/ScatterNode.h)
	// is the one example today. A second, optional interface (ISP) rather than a second method on every
	// ITerrainNode: a noise or erosion node has nothing to say here and does not need to know this type exists.
	class IScatterNode
	{
	public:
		virtual ~IScatterNode() = default;
		virtual bool Scatter(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs,
			TgPlacements& out, std::string& error) const = 0;
	};

	// One kind of terrain node (noise, erosion, a biome classifier...). A single purpose per implementation
	// (ISP): nothing here a node does not need. New kinds are added by writing a new ITerrainNode and
	// registering it (OCP) -- TerrainGraph.cpp never changes to add one.
	class ITerrainNode
	{
	public:
		virtual ~ITerrainNode() = default;

		// `inputs` are in the same order as the node's incoming edges (CompiledTerrainGraph::In). A null entry
		// means that input was left unconnected. Returns false (and fills `error`) when the node cannot run
		// with what it was given; the graph evaluation then stops and nothing downstream runs.
		virtual bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs,
			TgField& out, std::string& error) const = 0;

		// Null for every node except one that is also an IScatterNode (OCP: adding that capability to a node
		// does not touch this base class or any other node; a plain field node inherits this default untouched).
		virtual const IScatterNode* AsScatterNode() const { return nullptr; }
	};

	// Open catalogue of node types, keyed by TgNode::Type. A factory is registered once (usually from a static
	// initializer next to the node's implementation) and an instance is created and cached on first use.
	class TerrainNodeRegistry
	{
	public:
		using Factory = std::function<std::unique_ptr<ITerrainNode>()>;

		static TerrainNodeRegistry& Instance();

		void Register(std::string type, Factory factory);
		bool IsKnown(const std::string& type) const;
		const ITerrainNode* Find(const std::string& type) const;
		std::vector<std::string> KnownTypes() const;

	private:
		std::map<std::string, Factory> m_Factories;
		mutable std::map<std::string, std::unique_ptr<ITerrainNode>> m_Instances;
	};

	// Registers `Make` under `type` the first time any translation unit touches this global (construct it as a
	// file-scope static in the node's .cpp, next to its ITerrainNode implementation).
	struct TerrainNodeAutoRegister
	{
		TerrainNodeAutoRegister(const char* type, TerrainNodeRegistry::Factory factory)
		{
			TerrainNodeRegistry::Instance().Register(type, std::move(factory));
		}
	};
}
