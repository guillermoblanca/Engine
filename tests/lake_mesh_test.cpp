// T105: the combined mesh of the lakes (GB/World/Water/LakeMesh.h), headless and on GB_WorldGen alone. On the drainage test's synthetic map
// (two lake_fill lakes, basin A spilling over a saddle onto low land) with a main lake (SetLake, a disc) on the low land, and on a noise
// terrain: no triangle outside a lake's cells grown by the skirt, every vertex that is not a cell of its lake on ground at or above its level
// (the skirt is under the terrain, and no water is drawn over dry ground), the lake index and level of every vertex right, the main lake
// first, the winding counter-clockwise seen from +Y, and the same bytes every time.
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Water/LakeMesh.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	constexpr uint32_t kN = 49;   // 1 m per cell: Size 48

	// The map of drainage_test.cpp: low land at 1 m; basin A (rim 8 m, saddle 5 m at (14, 6), floor 3 m with pits at 0 and 1 m); basin B (rim
	// 9 m, saddle 7 m at (35, 30), floor 2 m); a 0.5 m pit at (10, 40).
	TgField SyntheticMap()
	{
		TgField f;
		f.Resize(kN, 48.0f);
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i)
			{
				float h = 1.0f;
				auto rim = [&](uint32_t i0, uint32_t i1, uint32_t j0, uint32_t j1) { return (i == i0 || i == i1) && j >= j0 && j <= j1 ? true : (j == j0 || j == j1) && i >= i0 && i <= i1; };
				auto inside = [&](uint32_t i0, uint32_t i1, uint32_t j0, uint32_t j1) { return i > i0 && i < i1 && j > j0 && j < j1; };
				if (rim(6, 22, 6, 30)) h = 8.0f;
				else if (inside(6, 22, 6, 30))
				{
					h = 3.0f;
					if (j == 18) h = 4.0f;
					if (i == 14 && j == 12) h = 0.0f;
					if (i == 14 && j == 24) h = 1.0f;
				}
				if (rim(28, 42, 6, 30)) h = 9.0f;
				else if (inside(28, 42, 6, 30)) h = 2.0f;
				if (i == 14 && j == 6) h = 5.0f;
				if (i == 35 && j == 30) h = 7.0f;
				if (i == 10 && j == 40) h = 0.5f;
				f.At(i, j) = h;
			}
		return f;
	}

	bool Evaluate(const TerrainGraph& g, TgContext& ctx, TgResult& r)
	{
		std::string error;
		const bool ok = EvaluateTerrainGraph(g, MakeDefaultTerrainNodeRegistry(), ctx, r, error);
		Check(ok, "the graph evaluates (" + error + ")");
		return ok && r.WaterOutputs.count("water") && r.Outputs.count("height");
	}

	TerrainGraph LakeFillGraph()
	{
		TerrainGraph g;
		g.Id = "t105";
		auto node = [&](const char* id, const char* type, std::map<std::string, double> params = {}, const char* source = "")
		{
			TgNode n; n.Id = id; n.Type = type; n.Label = id; n.Source = source; n.Params = std::move(params);
			g.Nodes.push_back(n);
		};
		node("ground", "height_input", {}, "height");
		node("drain", "drainage");
		node("lakes", "lake_fill", { { "MinDepth", 1.0 }, { "MinArea", 20.0 } });
		node("out_h", "output", {}, "height");
		node("out_w", "output", {}, "water");
		g.Edges = { { "ground", "drain", "height", "out" }, { "ground", "lakes", "height", "out" }, { "drain", "lakes", "level", "level" },
			{ "drain", "lakes", "receiver", "receiver" }, { "drain", "lakes", "order", "order" }, { "lakes", "out_h", "in", "height" },
			{ "lakes", "out_w", "in", "water" } };
		return g;
	}

	LakeMeshGround GroundOf(const TgField& h) { return LakeMeshGround{ h.Values.data(), h.N, h.Min(), h.Spacing() }; }

	// The index of the first lake of Lakes() covering a sample (its owner, as WaterRaster), -1 for none.
	int Owner(const std::vector<const LakeBody*>& lakes, const glm::vec2& p)
	{
		for (size_t k = 0; k < lakes.size(); ++k) if (lakes[k]->Covers(p)) return static_cast<int>(k);
		return -1;
	}

	struct Tally { size_t Triangles = 0, OutsideSkirt = 0, SkirtAbove = 0, SkirtBelow = 0, WrongLake = 0, WrongLevel = 0, Winding = 0, OffGrid = 0, Range = 0; };

	// Every invariant of the mesh against the water and the ground it was built from.
	Tally Validate(const LakeMesh& mesh, const WaterField& water, const LakeMeshGround& ground, int skirt, const char* what)
	{
		Tally t;
		const std::vector<const LakeBody*> lakes = water.Lakes();
		Check(mesh.Bodies.size() == lakes.size() && mesh.Ranges.size() == lakes.size(), std::string(what) + ": one body and one range per lake");
		for (size_t k = 0; k < std::min(mesh.Bodies.size(), lakes.size()); ++k)
			Check(mesh.Bodies[k].Level == lakes[k]->Level && mesh.Bodies[k].Center == lakes[k]->Center, std::string(what) + ": body " + std::to_string(k) + " is Lakes()[" + std::to_string(k) + "]");
		Check(mesh.Indices.size() % 3 == 0, std::string(what) + ": whole triangles");
		// the Chebyshev distance of a sample to the nearest cell of lake k, up to `skirt` (skirt + 1 = farther)
		auto distance = [&](int i, int j, int k)
		{
			int best = skirt + 1;
			for (int dj = -skirt; dj <= skirt; ++dj)
				for (int di = -skirt; di <= skirt; ++di)
				{
					const int ni = i + di, nj = j + dj;
					if (ni < 0 || nj < 0 || ni >= static_cast<int>(ground.N) || nj >= static_cast<int>(ground.N)) continue;
					if (Owner(lakes, ground.SamplePosition(static_cast<uint32_t>(ni), static_cast<uint32_t>(nj))) == k) best = std::min(best, std::max(std::abs(di), std::abs(dj)));
				}
			return best;
		};
		for (size_t v = 0; v < mesh.Vertices.size(); ++v)
		{
			const LakeVertex& lv = mesh.Vertices[v];
			if (lv.Lake >= mesh.Bodies.size()) { ++t.WrongLake; continue; }
			const LakeMesh::Range& r = mesh.Ranges[lv.Lake];
			if (v < r.FirstVertex || v >= r.FirstVertex + r.VertexCount) ++t.Range;
			if (lv.Position.y != mesh.Bodies[lv.Lake].Level || lv.Normal != glm::vec3(0.0f, 1.0f, 0.0f)) ++t.WrongLevel;
		}
		for (size_t q = 0; q + 2 < mesh.Indices.size(); q += 3)
		{
			++t.Triangles;
			const LakeVertex* v[3] = { &mesh.Vertices[mesh.Indices[q]], &mesh.Vertices[mesh.Indices[q + 1]], &mesh.Vertices[mesh.Indices[q + 2]] };
			const uint32_t k = v[0]->Lake;
			if (v[1]->Lake != k || v[2]->Lake != k || k >= lakes.size()) { ++t.WrongLake; continue; }
			const LakeMesh::Range& r = mesh.Ranges[k];
			if (q < r.FirstIndex || q >= r.FirstIndex + r.IndexCount) ++t.Range;
			if (glm::cross(v[1]->Position - v[0]->Position, v[2]->Position - v[0]->Position).y <= 0.0f) ++t.Winding;
			for (const LakeVertex* lv : v)
			{
				const float fi = (lv->Position.x - ground.Min.x) / ground.Spacing, fj = (lv->Position.z - ground.Min.y) / ground.Spacing;
				const int i = static_cast<int>(std::lround(fi)), j = static_cast<int>(std::lround(fj));
				if (std::fabs(fi - static_cast<float>(i)) > 1e-3f || std::fabs(fj - static_cast<float>(j)) > 1e-3f) { ++t.OffGrid; continue; }
				const glm::vec2 p = ground.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
				if (Owner(lakes, p) == static_cast<int>(k)) continue;   // a cell of its lake
				if (distance(i, j, static_cast<int>(k)) > skirt) ++t.OutsideSkirt;
				if (ground.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)) >= lakes[k]->Level) ++t.SkirtAbove;
				else ++t.SkirtBelow;
			}
		}
		Check(t.WrongLake == 0, std::string(what) + ": every triangle's three vertices carry one valid lake index (" + std::to_string(t.WrongLake) + ")");
		Check(t.WrongLevel == 0, std::string(what) + ": every vertex at its lake's level, the normal up (" + std::to_string(t.WrongLevel) + ")");
		Check(t.Range == 0, std::string(what) + ": every vertex and triangle in its lake's range (" + std::to_string(t.Range) + ")");
		Check(t.OffGrid == 0, std::string(what) + ": every vertex on a sample of the ground (" + std::to_string(t.OffGrid) + ")");
		Check(t.Winding == 0, std::string(what) + ": counter-clockwise seen from +Y (" + std::to_string(t.Winding) + ")");
		Check(t.OutsideSkirt == 0, std::string(what) + ": no triangle outside its lake's cells grown by the skirt (" + std::to_string(t.OutsideSkirt) + ")");
		Check(t.SkirtBelow == 0, std::string(what) + ": the skirt is under the terrain, no vertex past the shore over ground below the level (" + std::to_string(t.SkirtBelow) + ")");
		return t;
	}

	bool SameBytes(const LakeMesh& a, const LakeMesh& b)
	{
		if (a.Vertices.size() != b.Vertices.size() || a.Indices != b.Indices || a.Ranges.size() != b.Ranges.size()) return false;
		if (!a.Vertices.empty() && std::memcmp(a.Vertices.data(), b.Vertices.data(), a.Vertices.size() * sizeof(LakeVertex)) != 0) return false;
		for (size_t k = 0; k < a.Ranges.size(); ++k)
			if (std::memcmp(&a.Ranges[k], &b.Ranges[k], sizeof(LakeMesh::Range)) != 0) return false;
		return a.DroppedShoreTriangles == b.DroppedShoreTriangles && a.LakeCells == b.LakeCells;
	}
}

