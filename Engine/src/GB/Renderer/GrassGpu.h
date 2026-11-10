#pragma once
#include "GB/Renderer/RHI/RHI.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace GB
{
	class GrassField;

	// The GPU side of a GB::GrassField (GB/World/Grass.h, pure CPU): the instance buffer of its blades, read by the GrassRenderer. The field does
	// not know it exists: the render reads the world, never the other way round (docs/ARQUITECTURA_REVISION.md, H5).
	//
	// Keeping in step: the field bumps a revision whenever it rebuilds its blade list (Recenter, Regrow, RegrowAreas); Sync compares it and uploads
	// the list again. GetBuffer syncs too, so a field edited between two frames is drawn right without anyone calling Sync (the renderer reads the
	// buffer when it draws, as it did when the buffer lived in the field).
	//
	// Needs an OpenGL context (only when it uploads: making one is free). Owned by whoever draws the field (the editor's WorldInstance, the
	// Sandbox, the tests), put in Scene::Grass; it must not outlive the GrassField.
	class GrassGpu
	{
	public:
		explicit GrassGpu(const GrassField& field) : m_Field(&field) {}
		~GrassGpu();
		GrassGpu(const GrassGpu&) = delete;
		GrassGpu& operator=(const GrassGpu&) = delete;

		// Uploads the blades if the field changed since the last upload.
		void Sync() { GetBuffer(); }
		// The blade buffer (0 for a field without blades), uploaded first if the field changed.
		uint32_t GetBuffer() const { return GetHandle().Id; }   // the device's id (0: no blades)
		RHI::BufferHandle GetHandle() const;
		const GrassField& GetField() const { return *m_Field; }

		// T46: how the field's blades take the light, the PBR foliage layer's terms (Material::Translucency...; MATERIALES.md, "Follaje").
		// A render value, not the field's (GrassParams is the world's): whoever owns the GrassGpu sets it. All 0: the grass as before.
		struct Look
		{
			float Translucency = 0.0f;                         // the sun through the blades, on top of their own back light
			glm::vec3 TransmissionTint{ 0.85f, 1.0f, 0.42f };  // times the blade's colour
			float ColorVariation = 0.0f;                       // hue, saturation and brightness by clumps of ~0.6 m
			float MacroVariation = 0.0f;                       // low-frequency world patches, yellowish to olive
		};
		Look Shading;

	private:
		const GrassField* m_Field = nullptr;
		mutable RHI::BufferHandle m_Buffer;
		mutable uint64_t m_Revision = 0;   // the field's revision last uploaded (0: never; a field starts at 1)
	};
}
