#pragma once
#include "GB/Core/Fingerprint.h"
#include "GB/Scene/Image.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace GB::Qa
{
	// Golden-master testing, in the spirit of Unity's Graphics Test Framework (reference images, accepted on demand)
	// and Houdini's geodiff (data compared with tolerances). CPU only: works without a window.
	//
	// What a QA run was asked to do. A plain value the caller builds and passes to every check: Config::FromEnvironment()
	// reads the two variables below, and a test that needs something else just sets the field (no process-wide override).
	//   GB_UPDATE_GOLDEN=1   writes the references instead of comparing (review the diff, then commit them)
	//   GB_QA_OUT=dir        where failures leave their artifacts (default ./qa_artifacts)
	struct Config
	{
		bool UpdateGolden = false;
		std::string OutputDir = "qa_artifacts";

		static Config FromEnvironment();
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
	Result CheckGoldenImage(const Config& config, const std::string& goldenDir, const std::string& name, const Image& actual, const ImageTolerance& tol = {});

	// Named numbers (counts, statistics, hashes of generated data) compared to <goldenDir>/<name>.txt.
	// Tolerances live in the code that sets the values, so updating a reference never loosens a check.
	class GoldenData
	{
	public:
		void Set(const std::string& key, double value, double absTolerance = 0.0, double relTolerance = 0.0);
		Result Check(const Config& config, const std::string& goldenDir, const std::string& name) const;
		size_t Size() const { return m_Entries.size(); }

	private:
		struct Entry { std::string Key; double Value, Abs, Rel; };
		std::vector<Entry> m_Entries;
	};

	// The stable fingerprint of float data now lives in Core (GB/Core/Fingerprint.h); the QA name stays as an alias.
	using Fingerprint = GB::Fingerprint;
}
