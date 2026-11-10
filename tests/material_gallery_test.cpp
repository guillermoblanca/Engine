// T91: the material gallery. The families of tests/data/material_gallery.json (ice, glass, sand, wood, rock, brick; T103: mud, vegetation,
// leaves), two to seven variants each, as a box and a ball (or, by the family's "shape": a slab of ground, tufts of grass cards, a bush of
// cards, standing leaf cards) on a neutral floor under a midday sun with shadows and the default Neutral tone mapper. The materials are read with the
// game's loader (LoadVisuals) and turned into renderer materials by the game's VisualCache::MaterialFor; the test adds the procedural normal and
// roughness maps (ProcTexture::GenerateNormal / GenerateRoughness), which the game's path does not bind yet, and the opacity of the gallery
// section for glass and ice (the blend surface of today, until T34 brings transmission).
// The small picture is a golden image (tests/golden/images/material_gallery.png; update: GB_UPDATE_GOLDEN=1 ctest -R "^material_gallery$").
// `GBGLTests material_gallery --out <dir>` also writes the full-size gallery (galeria.png, 1600x900) and a close-up of every family
// (galeria_<family>.png) there, and the new textures of T102 and T103 flat (texturas_v2.png, texturas_v3.png; no GL needed). It prints the mean albedo of every material (sRGB luminance of colour x texture) for the art bible's R2.
// T106: tufts, bushes and leaf samples are built of curved cards (ProcMesh::CurvedCard, CardTuft, CardShrub, CardConifer; a leaf sample is
// the bare curved card and the plant of its "plant" entry beside it), checked on the CPU first (normals, tangents, determinism); `--out`
// also writes the cards' micro-relief flat and lit (texturas_v4.png) and a close-up of the leaf cards under a low sun (hojas_detalle.png).
// T107: the tufts of a grass patch are instances (six CardTuft variants a material, placed by dart throwing, each turned, sized, stretched
// and tilted), the conifer has its trunk (ProcMesh::CardConiferTrunk, bark2), it prints the plants' triangles, and `--out` also writes
// the leaves family and a strip of grass against a low sun (hojas_contraluz.png).
// T109: the cortezas family (a stump with its cut face, wood_end, and a short log beside it, the bark in metres round every ring) and,
// only with `--out` (not in the golden image), the trees of the "trees" section: TreeFactory's (GB/World/Trees.h) dressed with the
// gallery's barks and leaves. `--out` writes galeria_arboles.png, arboles_contraluz.png, arboles_corteza.png and arboles_madera.png.
// T110: TreeFactory maps the bark itself (u and v in metres, tangents from the UVs, one strip a branch, the cards' stem at v = 0), so the
// meshes go up as they come; the test checks them on the CPU (CheckBark: tangents, outward normals, stretch, open edges only where a
// branch is sunk in its parent or at the trunk's foot; the cards' tangents; determinism) and `--out` also writes the fork close up
// (arboles_union.png; the file name from GB_TREE_UNION_NAME if set, for a before/after pair).
// T113: the stages and states of a tree ("treeGrowth"): checked on the CPU (determinism, heights, CheckBark, budgets, closed pieces, the
// adult the default tree) and, with `--out`, drawn as arboles_fases.png (one band a habit, real scale) and arboles_estados.png.
// SKIPs (77) without an OpenGL 4.5 context.
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Qa/Qa.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/TreesGpu.h"
#include "GB/Visual/ProcMesh.h"
#include "GB/Visual/ProcTexture.h"
#include "GB/Visual/SceneVisuals.h"
#include "GB/Visual/VisualDefs.h"
#include "GB/World/Trees.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	struct Family
	{
		std::string Name;
		std::vector<std::string> Materials;
		std::string Shape = "box";   // "box" (a box and a ball), "slab" (a slab of ground and a ball), "tufts", "shrub", "cards" (T103)
		int Band = 0;                // the overview's band: 0 in front, 1 behind
	};

	float ToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
	float ToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

	// The mean albedo of a material as the art bible measures it (R2): colour x texture, averaged in linear light, as an sRGB luminance 0..255.
	// Weighted by the texture's alpha (T103 cards: only the leaf or the blade counts; the other textures are opaque, so nothing changes).
	float MeanAlbedo(const GB::VisualMaterial& m)
	{
		glm::vec3 sum(0.0f);
		float n = 1.0f;
		if (m.Texture.empty()) sum = m.Color;
		else
		{
			const GB::Image img = GB::ProcTexture::Generate(m.Texture, 128);
			n = 0.0f;
			for (size_t i = 0; i < static_cast<size_t>(img.Width) * img.Height; ++i)
			{
				const float a = static_cast<float>(img.Pixels[i * 4 + 3]) / 255.0f;
				n += a;
				for (int c = 0; c < 3; ++c)
					sum[c] += a * ToLinear(static_cast<float>(img.Pixels[i * 4 + static_cast<size_t>(c)]) / 255.0f) * m.Color[c];
			}
			n = std::max(n, 1e-6f);
		}
		const glm::vec3 mean = sum / n;
		return ToSrgb(0.2126f * mean.r + 0.7152f * mean.g + 0.0722f * mean.b) * 255.0f;
	}

	// T102: the new textures flat, without the shader, so they can be judged on their own: one row per kind, the columns albedo, normal,
	// roughness (G as grey), interior over a dark cyan, interior alpha as grey, and 2x2 repeats of the albedo and of the normal at half scale
	// (the tiling check). Every tile is drawn with v up (row 0 of the PNG is the top of the tile, v = 1), as on a ProcMesh wall: drops fall
	// down the picture.
	bool WriteTextureSheet(const std::string& path)
	{
		const std::vector<std::string> kinds = { "ice_faceted", "glass_wet", "ice" };
		constexpr uint32_t T = 256, Gap = 6, Cols = 7;
		const uint32_t W = Cols * T + (Cols + 1) * Gap, H = static_cast<uint32_t>(kinds.size()) * (T + Gap) + Gap;
		std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 40);
		for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
		for (size_t row = 0; row < kinds.size(); ++row)
		{
			const std::string& k = kinds[row];
			const GB::Image albedo = GB::ProcTexture::Generate(k, T), normal = GB::ProcTexture::GenerateNormal(k, T);
			const GB::Image rough = GB::ProcTexture::GenerateRoughness(k, T), interior = GB::ProcTexture::GenerateInterior(k, T);
			const uint32_t oy = Gap + static_cast<uint32_t>(row) * (T + Gap);
			for (uint32_t col = 0; col < Cols; ++col)
			{
				const uint32_t ox = Gap + col * (T + Gap);
				for (uint32_t y = 0; y < T; ++y)
					for (uint32_t x = 0; x < T; ++x)
					{
						const bool repeat = col >= 5;
						const uint32_t sx = repeat ? (x * 2) % T : x, sy = T - 1 - (repeat ? (y * 2) % T : y);
						const size_t s = (static_cast<size_t>(sy) * T + sx) * 4;
						uint8_t* d = &px[(static_cast<size_t>(oy + y) * W + ox + x) * 4];
						const GB::Image& src = col == 0 || col == 5 ? albedo : col == 1 || col == 6 ? normal : col == 2 ? rough : interior;
						if (col == 2) d[0] = d[1] = d[2] = src.Pixels[s + 1];
						else if (col == 3)
						{
							static const float bg[3] = { 10.0f, 60.0f, 80.0f };
							const float a = static_cast<float>(src.Pixels[s + 3]) / 255.0f;
							for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>(std::lround(bg[c] + (static_cast<float>(src.Pixels[s + static_cast<size_t>(c)]) - bg[c]) * a));
						}
						else if (col == 4) d[0] = d[1] = d[2] = src.Pixels[s + 3];
						else for (int c = 0; c < 3; ++c) d[c] = src.Pixels[s + static_cast<size_t>(c)];
					}
			}
		}
		return GB::SavePNG(path.c_str(), W, H, px.data());
	}

	// T103: the new textures flat. Two panels side by side: the surfaces that tile (mud, wood) on the left, the cards (leaves, grass) on the
	// right. Columns: the albedo over a light and dark checker (through its alpha), the alpha as grey, the normal, the roughness (G as grey),
	// and a 2x2 repeat of the albedo at half scale (the tiling check; a card shows its bleed colour around it). v up, as on a ProcMesh wall.
	bool WriteTextureSheetV3(const std::string& path)
	{
		const std::vector<std::string> left = { "mud", "mud_dry", "wood_planks2", "wood_log2", "bark2", "wood_old" };
		const std::vector<std::string> right = { "leaf_broad", "leaf_small", "leaf_needle", "grass_short", "grass_medium", "grass_tall", "grass_dry" };
		constexpr uint32_t T = 256, Gap = 6, Cols = 5, PanelGap = 30;
		const uint32_t panelW = Cols * T + (Cols + 1) * Gap;
		const uint32_t W = 2 * panelW + PanelGap, H = static_cast<uint32_t>(std::max(left.size(), right.size())) * (T + Gap) + Gap;
		std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 40);
		for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
		for (int panel = 0; panel < 2; ++panel)
		{
			const std::vector<std::string>& kinds = panel == 0 ? left : right;
			for (size_t row = 0; row < kinds.size(); ++row)
			{
				const std::string& k = kinds[row];
				const GB::Image albedo = GB::ProcTexture::Generate(k, T), normal = GB::ProcTexture::GenerateNormal(k, T), rough = GB::ProcTexture::GenerateRoughness(k, T);
				const uint32_t oy = Gap + static_cast<uint32_t>(row) * (T + Gap);
				for (uint32_t col = 0; col < Cols; ++col)
				{
					const uint32_t ox = static_cast<uint32_t>(panel) * (panelW + PanelGap) + Gap + col * (T + Gap);
					for (uint32_t y = 0; y < T; ++y)
						for (uint32_t x = 0; x < T; ++x)
						{
							const bool repeat = col == 4;
							const uint32_t sx = repeat ? (x * 2) % T : x, sy = T - 1 - (repeat ? (y * 2) % T : y);
							const size_t s = (static_cast<size_t>(sy) * T + sx) * 4;
							uint8_t* d = &px[(static_cast<size_t>(oy + y) * W + ox + x) * 4];
							if (col == 0)
							{
								const float bg = ((x / 16 + y / 16) & 1) ? 200.0f : 120.0f;
								const float a = static_cast<float>(albedo.Pixels[s + 3]) / 255.0f;
								for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>(std::lround(bg + (static_cast<float>(albedo.Pixels[s + static_cast<size_t>(c)]) - bg) * a));
							}
							else if (col == 1) d[0] = d[1] = d[2] = albedo.Pixels[s + 3];
							else if (col == 2) for (int c = 0; c < 3; ++c) d[c] = normal.Pixels[s + static_cast<size_t>(c)];
							else if (col == 3) d[0] = d[1] = d[2] = rough.Pixels[s + 1];
							else for (int c = 0; c < 3; ++c) d[c] = albedo.Pixels[s + static_cast<size_t>(c)];
						}
				}
			}
		}
		return GB::SavePNG(path.c_str(), W, H, px.data());
	}

	// T106: the cards flat with their micro-relief. One row per card; columns: the albedo over a light and dark checker (through its alpha),
	// the alpha as grey, the normal, the roughness (G as grey), the albedo lit by its normal map under a grazing light from the upper left
	// (tangent space; what the relief does without the renderer), and the middle third of that lit tile enlarged three times. v up.
	bool WriteTextureSheetV4(const std::string& path)
	{
		const std::vector<std::string> kinds = { "leaf_broad", "leaf_small", "leaf_needle", "grass_short", "grass_medium", "grass_tall", "grass_dry" };
		constexpr uint32_t T = 256, Gap = 6, Cols = 6;
		const uint32_t W = Cols * T + (Cols + 1) * Gap, H = static_cast<uint32_t>(kinds.size()) * (T + Gap) + Gap;
		std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 40);
		for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
		const glm::vec3 light = glm::normalize(glm::vec3(-0.75f, 0.45f, 0.35f));
		for (size_t row = 0; row < kinds.size(); ++row)
		{
			const std::string& k = kinds[row];
			const GB::Image albedo = GB::ProcTexture::Generate(k, T), normal = GB::ProcTexture::GenerateNormal(k, T), rough = GB::ProcTexture::GenerateRoughness(k, T);
			const uint32_t oy = Gap + static_cast<uint32_t>(row) * (T + Gap);
			for (uint32_t col = 0; col < Cols; ++col)
			{
				const uint32_t ox = Gap + col * (T + Gap);
				for (uint32_t y = 0; y < T; ++y)
					for (uint32_t x = 0; x < T; ++x)
					{
						const bool zoom = col == 5;
						const uint32_t sx = zoom ? T / 3 + x / 3 : x, sy = T - 1 - (zoom ? T / 3 + y / 3 : y);
						const size_t s = (static_cast<size_t>(sy) * T + sx) * 4;
						uint8_t* d = &px[(static_cast<size_t>(oy + y) * W + ox + x) * 4];
						const float bg = ((x / 16 + y / 16) & 1) ? 200.0f : 120.0f;
						const float a = static_cast<float>(albedo.Pixels[s + 3]) / 255.0f;
						if (col == 0 || col >= 4)
						{
							float shade = 1.0f;
							if (col >= 4)
							{
								const glm::vec3 n = glm::normalize(glm::vec3(normal.Pixels[s], normal.Pixels[s + 1], normal.Pixels[s + 2]) / 127.5f - 1.0f);
								shade = 0.2f + 1.3f * std::max(0.0f, glm::dot(n, light));
							}
							for (int c = 0; c < 3; ++c)
							{
								const float lit = std::min(255.0f, static_cast<float>(albedo.Pixels[s + static_cast<size_t>(c)]) * shade);
								d[c] = static_cast<uint8_t>(std::lround(bg + (lit - bg) * a));
							}
						}
						else if (col == 1) d[0] = d[1] = d[2] = albedo.Pixels[s + 3];
						else if (col == 2) for (int c = 0; c < 3; ++c) d[c] = normal.Pixels[s + static_cast<size_t>(c)];
						else d[0] = d[1] = d[2] = rough.Pixels[s + 1];
					}
			}
		}
		return GB::SavePNG(path.c_str(), W, H, px.data());
	}

	// T106: the curved cards are sound: every vertex normal on the side of its faces' normals, unit, the tangents unit and perpendicular to it,
	// and the same mesh for the same arguments.
	void CheckCurvedCards()
	{
		const std::vector<GB::MeshData> meshes = { GB::ProcMesh::CurvedCard(0.8f, 0.8f, 4, 4, -0.1f, 0.08f, 0.3f, 0.01f, 7),
			GB::ProcMesh::CurvedCard(0.5f, 1.0f, 3, 6, 0.2f, -0.05f, -0.5f, 0.02f, 9), GB::ProcMesh::CardShrub(0.6f, 0.6f, 14, 3),
			GB::ProcMesh::CardConifer(1.2f, 0.45f, 5, 4), GB::ProcMesh::CardTuft(0.5f, 0.45f, 3, 5), GB::ProcMesh::CardConiferTrunk(1.2f, 0.04f) };
		const char* names[] = { "curved card", "curved card (odd segments, bent forward)", "card shrub", "card conifer", "card tuft", "conifer trunk" };
		// T107: the triangles of the plants as the gallery builds them (the budget of the denser cores)
		std::fprintf(stderr, "triangles: card shrub (14) %zu, card conifer (7 tiers) %zu, card tuft (3) %zu, conifer trunk %zu\n",
			GB::ProcMesh::CardShrub(0.44f, 0.44f, 14, 1).Indices.size() / 3, GB::ProcMesh::CardConifer(0.96f, 0.56f, 7, 1).Indices.size() / 3,
			GB::ProcMesh::CardTuft(0.5f, 0.45f, 3, 1).Indices.size() / 3, GB::ProcMesh::CardConiferTrunk(0.96f, 0.035f).Indices.size() / 3);
		for (size_t k = 0; k < meshes.size(); ++k)
		{
			const GB::MeshData& m = meshes[k];
			bool facing = true, unit = true, tangents = true, indices = true;
			for (size_t i = 0; i + 2 < m.Indices.size(); i += 3)
			{
				for (int j = 0; j < 3; ++j) indices = indices && m.Indices[i + static_cast<size_t>(j)] < m.Vertices.size();
				if (!indices) break;
				const GB::Vertex& a = m.Vertices[m.Indices[i]], & b = m.Vertices[m.Indices[i + 1]], & c = m.Vertices[m.Indices[i + 2]];
				const glm::vec3 fn = glm::cross(b.Position - a.Position, c.Position - a.Position);
				if (glm::length(fn) < 1e-10f) continue;
				for (const GB::Vertex* v : { &a, &b, &c }) facing = facing && glm::dot(v->Normal, glm::normalize(fn)) > 0.0f;
			}
			for (const GB::Vertex& v : m.Vertices)
			{
				unit = unit && std::fabs(glm::length(v.Normal) - 1.0f) < 1e-3f;
				const glm::vec3 t(v.Tangent);
				tangents = tangents && std::fabs(glm::length(t) - 1.0f) < 1e-3f && std::fabs(glm::dot(t, v.Normal)) < 1e-3f;
			}
			Check(indices && !m.Empty(), std::string(names[k]) + ": indices in range");
			Check(facing, std::string(names[k]) + ": every vertex normal on the side of its faces");
			Check(unit && tangents, std::string(names[k]) + ": unit normals, unit tangents perpendicular to them");
		}
		Check(GB::ProcMesh::CurvedCard(0.8f, 0.8f, 4, 4, -0.1f, 0.08f, 0.3f, 0.01f, 7).Hash() == meshes[0].Hash(), "a curved card is the same every time");
		Check(GB::ProcMesh::CurvedCard(0.8f, 0.8f, 4, 4, -0.1f, 0.08f, 0.3f, 0.01f, 8).Hash() != meshes[0].Hash(), "the seed changes a curved card");
		Check(GB::ProcMesh::CardConifer(1.2f, 0.45f, 5, 4).Hash() == meshes[3].Hash(), "a card conifer is the same every time");
		// the fold lifts the side edges over the midrib toward +Z, and the base row stays on the ground
		const GB::MeshData flatFold = GB::ProcMesh::CurvedCard(1.0f, 1.0f, 4, 2, 0.0f, 0.1f, 0.0f, 0.0f, 0);
		Check(std::fabs(flatFold.Vertices[0].Position.z - 0.1f) < 1e-5f && std::fabs(flatFold.Vertices[2].Position.z) < 1e-5f, "the fold lifts the edges 0.1 over the midrib");
		bool base = true;
		for (const GB::Vertex& v : meshes[0].Vertices) if (v.UV.y == 0.0f) base = base && std::fabs(v.Position.y) < 1e-6f;
		Check(base, "a curved card's base row stays at y = 0");
	}

	// ---- T110: TreeFactory's meshes as the generator makes them (no remap in the test any more): what the gallery checks on the CPU
	// The generalized winding number of a point (Jacobson et al.): the solid angles of the triangles over 4 pi. About 1 inside one closed
	// tube, 0 outside, 0.5 on the mouth of an open one; it adds up where tubes overlap, so it says whether a point is inside the wood.
	float Winding(const GB::MeshData& m, const glm::vec3& p)
	{
		double sum = 0.0;
		for (size_t i = 0; i + 2 < m.Indices.size(); i += 3)
		{
			const glm::vec3 a = m.Vertices[m.Indices[i]].Position - p, b = m.Vertices[m.Indices[i + 1]].Position - p, c = m.Vertices[m.Indices[i + 2]].Position - p;
			const float la = glm::length(a), lb = glm::length(b), lc = glm::length(c);
			const float num = glm::dot(a, glm::cross(b, c));
			const float den = la * lb * lc + glm::dot(a, b) * lc + glm::dot(b, c) * la + glm::dot(c, a) * lb;
			sum += 2.0 * std::atan2(static_cast<double>(num), static_cast<double>(den));
		}
		return static_cast<float>(sum / (4.0 * 3.14159265358979));
	}

	struct BarkReport
	{
		size_t Triangles = 0, BadTangents = 0, BadNormals = 0, Inward = 0, Sampled = 0, OpenLoops = 0, GroundLoops = 0, ExposedLoops = 0, DanglingEdges = 0;
		size_t Measured = 0, Stretched = 0;   // triangles of branches over 2 cm radius, and those with u/v stretch out of 0.7..1.4
		float MinStretch = 1e9f, MaxStretch = 0.0f;
	};

	// The bark of a tree (`m`, as TreeFactory makes it):
	// - tangents: unit, perpendicular to the normal, along +dP/du of every triangle round it, the bitangent (cross(N, T) w) along +dP/dv;
	// - normals: unit, on the side of every face round them, and out of the wood: the winding number just outside a vertex (along its
	//   normal) is lower than just inside (every third vertex);
	// - stretch: |dP/du| / |dP/dv| per triangle, on branches over 2 cm of radius (the radius from a ring edge: its u span over the turn of
	//   its normals); the collars and the closed tips are left out (their u lines converge by design);
	// - open edges (the vertices welded by position): they close into loops, and every loop is the trunk's foot on the ground or the mouth
	//   of a branch sunk in its parent (its centre and its corners, pulled 15 % toward the centre, inside the wood: winding > 0.9).
	BarkReport CheckBark(const GB::MeshData& m)
	{
		BarkReport r;
		r.Triangles = m.Triangles();
		std::vector<glm::vec3> du(m.Vertices.size(), glm::vec3(0.0f)), dv(m.Vertices.size(), glm::vec3(0.0f));
		for (size_t i = 0; i + 2 < m.Indices.size(); i += 3)
		{
			const GB::Vertex& a = m.Vertices[m.Indices[i]], & b = m.Vertices[m.Indices[i + 1]], & c = m.Vertices[m.Indices[i + 2]];
			const glm::vec3 e1 = b.Position - a.Position, e2 = c.Position - a.Position;
			const glm::vec2 d1 = b.UV - a.UV, d2 = c.UV - a.UV;
			const float det = d1.x * d2.y - d2.x * d1.y;
			const glm::vec3 fn = glm::cross(e1, e2);
			if (std::fabs(det) < 1e-12f || glm::length(fn) < 1e-12f) continue;
			const glm::vec3 pu = (e1 * d2.y - e2 * d1.y) / det, pv = (e2 * d1.x - e1 * d2.x) / det;
			for (int k = 0; k < 3; ++k)
			{
				const uint32_t id = m.Indices[i + static_cast<size_t>(k)];
				du[id] += pu; dv[id] += pv;
				if (glm::dot(m.Vertices[id].Normal, fn) <= 0.0f) ++r.BadNormals;
			}
			// stretch, on a triangle with a ring edge (two corners at the same v) and no corner on a closed tip (the tip has u = its offset
			// at radius 0) nor a steep radius change (a collar)
			const GB::Vertex* q[3] = { &a, &b, &c };
			for (int k = 0; k < 3; ++k)
			{
				const GB::Vertex& p0 = *q[k], & p1 = *q[(k + 1) % 3];
				if (std::fabs(p0.UV.y - p1.UV.y) > 1e-6f) continue;
				const float turn = std::acos(glm::clamp(glm::dot(p0.Normal, p1.Normal), -1.0f, 1.0f));
				if (turn < 1e-3f) break;
				const float radius = std::fabs(p0.UV.x - p1.UV.x) / turn;
				const float lu = glm::length(pu), lv = glm::length(pv);
				if (radius < 0.02f || lv < 1e-6f) break;
				// a collar or a tip: a corner's normal leans along the branch by more than ~15 degrees
				const glm::vec3 along = glm::normalize(pv);
				if (std::fabs(glm::dot(a.Normal, along)) > 0.25f || std::fabs(glm::dot(b.Normal, along)) > 0.25f || std::fabs(glm::dot(c.Normal, along)) > 0.25f) break;
				const float s = lu / lv;
				++r.Measured;
				if (s < 0.7f || s > 1.4f) ++r.Stretched;
				r.MinStretch = std::min(r.MinStretch, s);
				r.MaxStretch = std::max(r.MaxStretch, s);
				break;
			}
		}
		for (size_t i = 0; i < m.Vertices.size(); ++i)
		{
			const GB::Vertex& v = m.Vertices[i];
			const glm::vec3 t(v.Tangent);
			bool ok = std::fabs(glm::length(t) - 1.0f) < 1e-3f && std::fabs(glm::dot(t, v.Normal)) < 1e-3f && std::fabs(glm::length(v.Normal) - 1.0f) < 1e-3f;
			if (glm::length(du[i]) > 1e-6f) ok = ok && glm::dot(t, du[i]) > 0.0f;
			// T111: the apex of a closed tip has its normal along the branch, parallel to dP/dv: its bitangent's sign is undefined there (the
			// dot is ~0 and rounding decides it), so a dot within 1e-3 of |dP/dv| does not count against it
			if (glm::length(dv[i]) > 1e-6f) ok = ok && glm::dot(glm::cross(v.Normal, t) * v.Tangent.w, dv[i]) > -1e-3f * glm::length(dv[i]);
			if (!ok) ++r.BadTangents;
		}
		// open edges between welded vertices
		std::map<std::tuple<int, int, int>, uint32_t> weld;
		std::vector<uint32_t> id(m.Vertices.size());
		std::vector<glm::vec3> at;
		for (size_t i = 0; i < m.Vertices.size(); ++i)
		{
			const glm::vec3 p = m.Vertices[i].Position * 1e5f;
			const auto key = std::make_tuple(static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)), static_cast<int>(std::lround(p.z)));
			const auto it = weld.emplace(key, static_cast<uint32_t>(at.size()));
			if (it.second) at.push_back(m.Vertices[i].Position);
			id[i] = it.first->second;
		}
		std::map<std::pair<uint32_t, uint32_t>, int> edges;   // directed edge -> uses
		for (size_t i = 0; i + 2 < m.Indices.size(); i += 3)
			for (int k = 0; k < 3; ++k)
			{
				const uint32_t a = id[m.Indices[i + static_cast<size_t>(k)]], b = id[m.Indices[i + static_cast<size_t>((k + 1) % 3)]];
				if (a != b) ++edges[{ a, b }];
			}
		std::map<uint32_t, std::vector<uint32_t>> next;   // open edges: used one way and never the other
		for (const auto& [e, n] : edges)
			if (edges.find({ e.second, e.first }) == edges.end()) next[e.first].push_back(e.second);
		std::map<uint32_t, bool> seen;
		for (const auto& [start, outs] : next)
		{
			if (seen[start]) continue;
			std::vector<uint32_t> loop;
			uint32_t cur = start;
			bool closed = false;
			while (!seen[cur])
			{
				seen[cur] = true;
				loop.push_back(cur);
				const auto it = next.find(cur);
				if (it == next.end() || it->second.size() != 1) break;
				cur = it->second[0];
				if (cur == start) { closed = true; break; }
			}
			if (!closed) { r.DanglingEdges += loop.size(); continue; }
			++r.OpenLoops;
			glm::vec3 centre(0.0f);
			for (uint32_t v : loop) centre += at[v];
			centre /= static_cast<float>(loop.size());
			if (std::fabs(centre.y) < 1e-3f) { ++r.GroundLoops; continue; }
			bool inside = Winding(m, centre) > 0.9f;
			for (uint32_t v : loop) inside = inside && Winding(m, glm::mix(at[v], centre, 0.15f)) > 0.9f;
			if (!inside) ++r.ExposedLoops;
		}
		// out of the wood: every third vertex, unless it is on an open loop (a mouth), a closed tip (its faces lean > 60 degrees off its
		// normal) or buried in other wood (just outside it is still inside: hidden, its side does not show)
		std::vector<bool> rim(at.size(), false), tipV(m.Vertices.size(), false);
		for (const auto& [from, outs] : next) { rim[from] = true; for (uint32_t to : outs) rim[to] = true; }
		for (size_t i = 0; i + 2 < m.Indices.size(); i += 3)
		{
			const glm::vec3 fn = glm::cross(m.Vertices[m.Indices[i + 1]].Position - m.Vertices[m.Indices[i]].Position, m.Vertices[m.Indices[i + 2]].Position - m.Vertices[m.Indices[i]].Position);
			if (glm::length(fn) < 1e-12f) continue;
			for (int k = 0; k < 3; ++k)
				if (glm::dot(m.Vertices[m.Indices[i + static_cast<size_t>(k)]].Normal, glm::normalize(fn)) < 0.5f) tipV[m.Indices[i + static_cast<size_t>(k)]] = true;
		}
		for (size_t i = 0; i < m.Vertices.size(); i += 3)
		{
			const GB::Vertex& v = m.Vertices[i];
			if (rim[id[i]] || tipV[i]) continue;
			const float delta = 0.002f;
			const float wo = Winding(m, v.Position + v.Normal * delta);
			if (wo > 0.5f) continue;
			++r.Sampled;
			const float wi = Winding(m, v.Position - v.Normal * delta);
			if (wi - wo < 0.5f) ++r.Inward;
		}
		return r;
	}

	// A (tapered, bent) log or branch standing on the origin along +Y, open at both ends: ProcMesh::Cylinder with its u remapped to metres
	// round every ring (Cylinder puts a whole number of tiles round the girth, at least one, which squeezes the bark round a thin branch).
	GB::MeshData BarkLog(float bottom, float top, float length, int sides, float bend)
	{
		GB::MeshData m = GB::ProcMesh::Cylinder(bottom, top, length, sides, false, bend);
		sides = std::clamp(sides, 3, 48);
		const size_t perRing = static_cast<size_t>(sides) + 1, rings = m.Vertices.size() / perRing;
		for (size_t r = 0; r < rings; ++r)
		{
			const float t = rings > 1 ? static_cast<float>(r) / static_cast<float>(rings - 1) : 0.0f, radius = bottom + (top - bottom) * t;
			for (size_t s = 0; s < perRing; ++s)
				m.Vertices[r * perRing + s].UV.x = (6.2831853f * static_cast<float>(s) / static_cast<float>(sides) - 3.14159265f) * radius;   // in metres round every ring, as TreeFactory (T110)
		}
		m.ComputeTangents();
		return m;
	}

	// The cut face of a log of radius `radius` at height `y`, facing up (or down), the wood_end tile's log fitted to it (0.5 +- 0.49),
	// turned by `turn` radians so two faces do not show the same checks.
	GB::MeshData CutFace(float radius, int sides, float y, bool up, float turn)
	{
		GB::MeshData m;
		const glm::vec3 n(0.0f, up ? 1.0f : -1.0f, 0.0f);
		m.Vertices.push_back({ glm::vec3(0.0f, y, 0.0f), n, { 0.5f, 0.5f }, glm::vec4(1, 0, 0, 1) });
		for (int s = 0; s <= sides; ++s)
		{
			const float a = 6.2831853f * static_cast<float>(s) / static_cast<float>(sides);
			const glm::vec2 c(std::cos(a), std::sin(a));
			m.Vertices.push_back({ glm::vec3(c.x * radius, y, c.y * radius), n, glm::vec2(0.5f) + 0.49f * glm::vec2(std::cos(a + turn), std::sin(a + turn)), glm::vec4(1, 0, 0, 1) });
		}
		for (int s = 0; s < sides; ++s)
		{
			const uint32_t a = 1u + static_cast<uint32_t>(s), b = a + 1u;
			if (up) m.Indices.insert(m.Indices.end(), { 0u, b, a }); else m.Indices.insert(m.Indices.end(), { 0u, a, b });
		}
		m.ComputeTangents();
		return m;
	}

	// The radius of a sphere around the origin of `m` holding all of it (for the renderer's culling and shadow fitting).
	float RadiusOf(const GB::MeshData& m)
	{
		float r = 0.0f;
		for (const GB::Vertex& v : m.Vertices) r = std::max(r, glm::length(v.Position));
		return r + 0.05f;
	}

	// The gallery's ground (T103): two bands of columns (a family's "band", 0 in front, 1 behind it), the samples of a family in rows along -Z,
	// four rows a column (a family with more takes more columns).
	constexpr float kColumn = 2.6f, kRow = 2.4f;
	constexpr size_t kRowsPerColumn = 4;
	std::vector<std::vector<glm::vec3>> Layout(const std::vector<Family>& families)
	{
		std::vector<std::vector<glm::vec3>> at(families.size());
		float bandZ = 0.0f;
		for (int band = 0; band < 2; ++band)
		{
			size_t columns = 0, rows = 0;
			for (const Family& f : families)
				if (f.Band == band)
				{
					columns += (f.Materials.size() + kRowsPerColumn - 1) / kRowsPerColumn;
					rows = std::max(rows, std::min(f.Materials.size(), kRowsPerColumn));
				}
			size_t column = 0;
			for (size_t i = 0; i < families.size(); ++i)
			{
				if (families[i].Band != band) continue;
				for (size_t m = 0; m < families[i].Materials.size(); ++m)
				{
					const float x = (static_cast<float>(column + m / kRowsPerColumn) - 0.5f * static_cast<float>(columns - 1)) * kColumn;
					at[i].push_back({ x, 0.0f, bandZ - static_cast<float>(m % kRowsPerColumn) * kRow });
				}
				column += (families[i].Materials.size() + kRowsPerColumn - 1) / kRowsPerColumn;
			}
			bandZ -= static_cast<float>(rows) * kRow + 0.6f;
		}
		return at;
	}
}

