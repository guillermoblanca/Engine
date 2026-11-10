// T91: the material gallery. The families of tests/data/material_gallery.json (ice, glass, sand, wood, rock, brick; T103: mud, vegetation,
// leaves), two to seven variants each, as a box and a ball (or, by the family's "shape": a slab of ground, tufts of grass cards, a bush of
// cards, standing leaf cards) on a neutral floor under a midday sun with shadows and the default Neutral tone mapper. The materials are read with the
// game's loader (LoadVisuals) and turned into renderer materials by the game's VisualCache::MaterialFor; the test adds the procedural normal and
// roughness maps (ProcTexture::GenerateNormal / GenerateRoughness), which the game's path does not bind yet, and the opacity of the gallery
// section for glass and ice (the blend surface of today, until T34 brings transmission).
// The small picture is a golden image (tests/golden/images/material_gallery.png; update: GB_UPDATE_GOLDEN=1 ctest -R "^material_gallery$").
// `GBGLTests material_gallery --out <dir>` also writes the full-size gallery (galeria.png, 1600x900) and a close-up of every family
// (galeria_<family>.png) there, and the new textures of T102 and T103 flat (texturas_v2.png, texturas_v3.png; no GL needed). It prints the mean albedo of every material (sRGB luminance of colour x texture) for the art bible's R2.
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
	}

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
	// Tufts of crossed cards (three a tuft, 60 degrees apart, each turned and sized by a hash) scattered over the patch; a bush of cards
	// around a centre (three upright, crossed, a ring of seven leaning out and two more on top).
	auto hash01 = [](uint32_t a, uint32_t b) { return GB::HashUnit(GB::VisualHash(a, b, 0x51ED270Bu)); };
	auto tufts = [&](const glm::vec2& card, uint32_t seed)
	{
		GB::MeshData out;
		const GB::MeshData quad = GB::ProcMesh::Card(card.x, card.y, 0.08f * card.y, 3);
		const float spacing = std::max(0.2f, 0.75f * card.x);
		const int nx = std::max(1, static_cast<int>(1.9f / spacing)), nz = std::max(1, static_cast<int>(1.5f / spacing));
		uint32_t k = seed * 977u;
		for (int z = 0; z < nz; ++z)
			for (int x = 0; x < nx; ++x, ++k)
			{
				const glm::vec3 at((static_cast<float>(x) + 0.5f) / static_cast<float>(nx) * 1.9f - 0.95f + spacing * 0.4f * (hash01(k, 1) - 0.5f), 0.06f,
				                   (static_cast<float>(z) + 0.5f) / static_cast<float>(nz) * 1.5f - 0.75f + spacing * 0.4f * (hash01(k, 2) - 0.5f));
				const float yaw = hash01(k, 3) * 3.14159f, size = 0.8f + 0.4f * hash01(k, 4);
				for (int c = 0; c < 3; ++c)
				{
					glm::mat4 m = glm::translate(glm::mat4(1.0f), at);
					m = glm::rotate(m, yaw + static_cast<float>(c) * glm::radians(60.0f), glm::vec3(0, 1, 0));
					m = glm::rotate(m, 0.12f * (hash01(k, 5 + static_cast<uint32_t>(c)) - 0.5f), glm::vec3(1, 0, 0));
					out.Append(quad, glm::scale(m, glm::vec3(size)));
				}
			}
		return out;
	};
	auto shrub = [&](const glm::vec2& card)
	{
		GB::MeshData out;
		const GB::MeshData quad = GB::ProcMesh::Card(card.x, card.y, 0.1f * card.y, 3);
		const glm::vec3 centre(0.0f, 0.06f, 0.0f);
		for (int c = 0; c < 3; ++c)
			out.Append(quad, glm::rotate(glm::translate(glm::mat4(1.0f), centre), static_cast<float>(c) * glm::radians(60.0f), glm::vec3(0, 1, 0)));
		for (int c = 0; c < 7; ++c)
		{
			const float yaw = static_cast<float>(c) * glm::radians(360.0f / 7.0f) + 0.3f;
			glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), centre), yaw, glm::vec3(0, 1, 0));
			m = glm::translate(m, glm::vec3(0.0f, 0.02f + 0.06f * hash01(static_cast<uint32_t>(c), 9), 0.12f));
			m = glm::rotate(m, glm::radians(28.0f + 14.0f * hash01(static_cast<uint32_t>(c), 10)), glm::vec3(1, 0, 0));   // lean out (+Z)
			out.Append(quad, glm::scale(m, glm::vec3(0.8f + 0.25f * hash01(static_cast<uint32_t>(c), 11))));
		}
		for (int c = 0; c < 2; ++c)
		{
			const glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), centre + glm::vec3(0.0f, 0.25f * card.y, 0.0f)), glm::radians(45.0f + 90.0f * static_cast<float>(c)), glm::vec3(0, 1, 0));
			out.Append(quad, glm::scale(glm::rotate(m, glm::radians(-50.0f), glm::vec3(1, 0, 0)), glm::vec3(0.75f)));
		}
		return out;
	};
	auto keep = [&](const GB::MeshData& data) { built.push_back(std::make_unique<GB::Mesh>(data.Vertices, data.Indices)); return built.back().get(); };

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
			GB::RenderObject g;
			g.MeshPtr = keep(shape == "tufts" ? tufts(card, seed) : shrub(card)); g.Mat = m;
			g.Transform = glm::translate(glm::mat4(1.0f), at); g.BoundingRadius = 1.6f;
			sc.Objects.push_back(g);
			return;
		}
		if (shape == "cards")
		{
			// two leaf cards standing on the floor: one facing the camera, one turned and a little behind
			GB::RenderObject a;
			a.MeshPtr = keep(GB::ProcMesh::Card(card.x, card.y, 0.0f, 1)); a.Mat = m; a.BoundingRadius = 1.0f;
			a.Transform = glm::translate(glm::mat4(1.0f), at + glm::vec3(-0.5f, 0.0f, 0.1f));
			sc.Objects.push_back(a);
			a.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), at + glm::vec3(0.5f, 0.0f, -0.25f)), glm::radians(-40.0f), glm::vec3(0, 1, 0));
			sc.Objects.push_back(a);
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
	}
	return failures;
}
