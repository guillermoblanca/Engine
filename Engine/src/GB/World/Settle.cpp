#include "GB/World/Settle.h"
#include "GB/World/Drainage.h"
#include "GB/World/Water/WaterRaster.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <utility>

namespace GB
{
	namespace
	{
		constexpr float kDeg = 57.2957795f;
		constexpr float kInf = std::numeric_limits<float>::max();

		float Smoothstep(float a, float b, float x)
		{
			const float t = std::clamp((x - a) / std::max(b - a, 1e-6f), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// ------------------------------------------------------------ records in TgPaths (Settle.h)
		TgPath Record(const glm::vec2& at, std::vector<float> values)
		{
			TgPath p;
			p.Points.assign(values.size(), at);
			p.Values = std::move(values);
			return p;
		}

		// The wet cells of a water field over the grid of a height field (WaterRaster: lakes by their mask or outline, the river's ribbon).
		WaterRaster WetOf(const TgField& h, const WaterField* water)
		{
			if (water) return RasterizeWater(*water, h.N, h.Min(), h.Spacing());
			WaterRaster r;
			r.N = h.N;
			r.Kinds.assign(h.Values.size(), WaterRaster::Dry);
			r.Level.assign(h.Values.size(), kNoWaterLevel);
			return r;
		}

		// The nearest seed cell of every cell (-1 when there is none): a two-pass propagation over the 8 neighbours, each cell offered its
		// neighbours' seeds and keeping the one nearest to it (exact distance to the seed, so a near-Euclidean transform). Linear in the cells.
		std::vector<int32_t> NearestSeed(const std::vector<uint8_t>& seed, uint32_t n, std::vector<float>* distance2 = nullptr)
		{
			const size_t count = static_cast<size_t>(n) * n;
			std::vector<int32_t> near(count, -1);
			std::vector<float> d2(count, kInf);
			for (size_t c = 0; c < count; ++c) if (seed[c]) { near[c] = static_cast<int32_t>(c); d2[c] = 0.0f; }
			const int N = static_cast<int>(n);
			auto offer = [&](int i, int j, int ni, int nj)
			{
				if (ni < 0 || nj < 0 || ni >= N || nj >= N) return;
				const int32_t s = near[static_cast<size_t>(nj) * n + static_cast<size_t>(ni)];
				if (s < 0) return;
				const float dx = static_cast<float>(i - s % N), dz = static_cast<float>(j - s / N);
				const float d = dx * dx + dz * dz;
				const size_t c = static_cast<size_t>(j) * n + static_cast<size_t>(i);
				if (d < d2[c] || (d == d2[c] && s < near[c])) { d2[c] = d; near[c] = s; }
			};
			for (int j = 0; j < N; ++j)
			{
				for (int i = 0; i < N; ++i) { offer(i, j, i - 1, j); offer(i, j, i - 1, j - 1); offer(i, j, i, j - 1); offer(i, j, i + 1, j - 1); }
				for (int i = N - 1; i >= 0; --i) offer(i, j, i + 1, j);
			}
			for (int j = N - 1; j >= 0; --j)
			{
				for (int i = N - 1; i >= 0; --i) { offer(i, j, i + 1, j); offer(i, j, i + 1, j + 1); offer(i, j, i, j + 1); offer(i, j, i - 1, j + 1); }
				for (int i = 0; i < N; ++i) offer(i, j, i - 1, j);
			}
			if (distance2) *distance2 = std::move(d2);
			return near;
		}

		// A summed-area table: the sum of a box of cells (inclusive, clamped to the grid) in O(1).
		struct Sat
		{
			int N = 0;
			std::vector<double> S;   // (N + 1)^2
			template <class F> void Build(int n, F value)
			{
				N = n;
				S.assign(static_cast<size_t>(n + 1) * static_cast<size_t>(n + 1), 0.0);
				for (int j = 0; j < n; ++j)
				{
					double row = 0.0;
					for (int i = 0; i < n; ++i)
					{
						row += value(static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i));
						S[static_cast<size_t>(j + 1) * static_cast<size_t>(n + 1) + static_cast<size_t>(i + 1)] = S[static_cast<size_t>(j) * static_cast<size_t>(n + 1) + static_cast<size_t>(i + 1)] + row;
					}
				}
			}
			// sum and number of cells of the box of half-side `r` around (i, j)
			double Sum(int i, int j, int r, int& cells) const
			{
				const int i0 = std::max(0, i - r), j0 = std::max(0, j - r), i1 = std::min(N - 1, i + r), j1 = std::min(N - 1, j + r);
				cells = (i1 - i0 + 1) * (j1 - j0 + 1);
				const size_t w = static_cast<size_t>(N + 1);
				return S[static_cast<size_t>(j1 + 1) * w + static_cast<size_t>(i1 + 1)] - S[static_cast<size_t>(j0) * w + static_cast<size_t>(i1 + 1)]
					- S[static_cast<size_t>(j1 + 1) * w + static_cast<size_t>(i0)] + S[static_cast<size_t>(j0) * w + static_cast<size_t>(i0)];
			}
			double Mean(int i, int j, int r) const { int cells = 0; const double s = Sum(i, j, r, cells); return cells ? s / cells : 0.0; }
		};

		// slope (tan) of every sample by central differences (one-sided at the border)
		std::vector<float> SlopeTan(const std::vector<float>& h, uint32_t n, float spacing)
		{
			std::vector<float> s(h.size(), 0.0f);
			for (uint32_t j = 0; j < n; ++j)
				for (uint32_t i = 0; i < n; ++i)
				{
					const uint32_t i0 = i > 0 ? i - 1 : i, i1 = i + 1 < n ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j + 1 < n ? j + 1 : j;
					const float gx = (h[static_cast<size_t>(j) * n + i1] - h[static_cast<size_t>(j) * n + i0]) / (static_cast<float>(i1 - i0) * spacing + 1e-6f);
					const float gz = (h[static_cast<size_t>(j1) * n + i] - h[static_cast<size_t>(j0) * n + i]) / (static_cast<float>(j1 - j0) * spacing + 1e-6f);
					s[static_cast<size_t>(j) * n + i] = std::sqrt(gx * gx + gz * gz);
				}
			return s;
		}

		// ------------------------------------------------------------ the nearest trail of every cell
		struct TrailHit { int Trail = -1; size_t Segment = 0; float T = 0.0f; float Distance = kInf; };

		std::vector<TrailHit> NearestTrails(const TgField& f, const std::vector<Trail>& trails, float reach)
		{
			const uint32_t n = f.N;
			const float sp = f.Spacing();
			const glm::vec2 lo = f.Min();
			std::vector<TrailHit> hits(f.Values.size());
			for (size_t t = 0; t < trails.size(); ++t)
			{
				const std::vector<glm::vec3>& P = trails[t].Points;
				for (size_t k = 0; k + 1 < P.size(); ++k)
				{
					const glm::vec2 a(P[k].x, P[k].z), b(P[k + 1].x, P[k + 1].z);
					const glm::vec2 ab = b - a;
					const float len2 = glm::dot(ab, ab);
					const int i0 = std::max(0, static_cast<int>(std::floor((std::min(a.x, b.x) - reach - lo.x) / sp)));
					const int i1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((std::max(a.x, b.x) + reach - lo.x) / sp)));
					const int j0 = std::max(0, static_cast<int>(std::floor((std::min(a.y, b.y) - reach - lo.y) / sp)));
					const int j1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((std::max(a.y, b.y) + reach - lo.y) / sp)));
					for (int j = j0; j <= j1; ++j)
						for (int i = i0; i <= i1; ++i)
						{
							const glm::vec2 p = f.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
							const float s = len2 > 1e-9f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
							const float d = glm::distance(p, a + s * ab);
							if (d > reach) continue;
							TrailHit& hit = hits[static_cast<size_t>(j) * n + static_cast<size_t>(i)];
							if (d < hit.Distance) hit = { static_cast<int>(t), k, s, d };
						}
				}
			}
			return hits;
		}

		float Median(std::vector<float> v)
		{
			if (v.empty()) return 0.0f;
			const size_t m = v.size() / 2;
			std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(m), v.end());
			const float hi = v[m];
			if (v.size() % 2) return hi;
			const float lo = *std::max_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(m));
			return 0.5f * (lo + hi);
		}
	}

	const char* TrailKindName(TrailKind kind)
	{
		switch (kind)
		{
		case TrailKind::Link: return "link";
		case TrailKind::Extra: return "extra";
		case TrailKind::Water: return "water";
		case TrailKind::Col: return "col";
		case TrailKind::Ford: return "ford";
		}
		return "?";
	}

	// ------------------------------------------------------------------ codec
	namespace
	{
		constexpr float kSiteRecord = 1.0f;   // the first value of a site's record: its layout version
		constexpr size_t kSiteValues = 26;
		constexpr size_t kTrailValues = 13;
		constexpr size_t kFordValues = 7;
		constexpr size_t kColValues = 7;
	}

	TgPaths EncodeSites(const std::vector<SettlementSite>& sites)
	{
		TgPaths out;
		for (const SettlementSite& s : sites)
			out.Items.push_back(Record(s.Position, { kSiteRecord, s.Radius, s.Score, static_cast<float>(s.Rank), s.Ground, s.PlatformLevel, s.WaterDistance,
				s.AboveWater, s.CampSlopeMax, s.CampSlopeMean, s.VillageSlopeMean, s.FlatShare, s.SteepShare, s.Roughness, s.EdgeDistance,
				s.Water, s.Flat, s.Area, s.Dry, s.Low, s.Forest, s.Stone, s.Cut, s.Fill, s.Position.x, s.Position.y }));
		return out;
	}

	bool DecodeSites(const TgPaths& paths, std::vector<SettlementSite>& out)
	{
		out.clear();
		for (const TgPath& p : paths.Items)
		{
			if (p.Values.size() != kSiteValues || p.Values[0] != kSiteRecord) { out.clear(); return false; }
			const std::vector<float>& v = p.Values;
			SettlementSite s;
			s.Position = { v[24], v[25] };
			s.Radius = v[1]; s.Score = v[2]; s.Rank = static_cast<int>(v[3]); s.Ground = v[4]; s.PlatformLevel = v[5]; s.WaterDistance = v[6];
			s.AboveWater = v[7]; s.CampSlopeMax = v[8]; s.CampSlopeMean = v[9]; s.VillageSlopeMean = v[10]; s.FlatShare = v[11]; s.SteepShare = v[12];
			s.Roughness = v[13]; s.EdgeDistance = v[14]; s.Water = v[15]; s.Flat = v[16]; s.Area = v[17]; s.Dry = v[18]; s.Low = v[19]; s.Forest = v[20];
			s.Stone = v[21]; s.Cut = v[22]; s.Fill = v[23];
			out.push_back(s);
		}
		return true;
	}

	void EncodeTrails(const std::vector<Trail>& trails, TgPaths& geometry, TgPaths& info)
	{
		geometry.Items.clear();
		info.Items.clear();
		for (const Trail& t : trails)
		{
			TgPath g;
			for (const glm::vec3& p : t.Points) { g.Points.push_back({ p.x, p.z }); g.Values.push_back(p.y); }
			geometry.Items.push_back(std::move(g));
			const glm::vec2 at = t.Points.empty() ? glm::vec2(0.0f) : glm::vec2(t.Points.front().x, t.Points.front().z);
			info.Items.push_back(Record(at, { 1.0f, static_cast<float>(t.Kind), static_cast<float>(t.From), static_cast<float>(t.To), t.Width, t.Length,
				t.Straight, t.Cost, t.Cut, t.Fill, t.WithinGrade, t.MaxGrade, static_cast<float>(t.Fords) }));
		}
	}

	bool DecodeTrails(const TgPaths& geometry, const TgPaths& info, std::vector<Trail>& out)
	{
		out.clear();
		if (geometry.Items.size() != info.Items.size()) return false;
		for (size_t k = 0; k < geometry.Items.size(); ++k)
		{
			const std::vector<float>& v = info.Items[k].Values;
			const TgPath& g = geometry.Items[k];
			if (v.size() != kTrailValues || v[0] != 1.0f || g.Values.size() != g.Points.size()) { out.clear(); return false; }
			Trail t;
			t.Kind = static_cast<TrailKind>(static_cast<int>(v[1]));
			t.From = static_cast<int>(v[2]); t.To = static_cast<int>(v[3]); t.Width = v[4]; t.Length = v[5]; t.Straight = v[6]; t.Cost = v[7];
			t.Cut = v[8]; t.Fill = v[9]; t.WithinGrade = v[10]; t.MaxGrade = v[11]; t.Fords = static_cast<int>(v[12]);
			for (size_t i = 0; i < g.Points.size(); ++i) t.Points.push_back({ g.Points[i].x, g.Values[i], g.Points[i].y });
			out.push_back(std::move(t));
		}
		return true;
	}

	TgPaths EncodeFords(const std::vector<Ford>& fords)
	{
		TgPaths out;
		for (const Ford& f : fords)
			out.Items.push_back(Record(f.Position, { 1.0f, f.Level, f.Depth, f.HalfWidth, static_cast<float>(f.Used), f.Built ? 1.0f : 0.0f, 0.0f }));
		return out;
	}

	bool DecodeFords(const TgPaths& paths, std::vector<Ford>& out)
	{
		out.clear();
		for (const TgPath& p : paths.Items)
		{
			if (p.Values.size() != kFordValues || p.Values[0] != 1.0f || p.Points.empty()) { out.clear(); return false; }
			Ford f;
			f.Position = p.Points.front(); f.Level = p.Values[1]; f.Depth = p.Values[2]; f.HalfWidth = p.Values[3]; f.Used = static_cast<int>(p.Values[4]); f.Built = p.Values[5] > 0.5f;
			out.push_back(f);
		}
		return true;
	}

	TgPaths EncodeCols(const std::vector<Col>& cols)
	{
		TgPaths out;
		for (const Col& c : cols)
			out.Items.push_back(Record(c.Position, { 1.0f, c.Height, static_cast<float>(c.BasinA), static_cast<float>(c.BasinB), static_cast<float>(c.Site), c.Grade, 0.0f }));
		return out;
	}

	bool DecodeCols(const TgPaths& paths, std::vector<Col>& out)
	{
		out.clear();
		for (const TgPath& p : paths.Items)
		{
			if (p.Values.size() != kColValues || p.Values[0] != 1.0f || p.Points.empty()) { out.clear(); return false; }
			Col c;
			c.Position = p.Points.front(); c.Height = p.Values[1]; c.BasinA = static_cast<int>(p.Values[2]); c.BasinB = static_cast<int>(p.Values[3]);
			c.Site = static_cast<int>(p.Values[4]); c.Grade = p.Values[5];
			out.push_back(c);
		}
		return true;
	}

	// ------------------------------------------------------------------ 1. sites
	SiteResult FindSettlementSites(const TgField& height, const WaterField* water, const TgField* forest, const SiteParams& P)
	{
		SiteResult out;
		const uint32_t n = height.N;
		const int N = static_cast<int>(n);
		const size_t count = height.Values.size();
		out.Score.assign(count, 0.0f);
		out.WaterDistance.assign(count, kNoWaterDistance);
		if (n < 3) return out;
		const float sp = height.Spacing();
		const std::vector<float>& h = height.Values;
		const WaterRaster wet = WetOf(height, water);
		std::vector<uint8_t> wetSeed(count, 0);
		bool anyWet = false;
		for (size_t c = 0; c < count; ++c) if (wet.Wet(c)) { wetSeed[c] = 1; anyWet = true; }
		std::vector<float> d2;
		const std::vector<int32_t> nearWet = NearestSeed(wetSeed, n, &d2);
		for (size_t c = 0; c < count; ++c) out.WaterDistance[c] = anyWet ? std::sqrt(d2[c]) * sp : kNoWaterDistance;

		std::vector<float> slope = SlopeTan(h, n, sp);
		for (float& s : slope) s = std::atan(s) * kDeg;
		const float lowest = *std::min_element(h.begin(), h.end()), span = std::max(*std::max_element(h.begin(), h.end()) - lowest, 1.0f);
		Sat slopeSum, steep, flat, wall, wood, rock, wetIn;
		slopeSum.Build(N, [&](size_t c) { return static_cast<double>(slope[c]); });
		steep.Build(N, [&](size_t c) { return slope[c] > 14.0f ? 1.0 : 0.0; });
		flat.Build(N, [&](size_t c) { return slope[c] <= 8.0f ? 1.0 : 0.0; });
		wall.Build(N, [&](size_t c) { return slope[c] > 40.0f && !wet.Wet(c) ? 1.0 : 0.0; });
		rock.Build(N, [&](size_t c) { return slope[c] > P.StoneSlope && !wet.Wet(c) ? 1.0 : 0.0; });
		wetIn.Build(N, [&](size_t c) { return wet.Wet(c) ? 1.0 : 0.0; });
		if (forest && forest->N == n) wood.Build(N, [&](size_t c) { return forest->Values[c] >= 0.5f ? 1.0 : 0.0; });

		auto box = [&](float radius) { return std::max(0, static_cast<int>(std::lround(radius * 0.8862f / sp))); };   // the square of the disc's area
		const int discR = box(P.Radius), wallR = std::max(1, static_cast<int>(std::lround(P.WallDistance / sp))), woodR = box(P.ForestRadius), rockR = box(P.StoneRadius);
		const int campR = std::max(1, static_cast<int>(std::ceil(P.CampRadius / sp)));
		const int stride = std::max(1, static_cast<int>(std::lround(P.Step / sp)));
		const glm::vec2 lo = height.Min(), hi = lo + glm::vec2(height.Size);

		struct Candidate { SettlementSite Site; size_t Cell; };
		std::vector<Candidate> good;
		for (int j = 0; j < N; j += stride)
			for (int i = 0; i < N; i += stride)
			{
				++out.Candidates;
				const size_t c = static_cast<size_t>(j) * n + static_cast<size_t>(i);
				const glm::vec2 p = height.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
				SettlementSite s;
				s.Position = p;
				s.Radius = P.Radius;
				s.Ground = s.PlatformLevel = h[c];
				s.EdgeDistance = std::min(std::min(p.x - lo.x, hi.x - p.x), std::min(p.y - lo.y, hi.y - p.y));
				if (s.EdgeDistance < P.Edge || wet.Wet(c)) continue;
				s.WaterDistance = out.WaterDistance[c];
				if (s.WaterDistance < P.WaterMin || s.WaterDistance > P.WaterMax) continue;
				s.AboveWater = nearWet[c] >= 0 ? h[c] - wet.Level[static_cast<size_t>(nearWet[c])] : 1e9f;
				if (s.AboveWater < P.FloodHeight) continue;
				const float relative = (h[c] - lowest) / span;
				if (relative > P.MaxRelativeHeight) continue;
				// the camp's ground (S2)
				float worst = 0.0f, sum = 0.0f;
				int cells = 0;
				for (int dj = -campR; dj <= campR; ++dj)
					for (int di = -campR; di <= campR; ++di)
					{
						if (static_cast<float>(di * di + dj * dj) * sp * sp > P.CampRadius * P.CampRadius) continue;
						const int ii = std::clamp(i + di, 0, N - 1), jj = std::clamp(j + dj, 0, N - 1);
						const float d = slope[static_cast<size_t>(jj) * n + static_cast<size_t>(ii)];
						worst = std::max(worst, d);
						sum += d;
						++cells;
					}
				s.CampSlopeMax = worst;
				s.CampSlopeMean = cells ? sum / static_cast<float>(cells) : 0.0f;
				if (s.CampSlopeMax > P.CampMaxSlope || s.CampSlopeMean > P.CampMeanSlope) continue;
				// the game's own filter (PickVillageSites): how far the ground departs from the centre's 22 m away
				for (int k = 0; k < 8; ++k)
				{
					const float a = static_cast<float>(k) * 0.785398f;
					s.Roughness = std::max(s.Roughness, std::fabs(height.Sample(p.x + P.RoughRadius * std::cos(a), p.y + P.RoughRadius * std::sin(a)) - h[c]));
				}
				if (s.Roughness > P.MaxRough) continue;
				// the village's disc (X3, the flat ground for it)
				int discCells = 0;
				s.VillageSlopeMean = static_cast<float>(slopeSum.Mean(i, j, discR));
				s.SteepShare = static_cast<float>(steep.Sum(i, j, discR, discCells) / std::max(discCells, 1));
				s.FlatShare = static_cast<float>(flat.Sum(i, j, discR, discCells) / std::max(discCells, 1));
				if (s.SteepShare > P.SteepShareMax || s.FlatShare < P.FlatShareMin) continue;
				if (wetIn.Sum(i, j, discR, discCells) > 0.0) continue;
				if (wall.Sum(i, j, wallR, discCells) > 0.0) continue;
				// the parts
				const float d = s.WaterDistance;
				s.Water = d <= P.WaterBest ? 0.7f + 0.3f * (d - P.WaterMin) / std::max(P.WaterBest - P.WaterMin, 1e-3f)
					: 1.0f - 0.6f * (d - P.WaterBest) / std::max(P.WaterMax - P.WaterBest, 1e-3f);
				s.Flat = std::clamp(1.0f - s.VillageSlopeMean / std::max(P.CampMaxSlope, 1e-3f), 0.0f, 1.0f);
				s.Area = s.FlatShare;
				s.Dry = std::clamp((s.AboveWater - P.FloodHeight) / 3.5f, 0.0f, 1.0f);
				s.Low = 1.0f - relative;
				int woodCells = 0, rockCells = 0;
				s.Forest = wood.N ? std::min(1.0f, static_cast<float>(wood.Sum(i, j, woodR, woodCells) / std::max(woodCells, 1)) / 0.3f) : 0.0f;
				s.Stone = std::min(1.0f, static_cast<float>(rock.Sum(i, j, rockR, rockCells) / std::max(rockCells, 1)) / 0.05f);
				s.Score = P.WWater * s.Water + P.WFlat * s.Flat + P.WArea * s.Area + P.WDry * s.Dry + P.WLow * s.Low + P.WForest * s.Forest + P.WStone * s.Stone;
				for (int dj = 0; dj < stride; ++dj)
					for (int di = 0; di < stride; ++di)
					{
						const int ii = i + di - stride / 2, jj = j + dj - stride / 2;
						if (ii >= 0 && jj >= 0 && ii < N && jj < N) out.Score[static_cast<size_t>(jj) * n + static_cast<size_t>(ii)] = s.Score;
					}
				good.push_back({ s, c });
			}
		out.Accepted = good.size();
		std::stable_sort(good.begin(), good.end(), [](const Candidate& a, const Candidate& b) { return a.Site.Score != b.Site.Score ? a.Site.Score > b.Site.Score : a.Cell < b.Cell; });
		for (const Candidate& c : good)
		{
			if (static_cast<int>(out.Sites.size()) >= P.Count) break;
			if (std::all_of(out.Sites.begin(), out.Sites.end(), [&](const SettlementSite& s) { return glm::distance(s.Position, c.Site.Position) >= P.Separation; }))
			{
				out.Sites.push_back(c.Site);
				out.Sites.back().Rank = static_cast<int>(out.Sites.size()) - 1;
			}
		}
		return out;
	}

	// ------------------------------------------------------------------ 2. routes
	namespace
	{
		struct Move { int Di, Dj; float Length; int Ai, Aj, Bi, Bj; };   // a step and the cells a long (knight) step passes over
		const std::vector<Move>& Moves16()
		{
			static const std::vector<Move> moves = []
			{
				std::vector<Move> m;
				for (int dj = -2; dj <= 2; ++dj)
					for (int di = -2; di <= 2; ++di)
					{
						const int a = std::abs(di), b = std::abs(dj);
						if ((a == 0 && b == 0) || (a == 2 && b != 1) || (b == 2 && a != 1)) continue;
						Move mv{ di, dj, std::sqrt(static_cast<float>(di * di + dj * dj)), 0, 0, 0, 0 };
						if (a == 2) { mv.Ai = di / 2; mv.Aj = 0; mv.Bi = di / 2; mv.Bj = dj; }
						else if (b == 2) { mv.Ai = 0; mv.Aj = dj / 2; mv.Bi = di; mv.Bj = dj / 2; }
						m.push_back(mv);
					}
				return m;
			}();
			return moves;
		}

		struct RouteGrid
		{
			uint32_t M = 0;
			int F = 1;
			float Cell = 1.0f;
			glm::vec2 Min{ 0.0f };
			std::vector<float> H, Side;
			std::vector<uint8_t> Kind;      // WaterRaster kinds
			std::vector<uint8_t> FordZone;  // a river cell a trail may wade
			glm::vec2 Position(size_t c) const { return Min + glm::vec2(static_cast<float>(c % M), static_cast<float>(c / M)) * Cell; }
			size_t CellOf(const glm::vec2& p) const
			{
				const int i = std::clamp(static_cast<int>(std::lround((p.x - Min.x) / Cell)), 0, static_cast<int>(M) - 1);
				const int j = std::clamp(static_cast<int>(std::lround((p.y - Min.y) / Cell)), 0, static_cast<int>(M) - 1);
				return static_cast<size_t>(j) * M + static_cast<size_t>(i);
			}
			bool Blocked(size_t c) const { return Kind[c] == WaterRaster::Lake || (Kind[c] == WaterRaster::River && !FordZone[c]); }
		};

		// The cost of a step from a to b (kInf when it cannot be taken): its length weighted by the grade along it, the cross slope of b and
		// the water of a ford.
		float StepCost(const RouteGrid& g, const RouteParams& P, size_t a, const Move& m)
		{
			const int M = static_cast<int>(g.M);
			const int ai = static_cast<int>(a % g.M), aj = static_cast<int>(a / g.M), bi = ai + m.Di, bj = aj + m.Dj;
			if (bi < 0 || bj < 0 || bi >= M || bj >= M) return kInf;
			const size_t b = static_cast<size_t>(bj) * g.M + static_cast<size_t>(bi);
			if (g.Blocked(b)) return kInf;
			if (m.Ai || m.Aj || m.Bi || m.Bj)
			{
				const size_t x = static_cast<size_t>(aj + m.Aj) * g.M + static_cast<size_t>(ai + m.Ai), y = static_cast<size_t>(aj + m.Bj) * g.M + static_cast<size_t>(ai + m.Bi);
				if (g.Blocked(x) || g.Blocked(y)) return kInf;
			}
			const float d = m.Length * g.Cell;
			const bool wade = g.Kind[b] == WaterRaster::River || g.Kind[a] == WaterRaster::River;
			const float grade = std::fabs(g.H[b] - g.H[a]) / d;
			if (grade > P.CliffGrade && !wade) return kInf;
			const float G = std::max(P.MaxGrade, 1e-3f);
			float factor = 1.0f + P.GradeWeight * std::min(grade / G, 1.0f) * std::min(grade / G, 1.0f);
			if (grade > G) factor += P.SteepWeight * (grade / G - 1.0f);
			if (!wade)
			{
				const float side = g.Side[b];
				if (side > 0.839f) return kInf;   // 40 degrees: a wall for the navigation
				factor += P.SideWeight * std::max(0.0f, side - P.SideFree) + P.SideMaxWeight * std::max(0.0f, side - P.SideMax);
			}
			else factor *= P.FordCost;
			return d * factor;
		}

		std::vector<glm::vec2> Smooth(std::vector<glm::vec2> pts, float spacing)
		{
			for (int pass = 0; pass < 2 && pts.size() > 2; ++pass)   // Chaikin, the ends kept
			{
				std::vector<glm::vec2> s;
				s.push_back(pts.front());
				for (size_t k = 0; k + 1 < pts.size(); ++k)
				{
					s.push_back(0.75f * pts[k] + 0.25f * pts[k + 1]);
					s.push_back(0.25f * pts[k] + 0.75f * pts[k + 1]);
				}
				s.push_back(pts.back());
				pts = std::move(s);
			}
			// resample every `spacing` metres
			std::vector<glm::vec2> out;
			if (pts.empty()) return out;
			out.push_back(pts.front());
			float carry = 0.0f;
			for (size_t k = 0; k + 1 < pts.size(); ++k)
			{
				const float len = glm::distance(pts[k], pts[k + 1]);
				float at = spacing - carry;
				while (at < len) { out.push_back(glm::mix(pts[k], pts[k + 1], at / len)); at += spacing; }
				carry = len - (at - spacing);
			}
			if (glm::distance(out.back(), pts.back()) > 0.25f * spacing) out.push_back(pts.back());
			else out.back() = pts.back();
			return out;
		}

		float PolyLength(const std::vector<glm::vec3>& p)
		{
			float l = 0.0f;
			for (size_t k = 1; k < p.size(); ++k) l += glm::distance(glm::vec2(p[k].x, p[k].z), glm::vec2(p[k - 1].x, p[k - 1].z));
			return l;
		}

		float NearestOnTrails(const std::vector<Trail>& trails, const glm::vec2& p)
		{
			float best = kInf;
			for (const Trail& t : trails)
				for (const glm::vec3& q : t.Points) best = std::min(best, glm::distance(p, glm::vec2(q.x, q.z)));
			return best;
		}
	}

	static RouteResult PlanRoutesOnce(const TgField& height, const WaterField* water, const std::vector<SettlementSite>& sites, const RouteParams& P)
	{
		RouteResult out;
		const uint32_t n = height.N;
		const size_t count = height.Values.size();
		out.Path.assign(count, 0.0f);
		if (n < 3) return out;
		const float sp = height.Spacing();
		const WaterRaster wet = WetOf(height, water);

		// the routing grid: every F-th sample
		RouteGrid g;
		g.F = std::max(1, static_cast<int>(std::lround(P.Cell / sp)));
		g.M = (n - 1) / static_cast<uint32_t>(g.F) + 1;
		g.Cell = sp * static_cast<float>(g.F);
		g.Min = height.Min();
		out.RouteN = g.M;
		const size_t mc = static_cast<size_t>(g.M) * g.M;
		g.H.resize(mc);
		g.Kind.resize(mc);
		g.FordZone.assign(mc, 0);
		for (uint32_t j = 0; j < g.M; ++j)
			for (uint32_t i = 0; i < g.M; ++i)
			{
				const size_t fine = static_cast<size_t>(j * static_cast<uint32_t>(g.F)) * n + i * static_cast<uint32_t>(g.F);
				g.H[static_cast<size_t>(j) * g.M + i] = height.Values[fine];
				g.Kind[static_cast<size_t>(j) * g.M + i] = wet.Kinds[fine];
			}
		g.Side = SlopeTan(g.H, g.M, g.Cell);

		// fords: the shallowest sections of the river, FordSpacing apart (the river's own depth: river_carve's bed under its level)
		const RiverData* river = water ? water->River() : nullptr;
		if (river && river->Points.size() >= 3)
		{
			const std::vector<glm::vec3>& R = river->Points;
			struct Section { float Depth; size_t K; float Hw; };
			std::vector<Section> sections;
			for (size_t k = 1; k + 1 < R.size(); ++k)
			{
				const glm::vec2 at(R[k].x, R[k].z);
				if (water->LakeLevelAt(at.x, at.y) != kNoWaterLevel) continue;
				const float hw = river->HalfWidthAt(k, 0.0f);
				glm::vec2 dir(R[k + 1].x - R[k - 1].x, R[k + 1].z - R[k - 1].z);
				dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec2(1.0f, 0.0f);
				const glm::vec2 side(-dir.y, dir.x);
				float bed = kInf;
				for (float t = -1.5f * hw; t <= 1.5f * hw + 1e-3f; t += 0.5f * sp)
					bed = std::min(bed, height.Sample(at.x + side.x * t, at.y + side.y * t));
				sections.push_back({ R[k].y - bed, k, hw });
			}
			std::stable_sort(sections.begin(), sections.end(), [](const Section& a, const Section& b) { return a.Depth != b.Depth ? a.Depth < b.Depth : a.K < b.K; });
			for (const Section& s : sections)
			{
				if (static_cast<int>(out.Fords.size()) >= P.MaxFords) break;
				if (s.Depth > P.FordDepth && !P.ShapeFords) break;
				const glm::vec2 at(R[s.K].x, R[s.K].z);
				if (std::any_of(out.Fords.begin(), out.Fords.end(), [&](const Ford& f) { return glm::distance(f.Position, at) < P.FordSpacing; })) continue;
				Ford f;
				f.Position = at;
				f.Level = R[s.K].y;
				f.Depth = s.Depth;
				f.HalfWidth = s.Hw;
				f.Built = s.Depth > P.FordDepth;
				out.Fords.push_back(f);
			}
			for (const Ford& f : out.Fords)
			{
				const float reach = 1.6f * f.HalfWidth + g.Cell;
				const size_t c0 = g.CellOf(f.Position);
				const int r = static_cast<int>(std::ceil(reach / g.Cell)) + 1, ci = static_cast<int>(c0 % g.M), cj = static_cast<int>(c0 / g.M);
				for (int dj = -r; dj <= r; ++dj)
					for (int di = -r; di <= r; ++di)
					{
						const int ii = ci + di, jj = cj + dj;
						if (ii < 0 || jj < 0 || ii >= static_cast<int>(g.M) || jj >= static_cast<int>(g.M)) continue;
						const size_t c = static_cast<size_t>(jj) * g.M + static_cast<size_t>(ii);
						if (g.Kind[c] == WaterRaster::River && glm::distance(g.Position(c), f.Position) <= reach) g.FordZone[c] = 1;
					}
			}
		}

		// one Dijkstra from every site at once: each cell's cost from the nearest site, which site, and the cell it was reached from
		const size_t S = sites.size();
		std::vector<float> dist(mc, kInf);
		std::vector<int32_t> label(mc, -1), parent(mc, -1);
		using Item = std::pair<float, uint32_t>;
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
		std::vector<size_t> source(S);
		for (size_t s = 0; s < S; ++s)
		{
			source[s] = g.CellOf(sites[s].Position);
			if (dist[source[s]] == 0.0f) continue;   // (two sites on one cell: the first keeps it)
			dist[source[s]] = 0.0f;
			label[source[s]] = static_cast<int32_t>(s);
			open.push({ 0.0f, static_cast<uint32_t>(source[s]) });
		}
		const std::vector<Move>& moves = Moves16();
		while (!open.empty())
		{
			const auto [d, c] = open.top();
			open.pop();
			if (d > dist[c]) continue;
			for (const Move& m : moves)
			{
				const float step = StepCost(g, P, c, m);
				if (step == kInf) continue;
				const size_t b = static_cast<size_t>(static_cast<int>(c / g.M) + m.Dj) * g.M + static_cast<size_t>(static_cast<int>(c % g.M) + m.Di);
				const float nd = d + step;
				if (nd < dist[b]) { dist[b] = nd; label[b] = label[c]; parent[b] = static_cast<int32_t>(c); open.push({ nd, static_cast<uint32_t>(b) }); }
			}
		}
		auto chainTo = [&](size_t c)   // the cells from c back to its site
		{
			std::vector<size_t> chain;
			for (int32_t k = static_cast<int32_t>(c); k >= 0; k = parent[static_cast<size_t>(k)]) chain.push_back(static_cast<size_t>(k));
			return chain;
		};

		// the links the regions of the sites share: the cheapest crossing of each border
		struct Link { float Cost; int A, B; size_t CA, CB; };
		std::map<std::pair<int, int>, Link> borders;
		for (size_t c = 0; c < mc; ++c)
		{
			if (label[c] < 0) continue;
			for (const Move& m : moves)
			{
				if (m.Dj < 0 || (m.Dj == 0 && m.Di < 0)) continue;
				const float step = StepCost(g, P, c, m);
				if (step == kInf) continue;
				const size_t b = static_cast<size_t>(static_cast<int>(c / g.M) + m.Dj) * g.M + static_cast<size_t>(static_cast<int>(c % g.M) + m.Di);
				if (label[b] < 0 || label[b] == label[c]) continue;
				const float cost = dist[c] + step + dist[b];
				int a = label[c], bb = label[b];
				size_t ca = c, cb = b;
				if (a > bb) { std::swap(a, bb); std::swap(ca, cb); }
				auto [it, fresh] = borders.try_emplace({ a, bb }, Link{ cost, a, bb, ca, cb });
				if (!fresh && cost < it->second.Cost) it->second = Link{ cost, a, bb, ca, cb };
			}
		}
		std::vector<Link> links;
		for (const auto& [key, l] : borders) links.push_back(l);
		std::stable_sort(links.begin(), links.end(), [](const Link& a, const Link& b) { return a.Cost != b.Cost ? a.Cost < b.Cost : (a.A != b.A ? a.A < b.A : a.B < b.B); });

		// the minimum spanning tree (Kruskal), then the extra links that save the most
		std::vector<int> root(S);
		for (size_t s = 0; s < S; ++s) root[s] = static_cast<int>(s);
		auto find = [&](int x) { while (root[static_cast<size_t>(x)] != x) x = root[static_cast<size_t>(x)] = root[static_cast<size_t>(root[static_cast<size_t>(x)])]; return x; };
		std::vector<Link> chosen;
		std::vector<TrailKind> kinds;
		std::vector<uint8_t> used(links.size(), 0);
		for (size_t k = 0; k < links.size(); ++k)
		{
			const int a = find(links[k].A), b = find(links[k].B);
			if (a == b) continue;
			root[static_cast<size_t>(a)] = b;
			chosen.push_back(links[k]);
			kinds.push_back(TrailKind::Link);
			used[k] = 1;
		}
		for (int extra = 0; extra < P.ExtraLinks; ++extra)
		{
			std::vector<float> net(S * S, kInf);
			for (size_t s = 0; s < S; ++s) net[s * S + s] = 0.0f;
			for (const Link& l : chosen) { const size_t a = static_cast<size_t>(l.A), b = static_cast<size_t>(l.B); net[a * S + b] = net[b * S + a] = std::min(net[a * S + b], l.Cost); }
			for (size_t k = 0; k < S; ++k)
				for (size_t i = 0; i < S; ++i)
					for (size_t j = 0; j < S; ++j)
						if (net[i * S + k] != kInf && net[k * S + j] != kInf) net[i * S + j] = std::min(net[i * S + j], net[i * S + k] + net[k * S + j]);
			size_t best = links.size();
			float bestRatio = P.ExtraRatio;
			for (size_t k = 0; k < links.size(); ++k)
			{
				if (used[k]) continue;
				const float way = net[static_cast<size_t>(links[k].A) * S + static_cast<size_t>(links[k].B)];
				if (way == kInf) continue;
				const float ratio = way / std::max(links[k].Cost, 1e-3f);
				if (ratio > bestRatio) { bestRatio = ratio; best = k; }
			}
			if (best == links.size()) break;
			used[best] = 1;
			chosen.push_back(links[best]);
			kinds.push_back(TrailKind::Extra);
		}
		{
			std::vector<int> roots;
			for (size_t s = 0; s < S; ++s) roots.push_back(find(static_cast<int>(s)));
			std::sort(roots.begin(), roots.end());
			out.Components = static_cast<int>(std::unique(roots.begin(), roots.end()) - roots.begin());
		}

		auto addTrail = [&](const std::vector<size_t>& cells, TrailKind kind, int from, int to, float cost)
		{
			std::vector<glm::vec2> pts;
			for (const size_t c : cells) pts.push_back(g.Position(c));
			if (from >= 0) pts.front() = sites[static_cast<size_t>(from)].Position;
			if (to >= 0) pts.back() = sites[static_cast<size_t>(to)].Position;
			Trail t;
			t.Kind = kind; t.From = from; t.To = to; t.Width = P.Width; t.Cost = cost;
			for (const glm::vec2& p : Smooth(pts, P.Spacing)) t.Points.push_back({ p.x, height.Sample(p.x, p.y), p.y });
			t.Length = PolyLength(t.Points);
			if (t.Points.size() >= 2) t.Straight = glm::distance(glm::vec2(t.Points.front().x, t.Points.front().z), glm::vec2(t.Points.back().x, t.Points.back().z));
			out.Trails.push_back(std::move(t));
		};
		for (size_t k = 0; k < chosen.size(); ++k)
		{
			std::vector<size_t> a = chainTo(chosen[k].CA);
			std::reverse(a.begin(), a.end());
			const std::vector<size_t> b = chainTo(chosen[k].CB);
			a.insert(a.end(), b.begin(), b.end());
			addTrail(a, kinds[k], chosen[k].A, chosen[k].B, chosen[k].Cost);
		}

		// spurs: each site to its nearest water
		for (size_t s = 0; s < S; ++s)
		{
			size_t best = mc;
			for (size_t c = 0; c < mc; ++c)
			{
				if (label[c] != static_cast<int32_t>(s) || g.Kind[c] != WaterRaster::Dry || dist[c] == kInf) continue;
				const int ci = static_cast<int>(c % g.M), cj = static_cast<int>(c / g.M);
				bool shore = false;
				for (int dj = -1; dj <= 1 && !shore; ++dj)
					for (int di = -1; di <= 1 && !shore; ++di)
					{
						const int ii = ci + di, jj = cj + dj;
						if (ii >= 0 && jj >= 0 && ii < static_cast<int>(g.M) && jj < static_cast<int>(g.M) && g.Kind[static_cast<size_t>(jj) * g.M + static_cast<size_t>(ii)] != WaterRaster::Dry) shore = true;
					}
				if (shore && (best == mc || dist[c] < dist[best])) best = c;
			}
			if (best == mc) continue;
			std::vector<size_t> chain = chainTo(best);
			std::reverse(chain.begin(), chain.end());
			float length = 0.0f;
			for (size_t k = 1; k < chain.size(); ++k) length += glm::distance(g.Position(chain[k]), g.Position(chain[k - 1]));
			if (length <= P.WaterSpurMax) addTrail(chain, TrailKind::Water, static_cast<int>(s), -1, dist[best]);
		}

		// cols: the lowest crossing between two big drainage basins (a basin: the cells that drain to one outlet; the outlets in one body of
		// water are one basin)
		{
			std::vector<uint8_t> outlets(mc, 0);
			for (size_t c = 0; c < mc; ++c) outlets[c] = g.Kind[c] != WaterRaster::Dry ? 1 : 0;
			DrainageOptions o;
			o.Outlets = outlets.data();
			const DrainageResult d = ComputeDrainage(g.H.data(), g.M, g.Cell, o);
			std::vector<int32_t> body(mc, -1);   // the connected body of water of each wet cell
			{
				std::vector<size_t> stack;
				for (size_t c0 = 0; c0 < mc; ++c0)
				{
					if (!outlets[c0] || body[c0] >= 0) continue;
					body[c0] = static_cast<int32_t>(c0);
					stack.assign(1, c0);
					while (!stack.empty())
					{
						const size_t c = stack.back();
						stack.pop_back();
						const int ci = static_cast<int>(c % g.M), cj = static_cast<int>(c / g.M);
						for (int dj = -1; dj <= 1; ++dj)
							for (int di = -1; di <= 1; ++di)
							{
								const int ii = ci + di, jj = cj + dj;
								if (ii < 0 || jj < 0 || ii >= static_cast<int>(g.M) || jj >= static_cast<int>(g.M)) continue;
								const size_t m = static_cast<size_t>(jj) * g.M + static_cast<size_t>(ii);
								if (outlets[m] && body[m] < 0) { body[m] = static_cast<int32_t>(c0); stack.push_back(m); }
							}
					}
				}
			}
			std::vector<int32_t> basin(mc, -1);
			for (size_t k = 0; k < mc && !d.Order.empty(); ++k)
			{
				const uint32_t c = d.Order[k];
				const int32_t r = d.Receiver[c];
				basin[c] = r < 0 ? (body[c] >= 0 ? body[c] : static_cast<int32_t>(c)) : basin[static_cast<size_t>(r)];
			}
			std::map<int32_t, size_t> area;
			for (size_t c = 0; c < mc; ++c) if (basin[c] >= 0) ++area[basin[c]];
			const size_t big = static_cast<size_t>(P.ColBasinShare * static_cast<float>(mc));
			std::map<int32_t, int> siteOf;
			for (size_t s = 0; s < S; ++s) if (basin[source[s]] >= 0) siteOf.emplace(basin[source[s]], static_cast<int>(s));
			std::map<std::pair<int32_t, int32_t>, std::pair<float, size_t>> saddles;
			for (size_t c = 0; c < mc; ++c)
			{
				const int ci = static_cast<int>(c % g.M), cj = static_cast<int>(c / g.M);
				const int nb[2][2] = { { 1, 0 }, { 0, 1 } };
				for (const auto& o2 : nb)
				{
					const int ii = ci + o2[0], jj = cj + o2[1];
					if (ii >= static_cast<int>(g.M) || jj >= static_cast<int>(g.M)) continue;
					const size_t m = static_cast<size_t>(jj) * g.M + static_cast<size_t>(ii);
					int32_t a = basin[c], b = basin[m];
					if (a < 0 || b < 0 || a == b || outlets[c] || outlets[m] || area[a] < big || area[b] < big) continue;
					if (!siteOf.count(a) && !siteOf.count(b)) continue;
					const size_t top = g.H[c] >= g.H[m] ? c : m;
					if (a > b) std::swap(a, b);
					auto [it, fresh] = saddles.try_emplace({ a, b }, std::make_pair(g.H[top], top));
					if (!fresh && (g.H[top] < it->second.first || (g.H[top] == it->second.first && top < it->second.second))) it->second = { g.H[top], top };
				}
			}
			std::vector<std::pair<std::pair<float, size_t>, std::pair<int32_t, int32_t>>> order;
			for (const auto& [key, v] : saddles) order.push_back({ v, key });
			std::sort(order.begin(), order.end());
			for (const auto& [v, key] : order)
			{
				if (static_cast<int>(out.Cols.size()) >= P.MaxCols) break;
				const size_t c = v.second;
				if (label[c] < 0) continue;
				Col col;
				col.Position = g.Position(c);
				col.Height = v.first;
				col.BasinA = key.first;
				col.BasinB = key.second;
				col.Site = label[c];
				std::vector<size_t> chain = chainTo(c);
				std::reverse(chain.begin(), chain.end());
				float length = 0.0f;
				for (size_t k = 1; k < chain.size(); ++k) length += glm::distance(g.Position(chain[k]), g.Position(chain[k - 1]));
				if (length > P.ColSpurMax) continue;
				out.Cols.push_back(col);
				if (NearestOnTrails(out.Trails, col.Position) > 30.0f) addTrail(chain, TrailKind::Col, col.Site, -1, dist[c]);
			}
		}

		// fords no trail crosses: a spur from the nearest site when it is near
		for (const Ford& f : out.Fords)
		{
			if (NearestOnTrails(out.Trails, f.Position) <= 30.0f) continue;
			const size_t c = g.CellOf(f.Position);
			if (label[c] < 0) continue;
			std::vector<size_t> chain = chainTo(c);
			std::reverse(chain.begin(), chain.end());
			float length = 0.0f;
			for (size_t k = 1; k < chain.size(); ++k) length += glm::distance(g.Position(chain[k]), g.Position(chain[k - 1]));
			if (length <= P.FordSpurMax) addTrail(chain, TrailKind::Ford, label[c], -1, dist[c]);
		}

		// which fords the trails cross
		for (Trail& t : out.Trails)
			for (Ford& f : out.Fords)
			{
				const float reach = 1.6f * f.HalfWidth + g.Cell;
				if (std::any_of(t.Points.begin(), t.Points.end(), [&](const glm::vec3& p) { return glm::distance(glm::vec2(p.x, p.z), f.Position) <= reach; })) { ++f.Used; ++t.Fords; }
			}

		// the mask
		const std::vector<TrailHit> hits = NearestTrails(height, out.Trails, 0.5f * P.Width + P.Fade + sp);
		for (size_t c = 0; c < count; ++c)
			if (hits[c].Trail >= 0) out.Path[c] = 1.0f - Smoothstep(0.5f * P.Width, 0.5f * P.Width + P.Fade, hits[c].Distance);
		return out;
	}

	RouteResult PlanRoutes(const TgField& height, const WaterField* water, const std::vector<SettlementSite>& sites, const RouteParams& P)
	{
		std::vector<SettlementSite> kept = sites;
		std::vector<int> index(sites.size());
		for (size_t s = 0; s < index.size(); ++s) index[s] = static_cast<int>(s);
		std::vector<int> dropped;
		for (;;)
		{
			RouteResult r = PlanRoutesOnce(height, water, kept, P);
			size_t worst = kept.size();
			if (P.ReachRatio > 0.0f && kept.size() > 2)
			{
				// each site's cheapest link to the network, per metre of straight line
				std::vector<float> cheapest(kept.size(), kInf);
				for (const Trail& t : r.Trails)
				{
					if ((t.Kind != TrailKind::Link && t.Kind != TrailKind::Extra) || t.From < 0 || t.To < 0) continue;
					const size_t a = static_cast<size_t>(t.From), b = static_cast<size_t>(t.To);
					const float ratio = t.Cost / std::max(glm::distance(kept[a].Position, kept[b].Position), 1.0f);
					cheapest[a] = std::min(cheapest[a], ratio);
					cheapest[b] = std::min(cheapest[b], ratio);
				}
				float worstRatio = P.ReachRatio;
				for (size_t s = 0; s < kept.size(); ++s)   // (a tie goes to the later site: the lower score)
					if (cheapest[s] != kInf && cheapest[s] > P.ReachRatio && cheapest[s] >= worstRatio) { worstRatio = cheapest[s]; worst = s; }
			}
			if (worst == kept.size())
			{
				for (size_t s = 0; s < kept.size(); ++s) kept[s].Rank = static_cast<int>(s);
				r.Sites = std::move(kept);
				r.Dropped = std::move(dropped);
				return r;
			}
			dropped.push_back(index[worst]);
			kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(worst));
			index.erase(index.begin() + static_cast<std::ptrdiff_t>(worst));
		}
	}

	// ------------------------------------------------------------------ 3. levelling
	void MeasureTrailGrade(const TgField& ground, const Trail& trail, float maxGrade, float window, float& within, float& steepest)
	{
		within = 1.0f;
		steepest = 0.0f;
		const std::vector<glm::vec3>& P = trail.Points;
		if (P.size() < 2) return;
		std::vector<float> arc(P.size(), 0.0f), z(P.size());
		for (size_t k = 0; k < P.size(); ++k)
		{
			if (k) arc[k] = arc[k - 1] + glm::distance(glm::vec2(P[k].x, P[k].z), glm::vec2(P[k - 1].x, P[k - 1].z));
			z[k] = ground.Sample(P[k].x, P[k].z);
		}
		double good = 0.0, all = 0.0;
		size_t b = 0;
		for (size_t k = 0; k + 1 < P.size(); ++k)
		{
			while (b + 1 < P.size() && arc[b] < arc[k] + window) ++b;
			if (b <= k) b = k + 1;
			const float run = std::max(arc[b] - arc[k], 1e-3f);
			const float grade = std::fabs(z[b] - z[k]) / run;
			if (run >= 0.5f * window) steepest = std::max(steepest, grade);
			const double seg = arc[k + 1] - arc[k];
			all += seg;
			if (grade <= maxGrade + 1e-3f) good += seg;
		}
		within = all > 0.0 ? static_cast<float>(good / all) : 1.0f;
	}

	GradeResult GradePaths(const TgField& height, const WaterField* water, const std::vector<SettlementSite>& sites, const std::vector<Trail>& trails,
		const std::vector<Ford>& fords, const GradeParams& P)
	{
		GradeResult out;
		const uint32_t n = height.N;
		const int N = static_cast<int>(n);
		const size_t count = height.Values.size();
		const std::vector<float>& h = height.Values;
		out.Height = h;
		out.Path.assign(count, 0.0f);
		out.Platform.assign(count, 0.0f);
		out.Sites = sites;
		out.Trails = trails;
		out.Fords = fords;
		if (n < 3) return out;
		const float sp = height.Spacing(), cell = sp * sp;
		const WaterRaster wet = WetOf(height, water);
		std::vector<uint8_t> wetSeed(count, 0);
		for (size_t c = 0; c < count; ++c) wetSeed[c] = wet.Wet(c) ? 1 : 0;
		std::vector<float> d2;
		const std::vector<int32_t> nearWet = NearestSeed(wetSeed, n, &d2);
		// by the water the ground is not cut lower than a little above the water's level (a ford's ramp comes down to it; nothing goes under it)
		auto floorAt = [&](size_t c, float current)
		{
			if (nearWet[c] < 0 || std::sqrt(d2[c]) * sp > P.NoCutNearWater) return -kInf;
			return std::min(current, wet.Level[static_cast<size_t>(nearWet[c])] + 0.3f);
		};
		// the drainage of the ground as it is: where a channel runs (culverts) and the outlets of the final fill
		DrainageOptions dopt;
		dopt.Outlets = wetSeed.data();
		const DrainageResult before = ComputeDrainage(h.data(), n, sp, dopt);
		std::vector<float>& g = out.Height;
		const float T = std::max(P.Talus, 0.05f);

		// platforms: a level disc at the median of its ground, with talus around it
		std::vector<int32_t> platformOf(count, -1);   // the disc (or its talus) each cell is in
		std::vector<uint8_t> core(count, 0);
		if (P.Platforms)
			for (size_t s = 0; s < out.Sites.size(); ++s)
			{
				SettlementSite& site = out.Sites[s];
				const float R = site.Radius;
				const float reach = R + P.TalusReach + sp;
				const int ci = static_cast<int>(std::lround((site.Position.x - height.Min().x) / sp)), cj = static_cast<int>(std::lround((site.Position.y - height.Min().y) / sp));
				const int r = static_cast<int>(std::ceil(reach / sp));
				std::vector<float> disc;
				for (int dj = -r; dj <= r; ++dj)
					for (int di = -r; di <= r; ++di)
					{
						const int ii = ci + di, jj = cj + dj;
						if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
						const size_t c = static_cast<size_t>(jj) * n + static_cast<size_t>(ii);
						if (!wet.Wet(c) && glm::distance(height.SamplePosition(static_cast<uint32_t>(ii), static_cast<uint32_t>(jj)), site.Position) <= R) disc.push_back(h[c]);
					}
				const float level = Median(std::move(disc));
				site.PlatformLevel = level;
				double cut = 0.0, fill = 0.0;
				for (int dj = -r; dj <= r; ++dj)
					for (int di = -r; di <= r; ++di)
					{
						const int ii = ci + di, jj = cj + dj;
						if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
						const size_t c = static_cast<size_t>(jj) * n + static_cast<size_t>(ii);
						if (wet.Wet(c)) continue;
						const float d = glm::distance(height.SamplePosition(static_cast<uint32_t>(ii), static_cast<uint32_t>(jj)), site.Position);
						if (d > reach) continue;
						float target = d <= R ? level : std::clamp(g[c], level - (d - R) * T, level + (d - R) * T);
						target = std::max(target, floorAt(c, g[c]));
						if (target != g[c]) platformOf[c] = static_cast<int32_t>(s);
						if (d <= R) { core[c] = 1; platformOf[c] = static_cast<int32_t>(s); }
						g[c] = target;
						out.Platform[c] = std::max(out.Platform[c], 1.0f - Smoothstep(R, R + P.Fade, d));
						cut += std::max(0.0f, h[c] - g[c]) * cell;
						fill += std::max(0.0f, g[c] - h[c]) * cell;
					}
				site.Cut = static_cast<float>(cut);
				site.Fill = static_cast<float>(fill);
			}

		// the profile of each trail: the ground along it, averaged, then the mean of the highest and the lowest profile within the grade
		const float G = std::max(P.MaxGrade, 1e-3f);
		for (Trail& t : out.Trails)
		{
			std::vector<glm::vec3>& Pt = t.Points;
			const size_t m = Pt.size();
			if (m < 2) continue;
			std::vector<float> arc(m, 0.0f), raw(m), z(m);
			std::vector<int> inside(m, -1);
			for (size_t k = 0; k < m; ++k)
			{
				if (k) arc[k] = arc[k - 1] + glm::distance(glm::vec2(Pt[k].x, Pt[k].z), glm::vec2(Pt[k - 1].x, Pt[k - 1].z));
				raw[k] = height.Sample(Pt[k].x, Pt[k].z);
				const glm::vec2 p(Pt[k].x, Pt[k].z);
				for (size_t s = 0; s < out.Sites.size() && P.Platforms; ++s)
					if (glm::distance(p, out.Sites[s].Position) <= out.Sites[s].Radius) inside[k] = static_cast<int>(s);
				if (inside[k] >= 0) raw[k] = out.Sites[static_cast<size_t>(inside[k])].PlatformLevel;
				else
				{
					// the ground after the platforms (their talus reaches out)
					const int ii = std::clamp(static_cast<int>(std::lround((p.x - height.Min().x) / sp)), 0, N - 1), jj = std::clamp(static_cast<int>(std::lround((p.y - height.Min().y) / sp)), 0, N - 1);
					const size_t c = static_cast<size_t>(jj) * n + static_cast<size_t>(ii);
					raw[k] += g[c] - h[c];
				}
			}
			// moving average over SmoothLength
			std::vector<double> prefix(m + 1, 0.0);
			for (size_t k = 0; k < m; ++k) prefix[k + 1] = prefix[k] + raw[k];
			size_t a = 0, b = 0;
			for (size_t k = 0; k < m; ++k)
			{
				while (arc[k] - arc[a] > 0.5f * P.SmoothLength) ++a;
				while (b + 1 < m && arc[b + 1] - arc[k] <= 0.5f * P.SmoothLength) ++b;
				z[k] = static_cast<float>((prefix[b + 1] - prefix[a]) / static_cast<double>(b - a + 1));
				if (inside[k] >= 0) z[k] = raw[k];
			}
			std::vector<float> lower = z, upper = z;
			for (size_t k = 1; k < m; ++k)
			{
				const float run = G * (arc[k] - arc[k - 1]);
				lower[k] = std::min(lower[k], lower[k - 1] + run);
				upper[k] = std::max(upper[k], upper[k - 1] - run);
			}
			for (size_t k = m - 1; k-- > 0;)
			{
				const float run = G * (arc[k + 1] - arc[k]);
				lower[k] = std::min(lower[k], lower[k + 1] + run);
				upper[k] = std::max(upper[k], upper[k + 1] - run);
			}
			for (size_t k = 0; k < m; ++k)
			{
				float zk = inside[k] >= 0 ? raw[k] : 0.5f * (lower[k] + upper[k]);
				zk = std::clamp(zk, raw[k] - P.MaxCut, raw[k] + P.MaxFill);
				const glm::vec2 p(Pt[k].x, Pt[k].z);
				const int ii = std::clamp(static_cast<int>(std::lround((p.x - height.Min().x) / sp)), 0, N - 1), jj = std::clamp(static_cast<int>(std::lround((p.y - height.Min().y) / sp)), 0, N - 1);
				const size_t c = static_cast<size_t>(jj) * n + static_cast<size_t>(ii);
				if (wet.Wet(c)) zk = raw[k];
				else if (floorAt(c, raw[k]) > -kInf) zk = std::max(zk, floorAt(c, raw[k]));
				else if (before.Area.size() == count && before.Area[c] >= P.CulvertArea && zk > raw[k]) zk = raw[k];
				Pt[k].y = zk;
			}
		}

		// the ground around each trail: its level on the tread, its talus out to the ground
		const float reach = 0.5f * P.Width + P.TalusReach + sp;
		const std::vector<TrailHit> hits = NearestTrails(height, out.Trails, reach);
		std::vector<float> next = g;
		std::vector<double> cut(out.Trails.size(), 0.0), fill(out.Trails.size(), 0.0);
		for (size_t c = 0; c < count; ++c)
		{
			const TrailHit& hit = hits[c];
			if (hit.Trail < 0) continue;
			out.Path[c] = 1.0f - Smoothstep(0.5f * P.Width, 0.5f * P.Width + P.Fade, hit.Distance);
			if (wet.Wet(c) || core[c]) continue;
			const std::vector<glm::vec3>& Pt = out.Trails[static_cast<size_t>(hit.Trail)].Points;
			const float z = glm::mix(Pt[hit.Segment].y, Pt[hit.Segment + 1].y, hit.T);
			const float d = hit.Distance - 0.5f * P.Width;
			float target = d <= 0.0f ? z : std::clamp(g[c], z - d * T, z + d * T);
			target = std::max(target, floorAt(c, g[c]));
			if (before.Area.size() == count && before.Area[c] >= P.CulvertArea) target = std::min(target, std::max(g[c], h[c]));
			next[c] = target;
			const float dz = target - h[c];
			if (target != g[c]) { cut[static_cast<size_t>(hit.Trail)] += std::max(0.0f, -dz) * cell; fill[static_cast<size_t>(hit.Trail)] += std::max(0.0f, dz) * cell; }
		}
		g = std::move(next);
		for (size_t t = 0; t < out.Trails.size(); ++t) { out.Trails[t].Cut = static_cast<float>(cut[t]); out.Trails[t].Fill = static_cast<float>(fill[t]); }

		// built fords: the bed of a crossing too deep to wade is raised to FordDepth under the water
		for (Ford& f : out.Fords)
		{
			if (!f.Built || f.Used == 0) { f.Built = false; continue; }
			const float zone = 1.6f * f.HalfWidth + 0.5f * P.Width + sp;
			for (size_t c = 0; c < count; ++c)
			{
				if (wet.Kinds[c] != WaterRaster::River || hits[c].Trail < 0 || hits[c].Distance > 0.5f * P.Width + P.Fade) continue;
				const glm::vec2 p = height.SamplePosition(static_cast<uint32_t>(c % n), static_cast<uint32_t>(c / n));
				if (glm::distance(p, f.Position) > zone) continue;
				g[c] = std::max(g[c], wet.Level[c] - P.FordDepth);
			}
		}

		// no pit. First the culverts: walking the ground's own drainage upstream first, a cell the levelling left no lower than the one above
		// it drains to is lowered below it, but never below its own ground (an embankment notched where the water crossed it, down to where
		// it was). Then what is left (a cut that closed a hollow) is filled, an epsilon per cell; the water is the outlet.
		if (before.Order.size() == count)
			for (size_t k = count; k-- > 0;)
			{
				const uint32_t c = before.Order[k];
				const int32_t r = before.Receiver[c];
				if (r < 0 || wet.Wet(c) || wet.Wet(static_cast<size_t>(r))) continue;
				float& below = g[static_cast<size_t>(r)];
				if (below < g[c]) continue;
				const float notch = std::nextafter(g[c], -kInf) - 0.001f;
				below = std::max(std::min(below, notch), std::min(below, h[static_cast<size_t>(r)]));
			}
		// then the ground filled (an epsilon per cell) where the levelling closed a hollow; the water is the outlet
		const DrainageResult after = ComputeDrainage(g.data(), n, sp, dopt);
		out.DrainFill.assign(count, 0.0f);
		for (size_t c = 0; c < count; ++c)
		{
			if (wet.Wet(c) || after.Filled.size() != count) continue;
			const float up = after.Filled[c] - g[c];
			if (up <= 0.0f) continue;
			if (up > out.DrainRaise) { out.DrainRaise = up; out.DrainRaiseAt = height.SamplePosition(static_cast<uint32_t>(c % n), static_cast<uint32_t>(c / n)); }
			out.DrainRaised += up > 0.05f;
			out.DrainVolume += static_cast<double>(up) * cell;
			out.DrainFill[c] = up;
			g[c] = after.Filled[c];
		}

		TgField levelled;
		levelled.N = n; levelled.Size = height.Size; levelled.Center = height.Center; levelled.Values = g;
		for (Trail& t : out.Trails)
		{
			for (glm::vec3& p : t.Points) p.y = levelled.Sample(p.x, p.z);
			MeasureTrailGrade(levelled, t, P.MaxGrade, 9.0f, t.WithinGrade, t.MaxGrade);
		}
		return out;
	}
}
