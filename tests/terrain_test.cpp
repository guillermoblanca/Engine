// Heightmap math (CPU) and procedural dune terrain (needs OpenGL for the mesh and textures).
// The GL part is skipped, with the CPU part still reported, when there is no OpenGL 4.5 context.
#include "GB/Effects/SandSpray.h"
#include "GB/Log.h"
#include "GB/Window.h"
#include "GB/World/Terrain.h"
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
	bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
}

int main()
{
	GB::Log::Init();

	// ---------------------------------------------------------------- Heightmap
	{
		GB::Heightmap hm(11, 100.0f, { 10.0f, -20.0f });   // x in [-40, 60], z in [-70, 30], 10 m spacing
		Check(Near(hm.GetSpacing(), 10.0f, 1e-5f) && hm.GetResolution() == 11, "spacing = size / (resolution - 1)");
		Check(Near(hm.GetMin().x, -40.0f, 1e-4f) && Near(hm.GetMax().y, 30.0f, 1e-4f), "bounds follow the center");
		Check(hm.Contains(0.0f, 0.0f) && !hm.Contains(61.0f, 0.0f), "Contains");
		for (uint32_t j = 0; j < 11; ++j)
			for (uint32_t i = 0; i < 11; ++i)
			{
				const glm::vec2 p = hm.SamplePosition(i, j);
				hm.At(i, j) = 0.5f * p.x + 0.25f * p.y;   // a tilted plane
			}
		Check(Near(hm.GetHeight(3.0f, -7.5f), 0.5f * 3.0f + 0.25f * -7.5f, 1e-3f), "bilinear interpolation is exact on a plane");
		Check(Near(hm.GetHeight(-1000.0f, -1000.0f), hm.At(0, 0), 1e-4f) && Near(hm.GetHeight(1000.0f, 1000.0f), hm.At(10, 10), 1e-4f), "queries outside the map clamp to the border");
		const glm::vec3 n = hm.GetNormal(0.0f, 0.0f);
		const glm::vec3 expected = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.25f));
		Check(Near(glm::length(n), 1.0f, 1e-4f) && glm::length(n - expected) < 1e-3f, "normal of a tilted plane");
		Check(Near(hm.GetMinHeight(), hm.At(0, 0), 1e-4f) && Near(hm.GetMaxHeight(), hm.At(10, 10), 1e-4f), "min / max height");
	}

	// ---------------------------------------------------------------- Terrain (GL)
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP (GL part): no OpenGL 4.5 context\n");
		return failures ? failures : 77;
	}

	GB::DuneParams dp;
	dp.Seed = 3;
	dp.WindAngleDegrees = 20.0f;
	dp.Wavelength = 50.0f;
	dp.Height = 6.0f;
	auto terrain = GB::Terrain::CreateDunes(dp, 240.0f, 241, GB::TerrainStyle::Desert);
	const GB::Heightmap& hm = terrain->GetHeightmap();

	// Same parameters, same dunes; another seed, other dunes.
	auto again = GB::Terrain::CreateDunes(dp, 240.0f, 241);
	bool same = true;
	for (uint32_t i = 0; i < 241 * 241 && same; i += 97)
		same = hm.At(i % 241, i / 241) == again->GetHeightmap().At(i % 241, i / 241);
	Check(same, "dune generation is deterministic");
	GB::DuneParams other = dp;
	other.Seed = 4;
	auto different = GB::Terrain::CreateDunes(other, 240.0f, 241);
	bool differs = false;
	for (uint32_t i = 0; i < 241 * 241 && !differs; i += 97)
		differs = hm.At(i % 241, i / 241) != different->GetHeightmap().At(i % 241, i / 241);
	Check(differs, "a different seed gives different dunes");

	Check(hm.GetMaxHeight() - hm.GetMinHeight() > 0.7f * dp.Height && hm.GetMaxHeight() - hm.GetMinHeight() < 2.5f * dp.Height, "dune relief is about Height");

	// Walk along the wind through the middle of the map: windward slopes must be gentle and lee slopes steep.
	{
		const float angle = glm::radians(dp.WindAngleDegrees);
		const glm::vec2 wind(std::cos(angle), std::sin(angle));
		double rising = 0.0, falling = 0.0;
		int nRise = 0, nFall = 0;
		const float step = 0.5f;
		for (int line = -3; line <= 3; ++line)
		{
			const glm::vec2 origin = glm::vec2(-wind.y, wind.x) * (static_cast<float>(line) * 9.0f) - wind * 80.0f;
			float previous = terrain->GetHeight(origin.x, origin.y);
			for (int k = 1; k < 320; ++k)
			{
				const glm::vec2 p = origin + wind * (static_cast<float>(k) * step);
				const float h = terrain->GetHeight(p.x, p.y);
				const float slope = (h - previous) / step;
				if (slope > 0.01f) { rising += slope; ++nRise; }
				else if (slope < -0.01f) { falling += -slope; ++nFall; }
				previous = h;
			}
		}
		const double meanRise = rising / std::max(nRise, 1), meanFall = falling / std::max(nFall, 1);
		std::fprintf(stderr, "info: mean windward slope %.3f, mean lee slope %.3f, windward share %.2f\n", meanRise, meanFall, static_cast<double>(nRise) / (nRise + nFall));
		Check(meanFall > 1.7 * meanRise, "the lee (slip) face is steeper than the windward slope");
		Check(nRise > nFall, "the gentle windward side takes up more of each dune");
	}

	// Crest points sit on high ground.
	{
		const auto& crests = terrain->GetCrestPoints();
		Check(crests.size() > 100, "plenty of crest points");
		const float limit = hm.GetMinHeight() + 0.55f * (hm.GetMaxHeight() - hm.GetMinHeight());
		bool high = true, onTerrain = true;
		for (const glm::vec3& c : crests)
		{
			high = high && c.y >= limit - 1e-3f;
			onTerrain = onTerrain && Near(c.y, terrain->GetHeight(c.x, c.z), 0.05f);
		}
		Check(high && onTerrain, "crest points lie on the terrain, in its upper part");
	}

	// Mesh, material and helpers.
	Check(terrain->GetMesh().GetIndexCount() == 240u * 240u * 6u, "two triangles per grid cell");
	const GB::Material& mat = terrain->GetMaterial();
	Check(mat.AlbedoMap && mat.NormalMap && mat.MacroMap, "grain, ripple and macro textures are set");
	Check(Near(mat.MacroRect.x, hm.GetMin().x, 1e-3f) && Near(mat.MacroRect.z, 240.0f, 1e-3f), "the macro map covers the whole terrain");
	const auto ground = terrain->MakeGroundFunction();
	Check(Near(ground(12.3f, -45.6f), terrain->GetHeight(12.3f, -45.6f), 1e-5f), "ground function = height query");
	GB::Scene scene;
	terrain->AddToScene(scene, glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)));
	Check(scene.Objects.size() == 1 && scene.Objects[0].MeshPtr == &terrain->GetMesh(), "AddToScene adds the terrain mesh");

	// Rendering: from above the terrain fills the frame with sand-colored, lit pixels.
	{
		GB::Renderer renderer(160, 90, 1, 1024);
		Check(renderer.IsValid(), "renderer initializes");
		GB::Scene lit;
		terrain->AddToScene(lit);
		lit.Sun.Direction = { std::cos(glm::radians(20.0f)) * 0.9f, -0.43f, std::sin(glm::radians(20.0f)) * 0.9f };
		lit.Env.ShadowCenter = { 0.0f, 0.0f, 0.0f };
		lit.Env.ShadowRadius = 40.0f;
		GB::Camera cam;
		cam.Position = { 0.0f, terrain->GetHeight(0.0f, 0.0f) + 25.0f, 30.0f };
		cam.Target = { 0.0f, terrain->GetHeight(0.0f, 0.0f), 0.0f };
		cam.FarPlane = 600.0f;
		GB::Framebuffer target(160, 90);
		renderer.Render(lit, cam, &target);
		const auto px = target.ReadPixels();
		int sand = 0;
		for (int y = 50; y < 88; y += 4)
			for (int x = 4; x < 156; x += 6)
			{
				const uint8_t* p = &px[(static_cast<size_t>(y) * 160 + x) * 4];
				if (p[0] > p[2] + 25 && p[0] > 60)
					++sand;
			}
		Check(sand > 120, "the lower part of the frame is sand-colored terrain");
	}

	// Blowing sand: emits from crests near the camera, drifts downwind and dies on the ground.
	{
		GB::SandSpray spray(*terrain, 2);
		spray.SetWind({ 9.0f, 0.0f, 3.0f });
		GB::Camera cam;
		cam.Position = { 0.0f, terrain->GetHeight(0.0f, 0.0f) + 10.0f, 0.0f };
		for (int i = 0; i < 240; ++i)
			spray.Update(1.0f / 60.0f, cam);
		const auto& grains = spray.GetGrains().GetParticles();
		Check(grains.size() > 200, "sand grains are emitted from the crests");
		bool near = true, aboveGround = true;
		for (const GB::Particle& p : grains)
		{
			near = near && glm::length(glm::vec2(p.Position.x - cam.Position.x, p.Position.z - cam.Position.z)) < 80.0f + 80.0f;
			aboveGround = aboveGround && p.Position.y >= terrain->GetHeight(p.Position.x, p.Position.z) - 0.05f;
		}
		Check(near, "grains come from the neighbourhood of the camera (plus what the wind carried)");
		Check(aboveGround, "grains never sink under the dunes");
		glm::vec3 meanVel(0.0f);
		for (const GB::Particle& p : grains) meanVel += p.Velocity;
		meanVel /= static_cast<float>(grains.size());
		Check(meanVel.x > 1.0f, "grains travel downwind");
	}
	return failures;
}