int GBTest_lake_mesh(int, char**)
{
	// ---------------------------------------------------------------- nothing
	{
		TgField flat;
		flat.Resize(9, 8.0f);
		const LakeMesh none = BuildLakeMesh(WaterField{}, GroundOf(flat));
		Check(none.Empty() && none.Vertices.empty() && none.Bodies.empty(), "no lake, no mesh");
	}

	// ---------------------------------------------------------------- the synthetic map: two lake_fill lakes and a main disc lake
	TgContext ctx;
	ctx.Resolution = kN; ctx.Size = 48.0f;
	std::map<std::string, TgField> inputs{ { "height", SyntheticMap() } };
	ctx.Inputs = &inputs;
	TgResult r;
	if (Evaluate(LakeFillGraph(), ctx, r))
	{
		WaterField water = r.WaterOutputs.at("water");
		const TgField& h = r.Outputs.at("height");
		const LakeMeshGround ground = GroundOf(h);
		Check(water.MoreLakes().size() == 2, "lake_fill makes basins A and B");
		// the main lake: a disc of 3.2 m at 1.5 m on the low land at sample (40, 42), where the ground all around is 1 m (below its level: no skirt)
		LakeBody main;
		main.Present = true;
		main.Center = h.SamplePosition(40, 42);
		main.Radius = 3.2f;
		main.Level = 1.5f;
		water.SetLake(main);

		const LakeMesh mesh = BuildLakeMesh(water, ground);
		const Tally t = Validate(mesh, water, ground, 1, "synthetic");
		std::printf("lake_mesh: synthetic 49 x 49, %zu lakes, %zu cells, %zu vertices, %zu triangles, %zu shore triangles left out, %zu skirt corners\n",
			mesh.Bodies.size(), mesh.LakeCells, mesh.Vertices.size(), mesh.TriangleCount(), mesh.DroppedShoreTriangles, t.SkirtAbove);
		Check(mesh.Bodies.size() == 3 && mesh.Bodies[0].Level == 1.5f && mesh.Bodies[1].Level == 5.0f && mesh.Bodies[2].Level == 7.0f,
			"the main lake is body 0, then A at 5 m and B at 7 m");
		Check(mesh.Bodies[1].Look.ShallowColor == DefaultWaterLook().Lake.ShallowColor, "a body carries the lake look of the WaterLook");
		for (size_t k = 0; k < mesh.Ranges.size(); ++k) Check(mesh.Ranges[k].IndexCount > 0, "every lake has triangles (" + std::to_string(k) + ")");
		// the main lake: drawn on its disc only (its neighbours are dry ground below its level), so its edge triangles are left out
		bool mainInDisc = true;
		for (uint32_t v = mesh.Ranges[0].FirstVertex; v < mesh.Ranges[0].FirstVertex + mesh.Ranges[0].VertexCount; ++v)
			mainInDisc = mainInDisc && main.Covers({ mesh.Vertices[v].Position.x, mesh.Vertices[v].Position.z });
		Check(mainInDisc, "the main lake over low land: every vertex inside its disc");
		Check(mesh.DroppedShoreTriangles > 0, "and the triangles that reach the dry ground past it are left out");
		// basin A: its rim (8 m, and the 5 m saddle) is the skirt; past the saddle the low land (1 m) never is
		size_t rimA = 0;
		bool pastSaddle = false;
		for (uint32_t v = mesh.Ranges[1].FirstVertex; v < mesh.Ranges[1].FirstVertex + mesh.Ranges[1].VertexCount; ++v)
		{
			const glm::vec3& p = mesh.Vertices[v].Position;
			const glm::vec2 s14_5 = h.SamplePosition(14, 5);
			if (std::fabs(p.x - s14_5.x) < 1e-3f && std::fabs(p.z - s14_5.y) < 1e-3f) pastSaddle = true;
			if (!water.MoreLakes()[0].Covers({ p.x, p.z })) ++rimA;
		}
		Check(rimA > 0, "basin A's skirt lies on its rim (" + std::to_string(rimA) + " vertices)");
		Check(!pastSaddle, "and not on the low land past its saddle");
		// every triangle whose three corners are cells of one lake is drawn
		size_t allCell = 0;
		const std::vector<const LakeBody*> lakes = water.Lakes();
		auto owner = [&](uint32_t i, uint32_t j) { return Owner(lakes, h.SamplePosition(i, j)); };
		for (uint32_t j = 0; j + 1 < kN; ++j)
			for (uint32_t i = 0; i + 1 < kN; ++i)
			{
				const int a = owner(i, j), b = owner(i + 1, j), c = owner(i, j + 1), d = owner(i + 1, j + 1);
				allCell += a >= 0 && a == b && a == c;
				allCell += d >= 0 && d == b && d == c;
			}
		size_t drawnAllCell = 0;
		for (size_t q = 0; q < mesh.Indices.size(); q += 3)
		{
			bool cells = true;
			for (int e = 0; e < 3; ++e)
			{
				const LakeVertex& v = mesh.Vertices[mesh.Indices[q + static_cast<size_t>(e)]];
				cells = cells && Owner(lakes, { v.Position.x, v.Position.z }) == static_cast<int>(v.Lake);
			}
			drawnAllCell += cells;
		}
		Check(drawnAllCell == allCell, "every triangle inside a lake is drawn (" + std::to_string(drawnAllCell) + " of " + std::to_string(allCell) + ")");

		const LakeMesh again = BuildLakeMesh(water, ground);
		Check(SameBytes(mesh, again), "the same bytes twice");

		LakeMeshOptions wide;
		wide.SkirtCells = 2;
		const LakeMesh mesh2 = BuildLakeMesh(water, ground, DefaultWaterLook(), wide);
		Validate(mesh2, water, ground, 2, "synthetic, skirt 2");
		Check(mesh2.TriangleCount() > mesh.TriangleCount(), "a skirt of two cells has more triangles");
	}

	// ---------------------------------------------------------------- a noise terrain: lakes where it pools
	{
		TerrainGraph noise = LakeFillGraph();
		noise.Nodes[0] = TgNode{ "ground", "heightfield_noise", "ground", 0, 0, "", { { "Frequency", 0.02 }, { "Octaves", 5 }, { "Amplitude", 20 } } };
		for (TgEdge& e : noise.Edges) if (e.From == "ground") e.FromPort = "height";
		TgContext nctx;
		nctx.Seed = 77; nctx.Resolution = 129; nctx.Size = 256.0f;
		TgResult a;
		if (Evaluate(noise, nctx, a))
		{
			const WaterField& water = a.WaterOutputs.at("water");
			const TgField& h = a.Outputs.at("height");
			const LakeMesh mesh = BuildLakeMesh(water, GroundOf(h));
			const Tally t = Validate(mesh, water, GroundOf(h), 1, "noise");
			Check(!water.MoreLakes().empty() && !mesh.Empty(), "noise: lakes, and a mesh");
			Check(SameBytes(mesh, BuildLakeMesh(water, GroundOf(h))), "noise: the same bytes twice");
			std::printf("lake_mesh: noise 129 x 129, %zu lakes, %zu cells, %zu vertices, %zu triangles, %zu shore triangles left out, %zu skirt corners\n",
				mesh.Bodies.size(), mesh.LakeCells, mesh.Vertices.size(), mesh.TriangleCount(), mesh.DroppedShoreTriangles, t.SkirtAbove);
		}
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("lake_mesh: all checks passed\n");
	return failures ? 1 : 0;
}
