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
// SKIPs (77) without an OpenGL 4.5 context.
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Qa/Qa.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Visual/ProcMesh.h"
#include "GB/Visual/ProcTexture.h"
#include "GB/Visual/SceneVisuals.h"
#include "GB/Visual/VisualDefs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
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
	Check(families.size() == 9, "nine families");
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
	}
	return failures;
}
