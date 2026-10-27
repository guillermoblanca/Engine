#pragma once
#include "GB/Renderer/Image.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace GB::Qa
{
	// Golden-master testing, in the spirit of Unity's Graphics Test Framework (reference images, accepted on demand)
	// and Houdini's geodiff (data compared with tolerances). CPU only: works without a window.
	//
	//   GB_UPDATE_GOLDEN=1   writes the references instead of comparing (review the diff, then commit them)
	//   GB_QA_OUT=dir        where failures leave their artifacts (default ./qa_artifacts)
	struct Config
	{
		static bool UpdateMode();
		static void SetUpdateMode(int mode);          // -1 = follow the environment, 0 = off, 1 = on (for tests)
		static std::string OutputDir();
		static void SetOutputDir(const std::string& dir);
		static std::string Platform();                // "linux", "windows" or "macos"
	};

	struct ImageTolerance
	{
		double Rmse = 3.0;             // root mean square error over all channels, 0..255
		double BadFraction = 0.01;     // share of pixels allowed to differ by more than BadDelta in any channel
		int BadDelta = 24;
	};

	struct ImageDiff
	{
		bool SizeMismatch = false;
		double Rmse = 0.0;
		int MaxDelta = 0;
		double BadFraction = 0.0;
		Image Visual;                  // expected | actual | difference x8, side by side
	};

	struct Result
	{
		bool Passed = true;
		std::string Message;
	};

	ImageDiff Compare(const Image& expected, const Image& actual, int badDelta = 24);
	bool Within(const ImageDiff& diff, const ImageTolerance& tol);

	// Compares `actual` (top-left origin RGBA8) with <goldenDir>/<platform>/<name>.png if it exists, else
	// <goldenDir>/<name>.png. A missing reference fails with instructions (or is created in update mode).
	// On failure, <out>/<name>.actual.png and <out>/<name>.compare.png are written.
	Result CheckGoldenImage(const std::string& goldenDir, const std::string& name, const Image& actual, const ImageTolerance& tol = {});

	// Named numbers (counts, statistics, hashes of generated data) compared to <goldenDir>/<name>.txt.
	// Tolerances live in the code that sets the values, so updating a reference never loosens a check.
	class GoldenData
	{
	public:
		void Set(const std::string& key, double value, double absTolerance = 0.0, double relTolerance = 0.0);
		Result Check(const std::string& goldenDir, const std::string& name) const;
		size_t Size() const { return m_Entries.size(); }

	private:
		struct Entry { std::string Key; double Value, Abs, Rel; };
		std::vector<Entry> m_Entries;
	};

	// Stable fingerprint of float data: values are quantized first, so tiny cross-platform differences do not
	// change it. FNV-1a, 64 bit.
	class Fingerprint
	{
	public:
		void Add(float value, float quantum = 1e-3f);
		void Add(int64_t value);
		uint64_t Value() const { return m_Hash; }
		// A double that fits exactly (53 bits) for storing in a GoldenData.
		double AsNumber() const { return static_cast<double>(m_Hash & ((1ull << 52) - 1)); }

	private:
		uint64_t m_Hash = 1469598103934665603ull;
	};
}
