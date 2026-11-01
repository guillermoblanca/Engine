#include "GB/World/Graph/ITerrainTarget.h"
#include "GB/World/Terrain.h"

namespace GB
{
	// GB::Heightmap has no in-place resize (its grid is fixed at construction, Terrain.h:20): the graph is
	// expected to be evaluated with a TgContext that already matches the wrapped Heightmap's resolution/size/
	// centre, so there is nothing to do here beyond documenting that contract. ApplyTerrainGraph below still
	// checks the sizes agree before writing a single height.
	void HeightmapTerrainTarget::Resize(uint32_t, float, const glm::vec2&) {}

	uint32_t HeightmapTerrainTarget::Resolution() const { return m_Heightmap.GetResolution(); }
	float HeightmapTerrainTarget::Size() const { return m_Heightmap.GetSize(); }
	glm::vec2 HeightmapTerrainTarget::Center() const { return (m_Heightmap.GetMin() + m_Heightmap.GetMax()) * 0.5f; }
	void HeightmapTerrainTarget::SetHeight(uint32_t i, uint32_t j, float height) { m_Heightmap.At(i, j) = height; }
	float HeightmapTerrainTarget::GetHeight(uint32_t i, uint32_t j) const { return m_Heightmap.At(i, j); }

	bool ApplyTerrainGraph(const TgResult& result, const std::string& slot, ITerrainTarget& target, std::string& error)
	{
		const auto it = result.Outputs.find(slot);
		if (it == result.Outputs.end()) { error = "graph has no output named '" + slot + "'"; return false; }

		const TgField& field = it->second;
		if (field.N != target.Resolution()) { error = "output '" + slot + "' resolution does not match the target"; return false; }

		// Read out fully before writing, so a target that throws or a future validation step leaves the
		// target untouched on failure (same two-phase shape as GB::ISaveHost::Validate/Commit).
		for (uint32_t j = 0; j < field.N; ++j)
			for (uint32_t i = 0; i < field.N; ++i)
				target.SetHeight(i, j, field.At(i, j));
		return true;
	}
}
