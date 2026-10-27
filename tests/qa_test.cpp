// The QA tooling itself: image comparison, golden images (accept/compare/platform override), golden data and
// fingerprints. CPU only.
#include "GB/Qa/Qa.h"
#include "GB/Renderer/Framebuffer.h"
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	GB::Image Gradient(uint32_t w = 64, uint32_t h = 36)
	{
		GB::Image img = GB::Image::Solid(w, h, 0, 0, 0);
		for (uint32_t y = 0; y < h; ++y)
			for (uint32_t x = 0; x < w; ++x)
			{
				uint8_t* p = &img.Pixels[(static_cast<size_t>(y) * w + x) * 4];
				p[0] = static_cast<uint8_t>(x * 255 / (w - 1)); p[1] = static_cast<uint8_t>(y * 255 / (h - 1)); p[2] = 90;
			}
		return img;
	}
}

int main()
{
	namespace fs = std::filesystem;
	const std::string root = "qa_test_tmp";
	fs::remove_all(root);
	GB::Qa::Config::SetOutputDir(root + "/out");

	// ---- comparison metrics
	{
		const GB::Image a = Gradient();
		GB::Image same = a;
		const GB::Qa::ImageDiff d0 = GB::Qa::Compare(a, same);
		Check(d0.Rmse == 0.0 && d0.MaxDelta == 0 && d0.BadFraction == 0.0 && GB::Qa::Within(d0, {}), "identical images match exactly");
		GB::Image noisy = a;
		for (size_t i = 0; i < noisy.Pixels.size(); i += 4) noisy.Pixels[i] = static_cast<uint8_t>(std::min(255, noisy.Pixels[i] + (i / 4 % 2 ? 2 : 0)));
		const GB::Qa::ImageDiff d1 = GB::Qa::Compare(a, noisy);
		Check(d1.MaxDelta == 2 && d1.Rmse > 0.5 && d1.Rmse < 2.0 && GB::Qa::Within(d1, {}), "a two-level noise passes the default tolerance");
		GB::Image patch = a;   // a 12x12 block replaced: a localized change
		for (uint32_t y = 10; y < 22; ++y) for (uint32_t x = 20; x < 32; ++x) { uint8_t* p = &patch.Pixels[(static_cast<size_t>(y) * 64 + x) * 4]; p[0] = 255; p[1] = 0; p[2] = 0; }
		const GB::Qa::ImageDiff d2 = GB::Qa::Compare(a, patch);
		Check(d2.BadFraction > 0.05 && !GB::Qa::Within(d2, {}), "a coloured patch is caught by the bad-pixel share");
		Check(GB::Qa::Within(d2, { 100.0, 0.5, 24 }), "and a loose tolerance accepts it");
		const GB::Qa::ImageDiff d3 = GB::Qa::Compare(a, Gradient(32, 18));
		Check(d3.SizeMismatch && !GB::Qa::Within(d3, { 1000.0, 1.0, 255 }), "a size mismatch never passes");
		Check(d2.Visual.Width == 64 * 3 && d2.Visual.Height == 36, "the visual comparison is expected | actual | difference");
	}

	// ---- golden images
	{
		const std::string dir = root + "/golden";
		const GB::Image a = Gradient();
		GB::Qa::Config::SetUpdateMode(0);
		GB::Qa::Result r = GB::Qa::CheckGoldenImage(dir, "gradient", a);
		Check(!r.Passed && r.Message.find("GB_UPDATE_GOLDEN") != std::string::npos && fs::exists(root + "/out/gradient.actual.png"), "a missing reference fails with instructions and leaves the candidate");
		GB::Qa::Config::SetUpdateMode(1);
		r = GB::Qa::CheckGoldenImage(dir, "gradient", a);
		Check(r.Passed && fs::exists(dir + "/gradient.png"), "update mode writes the reference");
		GB::Qa::Config::SetUpdateMode(0);
		r = GB::Qa::CheckGoldenImage(dir, "gradient", a);
		Check(r.Passed, "and it then matches (PNG round trip keeps the pixels and the orientation)");
		GB::Image changed = a;
		for (uint32_t y = 0; y < 36; ++y) for (uint32_t x = 0; x < 64; ++x) changed.Pixels[(static_cast<size_t>(y) * 64 + x) * 4 + 2] = 200;
		r = GB::Qa::CheckGoldenImage(dir, "gradient", changed);
		Check(!r.Passed && fs::exists(root + "/out/gradient.compare.png") && r.Message.find("rmse") != std::string::npos, "a changed render fails and leaves actual + compare images");
		// a platform-specific reference wins over the generic one
		GB::Image platformImage = changed;
		fs::create_directories(dir + "/" + GB::Qa::Config::Platform());
		GB::SavePNG((dir + "/" + GB::Qa::Config::Platform() + "/gradient.png").c_str(), 64, 36, platformImage.Pixels.data());
		r = GB::Qa::CheckGoldenImage(dir, "gradient", changed);
		Check(r.Passed, "a reference in the platform folder takes precedence");
	}

	// ---- golden data
	{
		const std::string dir = root + "/golden";
		GB::Qa::GoldenData data;
		data.Set("trees", 120, 0);
		data.Set("height.max", 14.25, 0.05);
		data.Set("share.forest", 0.4, 0, 0.02);
		GB::Qa::Config::SetUpdateMode(0);
		Check(!data.Check(dir, "world").Passed, "missing reference data fails");
		GB::Qa::Config::SetUpdateMode(1);
		Check(data.Check(dir, "world").Passed, "update mode writes it");
		GB::Qa::Config::SetUpdateMode(0);
		Check(data.Check(dir, "world").Passed, "and it matches");
		GB::Qa::GoldenData near;
		near.Set("trees", 120, 0); near.Set("height.max", 14.28, 0.05); near.Set("share.forest", 0.405, 0, 0.02);
		Check(near.Check(dir, "world").Passed, "values within their tolerances pass");
		GB::Qa::GoldenData far;
		far.Set("trees", 121, 0); far.Set("height.max", 14.25, 0.05); far.Set("share.forest", 0.4, 0, 0.02);
		const GB::Qa::Result bad = far.Check(dir, "world");
		Check(!bad.Passed && bad.Message.find("trees") != std::string::npos && bad.Message.find("121") != std::string::npos, "a value off by one with zero tolerance fails and says which");
		GB::Qa::GoldenData missing;
		missing.Set("trees", 120, 0); missing.Set("height.max", 14.25, 0.05); missing.Set("extra", 1, 0);
		const GB::Qa::Result res = missing.Check(dir, "world");
		Check(!res.Passed && res.Message.find("extra") != std::string::npos && res.Message.find("share.forest") != std::string::npos, "new and vanished keys are both reported");
	}

	// ---- fingerprints
	{
		auto print = [](float wobble, float scale)
		{
			GB::Qa::Fingerprint f;
			for (int i = 0; i < 100; ++i) f.Add(static_cast<float>(i) * 0.37f * scale + wobble * (i % 3 ? 1.0f : -1.0f), 0.01f);
			return f.Value();
		};
		Check(print(0.0f, 1.0f) == print(0.0f, 1.0f), "the same data gives the same fingerprint");
		Check(print(0.0001f, 1.0f) == print(0.0f, 1.0f), "a difference far below the quantum does not change it");
		Check(print(0.0f, 1.01f) != print(0.0f, 1.0f), "a real change does");
		GB::Qa::Fingerprint g; g.Add(int64_t{ 42 });
		Check(g.AsNumber() >= 0.0 && g.AsNumber() < 4503599627370496.0 && static_cast<uint64_t>(g.AsNumber()) == (g.Value() & ((1ull << 52) - 1)), "AsNumber is exact for storing in golden data");
	}
	fs::remove_all(root);
	return failures;
}
