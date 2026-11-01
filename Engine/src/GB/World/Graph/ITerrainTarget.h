#pragma once
#include "GB/World/Graph/TerrainGraph.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace GB
{
	class Heightmap;

	// What a terrain graph needs to write its result into, without knowing GB::Heightmap/Terrain (DIP, modelled
	// on GB::IGraphTarget in GB/Animation/AnimGraph.h). A test can give the graph an in-memory target in a few
	// lines; the engine gives it one that wraps a live Heightmap.
	class ITerrainTarget
	{
	public:
		virtual ~ITerrainTarget() = default;

		virtual void Resize(uint32_t resolution, float size, const glm::vec2& center) = 0;
		virtual uint32_t Resolution() const = 0;
		virtual float Size() const = 0;
		virtual glm::vec2 Center() const = 0;
		virtual void SetHeight(uint32_t i, uint32_t j, float height) = 0;
		virtual float GetHeight(uint32_t i, uint32_t j) const = 0;
	};

	// Self-contained target with no engine dependency at all: what the tests use.
	class InMemoryTerrainTarget final : public ITerrainTarget
	{
	public:
		void Resize(uint32_t resolution, float size, const glm::vec2& center) override { m_Field.Resize(resolution, size, center); }
		uint32_t Resolution() const override { return m_Field.N; }
		float Size() const override { return m_Field.Size; }
		glm::vec2 Center() const override { return m_Field.Center; }
		void SetHeight(uint32_t i, uint32_t j, float height) override { m_Field.At(i, j) = height; }
		float GetHeight(uint32_t i, uint32_t j) const override { return m_Field.At(i, j); }

		const TgField& Field() const { return m_Field; }

	private:
		TgField m_Field;
	};

	// The real adapter: wraps a live GB::Heightmap. TerrainGraph.h/.cpp and ITerrainNode.h/.cpp never include
	// GB/World/Terrain.h, so the graph's core and its node interface stay free of the renderer headers
	// Terrain.h pulls in; only this adapter, and a node that genuinely needs a Heightmap (erosion, to reuse
	// GB::HydraulicErosion::Run), include it.
	class HeightmapTerrainTarget final : public ITerrainTarget
	{
	public:
		explicit HeightmapTerrainTarget(Heightmap& heightmap) : m_Heightmap(heightmap) {}

		void Resize(uint32_t resolution, float size, const glm::vec2& center) override;
		uint32_t Resolution() const override;
		float Size() const override;
		glm::vec2 Center() const override;
		void SetHeight(uint32_t i, uint32_t j, float height) override;
		float GetHeight(uint32_t i, uint32_t j) const override;

	private:
		Heightmap& m_Heightmap;
	};

	// Applies a graph's result to a target; nothing is written until the whole field has been read out, so a
	// target never ends up half updated (same "nothing changes on failure" shape as GB::ISaveHost::Validate/
	// Commit in GB/Save/SaveWorld.h). `slot` is the TgResult::Outputs key to apply (normally "height").
	bool ApplyTerrainGraph(const TgResult& result, const std::string& slot, ITerrainTarget& target, std::string& error);
}
