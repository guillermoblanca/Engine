#pragma once
// T78 (render M5a): the frame's objects as a compact list, built once at the start of Renderer::Render, and the culling of the camera and
// of the shadow map done in one walk over it (DESACOPLE_MATERIALES §2.6 and §3). Before, the scene pass and the shadow pass each walked
// scene.Objects, computed every bounding sphere again and grouped the instanced objects in a std::map made each frame.
//
// Plain CPU code (no GPU type is touched: the mesh pointer is only a sort key), so it runs without a window and can be tested on its own.
// The buffers are kept between frames: after the first frames nothing is allocated.
#include "RenderObject.h"
#include "Visibility.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>
#include <glm/geometric.hpp>

namespace GB
{
	// One object of the scene, as the culling needs it.
	struct RenderItem
	{
		enum Flag : uint32_t
		{
			Bounded = 1u << 0,       // has a bounding sphere (BoundsOf): without one it is never culled
			Batched = 1u << 1,       // drawn in its mesh's instanced batch (Instanced, not skinned, the whole mesh)
			Skinned = 1u << 2,       // has joint matrices
			Blend = 1u << 3,         // translucent: drawn back to front after the opaque objects, casts no shadow
			Masked = 1u << 4,        // alpha-tested
			CastsShadow = 1u << 5,   // drawn into the shadow map (today: everything but the translucent objects)
		};

		glm::vec3 Center{ 0.0f };   // the bounding sphere in world space (when Bounded)
		float Radius = 0.0f;
		float MaxDistance = 0.0f;   // the effective draw distance from the camera: the object's and the scene's, the smaller; 0 = no limit
		uint32_t Object = 0;        // its index in scene.Objects
		uint32_t Flags = 0;

		bool Has(Flag f) const { return (Flags & f) != 0; }
	};

	// An instanced object waiting for its batch: the batches are drawn by mesh address, and inside one in scene order (what the std::map
	// keyed by mesh pointer gave before T78, so the draw order and the image are the same).
	struct BatchEntry
	{
		const Mesh* MeshPtr = nullptr;
		uint32_t Object = 0;
	};

	inline void SortBatches(std::vector<BatchEntry>& entries)
	{
		std::sort(entries.begin(), entries.end(), [](const BatchEntry& a, const BatchEntry& b)
		{
			if (a.MeshPtr != b.MeshPtr) return std::less<const Mesh*>()(a.MeshPtr, b.MeshPtr);
			return a.Object < b.Object;
		});
	}

	class RenderList
	{
	public:
		// The objects with a mesh, in scene order. `drawDistance` is Scene::DrawDistance.
		void Build(const std::vector<RenderObject>& objects, float drawDistance)
		{
			m_Items.clear();
			m_Items.reserve(objects.size());
			for (size_t i = 0; i < objects.size(); ++i)
			{
				const RenderObject& obj = objects[i];
				if (!obj.MeshPtr)
					continue;
				RenderItem item;
				item.Object = static_cast<uint32_t>(i);
				if (BoundsOf(obj, item.Center, item.Radius))
					item.Flags |= RenderItem::Bounded;
				float maxDistance = obj.MaxDistance;
				if (drawDistance > 0.0f) maxDistance = maxDistance > 0.0f ? std::min(maxDistance, drawDistance) : drawDistance;
				item.MaxDistance = maxDistance;
				if (obj.Instanced && !obj.JointMatrices && obj.IndexCount == 0) item.Flags |= RenderItem::Batched;
				if (obj.JointMatrices) item.Flags |= RenderItem::Skinned;
				if (obj.Mat.Alpha == AlphaMode::Blend) item.Flags |= RenderItem::Blend;
				else item.Flags |= RenderItem::CastsShadow;
				if (obj.Mat.Alpha == AlphaMode::Mask) item.Flags |= RenderItem::Masked;
				m_Items.push_back(item);
			}
		}

		// One walk over the list: what the camera sees (its frustum and the draw distance from `cameraPos`) and, with `light` (the shadow
		// map's box; null without shadows), what casts into the shadow map. The criteria are not the same: an object out of the camera's
		// view or past its draw distance can still throw its shadow into view, so the shadow test is the light's frustum alone.
		void Cull(const Frustum& camera, const glm::vec3& cameraPos, const Frustum* light)
		{
			m_Visible.clear();
			m_Casters.clear();
			m_Culled = 0;
			for (uint32_t i = 0; i < static_cast<uint32_t>(m_Items.size()); ++i)
			{
				const RenderItem& item = m_Items[i];
				const bool bounded = item.Has(RenderItem::Bounded);
				bool seen = true;
				if (bounded)
				{
					const bool tooFar = item.MaxDistance > 0.0f && glm::distance(item.Center, cameraPos) - item.Radius > item.MaxDistance;
					seen = !tooFar && InFrustum(camera, item.Center, item.Radius);
				}
				if (seen) m_Visible.push_back(i);
				else ++m_Culled;
				if (light && item.Has(RenderItem::CastsShadow) && (!bounded || InFrustum(*light, item.Center, item.Radius)))
					m_Casters.push_back(i);
			}
		}

		const std::vector<RenderItem>& Items() const { return m_Items; }
		const std::vector<uint32_t>& Visible() const { return m_Visible; }   // indices into Items(), in scene order
		const std::vector<uint32_t>& Casters() const { return m_Casters; }   // ... the same, for the shadow map
		uint32_t Culled() const { return m_Culled; }                         // the objects with a mesh the camera does not draw

	private:
		std::vector<RenderItem> m_Items;
		std::vector<uint32_t> m_Visible, m_Casters;
		uint32_t m_Culled = 0;
	};
}
