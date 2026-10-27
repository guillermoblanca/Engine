// Time of day (sun, moon, stars, ambient), light radius and the night sky background. The GL part skips without OpenGL 4.5.
#include "GB/Effects/Sky.h"
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	GB::SkyState At(float hours, float clouds = 0.2f)
	{
		GB::SkyDesc d;
		d.Hours = hours;
		d.CloudCover = clouds;
		return GB::Sky::Compute(d, GB::Environment{}, GB::DirectionalLight{});
	}
}

int main()
{
	GB::Log::Init();

	const GB::SkyState noon = At(12.0f), dusk = At(17.8f), night = At(0.0f), dawn = At(6.2f);
	Check(noon.NightFactor < 0.01f && noon.Env.Stars < 0.01f, "no stars at noon");
	Check(noon.Sun.Direction.y < -0.7f, "the noon sun shines steeply downward");
	Check(noon.Sun.Intensity > 3.9f, "the noon sun is strong");
	Check(night.NightFactor > 0.99f && night.Env.Stars > 0.5f, "stars at midnight");
	Check(night.Sun.Intensity < 0.5f && night.Sun.Intensity > 0.05f && night.Sun.Color.b > night.Sun.Color.r, "the moon is weak and bluish");
	Check(night.Sun.Direction.y < 0.0f, "the moon light travels downward");
	Check(night.Env.Exposure > noon.Env.Exposure, "exposure rises in the dark");
	Check(night.Env.SkyTop.b < noon.Env.SkyTop.b * 0.2f, "the night sky is much darker");
	Check(dusk.Env.SkyHorizon.r > dusk.Env.SkyHorizon.b * 1.3f, "the horizon turns orange at sunset");
	Check(dusk.Sun.Color.r > dusk.Sun.Color.b * 1.5f, "the low sun is warm");
	Check(dawn.Sun.Intensity < noon.Sun.Intensity * 0.6f, "the sun is dimmer near the horizon");
	Check(At(12.0f, 0.8f).Env.CloudCover == 0.8f, "cloud cover is passed on");
	Check(At(0.0f, 1.0f).Env.Stars < night.Env.Stars, "clouds hide stars");
	float prev = -1.0f; bool smooth = true;
	for (float h = 0.0f; h <= 24.0f; h += 0.1f)
	{
		const float i = At(h).Sun.Intensity;
		if (prev >= 0.0f && std::fabs(i - prev) > 1.0f) smooth = false;
		prev = i;
	}
	Check(smooth, "the light changes smoothly through the day (no jumps)");

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
	GB::Renderer renderer(160, 90, 1, 1024);
	Check(renderer.IsValid(), "renderer initializes");
	GB::Camera cam;
	cam.Position = { 0.0f, 1.5f, 0.0f };
	// Looking up at the sky, a quarter turn away from the moon (so its halo is not in view).
	const glm::vec3 moon = -night.Sun.Direction;
	const glm::vec3 side = glm::normalize(glm::cross(moon, glm::vec3(0, 1, 0)));
	cam.Target = cam.Position + glm::normalize(side + glm::vec3(0.0f, 0.7f, 0.0f)) * 10.0f;
	auto sum = [&](const GB::Scene& sc, int* bright = nullptr)
	{
		GB::Framebuffer t(160, 90);
		renderer.Render(sc, cam, &t);
		const auto v = t.ReadPixels();
		long total = 0; int b = 0;
		for (size_t i = 0; i + 3 < v.size(); i += 4)
		{
			total += v[i] + v[i + 1] + v[i + 2];
			if (v[i] + v[i + 1] + v[i + 2] > 300) ++b;
		}
		if (bright) *bright = b;
		return total;
	};
	GB::Scene day, nightScene;
	day.Sun.CastShadows = nightScene.Sun.CastShadows = false;
	day.Env = noon.Env; day.Sun = noon.Sun;
	nightScene.Env = night.Env; nightScene.Sun = night.Sun;
	int starPixels = 0, dayBright = 0;
	const long dayLum = sum(day, &dayBright), nightLum = sum(nightScene, &starPixels);
	std::fprintf(stderr, "info: sky luminance day %ld night %ld, star pixels %d\n", dayLum, nightLum, starPixels);
	Check(nightLum < dayLum / 3, "the night sky renders much darker");
	Check(starPixels > 3, "stars show up as bright pixels at night");
	nightScene.Env.Stars = 0.0f;
	int none = 0;
	sum(nightScene, &none);
	Check(none < starPixels, "stars are what makes the night sky bright pixels");
	GB::Scene cloudy;
	cloudy.Sun.CastShadows = false;
	cloudy.Env = noon.Env; cloudy.Sun = noon.Sun;
	cloudy.Env.CloudCover = 0.9f;
	Check(sum(cloudy) != dayLum, "clouds change the sky");

	// HDRI environment: the background and the ambient light come from the map, and it can be rotated.
	{
		const uint32_t W = 64, H = 32;
		std::vector<float> px(static_cast<size_t>(W) * H * 3);
		for (uint32_t y = 0; y < H; ++y)
			for (uint32_t x = 0; x < W; ++x)
			{
				const bool red = x >= W / 4 && x < 3 * W / 4;   // the half around +x (u 0.25..0.75)
				float* p = &px[(static_cast<size_t>(y) * W + x) * 3];
				p[0] = red ? 2.0f : 0.02f; p[1] = 0.05f; p[2] = red ? 0.02f : 2.0f;
			}
		auto map = GB::EnvironmentMap::FromPixels(W, H, px);
		Check(map && map->IsValid() && map->GetMipCount() == 7, "an environment map is created from float pixels");
		Check(!GB::EnvironmentMap::Load("/nonexistent/file.hdr"), "a missing HDRI returns null");
		auto sunset = GB::EnvironmentMap::Load(std::string(GB_ASSET_DIR) + "/hdri/sunset_sky.hdr");
		Check(sunset && sunset->GetWidth() == 512 && sunset->GetHeight() == 256, "the sample HDRI loads (512x256)");
		GB::Scene sc;
		sc.Sun.CastShadows = false;
		sc.Sun.Intensity = 0.0f;
		sc.Env.EnvMap = map.get();
		sc.Env.BloomIntensity = 0.0f;
		auto look = [&](glm::vec3 dir)
		{
			GB::Camera c;
			c.Position = { 0.0f, 0.0f, 0.0f };
			c.Target = dir;
			GB::Framebuffer t(32, 18);
			renderer.Render(sc, c, &t);
			const auto v = t.ReadPixels();
			const uint8_t* q = &v[(static_cast<size_t>(9) * 32 + 16) * 4];
			return glm::ivec3(q[0], q[1], q[2]);
		};
		const glm::ivec3 east = look({ 1, 0, 0 }), west = look({ -1, 0, 0 });
		std::fprintf(stderr, "info: HDRI east %d %d %d, west %d %d %d\n", east.x, east.y, east.z, west.x, west.y, west.z);
		Check(east.x > east.z * 3 && west.z > west.x * 3, "the sky background shows the HDRI (red east, blue west)");
		map->Rotation = 3.14159265f;
		const glm::ivec3 turned = look({ 1, 0, 0 });
		Check(turned.z > turned.x * 3, "rotating the HDRI turns the sky");
		map->Rotation = 0.0f;
		// A sphere is lit by the map: the side facing the red half is redder than the other.
		GB::Mesh ball = GB::Mesh::CreateSphere(1.0f, 32, 16);
		GB::RenderObject o;
		o.MeshPtr = &ball;
		o.Mat.Albedo = glm::vec3(0.8f);
		o.Mat.Roughness = 0.9f;
		sc.Objects.push_back(o);
		auto ballAt = [&](glm::vec3 camPos)
		{
			GB::Camera c;
			c.Position = camPos;
			c.Target = { 0, 0, 0 };
			GB::Framebuffer t(32, 18);
			renderer.Render(sc, c, &t);
			const auto v = t.ReadPixels();
			const uint8_t* q = &v[(static_cast<size_t>(9) * 32 + 16) * 4];
			return glm::ivec3(q[0], q[1], q[2]);
		};
		const glm::ivec3 fromEast = ballAt({ 4, 0, 0 }), fromWest = ballAt({ -4, 0, 0 });
		std::fprintf(stderr, "info: ball from east %d %d %d, from west %d %d %d\n", fromEast.x, fromEast.y, fromEast.z, fromWest.x, fromWest.y, fromWest.z);
		Check(fromEast.x > fromEast.z && fromWest.z > fromWest.x, "the HDRI lights objects from its bright side");
	}

	// Point lights with a radius fade to nothing at it.
	{
		GB::Mesh floorMesh = GB::Mesh::CreateGrid(60.0f, 8);
		GB::Scene sc;
		sc.Sun.CastShadows = false;
		sc.Sun.Intensity = 0.0f;
		sc.Env = night.Env;
		sc.Env.Intensity = 0.05f;
		GB::RenderObject floor;
		floor.MeshPtr = &floorMesh;
		floor.Mat.Albedo = glm::vec3(0.5f);
		floor.Mat.HeightScale = 0.0f;
		floor.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(floor);
		GB::Camera top;
		top.Position = { 0.0f, 40.0f, 0.1f };
		top.Target = { 0.0f, 0.0f, 0.0f };
		top.FovYDegrees = 60.0f;
		auto at = [&](float wx)
		{
			GB::Framebuffer t(160, 90);
			renderer.Render(sc, top, &t);
			const auto v = t.ReadPixels();
			const glm::vec4 clip = top.GetProjection(160.0f / 90.0f) * top.GetView() * glm::vec4(wx, 0.0f, 0.0f, 1.0f);
			const int x = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * 160.0f), y = 45;
			const uint8_t* p = &v[(static_cast<size_t>(y) * 160 + x) * 4];
			return p[0] + p[1] + p[2];
		};
		const int dark = at(0.0f);
		GB::PointLight light;
		light.Position = { 0.0f, 3.0f, 0.0f };
		light.Intensity = 60.0f;
		light.Radius = 8.0f;
		sc.PointLights.push_back(light);
		const int lit = at(0.0f), beyond = at(12.0f);
		std::fprintf(stderr, "info: light radius: dark %d lit %d beyond %d\n", dark, lit, beyond);
		Check(lit > dark + 60, "a point light lights the floor under it");
		Check(std::abs(beyond - dark) <= 3, "nothing is lit beyond the light's radius");
		for (int i = 0; i < 31; ++i)
		{
			light.Position = { 30.0f + static_cast<float>(i), 3.0f, 20.0f };
			sc.PointLights.push_back(light);
		}
		Check(at(0.0f) == lit, "32 point lights are supported (extra far lights do not change the result)");
	}
	return failures;
}
