// Procedural trees (broadleaf, conifer, palm, bare) and rocks: determinism, requested size, per-species triangle budgets,
// level of detail and style variety. Needs OpenGL 4.5 (the meshes live on the GPU): skips without it.
// GB_VEGETATION_PREVIEW=dir also renders close-up lineups of every species and rock there (a look-and-compare aid).
#include "GB/Log.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include "GB/World/Rocks.h"
#include "GB/World/Trees.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

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

int main()
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
		{ "bare", Params(TreeSpecies::Bare, 8.0f, 7), 9500 } };
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
		Check((cs.P.Species == TreeSpecies::Bare) == (t->Foliage == nullptr), "only bare trees have no foliage");
		std::fprintf(stderr, "  %-8s %6zu triangles (%zu cards), crown %.2f m\n", cs.Name, t->TriangleCount, t->LeafCardCount, t->CrownRadius);
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

	// optional close-up lineups
	if (const char* dir = std::getenv("GB_VEGETATION_PREVIEW"))
	{
		constexpr uint32_t W = 1600, H = 700;
		GB::Renderer renderer(W, H, 4, 2048);
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
			std::vector<std::unique_ptr<GB::Tree>> keep;
			for (size_t i = 0; i < list.size(); ++i)
			{
				keep.push_back(factory.Create(list[i]));
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

		GB::Scene sc;
		sc.Objects.push_back(floorObject({ 0.3f, 0.38f, 0.2f }));
		std::vector<std::unique_ptr<GB::Rock>> rocks;
		for (uint32_t s = 1; s <= 8; ++s)
		{
			GB::RockParams rp;
			rp.Seed = s * 7u; rp.Size = 1.0f; rp.Stretch = { 1.0f + 0.1f * static_cast<float>(s % 3), 0.55f + 0.12f * static_cast<float>(s % 4), 1.0f };
			rp.Moss = s % 3 == 0 ? 0.7f : 0.0f; rp.Sand = s % 3 == 1 ? 0.7f : 0.0f;
			rp.Color = s % 3 == 1 ? glm::vec3(0.55f, 0.40f, 0.28f) : glm::vec3(0.42f, 0.40f, 0.37f);
			rocks.push_back(GB::RockFactory::Create(rp));
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
