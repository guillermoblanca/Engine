#pragma once
#include <algorithm>
#include <cstdint>
#include <thread>
#include <vector>

namespace GB
{
	// Runs row(j) for every j in [0, rows), the rows split into a few contiguous bands, one thread each (T125). Only for a loop whose rows are
	// independent: a row reads shared inputs that nothing writes meanwhile and writes only its own outputs, with no state carried from one row
	// to the next (no accumulator, no random stream, no shared container that grows). Then every row computes exactly what the sequential loop
	// computed, so the result is the same bit for bit however many threads there are: the generator stays deterministic.
	template <class Row>
	void ParallelRows(uint32_t rows, Row&& row, uint32_t minRowsPerThread = 32)
	{
		const uint32_t hardware = std::max(1u, std::thread::hardware_concurrency());
		const uint32_t count = std::min({ hardware, 16u, std::max(1u, rows / std::max(1u, minRowsPerThread)) });
		auto band = [&](uint32_t t)
		{
			const uint32_t j0 = static_cast<uint32_t>(static_cast<uint64_t>(rows) * t / count), j1 = static_cast<uint32_t>(static_cast<uint64_t>(rows) * (t + 1) / count);
			for (uint32_t j = j0; j < j1; ++j) row(j);
		};
		if (count <= 1) { band(0); return; }
		std::vector<std::thread> threads;
		threads.reserve(count - 1);
		for (uint32_t t = 1; t < count; ++t) threads.emplace_back(band, t);
		band(0);
		for (std::thread& thread : threads) thread.join();
	}
}