int GBTest_material_gallery(int argc, char** argv)
{
	GB::Log::Init();
	std::string outDir;
	for (int i = 1; i + 1 < argc; ++i)
		if (std::strcmp(argv[i], "--out") == 0) outDir = argv[i + 1];

	// ---- the data: the materials through the game's loader, the layout and the opacities from the gallery section
	std::ifstream file(GB_TEST_DATA_DIR "/material_gallery.json", std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	GB::VisualLibrary library;
	const std::string err = GB::LoadVisuals(text.str(), library);
	Check(err.empty(), "the gallery's materials load (" + err + ")");
	const nlohmann::json doc = nlohmann::json::parse(text.str(), nullptr, false);
	Check(!doc.is_discarded() && doc.contains("gallery"), "the gallery section is there");
	if (failures) return failures;
	std::vector<Family> families;
	size_t samples = 0;
	for (const auto& f : doc["gallery"]["families"])
	{
		families.push_back({ f.value("name", ""), f["materials"].get<std::vector<std::string>>(), f.value("shape", "box"), f.value("band", 0) });
		samples += families.back().Materials.size();
		for (const std::string& id : families.back().Materials) Check(library.Materials.count(id) == 1, "family " + families.back().Name + ": material " + id + " is defined");
	}
	Check(families.size() == 10, "ten families");
	std::map<std::string, std::string> shapes;   // per material, overriding its family's
	if (doc["gallery"].contains("shapes"))
		for (const auto& [id, value] : doc["gallery"]["shapes"].items()) shapes[id] = value.get<std::string>();
	std::map<std::string, glm::vec2> cardSize;   // the card of a tuft, a bush or a leaf sample, metres
	if (doc["gallery"].contains("cardSize"))
		for (const auto& [id, value] : doc["gallery"]["cardSize"].items()) cardSize[id] = { value[0].get<float>(), value[1].get<float>() };
	Check(library.Materials.size() == samples, "every material of the file is in the gallery (" + std::to_string(library.Materials.size()) + " materials, " + std::to_string(samples) + " samples)");
	std::map<std::string, std::string> plants;   // T106: what a "cards" sample builds beside its bare card ("shrub", "conifer")
	if (doc["gallery"].contains("plant"))
		for (const auto& [id, value] : doc["gallery"]["plant"].items()) plants[id] = value.get<std::string>();
	// T109: the cut face of the log samples, and the trees (TreeFactory's, dressed with the gallery's materials; only in the --out takes)
	const std::string cutFace = doc["gallery"].value("cutFace", "");
	Check(cutFace.empty() || library.Materials.count(cutFace) == 1, "the cut face's material " + cutFace + " is defined");
	struct TreeSample
	{
		std::string Name, Bark, Leaves;
		GB::TreeParams Params;
	};
	std::vector<TreeSample> trees;
	if (doc["gallery"].contains("trees"))
		for (const auto& t : doc["gallery"]["trees"])
		{
			TreeSample s;
			s.Name = t.value("name", "");
			s.Bark = t.value("bark", "");
			s.Leaves = t.value("leaves", "");
			const std::string species = t.value("species", "broadleaf");
			s.Params.Species = species == "conifer" ? GB::TreeSpecies::Conifer : species == "bare" ? GB::TreeSpecies::Bare : species == "palm" ? GB::TreeSpecies::Palm : GB::TreeSpecies::Broadleaf;
			s.Params.Style = t.value("style", -1);
			s.Params.Seed = t.value("seed", 1u);
			s.Params.Height = t.value("height", 8.0f);
			Check(library.Materials.count(s.Bark) == 1, "tree " + s.Name + ": bark " + s.Bark + " is defined");
			Check(s.Params.Species == GB::TreeSpecies::Bare || library.Materials.count(s.Leaves) == 1, "tree " + s.Name + ": leaves " + s.Leaves + " are defined");
			trees.push_back(s);
		}
	std::map<std::string, float> opacity;
	if (doc["gallery"].contains("opacity"))
		for (const auto& [id, value] : doc["gallery"]["opacity"].items()) opacity[id] = value.get<float>();
	for (const auto& [id, m] : library.Materials)
	{
		Check(m.Texture.empty() || GB::ProcTexture::Known(m.Texture), id + ": names a texture this build makes");
		std::fprintf(stderr, "albedo %-14s texture %-14s mean sRGB %5.1f\n", id.c_str(), m.Texture.empty() ? "-" : m.Texture.c_str(), MeanAlbedo(m));
	}
	if (failures) return failures;
	if (!outDir.empty())
	{
		const std::string sheet = outDir + "/texturas_v2.png";
		Check(WriteTextureSheet(sheet), "writes " + sheet);
		std::fprintf(stderr, "wrote %s\n", sheet.c_str());
		const std::string sheet3 = outDir + "/texturas_v3.png";
		Check(WriteTextureSheetV3(sheet3), "writes " + sheet3);
		std::fprintf(stderr, "wrote %s\n", sheet3.c_str());
		const std::string sheet4 = outDir + "/texturas_v4.png";
		Check(WriteTextureSheetV4(sheet4), "writes " + sheet4);
		std::fprintf(stderr, "wrote %s\n", sheet4.c_str());
	}
	CheckCurvedCards();
	if (failures) return failures;

	// ---- T110: the trees on the CPU, as TreeFactory makes them (bark UVs in metres, tangents from the UVs, the cards' stem at v = 0): their
	// bark checked (CheckBark), their triangles printed, the same tree every time
	struct TreeMeshes
	{
		GB::MeshData Trunk, Foliage;
		float Height = 0.0f, CrownRadius = 0.0f, BoundsHeight = 0.0f;
	};
	std::vector<TreeMeshes> treeMeshes;
	for (const TreeSample& t : trees)
	{
		const std::unique_ptr<GB::Tree> tree = GB::TreeFactory::Create(t.Params);
		TreeMeshes tm;
		tm.Trunk = tree->Trunk;
		tm.Foliage = tree->Foliage;
		tm.Height = tree->Height; tm.CrownRadius = tree->CrownRadius; tm.BoundsHeight = tree->BoundsHeight;
		const BarkReport r = CheckBark(tm.Trunk);
		std::fprintf(stderr, "tree %-22s triangles %5zu (bark %5zu, leaves %5zu, %4zu cards), crown %.2f m, top %.2f m | bark: tangents off %zu, normals off %zu,"
		             " inward %zu/%zu; open loops %zu (%zu on the ground, %zu exposed), dangling %zu; stretch u/v %.2f..%.2f (%zu/%zu out of 0.7..1.4)\n",
		             t.Name.c_str(), tree->TriangleCount, tm.Trunk.Triangles(), tm.Foliage.Triangles(), tree->LeafCardCount, tree->CrownRadius, tree->BoundsHeight,
		             r.BadTangents, r.BadNormals, r.Inward, r.Sampled, r.OpenLoops, r.GroundLoops, r.ExposedLoops, r.DanglingEdges,
		             r.MinStretch, r.MaxStretch, r.Stretched, r.Measured);
		std::fprintf(stderr, "tree %-22s hash bark %016llx leaves %016llx\n", t.Name.c_str(), static_cast<unsigned long long>(tm.Trunk.Hash()),
		             static_cast<unsigned long long>(tm.Foliage.Hash()));
		Check(r.BadTangents == 0, "tree " + t.Name + ": unit tangents perpendicular to the normals, along +dP/du, bitangents along +dP/dv");
		Check(r.BadNormals == 0, "tree " + t.Name + ": every bark normal on the side of its faces");
		Check(r.Inward == 0, "tree " + t.Name + ": every sampled bark normal points out of the wood");
		Check(r.DanglingEdges == 0 && r.ExposedLoops == 0 && r.GroundLoops <= 1, "tree " + t.Name + ": the only open edges are the trunk's foot and the mouths sunk in a parent");
		Check(r.Measured > 0 && r.Stretched == 0, "tree " + t.Name + ": the bark is stretched 0.7..1.4 (u over v) on branches over 2 cm");
		bool cards = true;
		for (const GB::Vertex& v : tm.Foliage.Vertices)
			cards = cards && std::fabs(glm::length(glm::vec3(v.Tangent)) - 1.0f) < 1e-3f && std::fabs(glm::dot(glm::vec3(v.Tangent), v.Normal)) < 1e-3f && v.UV.y >= 0.0f && v.UV.y <= 1.0f;
		Check(cards, "tree " + t.Name + ": the cards have unit tangents perpendicular to their normals and v in 0..1");
		const std::unique_ptr<GB::Tree> again = GB::TreeFactory::Create(t.Params);
		Check(again->Trunk.Hash() == tree->Trunk.Hash() && again->Foliage.Hash() == tree->Foliage.Hash(), "tree " + t.Name + ": the same tree every time");
		{
			// T111: the far level of detail (TreeParams::Detail 0) must stay clearly cheaper; its bark is checked as well
			GB::TreeParams farParams = t.Params;
			farParams.Detail = 0;
			const std::unique_ptr<GB::Tree> far = GB::TreeFactory::Create(farParams);
			const BarkReport rf = CheckBark(far->Trunk);
			std::fprintf(stderr, "tree %-22s detail 0 hash bark %016llx leaves %016llx\n", t.Name.c_str(), static_cast<unsigned long long>(far->Trunk.Hash()),
			             static_cast<unsigned long long>(far->Foliage.Hash()));
			std::fprintf(stderr, "tree %-22s detail 0: triangles %5zu (bark %5zu, leaves %5zu), crown %.2f m | bark: tangents off %zu, normals off %zu, inward %zu/%zu;"
			             " exposed loops %zu, dangling %zu, stretch u/v %.2f..%.2f (%zu/%zu out)\n", t.Name.c_str(), far->TriangleCount, far->Trunk.Triangles(),
			             far->Foliage.Triangles(), far->CrownRadius, rf.BadTangents, rf.BadNormals, rf.Inward, rf.Sampled, rf.ExposedLoops, rf.DanglingEdges,
			             rf.MinStretch, rf.MaxStretch, rf.Stretched, rf.Measured);
			Check(rf.BadTangents == 0 && rf.BadNormals == 0 && rf.Inward == 0 && rf.DanglingEdges == 0 && rf.ExposedLoops == 0 && rf.GroundLoops <= 1 && rf.Stretched == 0,
			      "tree " + t.Name + ": the bark of its far level of detail passes the same checks");
			Check(far->TriangleCount < tree->TriangleCount * 3 / 4, "tree " + t.Name + ": its far level of detail is clearly cheaper (under 3/4 of the triangles)");
		}
		treeMeshes.push_back(std::move(tm));
	}
	{
		// the game's leaf textures: their coverage (mean alpha), for the report
		const GB::TreeImages& images = GB::TreeFactory::Textures();
		const char* names[] = { "bark", "bark_palm", "leaf", "leaf_round", "leaf_maple", "needle", "frond" };
		for (size_t k = 2; k < static_cast<size_t>(GB::TreeTexture::Count); ++k)
		{
			const GB::Image& img = images.Images[k];
			double a = 0.0;
			for (size_t i = 3; i < img.Pixels.size(); i += 4) a += img.Pixels[i];
			std::fprintf(stderr, "tree texture %-10s coverage %.3f\n", names[k], a / 255.0 / static_cast<double>(img.Pixels.size() / 4));
		}
	}
	if (failures) return failures;

	// ---- T113: the growth stages (TreeParams::Stage) of every habit of treeGrowth.rows and the states of treeGrowth.states, on the CPU:
	// the same tree every time, taller stage after stage, the bark (with its cut faces: the stubs, the stump, the logs) closed and passing
	// CheckBark, the far level of detail cheaper, the triangles within their budget; the stump, the fallen trunk, the logs and the root
	// ball closed
	struct Grown
	{
		std::string Name, Bark, Leaves;
		std::shared_ptr<GB::Tree> Tree;
	};
	std::vector<std::vector<Grown>> growthRows;   // per habit, the six stages
	std::vector<Grown> treeStates;                // the tree whole, taken 0.5 and 1, stump, fallen trunk, bucked, planted, uprooted, dead standing
	const char* stageNames[6] = { "brote", "planton", "joven", "adulto", "maduro", "viejo" };
	auto parseTree = [&](const nlohmann::json& t)
	{
		TreeSample s;
		s.Name = t.value("name", "");
		s.Bark = t.value("bark", "");
		s.Leaves = t.value("leaves", "");
		const std::string species = t.value("species", "broadleaf");
		s.Params.Species = species == "conifer" ? GB::TreeSpecies::Conifer : species == "bare" ? GB::TreeSpecies::Bare : GB::TreeSpecies::Broadleaf;
		s.Params.Style = t.value("style", -1);
		s.Params.Seed = t.value("seed", 1u);
		s.Params.Height = t.value("height", 8.0f);
		Check(library.Materials.count(s.Bark) == 1 && (s.Params.Species == GB::TreeSpecies::Bare || library.Materials.count(s.Leaves) == 1), "tree " + s.Name + ": its materials are defined");
		return s;
	};
	auto barkOf = [](const GB::Tree& t) { GB::MeshData m = t.Trunk; m.Append(t.Cuts); return m; };   // the wood closed by its cut faces
	// open edges of a mesh, its vertices welded by position (0 for a closed solid)
	auto openEdges = [](const GB::MeshData& m)
	{
		std::map<std::tuple<long, long, long>, uint32_t> weld;
		std::vector<uint32_t> id(m.Vertices.size());
		for (size_t i = 0; i < m.Vertices.size(); ++i)
		{
			const glm::vec3 q = m.Vertices[i].Position * 1e5f;
			id[i] = weld.emplace(std::make_tuple(std::lround(q.x), std::lround(q.y), std::lround(q.z)), static_cast<uint32_t>(weld.size())).first->second;
		}
		std::map<std::pair<uint32_t, uint32_t>, int> edges;
		for (size_t i = 0; i + 2 < m.Indices.size(); i += 3)
			for (int k = 0; k < 3; ++k)
			{
				const uint32_t a = id[m.Indices[i + static_cast<size_t>(k)]], b = id[m.Indices[i + static_cast<size_t>((k + 1) % 3)]];
				if (a != b) ++edges[{ a, b }];
			}
		size_t open = 0;
		for (const auto& [e, n] : edges)
			if (edges.find({ e.second, e.first }) == edges.end()) ++open;
		return open;
	};
	// the bark checks of T110/T111 on a tree or piece; `ground` open loops allowed on the ground (the foot of a standing trunk or stump)
	// (the bark alone for its normals, tangents and stretch: the cut faces meet it at a sharp edge, where "out of the wood" means nothing;
	// the bark with its cut faces for the open edges)
	auto checkWood = [&](const std::string& name, const GB::Tree& t, size_t ground, bool small)
	{
		const BarkReport r = CheckBark(t.Trunk), closed = CheckBark(barkOf(t)), faces = CheckBark(t.Cuts);
		std::fprintf(stderr, "tree %-30s triangles %5zu (bark %5zu, cuts %4zu, soil %4zu, leaves %5zu, %4zu cards), height %.2f m, top %.2f, crown %.2f |"
		             " tangents off %zu, normals off %zu, inward %zu/%zu, stretch %.2f..%.2f (%zu/%zu out) | with the cut faces: open loops %zu (%zu on the ground,"
		             " %zu exposed), dangling %zu; cut faces: tangents off %zu, normals off %zu\n",
		             name.c_str(), t.TriangleCount, t.Trunk.Triangles(), t.Cuts.Triangles(), t.Soil.Triangles(), t.Foliage.Triangles(), t.LeafCardCount, t.Height,
		             t.BoundsHeight, t.CrownRadius, r.BadTangents, r.BadNormals, r.Inward, r.Sampled, r.MinStretch, r.MaxStretch, r.Stretched, r.Measured,
		             closed.OpenLoops, closed.GroundLoops, closed.ExposedLoops, closed.DanglingEdges, faces.BadTangents, faces.BadNormals);
		Check(r.BadTangents == 0 && r.BadNormals == 0 && faces.BadTangents == 0 && faces.BadNormals == 0, "tree " + name + ": tangents and normals of the bark and its cut faces");
		Check(r.Inward == 0, "tree " + name + ": every sampled bark normal points out of the wood");
		Check(closed.DanglingEdges == 0 && closed.ExposedLoops == 0 && closed.GroundLoops <= ground, "tree " + name + ": the wood is closed (open only at the foot and the mouths sunk in a parent)");
		// (a sprout's stem and a sapling's trunk are under 2 cm: nothing to measure the stretch on)
		Check(r.Stretched == 0 && (r.Measured > 0 || small), "tree " + name + ": the bark is stretched 0.7..1.4 (u over v) on branches over 2 cm");
	};
	if (doc["gallery"].contains("treeGrowth"))
	{
		const nlohmann::json& tg = doc["gallery"]["treeGrowth"];
		for (const auto& row : tg["rows"])
		{
			const TreeSample s = parseTree(row);
			std::vector<Grown> stages;
			float prevHeight = 0.0f;
			for (int st = 0; st < 6; ++st)
			{
				GB::TreeParams tp = s.Params;
				tp.Stage = static_cast<GB::TreeStage>(st);
				const std::string name = s.Name + "_" + stageNames[st];
				std::shared_ptr<GB::Tree> t = GB::TreeFactory::Create(tp);
				const std::unique_ptr<GB::Tree> again = GB::TreeFactory::Create(tp);
				Check(again->Trunk.Hash() == t->Trunk.Hash() && again->Foliage.Hash() == t->Foliage.Hash() && again->Cuts.Hash() == t->Cuts.Hash(), "tree " + name + ": the same tree every time");
				Check(t->Height > prevHeight && std::fabs(t->Height - GB::TreeFactory::StageHeight(tp)) < 1e-4f, "tree " + name + ": taller than the stage before it, as tall as its stage");
				Check(t->BoundsHeight < t->Height * 1.12f, "tree " + name + ": its top near its height");
				prevHeight = t->Height;
				checkWood(name, *t, 1, st <= 1);
				GB::TreeParams farParams = tp;
				farParams.Detail = 0;
				const std::unique_ptr<GB::Tree> far = GB::TreeFactory::Create(farParams);
				checkWood(name + "_lejos", *far, 1, st <= 1);
				Check(far->TriangleCount < t->TriangleCount, "tree " + name + ": its far level of detail is cheaper");
				if (st == 3)
				{
					Check(far->TriangleCount < t->TriangleCount * 3 / 4, "tree " + name + ": its far level of detail is clearly cheaper (under 3/4 of the triangles)");
					// the adult of the stages is the tree as it was before T113
					GB::TreeParams before = s.Params;
					const std::unique_ptr<GB::Tree> plain = GB::TreeFactory::Create(before);
					Check(plain->Trunk.Hash() == t->Trunk.Hash() && plain->Foliage.Hash() == t->Foliage.Hash() && t->Cuts.Empty() && t->Soil.Empty(), "tree " + name + ": the adult is the default tree");
				}
				stages.push_back({ name, s.Bark, s.Leaves, t });
			}
			const size_t adult = stages[3].Tree->TriangleCount;
			for (int st = 0; st < 3; ++st)
				Check(stages[static_cast<size_t>(st)].Tree->TriangleCount < adult, "tree " + stages[static_cast<size_t>(st)].Name + ": fewer triangles than the adult");
			Check(stages[4].Tree->TriangleCount * 100 <= adult * 120, "tree " + stages[4].Name + ": at most 20 % more triangles than the adult");
			Check(stages[5].Tree->TriangleCount * 100 <= adult * 135, "tree " + stages[5].Name + ": at most 35 % more triangles than the adult");
			growthRows.push_back(std::move(stages));
		}
		if (tg.contains("states"))
		{
			const TreeSample s = parseTree(tg["states"]);
			auto make = [&](const std::string& name, GB::TreeParams tp)
			{
				treeStates.push_back({ s.Name + "_" + name, s.Bark, s.Leaves, std::shared_ptr<GB::Tree>(GB::TreeFactory::Create(tp)) });
				return treeStates.back().Tree;
			};
			GB::TreeParams tp = s.Params;
			const std::shared_ptr<GB::Tree> whole = make("entero", tp);
			tp.BranchesTaken = 0.5f;
			const std::shared_ptr<GB::Tree> half = make("ramas_0.5", tp);
			tp.BranchesTaken = 1.0f;
			const std::shared_ptr<GB::Tree> all = make("ramas_1", tp);
			tp.BranchesTaken = 0.0f;
			tp.FallAzimuth = 0.0f;
			tp.Part = GB::TreePart::Stump;
			const std::shared_ptr<GB::Tree> stump = make("tocon", tp);
			tp.Part = GB::TreePart::FallenTrunk;
			const std::shared_ptr<GB::Tree> fallen = make("tronco_caido", tp);
			tp.Part = GB::TreePart::Bucked;
			const std::shared_ptr<GB::Tree> bucked = make("troceado", tp);
			tp.Part = GB::TreePart::Standing;
			tp.Stage = GB::TreeStage::Sapling;
			tp.Planted = true;
			const std::shared_ptr<GB::Tree> planted = make("planton_plantado", tp);
			tp.Planted = false;
			tp.Uprooted = true;
			const std::shared_ptr<GB::Tree> uprooted = make("planton_arrancado", tp);
			tp.Stage = GB::TreeStage::Young;
			const std::shared_ptr<GB::Tree> uprootedYoung = make("joven_arrancado", tp);
			treeStates.pop_back();   // (checked, not shown)
			for (const Grown& g : treeStates)
				checkWood(g.Name, *g.Tree, g.Tree->Params.Part == GB::TreePart::FallenTrunk || g.Tree->Params.Part == GB::TreePart::Bucked ? 0 : 1, g.Tree->Params.Stage == GB::TreeStage::Sapling);
			checkWood(s.Name + "_joven_arrancado", *uprootedYoung, 1, false);
			Check(half->Cuts.Triangles() > 0 && all->Cuts.Triangles() > half->Cuts.Triangles() && all->Foliage.Triangles() < half->Foliage.Triangles() &&
			      half->Foliage.Triangles() < whole->Foliage.Triangles(), "taking branches leaves stubs with cut faces and fewer leaves, more at 1 than at 0.5");
			Check(half->Height == whole->Height && all->Height == whole->Height, "taking branches does not rescale the tree");
			Check(stump->Foliage.Empty() && !stump->Cuts.Empty() && stump->Height > 0.25f && stump->Height < 1.0f, "the stump is a short foot with its cut face");
			Check(fallen->Foliage.Empty() && fallen->BoundsHeight < 0.4f * whole->Height && fallen->CrownRadius > 0.6f * whole->Height,
			      "the fallen trunk lies on the ground, about as long as the tree was tall");
			Check(!bucked->Cuts.Empty() && bucked->BoundsHeight < 1.5f, "the bucked tree is a low pile of logs and branches");
			Check(!planted->Soil.Empty() && !uprooted->Soil.Empty() && planted->Height < 2.1f, "the planted and the uprooted sapling have their earth");
			Check(openEdges(uprooted->Soil) == 0 && openEdges(uprootedYoung->Soil) == 0, "the root ball is closed");
		}
		if (tg.contains("deadStanding"))
		{
			const TreeSample s = parseTree(tg["deadStanding"]);
			GB::TreeParams tp = s.Params;
			tp.Stage = GB::TreeStage::Old;
			treeStates.push_back({ s.Name, s.Bark, s.Leaves, std::shared_ptr<GB::Tree>(GB::TreeFactory::Create(tp)) });
			checkWood(s.Name, *treeStates.back().Tree, 1, false);
		}
	}
	if (failures) return failures;

	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return 77;
	}

	// ---- the GPU side: the game's material, plus the normal and roughness maps of its texture
	GB::VisualCache cache(library);
	std::map<std::string, std::unique_ptr<GB::Texture2D>> maps;
	auto map = [&](const std::string& key, const GB::Image& img) -> const GB::Texture2D*
	{
		auto it = maps.find(key);
		if (it == maps.end()) it = maps.emplace(key, std::make_unique<GB::Texture2D>(img, GB::TextureEncoding::Linear)).first;
		return it->second->IsValid() ? it->second.get() : nullptr;
	};
	auto material = [&](const std::string& id)
	{
		GB::Material m = cache.MaterialFor(id);
		const GB::VisualMaterial& v = library.Materials.at(id);
		if (!v.Texture.empty())
		{
			const uint32_t size = GB::ProcTexture::Size(false);
			m.NormalMap = map(v.Texture + ".normal", GB::ProcTexture::GenerateNormal(v.Texture, size));
			m.MetallicRoughnessMap = map(v.Texture + ".rough", GB::ProcTexture::GenerateRoughness(v.Texture, size));
			// the parallax interior layer (T101): only ice has one; the other kinds would bind a transparent map, so they are skipped
			if (v.Texture == "ice" || v.Texture == "ice_faceted")
			{
				const std::string key = v.Texture + ".interior";
				auto it = maps.find(key);
				if (it == maps.end()) it = maps.emplace(key, std::make_unique<GB::Texture2D>(GB::ProcTexture::GenerateInterior(v.Texture, size), GB::TextureEncoding::SRGB)).first;
				m.InteriorMap = it->second->IsValid() ? it->second.get() : nullptr;
			}
		}
		if (const auto it = opacity.find(id); it != opacity.end()) m.Opacity = it->second;
		if (m.Alpha == GB::AlphaMode::Mask) m.AlphaCutoff = 0.4f;   // as the tree crowns (TreesGpu); the data cannot set it yet
		return m;
	};

	const GB::MeshData boxData = GB::ProcMesh::Box(glm::vec3(0.9f)), ballData = GB::ProcMesh::Blob(glm::vec3(0.45f), 4, 0.0f, 1);
	GB::Mesh box(boxData.Vertices, boxData.Indices), ball(ballData.Vertices, ballData.Indices);
	GB::Mesh floorMesh = GB::Mesh::CreateGrid(80.0f, 16);
	// T103: the ground slab of the mud and the patch under the grass; the cards of every tuft or bush are built per sample (kept here)
	const GB::MeshData slabData = GB::ProcMesh::Box(glm::vec3(2.0f, 0.06f, 1.6f));
	GB::Mesh slab(slabData.Vertices, slabData.Indices);
	std::vector<std::unique_ptr<GB::Mesh>> built;
	GB::Material soil;
	soil.Albedo = glm::vec3(0.16f, 0.12f, 0.085f); soil.Roughness = 0.95f; soil.HeightScale = 0.0f; soil.CavityStrength = 0.0f;
	// Tufts of crossed curved cards (ProcMesh::CardTuft, T106: three a tuft, arched and creased, every tuft its own seed; each turned and sized
	// by a hash) scattered over the patch; a bush of curved cards (ProcMesh::CardShrub, T106).
	auto hash01 = [](uint32_t a, uint32_t b) { return GB::HashUnit(GB::VisualHash(a, b, 0x51ED270Bu)); };
	auto keep = [&](const GB::MeshData& data) { built.push_back(std::make_unique<GB::Mesh>(data.Vertices, data.Indices)); return built.back().get(); };
	// T107: the tufts of a patch are instances, not one merged mesh: kTuftVariants CardTufts (each its own seed), every tuft one of them
	// with its own matrix, so the foliage layer's per-instance colour and its occlusion from the pivot work tuft by tuft. They are placed
	// by dart throwing (a minimum distance, no grid), each turned, sized, stretched in height and tilted a little by a hash.
	constexpr int kTuftVariants = 6;
	std::map<std::string, std::vector<const GB::Mesh*>> tuftMeshes;   // per material
	std::map<std::string, float> tuftRadius;
	auto tufts = [&](GB::Scene& sc, const std::string& id, const GB::Material& m, const glm::vec2& card, uint32_t seed, const glm::vec3& at)
	{
		std::vector<const GB::Mesh*>& variants = tuftMeshes[id];
		if (variants.empty())
			for (int v = 0; v < kTuftVariants; ++v)
			{
				const GB::MeshData data = GB::ProcMesh::CardTuft(card.x, card.y, 3, seed * 7919u + static_cast<uint32_t>(v));
				variants.push_back(keep(data));
				tuftRadius[id] = std::max(tuftRadius[id], RadiusOf(data));
			}
		const float spacing = std::max(0.2f, 0.75f * card.x), minDistance = 0.8f * spacing;
		const int target = static_cast<int>(1.9f * 1.5f / (spacing * spacing));
		std::vector<glm::vec2> placed;
		for (uint32_t k = seed * 977u, tries = 0; static_cast<int>(placed.size()) < target && tries < static_cast<uint32_t>(target) * 40u; ++k, ++tries)
		{
			const glm::vec2 p(-0.92f + 1.84f * hash01(k, 1), -0.72f + 1.44f * hash01(k, 2));
			bool free = true;
			for (const glm::vec2& q : placed) free = free && glm::length(p - q) >= minDistance;
			if (!free) continue;
			placed.push_back(p);
			const float yaw = hash01(k, 3) * 6.2831853f, size = 0.75f + 0.5f * hash01(k, 4), stretch = 0.8f + 0.45f * hash01(k, 5);
			const float tiltAxis = hash01(k, 6) * 6.2831853f, tilt = glm::radians(10.0f * hash01(k, 7));
			glm::mat4 t = glm::translate(glm::mat4(1.0f), at + glm::vec3(p.x, 0.06f, p.y));
			t = glm::rotate(t, tilt, glm::vec3(std::cos(tiltAxis), 0.0f, std::sin(tiltAxis)));
			t = glm::rotate(t, yaw, glm::vec3(0, 1, 0));
			GB::RenderObject o;
			o.MeshPtr = variants[GB::VisualHash(k, 8u) % kTuftVariants]; o.Mat = m; o.Instanced = true;
			o.Transform = glm::scale(t, glm::vec3(size, size * stretch, size));
			o.BoundingRadius = tuftRadius[id] * size * std::max(stretch, 1.0f);
			sc.Objects.push_back(o);
		}
	};
	auto shrub = [&](const glm::vec2& card, uint32_t seed) { return GB::ProcMesh::CardShrub(card.x, card.y, 14, seed); };

	// midday: a high sun, a grey sky (neutral ambient), shadows over the samples; a mid-grey floor
	auto stage = [&](const glm::vec3& centre, float radius)
	{
		GB::Scene sc;
		GB::RenderObject f;
		f.MeshPtr = &floorMesh; f.Mat.Albedo = glm::vec3(0.30f); f.Mat.Roughness = 0.9f; f.Mat.HeightScale = 0.0f; f.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(f);
		sc.Sun.Direction = { -0.35f, -1.0f, -0.45f };
		sc.Sun.Color = { 1.0f, 0.97f, 0.92f };
		sc.Env.SkyTop = { 0.42f, 0.45f, 0.5f };
		sc.Env.SkyHorizon = { 0.6f, 0.61f, 0.62f };
		sc.Env.SkyGround = { 0.2f, 0.2f, 0.2f };
		sc.Env.ShadowCenter = centre;
		sc.Env.ShadowRadius = radius;
		sc.Env.Vignette = 0.0f;
		return sc;
	};
	// one sample: a box turned a little (two faces lit differently) and a ball in front of it to the right; or the family's own shape (T103)
	auto addSample = [&](GB::Scene& sc, const std::string& id, const glm::vec3& at, const std::string& familyShape)
	{
		const GB::Material m = material(id);
		const auto so = shapes.find(id);
		const std::string shape = so != shapes.end() ? so->second : familyShape;
		const auto cs = cardSize.find(id);
		const glm::vec2 card = cs != cardSize.end() ? cs->second : glm::vec2(0.6f);
		const uint32_t seed = GB::VisualHash(id.c_str());
		if (shape == "tufts" || shape == "shrub")
		{
			GB::RenderObject patch;
			patch.MeshPtr = &slab; patch.Mat = soil; patch.Transform = glm::translate(glm::mat4(1.0f), at + glm::vec3(0.0f, 0.03f, 0.0f)); patch.BoundingRadius = 1.4f;
			sc.Objects.push_back(patch);
			if (shape == "tufts")
			{
				tufts(sc, id, m, card, seed, at);
				return;
			}
			// the bush's pivot on the patch (y 0.06), so the foliage occlusion measures from the ground it stands on
			GB::RenderObject g;
			const GB::MeshData data = shrub(card, seed);
			g.MeshPtr = keep(data); g.Mat = m;
			g.Transform = glm::translate(glm::mat4(1.0f), at + glm::vec3(0.0f, 0.06f, 0.0f)); g.BoundingRadius = RadiusOf(data);
			sc.Objects.push_back(g);
			return;
		}
		if (shape == "cards")
		{
			// T106: on the left the bare card, curved (folded along its midrib, arched back, a little twisted) and leaning back a little; on the
			// right the plant made of it (the "plant" of the gallery section: a bush or a young conifer of curved cards)
			GB::RenderObject a;
			const GB::MeshData bare = GB::ProcMesh::CurvedCard(card.x, card.y, 4, 4, -0.12f * card.y, 0.07f * card.x, 0.25f, 0.008f * card.x, seed);
			a.MeshPtr = keep(bare); a.Mat = m; a.BoundingRadius = RadiusOf(bare);
			a.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), at + glm::vec3(-0.55f, 0.0f, 0.1f)), glm::radians(-12.0f), glm::vec3(1, 0, 0));
			sc.Objects.push_back(a);
			const auto pl = plants.find(id);
			const std::string plant = pl != plants.end() ? pl->second : "shrub";
			const GB::MeshData whole = plant == "conifer" ? GB::ProcMesh::CardConifer(1.2f * card.y, 0.7f * card.x, 7, seed)
			                                              : GB::ProcMesh::CardShrub(0.55f * card.x, 0.55f * card.y, 14, seed);
			GB::RenderObject b;
			b.MeshPtr = keep(whole); b.Mat = m; b.BoundingRadius = RadiusOf(whole);
			b.Transform = glm::translate(glm::mat4(1.0f), at + glm::vec3(0.55f, 0.0f, -0.15f));
			sc.Objects.push_back(b);
			if (plant == "conifer")
			{
				// T107: the trunk, its own object in bark2 (a mesh has one material), at the conifer's pivot. Its bark is a young tree's:
				// the texture at four times its scale (the plates of bark2 are ~8 cm wide, a whole tree's)
				const GB::MeshData trunkData = GB::ProcMesh::CardConiferTrunk(1.2f * card.y, 0.035f * card.x / 0.8f);
				GB::RenderObject t;
				t.MeshPtr = keep(trunkData); t.Mat = material("bark2"); t.Mat.UVScale *= 4.0f; t.BoundingRadius = RadiusOf(trunkData);
				t.Transform = b.Transform;
				sc.Objects.push_back(t);
			}
			return;
		}
		if (shape == "log")
		{
			// T109: a stump (the bark round it, its cut face on top) and a short log lying beside it, both ends cut; the cut faces in the
			// gallery's cutFace material (wood_end), every face turned its own way
			const GB::Material face = cutFace.empty() ? m : material(cutFace);
			auto put = [&](const GB::MeshData& data, const GB::Material& mat, const glm::mat4& t)
			{
				GB::RenderObject o;
				o.MeshPtr = keep(data); o.Mat = mat; o.Transform = t; o.BoundingRadius = RadiusOf(data);
				sc.Objects.push_back(o);
			};
			const float turn = 6.2831853f * GB::HashUnit(seed);
			const glm::mat4 stump = glm::translate(glm::mat4(1.0f), at + glm::vec3(-0.5f, 0.0f, 0.1f));
			put(BarkLog(0.34f, 0.31f, 0.55f, 24, 0.0f), m, stump);
			put(CutFace(0.31f, 24, 0.55f, true, turn), face, stump);
			// the log along Z, its axis 0.17 m over the floor
			const glm::mat4 log = glm::rotate(glm::translate(glm::mat4(1.0f), at + glm::vec3(0.55f, 0.17f, -0.55f)), glm::radians(90.0f), glm::vec3(1, 0, 0));
			put(BarkLog(0.17f, 0.155f, 1.1f, 16, 0.0f), m, log);
			put(CutFace(0.17f, 16, 0.0f, false, turn + 2.0f), face, log);
			put(CutFace(0.155f, 16, 1.1f, true, turn + 4.0f), face, log);
			return;
		}
		if (shape == "slab")
		{
			GB::RenderObject g;
			g.MeshPtr = &slab; g.Mat = m; g.Transform = glm::translate(glm::mat4(1.0f), at + glm::vec3(0.0f, 0.03f, 0.0f)); g.BoundingRadius = 1.4f;
			sc.Objects.push_back(g);
			GB::RenderObject s;
			s.MeshPtr = &ball; s.Mat = m;
			s.Transform = glm::translate(glm::mat4(1.0f), at + glm::vec3(0.55f, 0.06f + 0.4f, -0.4f));
			s.BoundingRadius = 0.5f;
			sc.Objects.push_back(s);
			return;
		}
		GB::RenderObject b;
		b.MeshPtr = &box; b.Mat = m;
		b.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), at + glm::vec3(-0.55f, 0.45f, 0.0f)), glm::radians(30.0f), glm::vec3(0, 1, 0));
		b.BoundingRadius = 0.8f;
		sc.Objects.push_back(b);
		GB::RenderObject s;
		s.MeshPtr = &ball; s.Mat = m;
		s.Transform = glm::translate(glm::mat4(1.0f), at + glm::vec3(0.6f, 0.45f, 0.25f));
		s.BoundingRadius = 0.5f;
		sc.Objects.push_back(s);
	};

	const std::vector<std::vector<glm::vec3>> layout = Layout(families);
	GB::Scene scene = stage({ 0.0f, 0.0f, -6.0f }, 16.0f);
	for (size_t c = 0; c < families.size(); ++c)
		for (size_t r = 0; r < families[c].Materials.size(); ++r) addSample(scene, families[c].Materials[r], layout[c][r], families[c].Shape);
	GB::Camera overview;
	overview.Position = { 0.0f, 9.0f, 7.6f };
	overview.Target = { 0.0f, 0.0f, -5.6f };
	overview.FovYDegrees = 45.0f;

	// ---- the golden image: small, one sample (identical across drivers), like golden_images
	{
		constexpr uint32_t W = 480, H = 270;
		GB::Renderer renderer(W, H, 1, 1024);
		Check(renderer.IsValid(), "renderer initializes");
		GB::Framebuffer fb(W, H);
		renderer.Render(scene, overview, &fb);
		GB::Image img;
		img.Width = W; img.Height = H; img.Pixels = fb.ReadPixels();
		const GB::Qa::Result r = GB::Qa::CheckGoldenImage(GB::Qa::Config::FromEnvironment(), GB_GOLDEN_DIR "/images", "material_gallery", img, { 4.0, 0.02, 32 });
		std::fprintf(stderr, "%s: %s\n", r.Passed ? "ok" : "FAIL", r.Message.c_str());
		if (!r.Passed) ++failures;
	}

	// ---- T113: TreesGpu uploads the cut faces and the earth and places them (a stump: bark and cuts, lying still; a planted sapling: bark,
	// earth and crown; a default tree: bark and crown, as before)
	{
		GB::TreesGpu treesGpu;
		GB::TreeParams tp;
		tp.Seed = 7;
		tp.Style = 2;
		tp.Height = 8.5f;
		const std::unique_ptr<GB::TreeGpu> plain = treesGpu.Create(tp);
		tp.Part = GB::TreePart::Stump;
		const std::unique_ptr<GB::TreeGpu> stump = treesGpu.Create(tp);
		tp.Part = GB::TreePart::Standing;
		tp.Stage = GB::TreeStage::Sapling;
		tp.Planted = true;
		const std::unique_ptr<GB::TreeGpu> planted = treesGpu.Create(tp);
		GB::Scene a, b, c;
		plain->AddToScene(a, glm::vec3(0.0f));
		stump->AddToScene(b, glm::vec3(0.0f));
		planted->AddToScene(c, glm::vec3(0.0f));
		std::fprintf(stderr, "TreesGpu objects: default %zu, stump %zu, planted sapling %zu\n", a.Objects.size(), b.Objects.size(), c.Objects.size());
		Check(!plain->Cuts && !plain->Soil && a.Objects.size() == 2, "TreesGpu: a default tree is its bark and its crown");
		Check(stump->Cuts && !stump->Soil && b.Objects.size() == 2 && stump->TrunkMaterial.WindSway == 0.0f && stump->CutMaterial.WindSway == 0.0f,
		      "TreesGpu: a stump is its bark and its cut faces, still in the wind");
		Check(planted->Soil && !planted->Cuts && c.Objects.size() == 3, "TreesGpu: a planted sapling is its bark, its earth and its crown");
	}

	// ---- the pictures for the documentation: the whole gallery, then every family on its own with its variants side by side
	if (!outDir.empty())
	{
		constexpr uint32_t W = 1600, H = 900;
		GB::Renderer renderer(W, H, 4, 2048);
		GB::Framebuffer fb(W, H);
		auto save = [&](const GB::Scene& sc, const GB::Camera& cam, const std::string& name)
		{
			renderer.Render(sc, cam, &fb);
			const std::vector<uint8_t> px = fb.ReadPixels();
			const std::string path = outDir + "/" + name;
			Check(GB::SavePNG(path.c_str(), W, H, px.data()), "writes " + path);
			std::fprintf(stderr, "wrote %s\n", path.c_str());
		};
		save(scene, overview, "galeria.png");
		for (const Family& f : families)
		{
			const size_t n = f.Materials.size();
			GB::Scene sc = stage({ 0.0f, 0.0f, 0.0f }, 4.5f);
			bool transmissive = false;
			for (size_t i = 0; i < n; ++i)
			{
				// up to four side by side (as before T103); more in two rows, the second behind
				const size_t perRow = n <= 4 ? n : (n + 1) / 2, row = i / perRow, inRow = row == 0 ? perRow : n - perRow;
				const float spacing = f.Shape == "cards" ? 2.3f : 2.5f;   // leaf cards are small: closer together, and the camera nearer
				addSample(sc, f.Materials[i], { (static_cast<float>(i % perRow) - 0.5f * static_cast<float>(inRow - 1)) * spacing, 0.0f, -2.6f * static_cast<float>(row) }, f.Shape);
				transmissive = transmissive || material(f.Materials[i]).Transmission > 0.0f;
			}
			// glass and clear ice show what is behind them: a row of coloured posts there, or they refract a plain grey floor
			if (transmissive)
			{
				static const glm::vec3 colours[] = { { 0.8f, 0.15f, 0.12f }, { 0.85f, 0.7f, 0.15f }, { 0.15f, 0.35f, 0.8f }, { 0.9f, 0.9f, 0.88f },
				                                     { 0.2f, 0.6f, 0.25f }, { 0.85f, 0.4f, 0.1f }, { 0.08f, 0.08f, 0.08f } };
				// seven posts a sample (fourteen for two, as before), so three samples have posts behind the outer ones too
				const int posts = 7 * static_cast<int>(std::max<size_t>(n, 2));
				for (int i = 0; i < posts; ++i)
				{
					GB::RenderObject post;
					post.MeshPtr = &box;
					post.Mat.Albedo = colours[i % 7];
					post.Mat.Roughness = 0.6f;
					post.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { (static_cast<float>(i) - 0.5f * static_cast<float>(posts - 1)) * 0.42f, 1.0f, -1.6f }), { 0.3f, 2.2f, 0.3f });
					post.BoundingRadius = 1.3f;
					sc.Objects.push_back(post);
				}
			}
			GB::Camera close;
			const size_t perRow = n <= 4 ? n : (n + 1) / 2, rows = (n + perRow - 1) / perRow;
			close.Target = { 0.0f, 0.45f, 0.1f - 1.3f * static_cast<float>(rows - 1) };
			close.Position = { 0.0f, 2.1f + 1.4f * static_cast<float>(rows - 1), 1.6f + 1.6f * static_cast<float>(perRow) };
			close.FovYDegrees = 40.0f;
			if (f.Shape == "cards")
			{
				close.Target = { 0.0f, 0.4f, 0.0f };
				close.Position = { 0.0f, 1.1f, 0.9f + 1.5f * static_cast<float>(perRow) };
			}
			save(sc, close, "galeria_" + f.Name + ".png");
		}
		// T106: a close-up of the leaf cards' micro-relief: the three leaf cards curved and leaning back toward a camera 45 degrees above them,
		// under a low sun from the front right (about 20 degrees high), which rakes across the veins and puts the highlights on the lamina
		{
			GB::Scene sc = stage({ 0.0f, 0.0f, 0.0f }, 2.5f);
			sc.Sun.Direction = { -0.75f, -0.34f, -0.55f };
			const char* ids[] = { "leaf_broad", "leaf_small", "leaf_needle" };
			for (int i = 0; i < 3; ++i)
			{
				const GB::MeshData data = GB::ProcMesh::CurvedCard(0.9f, 0.9f, 4, 6, -0.1f, 0.06f, 0.2f, 0.006f, GB::VisualHash(ids[i]));
				GB::RenderObject o;
				o.MeshPtr = keep(data); o.Mat = material(ids[i]); o.BoundingRadius = RadiusOf(data);
				o.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), { (static_cast<float>(i) - 1.0f) * 0.95f, 0.02f, 0.0f }), glm::radians(-38.0f), glm::vec3(1, 0, 0));
				sc.Objects.push_back(o);
			}
			GB::Camera cam;
			cam.Target = { 0.0f, 0.3f, -0.3f };
			cam.Position = { 0.0f, 0.3f + 1.75f, -0.3f + 1.75f };
			cam.FovYDegrees = 40.0f;
			save(sc, cam, "hojas_detalle.png");
		}
		// T107: the plants against the light, as the reference (art-book .../ref_vegetacion_v3.jpg): the row of the leaves family (each card
		// and its plant, the conifer with its trunk) and a strip of grass tufts in front, under a low warm sun (about 17 degrees) behind
		// them, the camera looking toward it
		{
			GB::Scene sc = stage({ 0.0f, 0.0f, 0.0f }, 5.0f);
			sc.Sun.Direction = { 0.18f, -0.3f, 1.0f };
			sc.Sun.Color = { 1.0f, 0.9f, 0.74f };
			const Family* leaves = nullptr;
			for (const Family& f : families) if (f.Shape == "cards") leaves = &f;
			if (leaves)
				for (size_t i = 0; i < leaves->Materials.size(); ++i)
					addSample(sc, leaves->Materials[i], { (static_cast<float>(i) - 0.5f * static_cast<float>(leaves->Materials.size() - 1)) * 2.3f, 0.0f, 0.0f }, "cards");
			const auto gc = cardSize.find("grass_medium");
			if (library.Materials.count("grass_medium"))
				tufts(sc, "grass_medium", material("grass_medium"), gc != cardSize.end() ? gc->second : glm::vec2(0.5f), GB::VisualHash("grass_medium_backlit"),
				      { 0.0f, -0.06f, 2.0f });
			GB::Camera cam;
			cam.Target = { 0.0f, 0.3f, -0.2f };
			cam.Position = { 0.0f, 1.75f, 5.8f };
			cam.FovYDegrees = 40.0f;
			save(sc, cam, "hojas_contraluz.png");
		}

		// ---- T109: the trees. Bark: the sample's material on the trunk as TreeFactory maps it (T110). Crown: its leaf material as TreesGpu sets a crown up (the
		// crown normal baked in the mesh kept on both faces, cutoff 0.4), so no sphere blend (normalBlend 0) and no occlusion at the foot of
		// the pivot; the interior occlusion sized to the tree (the core within a quarter of its height of the trunk)
		// T111: every tree stands on a low mound of dry earth (a gallery object, not TreeFactory's: as the reference, art-book
		// .../ref_arboles_*.jpg), the tree set a little into its top so its roots dive into it; its leaves sample the gallery's leaf
		// textures at twice the repeat (UVScale 2: those textures are drawn for 0.8 m cards, TreeFactory's cards are ~1.4 m, so the
		// leaves come out at their size; the card is a whole number of tiles, so no sprig is cut at its edge)
		std::vector<const GB::Mesh*> trunkMesh(trees.size(), nullptr), crownMesh(trees.size(), nullptr), moundMesh(trees.size(), nullptr);
		GB::Material earth = soil;   // dark earth with a little moss in it
		earth.Albedo = glm::vec3(0.12f, 0.115f, 0.07f);
		auto moundOf = [&](size_t i) { return glm::vec2(std::clamp(0.16f * treeMeshes[i].Height + 0.5f, 1.1f, 2.1f), 0.16f); };   // radius, height
		auto addTree = [&](GB::Scene& sc, size_t i, const glm::vec3& at, float yaw, bool crown = true, bool mound = true)
		{
			const TreeSample& t = trees[i];
			const TreeMeshes& tm = treeMeshes[i];
			if (!trunkMesh[i]) trunkMesh[i] = keep(tm.Trunk);
			if (!crownMesh[i] && !tm.Foliage.Empty()) crownMesh[i] = keep(tm.Foliage);
			const glm::vec2 md = moundOf(i);
			glm::vec3 foot = at;
			if (mound)
			{
				if (!moundMesh[i]) moundMesh[i] = keep(GB::ProcMesh::Blob(glm::vec3(md.x, md.y, md.x), 3, 0.12f, GB::VisualHash(t.Name.c_str()), 0.0f));
				GB::RenderObject m;
				m.MeshPtr = moundMesh[i];
				m.Mat = earth;
				m.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), at), yaw, glm::vec3(0, 1, 0));
				m.BoundingRadius = md.x * 1.2f;
				sc.Objects.push_back(m);
				foot.y += md.y * 0.92f;
			}
			const float top = std::max(tm.Height, tm.BoundsHeight);
			GB::RenderObject o;
			o.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), foot), yaw, glm::vec3(0, 1, 0));
			o.BoundingCenter = { 0.0f, 0.5f * top, 0.0f };
			o.BoundingRadius = 1.1f * glm::length(glm::vec2(0.5f * top, tm.CrownRadius + 0.5f));
			o.MeshPtr = trunkMesh[i]; o.Mat = material(t.Bark);
			sc.Objects.push_back(o);
			if (crownMesh[i] && crown)
			{
				GB::Material leaves = material(t.Leaves);
				leaves.UVScale = 2.0f;
				leaves.FoliageNormals = true;
				leaves.NormalBlend = 0.0f;
				leaves.BaseOcclusion = 0.0f;
				leaves.OcclusionHeight = 0.25f * tm.Height;
				leaves.InteriorOcclusion = 0.3f;
				o.MeshPtr = crownMesh[i]; o.Mat = leaves;
				sc.Objects.push_back(o);
			}
		};
		auto treeIndex = [&](const std::string& name)
		{
			for (size_t i = 0; i < trees.size(); ++i) if (trees[i].Name == name) return static_cast<int>(i);
			return -1;
		};
		auto yawOf = [&](size_t i) { return 6.2831853f * GB::HashUnit(GB::VisualHash(trees[i].Name.c_str())); };
		// T111: a row of trees side by side, each as far from the next as their real crowns (CrownRadius) and a metre between; centred on x = 0.
		// Returns the x of every tree.
		auto rowOf = [&](const std::vector<size_t>& row, float gap)
		{
			std::vector<float> xs;
			float x = 0.0f;
			for (size_t k = 0; k < row.size(); ++k)
			{
				if (k > 0) x += treeMeshes[row[k - 1]].CrownRadius + treeMeshes[row[k]].CrownRadius + gap;
				xs.push_back(x);
			}
			for (float& v : xs) v -= 0.5f * x;
			return xs;
		};
		if (!trees.empty())
		{
			// every tree of the section in four rows: the bare ones in front, then the slender broadleaves (columnar, saplings), the wide ones
			// (spreading, ovate), the conifers at the back; in a row each tree as far from the next as their crowns, and every row behind the
			// one in front by their widest crowns (no tree reaches into another); each tree turned by a hash of its name
			std::vector<size_t> rows[4];
			for (size_t i = 0; i < trees.size(); ++i)
			{
				const GB::TreeParams& tp = trees[i].Params;
				rows[tp.Species == GB::TreeSpecies::Bare ? 0 : tp.Species == GB::TreeSpecies::Conifer ? 3 : (tp.Style == 2 || tp.Style == 3) ? 1 : 2].push_back(i);
			}
			GB::Scene sc = stage({ 0.0f, 3.0f, -20.0f }, 48.0f);
			float z = 0.0f, prevCrown = 0.0f, width = 0.0f, zBack = 0.0f;
			for (int r = 0; r < 4; ++r)
			{
				if (rows[r].empty()) continue;
				float crownMax = 0.0f;
				for (size_t i : rows[r]) crownMax = std::max(crownMax, treeMeshes[i].CrownRadius);
				if (prevCrown > 0.0f) z -= prevCrown + crownMax + 1.0f;
				const std::vector<float> xs = rowOf(rows[r], 1.0f);
				for (size_t k = 0; k < rows[r].size(); ++k)
				{
					addTree(sc, rows[r][k], { xs[k], 0.0f, z }, yawOf(rows[r][k]));
					width = std::max(width, 2.0f * std::fabs(xs[k]) + 2.0f * treeMeshes[rows[r][k]].CrownRadius);
				}
				prevCrown = crownMax;
				zBack = z;
			}
			std::fprintf(stderr, "tree gallery: %.1f m wide, %.1f m deep\n", width, -zBack);
			GB::Camera cam;
			cam.Target = { 0.0f, 2.5f, 0.45f * zBack };
			cam.Position = { 0.0f, 0.42f * width, 0.45f * zBack + 0.95f * width };
			cam.FovYDegrees = 40.0f;
			sc.Env.ShadowCenter = cam.Target;
			sc.Env.ShadowRadius = 0.6f * width - 0.5f * zBack;
			save(sc, cam, "galeria_arboles.png");
			// the same against a low warm sun behind the trees
			sc.Sun.Direction = { 0.18f, -0.3f, 1.0f };
			sc.Sun.Color = { 1.0f, 0.9f, 0.74f };
			save(sc, cam, "arboles_contraluz.png");
		}
		// T111: a row like the reference (art-book .../ref_arboles_*.jpg, GEN1 and GEN2): broadleaf, conifer, bare, sapling and cypress on
		// their mounds, on a pale floor under a pale sky, the sun from the right and in front casting the shadows to the left, the camera
		// level with the crowns
		if (treeIndex("frondoso_extendido_a") >= 0 && treeIndex("conifera_abeto_a") >= 0 && treeIndex("seco_a") >= 0 && treeIndex("planton_a") >= 0 &&
		    treeIndex("conifera_cipres_a") >= 0)
		{
			std::vector<size_t> row;
			for (const char* name : { "frondoso_extendido_a", "conifera_abeto_a", "seco_a", "planton_a", "conifera_cipres_a" })
				row.push_back(static_cast<size_t>(treeIndex(name)));
			const std::vector<float> xs = rowOf(row, 0.5f);
			const float width = xs.back() - xs.front() + treeMeshes[row.front()].CrownRadius + treeMeshes[row.back()].CrownRadius;
			GB::Scene sc = stage({ 0.0f, 3.0f, 0.0f }, 0.6f * width);
			sc.Objects[0].Mat.Albedo = glm::vec3(0.45f);
			sc.Sun.Direction = { -0.8f, -0.55f, -0.45f };
			sc.Sun.Color = { 1.0f, 0.98f, 0.95f };
			sc.Env.SkyTop = { 0.72f, 0.74f, 0.77f };
			sc.Env.SkyHorizon = { 0.82f, 0.82f, 0.83f };
			sc.Env.SkyGround = { 0.45f, 0.45f, 0.45f };
			for (size_t k = 0; k < row.size(); ++k)
				addTree(sc, row[k], { xs[k], 0.0f, 0.0f }, yawOf(row[k]));
			GB::Camera cam;
			cam.FovYDegrees = 30.0f;
			const float dist = 0.5f * width / (std::tan(glm::radians(0.5f * cam.FovYDegrees)) * 16.0f / 9.0f) + 3.0f;
			cam.Target = { 0.0f, 4.2f, 0.0f };
			cam.Position = { 0.0f, 4.6f, dist };
			save(sc, cam, "arboles_referencia.png");
		}
		// the bark close up: the trunk of a spreading broadleaf (its crown left out: it hangs down to the ground and hides the fork) and of
		// a bare tree, where the limbs leave them, under a sun low from the left
		if (treeIndex("frondoso_extendido_a") >= 0 && treeIndex("seco_a") >= 0)
		{
			GB::Scene sc = stage({ 0.0f, 2.0f, 0.0f }, 9.0f);
			sc.Sun.Direction = { -0.8f, -0.5f, -0.35f };
			addTree(sc, static_cast<size_t>(treeIndex("frondoso_extendido_a")), { -1.5f, 0.0f, 0.0f }, 0.6f, false);
			addTree(sc, static_cast<size_t>(treeIndex("seco_a")), { 1.5f, 0.0f, 0.0f }, 2.0f);
			GB::Camera cam;
			cam.Position = { 0.0f, 1.9f, 4.6f };
			cam.Target = { 0.0f, 2.0f, 0.0f };
			cam.FovYDegrees = 50.0f;
			save(sc, cam, "arboles_corteza.png");
			// T110: the fork close up, in two halves: left the spreading broadleaf's trunk where the limbs leave it (crown left out), right
			// the bare tree's; each half the middle of its own take, under the same low sun from the left
			auto take = [&](const std::string& name, float yaw, float forkY, float dist)
			{
				GB::Scene one = stage({ 0.0f, 2.0f, 0.0f }, 9.0f);
				one.Sun.Direction = { -0.8f, -0.5f, -0.35f };
				addTree(one, static_cast<size_t>(treeIndex(name)), { 0.0f, 0.0f, 0.0f }, yaw, false);
				GB::Camera c;
				c.Position = { 0.0f, forkY - 0.3f, dist };
				c.Target = { 0.0f, forkY, 0.0f };
				c.FovYDegrees = 40.0f;
				renderer.Render(one, c, &fb);
				return fb.ReadPixels();
			};
			const std::vector<uint8_t> left = take("frondoso_extendido_a", 0.6f, 1.9f, 2.6f), right = take("seco_a", 2.0f, 2.1f, 2.4f);
			std::vector<uint8_t> both(left.size());
			for (uint32_t y = 0; y < H; ++y)
				for (uint32_t x = 0; x < W; ++x)
				{
					const bool l = x < W / 2;
					const uint32_t sx = l ? x + W / 4 : x - W / 4;
					for (int c = 0; c < 4; ++c)
						both[(static_cast<size_t>(y) * W + x) * 4 + static_cast<size_t>(c)] = (l ? left : right)[(static_cast<size_t>(y) * W + sx) * 4 + static_cast<size_t>(c)];
				}
			const std::string path = outDir + "/" + (std::getenv("GB_TREE_UNION_NAME") ? std::getenv("GB_TREE_UNION_NAME") : "arboles_union.png");
			Check(GB::SavePNG(path.c_str(), W, H, both.data()), "writes " + path);
			std::fprintf(stderr, "wrote %s\n", path.c_str());
		}
		// cut wood: a stump with its cut face, a cut log with its bark, a debarked log, two fallen branches (oak-like and birch bark), a birch
		// log and two weathered split boards; every cut face in the cutFace material
		if (!cutFace.empty() && library.Materials.count("bark_oak") && library.Materials.count("bark_birch") && library.Materials.count("wood_old"))
		{
			GB::Scene sc = stage({ 0.0f, 0.0f, 0.3f }, 3.5f);
			sc.Sun.Direction = { -0.6f, -0.65f, -0.45f };
			const GB::Material face = material(cutFace);
			auto put = [&](const GB::MeshData& data, const GB::Material& mat, const glm::mat4& t)
			{
				GB::RenderObject o;
				o.MeshPtr = keep(data); o.Mat = mat; o.Transform = t; o.BoundingRadius = RadiusOf(data);
				sc.Objects.push_back(o);
			};
			const glm::mat4 lying = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1));   // +Y along +X
			// the logs turned so the camera sees their far (+X) end face
			auto yawed = [&](const glm::vec3& p, float degrees) { return glm::rotate(glm::translate(glm::mat4(1.0f), p), glm::radians(degrees), glm::vec3(0, 1, 0)) * lying; };
			const glm::mat4 stump = glm::translate(glm::mat4(1.0f), { -1.6f, 0.0f, -0.3f });
			put(BarkLog(0.36f, 0.31f, 0.5f, 28, 0.0f), material("bark_oak"), stump);
			put(CutFace(0.31f, 28, 0.5f, true, 0.7f), face, stump);
			const glm::mat4 log = yawed({ -0.7f, 0.21f, -0.7f }, -30.0f);
			put(BarkLog(0.21f, 0.19f, 1.7f, 20, 0.0f), material("bark2"), log);
			put(CutFace(0.21f, 20, 0.0f, false, 2.1f), face, log);
			put(CutFace(0.19f, 20, 1.7f, true, 4.4f), face, log);
			const glm::mat4 debarked = yawed({ -0.6f, 0.16f, 0.45f }, -25.0f);
			put(BarkLog(0.16f, 0.15f, 1.5f, 18, 0.0f), material("wood_log2"), debarked);
			put(CutFace(0.16f, 18, 0.0f, false, 1.3f), face, debarked);
			put(CutFace(0.15f, 18, 1.5f, true, 5.0f), face, debarked);
			const glm::mat4 birch = yawed({ 0.9f, 0.13f, -0.9f }, -30.0f);
			put(BarkLog(0.13f, 0.12f, 0.9f, 16, 0.0f), material("bark_birch"), birch);
			put(CutFace(0.13f, 16, 0.0f, false, 3.3f), face, birch);
			put(CutFace(0.12f, 16, 0.9f, true, 0.4f), face, birch);
			// fallen branches, tapered and bent, lying on their side
			put(BarkLog(0.07f, 0.025f, 2.2f, 10, 0.18f), material("bark2"), yawed({ -1.9f, 0.07f, 1.2f }, 0.0f));
			put(BarkLog(0.05f, 0.02f, 1.6f, 10, -0.12f), material("bark_birch"), yawed({ 0.2f, 0.05f, 1.55f }, 10.0f));
			// two weathered split boards, one board of wood_old wide (20 cm)
			const GB::MeshData board = GB::ProcMesh::Box(glm::vec3(1.2f, 0.05f, 0.2f));
			put(board, material("wood_old"), glm::rotate(glm::translate(glm::mat4(1.0f), { 1.8f, 0.025f, 0.25f }), glm::radians(80.0f), glm::vec3(0, 1, 0)));
			put(board, material("wood_old"), glm::rotate(glm::translate(glm::mat4(1.0f), { 2.1f, 0.075f, 0.3f }), glm::radians(95.0f), glm::vec3(0, 1, 0)));
			GB::Camera cam;
			cam.Position = { 0.0f, 1.8f, 3.9f };
			cam.Target = { 0.0f, 0.15f, 0.1f };
			cam.FovYDegrees = 45.0f;
			save(sc, cam, "arboles_madera.png");
		}

		// ---- T113: the stages and the states of a tree, in the light of arboles_referencia.png (a pale floor under a pale sky, the sun from the
		// right and in front). A grown tree or piece as TreesGpu draws it: the bark, the cut faces in the cutFace material, the earth, the crown
		// as addTree sets it up
		auto referenceLight = [&](const glm::vec3& centre, float radius)
		{
			GB::Scene sc = stage(centre, radius);
			sc.Objects[0].Mat.Albedo = glm::vec3(0.45f);
			sc.Sun.Direction = { -0.8f, -0.55f, -0.45f };
			sc.Sun.Color = { 1.0f, 0.98f, 0.95f };
			sc.Env.SkyTop = { 0.72f, 0.74f, 0.77f };
			sc.Env.SkyHorizon = { 0.82f, 0.82f, 0.83f };
			sc.Env.SkyGround = { 0.45f, 0.45f, 0.45f };
			return sc;
		};
		GB::Material cutMat;
		cutMat.Albedo = glm::vec3(0.62f, 0.49f, 0.33f);
		cutMat.Roughness = 0.85f;
		if (!cutFace.empty()) cutMat = material(cutFace);
		auto addGrown = [&](GB::Scene& sc, const Grown& g, const glm::vec3& at, float yaw)
		{
			const GB::Tree& t = *g.Tree;
			const float top = std::max(t.Height, t.BoundsHeight);
			GB::RenderObject o;
			o.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), at), yaw, glm::vec3(0, 1, 0));
			o.BoundingCenter = { 0.0f, 0.5f * top, 0.0f };
			o.BoundingRadius = 1.1f * glm::length(glm::vec2(0.5f * top, t.CrownRadius + 0.5f));
			o.MeshPtr = keep(t.Trunk); o.Mat = material(g.Bark);
			sc.Objects.push_back(o);
			if (!t.Cuts.Empty()) { o.MeshPtr = keep(t.Cuts); o.Mat = cutMat; sc.Objects.push_back(o); }
			if (!t.Soil.Empty()) { o.MeshPtr = keep(t.Soil); o.Mat = earth; sc.Objects.push_back(o); }
			if (!t.Foliage.Empty())
			{
				GB::Material leaves = material(g.Leaves);
				leaves.UVScale = 2.0f;
				leaves.FoliageNormals = true;
				leaves.NormalBlend = 0.0f;
				leaves.BaseOcclusion = 0.0f;
				leaves.OcclusionHeight = 0.25f * t.Height;
				leaves.InteriorOcclusion = 0.3f;
				o.MeshPtr = keep(t.Foliage); o.Mat = leaves;
				sc.Objects.push_back(o);
			}
		};
		// the six stages of every habit at their real scale, one band a habit (the same orthographic scale in all of them), each stage in a
		// column as wide as its widest crown, on a low mound sized to it; the bands stacked into one picture
		if (!growthRows.empty())
		{
			const size_t cols = growthRows[0].size();
			std::vector<float> colW(cols, 0.0f);
			float maxTop = 0.0f;
			for (const auto& row : growthRows)
				for (size_t c = 0; c < cols; ++c)
				{
					colW[c] = std::max(colW[c], 2.0f * row[c].Tree->CrownRadius + 0.8f);
					maxTop = std::max(maxTop, row[c].Tree->BoundsHeight);
				}
			float total = 0.0f;
			for (float w : colW) total += w;
			std::vector<float> colX(cols);
			{
				float x = -0.5f * total;
				for (size_t c = 0; c < cols; ++c) { colX[c] = x + 0.5f * colW[c]; x += colW[c]; }
			}
			const float pxPerM = static_cast<float>(W) / (total + 1.0f);
			const float tilt = glm::radians(6.0f);
			const uint32_t ground = 40;   // pixels under the foot of the trees in a band
			const uint32_t band = std::min<uint32_t>(H, static_cast<uint32_t>(std::ceil(maxTop * pxPerM * std::cos(tilt))) + ground + 30);
			std::vector<uint8_t> sheet;
			for (const auto& row : growthRows)
			{
				GB::Scene sc = referenceLight({ 0.0f, 3.0f, 0.0f }, 0.55f * total);
				const float yaw = 6.2831853f * GB::HashUnit(GB::VisualHash(row[0].Name.c_str()));
				for (size_t c = 0; c < cols; ++c)
				{
					const GB::Tree& t = *row[c].Tree;
					const float mr = std::clamp(0.2f * t.Height + 0.35f, 0.45f, 2.2f), mh = std::clamp(0.03f + 0.012f * t.Height, 0.035f, 0.15f);
					GB::RenderObject m;
					m.MeshPtr = keep(GB::ProcMesh::Blob(glm::vec3(mr, mh, mr), 3, 0.12f, GB::VisualHash(row[c].Name.c_str()), 0.0f));
					m.Mat = earth;
					m.Transform = glm::translate(glm::mat4(1.0f), { colX[c], 0.0f, 0.0f });
					m.BoundingRadius = mr * 1.2f;
					sc.Objects.push_back(m);
					addGrown(sc, row[c], { colX[c], mh * 0.92f, 0.0f }, yaw);
				}
				// orthographic, a little from above: the foot of the trees `ground` pixels over the bottom of the picture
				GB::Camera cam;
				cam.Orthographic = true;
				cam.OrthoHalfHeight = 0.5f * static_cast<float>(H) / pxPerM;
				const float below = (0.5f * static_cast<float>(H) - static_cast<float>(ground)) / pxPerM;   // metres from the centre down to the foot
				cam.Target = { 0.0f, below / std::cos(tilt), 0.0f };
				cam.Position = cam.Target + glm::vec3(0.0f, std::sin(tilt), std::cos(tilt)) * 200.0f;
				cam.NearPlane = 1.0f;
				cam.FarPlane = 400.0f;
				sc.Env.ShadowCenter = { 0.0f, 3.0f, 0.0f };
				sc.Env.ShadowRadius = 0.55f * total;
				renderer.Render(sc, cam, &fb);
				const std::vector<uint8_t> px = fb.ReadPixels();
				// (ReadPixels gives the top row first: the band is the last `band` rows; the first habit on top)
				sheet.insert(sheet.end(), px.end() - static_cast<std::ptrdiff_t>(band) * W * 4, px.end());
			}
			const std::string path = outDir + "/arboles_fases.png";
			Check(GB::SavePNG(path.c_str(), W, band * static_cast<uint32_t>(growthRows.size()), sheet.data()), "writes " + path);
			std::fprintf(stderr, "wrote %s (%u x %u, %.1f px a metre)\n", path.c_str(), W, band * static_cast<uint32_t>(growthRows.size()), pxPerM);
		}
		// the states: behind, the tree whole, with its branches taken at 0.5 and at 1, and a dead tree standing; in front, the stump with the
		// trunk fallen from it, the tree bucked into logs and a pile of branches, a planted sapling and an uprooted one standing on its root ball
		if (!treeStates.empty())
		{
			auto find = [&](const std::string& suffix) -> const Grown*
			{
				for (const Grown& g : treeStates)
					if (g.Name.size() >= suffix.size() && g.Name.compare(g.Name.size() - suffix.size(), suffix.size(), suffix) == 0) return &g;
				return nullptr;
			};
			const Grown* back[] = { find("_entero"), find("_ramas_0.5"), find("_ramas_1"), treeStates.back().Tree->Params.Species == GB::TreeSpecies::Bare ? &treeStates.back() : nullptr };
			const Grown* stump = find("_tocon"), * fallen = find("_tronco_caido"), * bucked = find("_troceado"), * planted = find("_planton_plantado"),
			           * uprooted = find("_planton_arrancado");
			float backW = 0.0f;
			for (const Grown* g : back) if (g) backW += 2.0f * g->Tree->CrownRadius + 1.0f;
			// the front row: [stump + fallen trunk] [bucked] [planted] [uprooted], each as wide as it reaches along +X from its origin
			struct Slot { const Grown* G; float Left, Right; };
			std::vector<Slot> front;
			if (stump && fallen) front.push_back({ stump, 1.0f, fallen->Tree->CrownRadius + 0.6f });
			if (bucked) front.push_back({ bucked, 1.0f, bucked->Tree->CrownRadius + 0.6f });
			if (planted) front.push_back({ planted, planted->Tree->CrownRadius + 0.6f, planted->Tree->CrownRadius + 0.6f });
			if (uprooted) front.push_back({ uprooted, uprooted->Tree->CrownRadius + 0.6f, uprooted->Tree->CrownRadius + 0.6f });
			float frontW = 0.0f;
			for (const Slot& s : front) frontW += s.Left + s.Right;
			const float width = std::max(backW, frontW);
			GB::Scene sc = referenceLight({ 0.0f, 2.0f, 0.0f }, 0.6f * width + 4.0f);
			float x = -0.5f * backW;
			for (const Grown* g : back)
			{
				if (!g) continue;
				x += g->Tree->CrownRadius + 0.5f;
				addGrown(sc, *g, { x, 0.0f, -4.0f }, 0.4f);
				x += g->Tree->CrownRadius + 0.5f;
			}
			x = -0.5f * frontW;
			for (const Slot& s : front)
			{
				x += s.Left;
				glm::vec3 at{ x, 0.0f, 5.0f };
				if (s.G == uprooted)
				{
					float low = 0.0f;   // stood on the bottom of its root ball
					for (const GB::Vertex& v : s.G->Tree->Soil.Vertices) low = std::min(low, v.Position.y);
					at.y = -low;
				}
				// (the pieces of the felled tree lie along +X: FallAzimuth 0, not turned)
				addGrown(sc, *s.G, at, s.G == planted || s.G == uprooted ? 0.4f : 0.0f);
				if (s.G == stump) addGrown(sc, *fallen, at, 0.0f);
				x += s.Right;
			}
			GB::Camera cam;
			cam.FovYDegrees = 30.0f;
			const float dist = 0.5f * width / (std::tan(glm::radians(0.5f * cam.FovYDegrees)) * 16.0f / 9.0f);
			cam.Target = { 0.0f, 3.2f, 1.0f };
			cam.Position = { 0.0f, 3.2f + 0.2f * dist, 1.0f + dist };
			sc.Env.ShadowCenter = { 0.0f, 2.0f, 0.0f };
			save(sc, cam, "arboles_estados.png");
		}
	}
	return failures;
}
