#include "Qa.h"
#include "GB/Renderer/Framebuffer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace GB::Qa
{
	namespace
	{
		int g_UpdateOverride = -1;
		std::string g_OutOverride;
		namespace fs = std::filesystem;

		std::string Join(const std::string& dir, const std::string& file) { return (fs::path(dir) / file).string(); }
		void EnsureDir(const std::string& dir) { std::error_code ec; fs::create_directories(dir, ec); }
	}

	bool Config::UpdateMode()
	{
		if (g_UpdateOverride >= 0) return g_UpdateOverride != 0;
		const char* v = std::getenv("GB_UPDATE_GOLDEN");
		return v && *v && std::string(v) != "0";
	}
	void Config::SetUpdateMode(int mode) { g_UpdateOverride = mode; }
	std::string Config::OutputDir()
	{
		if (!g_OutOverride.empty()) return g_OutOverride;
		const char* v = std::getenv("GB_QA_OUT");
		return v && *v ? v : "qa_artifacts";
	}
	void Config::SetOutputDir(const std::string& dir) { g_OutOverride = dir; }
	std::string Config::Platform()
	{
#if defined(_WIN32)
		return "windows";
#elif defined(__APPLE__)
		return "macos";
#else
		return "linux";
#endif
	}

	ImageDiff Compare(const Image& expected, const Image& actual, int badDelta)
	{
		ImageDiff d;
		if (expected.Width != actual.Width || expected.Height != actual.Height || !expected.IsValid() || !actual.IsValid())
		{
			d.SizeMismatch = true;
			return d;
		}
		const size_t pixels = static_cast<size_t>(expected.Width) * expected.Height;
		double sum = 0.0;
		size_t bad = 0;
		Image diff = Image::Solid(expected.Width, expected.Height, 0, 0, 0);
		for (size_t i = 0; i < pixels; ++i)
		{
			int worst = 0;
			for (int c = 0; c < 3; ++c)
			{
				const int a = expected.Pixels[i * 4 + static_cast<size_t>(c)], b = actual.Pixels[i * 4 + static_cast<size_t>(c)];
				const int delta = std::abs(a - b);
				sum += static_cast<double>(delta) * delta;
				worst = std::max(worst, delta);
				diff.Pixels[i * 4 + static_cast<size_t>(c)] = static_cast<uint8_t>(std::min(delta * 8, 255));
			}
			d.MaxDelta = std::max(d.MaxDelta, worst);
			if (worst > badDelta) ++bad;
		}
		d.Rmse = std::sqrt(sum / static_cast<double>(pixels * 3));
		d.BadFraction = static_cast<double>(bad) / static_cast<double>(pixels);
		// side by side: expected | actual | diff
		Image all = Image::Solid(expected.Width * 3, expected.Height, 0, 0, 0);
		for (uint32_t y = 0; y < expected.Height; ++y)
			for (uint32_t k = 0; k < 3; ++k)
			{
				const Image& src = k == 0 ? expected : k == 1 ? actual : diff;
				std::copy_n(&src.Pixels[static_cast<size_t>(y) * expected.Width * 4], static_cast<size_t>(expected.Width) * 4,
					&all.Pixels[(static_cast<size_t>(y) * expected.Width * 3 + static_cast<size_t>(k) * expected.Width) * 4]);
			}
		d.Visual = std::move(all);
		return d;
	}

	bool Within(const ImageDiff& diff, const ImageTolerance& tol)
	{
		return !diff.SizeMismatch && diff.Rmse <= tol.Rmse && diff.BadFraction <= tol.BadFraction;
	}

	Result CheckGoldenImage(const std::string& goldenDir, const std::string& name, const Image& actual, const ImageTolerance& tol)
	{
		Result r;
		const std::string generic = Join(goldenDir, name + ".png");
		const std::string platform = Join(Join(goldenDir, Config::Platform()), name + ".png");
		const std::string reference = fs::exists(platform) ? platform : generic;

		if (Config::UpdateMode())
		{
			EnsureDir(fs::path(reference).parent_path().string());
			if (!SavePNG(reference.c_str(), actual.Width, actual.Height, actual.Pixels.data()))
				return { false, "could not write the reference '" + reference + "'" };
			return { true, "updated " + reference };
		}
		if (!fs::exists(reference))
		{
			EnsureDir(Config::OutputDir());
			SavePNG(Join(Config::OutputDir(), name + ".actual.png").c_str(), actual.Width, actual.Height, actual.Pixels.data());
			return { false, "no reference image '" + generic + "'. Run with GB_UPDATE_GOLDEN=1, look at the picture and commit it (the candidate is in " + Config::OutputDir() + ")" };
		}
		const Image expected = Image::Load(reference, false);
		const ImageDiff diff = Compare(expected, actual, tol.BadDelta);
		if (Within(diff, tol))
		{
			char buf[160];
			std::snprintf(buf, sizeof buf, "%s: rmse %.2f, max %d, %.2f%% pixels off", name.c_str(), diff.Rmse, diff.MaxDelta, diff.BadFraction * 100.0);
			return { true, buf };
		}
		EnsureDir(Config::OutputDir());
		SavePNG(Join(Config::OutputDir(), name + ".actual.png").c_str(), actual.Width, actual.Height, actual.Pixels.data());
		if (!diff.SizeMismatch)
			SavePNG(Join(Config::OutputDir(), name + ".compare.png").c_str(), diff.Visual.Width, diff.Visual.Height, diff.Visual.Pixels.data());
		char buf[320];
		if (diff.SizeMismatch)
			std::snprintf(buf, sizeof buf, "%s: size %ux%u differs from the reference %ux%u", name.c_str(), actual.Width, actual.Height, expected.Width, expected.Height);
		else
			std::snprintf(buf, sizeof buf, "%s: rmse %.2f (limit %.2f), max %d, %.2f%% pixels off (limit %.2f%%); see %s/%s.compare.png", name.c_str(),
				diff.Rmse, tol.Rmse, diff.MaxDelta, diff.BadFraction * 100.0, tol.BadFraction * 100.0, Config::OutputDir().c_str(), name.c_str());
		return { false, buf };
	}

	void GoldenData::Set(const std::string& key, double value, double absTolerance, double relTolerance)
	{
		for (Entry& e : m_Entries)
			if (e.Key == key) { e = { key, value, absTolerance, relTolerance }; return; }
		m_Entries.push_back({ key, value, absTolerance, relTolerance });
	}

	Result GoldenData::Check(const std::string& goldenDir, const std::string& name) const
	{
		const std::string path = Join(goldenDir, name + ".txt");
		auto write = [&](const std::string& file)
		{
			EnsureDir(fs::path(file).parent_path().string());
			std::ofstream out(file);
			out << "# golden data '" << name << "' (generated; tolerances are in the test code)\n";
			out.precision(17);
			for (const Entry& e : m_Entries) out << e.Key << " = " << e.Value << "\n";
			return out.good();
		};
		if (Config::UpdateMode())
			return write(path) ? Result{ true, "updated " + path } : Result{ false, "could not write " + path };

		std::ifstream in(path);
		if (!in)
		{
			write(Join(Config::OutputDir(), name + ".actual.txt"));
			return { false, "no reference data '" + path + "'. Run with GB_UPDATE_GOLDEN=1 and commit it (candidate in " + Config::OutputDir() + ")" };
		}
		std::map<std::string, double> ref;
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty() || line[0] == '#') continue;
			const size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			std::string key = line.substr(0, eq);
			while (!key.empty() && key.back() == ' ') key.pop_back();
			ref[key] = std::atof(line.c_str() + eq + 1);
		}
		std::ostringstream problems;
		int bad = 0;
		for (const Entry& e : m_Entries)
		{
			const auto it = ref.find(e.Key);
			if (it == ref.end()) { problems << "\n  " << e.Key << ": not in the reference"; ++bad; continue; }
			const double allowed = e.Abs + e.Rel * std::fabs(it->second);
			if (std::fabs(e.Value - it->second) > allowed)
			{
				problems << "\n  " << e.Key << ": " << e.Value << " (reference " << it->second << ", allowed +-" << allowed << ")";
				++bad;
			}
			ref.erase(it);
		}
		for (const auto& left : ref) { problems << "\n  " << left.first << ": in the reference but no longer produced"; ++bad; }
		if (bad == 0) return { true, name + ": " + std::to_string(m_Entries.size()) + " values match" };
		write(Join(Config::OutputDir(), name + ".actual.txt"));
		return { false, name + ": " + std::to_string(bad) + " value(s) differ" + problems.str() + "\n  (new values in " + Config::OutputDir() + "/" + name + ".actual.txt)" };
	}

	void Fingerprint::Add(int64_t value)
	{
		for (int i = 0; i < 8; ++i)
		{
			m_Hash ^= static_cast<uint64_t>(value >> (8 * i)) & 0xFFu;
			m_Hash *= 1099511628211ull;
		}
	}

	void Fingerprint::Add(float value, float quantum)
	{
		Add(static_cast<int64_t>(std::llround(static_cast<double>(value) / static_cast<double>(quantum))));
	}
}
