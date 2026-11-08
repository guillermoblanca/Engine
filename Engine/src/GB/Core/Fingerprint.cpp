#include "GB/Core/Fingerprint.h"
#include <cmath>

namespace GB
{
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
