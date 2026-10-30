#include "GB/Gameplay/WorldResources.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
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


		// The model of a node: which procedural tree or rock of the world draws it, how big, how many rocks are piled on it, how much room it takes.
		ResourceSpot MakeSpot(const ResourceGround& ground, const ResourceNodeDef& def, const glm::vec2& p, uint32_t h, float spacing)
		{
			ResourceSpot s;
			s.Node = def.Id;
			s.Position = glm::vec3(p.x, ground.Height(p.x, p.y), p.y);
			s.Yaw = 360.0f * Unit(Mix(h ^ 3u));
			s.Radius = spacing;
			const ResourceSpawnDef& sp = def.Spawn;
			if (!sp.Prop.empty())
			{
				const int count = sp.Prop == "tree" ? ground.TreeVariants : ground.RockVariants;
				if (count > 0)
				{
					s.Variant = static_cast<int>(Mix(h ^ 4u) % static_cast<uint32_t>(count));
					s.Scale = sp.ScaleMin + (sp.ScaleMax - sp.ScaleMin) * Unit(Mix(h ^ 5u));
					if (sp.Stack > 0) s.Stack = static_cast<int>(Mix(h ^ 6u) % static_cast<uint32_t>(sp.Stack + 1));
				}
			}
			return s;
		}

		void PlanBase(const ResourceGround& ground, const std::map<std::string, ResourceNodeDef>& nodes, uint32_t seed, float density, Occupancy& taken, Occupancy& blockers,
			std::vector<ResourceSpot>& spots)
		{
		std::vector<const ResourceNodeDef*> order;
		for (const auto& [id, def] : nodes)
			if (def.Spawn.Placed()) order.push_back(&def);
		// rarest first (then by id, so the order never depends on anything but the data)
		std::stable_sort(order.begin(), order.end(), [](const ResourceNodeDef* a, const ResourceNodeDef* b) { return a->Spawn.PerHectare < b->Spawn.PerHectare; });

		const float margin = 2.0f;   // never right at the edge of the map
		const glm::vec2 lo = ground.Min + glm::vec2(margin), hi = ground.Max - glm::vec2(margin);
		if (hi.x <= lo.x || hi.y <= lo.y) return;

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
					spots.push_back(MakeSpot(ground, *def, p, h, sp.Spacing));
					++placed;
				}
		}
		}

		// ---- patches
		float Lerp(float a, float b, float t) { return a + (b - a) * t; }

		void PlanPatches(const ResourceGround& ground, const std::map<std::string, ResourceNodeDef>& nodes, const NatureConfig& cfg, uint32_t seed, Occupancy& taken,
			Occupancy& blockers, std::vector<ResourceSpot>& spots)
		{
			const float margin = 2.0f;
			const glm::vec2 lo = ground.Min + glm::vec2(margin), hi = ground.Max - glm::vec2(margin);
			if (hi.x <= lo.x || hi.y <= lo.y) return;
			int budget = cfg.MaxNodes;
			for (const NaturePatchDef& pd : cfg.Patches)
			{
				std::vector<std::pair<const ResourceNodeDef*, float>> mix;
				float total = 0.0f;
				for (const NaturePatchMix& m : pd.Mix)
					if (const auto it = nodes.find(m.Node); it != nodes.end() && m.Weight > 0.0f) { mix.push_back({ &it->second, m.Weight }); total += m.Weight; }
				if (mix.empty() || total <= 0.0f || pd.Cell < 8.0f) continue;
				const uint32_t salt = Mix(seed * 0x27D4EB2Fu ^ HashName("patch:" + pd.Id));
				const int ni = static_cast<int>(std::floor((hi.x - lo.x) / pd.Cell)) + 1, nj = static_cast<int>(std::floor((hi.y - lo.y) / pd.Cell)) + 1;
				struct Cell { uint32_t H; int I, J; };
				std::vector<Cell> cells;
				for (int j = 0; j < nj; ++j)
					for (int i = 0; i < ni; ++i) cells.push_back({ Hash(i, j, salt), i, j });
				std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return a.H != b.H ? a.H < b.H : a.J != b.J ? a.J < b.J : a.I < b.I; });
				int placedOfKind = 0;
				for (const Cell& c : cells)
				{
					if (budget <= 0 || (pd.Max > 0 && placedOfKind >= pd.Max)) break;
					const uint32_t h = c.H;
					const glm::vec2 centre = lo + (glm::vec2(static_cast<float>(c.I), static_cast<float>(c.J)) + glm::vec2(0.1f + 0.8f * Unit(Mix(h ^ 1u)), 0.1f + 0.8f * Unit(Mix(h ^ 2u)))) * pd.Cell;
					if (centre.x < lo.x || centre.y < lo.y || centre.x > hi.x || centre.y > hi.y) continue;
					const auto chance = pd.Biomes.find(ground.Biome(centre.x, centre.y));
					if (chance == pd.Biomes.end()) continue;
					if (Unit(Mix(h ^ 7u)) >= std::min(1.0f, chance->second * cfg.Abundance)) continue;
					const float u = Unit(Mix(h ^ 8u));
					const float radius = Lerp(pd.RadiusMin, pd.RadiusMax, u * u) * std::sqrt(std::max(0.25f, cfg.Abundance * 0.5f + 0.5f));   // many small patches, a few big ones
					const uint32_t pSalt = Mix(h ^ 0x51u);
					const int steps = static_cast<int>(std::ceil(1.35f * radius / pd.Spacing));
					for (int gj = -steps; gj <= steps; ++gj)
						for (int gi = -steps; gi <= steps; ++gi)
						{
							if (budget <= 0 || (pd.Max > 0 && placedOfKind >= pd.Max)) break;
							const uint32_t ph = Hash(gi, gj, pSalt);
							const glm::vec2 p = centre + (glm::vec2(static_cast<float>(gi), static_cast<float>(gj)) + glm::vec2(0.5f * Unit(Mix(ph ^ 1u)) - 0.25f, 0.5f * Unit(Mix(ph ^ 2u)) - 0.25f)) * pd.Spacing;
							if (p.x < lo.x || p.y < lo.y || p.x > hi.x || p.y > hi.y) continue;
							// a ragged edge: the distance to the centre, pushed in and out by a noise field
							const float edge = glm::length(p - centre) / radius + 0.7f * (Clumps(p.x, p.y, 11.0f, salt ^ 0xE1u) - 0.5f);
							if (edge > 1.0f) continue;
							if (Clumps(p.x, p.y, 17.0f, pSalt ^ 0xC7u) < pd.Clearings) continue;   // a glade
							if (Unit(Mix(ph ^ 3u)) >= pd.Fill * (1.0f - 0.5f * edge * edge)) continue;
							if (pd.Biomes.find(ground.Biome(p.x, p.y)) == pd.Biomes.end()) continue;
							if (ground.SlopeDegrees && ground.SlopeDegrees(p.x, p.y) > pd.MaxSlope) continue;
							if (ground.WaterDistance && ground.WaterDistance(p.x, p.y) < 1.5f) continue;
							float pick = Unit(Mix(ph ^ 4u)) * total;
							const ResourceNodeDef* def = mix.back().first;
							for (const auto& [d, w] : mix) { if (pick < w) { def = d; break; } pick -= w; }
							ResourceSpot probe = MakeSpot(ground, *def, p, ph, pd.Spacing);
							probe.Radius = pd.Spacing * std::sqrt(probe.Scale);   // a big tree wants more room than a sapling
							if (!ground.Blockers.empty() && !blockers.Free(p, 1.0f)) continue;
							if (!taken.Free(p, probe.Radius)) continue;
							taken.Add(p, probe.Radius);
							spots.push_back(std::move(probe));
							++placedOfKind;
							--budget;
						}
				}
			}
		}
	}

	std::vector<ResourceSpot> PlanWorldResources(const ResourceGround& ground, const std::map<std::string, ResourceNodeDef>& nodes, uint32_t seed, float density)
	{
		std::vector<ResourceSpot> spots;
		if (!ground.Height || !ground.Biome || density <= 0.0f) return spots;
		Occupancy taken(8.0f), blockers(8.0f);
		for (const ResourceGround::Blocker& b : ground.Blockers) blockers.Add(b.At, b.Radius);
		PlanBase(ground, nodes, seed, density, taken, blockers, spots);
		return spots;
	}

	std::vector<ResourceSpot> PlanWorldNature(const ResourceGround& ground, const std::map<std::string, ResourceNodeDef>& nodes, const NatureConfig& config, uint32_t seed)
	{
		std::vector<ResourceSpot> spots;
		if (!ground.Height || !ground.Biome || config.Abundance <= 0.0f) return spots;
		Occupancy taken(8.0f), blockers(8.0f);
		for (const ResourceGround::Blocker& b : ground.Blockers) blockers.Add(b.At, b.Radius);
		PlanBase(ground, nodes, seed, config.Abundance, taken, blockers, spots);
		PlanPatches(ground, nodes, config, seed, taken, blockers, spots);
		return spots;
	}

	bool NatureConfig::operator==(const NatureConfig& o) const { return NatureConfigToJson(*this) == NatureConfigToJson(o); }

	std::string ParseNatureConfig(const std::string& text, NatureConfig& out)
	{
		using Json = nlohmann::json;
		static const char* const kBiomes[] = { "Water", "Shore", "Meadow", "Forest", "Rock", "Snow", "Dunes" };
		try
		{
			const Json j = Json::parse(text);
			if (!j.is_object()) return "the nature config must be an object";
			NatureConfig c;
			c.Abundance = j.value("abundance", 1.0f);
			c.MaxNodes = j.value("maxNodes", 3000);
			if (!(c.Abundance > 0.0f && c.Abundance <= 10.0f)) return "abundance must be above 0 and at most 10";
			if (c.MaxNodes < 0 || c.MaxNodes > 100000) return "maxNodes must be between 0 and 100000";
			if (auto it = j.find("patches"); it != j.end())
			{
				if (!it->is_array()) return "patches must be a list";
				for (const Json& o : *it)
				{
					NaturePatchDef p;
					p.Id = o.value("id", std::string());
					if (p.Id.empty()) return "a patch has no id";
					for (const NaturePatchDef& q : c.Patches) if (q.Id == p.Id) return "two patches called '" + p.Id + "'";
					p.Cell = o.value("cell", 64.0f);
					p.RadiusMin = o.value("radiusMin", 12.0f);
					p.RadiusMax = o.value("radiusMax", 30.0f);
					p.Spacing = o.value("spacing", 3.6f);
					p.Fill = o.value("fill", 0.9f);
					p.Clearings = o.value("clearings", 0.25f);
					p.MaxSlope = o.value("maxSlope", 30.0f);
					p.Max = o.value("max", 0);
					if (!(p.Cell >= 8.0f && p.Cell <= 400.0f) || !(p.RadiusMin >= 2.0f && p.RadiusMax >= p.RadiusMin && p.RadiusMax <= 200.0f)) return "patch '" + p.Id + "': cell (8..400) or radius out of range";
					if (!(p.Spacing >= 1.0f && p.Spacing <= 30.0f) || !(p.Fill > 0.0f && p.Fill <= 1.0f) || !(p.Clearings >= 0.0f && p.Clearings <= 0.6f)) return "patch '" + p.Id + "': spacing (1..30), fill (0..1) or clearings (0..0.6) out of range";
					if (!(p.MaxSlope >= 0.0f && p.MaxSlope <= 90.0f) || p.Max < 0) return "patch '" + p.Id + "': maxSlope or max out of range";
					if (auto bs = o.find("biomes"); bs != o.end() && bs->is_object())
						for (auto kv = bs->begin(); kv != bs->end(); ++kv)
						{
							if (std::find(std::begin(kBiomes), std::end(kBiomes), kv.key()) == std::end(kBiomes)) return "patch '" + p.Id + "': unknown biome '" + kv.key() + "'";
							const float chance = kv.value().get<float>();
							if (!(chance >= 0.0f && chance <= 1.0f)) return "patch '" + p.Id + "': a biome chance must be between 0 and 1";
							p.Biomes[kv.key()] = chance;
						}
					if (p.Biomes.empty()) return "patch '" + p.Id + "' has no biomes";
					if (auto mx = o.find("mix"); mx != o.end() && mx->is_array())
						for (const Json& m : *mx)
						{
							NaturePatchMix e;
							e.Node = m.value("node", std::string());
							e.Weight = m.value("weight", 1.0f);
							if (e.Node.empty() || !(e.Weight > 0.0f)) return "patch '" + p.Id + "': a mix entry needs a node and a weight above 0";
							p.Mix.push_back(e);
						}
					if (p.Mix.empty()) return "patch '" + p.Id + "' has an empty mix";
					c.Patches.push_back(std::move(p));
				}
			}
			out = std::move(c);
			return {};
		}
		catch (const std::exception& e)
		{
			return std::string("the nature config is not valid (") + e.what() + ")";
		}
	}

	std::string LoadNatureConfigFile(const std::string& path, NatureConfig& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "cannot open " + path;
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = ParseNatureConfig(ss.str(), out);
		return error.empty() ? error : path + ": " + error;
	}

	std::string NatureConfigToJson(const NatureConfig& cfg)
	{
		using Json = nlohmann::json;
		auto round = [](float v) { return std::round(v * 1000.0f) / 1000.0f; };
		Json j = { { "abundance", round(cfg.Abundance) }, { "maxNodes", cfg.MaxNodes } };
		Json patches = Json::array();
		for (const NaturePatchDef& p : cfg.Patches)
		{
			Json biomes = Json::object(), mix = Json::array();
			for (const auto& [b, c] : p.Biomes) biomes[b] = round(c);
			for (const NaturePatchMix& m : p.Mix) mix.push_back({ { "node", m.Node }, { "weight", round(m.Weight) } });
			Json o = { { "id", p.Id }, { "biomes", biomes }, { "cell", round(p.Cell) }, { "radiusMin", round(p.RadiusMin) }, { "radiusMax", round(p.RadiusMax) }, { "spacing", round(p.Spacing) },
				{ "fill", round(p.Fill) }, { "clearings", round(p.Clearings) }, { "maxSlope", round(p.MaxSlope) }, { "mix", mix } };
			if (p.Max > 0) o["max"] = p.Max;
			patches.push_back(o);
		}
		j["patches"] = patches;
		return j.dump(2);
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
		g.TreeVariants = static_cast<int>(world.TreeRecipes.size());
		g.RockVariants = static_cast<int>(world.RockRecipes.size());
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
