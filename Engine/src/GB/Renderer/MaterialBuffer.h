#pragma once

// The materials of the renderer on the GPU (render M4, T36): a MaterialTable and the storage buffer that mirrors it at MaterialData::Binding,
// read by the PBR and shadow programs (`lib/material`) at the draw's `uMaterialId`.
//
// - Resolve(object, material) gives a draw its id. Each object of the scene remembers the id it got (by its index in scene.Objects): in the
//   same frame (the shadow pass, then the scene pass) it is reused as is; in a later frame the material is packed again and compared with
//   that entry (a 320 B memcmp), and only when it differs does it go through the table's hash. A new entry is uploaded before the draw that
//   needs it (UpdateBuffer of the new range; GL keeps the order with the draws before).
// - Prepare + Flush do the same before the passes, for the draws known then (Renderer::Render: the objects drawn alone), so the new
//   materials of a frame go up in one UpdateBuffer and the passes only set ids. The draws resolve again (the same frame: no packing) and
//   upload only what Prepare could not see (an instanced batch's first object, the first time its material appears).
// - The buffer is the renderer's own, not the frame's ring: the table outlives frames and only what is new is uploaded. It grows by doubling
//   (a new buffer with the whole table, bound again). The whole buffer is bound by range at offset 0 (aligned to any StorageBufferAlignment).
// - The table only grows (the selection highlight, the seasons, the editor's edits each add materials): past MaxMaterials it is emptied at
//   the start of a frame and refills with what that frame draws.
#include "GB/Renderer/MaterialTable.h"
#include "GB/Renderer/RHI/RHI.h"
#include "GB/Core/Log.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace GB
{
	class MaterialBuffer
	{
	public:
		static constexpr uint32_t MaxMaterials = 4096;   // 1.25 MiB of buffer
		static constexpr uint32_t FirstCapacity = 256;   // 80 KiB

		// Per frame, what the materials cost (RenderStats).
		struct Stats
		{
			uint32_t Resolves = 0;   // Resolve calls (draws of objects, shadow and scene)
			uint32_t Packed = 0;     // ... that packed the material (the object's first resolve in the frame)
			uint32_t Hashed = 0;     // ... that went through the table's hash (the object's material changed or was not known)
			uint32_t Added = 0;      // materials added to the table
			uint32_t Count = 0;      // materials in the table
		};

		MaterialBuffer() = default;
		~MaterialBuffer() { RHI::GetDevice().DestroyBuffer(m_Buffer); }
		MaterialBuffer(const MaterialBuffer&) = delete;
		MaterialBuffer& operator=(const MaterialBuffer&) = delete;

		// Before the frame's first draw: the context the materials are packed with, the number of scene objects, and the buffer bound.
		void BeginFrame(const MaterialContext& context, size_t objects)
		{
			m_Context = context;
			m_Stats = {};
			++m_Frame;
			if (m_Table.Size() > MaxMaterials)
				m_Table.Clear();
			if (m_Objects.size() < objects)
				m_Objects.resize(objects);
			if (!m_Buffer)
				Grow(FirstCapacity);
			else
				Bind();
			m_Stats.Count = m_Table.Size();
		}

		// The id the draw of scene object `object` (its index) with `material` sets in uMaterialId, uploaded.
		MaterialId Resolve(uint32_t object, const Material& material)
		{
			++m_Stats.Resolves;
			const MaterialId id = Find(object, material);
			if (m_Table.Dirty())
				Upload();
			return id;
		}

		// Resolve before the draws, without uploading; Flush uploads what the Prepare calls added.
		void Prepare(uint32_t object, const Material& material) { Find(object, material); }
		void Flush()
		{
			if (m_Table.Dirty())
				Upload();
		}

		const Stats& FrameStats() const { return m_Stats; }
		const MaterialTable& Table() const { return m_Table; }

	private:
		MaterialId Find(uint32_t object, const Material& material)
		{
			Remembered& r = m_Objects[object];
			const uint32_t generation = m_Table.Generation();
			if (r.Frame == m_Frame && r.Generation == generation)
				return r.Id;
			++m_Stats.Packed;
			const MaterialDesc desc = PackMaterial(material, m_Context);
			if (r.Id == MaterialTable::None || r.Generation != generation || !m_Table.Matches(r.Id, desc))
			{
				++m_Stats.Hashed;
				const uint32_t before = m_Table.Size();
				r.Id = m_Table.Acquire(desc);
				r.Generation = generation;
				if (m_Table.Size() != before)
					++m_Stats.Added;
			}
			r.Frame = m_Frame;
			m_Stats.Count = m_Table.Size();
			return r.Id;
		}

		struct Remembered
		{
			uint64_t Frame = 0;
			uint32_t Generation = 0;
			MaterialId Id = MaterialTable::None;
		};

		void Bind()
		{
			RHI::GetDevice().Commands().BindStorageBufferRange(MaterialData::Binding, m_Buffer, 0, static_cast<size_t>(m_Capacity) * sizeof(MaterialData));
		}

		// A new buffer of `capacity` entries with the whole table, bound.
		void Grow(uint32_t capacity)
		{
			RHI::Device& device = RHI::GetDevice();
			device.DestroyBuffer(m_Buffer);   // (GL keeps it alive for the draws already issued)
			m_Buffer = device.CreateBuffer(static_cast<size_t>(capacity) * sizeof(MaterialData), nullptr, true);
			m_Capacity = capacity;
			if (m_Table.Size() > 0)
				device.UpdateBuffer(m_Buffer, 0, static_cast<size_t>(m_Table.Size()) * sizeof(MaterialData), m_Table.Data());
			m_Table.MarkUploaded();
			Bind();
			GB_CORE_INFO("Renderer: material buffer of {} entries ({} in use)", capacity, m_Table.Size());
		}

		// The entries added since the last upload.
		void Upload()
		{
			if (m_Table.Size() > m_Capacity)
			{
				uint32_t capacity = std::max(m_Capacity, FirstCapacity);
				while (capacity < m_Table.Size()) capacity *= 2;
				Grow(capacity);
				return;
			}
			const uint32_t first = m_Table.UploadedEnd();
			RHI::GetDevice().UpdateBuffer(m_Buffer, static_cast<size_t>(first) * sizeof(MaterialData), static_cast<size_t>(m_Table.Size() - first) * sizeof(MaterialData),
				m_Table.Data() + first);
			m_Table.MarkUploaded();
		}

		MaterialTable m_Table;
		MaterialContext m_Context;
		std::vector<Remembered> m_Objects;   // by index in scene.Objects
		uint64_t m_Frame = 0;
		Stats m_Stats;
		RHI::BufferHandle m_Buffer;
		uint32_t m_Capacity = 0;
	};
}
