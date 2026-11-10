#include "GB/World/Graph/TgHash.h"

namespace GB
{
	namespace
	{
		template <typename T> uint64_t Pod(const T& v, uint64_t h) { return TgHashBytes(&v, sizeof v, h); }
		uint64_t Vec2(const glm::vec2& v, uint64_t h) { return Pod(v.y, Pod(v.x, h)); }
		uint64_t Floats(const std::vector<float>& v, uint64_t h)
		{
			h = Pod(static_cast<uint64_t>(v.size()), h);
			return v.empty() ? h : TgHashBytes(v.data(), v.size() * sizeof(float), h);
		}
	}

	uint64_t TgHashBytes(const void* data, size_t size, uint64_t hash)
	{
		const unsigned char* p = static_cast<const unsigned char*>(data);
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= p[i];
			hash *= 1099511628211ull;
		}
		return hash;
	}

	uint64_t TgHashString(const std::string& text, uint64_t hash)
	{
		hash = Pod(static_cast<uint64_t>(text.size()), hash);
		return TgHashBytes(text.data(), text.size(), hash);
	}

	uint64_t TgHashField(const TgField& f, uint64_t h)
	{
		h = Pod(f.N, h);
		h = Pod(f.Size, h);
		h = Vec2(f.Center, h);
		return Floats(f.Values, h);
	}

	uint64_t TgHashValue(const TgValue& v, uint64_t h)
	{
		h = Pod(static_cast<int>(v.Kind), h);
		switch (v.Kind)
		{
		case TgPortKind::Field: return TgHashField(v.Field, h);
		case TgPortKind::Placements:
			h = Pod(static_cast<uint64_t>(v.Placements.Items.size()), h);
			for (const TgPlacement& p : v.Placements.Items) h = Pod(p.Variant, Pod(p.Scale, Pod(p.Yaw, Vec2(p.Position, h))));
			return h;
		case TgPortKind::Paths:
			h = Pod(static_cast<uint64_t>(v.Paths.Items.size()), h);
			for (const TgPath& p : v.Paths.Items)
			{
				h = Pod(static_cast<uint64_t>(p.Points.size()), h);
				for (const glm::vec2& q : p.Points) h = Vec2(q, h);
				h = Floats(p.Values, h);
			}
			return h;
		case TgPortKind::Water:
		{
			const LakeBody& lake = v.Water.Lake();
			h = Pod(static_cast<unsigned char>(lake.Present ? 1 : 0), h);
			h = Vec2(lake.Center, h);
			h = Pod(lake.Radius, Pod(lake.Level, h));
			h = Pod(lake.Outline.Irregularity, Pod(lake.Outline.Phase1, Pod(lake.Outline.Phase2, Pod(lake.Outline.Phase3, h))));
			const RiverData* river = v.Water.River();
			h = Pod(static_cast<unsigned char>(river ? 1 : 0), h);
			if (river)
			{
				h = Pod(static_cast<uint64_t>(river->Points.size()), h);
				for (const glm::vec3& p : river->Points) h = Pod(p.z, Pod(p.y, Pod(p.x, h)));
				h = Floats(river->Speed, h);
				h = Floats(river->HalfWidths, h);
				h = Pod(river->HalfWidth, Pod(river->Depth, Pod(river->MouthLength, h)));
			}
			// the lakes lake_fill adds (E1), and a main lake with a mask: hashed only when there are any, so the water of every graph without
			// them hashes as it always did
			auto lakeExtras = [&](const LakeBody& l)
			{
				h = Pod(l.Mask.Spacing, Vec2(l.Mask.Origin, h));
				h = Pod(l.Mask.W, Pod(l.Mask.H, h));
				h = Pod(static_cast<uint64_t>(l.Mask.Cells.size()), h);
				if (!l.Mask.Cells.empty()) h = TgHashBytes(l.Mask.Cells.data(), l.Mask.Cells.size(), h);
				h = Pod(l.Area, Pod(l.Volume, Pod(l.MaxDepth, Vec2(l.Outlet, h))));
			};
			if (!lake.Mask.Empty()) lakeExtras(lake);
			if (!v.Water.MoreLakes().empty())
			{
				h = Pod(static_cast<uint64_t>(v.Water.MoreLakes().size()), h);
				for (const LakeBody& l : v.Water.MoreLakes())
				{
					h = Pod(static_cast<unsigned char>(l.Present ? 1 : 0), h);
					h = Vec2(l.Center, h);
					h = Pod(l.Radius, Pod(l.Level, h));
					h = Pod(l.Outline.Irregularity, Pod(l.Outline.Phase1, Pod(l.Outline.Phase2, Pod(l.Outline.Phase3, h))));
					lakeExtras(l);
				}
			}
			return h;
		}
		default: return h;
		}
	}

	uint64_t TgHashResult(const TgResult& r)
	{
		uint64_t h = kTgHashSeed;
		for (const auto& [node, ports] : r.Nodes)
		{
			h = TgHashString(node, h);
			for (const auto& [port, value] : ports) h = TgHashValue(value, TgHashString(port, h));
		}
		for (const auto& [name, f] : r.Outputs) h = TgHashField(f, TgHashString(name, h));
		for (const auto& [name, p] : r.PlacementOutputs) { TgValue v; v.Kind = TgPortKind::Placements; v.Placements = p; h = TgHashValue(v, TgHashString(name, h)); }
		for (const auto& [name, p] : r.PathOutputs) { TgValue v; v.Kind = TgPortKind::Paths; v.Paths = p; h = TgHashValue(v, TgHashString(name, h)); }
		for (const auto& [name, w] : r.WaterOutputs) { TgValue v; v.Kind = TgPortKind::Water; v.Water = w; h = TgHashValue(v, TgHashString(name, h)); }
		return h;
	}
}
