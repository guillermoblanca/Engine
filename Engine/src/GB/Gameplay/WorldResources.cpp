#include "GB/Gameplay/WorldResources.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace GB
{
	namespace
	{
		// Integer hashing only, so the choice of cells is the same on every platform; the floats come in where the ground is read.
		uint32_t Mix(uint32_t h)
		{
			h ^= h >> 16; h *= 0x7feb352du;
			h ^= h >> 15; h *= 0x846ca68bu;
			return h ^ (h >> 16);
		}
		uint32_t Hash(int i, int j, uint32_t salt)
		{
			return Mix(static_cast<uint32_t>(i) * 0x9E3779B1u ^ Mix(static_cast<uint32_t>(j) * 0x85EBCA77u ^ salt));
		}
		float Unit(uint32_t h) { return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f; }   // [0, 1)
		uint32_t HashName(const std::string& s)
		{
			uint32_t h = 2166136261u;
			for (const char c : s) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
			return h;
		}

		// Value noise on a lattice of `cell` metres: the groves and veins of Clump.
		float Clumps(float x, float z, float cell, uint32_t salt)
		{
			const float fx = x / cell, fz = z / cell;
			const float ix = std::floor(fx), iz = std::floor(fz);
			float u = fx - ix, v = fz - iz;
			u = u * u * (3.0f - 2.0f * u); v = v * v * (3.0f - 2.0f * v);
			const int i = static_cast<int>(ix), j = static_cast<int>(iz);
			auto h = [&](int di, int dj) { return Unit(Hash(i + di, j + dj, salt)); };
			const float a = h(0, 0) + (h(1, 0) - h(0, 0)) * u, b = h(0, 1) + (h(1, 1) - h(0, 1)) * u;
			return a + (b - a) * v;
		}

		// The nodes placed so far, bucketed on a grid so that checking the neighbours of a candidate is cheap.
		class Occupancy
		{
		public:
			explicit Occupancy(float cell) : m_Cell(cell) {}
			bool Free(const glm::vec2& p, float spacing) const
			{
				// far enough for the widest spacing already in: a node that asked for 9 m must be seen from two cells away too
				const int reach = static_cast<int>(std::ceil(std::max(spacing, m_Widest) / m_Cell));
				const int ci = Cell(p.x), cj = Cell(p.y);
				for (int dj = -reach; dj <= reach; ++dj)
					for (int di = -reach; di <= reach; ++di)
					{
						auto it = m_Buckets.find(Key(ci + di, cj + dj));
						if (it == m_Buckets.end()) continue;
						for (const glm::vec3& q : it->second)   // x, z and the spacing that node asked for: the larger of the two wins
						{
							const float need = std::max(spacing, q.z);
							const glm::vec2 d = glm::vec2(q.x, q.y) - p;
							if (d.x * d.x + d.y * d.y < need * need) return false;
						}
					}
				return true;
			}
			void Add(const glm::vec2& p, float spacing)
			{
				m_Buckets[Key(Cell(p.x), Cell(p.y))].push_back({ p.x, p.y, spacing });
				m_Widest = std::max(m_Widest, spacing);
			}
		private:
			int Cell(float v) const { return static_cast<int>(std::floor(v / m_Cell)); }
			static int64_t Key(int i, int j) { return (static_cast<int64_t>(i) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(j)); }
			float m_Cell;
			float m_Widest = 0.0f;
			std::unordered_map<int64_t, std::vector<glm::vec3>> m_Buckets;
		};

	}

	std::vector<ResourceSpot> PlanWorldResources(const ResourceGround& ground, const std::map<std::string, ResourceNodeDef>& nodes, uint32_t seed, float density)
	{
		std::vector<ResourceSpot> spots;
		if (!ground.Height || !ground.Biome || density <= 0.0f) return spots;
		std::vector<const ResourceNodeDef*> order;
		for (const auto& [id, def] : nodes)
			if (def.Spawn.Placed()) order.push_back(&def);
		// rarest first (then by id, so the order never depends on anything but the data)
		std::stable_sort(order.begin(), order.end(), [](const ResourceNodeDef* a, const ResourceNodeDef* b) { return a->Spawn.PerHectare < b->Spawn.PerHectare; });

		Occupancy taken(8.0f);
		Occupancy blockers(8.0f);
		for (const ResourceGround::Blocker& b : ground.Blockers) blockers.Add(b.At, b.Radius);
		const float margin = 2.0f;   // never right at the edge of the map
		const glm::vec2 lo = ground.Min + glm::vec2(margin), hi = ground.Max - glm::vec2(margin);
		if (hi.x <= lo.x || hi.y <= lo.y) return spots;

		for (const ResourceNodeDef* def : order)
		{
			const ResourceSpawnDef& sp = def->Spawn;
			const uint32_t salt = Mix(seed * 0x27D4EB2Fu ^ HashName(def->Id));
			// One candidate per cell of `Spacing` metres, jittered inside it, kept with the probability that gives PerHectare on average.
			const float cell = sp.Spacing;
			// the clumping leaves out a share Clump of the ground, so the rest gets that much more: PerHectare stays the average over the matching ground
			const float keep = std::min(1.0f, sp.PerHectare * density * cell * cell / 10000.0f / (1.0f - sp.Clump));
			const int most = sp.Max > 0 ? std::max(1, static_cast<int>(std::lround(static_cast<float>(sp.Max) * density))) : 0;
			const int ni = static_cast<int>(std::floor((hi.x - lo.x) / cell)), nj = static_cast<int>(std::floor((hi.y - lo.y) / cell));
			// The kept cells are visited in the order of their hash, not row by row: a type with a cap spreads over the whole map instead of filling
			// the first rows (ties, which a 24-bit hash can have, go by row and column).
			struct Cell { uint32_t H; int I, J; };
			std::vector<Cell> cells;
			for (int j = 0; j < nj; ++j)
				for (int i = 0; i < ni; ++i)
					if (const uint32_t h = Hash(i, j, salt); Unit(h) < keep) cells.push_back({ h, i, j });
			std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return a.H != b.H ? a.H < b.H : a.J != b.J ? a.J < b.J : a.I < b.I; });
			int placed = 0;
			for (const Cell& c : cells)
				{
					if (most > 0 && placed >= most) break;
					const uint32_t h = c.H;
					const int i = c.I, j = c.J;
					const glm::vec2 p = lo + (glm::vec2(static_cast<float>(i), static_cast<float>(j)) + glm::vec2(0.15f + 0.7f * Unit(Mix(h ^ 1u)), 0.15f + 0.7f * Unit(Mix(h ^ 2u)))) * cell;
					if (sp.Clump > 0.0f && Clumps(p.x, p.y, 28.0f, salt ^ 0xC1u) < sp.Clump) continue;
					const std::string biome = ground.Biome(p.x, p.y);
					if (std::find(sp.Biomes.begin(), sp.Biomes.end(), biome) == sp.Biomes.end()) continue;
					if (ground.SlopeDegrees && ground.SlopeDegrees(p.x, p.y) > sp.MaxSlope) continue;
					if (sp.WaterWithin > 0.0f && !(ground.WaterDistance && ground.WaterDistance(p.x, p.y) <= sp.WaterWithin)) continue;
					if (ground.WaterDistance && ground.WaterDistance(p.x, p.y) < 1.0f) continue;   // never with its feet in the water
					if (!ground.Blockers.empty() && !blockers.Free(p, 1.0f)) continue;            // a node's own footprint against each tree or rock's radius
					if (!taken.Free(p, sp.Spacing)) continue;
					taken.Add(p, sp.Spacing);
					spots.push_back({ def->Id, glm::vec3(p.x, ground.Height(p.x, p.y), p.y), 360.0f * Unit(Mix(h ^ 3u)) });
					++placed;
				}
		}
		return spots;
	}

	std::map<std::string, int> CountSpots(const std::vector<ResourceSpot>& spots)
	{
		std::map<std::string, int> counts;
		for (const ResourceSpot& s : spots) ++counts[s.Node];
		return counts;
	}

	ResourceGround ResourceGroundOf(const GeneratedWorld& world)
	{
		ResourceGround g;
		const GeneratedWorld* w = &world;
		const Terrain* t = world.TerrainRef;
		const Heightmap& hm = t->GetHeightmap();
		g.Min = hm.GetMin();
		g.Max = hm.GetMax();
		g.Height = [t](float x, float z) { return t->GetHeight(x, z); };
		g.Biome = [w](float x, float z) { return std::string(BiomeName(w->BiomeAt(x, z))); };
		g.SlopeDegrees = [t](float x, float z)
		{
			const glm::vec3 n = t->GetHeightmap().GetNormal(x, z);
			return glm::degrees(std::acos(glm::clamp(n.y, -1.0f, 1.0f)));
		};
		// The biome (above) and the distance to the water come from the map baked with the world: a true distance to the water as it lies (the river's
		// banks, the irregular shore of the lake), and a read of a grid instead of a walk along the whole river per question.
		// The map is sampled every few metres: a hollow below the lake's level narrower than a sample (the steep valleys of the alpine world) is
		// water to the walkers (MakeTerrainCharacterWorld) and not to the map, and a node could stand in it 2 m deep (found by the H20 sweep: alpine
		// seed 3, a flint outcrop nobody could reach). The point itself is asked as the walkers ask it.
		g.WaterDistance = [w, t](float x, float z)
		{
			const float ground = t->GetHeight(x, z);
			if ((t->HasLake() && ground < t->GetWaterLevel()) || t->GetRiverLevelAt(x, z) > ground) return 0.0f;
			return w->WaterDistanceAt(x, z);
		};
		for (const TreePlacement& tree : world.Trees) g.Blockers.push_back({ glm::vec2(tree.Position.x, tree.Position.z), 1.2f * tree.Scale });
		for (const RockPlacement& rock : world.Rocks)
		{
			const float size = rock.Variant >= 0 && rock.Variant < static_cast<int>(world.RockRecipes.size()) ? world.RockRecipes[static_cast<size_t>(rock.Variant)].Size : 1.0f;
			g.Blockers.push_back({ glm::vec2(rock.Position.x, rock.Position.z), size * rock.Scale * 1.3f });
		}
		return g;
	}
}
