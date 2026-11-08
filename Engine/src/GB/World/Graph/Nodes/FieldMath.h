#pragma once
#include "GB/World/Graph/TgField.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace GB::FieldMath
{
	// Small measures several terrain graph nodes take of a field (slope, curvature, range). Header-only and pure.

	// The input at `slot`, or null (and `error` set) when a required one is missing or empty.
	inline const TgField* Require(const std::vector<const TgField*>& inputs, size_t slot, const std::string& who, const char* port, std::string& error)
	{
		const TgField* f = inputs.size() > slot ? inputs[slot] : nullptr;
		if (!f) { error = who + ": input '" + port + "' is not connected"; return nullptr; }
		if (f->N == 0) { error = who + ": input '" + port + "' is empty"; return nullptr; }
		return f;
	}

	inline const TgField* Optional(const std::vector<const TgField*>& inputs, size_t slot)
	{
		return inputs.size() > slot && inputs[slot] && inputs[slot]->N > 0 ? inputs[slot] : nullptr;
	}

	inline float Clamped(const TgField& f, int i, int j)
	{
		const int n = static_cast<int>(f.N) - 1;
		return f.At(static_cast<uint32_t>(std::clamp(i, 0, n)), static_cast<uint32_t>(std::clamp(j, 0, n)));
	}

	// Gradient (dh/dx, dh/dz) by central differences (one-sided at the border).
	inline glm::vec2 Gradient(const TgField& f, uint32_t i, uint32_t j)
	{
		const uint32_t i0 = i > 0 ? i - 1 : i, i1 = i + 1 < f.N ? i + 1 : i;
		const uint32_t j0 = j > 0 ? j - 1 : j, j1 = j + 1 < f.N ? j + 1 : j;
		const float s = f.Spacing();
		return { (f.At(i1, j) - f.At(i0, j)) / (static_cast<float>(i1 - i0) * s + 1e-6f), (f.At(i, j1) - f.At(i, j0)) / (static_cast<float>(j1 - j0) * s + 1e-6f) };
	}

	inline float SlopeDegrees(const TgField& f, uint32_t i, uint32_t j)
	{
		const glm::vec2 g = Gradient(f, i, j);
		return std::atan(std::sqrt(g.x * g.x + g.y * g.y)) * 57.2957795f;
	}

	// Mean of the ring of 8 samples `radius` cells away minus the centre, over the distance: > 0 in a hollow (a gully, a cirque), < 0 on a
	// ridge or a crest. Metres per metre, so it does not depend on the grid's spacing.
	inline float Concavity(const TgField& f, uint32_t i, uint32_t j, int radius)
	{
		const int ii = static_cast<int>(i), jj = static_cast<int>(j), r = std::max(radius, 1);
		float sum = 0.0f;
		for (int dj = -1; dj <= 1; ++dj)
			for (int di = -1; di <= 1; ++di)
				if (di != 0 || dj != 0) sum += Clamped(f, ii + di * r, jj + dj * r);
		return (sum / 8.0f - f.At(i, j)) / (static_cast<float>(r) * f.Spacing());
	}

	inline float Smoothstep(float edge0, float edge1, float x)
	{
		if (edge0 == edge1) return x < edge0 ? 0.0f : 1.0f;
		const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// Box blur of `radius` cells, `passes` times (separable, clamped at the border).
	inline void BoxBlur(TgField& f, int radius, int passes)
	{
		if (radius < 1 || f.N < 2) return;
		const int n = static_cast<int>(f.N);
		std::vector<float> tmp(f.Values.size());
		for (int pass = 0; pass < passes; ++pass)
		{
			for (int j = 0; j < n; ++j)
			{
				float sum = 0.0f;
				for (int i = -radius; i <= radius; ++i) sum += Clamped(f, i, j);
				for (int i = 0; i < n; ++i)
				{
					tmp[static_cast<size_t>(j) * f.N + static_cast<size_t>(i)] = sum / static_cast<float>(2 * radius + 1);
					sum += Clamped(f, i + radius + 1, j) - Clamped(f, i - radius, j);
				}
			}
			for (int i = 0; i < n; ++i)
			{
				float sum = 0.0f;
				auto T = [&](int jj) { return tmp[static_cast<size_t>(std::clamp(jj, 0, n - 1)) * f.N + static_cast<size_t>(i)]; };
				for (int j = -radius; j <= radius; ++j) sum += T(j);
				for (int j = 0; j < n; ++j)
				{
					f.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)) = sum / static_cast<float>(2 * radius + 1);
					sum += T(j + radius + 1) - T(j - radius);
				}
			}
		}
	}
}
