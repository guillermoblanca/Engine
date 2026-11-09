#include "Grass.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		struct Rng
		{
			uint64_t s;
			explicit Rng(uint64_t seed) : s(0x9E3779B97F4A7C15ull ^ (seed * 0xBF58476D1CE4E5B9ull)) { next(); next(); }
			float next()
			{
				s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
				return static_cast<float>(((s * 0x2545F4914F6CDD1Dull) >> 40) & 0xFFFFFF) / 16777216.0f;
			}
		};
	}

	GrassField::GrassField(const GrassParams& params) : Params(params)
	{
		Generate();
	}

	void GrassField::Generate()
	{
		const GrassParams& p = Params;
		m_ChunksPerSide = std::max(1, static_cast<int>(std::ceil(p.Size / p.ChunkSize)));
		m_Anchor = p.Center - glm::vec2(p.Size * 0.5f);
		m_First = { 0, 0 };
		for (int cz = 0; cz < m_ChunksPerSide; ++cz)
			for (int cx = 0; cx < m_ChunksPerSide; ++cx)
				m_Cache[{ cx, cz }] = GrowChunk(cx, cz);
		Assemble();
	}

	// One chunk, from the seed of its place on the grid (a negative index, west or south of where the field began, is a place like any other).
	GrassField::ChunkBlades GrassField::GrowChunk(int cx, int cz) const
	{
		const GrassParams& p = Params;
		const float cosMaxSlope = std::cos(glm::radians(p.MaxSlopeDegrees));
		ChunkBlades out;
		Rng rng(static_cast<uint64_t>(p.Seed) * 7919u + static_cast<uint64_t>(static_cast<int64_t>(cz)) * 104729u + static_cast<uint64_t>(static_cast<int64_t>(cx)) * 1299709u + 17u);
		const glm::vec2 lo = m_Anchor + glm::vec2(static_cast<float>(cx), static_cast<float>(cz)) * p.ChunkSize;
		const uint32_t target = static_cast<uint32_t>(p.ChunkSize * p.ChunkSize * p.Density);
		float minY = 1e9f, maxY = -1e9f;

		for (uint32_t i = 0; i < target; ++i)
		{
			const float x = lo.x + rng.next() * p.ChunkSize, z = lo.y + rng.next() * p.ChunkSize;
			// Draw every random number first so the sequence does not depend on rejections.
			const float yaw = rng.next() * 6.2831853f, hRand = rng.next(), wRand = rng.next(), seed = rng.next(), keep = rng.next();
			if (p.DensityFn && keep > p.DensityFn(x, z))
				continue;
			if (p.NormalFn && p.NormalFn(x, z).y < cosMaxSlope)
				continue;
			const float y = p.HeightFn ? p.HeightFn(x, z) : 0.0f;
			GrassBlade b;
			b.PositionYaw = { x, y, z, yaw };
			b.Shape = { glm::mix(p.Height.x, p.Height.y, hRand * hRand), p.Width * (0.7f + 0.6f * wRand), seed, p.Lean * (0.5f + rng.next()) };
			if (p.HeightScaleFn) b.Shape.x *= std::max(p.HeightScaleFn(x, z), 0.05f);
			if (p.TintFn) b.Tint = p.TintFn(x, z);
			out.Blades.push_back(b);
			minY = std::min(minY, y);
			maxY = std::max(maxY, y);
		}
		out.Empty = out.Blades.empty();
		if (out.Empty)
			return out;
		const float tallest = p.HeightScaleFn ? p.Height.y * 2.0f : p.Height.y;
		out.Chunk.Center = { lo.x + p.ChunkSize * 0.5f, (minY + maxY) * 0.5f + 0.5f, lo.y + p.ChunkSize * 0.5f };
		out.Chunk.Radius = std::sqrt(2.0f) * p.ChunkSize * 0.5f + (maxY - minY) * 0.5f + tallest;
		out.Chunk.Count = static_cast<uint32_t>(out.Blades.size());
		return out;
	}

	// The window's chunks, row by row, into one blade list (the order of a field that never moved is the order it was grown in).
	void GrassField::Assemble()
	{
		m_Blades.clear();
		m_Chunks.clear();
		for (int cz = m_First.y; cz < m_First.y + m_ChunksPerSide; ++cz)
			for (int cx = m_First.x; cx < m_First.x + m_ChunksPerSide; ++cx)
			{
				const ChunkBlades& c = m_Cache.at({ cx, cz });
				if (c.Empty)
					continue;
				GrassChunk chunk = c.Chunk;
				chunk.First = static_cast<uint32_t>(m_Blades.size());
				m_Blades.insert(m_Blades.end(), c.Blades.begin(), c.Blades.end());
				m_Chunks.push_back(chunk);
			}
		++m_Revision;
	}

	void GrassField::Regrow()
	{
		m_Cache.clear();
		for (int cz = m_First.y; cz < m_First.y + m_ChunksPerSide; ++cz)
			for (int cx = m_First.x; cx < m_First.x + m_ChunksPerSide; ++cx)
				m_Cache[{ cx, cz }] = GrowChunk(cx, cz);
		Assemble();
	}

	bool GrassField::RegrowAreas(const std::vector<glm::vec4>& rects)
	{
		const float s = Params.ChunkSize;
		bool any = false;
		for (auto& [key, chunk] : m_Cache)
		{
			const glm::vec2 lo = m_Anchor + glm::vec2(static_cast<float>(key.first), static_cast<float>(key.second)) * s;
			bool touched = false;
			for (const glm::vec4& r : rects) touched = touched || !(lo.x > r.z || lo.y > r.w || lo.x + s < r.x || lo.y + s < r.y);
			if (!touched) continue;
			chunk = GrowChunk(key.first, key.second);
			any = true;
		}
		if (any) Assemble();
		return any;
	}

	bool GrassField::Recenter(const glm::vec2& center)
	{
		const glm::vec2 corner = (center - glm::vec2(Params.Size * 0.5f) - m_Anchor) / Params.ChunkSize;
		const glm::ivec2 first(static_cast<int>(std::lround(corner.x)), static_cast<int>(std::lround(corner.y)));
		if (first == m_First)
			return false;
		m_First = first;
		for (auto it = m_Cache.begin(); it != m_Cache.end();)   // forget what fell out of the window
		{
			const auto [cx, cz] = it->first;
			const bool inside = cx >= first.x && cx < first.x + m_ChunksPerSide && cz >= first.y && cz < first.y + m_ChunksPerSide;
			it = inside ? std::next(it) : m_Cache.erase(it);
		}
		for (int cz = first.y; cz < first.y + m_ChunksPerSide; ++cz)
			for (int cx = first.x; cx < first.x + m_ChunksPerSide; ++cx)
				if (!m_Cache.count({ cx, cz }))
					m_Cache[{ cx, cz }] = GrowChunk(cx, cz);
		Assemble();
		return true;
	}

	size_t GrassField::CountVisibleBlades(const glm::vec3& cameraPosition) const
	{
		size_t total = 0;
		for (const GrassChunk& c : m_Chunks)
			if (glm::length(c.Center - cameraPosition) - c.Radius <= Params.DrawDistance)
				total += c.Count;
		return total;
	}
}
