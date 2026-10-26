#include "Grass.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>

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

	GrassField::~GrassField()
	{
		if (m_Buffer)
			glDeleteBuffers(1, &m_Buffer);
	}

	void GrassField::Generate()
	{
		const GrassParams& p = Params;
		const int n = std::max(1, static_cast<int>(std::ceil(p.Size / p.ChunkSize)));
		const glm::vec2 origin = p.Center - glm::vec2(p.Size * 0.5f);
		const float cosMaxSlope = std::cos(glm::radians(p.MaxSlopeDegrees));

		for (int cz = 0; cz < n; ++cz)
		{
			for (int cx = 0; cx < n; ++cx)
			{
				Rng rng(static_cast<uint64_t>(p.Seed) * 7919u + static_cast<uint64_t>(cz) * 104729u + static_cast<uint64_t>(cx) * 1299709u + 17u);
				const glm::vec2 lo = origin + glm::vec2(static_cast<float>(cx), static_cast<float>(cz)) * p.ChunkSize;
				const uint32_t target = static_cast<uint32_t>(p.ChunkSize * p.ChunkSize * p.Density);
				GrassChunk chunk;
				chunk.First = static_cast<uint32_t>(m_Blades.size());
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
					m_Blades.push_back(b);
					minY = std::min(minY, y);
					maxY = std::max(maxY, y);
				}
				chunk.Count = static_cast<uint32_t>(m_Blades.size()) - chunk.First;
				if (chunk.Count == 0)
					continue;
				chunk.Center = { lo.x + p.ChunkSize * 0.5f, (minY + maxY) * 0.5f + 0.5f, lo.y + p.ChunkSize * 0.5f };
				chunk.Radius = std::sqrt(2.0f) * p.ChunkSize * 0.5f + (maxY - minY) * 0.5f + p.Height.y;
				m_Chunks.push_back(chunk);
			}
		}
	}

	size_t GrassField::CountVisibleBlades(const glm::vec3& cameraPosition) const
	{
		size_t total = 0;
		for (const GrassChunk& c : m_Chunks)
			if (glm::length(c.Center - cameraPosition) - c.Radius <= Params.DrawDistance)
				total += c.Count;
		return total;
	}

	uint32_t GrassField::GetBuffer() const
	{
		if (!m_Buffer && !m_Blades.empty())
		{
			glCreateBuffers(1, &m_Buffer);
			glNamedBufferStorage(m_Buffer, static_cast<GLsizeiptr>(m_Blades.size() * sizeof(GrassBlade)), m_Blades.data(), 0);
		}
		return m_Buffer;
	}
}
