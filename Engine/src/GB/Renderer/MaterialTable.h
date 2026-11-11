#pragma once

// Materials by id (render M4, T36; DESACOPLE_MATERIALES §2.2-2.3): the render frontend's side, CPU only, no GL.
//
// - MaterialDesc: what a material is for the shaders: its shading model, parameters and layer slots, as the MaterialData the GPU reads. The
//   textures are not in it yet: they are still bound per draw from the Material (M6 puts them in arrays and their layers in MaterialData::Tex).
// - PackMaterial: a render Material (the public API the eight sources of §1.1 fill) to its MaterialDesc. Two values depend on the renderer,
//   not on the material, and are packed already resolved, as the loose uniforms were: the ground marks' depth is 0 without a ground, and
//   alpha to coverage needs a multisampled target.
// - MaterialTable: MaterialDesc -> MaterialId, deduplicated by a hash of the content (equal descs get the same id; a collision is told
//   apart by comparing the bytes). The table keeps the packed data contiguous, ready to upload, and the range added since the last upload.
//   Ids are dense and stay valid until Clear.
#include "GB/Renderer/Material.h"
#include "GB/Renderer/MaterialData.h"
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace GB
{
	using MaterialId = uint32_t;

	struct MaterialDesc
	{
		MaterialData Data;

		uint32_t Model() const { return Data.Flags.y; }
		uint32_t LayerMask() const { return Data.Flags.x; }
		// MaterialData has no padding (static_asserts): the bytes are the content.
		bool operator==(const MaterialDesc& o) const { return std::memcmp(&Data, &o.Data, sizeof(MaterialData)) == 0; }
	};
	static_assert(sizeof(MaterialDesc) == sizeof(MaterialData));

	// What the renderer adds to a material when it packs it.
	struct MaterialContext
	{
		bool GroundMarks = false;   // the scene has a valid ground marks map (scene.Ground)
		bool Multisample = false;   // the scene target has more than one sample
	};

	// T46: whether a material has the foliage layer on: one of its values on, and opaque or masked (the translucent pass draws with the base
	// PBR program only; a Blend material keeps the values but not the layer). PackMaterial sets MaterialData::LayerFoliage by it and the
	// Renderer draws with its foliage program (GB_FOLIAGE) by it, so the two always agree.
	inline bool FoliageOn(const Material& m)
	{
		if (m.Alpha == AlphaMode::Blend) return false;
		return m.Translucency > 0.0f || m.ColorVariation > 0.0f || m.MacroVariation > 0.0f || m.NormalBlend > 0.0f ||
			(m.AlphaMipScale > 0.0f && m.Alpha == AlphaMode::Mask) || (m.OcclusionHeight > 0.0f && (m.BaseOcclusion > 0.0f || m.InteriorOcclusion > 0.0f));
	}

	inline MaterialDesc PackMaterial(const Material& m, const MaterialContext& context)
	{
		using D = MaterialData;
		MaterialDesc desc{};
		D& d = desc.Data;
		d.BaseColor = glm::vec4(m.Albedo, m.Opacity);
		d.Emissive = glm::vec4(m.Emissive, m.AlphaCutoff);
		d.Surface = glm::vec4(m.Metallic, m.Roughness, m.AO, m.OcclusionStrength);
		d.Uv = glm::vec4(m.UVScale, m.NormalStrength, m.NormalFlipX ? -1.0f : 1.0f, m.NormalFlipY ? -1.0f : 1.0f);

		uint32_t tex = 0;
		if (m.AlbedoMap) tex |= D::TexAlbedo;
		if (m.NormalMap) tex |= D::TexNormal;
		if (m.HeightMap) tex |= D::TexHeightMap;
		if (m.HeightMap && m.HeightScale != 0.0f) tex |= D::TexHeight;
		if (m.MetallicRoughnessMap) tex |= D::TexMR;
		if (m.OcclusionMap) tex |= D::TexOcclusion;
		if (m.EmissiveMap) tex |= D::TexEmissive;
		if (m.MacroMap) tex |= D::TexMacro;
		if (m.SurfaceNormalMap && m.MacroMap) tex |= D::TexTerrainNormals;
		// Transmission only on a Blend material; any other packs zeros, so the bytes (and ids) of every other material are as before T34.
		const bool transmission = m.Alpha == AlphaMode::Blend && m.Transmission > 0.0f;
		if (transmission && m.InteriorMap) tex |= D::TexInterior;
		d.Tex.x = tex;

		const bool trail = context.GroundMarks && m.TrailDepth > 0.0f;
		uint32_t layers = 0;
		if (m.Grain != 0.0f) layers |= D::LayerGrain;
		if (m.Moss > 0.0f) layers |= D::LayerMoss;
		if (m.Sand > 0.0f) layers |= D::LayerSand;
		if (m.WaterLine > -1e8f) layers |= D::LayerWaterLine;
		if (trail) layers |= D::LayerTrail;
		if (m.MacroMap) layers |= D::LayerMacro;
		if (transmission) layers |= D::LayerTransmission;
		// T46: the foliage layer only with one of its values on; else its slots stay zero (the tint's default is not), so every other material
		// packs the bytes it packed before.
		const bool foliage = FoliageOn(m);
		if (foliage) layers |= D::LayerFoliage;
		uint32_t flags = 0;
		if (m.FoliageNormals) flags |= D::FlagFoliageNormals;
		if (m.Alpha == AlphaMode::Mask && context.Multisample) flags |= D::FlagAlphaToCoverage;
		if (m.DoubleSided) flags |= D::FlagDoubleSided;
		d.Flags = glm::uvec4(layers, 0u, static_cast<uint32_t>(m.Alpha), flags);

		d.Wind = glm::vec4(m.WindSway, m.WindHeight, m.WindFlutter, m.WindBase);
		d.ModelParams = glm::vec4(m.HeightScale, m.CavityStrength, transmission ? m.Ior : 0.0f, transmission ? m.Transmission : 0.0f);
		d.Layer[D::SlotGrain] = glm::vec4(m.Grain, m.GrainScale, 0.0f, 0.0f);
		d.Layer[D::SlotMoss] = glm::vec4(m.MossColor, m.Moss);
		d.Layer[D::SlotSand] = glm::vec4(m.SandColor, m.Sand);
		d.Layer[D::SlotWaterLine] = glm::vec4(m.WaterLine, m.BaseY, 0.0f, 0.0f);
		d.Layer[D::SlotTrail] = glm::vec4(trail ? m.TrailDepth : 0.0f, m.TrailNormalBoost, 0.0f, 0.0f);
		d.Layer[D::SlotTrailTint] = glm::vec4(m.TrailTint, 0.0f);
		d.Layer[D::SlotMacro] = m.MacroRect;
		d.Layer[D::SlotTransmission] = transmission ? glm::vec4(m.Absorption, m.Thickness) : glm::vec4(0.0f);
		d.Layer[D::SlotInterior] = transmission && m.InteriorMap ? glm::vec4(m.InteriorTint, m.InteriorStrength) : glm::vec4(0.0f);
		d.Layer[D::SlotTransmissionExtra] = transmission
			? glm::vec4(m.InteriorMap ? m.InteriorDepth : 0.0f, m.Scatter, m.ShadowDensity, m.ThicknessVariation) : glm::vec4(0.0f);
		d.Layer[D::SlotFoliage] = foliage ? glm::vec4(m.TransmissionTint, m.Translucency) : glm::vec4(0.0f);
		d.Layer[D::SlotFoliageExtra] = foliage
			? glm::vec4(m.ColorVariation, m.NormalBlend, m.MacroVariation, m.Alpha == AlphaMode::Mask ? m.AlphaMipScale : 0.0f) : glm::vec4(0.0f);
		// T107: the occlusion only with a height to fade over (else both strengths pack 0, and the shader skips it)
		const bool occlusion = foliage && m.OcclusionHeight > 0.0f;
		d.Layer[D::SlotFoliageShade] = foliage
			? glm::vec4(occlusion ? m.BaseOcclusion : 0.0f, occlusion ? m.OcclusionHeight : 0.0f, occlusion ? m.InteriorOcclusion : 0.0f, m.BacklightSpread)
			: glm::vec4(0.0f);
		return desc;
	}

	class MaterialTable
	{
	public:
		static constexpr MaterialId None = ~0u;

		// A 64-bit hash of the desc's bytes (40 words, multiply and rotate).
		static uint64_t Hash(const MaterialDesc& desc)
		{
			uint64_t words[sizeof(MaterialData) / 8];
			std::memcpy(words, &desc.Data, sizeof(words));
			uint64_t h = 0x9E3779B97F4A7C15ull;
			for (uint64_t w : words)
			{
				h ^= w * 0xBF58476D1CE4E5B9ull;
				h = (h << 27 | h >> 37) * 0x94D049BB133111EBull;
			}
			return h ^ (h >> 31);
		}

		// The id of `desc`: the one it already has, or a new one at the end of the table.
		MaterialId Acquire(const MaterialDesc& desc)
		{
			++m_Lookups;
			const uint64_t h = Hash(desc);
			const auto head = m_Heads.find(h);
			MaterialId chain = head == m_Heads.end() ? None : head->second;
			for (MaterialId id = chain; id != None; id = m_Next[id])
				if (Matches(id, desc)) return id;
			const MaterialId id = static_cast<MaterialId>(m_Data.size());
			m_Data.push_back(desc.Data);
			m_Next.push_back(chain);
			m_Heads[h] = id;
			return id;
		}

		// Whether `id` is in the table and holds `desc` (a cheap check for a caller that remembers the id it got last time).
		bool Matches(MaterialId id, const MaterialDesc& desc) const
		{
			return id < m_Data.size() && std::memcmp(&m_Data[id], &desc.Data, sizeof(MaterialData)) == 0;
		}

		MaterialDesc Desc(MaterialId id) const { return { m_Data[id] }; }
		const MaterialData* Data() const { return m_Data.data(); }
		uint32_t Size() const { return static_cast<uint32_t>(m_Data.size()); }
		uint64_t Lookups() const { return m_Lookups; }   // Acquire calls (hashes) since the table was made

		// The ids added since MarkUploaded, [UploadedEnd(), Size()).
		uint32_t UploadedEnd() const { return m_Uploaded; }
		bool Dirty() const { return m_Uploaded < m_Data.size(); }
		void MarkUploaded() { m_Uploaded = Size(); }

		// Drops every material: the ids handed out are no longer valid (Generation moves).
		void Clear()
		{
			m_Data.clear();
			m_Next.clear();
			m_Heads.clear();
			m_Uploaded = 0;
			++m_Generation;
		}
		uint32_t Generation() const { return m_Generation; }

	private:
		std::vector<MaterialData> m_Data;     // by id: what the GPU buffer holds
		std::vector<MaterialId> m_Next;       // by id: the next id with the same hash (None ends the chain)
		std::unordered_map<uint64_t, MaterialId> m_Heads;   // hash -> the last id added with it
		uint32_t m_Uploaded = 0;
		uint32_t m_Generation = 0;
		uint64_t m_Lookups = 0;
	};
}
