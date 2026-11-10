// T91: the material gallery. The families of tests/data/material_gallery.json (ice, glass, sand, wood, rock, brick), two or three variants each,
// as a box and a ball on a neutral floor under a midday sun with shadows and the default Neutral tone mapper. The materials are read with the
// game's loader (LoadVisuals) and turned into renderer materials by the game's VisualCache::MaterialFor; the test adds the procedural normal and
// roughness maps (ProcTexture::GenerateNormal / GenerateRoughness), which the game's path does not bind yet, and the opacity of the gallery
// section for glass and ice (the blend surface of today, until T34 brings transmission).
// The small picture is a golden image (tests/golden/images/material_gallery.png; update: GB_UPDATE_GOLDEN=1 ctest -R "^material_gallery$").
// `GBGLTests material_gallery --out <dir>` also writes the full-size gallery (galeria.png, 1600x900) and a close-up of every family
// (galeria_<family>.png) there. It prints the mean albedo of every material (sRGB luminance of colour x texture) for the art bible's R2.
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
	};

	float ToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
	float ToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

	// The mean albedo of a material as the art bible measures it (R2): colour x texture, averaged in linear light, as an sRGB luminance 0..255.
	float MeanAlbedo(const GB::VisualMaterial& m)
	{
		glm::vec3 sum(0.0f);
		size_t n = 1;
		if (m.Texture.empty()) sum = m.Color;
		else
		{
			const GB::Image img = GB::ProcTexture::Generate(m.Texture, 128);
			n = static_cast<size_t>(img.Width) * img.Height;
			for (size_t i = 0; i < n; ++i)
				for (int c = 0; c < 3; ++c)
					sum[c] += ToLinear(static_cast<float>(img.Pixels[i * 4 + static_cast<size_t>(c)]) / 255.0f) * m.Color[c];
		}
		const glm::vec3 mean = sum / static_cast<float>(n);
		return ToSrgb(0.2126f * mean.r + 0.7152f * mean.g + 0.0722f * mean.b) * 255.0f;
	}

	// The gallery's ground: rows of samples along -Z, one column per family along X.
	constexpr float kColumn = 2.6f, kRow = 2.4f;
	glm::vec3 SampleAt(size_t family, size_t families, size_t row)
	{
		return { (static_cast<float>(family) - 0.5f * static_cast<float>(families - 1)) * kColumn, 0.0f, -static_cast<float>(row) * kRow };
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
		families.push_back({ f.value("name", ""), f["materials"].get<std::vector<std::string>>() });
		samples += families.back().Materials.size();
		for (const std::string& id : families.back().Materials) Check(library.Materials.count(id) == 1, "family " + families.back().Name + ": material " + id + " is defined");
	}
	Check(families.size() == 6, "six families");
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
		}
		if (const auto it = opacity.find(id); it != opacity.end()) m.Opacity = it->second;
		return m;
	};

	const GB::MeshData boxData = GB::ProcMesh::Box(glm::vec3(0.9f)), ballData = GB::ProcMesh::Blob(glm::vec3(0.45f), 4, 0.0f, 1);
	GB::Mesh box(boxData.Vertices, boxData.Indices), ball(ballData.Vertices, ballData.Indices);
	GB::Mesh floorMesh = GB::Mesh::CreateGrid(80.0f, 16);

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
	// one sample: a box turned a little (two faces lit differently) and a ball in front of it to the right
	auto addSample = [&](GB::Scene& sc, const std::string& id, const glm::vec3& at)
	{
		const GB::Material m = material(id);
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

	GB::Scene scene = stage({ 0.0f, 0.0f, -1.2f }, 9.0f);
	for (size_t c = 0; c < families.size(); ++c)
		for (size_t r = 0; r < families[c].Materials.size(); ++r) addSample(scene, families[c].Materials[r], SampleAt(c, families.size(), r));
	GB::Camera overview;
	overview.Position = { 0.0f, 7.0f, 9.6f };
	overview.Target = { 0.0f, 0.1f, -2.3f };
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
			for (size_t i = 0; i < n; ++i)
				addSample(sc, f.Materials[i], { (static_cast<float>(i) - 0.5f * static_cast<float>(n - 1)) * 2.5f, 0.0f, 0.0f });
			GB::Camera close;
			close.Target = { 0.0f, 0.45f, 0.1f };
			close.Position = { 0.0f, 2.1f, 1.6f + 1.6f * static_cast<float>(n) };
			close.FovYDegrees = 40.0f;
			save(sc, close, "galeria_" + f.Name + ".png");
		}
	}
	return failures;
}
