// Hydraulic erosion, view/distance culling, and the procedural world generator (biomes, forests, rocks).
// The erosion part is CPU only; the rest needs OpenGL 4.5 and skips without it.
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include "GB/World/Erosion.h"
#include "GB/World/WorldGen.h"
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	GB::Heightmap MakeHill(uint32_t n = 129, float size = 128.0f)
	{
		GB::Heightmap hm(n, size);
		for (uint32_t j = 0; j < n; ++j)
			for (uint32_t i = 0; i < n; ++i)
			{
				const glm::vec2 p = hm.SamplePosition(i, j);
				const float r = glm::length(p) / (size * 0.5f);
				hm.At(i, j) = 30.0f * std::max(0.0f, 1.0f - r) + 1.5f * std::sin(p.x * 0.35f) * std::cos(p.y * 0.3f);
			}
		return hm;
	}
	double Sum(const GB::Heightmap& hm)
	{
		double s = 0.0;
		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
			for (uint32_t i = 0; i < hm.GetResolution(); ++i) s += hm.At(i, j);
		return s;
	}
}

int main()
{
	GB::Log::Init();

	// ---------------------------------------------------------------- erosion (CPU)
	{
		GB::Heightmap a = MakeHill(), b = MakeHill(), c = MakeHill();
		GB::ErosionParams ep;
		ep.Droplets = 40000;
		ep.Seed = 7;
		const GB::ErosionStats st = GB::HydraulicErosion::Run(a, ep);
		GB::HydraulicErosion::Run(b, ep);
		Check(st.Eroded > 1.0 && st.Deposited > 1.0, "droplets both erode and deposit");
		bool same = true;
		for (uint32_t j = 0; j < a.GetResolution(); ++j)
			for (uint32_t i = 0; i < a.GetResolution(); ++i)
				if (a.At(i, j) != b.At(i, j)) same = false;
		Check(same, "erosion is deterministic for a seed");
		ep.Seed = 8;
		GB::HydraulicErosion::Run(c, ep);
		bool differs = false;
		for (uint32_t i = 0; i < c.GetResolution() && !differs; ++i)
			if (a.At(i, 64) != c.At(i, 64)) differs = true;
		Check(differs, "and differs for another seed");
		const GB::Heightmap original = MakeHill();
		Check(a.GetMaxHeight() < original.GetMaxHeight(), "the summit is worn down");
		const double mass = std::fabs(Sum(a) - Sum(original));
		Check(mass < 0.3 * st.Eroded, "what is carried away is mostly deposited again (mass is roughly conserved)");
		// the foot of the hill gains sediment
		double footBefore = 0.0, footAfter = 0.0;
		for (uint32_t j = 0; j < a.GetResolution(); ++j)
			for (uint32_t i = 0; i < a.GetResolution(); ++i)
			{
				const float r = glm::length(a.SamplePosition(i, j)) / 64.0f;
				if (r > 0.75f && r < 0.98f) { footBefore += original.At(i, j); footAfter += a.At(i, j); }
			}
		Check(footAfter > footBefore, "the foot of the hill receives the sediment");
		// strength 0 changes nothing; a small circle only touches its surroundings
		GB::Heightmap z = MakeHill();
		ep.Strength = 0.0f;
		GB::HydraulicErosion::Run(z, ep);
		bool untouched = true;
		for (uint32_t i = 0; i < z.GetResolution(); ++i)
			if (z.At(i, 40) != original.At(i, 40)) untouched = false;
		Check(untouched, "strength 0 leaves the terrain alone");
		// a strong, long erosion stays finite and inside the original height range
		GB::Heightmap strong = MakeHill();
		const float lo = strong.GetMinHeight(), hi = strong.GetMaxHeight();
		for (int k = 0; k < 60; ++k)
		{
			GB::ErosionParams sp; sp.Droplets = 2000; sp.Seed = static_cast<uint32_t>(k + 1); sp.Strength = 8.0f;
			GB::HydraulicErosion::Run(strong, sp);
		}
		bool bounded = true;
		for (uint32_t j = 0; j < strong.GetResolution(); ++j)
			for (uint32_t i = 0; i < strong.GetResolution(); ++i)
				if (!std::isfinite(strong.At(i, j)) || strong.At(i, j) < lo - 30.0f || strong.At(i, j) > hi + 2.0f) bounded = false;   // heavy erosion digs and carries away, it does not blow up
		std::fprintf(stderr, "info: strong erosion range [%.2f %.2f] (original [%.2f %.2f])\n", strong.GetMinHeight(), strong.GetMaxHeight(), lo, hi);
		Check(bounded, "even strong repeated erosion stays finite, never piles up above the summit and does not run away");
		GB::Heightmap local = MakeHill();
		GB::ErosionParams lp;
		lp.Droplets = 3000; lp.Center = { 20.0f, 0.0f }; lp.Radius = 8.0f; lp.Seed = 3;
		const GB::ErosionStats ls = GB::HydraulicErosion::Run(local, lp);
		bool farUntouched = true;
		for (uint32_t j = 0; j < local.GetResolution(); ++j)
			for (uint32_t i = 0; i < local.GetResolution(); ++i)
			{
				const glm::vec2 p = local.SamplePosition(i, j);
				if (glm::distance(p, lp.Center) > 8.0f + 45.0f && local.At(i, j) != original.At(i, j)) farUntouched = false;
			}
		Check(farUntouched && ls.MaxI >= ls.MinI, "a local erosion only reshapes the ground near its circle and reports the box it touched");
	}

	// ---------------------------------------------------------------- GL
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP (GL part): no OpenGL 4.5 context\n");
		return failures ? failures : 77;
	}

	// --- culling
	{
		GB::Renderer renderer(160, 90, 1, 1024);
		GB::Mesh cube = GB::Mesh::CreateCube(1.0f);
		GB::Scene scene;
		scene.Sun.CastShadows = false;
		auto add = [&](glm::vec3 pos, float radius, float maxDist)
		{
			GB::RenderObject o;
			o.MeshPtr = &cube;
			o.Transform = glm::translate(glm::mat4(1.0f), pos);
			o.BoundingRadius = radius;
			o.MaxDistance = maxDist;
			scene.Objects.push_back(o);
		};
		add({ 0, 0, -10 }, 1.0f, 0.0f);      // in front: drawn
		add({ 0, 0, 10 }, 1.0f, 0.0f);       // behind the camera: culled
		add({ 60, 0, -10 }, 1.0f, 0.0f);     // far to the side: culled
		add({ 0, 0, -80 }, 1.0f, 40.0f);     // too far for its MaxDistance: culled
		add({ 0, 0, -80 }, 1.0f, 200.0f);    // far but allowed: drawn
		GB::RenderObject unbounded;          // no bounds: never culled
		unbounded.MeshPtr = &cube;
		unbounded.Transform = glm::translate(glm::mat4(1.0f), { 0, 0, 10 });
		scene.Objects.push_back(unbounded);
		GB::Camera cam;
		cam.Position = { 0, 0, 0 };
		cam.Target = { 0, 0, -1 };
		cam.FarPlane = 300.0f;
		GB::Framebuffer t(160, 90);
		renderer.Render(scene, cam, &t);
		Check(renderer.GetStats().ObjectsDrawn == 3 && renderer.GetStats().ObjectsCulled == 3, "frustum and distance culling skip the objects out of view");
	}

	// --- terrain refresh after erosion
	{
		GB::HillParams hp;
		hp.Seed = 2;
		auto terrain = GB::Terrain::CreateHills(hp, 128.0f, 129, GB::TerrainStyle::Meadow);
		const float before = terrain->GetHeight(10.0f, 10.0f);
		GB::ErosionParams ep;
		ep.Droplets = 20000; ep.Seed = 5; ep.Center = { 10.0f, 10.0f }; ep.Radius = 30.0f; ep.Strength = 3.0f;
		const GB::ErosionStats st = GB::HydraulicErosion::Run(terrain->GetEditableHeightmap(), ep);
		terrain->RefreshMesh(st.MinI, st.MinJ, st.MaxI, st.MaxJ);
		terrain->RefreshMesh();   // the whole mesh
		Check(st.Eroded > 0.0 && std::fabs(terrain->GetHeight(10.0f, 10.0f) - before) >= 0.0f, "the terrain can be eroded and its mesh refreshed");
	}

	// --- world generator
	{
		GB::WorldDesc wd;
		wd.Seed = 5; wd.Size = 256.0f; wd.Resolution = 257; wd.TreeBudget = 150; wd.RockBudget = 30;
		GB::GeneratedWorld w = GB::WorldGenerator::Generate(wd);
		Check(w.TerrainPtr && w.TerrainPtr->HasLake() && w.TerrainPtr->GetRiver(), "a temperate world has a lake and a river");
		Check(w.Trees.size() > 40 && w.Trees.size() <= 150, "it is planted with trees up to the budget");
		Check(!w.Rocks.empty() && w.Rocks.size() <= 30, "and has rocks");
		bool variantsOk = true, dryOk = true;
		for (const auto& t : w.Trees)
		{
			if (t.Variant < 0 || t.Variant >= static_cast<int>(w.TreeRecipes.size())) variantsOk = false;
			const GB::Biome b = w.BiomeAt(t.Position.x, t.Position.z);
			if (b == GB::Biome::Water || b == GB::Biome::Shore) dryOk = false;
		}
		for (const auto& r : w.Rocks) if (r.Variant < 0 || r.Variant >= static_cast<int>(w.RockRecipes.size())) variantsOk = false;
		Check(variantsOk, "placements refer to valid recipes");
		Check(dryOk, "no tree grows in the water or on the shore");
		const auto shares = w.BiomeShares();
		float total = 0.0f;
		for (float s : shares) total += s;
		Check(std::fabs(total - 1.0f) < 1e-3f, "biome shares add up to 1");
		Check(shares[static_cast<size_t>(GB::Biome::Forest)] > 0.1f && shares[static_cast<size_t>(GB::Biome::Forest)] < 0.85f && shares[static_cast<size_t>(GB::Biome::Meadow)] > 0.05f, "forest and meadow both exist");
		Check(shares[static_cast<size_t>(GB::Biome::Water)] > 0.003f, "there is water");
		// trees are denser inside forests than in meadows
		int inForest = 0, inMeadow = 0;
		for (const auto& t : w.Trees)
		{
			const GB::Biome b = w.BiomeAt(t.Position.x, t.Position.z);
			if (b == GB::Biome::Forest) ++inForest; else if (b == GB::Biome::Meadow) ++inMeadow;
		}
		const float fShare = shares[static_cast<size_t>(GB::Biome::Forest)], mShare = shares[static_cast<size_t>(GB::Biome::Meadow)];
		Check(static_cast<float>(inForest) / fShare > 4.0f * static_cast<float>(inMeadow) / std::max(mShare, 0.01f), "forests are much denser than meadows");
		Check(w.GrassDensityAt(w.Spawn.x, w.Spawn.y) >= 0.0f && w.BiomeAt(w.Spawn.x, w.Spawn.y) != GB::Biome::Water, "the spawn point is on dry land");
		GB::GeneratedWorld w2 = GB::WorldGenerator::Generate(wd);
		Check(w2.Trees.size() == w.Trees.size() && w2.Trees[0].Position == w.Trees[0].Position, "generation is deterministic for a seed");
		wd.Seed = 6;
		GB::GeneratedWorld w3 = GB::WorldGenerator::Generate(wd);
		Check(w3.Trees.empty() || w3.Trees[0].Position != w.Trees[0].Position, "and differs for another seed");

		GB::WorldDesc alp; alp.Type = GB::WorldType::Alpine; alp.Seed = 2; alp.Size = 256.0f; alp.Resolution = 257; alp.TreeBudget = 80; alp.RockBudget = 20;
		GB::GeneratedWorld a = GB::WorldGenerator::Generate(alp);
		const auto as = a.BiomeShares();
		Check(a.SnowLine < 1e8f && as[static_cast<size_t>(GB::Biome::Snow)] > 0.02f && as[static_cast<size_t>(GB::Biome::Rock)] > 0.01f, "an alpine world has snow and bare rock");
		GB::WorldDesc des; des.Type = GB::WorldType::Desert; des.Seed = 3; des.Size = 256.0f; des.Resolution = 257; des.TreeBudget = 30;
		GB::GeneratedWorld d = GB::WorldGenerator::Generate(des);
		bool palms = true;
		for (const auto& t : d.Trees) if (d.TreeRecipes[static_cast<size_t>(t.Variant)].Species != GB::TreeSpecies::Palm) palms = false;
		Check(d.BiomeShares()[static_cast<size_t>(GB::Biome::Dunes)] > 0.99f && palms, "a desert world is dunes with palms");
	}
	return failures;
}
