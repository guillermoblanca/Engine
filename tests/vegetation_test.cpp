// Procedural trees (broadleaf, conifer, palm, bare) and rocks: determinism, requested size, per-species triangle budgets,
// level of detail and style variety. Needs OpenGL 4.5 (the meshes live on the GPU): skips without it.
// GB_VEGETATION_PREVIEW=dir also renders close-up lineups of every species and rock there (a look-and-compare aid).
#include "GB/Log.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/GrassGpu.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/RocksGpu.h"
#include "GB/Renderer/TreesGpu.h"
#include "GB/Platform/Window.h"
#include "GB/World/Grass.h"
#include "GB/World/Rocks.h"
#include "GB/World/Trees.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	GB::TreeParams Params(GB::TreeSpecies sp, float h, uint32_t seed, int style = -1, int detail = 1)
	{
		GB::TreeParams p;
		p.Species = sp; p.Height = h; p.Seed = seed; p.Style = style; p.Detail = detail;
		return p;
	}
}

int GBTest_vegetation(int, char**)
{
	GB::Log::Init();
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return 77;
	}
	GB::TreeFactory factory;
	using GB::TreeSpecies;

	// determinism and variety
	{
		auto a = factory.Create(Params(TreeSpecies::Broadleaf, 8.0f, 3));
		auto b = factory.Create(Params(TreeSpecies::Broadleaf, 8.0f, 3));
		auto c = factory.Create(Params(TreeSpecies::Broadleaf, 8.0f, 4));
		Check(a->TriangleCount == b->TriangleCount && a->CrownRadius == b->CrownRadius, "a tree is deterministic for a given seed");
		Check(a->TriangleCount != c->TriangleCount || a->CrownRadius != c->CrownRadius, "different seeds give different trees");
		float wide = 0.0f, narrow = 0.0f;
		for (uint32_t s = 1; s <= 4; ++s)
		{
			wide += factory.Create(Params(TreeSpecies::Broadleaf, 8.0f, s, 0))->CrownRadius;
			narrow += factory.Create(Params(TreeSpecies::Broadleaf, 8.0f, s, 2))->CrownRadius;
		}
		Check(wide > narrow * 1.1f, "the spreading habit is wider than the columnar one");
	}

	// height, bounds and budgets per species
	struct Case { const char* Name; GB::TreeParams P; size_t MaxTriangles; };
	const Case cases[] = {
		{ "oak", Params(TreeSpecies::Broadleaf, 8.0f, 1, 0), 9500 }, { "lime", Params(TreeSpecies::Broadleaf, 10.0f, 2, 1), 9500 }, { "poplar", Params(TreeSpecies::Broadleaf, 9.0f, 3, 2), 9500 },
		{ "spruce", Params(TreeSpecies::Conifer, 11.0f, 4, 0), 7000 }, { "pine", Params(TreeSpecies::Conifer, 11.0f, 5, 1), 7000 },
		{ "coconut", Params(TreeSpecies::Palm, 7.5f, 1, 0), 4000 }, { "date", Params(TreeSpecies::Palm, 9.0f, 2, 1), 4000 },
		{ "bare", Params(TreeSpecies::Bare, 8.0f, 7), 9500 },
		{ "sapling", Params(TreeSpecies::Broadleaf, 5.0f, 8, 3), 4000 }, { "cypress", Params(TreeSpecies::Conifer, 10.0f, 6, 2), 7000 } };   // T111: Style 3 / Style 2
	for (const Case& cs : cases)
	{
		auto t = factory.Create(cs.P);
		char msg[160];
		std::snprintf(msg, sizeof msg, "%s reaches its requested height (%.2f of %.2f)", cs.Name, t->BoundsHeight, cs.P.Height);
		Check(t->BoundsHeight > cs.P.Height * 0.93f && t->BoundsHeight < cs.P.Height * 1.08f, msg);
		std::snprintf(msg, sizeof msg, "%s stays inside its triangle budget (%zu <= %zu)", cs.Name, t->TriangleCount, cs.MaxTriangles);
		Check(t->TriangleCount <= cs.MaxTriangles && t->TriangleCount > 300, msg);
		std::snprintf(msg, sizeof msg, "%s has a sensible crown radius (%.2f)", cs.Name, t->CrownRadius);
		Check(t->CrownRadius > cs.P.Height * 0.12f && t->CrownRadius < cs.P.Height * 0.9f, msg);
		Check((cs.P.Species == TreeSpecies::Bare) == (t->Foliage.Empty()), "only bare trees have no foliage");
		std::fprintf(stderr, "  %-8s %6zu triangles (%zu cards), crown %.2f m, hash bark %016llx leaves %016llx\n", cs.Name, t->TriangleCount, t->LeafCardCount,
		             t->CrownRadius, static_cast<unsigned long long>(t->Trunk.Hash()), static_cast<unsigned long long>(t->Foliage.Hash()));
	}

	// level of detail and palm fruit
	{
		for (TreeSpecies sp : { TreeSpecies::Broadleaf, TreeSpecies::Conifer, TreeSpecies::Palm })
		{
			auto lo = factory.Create(Params(sp, 9.0f, 6, -1, 0)), mid = factory.Create(Params(sp, 9.0f, 6, -1, 1)), hi = factory.Create(Params(sp, 9.0f, 6, -1, 2));
			Check(lo->TriangleCount < mid->TriangleCount * 0.9f && mid->TriangleCount < hi->TriangleCount, "the detail levels order the triangle count");
			std::fprintf(stderr, "  detail %zu / %zu / %zu\n", lo->TriangleCount, mid->TriangleCount, hi->TriangleCount);
		}
		GB::TreeParams p = Params(TreeSpecies::Palm, 7.5f, 1);
		p.Fruit = 0.0f;
		auto bare = factory.Create(p);
		p.Fruit = 1.0f;
		auto fruity = factory.Create(p);
		Check(fruity->TriangleCount > bare->TriangleCount + 100, "coconuts add geometry");
	}

	// rocks
	{
		GB::RockParams p;
		p.Seed = 9;
		std::vector<GB::Vertex> flat, rough;
		std::vector<uint32_t> fi, ri;
		p.Strata = 0.0f; p.Cracks = 0.0f; p.Erosion = 0.0f;
		GB::RockFactory::BuildGeometry(p, flat, fi);
		p.Strata = 1.0f; p.Cracks = 1.0f; p.Erosion = 1.0f;
		GB::RockFactory::BuildGeometry(p, rough, ri);
		Check(flat.size() == rough.size(), "the surface detail keeps the triangle count");
		double diff = 0.0;
		for (size_t i = 0; i < flat.size(); ++i) diff += glm::length(flat[i].Position - rough[i].Position);
		Check(diff / static_cast<double>(flat.size()) > 0.002, "strata, cracks and erosion change the surface");
		// silhouette: a cut rock is less spherical than a balloon (spread of the radius around the centroid)
		auto spread = [](const std::vector<GB::Vertex>& v)
		{
			glm::vec3 c(0.0f);
			for (const auto& x : v) c += x.Position;
			c /= static_cast<float>(v.size());
			float lo = 1e9f, hi = 0.0f;
			for (const auto& x : v) { const float r = glm::length(x.Position - c); lo = std::min(lo, r); hi = std::max(hi, r); }
			return hi / lo;
		};
		Check(spread(flat) > 1.35f, "the rock silhouette is not a balloon");
		for (uint32_t s = 1; s <= 12; ++s)
		{
			p.Seed = s;
			std::vector<GB::Vertex> v;
			std::vector<uint32_t> idx;
			GB::RockFactory::BuildGeometry(p, v, idx);
			bool finite = true;
			for (const auto& x : v)
				if (!std::isfinite(x.Position.x + x.Position.y + x.Position.z) || !std::isfinite(x.Normal.x + x.Normal.y + x.Normal.z)) finite = false;
			Check(finite, "rock vertices and normals are finite for every seed");
		}
	}

	// T128: the bark's data maps (normal, height for the cavity, roughness), painted from the albedo's pattern: there, the albedo's size,
	// with real relief (the mean tilt of the normal map) and, on the fissured bark, furrows (texels low enough for the cavity) between plates
	{
		const GB::TreeImages& images = GB::TreeFactory::Textures();
		for (const GB::TreeTexture t : { GB::TreeTexture::Bark, GB::TreeTexture::BarkPalm })
		{
			const size_t k = static_cast<size_t>(t);
			const GB::Image& a = images.Images[k];
			const GB::Image& n = images.Normals[k];
			const GB::Image& h = images.Heights[k];
			const GB::Image& r = images.Roughness[k];
			const bool sized = n.IsValid() && h.IsValid() && r.IsValid() && n.Width == a.Width && n.Height == a.Height && h.Width == a.Width &&
				h.Height == a.Height && r.Width == a.Width && r.Height == a.Height;
			Check(sized, "the bark has its normal, height and roughness maps, at the albedo's size");
			if (!sized) continue;
			double tilt = 0.0;
			size_t low = 0;
			const size_t texels = static_cast<size_t>(n.Width) * n.Height;
			for (size_t i = 0; i < texels; ++i)
			{
				const double nx = n.Pixels[i * 4] / 255.0 * 2.0 - 1.0, ny = n.Pixels[i * 4 + 1] / 255.0 * 2.0 - 1.0;
				tilt += std::sqrt(nx * nx + ny * ny);
				if (h.Pixels[i * 4] < 90) ++low;
			}
			tilt /= static_cast<double>(texels);
			const double furrows = static_cast<double>(low) / static_cast<double>(texels);
			std::fprintf(stderr, "  bark map %s: mean tilt %.3f, furrow texels %.1f%%\n", t == GB::TreeTexture::Bark ? "bark" : "palm", tilt, 100.0 * furrows);
			Check(tilt > 0.15, "the bark's normal map has relief (mean tilt over 0.15)");
			if (t == GB::TreeTexture::Bark)
				Check(furrows > 0.05 && furrows < 0.45, "the fissured bark has furrows between its plates (5-45% of the texels low)");
		}
	}

	// T128: GB_TREE_BENCH=<rounds>: the GPU cost of the trees' materials, in one process (the same clocks for every variant): a grove of
	// TreesGpu trees at 1920x1080, MSAA 4, seen from eye height in the forest; the variants take turns, `rounds` times, 40 frames each
	// (10 of warm-up), and the objects pass (GpuSceneMs) is reported as the median of the per-round means and the minimum frame
	if (const char* benchEnv = std::getenv("GB_TREE_BENCH"))
	{
		const int rounds = std::max(1, std::atoi(benchEnv));
		constexpr uint32_t BW = 1920, BH = 1080;
		GB::Renderer renderer(BW, BH, 4, 2048);
		GB::Framebuffer fb(BW, BH);
		GB::TreesGpu treesGpu;
		GB::Mesh floor = GB::Mesh::CreateGrid(160.0f, 8);
		std::vector<std::unique_ptr<GB::TreeGpu>> kinds;
		for (uint32_t s = 1; s <= 6; ++s) kinds.push_back(treesGpu.Create(Params(TreeSpecies::Broadleaf, 8.0f + 0.4f * static_cast<float>(s), s, static_cast<int>(s) % 3)));
		for (uint32_t s = 1; s <= 3; ++s) kinds.push_back(treesGpu.Create(Params(TreeSpecies::Conifer, 10.0f + static_cast<float>(s), s + 3, static_cast<int>(s) % 2)));
		struct Look { GB::Material Trunk, Foliage; };
		std::vector<Look> full;
		for (const auto& k : kinds) full.push_back({ k->TrunkMaterial, k->FoliageMaterial });
		auto plainTrunk = [](GB::Material m) { m.NormalMap = nullptr; m.HeightMap = nullptr; m.MetallicRoughnessMap = nullptr; m.CavityStrength = 0.0f; return m; };
		auto plainLeaves = [](GB::Material m)
		{
			m.Translucency = 0.0f; m.ColorVariation = 0.0f; m.MacroVariation = 0.0f; m.NormalBlend = 0.0f; m.AlphaMipScale = 0.0f;
			m.BaseOcclusion = 0.0f; m.OcclusionHeight = 0.0f; m.InteriorOcclusion = 0.0f; m.BacklightSpread = 0.0f;
			return m;
		};
		struct Variant { const char* Name; std::function<Look(const Look&)> Make; };
		const std::vector<Variant> variants = {
			{ "base (T118)", [&](const Look& l) { return Look{ plainTrunk(l.Trunk), plainLeaves(l.Foliage) }; } },
			{ "bark maps only", [&](const Look& l) { return Look{ l.Trunk, plainLeaves(l.Foliage) }; } },
			{ "foliage only", [&](const Look& l) { return Look{ plainTrunk(l.Trunk), l.Foliage }; } },
			{ "full", [&](const Look& l) { return l; } },
			{ "full, alphaMip 0", [&](const Look& l) { Look v = l; v.Foliage.AlphaMipScale = 0.0f; return v; } },
			{ "full, spread 0", [&](const Look& l) { Look v = l; v.Foliage.BacklightSpread = 0.0f; return v; } },
			{ "full, interior 0", [&](const Look& l) { Look v = l; v.Foliage.InteriorOcclusion = 0.0f; v.Foliage.OcclusionHeight = 0.0f; return v; } },
			{ "full, no colour var", [&](const Look& l) { Look v = l; v.Foliage.ColorVariation = 0.0f; v.Foliage.MacroVariation = 0.0f; return v; } },
			{ "full, macro 0", [&](const Look& l) { Look v = l; v.Foliage.MacroVariation = 0.0f; return v; } },
			{ "full, per-tree var 0", [&](const Look& l) { Look v = l; v.Foliage.ColorVariation = 0.0f; return v; } },
			{ "full, translucency 0", [&](const Look& l) { Look v = l; v.Foliage.Translucency = 0.0f; return v; } },
			{ "translucency+spread only", [&](const Look& l) { Look v{ l.Trunk, plainLeaves(l.Foliage) }; v.Foliage.Translucency = l.Foliage.Translucency; v.Foliage.TransmissionTint = l.Foliage.TransmissionTint; v.Foliage.BacklightSpread = l.Foliage.BacklightSpread; return v; } },
			{ "colour var only", [&](const Look& l) { Look v{ l.Trunk, plainLeaves(l.Foliage) }; v.Foliage.ColorVariation = l.Foliage.ColorVariation; return v; } },
			{ "full, bark normal only", [&](const Look& l) { Look v = l; v.Trunk.HeightMap = nullptr; v.Trunk.MetallicRoughnessMap = nullptr; v.Trunk.CavityStrength = 0.0f; return v; } },
		};
		auto grove = [&](const std::vector<Look>& looks)
		{
			GB::Scene sc;
			GB::RenderObject f;
			f.MeshPtr = &floor; f.Mat.Albedo = { 0.25f, 0.33f, 0.14f }; f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
			sc.Objects.push_back(f);
			for (size_t k = 0; k < kinds.size(); ++k) { kinds[k]->TrunkMaterial = looks[k].Trunk; kinds[k]->FoliageMaterial = looks[k].Foliage; }
			for (int z = 0; z < 14; ++z)
				for (int x = 0; x < 14; ++x)
				{
					const uint32_t h = static_cast<uint32_t>(x * 73 + z * 151);
					const glm::vec3 at((static_cast<float>(x) - 6.5f) * 6.5f + static_cast<float>(h % 7) * 0.4f, 0.0f, -static_cast<float>(z) * 6.5f - 4.0f + static_cast<float>(h % 5) * 0.5f);
					kinds[h % kinds.size()]->AddToScene(sc, at, 0.37f * static_cast<float>(h % 17), 0.9f + 0.05f * static_cast<float>(h % 5));
				}
			sc.Env.ShadowCenter = { 0, 4, -30 }; sc.Env.ShadowRadius = 60.0f;
			sc.Sun.Direction = glm::normalize(glm::vec3(-0.4f, -0.75f, -0.5f));
			return sc;
		};
		GB::Camera cam; cam.Position = { 0.0f, 1.8f, 6.0f }; cam.Target = { 0.0f, 3.0f, -20.0f };
		std::vector<std::vector<double>> means(variants.size()), mins(variants.size());
		for (int r = 0; r < rounds; ++r)
			for (size_t v = 0; v < variants.size(); ++v)
			{
				std::vector<Look> looks;
				for (const Look& l : full) looks.push_back(variants[v].Make(l));
				const GB::Scene sc = grove(looks);
				double sum = 0.0, lo = 1e9;
				int n = 0;
				for (int i = 0; i < 40; ++i)
				{
					renderer.Render(sc, cam, &fb);
					glFinish();
					if (i < 10) continue;
					const double ms = renderer.GetStats().GpuSceneMs;
					sum += ms; lo = std::min(lo, ms); ++n;
				}
				means[v].push_back(sum / n);
				mins[v].push_back(lo);
			}
		auto median = [](std::vector<double> x) { std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
		std::printf("tree bench: %d trees of %zu kinds, %ux%u MSAA 4, %d rounds of 30 frames (objects pass, ms)\n", 14 * 14, kinds.size(), BW, BH, rounds);
		const double base = median(means[0]);
		for (size_t v = 0; v < variants.size(); ++v)
		{
			const double m = median(means[v]);
			std::printf("  %-24s median of means %.3f (%+.1f%%), min %.3f\n", variants[v].Name, m, 100.0 * (m / base - 1.0), *std::min_element(mins[v].begin(), mins[v].end()));
		}
		for (size_t k = 0; k < kinds.size(); ++k) { kinds[k]->TrunkMaterial = full[k].Trunk; kinds[k]->FoliageMaterial = full[k].Foliage; }
	}

	// T128: GB_GRASS_BENCH=<rounds>: the GPU cost of the grass pass (GpuGrassMs) by GrassGpu::Look, the same way: a 40 m field at eye
	// height, 1920x1080, MSAA 4, back-lit, the looks taking turns
	if (const char* benchEnv = std::getenv("GB_GRASS_BENCH"))
	{
		const int rounds = std::max(1, std::atoi(benchEnv));
		constexpr uint32_t BW = 1920, BH = 1080;
		GB::Renderer renderer(BW, BH, 4, 2048);
		GB::Framebuffer fb(BW, BH);
		GB::Mesh floor = GB::Mesh::CreateGrid(80.0f, 8);
		GB::GrassParams gp;
		gp.Seed = 5; gp.Size = 40.0f; gp.Density = 30.0f;
		GB::GrassField field(gp);
		GB::GrassGpu fieldGpu(field);
		using Look = GB::GrassGpu::Look;
		struct Variant { const char* Name; Look Shading; };
		Look before;   // T128's first pass: the game's defaults without the spread and the occlusion
		before.BacklightSpread = 0.0f; before.BaseOcclusion = 0.0f; before.OcclusionHeight = 0.0f; before.InteriorOcclusion = 0.0f;
		Look spreadOnly = before;
		spreadOnly.BacklightSpread = Look{}.BacklightSpread;
		Look occlusionOnly = before;
		occlusionOnly.BaseOcclusion = Look{}.BaseOcclusion; occlusionOnly.OcclusionHeight = Look{}.OcclusionHeight; occlusionOnly.InteriorOcclusion = Look{}.InteriorOcclusion;
		const std::vector<Variant> variants = { { "before (no spread, no occ.)", before }, { "off (T46)", Look::Off() }, { "full (defaults)", Look{} },
			{ "spread only", spreadOnly }, { "occlusion only", occlusionOnly } };
		GB::Camera cam; cam.Position = { 0.0f, 1.7f, 12.0f }; cam.Target = { 0.0f, 0.4f, -10.0f };
		std::vector<std::vector<double>> means(variants.size()), mins(variants.size());
		for (int r = 0; r < rounds; ++r)
			for (size_t v = 0; v < variants.size(); ++v)
			{
				fieldGpu.Shading = variants[v].Shading;
				GB::Scene sc;
				GB::RenderObject f;
				f.MeshPtr = &floor; f.Mat.Albedo = { 0.25f, 0.33f, 0.14f }; f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
				sc.Objects.push_back(f);
				sc.Grass.push_back(&fieldGpu);
				sc.Env.ShadowCenter = { 0, 0, 0 }; sc.Env.ShadowRadius = 30.0f;
				sc.Sun.Direction = glm::normalize(glm::vec3(0.2f, -0.3f, 0.93f));
				double sum = 0.0, lo = 1e9;
				int n = 0;
				for (int i = 0; i < 40; ++i)
				{
					renderer.Render(sc, cam, &fb);
					glFinish();
					if (i < 10) continue;
					const double ms = renderer.GetStats().GpuGrassMs;
					sum += ms; lo = std::min(lo, ms); ++n;
				}
				means[v].push_back(sum / n);
				mins[v].push_back(lo);
			}
		auto median = [](std::vector<double> x) { std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
		std::printf("grass bench: %zu blades, %ux%u MSAA 4, %d rounds of 30 frames (grass pass, ms)\n", field.GetBladeCount(), BW, BH, rounds);
		const double base = median(means[0]), baseMin = *std::min_element(mins[0].begin(), mins[0].end());
		for (size_t v = 0; v < variants.size(); ++v)
		{
			const double m = median(means[v]), lo = *std::min_element(mins[v].begin(), mins[v].end());
			std::printf("  %-28s median of means %.3f (%+.1f%%), min %.3f (%+.1f%%)\n", variants[v].Name, m, 100.0 * (m / base - 1.0), lo, 100.0 * (lo / baseMin - 1.0));
		}
	}

	// optional close-up lineups
	if (const char* dir = std::getenv("GB_VEGETATION_PREVIEW"))
	{
		constexpr uint32_t W = 1600, H = 700;
		GB::Renderer renderer(W, H, 4, 2048);
		GB::TreesGpu treesGpu;
		GB::Framebuffer fb(W, H);
		GB::Mesh floor = GB::Mesh::CreateGrid(80.0f, 8);
		auto floorObject = [&](const glm::vec3& color)
		{
			GB::RenderObject f;
			f.MeshPtr = &floor; f.Mat.Albedo = color; f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
			return f;
		};
		auto lineup = [&](const char* name, const std::vector<GB::TreeParams>& list, float spacing, float camDist, float camHeight)
		{
			GB::Scene sc;
			sc.Objects.push_back(floorObject({ 0.28f, 0.4f, 0.16f }));
			std::vector<std::unique_ptr<GB::TreeGpu>> keep;
			for (size_t i = 0; i < list.size(); ++i)
			{
				keep.push_back(treesGpu.Create(list[i]));
				keep.back()->AddToScene(sc, { (static_cast<float>(i) - 0.5f * static_cast<float>(list.size() - 1)) * spacing, 0.0f, 0.0f }, 0.0f, 1.0f);
			}
			sc.Env.ShadowCenter = { 0, 3, 0 }; sc.Env.ShadowRadius = 26.0f;
			sc.Sun.Direction = { -0.5f, -0.6f, -0.45f };
			GB::Camera cam; cam.Position = { 0.0f, camHeight, camDist }; cam.Target = { 0.0f, camHeight * 0.9f, 0.0f };
			renderer.Render(sc, cam, &fb);
			const std::string path = std::string(dir) + "/" + name + ".png";
			fb.SavePNG(path.c_str());
			std::fprintf(stderr, "preview: %s\n", path.c_str());
		};
		std::vector<GB::TreeParams> broad, conifers, palms, bare;
		for (uint32_t s = 1; s <= 5; ++s) broad.push_back(Params(TreeSpecies::Broadleaf, 8.0f + 0.5f * static_cast<float>(s), s, static_cast<int>(s) % 3));
		for (uint32_t s = 1; s <= 5; ++s) conifers.push_back(Params(TreeSpecies::Conifer, 9.0f + static_cast<float>(s), s + 3, static_cast<int>(s) % 2));
		for (uint32_t s = 1; s <= 5; ++s) palms.push_back(Params(TreeSpecies::Palm, 6.0f + 0.8f * static_cast<float>(s), s, static_cast<int>(s) % 2));
		for (uint32_t s = 1; s <= 4; ++s) bare.push_back(Params(TreeSpecies::Bare, 7.0f + static_cast<float>(s) * 0.4f, s));
		lineup("broadleaf", broad, 11.0f, 36.0f, 5.0f);
		lineup("conifer", conifers, 8.0f, 32.0f, 5.5f);
		lineup("palm", palms, 8.0f, 28.0f, 4.0f);
		lineup("bare", bare, 10.0f, 28.0f, 4.5f);
		{
			// T128: the bark in the sun from 3 m: a broadleaf and a bare trunk side by side
			GB::Scene bs;
			bs.Objects.push_back(floorObject({ 0.28f, 0.4f, 0.16f }));
			const std::unique_ptr<GB::TreeGpu> oak = treesGpu.Create(Params(TreeSpecies::Broadleaf, 9.0f, 2, 0));
			const std::unique_ptr<GB::TreeGpu> dead = treesGpu.Create(Params(TreeSpecies::Bare, 7.5f, 3));
			oak->AddToScene(bs, { -0.9f, 0.0f, 0.0f }, 0.0f, 1.0f);
			dead->AddToScene(bs, { 0.9f, 0.0f, 0.0f }, 0.0f, 1.0f);
			bs.Env.ShadowCenter = { 0, 1.5f, 0 }; bs.Env.ShadowRadius = 6.0f;
			bs.Sun.Direction = { -0.55f, -0.45f, -0.7f };
			GB::Camera cam; cam.Position = { 0.0f, 1.3f, 3.2f }; cam.Target = { 0.0f, 1.2f, 0.0f };
			renderer.Render(bs, cam, &fb);
			const std::string path = std::string(dir) + "/bark.png";
			fb.SavePNG(path.c_str());
			std::fprintf(stderr, "preview: %s\n", path.c_str());
		}

		GB::Scene sc;
		sc.Objects.push_back(floorObject({ 0.3f, 0.38f, 0.2f }));
		std::vector<std::unique_ptr<GB::RockGpu>> rocks;
		for (uint32_t s = 1; s <= 8; ++s)
		{
			GB::RockParams rp;
			rp.Seed = s * 7u; rp.Size = 1.0f; rp.Stretch = { 1.0f + 0.1f * static_cast<float>(s % 3), 0.55f + 0.12f * static_cast<float>(s % 4), 1.0f };
			rp.Moss = s % 3 == 0 ? 0.7f : 0.0f; rp.Sand = s % 3 == 1 ? 0.7f : 0.0f;
			rp.Color = s % 3 == 1 ? glm::vec3(0.55f, 0.40f, 0.28f) : glm::vec3(0.42f, 0.40f, 0.37f);
			rocks.push_back(GB::RocksGpu::Create(rp));
			rocks.back()->AddToScene(sc, { (static_cast<float>(s) - 4.5f) * 3.0f, 0.0f, 0.0f }, 0.5f * static_cast<float>(s), 1.0f, 0.0f);
		}
		sc.Env.ShadowCenter = { 0, 1, 0 }; sc.Env.ShadowRadius = 16.0f;
		sc.Sun.Direction = { -0.5f, -0.6f, -0.45f };
		GB::Camera cam; cam.Position = { 0.0f, 3.5f, 13.0f }; cam.Target = { 0.0f, 0.6f, 0.0f };
		renderer.Render(sc, cam, &fb);
		const std::string path = std::string(dir) + "/rocks.png";
		fb.SavePNG(path.c_str());
	}

	return failures;
}
