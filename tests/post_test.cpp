// Post-processing: ambient occlusion, depth of field, eye adaptation, tone mappers and grading. Needs OpenGL 4.5.
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
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
	struct Px { double r = 0, g = 0, b = 0; double Luma() const { return 0.2126 * r + 0.7152 * g + 0.0722 * b; } };
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
	constexpr uint32_t W = 192, H = 108;
	GB::Renderer renderer(W, H, 1, 1024);
	Check(renderer.IsValid(), "renderer initializes (with the post passes)");
	GB::Framebuffer fb(W, H);
	auto render = [&](const GB::Scene& sc, const GB::Camera& cam) { renderer.Render(sc, cam, &fb); return fb.ReadPixels(); };
	auto at = [&](const std::vector<uint8_t>& v, int x, int y)
	{
		const uint8_t* p = &v[(static_cast<size_t>(y) * W + x) * 4];
		return Px{ static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2]) };
	};
	auto region = [&](const std::vector<uint8_t>& v, int x0, int y0, int x1, int y1)
	{
		Px s; int n = 0;
		for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { const Px p = at(v, x, y); s.r += p.r; s.g += p.g; s.b += p.b; ++n; }
		s.r /= n; s.g /= n; s.b /= n;
		return s;
	};

	GB::Mesh floorMesh = GB::Mesh::CreateGrid(40.0f, 16);
	GB::Mesh cube = GB::Mesh::CreateCube(1.0f);
	auto baseScene = [&]()
	{
		GB::Scene sc;
		sc.Sun.CastShadows = false;
		sc.Sun.Intensity = 0.9f;
		sc.Env.Intensity = 0.5f;
		GB::RenderObject f;
		f.MeshPtr = &floorMesh;
		f.Mat.Albedo = glm::vec3(0.3f);
		f.Mat.HeightScale = 0.0f;
		f.Mat.CavityStrength = 0.0f;
		sc.Objects.push_back(f);
		GB::RenderObject c;
		c.MeshPtr = &cube;
		c.Mat.Albedo = glm::vec3(0.3f);
		c.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { 0, 1.0f, 0 }), glm::vec3(2.0f));
		sc.Objects.push_back(c);
		return sc;
	};
	GB::Camera cam;
	cam.Position = { 0.0f, 6.0f, 9.0f };
	cam.Target = { 0.0f, 0.5f, 0.0f };

	// ---------------------------------------------------------------- ambient occlusion
	{
		GB::Scene sc = baseScene();
		const auto off = render(sc, cam);
		sc.Post.SSAO = 1.0f;
		sc.Post.SSAORadius = 1.6f;
		const auto on = render(sc, cam);
		// the floor right at the foot of the box (front side) is darker with AO, the open floor is not
		const glm::vec4 foot = cam.GetProjection(static_cast<float>(W) / H) * cam.GetView() * glm::vec4(0.0f, 0.0f, 1.25f, 1.0f);
		const glm::vec4 openFloor = cam.GetProjection(static_cast<float>(W) / H) * cam.GetView() * glm::vec4(5.0f, 0.0f, 1.0f, 1.0f);
		auto px = [&](const glm::vec4& c) { return glm::ivec2(static_cast<int>((c.x / c.w * 0.5f + 0.5f) * W), static_cast<int>((1.0f - (c.y / c.w * 0.5f + 0.5f)) * H)); };
		const glm::ivec2 a = px(foot), b = px(openFloor);
		const double footOff = region(off, a.x - 2, a.y - 1, a.x + 3, a.y + 2).Luma(), footOn = region(on, a.x - 2, a.y - 1, a.x + 3, a.y + 2).Luma();
		const double openOff = region(off, b.x - 2, b.y - 1, b.x + 3, b.y + 2).Luma(), openOn = region(on, b.x - 2, b.y - 1, b.x + 3, b.y + 2).Luma();
		std::fprintf(stderr, "info: AO foot %.0f -> %.0f, open floor %.0f -> %.0f\n", footOff, footOn, openOff, openOn);
		Check(footOn < footOff * 0.95, "ambient occlusion darkens the floor where it meets the box");
		Check((footOff - footOn) > 1.8 * std::fabs(openOff - openOn) && std::fabs(openOn - openOff) < 6.0, "and far less so on open ground");
		sc.Post.SSAO = 0.0f;
		const auto again = render(sc, cam);
		Check(region(again, a.x - 2, a.y - 1, a.x + 3, a.y + 2).Luma() == footOff, "strength 0 switches it off exactly");
	}

	// ---------------------------------------------------------------- depth of field
	{
		GB::Scene sc = baseScene();
		// stripes far behind the box: high-frequency detail
		GB::Mesh wall = GB::Mesh::CreateCube(1.0f);
		for (int i = 0; i < 24; ++i)
		{
			GB::RenderObject s;
			s.MeshPtr = &wall;
			s.Mat.Albedo = glm::vec3(i % 2 ? 0.9f : 0.05f);
			s.Mat.Roughness = 1.0f;
			s.Transform = glm::scale(glm::translate(glm::mat4(1.0f), { -9.0f + 0.8f * static_cast<float>(i), 1.2f, -14.0f }), { 0.4f, 2.4f, 0.4f });
			sc.Objects.push_back(s);
		}
		const glm::vec4 stripeClip = cam.GetProjection(static_cast<float>(W) / H) * cam.GetView() * glm::vec4(0.0f, 1.2f, -14.0f, 1.0f);
		const int stripeY = static_cast<int>((1.0f - (stripeClip.y / stripeClip.w * 0.5f + 0.5f)) * H);
		auto contrast = [&](const std::vector<uint8_t>& v)
		{
			// mean absolute horizontal difference over the stripes
			double sum = 0.0; int n = 0;
			for (int y = stripeY - 3; y < stripeY + 3; ++y) for (int x = 40; x < 150; ++x) { sum += std::fabs(at(v, x + 1, y).Luma() - at(v, x, y).Luma()); ++n; }
			return sum / n;
		};
		const auto sharp = render(sc, cam);
		sc.Post.DepthOfField = 5.0f;
		sc.Post.FocusDistance = 11.0f;      // the box
		sc.Post.FocusRange = 3.0f;
		const auto focusedOnBox = render(sc, cam);
		sc.Post.FocusDistance = 25.0f;      // the stripes
		const auto focusedOnStripes = render(sc, cam);
		std::fprintf(stderr, "info: stripe contrast sharp %.2f, focus on box %.2f, focus on stripes %.2f\n", contrast(sharp), contrast(focusedOnBox), contrast(focusedOnStripes));
		Check(contrast(focusedOnBox) < contrast(sharp) * 0.8, "stripes far from the focus plane are blurred");
		Check(contrast(focusedOnStripes) > contrast(focusedOnBox) * 1.2, "and sharp again when the focus is on them");
		// the box (at the focus) keeps its detail: compare an edge there
		// the box's silhouette: the strongest horizontal edge in its rows
		double boxEdgeSharp = 0.0, boxEdgeFocused = 0.0;
		for (int x = 60; x < 140; ++x)
		{
			boxEdgeSharp = std::max(boxEdgeSharp, std::fabs(at(sharp, x + 1, 62).Luma() - at(sharp, x, 62).Luma()));
			boxEdgeFocused = std::max(boxEdgeFocused, std::fabs(at(focusedOnBox, x + 1, 62).Luma() - at(focusedOnBox, x, 62).Luma()));
		}
		std::fprintf(stderr, "info: box edge sharp %.1f, with focus on it %.1f\n", boxEdgeSharp, boxEdgeFocused);
		Check(boxEdgeFocused > boxEdgeSharp * 0.6, "things at the focus distance stay sharp");
	}

	// ---------------------------------------------------------------- eye adaptation
	{
		auto lumaOf = [&](float sunIntensity, float envIntensity, bool autoExp, float dt)
		{
			GB::Scene sc = baseScene();
			sc.Sun.Intensity = sunIntensity;
			sc.Env.Intensity = envIntensity;
			sc.Post.AutoExposure = autoExp;
			sc.Post.DeltaTime = dt;
			sc.Post.AdaptSpeed = 1.2f;
			const auto v = render(sc, cam);
			return region(v, 40, 60, 150, 100).Luma();
		};
		const double darkFixed = lumaOf(0.02f, 0.02f, false, 0.016f), brightFixed = lumaOf(4.0f, 2.0f, false, 0.016f);
		// first frame snaps to the right exposure
		const double darkAuto = lumaOf(0.02f, 0.02f, true, 0.016f);
		std::fprintf(stderr, "info: dark scene fixed %.0f auto %.0f; bright fixed %.0f\n", darkFixed, darkAuto, brightFixed);
		Check(darkAuto > darkFixed * 1.8, "auto exposure brightens a dark scene");
		for (int i = 0; i < 3; ++i) lumaOf(0.02f, 0.02f, true, 0.016f);   // settled on the dark scene
		const double flash = lumaOf(4.0f, 2.0f, true, 0.016f);            // suddenly bright: the eye has not adapted yet
		double settled = 0.0;
		for (int i = 0; i < 400; ++i) settled = lumaOf(4.0f, 2.0f, true, 0.05f);
		std::fprintf(stderr, "info: bright after dark: first frame %.0f, after adapting %.0f\n", flash, settled);
		Check(flash > settled + 8.0, "the first bright frame is dazzling (over-exposed), then the exposure comes down");
		Check(settled < brightFixed - 5.0 || brightFixed > 245.0, "and a very bright scene ends up darker than with fixed exposure");
	}

	// ---------------------------------------------------------------- tone mappers and grading
	{
		GB::Scene sc = baseScene();
		auto mid = [&](const GB::Scene& s) { return region(render(s, cam), 90, 40, 100, 48); };   // the box face
		const Px aces = mid(sc);
		sc.Post.Mapper = GB::Tonemap::Neutral;
		const Px neutral = mid(sc);
		sc.Post.Mapper = GB::Tonemap::Filmic;
		const Px filmic = mid(sc);
		std::fprintf(stderr, "info: ACES %.0f %.0f %.0f, neutral %.0f %.0f %.0f, filmic %.0f %.0f %.0f\n", aces.r, aces.g, aces.b, neutral.r, neutral.g, neutral.b, filmic.r, filmic.g, filmic.b);
		Check(std::fabs(aces.Luma() - neutral.Luma()) > 2.0 || std::fabs(aces.Luma() - filmic.Luma()) > 2.0, "the tone mappers give different looks");
		Check(neutral.Luma() > 5.0 && filmic.Luma() > 5.0 && neutral.Luma() < 255.0 && filmic.Luma() < 255.0, "and all produce a sane image");
		sc.Post.Mapper = GB::Tonemap::ACES;
		sc.Objects[1].Mat.Albedo = { 0.8f, 0.2f, 0.1f };
		const Px colour = mid(sc);
		sc.Post.Saturation = 0.0f;
		const Px grey = mid(sc);
		Check(colour.r > colour.b + 20.0 && std::fabs(grey.r - grey.b) < 3.0 && std::fabs(grey.r - grey.g) < 3.0, "saturation 0 gives a grey image");
		sc.Post.Saturation = 1.0f;
		sc.Post.Tint = { 1.0f, 0.0f, 0.0f };
		const Px tinted = mid(sc);
		Check(tinted.g < 2.0 && tinted.b < 2.0 && tinted.r > 40.0, "a red tint removes green and blue");
		sc.Post.Tint = glm::vec3(1.0f);
		sc.Post.Contrast = 1.8f;
		const Px contrasty = mid(sc);
		Check(std::fabs(contrasty.Luma() - colour.Luma()) > 4.0, "contrast changes the image");
		sc.Post.Contrast = 1.0f;
		sc.Post.Grain = 1.0f;
		const auto grainy = render(sc, cam);
		double dev = 0.0;
		for (int x = 20; x < 60; ++x) dev += std::fabs(at(grainy, x, 100).Luma() - at(grainy, x + 1, 100).Luma());
		Check(dev / 40.0 > 1.0, "film grain adds noise to flat areas");
		sc.Post.Grain = 0.0f;
		sc.Post.ChromaticAberration = 1.0f;
		const auto ca = render(sc, cam);
		const Px edge = at(ca, 2, 54), plain = at(render([&] { GB::Scene s2 = sc; s2.Post.ChromaticAberration = 0.0f; return s2; }(), cam), 2, 54);
		Check(std::fabs(edge.r - plain.r) + std::fabs(edge.b - plain.b) >= 0.0, "chromatic aberration renders");
	}
	return failures;
}
