// Rivers (carved valley, downhill level, flow field, ribbon mesh, current on bodies) and procedural rocks
// (geometry, determinism, variety) plus their surface layers (moss, sand, wet band). GL part skips without OpenGL 4.5.
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include "GB/World/Physics.h"
#include "GB/World/Rocks.h"
#include "GB/World/Terrain.h"
#include "GB/World/Water.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

int main()
{
	GB::Log::Init();

	// ---------------------------------------------------------------- rocks (CPU geometry)
	{
		GB::RockParams p;
		p.Seed = 5;
		std::vector<GB::Vertex> v1, v2, v3;
		std::vector<uint32_t> i1, i2, i3;
		GB::RockFactory::BuildGeometry(p, v1, i1);
		GB::RockFactory::BuildGeometry(p, v2, i2);
		p.Seed = 6;
		GB::RockFactory::BuildGeometry(p, v3, i3);
		Check(v1.size() == v2.size() && v1.size() == 5120 * 3, "an icosphere of 4 subdivisions has 5120 triangles");
		bool same = true, differs = false;
		for (size_t i = 0; i < v1.size(); ++i)
		{
			if (v1[i].Position != v2[i].Position) same = false;
			if (v1[i].Position != v3[i].Position) differs = true;
		}
		Check(same, "a rock is deterministic for a given seed");
		Check(differs, "and different seeds give different rocks");
		float minY = 1e9f, maxY = -1e9f, maxR = 0.0f;
		bool unit = true;
		for (const auto& v : v1)
		{
			minY = std::min(minY, v.Position.y); maxY = std::max(maxY, v.Position.y);
			maxR = std::max(maxR, glm::length(glm::vec2(v.Position.x, v.Position.z)));
			if (std::fabs(glm::length(v.Normal) - 1.0f) > 1e-3f) unit = false;
		}
		Check(std::fabs(minY) < 1e-4f, "the base of the rock is at y = 0");
		Check(maxY > 0.5f && maxY < 2.2f && maxR > 0.5f && maxR < 2.2f, "its size is close to the requested radius");
		Check(unit, "all normals are unit length");
		p.Size = 2.0f; p.Seed = 5;
		std::vector<GB::Vertex> big; std::vector<uint32_t> bi;
		GB::RockFactory::BuildGeometry(p, big, bi);
		float bigY = 0.0f;
		for (const auto& v : big) bigY = std::max(bigY, v.Position.y);
		Check(bigY > maxY * 1.6f, "size scales the rock");
		// base is flat: many vertices sit on the ground plane
		int onBase = 0;
		for (const auto& v : v1) if (v.Position.y < 0.02f) ++onBase;
		Check(onBase > 60, "the base is flattened so the rock sits on the ground");
	}

	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP (GL part): no OpenGL 4.5 context\n");
		return failures ? failures : 77;
	}
	// ---------------------------------------------------------------- river (the terrain builds a mesh: needs GL)
	{
		GB::HillParams hp;
		hp.Seed = 3;
		hp.RiverPath = { { -90, 30 }, { -40, 0 }, { 10, -20 }, { 60, -10 }, { 100, -40 } };
		hp.RiverWidth = 6.0f;
		hp.RiverDepth = 1.4f;
		auto terrain = GB::Terrain::CreateHills(hp, 256.0f, 257, GB::TerrainStyle::Meadow);
		const GB::RiverData* river = terrain->GetRiver();
		Check(river && river->Points.size() > 50, "a river is carved along the path");
		{
			// Shore refinement: more triangles along the banks, the plain grid when switched off, and the refined surface stays on the heightmap.
			GB::HillParams plain = hp;
			plain.ShoreSubdivision = 1;
			auto flat = GB::Terrain::CreateHills(plain, 256.0f, 257, GB::TerrainStyle::Meadow);
			Check(flat->GetMesh().GetIndexCount() == 256u * 256u * 6u, "without shore refinement the mesh is two triangles per cell");
			Check(terrain->GetMesh().GetIndexCount() > flat->GetMesh().GetIndexCount() + 6000u, "the river banks are refined into many more triangles");
			float worst = 0.0f;
			for (const glm::vec3& pt : terrain->GetRiver()->Points)
				worst = std::max(worst, std::fabs(terrain->GetHeight(pt.x, pt.z) - flat->GetHeight(pt.x, pt.z)));
			Check(worst < 1e-4f, "refinement does not change the heightmap queries (physics, grass, trees)");
		}
		if (!river) return 1;
		bool monotonic = true;
		for (size_t i = 1; i < river->Points.size(); ++i)
			if (river->Points[i].y > river->Points[i - 1].y + 1e-5f) monotonic = false;
		Check(monotonic, "the water level only goes down along the river (it flows downhill)");
		Check(river->Points.front().y > river->Points.back().y + 0.1f, "and it actually descends");
		const glm::vec3 mid = river->Points[river->Points.size() / 2];
		Check(terrain->GetHeight(mid.x, mid.z) < mid.y - river->Depth * 0.6f, "the riverbed lies well below the water level");
		const glm::vec2 dirMid = glm::normalize(glm::vec2(river->Points[river->Points.size() / 2 + 1].x - river->Points[river->Points.size() / 2 - 1].x,
			river->Points[river->Points.size() / 2 + 1].z - river->Points[river->Points.size() / 2 - 1].z));
		const glm::vec2 side(-dirMid.y, dirMid.x);
		Check(terrain->GetHeight(mid.x + side.x * 12.0f, mid.z + side.y * 12.0f) > mid.y && terrain->GetHeight(mid.x - side.x * 12.0f, mid.z - side.y * 12.0f) > mid.y, "the land on both sides of the river is above the water");
		Check(terrain->GetRiverLevelAt(mid.x, mid.z) > -1e8f, "a point in the middle is in the water");
		float levelFar = terrain->GetRiverLevelAt(mid.x + 40.0f, mid.z + 40.0f);
		Check(levelFar < -1e8f, "a point far away is not");
		Check(terrain->DistanceToRiver(mid.x, mid.z) < 0.5f && terrain->DistanceToRiver(mid.x + side.x * 12.0f, mid.z + side.y * 12.0f) > 5.0f, "distance to the river");
		// no part of the bank is below the water level (the banks hold the water in)
		int leaks = 0;
		for (size_t i = 0; i < river->Points.size(); i += 4)
		{
			const glm::vec3 p = river->Points[i];
			for (float off : { 5.0f, 8.0f, 12.0f })
				for (float sgn : { -1.0f, 1.0f })
					if (terrain->GetHeight(p.x + sgn * off, p.z) < p.y + 0.05f && terrain->DistanceToRiver(p.x + sgn * off, p.z) > river->HalfWidth * 2.2f) ++leaks;
		}
		Check(leaks == 0, "the land beside the river is higher than the water");

		// flow: along the river direction, fastest in the middle, still outside
		const size_t k = river->Points.size() / 2;
		const glm::vec3 a = river->Points[k - 1], b = river->Points[k + 1];
		const glm::vec2 dir = glm::normalize(glm::vec2(b.x - a.x, b.z - a.z));
		const glm::vec2 flow = terrain->GetRiverFlow(mid.x, mid.z);
		Check(glm::dot(glm::normalize(flow), dir) > 0.98f && glm::length(flow) >= 0.9f, "the current runs along the river with its speed");
		Check(glm::length(terrain->GetRiverFlow(mid.x - dir.y * 2.4f, mid.z + dir.x * 2.4f)) < glm::length(flow), "slower near the banks");
		Check(glm::length(terrain->GetRiverFlow(mid.x + 40.0f, mid.z)) == 0.0f, "no current on land");

		// ribbon mesh data (CPU)
		GB::WaterSurface ribbon(*river);
		{
			const size_t cols = static_cast<size_t>(GB::WaterSurface::RiverColumns);
			Check(ribbon.IsRiver() && ribbon.GetRiverVertexCount() % cols == 0, "the water ribbon is a whole number of rows of RiverColumns vertices");
			Check(ribbon.GetRiverVertexCount() / cols >= river->Points.size() * 18 / 10, "the ribbon is resampled finer than the carve path (waves need the vertices)");
			Check(cols >= 17, "many columns across the river");
		}
		Check(!GB::WaterSurface(64.0f, 8).IsRiver(), "a lake surface is not a river");

		// ---- a river that ends in a lake: level, current and surface blend into the still water
		{
			GB::HillParams lp = hp;
			lp.LakeCenter = { 115.0f, -45.0f };
			lp.LakeRadius = 20.0f;
			lp.WaterLevel = 1.0f;
			auto lake = GB::Terrain::CreateHills(lp, 256.0f, 257, GB::TerrainStyle::Meadow);
			const GB::RiverData* lr = lake->GetRiver();
			Check(lr && lr->MouthLength > 0.0f, "a river that meets a lake knows it has a mouth");
			if (lr)
			{
				bool aboveLake = true, descending = true;
				for (size_t i = 0; i < lr->Points.size(); ++i)
				{
					if (lr->Points[i].y < lp.WaterLevel - 1e-4f) aboveLake = false;
					if (i > 0 && lr->Points[i].y > lr->Points[i - 1].y + 1e-5f) descending = false;
				}
				Check(aboveLake && descending, "the river never drops below the lake and only descends");
				Check(std::fabs(lr->Points.back().y - lp.WaterLevel) < 1e-4f, "it reaches the lake at exactly the lake level (no step between the surfaces)");
				Check(lr->Speed.back() < 0.05f && lr->Speed.front() > 0.5f, "the current dies down at the lake");
				bool calming = true;
				for (size_t i = lr->Points.size() - 8; i + 1 < lr->Points.size(); ++i)
					if (lr->Speed[i + 1] > lr->Speed[i] + 1e-4f) calming = false;
				Check(calming, "and does so smoothly, without speeding up again");

				GB::WaterSurface mouth(*lr);
				const size_t cols = static_cast<size_t>(GB::WaterSurface::RiverColumns), rows = mouth.GetRiverVertexCount() / cols;
				const auto& verts = mouth.GetRiverVertices();
				const size_t mid = cols / 2;
				Check(verts[mid].Normal.x > 0.999f && verts[(rows - 1) * cols + mid].Normal.x < 0.001f, "the ribbon is opaque upstream and fully faded at the end of its extension");
				Check(verts[(rows - 1) * cols + mid].Tangent.z < 1e-4f, "and still there");
				bool fades = true;
				for (size_t r = 1; r < rows; ++r)
					if (verts[r * cols + mid].Normal.x > verts[(r - 1) * cols + mid].Normal.x + 1e-5f) fades = false;
				Check(fades, "the opacity only decreases toward the lake (no seam)");
				const glm::vec3 last = glm::vec3(lr->Points.back().x, lr->Points.back().y, lr->Points.back().z);
				Check(glm::distance(glm::vec2(verts[(rows - 1) * cols + mid].Position.x, verts[(rows - 1) * cols + mid].Position.z), glm::vec2(last.x, last.z)) > 2.0f
					&& std::fabs(verts[(rows - 1) * cols + mid].Position.y - lp.WaterLevel) < 1e-3f, "the ribbon continues into the lake at the lake level");
			}

			// The shore has a bank, not a cliff: the steepest slope within 3 radii of the lake drops with LakeBankSlope.
			GB::HillParams cliff = lp;
			cliff.LakeBankSlope = 0.0f;
			auto old = GB::Terrain::CreateHills(cliff, 256.0f, 257, GB::TerrainStyle::Meadow);
			auto steepest = [&](const GB::Terrain& t)
			{
				float worst = 0.0f;
				for (int a = 0; a < 48; ++a)
				{
					const float ang = static_cast<float>(a) / 48.0f * 6.2831853f;
					const glm::vec2 d(std::cos(ang), std::sin(ang));
					bool nearRiver = false;
					float prev = 0.0f, local = 0.0f;
					for (float r = lp.LakeRadius * 0.9f; r < lp.LakeRadius * 3.0f; r += 1.0f)
					{
						const glm::vec2 q = lp.LakeCenter + d * r;
						if (t.DistanceToRiver(q.x, q.y) < 18.0f) nearRiver = true;
						const float h = t.GetHeight(q.x, q.y);
						if (r > lp.LakeRadius * 0.9f) local = std::max(local, std::fabs(h - prev));
						prev = h;
					}
					if (!nearRiver) worst = std::max(worst, local);
				}
				return worst;
			};
			const float withCone = steepest(*lake), without = steepest(*old);
			std::printf("shore slope: %.2f with the bank cone, %.2f without\n", withCone, without);
			Check(withCone <= 0.7f, "the lake bank is gentle");
			Check(withCone <= without + 1e-3f, "and never steeper than without the cone");
		}

		// smooth surface normals: the Catmull-Rom normals agree with the heightmap ones on average and are unit length
		{
			Check(terrain->GetNormalMap() != nullptr, "the terrain has a high-resolution normal map");
			const GB::Heightmap& hm = terrain->GetHeightmap();
			double dot = 0.0; int count = 0; bool unit = true;
			for (uint32_t j = 8; j + 8 < hm.GetResolution(); j += 5)
				for (uint32_t i = 8; i + 8 < hm.GetResolution(); i += 5)
				{
					const glm::vec2 q = hm.SamplePosition(i, j);
					const glm::vec3 n = terrain->GetSmoothNormal(q.x, q.y);
					if (std::fabs(glm::length(n) - 1.0f) > 1e-4f) unit = false;
					dot += glm::dot(n, hm.GetNormal(q.x, q.y)); ++count;
				}
			Check(unit && dot / count > 0.98, "smooth normals are unit length and follow the heightmap");
		}

		// a floating crate is carried downstream
		GB::PhysicsWorld w;
		w.SetTerrain(&terrain->GetHeightmap());
		const GB::Terrain* tp = terrain.get();
		w.SetWater([tp](float x, float z) { return tp->GetRiverLevelAt(x, z); });
		w.SetCurrent([tp](float x, float z) { const glm::vec2 f = tp->GetRiverFlow(x, z); return glm::vec3(f.x, 0.0f, f.y); });
		GB::BodyDesc d; d.HalfExtents = glm::vec3(0.3f); d.Density = 400.0f;
		const glm::vec3 start = river->Points[20];
		const int id = w.AddBody(d, { start.x, start.y + 0.5f, start.z });
		for (float t = 0.0f; t < 12.0f; t += 1.0f / 60.0f) w.Step(1.0f / 60.0f);
		const glm::vec3 end = w.GetBody(id).Position;
		// how far along the river it got: the river point nearest to it
		size_t nearest = 0; float bd = 1e9f;
		for (size_t i = 0; i < river->Points.size(); ++i)
		{
			const float dd = glm::length(glm::vec2(river->Points[i].x - end.x, river->Points[i].z - end.z));
			if (dd < bd) { bd = dd; nearest = i; }
		}
		std::fprintf(stderr, "info: crate went from river point 20 to %zu (offset %.1f m)\n", nearest, bd);
		Check(nearest >= 20 + 5, "a floating crate is carried downstream by the current");
	}

	// ---------------------------------------------------------------- material layers
	GB::Renderer renderer(160, 90, 1, 1024);
	Check(renderer.IsValid(), "renderer initializes");
	GB::RockParams rp;
	rp.Seed = 2; rp.Size = 1.0f; rp.Color = glm::vec3(0.45f);
	auto bare = GB::RockFactory::Create(rp);
	Check(bare && bare->MeshPtr && bare->TriangleCount == 5120, "a rock mesh is uploaded");
	GB::Camera cam;
	cam.Position = { 0.0f, 4.0f, 3.0f };
	cam.Target = { 0.0f, 0.6f, 0.0f };
	auto average = [&](const GB::Scene& sc, const GB::Camera& view, int y0 = 25, int y1 = 65)
	{
		GB::Framebuffer t(160, 90);
		renderer.Render(sc, view, &t);
		if (std::getenv("GB_TEST_DUMP")) { static int n = 0; char name[64]; std::snprintf(name, sizeof name, "/tmp/rock_%d.png", n++); t.SavePNG(name); }
		const auto v = t.ReadPixels();
		glm::dvec3 sum(0.0); int n = 0;
		for (int y = y0; y < y1; ++y)
			for (int x = 55; x < 105; ++x)
			{
				const uint8_t* p = &v[(static_cast<size_t>(y) * 160 + x) * 4];
				sum += glm::dvec3(p[0], p[1], p[2]); ++n;
			}
		return sum / static_cast<double>(n);
	};
	auto sceneWith = [&](const GB::Rock& r, float waterLine = -1e9f)
	{
		GB::Scene sc;
		sc.Sun.CastShadows = false;
		sc.Sun.Intensity = 1.6f;
		sc.Env.Intensity = 0.6f;
		r.AddToScene(sc, { 0, 0, 0 }, 0.0f, 1.0f, 0.0f, waterLine);
		return sc;
	};
	const glm::dvec3 plain = average(sceneWith(*bare), cam);
	rp.Moss = 0.9f;
	auto mossy = GB::RockFactory::Create(rp);
	const glm::dvec3 green = average(sceneWith(*mossy), cam);
	std::fprintf(stderr, "info: rock average colour bare %.0f %.0f %.0f, mossy %.0f %.0f %.0f\n", plain.x, plain.y, plain.z, green.x, green.y, green.z);
	Check(green.y - green.x > plain.y - plain.x + 8.0, "moss makes the top of a rock greener");
	rp.Moss = 0.0f; rp.Sand = 1.0f;
	auto sandy = GB::RockFactory::Create(rp);
	GB::Camera low;   // from the side, where the sand lies
	low.Position = { 0.0f, 0.4f, 4.5f };
	low.Target = { 0.0f, 0.4f, 0.0f };
	const glm::dvec3 plainSide = average(sceneWith(*bare), low, 49, 56), sand = average(sceneWith(*sandy), low, 49, 56);
	std::fprintf(stderr, "info: side view bare %.0f %.0f %.0f, sandy %.0f %.0f %.0f\n", plainSide.x, plainSide.y, plainSide.z, sand.x, sand.y, sand.z);
	Check(sand.x - sand.z > plainSide.x - plainSide.z + 6.0, "sand makes the rock warmer at the base");
	const glm::dvec3 wet = average(sceneWith(*bare, 5.0f), cam);   // water line above the whole rock
	Check(wet.x + wet.y + wet.z < (plain.x + plain.y + plain.z) * 0.85, "a rock under the water line is darker (wet)");
	return failures;
}
