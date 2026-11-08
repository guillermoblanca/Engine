#include "GB/World/Graph/Nodes/ValleyNodes.h"
#include "GB/World/Graph/Nodes/FieldMath.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/TerrainNoise.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GB
{
	using namespace FieldMath;

	namespace
	{
		constexpr uint32_t kValley = 0x4F7A9A;

		std::string Who(const TgNode& node) { return node.Type + " node '" + node.Id + "'"; }

		const TgValue* Input(const std::vector<const TgValue*>& inputs, size_t slot, TgPortKind kind)
		{
			return inputs.size() > slot && inputs[slot] && inputs[slot]->Kind == kind ? inputs[slot] : nullptr;
		}

		// A point on a course, close enough to the next that a cell never falls between two of them.
		struct Seed { glm::vec2 Position; float Along = 0.0f; float Bed = 0.0f; };

		// The paths cut into seeds every half a cell; `bed` (optional) gives each path point a value carried to the seeds by interpolation.
		std::vector<Seed> SeedsOf(const TgPaths& paths, float step, const std::vector<std::vector<float>>* bed)
		{
			std::vector<Seed> seeds;
			for (size_t p = 0; p < paths.Items.size(); ++p)
			{
				const TgPath& path = paths.Items[p];
				float along = 0.0f;
				for (size_t k = 0; k < path.Points.size(); ++k)
				{
					const float b0 = bed ? (*bed)[p][k] : 0.0f;
					if (k + 1 == path.Points.size()) { seeds.push_back({ path.Points[k], along, b0 }); break; }
					const glm::vec2 a = path.Points[k], c = path.Points[k + 1];
					const float b1 = bed ? (*bed)[p][k + 1] : 0.0f;
					const float length = glm::distance(a, c);
					const int steps = std::max(1, static_cast<int>(std::ceil(length / step)));
					for (int s = 0; s < steps; ++s)
					{
						const float t = static_cast<float>(s) / static_cast<float>(steps);
						seeds.push_back({ glm::mix(a, c, t), along + t * length, glm::mix(b0, b1, t) });
					}
					along += length;
				}
			}
			return seeds;
		}

		// The nearest seed of every cell of a field's grid: the seeds are dropped in their cells, then two sweeps hand each cell the nearest seed
		// of its already-visited neighbours (a dead-reckoning distance transform, near-exact and linear in the cells). -1 where there is none.
		std::vector<int> NearestSeeds(const TgField& grid, const std::vector<Seed>& seeds)
		{
			const int n = static_cast<int>(grid.N);
			std::vector<int> nearest(static_cast<size_t>(n) * n, -1);
			std::vector<float> best(nearest.size(), std::numeric_limits<float>::max());
			const glm::vec2 lo = grid.Min();
			const float spacing = grid.Spacing();
			auto distance2 = [&](int i, int j, int s) { const glm::vec2 d = grid.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j)) - seeds[static_cast<size_t>(s)].Position; return glm::dot(d, d); };
			for (size_t s = 0; s < seeds.size(); ++s)
			{
				const int i = std::clamp(static_cast<int>(std::lround((seeds[s].Position.x - lo.x) / spacing)), 0, n - 1);
				const int j = std::clamp(static_cast<int>(std::lround((seeds[s].Position.y - lo.y) / spacing)), 0, n - 1);
				const float d = distance2(i, j, static_cast<int>(s));
				const size_t c = static_cast<size_t>(j) * n + i;
				if (d < best[c]) { best[c] = d; nearest[c] = static_cast<int>(s); }
			}
			auto offer = [&](int i, int j, int ni, int nj)
			{
				if (ni < 0 || nj < 0 || ni >= n || nj >= n) return;
				const int s = nearest[static_cast<size_t>(nj) * n + ni];
				if (s < 0) return;
				const size_t c = static_cast<size_t>(j) * n + i;
				const float d = distance2(i, j, s);
				if (d < best[c]) { best[c] = d; nearest[c] = s; }
			};
			for (int j = 0; j < n; ++j)
			{
				for (int i = 0; i < n; ++i) { offer(i, j, i - 1, j); offer(i, j, i - 1, j - 1); offer(i, j, i, j - 1); offer(i, j, i + 1, j - 1); }
				for (int i = n - 1; i >= 0; --i) offer(i, j, i + 1, j);
			}
			for (int j = n - 1; j >= 0; --j)
			{
				for (int i = n - 1; i >= 0; --i) { offer(i, j, i + 1, j); offer(i, j, i + 1, j + 1); offer(i, j, i, j + 1); offer(i, j, i - 1, j + 1); }
				for (int i = 0; i < n; ++i) offer(i, j, i - 1, j);
			}
			return nearest;
		}

		glm::vec2 CatmullRom(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, float t)
		{
			const float t2 = t * t, t3 = t2 * t;
			return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
		}

		// Points every `spacing` metres along a polyline (the last one kept).
		std::vector<glm::vec2> Resample(const std::vector<glm::vec2>& line, float spacing)
		{
			std::vector<glm::vec2> out;
			if (line.empty()) return out;
			out.push_back(line[0]);
			float carry = 0.0f;
			for (size_t k = 1; k < line.size(); ++k)
			{
				const glm::vec2 a = line[k - 1], b = line[k];
				const float length = glm::distance(a, b);
				float at = spacing - carry;
				while (at <= length) { out.push_back(glm::mix(a, b, at / std::max(length, 1e-6f))); at += spacing; }
				carry = length - (at - spacing);
			}
			if (glm::distance(out.back(), line.back()) > spacing * 0.25f) out.push_back(line.back());
			return out;
		}
	}

	// ------------------------------------------------------------------ path_curve
	TgNodeSchema PathCurveNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Path";
		s.Summary = "A smooth curve through up to six control points, with a meander: the course of a valley, a road, a river laid by hand.";
		s.Color = kValley;
		s.Inputs = {};
		s.Outputs = { { "path", TgPortKind::Paths } };
		s.Params = {
			{ "Points", 3, 2, 6, true, "control points used (P0 first: a valley runs from it, downhill)" },
			{ "P0X", -300, -5000, 5000, false, "metres" }, { "P0Z", -300, -5000, 5000, false, "metres" },
			{ "P1X", 0, -5000, 5000, false, "metres" }, { "P1Z", 0, -5000, 5000, false, "metres" },
			{ "P2X", 300, -5000, 5000, false, "metres" }, { "P2Z", 300, -5000, 5000, false, "metres" },
			{ "P3X", 0, -5000, 5000, false, "metres" }, { "P3Z", 0, -5000, 5000, false, "metres" },
			{ "P4X", 0, -5000, 5000, false, "metres" }, { "P4Z", 0, -5000, 5000, false, "metres" },
			{ "P5X", 0, -5000, 5000, false, "metres" }, { "P5Z", 0, -5000, 5000, false, "metres" },
			{ "Amplitude", 0, 0, 500, false, "metres the curve swings to either side" },
			{ "Wavelength", 400, 10, 5000, false, "metres between two swings" },
			{ "Spacing", 8, 1, 100, false, "metres between the path's points" },
			{ "Seed", 0, 0, 100, true, "the meander's phase, mixed with the world's seed" },
		};
		return s;
	}

	bool PathCurveNode::Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>&, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		const int count = std::clamp(static_cast<int>(TgParam(node, "Points", 3.0)), 2, 6);
		std::vector<glm::vec2> control;
		for (int k = 0; k < count; ++k)
		{
			const std::string i = std::to_string(k);
			control.push_back({ static_cast<float>(TgParam(node, ("P" + i + "X").c_str(), 0.0)), static_cast<float>(TgParam(node, ("P" + i + "Z").c_str(), 0.0)) });
		}
		const float spacing = std::max(static_cast<float>(TgParam(node, "Spacing", 8.0)), 0.5f);
		const float amplitude = static_cast<float>(TgParam(node, "Amplitude", 0.0)), wavelength = std::max(static_cast<float>(TgParam(node, "Wavelength", 400.0)), 1.0f);
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 131u;

		// the spline, finely, then evenly spaced
		std::vector<glm::vec2> fine;
		for (int k = 0; k + 1 < count; ++k)
		{
			const glm::vec2 p0 = control[static_cast<size_t>(std::max(k - 1, 0))], p1 = control[static_cast<size_t>(k)], p2 = control[static_cast<size_t>(k + 1)];
			const glm::vec2 p3 = control[static_cast<size_t>(std::min(k + 2, count - 1))];
			const int steps = std::max(8, static_cast<int>(glm::distance(p1, p2) / 1.0f));
			for (int s = 0; s < steps; ++s) fine.push_back(CatmullRom(p0, p1, p2, p3, static_cast<float>(s) / static_cast<float>(steps)));
		}
		fine.push_back(control.back());
		std::vector<glm::vec2> line = Resample(fine, std::min(spacing, 2.0f));
		if (line.size() < 2) { error = Who(node) + ": its control points are all in one place"; return false; }

		// the meander: a swing to the side, irregular in length and size, still at both ends
		if (amplitude > 0.0f)
		{
			float total = 0.0f;
			for (size_t k = 1; k < line.size(); ++k) total += glm::distance(line[k - 1], line[k]);
			std::vector<glm::vec2> bent(line.size());
			float along = 0.0f;
			const float phase = static_cast<float>(TerrainNoise::Hash(1, 2, seed)) * 6.2831853f;
			for (size_t k = 0; k < line.size(); ++k)
			{
				if (k > 0) along += glm::distance(line[k - 1], line[k]);
				const glm::vec2 tangent = glm::normalize(line[std::min(k + 1, line.size() - 1)] - line[k > 0 ? k - 1 : 0] + glm::vec2(1e-6f));
				const glm::vec2 side(-tangent.y, tangent.x);
				const float wobble = (TerrainNoise::ValueNoise(along / wavelength * 1.7f, 0.5f, seed + 7u) - 0.5f) * 2.0f;
				const float taper = Smoothstep(0.0f, 0.15f, along / total) * Smoothstep(0.0f, 0.15f, 1.0f - along / total);
				bent[k] = line[k] + side * amplitude * taper * (0.75f * std::sin(along / wavelength * 6.2831853f + phase) + 0.45f * wobble);
			}
			line = std::move(bent);
		}
		line = Resample(line, spacing);

		TgValue& out = outputs["path"];
		out.Kind = TgPortKind::Paths;
		TgPath path;
		float along = 0.0f;
		for (size_t k = 0; k < line.size(); ++k)
		{
			if (k > 0) along += glm::distance(line[k - 1], line[k]);
			path.Points.push_back(line[k]);
			path.Values.push_back(along);
		}
		out.Paths.Items.push_back(std::move(path));
		return true;
	}

	// ------------------------------------------------------------------ valley_carve
	TgNodeSchema ValleyCarveNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Path";
		s.Summary = "Digs a valley along a course: a flat floor that runs downhill, walls rising to the ground as a U (or a V).";
		s.Color = kValley;
		s.Inputs = { { "height", TgPortKind::Field }, { "path", TgPortKind::Paths, false, "the course, from its head (first point) down to its mouth" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "floor", TgPortKind::Field }, { "distance", TgPortKind::Field } };
		s.Params = {
			{ "FloorWidth", 120, 0, 2000, false, "metres of flat floor to either side of the course" },
			{ "WidthVariation", 0.3, 0, 1, false, "the floor narrows and widens along the course by this share" },
			{ "WallWidth", 350, 1, 3000, false, "metres over which the walls rise to the ground" },
			{ "WallGamma", 1.8, 0.3, 4, false, "> 1: a U (glacial), < 1: a V (a stream's)" },
			{ "Incision", 8, -50, 300, false, "metres the floor lies under the ground along the course" },
			{ "BedSmoothing", 300, 0, 3000, false, "metres of ground along the course averaged into the floor" },
			{ "FloorTilt", 0.012, 0, 0.2, false, "metres per metre the floor rises away from the course (its water finds the middle)" },
			{ "MaxFill", 4, 0, 100, false, "metres ground under the floor is raised at most" },
			{ "Seed", 0, 0, 100, true, "the width's variation, mixed with the world's seed" },
		};
		return s;
	}

	bool ValleyCarveNode::Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* course = Input(inputs, 1, TgPortKind::Paths);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		if (!course || course->Paths.Items.empty()) { error = Who(node) + ": input 'path' has no course"; return false; }
		const TgField& ground = height->Field;
		const float floorWidth = static_cast<float>(TgParam(node, "FloorWidth", 120.0)), variation = static_cast<float>(TgParam(node, "WidthVariation", 0.3));
		const float wallWidth = std::max(static_cast<float>(TgParam(node, "WallWidth", 350.0)), 1.0f), gamma = static_cast<float>(TgParam(node, "WallGamma", 1.8));
		const float incision = static_cast<float>(TgParam(node, "Incision", 8.0)), smoothing = static_cast<float>(TgParam(node, "BedSmoothing", 300.0));
		const float tilt = static_cast<float>(TgParam(node, "FloorTilt", 0.012));
		const float maxFill = std::max(static_cast<float>(TgParam(node, "MaxFill", 4.0)), 0.0f);
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 131u + 17u;

		// the floor along each course: the lowest ground met so far going down it (never rising downstream, never above the ground it runs
		// on), averaged over BedSmoothing, Incision below
		std::vector<std::vector<float>> bed;
		for (const TgPath& path : course->Paths.Items)
		{
			std::vector<float> raw(path.Points.size()), along(path.Points.size(), 0.0f);
			for (size_t k = 0; k < path.Points.size(); ++k)
			{
				raw[k] = ground.Sample(path.Points[k].x, path.Points[k].y);
				if (k > 0) along[k] = along[k - 1] + glm::distance(path.Points[k - 1], path.Points[k]);
			}
			for (size_t k = 1; k < raw.size(); ++k) raw[k] = std::min(raw[k], raw[k - 1]);
			std::vector<float> b(raw.size());
			size_t lo = 0, hi = 0;
			double sum = 0.0;
			for (size_t k = 0; k < raw.size(); ++k)   // a sliding window of +-smoothing/2 metres
			{
				while (hi < raw.size() && along[hi] <= along[k] + smoothing * 0.5f) sum += raw[hi++];
				while (along[lo] < along[k] - smoothing * 0.5f) sum -= raw[lo++];
				b[k] = static_cast<float>(sum / static_cast<double>(std::max<size_t>(hi - lo, 1)));
			}
			for (size_t k = 1; k < b.size(); ++k) b[k] = std::min(b[k], b[k - 1]);
			for (float& v : b) v -= incision;
			bed.push_back(std::move(b));
		}

		const std::vector<Seed> seeds = SeedsOf(course->Paths, ground.Spacing() * 0.5f, &bed);
		if (seeds.empty()) { error = Who(node) + ": input 'path' has no point"; return false; }
		const std::vector<int> nearest = NearestSeeds(ground, seeds);

		TgValue& carved = outputs["height"];
		carved.Kind = TgPortKind::Field;
		carved.Field = ground;
		TgValue& floorMask = outputs["floor"];
		floorMask.Kind = TgPortKind::Field;
		floorMask.Field.Resize(ground.N, ground.Size, ground.Center);
		TgValue& distanceOut = outputs["distance"];
		distanceOut.Kind = TgPortKind::Field;
		distanceOut.Field.Resize(ground.N, ground.Size, ground.Center);
		for (uint32_t j = 0; j < ground.N; ++j)
			for (uint32_t i = 0; i < ground.N; ++i)
			{
				const int s = nearest[static_cast<size_t>(j) * ground.N + i];
				if (s < 0) continue;
				const Seed& at = seeds[static_cast<size_t>(s)];
				const float d = glm::distance(ground.SamplePosition(i, j), at.Position);
				const float half = floorWidth * (1.0f + variation * (TerrainNoise::ValueNoise(at.Along / 450.0f, 0.5f, seed) - 0.5f) * 2.0f);
				const float h = ground.At(i, j);
				const float floorHeight = at.Bed + tilt * d;
				const float wall = d <= half ? 0.0f : std::pow(std::min((d - half) / wallWidth, 1.0f), gamma);
				// the walls rise from the floor to the ground they cut; ground below the floor is filled by MaxFill at most (a hollow on the
				// floor becomes a pond, a course across a slope does not raise an embankment)
				const float target = h >= floorHeight ? glm::mix(floorHeight, h, wall) : h + std::min(floorHeight - h, maxFill) * (1.0f - wall);
				carved.Field.At(i, j) = d <= half + wallWidth ? target : h;
				floorMask.Field.At(i, j) = 1.0f - Smoothstep(half, half + wallWidth * 0.25f, d);
				distanceOut.Field.At(i, j) = d;
			}
		return true;
	}

	// ------------------------------------------------------------------ mask_distance_to_path
	TgNodeSchema PathDistanceMaskNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Mask";
		s.Summary = "1 within Inner metres of the paths, fading to 0 at Outer (a riparian strip along a river, a verge along a road).";
		s.Color = kValley;
		s.Inputs = { { "height", TgPortKind::Field, false, "gives the footprint of the mask" }, { "path", TgPortKind::Paths } };
		s.Outputs = { { "mask", TgPortKind::Field } };
		s.Params = { { "Inner", 10, 0, 2000, false, "metres" }, { "Outer", 40, 0, 3000, false, "metres" } };
		return s;
	}

	bool PathDistanceMaskNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* paths = Input(inputs, 1, TgPortKind::Paths);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		if (!paths) { error = Who(node) + ": input 'path' is not connected"; return false; }
		const TgField& footprint = height->Field;
		const float inner = static_cast<float>(TgParam(node, "Inner", 10.0)), outer = std::max(static_cast<float>(TgParam(node, "Outer", 40.0)), inner);
		const std::vector<Seed> seeds = SeedsOf(paths->Paths, footprint.Spacing() * 0.5f, nullptr);
		const std::vector<int> nearest = NearestSeeds(footprint, seeds);
		TgValue& mask = outputs["mask"];
		mask.Kind = TgPortKind::Field;
		mask.Field.Resize(footprint.N, footprint.Size, footprint.Center);
		for (uint32_t j = 0; j < footprint.N; ++j)
			for (uint32_t i = 0; i < footprint.N; ++i)
			{
				const int s = nearest[static_cast<size_t>(j) * footprint.N + i];
				if (s < 0) continue;
				mask.Field.At(i, j) = 1.0f - Smoothstep(inner, outer, glm::distance(footprint.SamplePosition(i, j), seeds[static_cast<size_t>(s)].Position));
			}
		return true;
	}
}
