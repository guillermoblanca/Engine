#include "GB/World/Regions.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		constexpr int kBiomes = 7;

		std::string Lower(std::string s)
		{
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		// Where a point lies from the middle of the map, as a compass word ("North" is -z, "East" is +x); "Central" near the middle.
		std::string Direction(const glm::vec2& offset, float halfSize)
		{
			if (glm::length(offset) < 0.3f * halfSize) return "Central";
			static const char* kNames[8] = { "East", "Southeast", "South", "Southwest", "West", "Northwest", "North", "Northeast" };
			const float angle = std::atan2(offset.y, offset.x);   // 0 = +x (east), +pi/2 = +z (south)
			int octant = static_cast<int>(std::lround(angle / (3.14159265f / 4.0f)));
			octant = ((octant % 8) + 8) % 8;
			return kNames[octant];
		}

		void Mix(uint64_t& h, uint64_t v) { h ^= v; h *= 1099511628211ull; }
		void Mix(uint64_t& h, const std::string& s) { for (const char c : s) Mix(h, static_cast<unsigned char>(c)); Mix(h, 0xffu); }
	}

	RegionRules DefaultRegionRules()
	{
		RegionRules r;
		r.Danger = { { "Water", 0.2f }, { "Shore", 0.1f }, { "Meadow", 0.05f }, { "Forest", 0.25f }, { "Rock", 0.35f }, { "Snow", 0.5f }, { "Dunes", 0.3f } };
		r.Names = { { "Water", "Lake" }, { "Shore", "Shore" }, { "Meadow", "Meadows" }, { "Forest", "Woods" }, { "Rock", "Crags" }, { "Snow", "Snowfields" }, { "Dunes", "Dunes" } };
		return r;
	}

	RegionMap RegionMap::Build(const GeneratedWorld& world, const RegionRules& rules, const std::vector<RegionThing>& things)
	{
		return Build(world.Maps, rules, things);
	}

	RegionMap RegionMap::Build(const BiomeMap& maps, const RegionRules& rulesIn, const std::vector<RegionThing>& things)
	{
		RegionMap out;
		out.m_Rules = rulesIn;
		if (maps.Empty() || maps.N < 2 || maps.Cell <= 0.0f) return out;
		RegionRules& rules = out.m_Rules;
		rules.Tile = std::max(rules.Tile, maps.Cell);
		rules.ScoutCell = std::max(rules.ScoutCell, maps.Cell);
		rules.BlockTiles = std::max(rules.BlockTiles, 1);
		const int N = maps.N;
		const float size = static_cast<float>(N - 1) * maps.Cell;
		out.m_Min = maps.Min;
		out.m_Max = maps.Min + glm::vec2(size);
		const int T = std::max(1, static_cast<int>(std::ceil(size / rules.Tile - 1e-4f)));
		out.m_TilesX = out.m_TilesZ = T;
		const size_t tiles = static_cast<size_t>(T) * static_cast<size_t>(T);
		auto tileOfSample = [&](int i, int j)
		{
			const int tx = std::min(static_cast<int>(static_cast<float>(i) * maps.Cell / rules.Tile), T - 1);
			const int tz = std::min(static_cast<int>(static_cast<float>(j) * maps.Cell / rules.Tile), T - 1);
			return tz * T + tx;
		};

		// 1) the biome of each tile
		std::vector<std::array<int, kBiomes>> counts(tiles);
		for (auto& c : counts) c.fill(0);
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i)
			{
				const uint8_t b = maps.Biomes[static_cast<size_t>(j) * static_cast<size_t>(N) + static_cast<size_t>(i)];
				++counts[static_cast<size_t>(tileOfSample(i, j))][std::min<size_t>(b, kBiomes - 1)];
			}
		std::vector<int> label(tiles, 0);
		for (size_t t = 0; t < tiles; ++t)
		{
			int total = 0;
			for (int b = 0; b < kBiomes; ++b) total += counts[t][static_cast<size_t>(b)];
			const int water = counts[t][static_cast<size_t>(Biome::Water)];
			if (total > 0 && static_cast<float>(water) > rules.WaterShare * static_cast<float>(total)) { label[t] = static_cast<int>(Biome::Water); continue; }
			int best = -1, most = -1;
			for (int b = 1; b < kBiomes; ++b)
				if (counts[t][static_cast<size_t>(b)] > most) { most = counts[t][static_cast<size_t>(b)]; best = b; }
			label[t] = most > 0 ? best : static_cast<int>(Biome::Water);
		}

		// 2) clusters: 4-connected tiles of the same biome in the same block
		std::vector<int> cluster(tiles, -1);
		int clusters = 0;
		std::vector<int> stack;
		for (size_t start = 0; start < tiles; ++start)
		{
			if (cluster[start] >= 0) continue;
			const int sx = static_cast<int>(start) % T, sz = static_cast<int>(start) / T;
			const int bx = sx / rules.BlockTiles, bz = sz / rules.BlockTiles;
			cluster[start] = clusters;
			stack.assign(1, static_cast<int>(start));
			while (!stack.empty())
			{
				const int t = stack.back();
				stack.pop_back();
				const int x = t % T, z = t / T;
				const int nx[4] = { x - 1, x + 1, x, x };
				const int nz[4] = { z, z, z - 1, z + 1 };
				for (int k = 0; k < 4; ++k)
				{
					if (nx[k] < 0 || nz[k] < 0 || nx[k] >= T || nz[k] >= T) continue;
					if (nx[k] / rules.BlockTiles != bx || nz[k] / rules.BlockTiles != bz) continue;
					const size_t n = static_cast<size_t>(nz[k] * T + nx[k]);
					if (cluster[n] >= 0 || label[n] != label[start]) continue;
					cluster[n] = clusters;
					stack.push_back(static_cast<int>(n));
				}
			}
			++clusters;
		}

		// 3) small clusters join the neighbour they share the longest border with
		for (;;)
		{
			std::vector<int> sizes(static_cast<size_t>(clusters), 0);
			for (const int c : cluster) ++sizes[static_cast<size_t>(c)];
			int live = 0;
			for (const int s : sizes) live += s > 0 ? 1 : 0;
			if (live <= 1) break;
			int small = -1;
			for (int c = 0; c < clusters && small < 0; ++c)
				if (sizes[static_cast<size_t>(c)] > 0 && sizes[static_cast<size_t>(c)] < rules.MinTiles) small = c;
			if (small < 0) break;
			std::map<int, int> border;
			for (size_t t = 0; t < tiles; ++t)
			{
				if (cluster[t] != small) continue;
				const int x = static_cast<int>(t) % T, z = static_cast<int>(t) / T;
				const int nx[4] = { x - 1, x + 1, x, x };
				const int nz[4] = { z, z, z - 1, z + 1 };
				for (int k = 0; k < 4; ++k)
				{
					if (nx[k] < 0 || nz[k] < 0 || nx[k] >= T || nz[k] >= T) continue;
					const int other = cluster[static_cast<size_t>(nz[k] * T + nx[k])];
					if (other != small) ++border[other];
				}
			}
			int into = -1, longest = 0;
			for (const auto& [other, length] : border)   // in id order: ties keep the lowest
				if (length > longest) { longest = length; into = other; }
			if (into < 0) break;   // (cannot happen on a connected grid)
			for (int& c : cluster) if (c == small) c = into;
		}

		// 4) numbered by their first tile
		std::map<int, int> number;
		for (size_t t = 0; t < tiles; ++t)
			if (number.find(cluster[t]) == number.end()) { const int id = static_cast<int>(number.size()); number[cluster[t]] = id; }
		out.m_TileRegion.resize(tiles);
		for (size_t t = 0; t < tiles; ++t) out.m_TileRegion[t] = number[cluster[t]];
		const size_t R = number.size();
		out.m_Regions.resize(R);

		// the summaries: biomes, water, bounds and the centroid from the samples
		std::vector<std::array<int, kBiomes>> biomeCount(R);
		for (auto& c : biomeCount) c.fill(0);
		std::vector<glm::dvec2> sum(R, glm::dvec2(0.0));
		std::vector<int> samples(R, 0), land(R, 0), near(R, 0);
		for (size_t r = 0; r < R; ++r) { out.m_Regions[r].Min = glm::vec2(1e30f); out.m_Regions[r].Max = glm::vec2(-1e30f); }
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i)
			{
				const size_t s = static_cast<size_t>(j) * static_cast<size_t>(N) + static_cast<size_t>(i);
				const size_t r = static_cast<size_t>(out.m_TileRegion[static_cast<size_t>(tileOfSample(i, j))]);
				const glm::vec2 p = maps.SamplePosition(i, j);
				const uint8_t b = std::min<uint8_t>(maps.Biomes[s], kBiomes - 1);
				++biomeCount[r][b];
				++samples[r];
				sum[r] += glm::dvec2(p);
				RegionSummary& g = out.m_Regions[r];
				g.Min = glm::min(g.Min, p);
				g.Max = glm::max(g.Max, p);
				if (b != static_cast<uint8_t>(Biome::Water))
				{
					++land[r];
					const float wd = s < maps.WaterDistance.size() ? maps.WaterDistance[s] : 1e9f;
					if (wd <= rules.NearWater) ++near[r];
				}
			}
		for (size_t r = 0; r < R; ++r)
		{
			RegionSummary& g = out.m_Regions[r];
			g.Id = static_cast<int>(r);
			const float n = static_cast<float>(std::max(samples[r], 1));
			int best = 0;
			for (int b = 0; b < kBiomes; ++b)
			{
				g.Biomes[static_cast<size_t>(b)] = static_cast<float>(biomeCount[r][static_cast<size_t>(b)]) / n;
				if (biomeCount[r][static_cast<size_t>(b)] > biomeCount[r][static_cast<size_t>(best)]) best = b;
			}
			g.Dominant = static_cast<Biome>(best);
			g.WaterShare = g.Biomes[static_cast<size_t>(Biome::Water)];
			g.Water = g.Dominant == Biome::Water;
			g.NearWater = land[r] > 0 ? static_cast<float>(near[r]) / static_cast<float>(land[r]) : 0.0f;
			g.Area = static_cast<float>(samples[r]) * maps.Cell * maps.Cell;
			g.Centroid = glm::vec2(sum[r] / static_cast<double>(std::max(samples[r], 1)));
			for (int b = 0; b < kBiomes; ++b)
				if (const auto d = rules.Danger.find(BiomeName(static_cast<Biome>(b))); d != rules.Danger.end()) g.BaseDanger += g.Biomes[static_cast<size_t>(b)] * d->second;
			g.BaseDanger = std::clamp(g.BaseDanger, 0.0f, 1.0f);
			g.Danger = g.BaseDanger;
		}
		for (size_t t = 0; t < tiles; ++t) ++out.m_Regions[static_cast<size_t>(out.m_TileRegion[t])].Tiles;

		// the anchors: the dry ground nearest to the centroid of each region, and to the centre of each tile
		std::vector<float> bestRegion(R, 1e30f);
		std::vector<float> bestTile(tiles, 1e30f);
		out.m_TileAnchor.resize(tiles);
		for (size_t t = 0; t < tiles; ++t) out.m_TileAnchor[t] = out.TileCenter(static_cast<int>(t));
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i)
			{
				const size_t s = static_cast<size_t>(j) * static_cast<size_t>(N) + static_cast<size_t>(i);
				if (maps.Biomes[s] == static_cast<uint8_t>(Biome::Water)) continue;
				const size_t t = static_cast<size_t>(tileOfSample(i, j));
				const size_t r = static_cast<size_t>(out.m_TileRegion[t]);
				const glm::vec2 p = maps.SamplePosition(i, j);
				const glm::vec2 dr = p - out.m_Regions[r].Centroid;
				const float d2r = glm::dot(dr, dr);
				if (d2r < bestRegion[r])
				{
					bestRegion[r] = d2r;
					out.m_Regions[r].Anchor = p;
					out.m_Regions[r].WaterDistance = s < maps.WaterDistance.size() ? maps.WaterDistance[s] : 1e9f;
				}
				const glm::vec2 dt = p - out.TileCenter(static_cast<int>(t));
				const float d2t = glm::dot(dt, dt);
				if (d2t < bestTile[t]) { bestTile[t] = d2t; out.m_TileAnchor[t] = p; }
			}
		for (size_t r = 0; r < R; ++r)
			if (bestRegion[r] > 1e29f) { out.m_Regions[r].Anchor = out.m_Regions[r].Centroid; out.m_Regions[r].WaterDistance = 0.0f; }   // all water

		// the neighbours
		for (int z = 0; z < T; ++z)
			for (int x = 0; x < T; ++x)
			{
				const int a = out.m_TileRegion[static_cast<size_t>(z * T + x)];
				const int right = x + 1 < T ? out.m_TileRegion[static_cast<size_t>(z * T + x + 1)] : a;
				const int down = z + 1 < T ? out.m_TileRegion[static_cast<size_t>((z + 1) * T + x)] : a;
				for (const int b : { right, down })
				{
					if (b == a) continue;
					auto& na = out.m_Regions[static_cast<size_t>(a)].Neighbours;
					auto& nb = out.m_Regions[static_cast<size_t>(b)].Neighbours;
					if (std::find(na.begin(), na.end(), b) == na.end()) na.push_back(b);
					if (std::find(nb.begin(), nb.end(), a) == nb.end()) nb.push_back(a);
				}
			}
		for (RegionSummary& g : out.m_Regions) std::sort(g.Neighbours.begin(), g.Neighbours.end());

		// the names: where it lies and what it is ("North Woods"); a second of the same name gets a number
		std::map<std::string, int> used;
		const glm::vec2 middle = out.m_Min + glm::vec2(size * 0.5f);
		for (RegionSummary& g : out.m_Regions)
		{
			const std::string biome = BiomeName(g.Dominant);
			const auto n = rules.Names.find(biome);
			const std::string what = n != rules.Names.end() ? n->second : biome;
			std::string name = Direction(g.Centroid - middle, size * 0.5f) + " " + what;
			const int k = ++used[name];
			if (k > 1) name += " " + std::to_string(k);
			g.Name = name;
		}

		// the scout cells
		const int C = std::max(1, static_cast<int>(std::ceil(size / rules.ScoutCell - 1e-4f)));
		out.m_CellsX = out.m_CellsZ = C;
		const size_t cells = static_cast<size_t>(C) * static_cast<size_t>(C);
		out.m_CellRegion.resize(cells);
		out.m_CellsIn.assign(R, 0);
		out.m_CellWater.assign(cells, 1e9f);
		for (size_t c = 0; c < cells; ++c)
		{
			const int r = out.TileRegion(out.TileAt(out.CellCenter(static_cast<int>(c))));
			out.m_CellRegion[c] = r;
			++out.m_CellsIn[static_cast<size_t>(r)];
		}
		if (!maps.WaterDistance.empty())
			for (int j = 0; j < N; ++j)
				for (int i = 0; i < N; ++i)
				{
					const int c = out.CellAt(maps.SamplePosition(i, j));
					float& w = out.m_CellWater[static_cast<size_t>(c)];
					w = std::min(w, maps.WaterDistance[static_cast<size_t>(j) * static_cast<size_t>(N) + static_cast<size_t>(i)]);
				}

		// the wild resources
		for (const RegionThing& thing : things)
		{
			if (thing.Pos.x < out.m_Min.x || thing.Pos.y < out.m_Min.y || thing.Pos.x > out.m_Max.x || thing.Pos.y > out.m_Max.y) continue;
			++out.m_Regions[static_cast<size_t>(out.RegionAt(thing.Pos))].Resources[thing.Kind];
		}
		return out;
	}

	const RegionSummary* RegionMap::Find(const std::string& key) const
	{
		if (!key.empty() && std::all_of(key.begin(), key.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
			return Find(std::atoi(key.c_str()));
		const std::string want = Lower(key);
		for (const RegionSummary& g : m_Regions)
			if (Lower(g.Name) == want) return &g;
		return nullptr;
	}

	int RegionMap::TileAt(const glm::vec2& p) const
	{
		if (m_TilesX == 0) return -1;
		const int x = std::clamp(static_cast<int>(std::floor((p.x - m_Min.x) / m_Rules.Tile)), 0, m_TilesX - 1);
		const int z = std::clamp(static_cast<int>(std::floor((p.y - m_Min.y) / m_Rules.Tile)), 0, m_TilesZ - 1);
		return z * m_TilesX + x;
	}

	glm::vec2 RegionMap::TileCenter(int tile) const
	{
		const int x = tile % std::max(m_TilesX, 1), z = tile / std::max(m_TilesX, 1);
		const glm::vec2 c = m_Min + (glm::vec2(static_cast<float>(x), static_cast<float>(z)) + 0.5f) * m_Rules.Tile;
		return glm::min(c, m_Max);
	}

	int RegionMap::RegionAt(const glm::vec2& p) const { return TileRegion(TileAt(p)); }

	std::vector<int> RegionMap::TilesOf(int region) const
	{
		std::vector<int> out;
		for (size_t t = 0; t < m_TileRegion.size(); ++t)
			if (m_TileRegion[t] == region) out.push_back(static_cast<int>(t));
		return out;
	}

	int RegionMap::CellAt(const glm::vec2& p) const
	{
		if (m_CellsX == 0) return -1;
		const int x = std::clamp(static_cast<int>(std::floor((p.x - m_Min.x) / m_Rules.ScoutCell)), 0, m_CellsX - 1);
		const int z = std::clamp(static_cast<int>(std::floor((p.y - m_Min.y) / m_Rules.ScoutCell)), 0, m_CellsZ - 1);
		return z * m_CellsX + x;
	}

	glm::vec2 RegionMap::CellCenter(int cell) const
	{
		const int x = cell % std::max(m_CellsX, 1), z = cell / std::max(m_CellsX, 1);
		const glm::vec2 c = m_Min + (glm::vec2(static_cast<float>(x), static_cast<float>(z)) + 0.5f) * m_Rules.ScoutCell;
		return glm::min(c, m_Max);
	}

	void RegionMap::CellsNear(const glm::vec2& p, float radius, std::vector<int>& out) const
	{
		out.clear();
		if (m_CellsX == 0) return;
		const float s = m_Rules.ScoutCell;
		const int x0 = std::max(0, static_cast<int>(std::floor((p.x - radius - m_Min.x) / s))), x1 = std::min(m_CellsX - 1, static_cast<int>(std::floor((p.x + radius - m_Min.x) / s)));
		const int z0 = std::max(0, static_cast<int>(std::floor((p.y - radius - m_Min.y) / s))), z1 = std::min(m_CellsZ - 1, static_cast<int>(std::floor((p.y + radius - m_Min.y) / s)));
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x)
			{
				const int c = z * m_CellsX + x;
				const glm::vec2 d = CellCenter(c) - p;
				if (glm::dot(d, d) <= radius * radius) out.push_back(c);
			}
		if (out.empty()) out.push_back(CellAt(p));   // where it stands, at least
	}

	float RegionMap::WaterDistanceAt(const glm::vec2& p) const
	{
		const int c = CellAt(p);
		return c < 0 || m_CellWater.empty() ? 1e9f : m_CellWater[static_cast<size_t>(c)];
	}

	void RegionMap::SetPredators(const std::vector<glm::vec2>& positions)
	{
		for (RegionSummary& g : m_Regions) g.Predators = 0;
		for (const glm::vec2& p : positions)
			if (const int r = RegionAt(p); r >= 0) ++m_Regions[static_cast<size_t>(r)].Predators;
		for (RegionSummary& g : m_Regions) g.Danger = std::clamp(g.BaseDanger + m_Rules.PredatorDanger * static_cast<float>(g.Predators), 0.0f, 1.0f);
	}

	uint64_t RegionMap::Fingerprint() const
	{
		uint64_t h = 1469598103934665603ull;
		for (const int r : m_TileRegion) Mix(h, static_cast<uint64_t>(r));
		for (const RegionSummary& g : m_Regions)
		{
			Mix(h, g.Name);
			for (const auto& [kind, n] : g.Resources) { Mix(h, kind); Mix(h, static_cast<uint64_t>(n)); }
		}
		return h;
	}

	std::string RegionMap::List() const
	{
		std::string s;
		char line[256];
		for (const RegionSummary& g : m_Regions)
		{
			int nodes = 0;
			for (const auto& [kind, n] : g.Resources) nodes += n;
			std::snprintf(line, sizeof line, "%2d %-22s %-6s %5.2f ha  water %3.0f%%  near water %3.0f%%  %3d wild nodes  danger %.2f\n", g.Id, g.Name.c_str(), BiomeName(g.Dominant),
				static_cast<double>(g.Area / 10000.0f), static_cast<double>(g.WaterShare * 100.0f), static_cast<double>(g.NearWater * 100.0f), nodes, static_cast<double>(g.Danger));
			s += line;
		}
		return s;
	}

	std::string RegionMap::Describe(int id) const
	{
		const RegionSummary* g = Find(id);
		if (!g) return "no region " + std::to_string(id);
		char buf[512];
		std::snprintf(buf, sizeof buf, "%d %s: %s, %d tiles, %.2f ha, from (%.0f, %.0f) to (%.0f, %.0f), anchor (%.0f, %.0f), %.0f m from the water at the anchor\n", g->Id, g->Name.c_str(),
			BiomeName(g->Dominant), g->Tiles, static_cast<double>(g->Area / 10000.0f), static_cast<double>(g->Min.x), static_cast<double>(g->Min.y), static_cast<double>(g->Max.x),
			static_cast<double>(g->Max.y), static_cast<double>(g->Anchor.x), static_cast<double>(g->Anchor.y), static_cast<double>(std::min(g->WaterDistance, 99999.0f)));
		std::string s = buf;
		s += "  biomes:";
		for (int b = 0; b < kBiomes; ++b)
			if (g->Biomes[static_cast<size_t>(b)] >= 0.005f) { std::snprintf(buf, sizeof buf, " %s %.0f%%", BiomeName(static_cast<Biome>(b)), static_cast<double>(g->Biomes[static_cast<size_t>(b)] * 100.0f)); s += buf; }
		std::snprintf(buf, sizeof buf, "\n  water %.0f%%, dry ground by the water %.0f%%; danger %.2f (ground %.2f, %d predators)\n", static_cast<double>(g->WaterShare * 100.0f),
			static_cast<double>(g->NearWater * 100.0f), static_cast<double>(g->Danger), static_cast<double>(g->BaseDanger), g->Predators);
		s += buf;
		s += "  wild resources:";
		if (g->Resources.empty()) s += " none";
		for (const auto& [kind, n] : g->Resources) s += " " + kind + " " + std::to_string(n);
		s += "\n  neighbours:";
		for (const int n : g->Neighbours) s += " " + m_Regions[static_cast<size_t>(n)].Name + ";";
		return s + "\n";
	}
}
