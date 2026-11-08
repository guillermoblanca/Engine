#pragma once
#include <cstdint>

namespace GB
{
	// Stable fingerprint of float data: values are quantized first, so tiny cross-platform differences do not
	// change it. FNV-1a, 64 bit. Core utility (architecture step 3, H11): the save digests use it without seeing the QA tools;
	// GB/Qa/Qa.h keeps the old name Qa::Fingerprint as an alias.
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
