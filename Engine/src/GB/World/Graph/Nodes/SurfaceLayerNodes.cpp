#include "GB/World/Graph/Nodes/SurfaceLayerNodes.h"
#include "GB/World/Biome.h"
#include "GB/World/Graph/Nodes/FieldMath.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Water/WaterRaster.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace GB
{
	namespace
	{
		constexpr uint32_t kLayer = 0x8C6E46, kTree = 0x3C7832;

		std::string Who(const TgNode& node) { return node.Type + " node '" + node.Id + "'"; }

		const TgValue* Input(const std::vector<const TgValue*>& inputs, size_t slot, TgPortKind kind)
		{
			return inputs.size() > slot && inputs[slot] && inputs[slot]->Kind == kind ? inputs[slot] : nullptr;
		}

		bool CheckRevision(const TgNode& node, std::string& error)
		{
			const double revision = TgParam(node, "Revision", 1.0);
			if (revision == 1.0) return true;
			error = Who(node) + ": Revision " + std::to_string(revision) + " is unknown (this engine has 1)";
			return false;
		}

		// The field at `slot` when it is connected; an error when it is not the size of `like`.
		bool SameSize(const TgValue* v, const TgField& like, const TgNode& node, const char* port, std::string& error)
		{
			if (!v || v->Field.N == like.N) return true;
			error = Who(node) + ": input '" + port + "' is not the size of the height field";
			return false;
		}

		TgField& FieldOut(std::map<std::string, TgValue>& outputs, const std::string& name, const TgField& like)
		{
			TgValue& v = outputs[name];
			v.Kind = TgPortKind::Field;
			v.Field.Resize(like.N, like.Size, like.Center);
			return v.Field;
		}

		float Param(const TgNode& node, const char* key, double fallback) { return static_cast<float>(TgParam(node, key, fallback)); }
		float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
		// 1 up to half of `reach`, falling to 0 at `reach` (the edge of a band measured from something)
		float Near(float d, float reach) { return reach <= 0.0f ? 0.0f : 1.0f - FieldMath::Smoothstep(0.5f * reach, reach, d); }
		// 1 under `deg` - 4, 0 over `deg` + 4
		float Gentle(float slope, float deg) { return 1.0f - FieldMath::Smoothstep(deg - 4.0f, deg + 4.0f, slope); }

		std::vector<float> Slopes(const TgField& h)
		{
			std::vector<float> s(h.Values.size());
			for (uint32_t j = 0; j < h.N; ++j)
				for (uint32_t i = 0; i < h.N; ++i) s[static_cast<size_t>(j) * h.N + i] = FieldMath::SlopeDegrees(h, i, j);
			return s;
		}

		// Exact Euclidean distance transform of a set of seed cells (Felzenszwalb and Huttenlocher, 2012), with the nearest seed of every cell.
		// D2 is in cells squared (kFar where there is no seed at all); Nearest is the flat index of the seed (-1 without one). Ties go to the
		// lower index along each pass, so it is deterministic.
		struct Distance
		{
			std::vector<float> D2;
			std::vector<int> Nearest;
			float Metres(size_t c, float spacing) const { return Nearest[c] < 0 ? 1e9f : std::sqrt(D2[c]) * spacing; }
		};
		constexpr double kFar = 1e30;

		void Envelope(const double* f, int n, double* out, int* arg, std::vector<int>& v, std::vector<double>& z)
		{
			int k = -1;
			for (int q = 0; q < n; ++q)
			{
				if (f[q] >= kFar) continue;   // (no seed on this line here: it roots no parabola)
				const double fq = f[q] + static_cast<double>(q) * q;
				while (k >= 0)
				{
					const int p = v[static_cast<size_t>(k)];
					const double s = (fq - (f[p] + static_cast<double>(p) * p)) / (2.0 * (q - p));
					if (s <= z[static_cast<size_t>(k)]) { --k; continue; }
					++k;
					v[static_cast<size_t>(k)] = q; z[static_cast<size_t>(k)] = s; z[static_cast<size_t>(k) + 1] = std::numeric_limits<double>::infinity();
					break;
				}
				if (k < 0) { k = 0; v[0] = q; z[0] = -std::numeric_limits<double>::infinity(); z[1] = std::numeric_limits<double>::infinity(); }
			}
			if (k < 0) { for (int q = 0; q < n; ++q) { out[q] = kFar; arg[q] = -1; } return; }
			k = 0;
			for (int q = 0; q < n; ++q)
			{
				while (z[static_cast<size_t>(k) + 1] < q) ++k;
				const int p = v[static_cast<size_t>(k)];
				out[q] = static_cast<double>(q - p) * (q - p) + f[p];
				arg[q] = p;
			}
		}

		Distance DistanceTo(const std::vector<uint8_t>& seeds, uint32_t n)
		{
			const int N = static_cast<int>(n);
			const size_t count = static_cast<size_t>(n) * n;
			std::vector<double> colD(count), f(n), out(n);
			std::vector<int> colRow(count), arg(n), v(n);
			std::vector<double> z(static_cast<size_t>(n) + 1);
			for (int i = 0; i < N; ++i)   // down every column: the nearest seed row
			{
				for (int j = 0; j < N; ++j) f[static_cast<size_t>(j)] = seeds[static_cast<size_t>(j) * n + static_cast<size_t>(i)] ? 0.0 : kFar;
				Envelope(f.data(), N, out.data(), arg.data(), v, z);
				for (int j = 0; j < N; ++j) { colD[static_cast<size_t>(j) * n + static_cast<size_t>(i)] = out[static_cast<size_t>(j)]; colRow[static_cast<size_t>(j) * n + static_cast<size_t>(i)] = arg[static_cast<size_t>(j)]; }
			}
			Distance d;
			d.D2.assign(count, static_cast<float>(kFar));
			d.Nearest.assign(count, -1);
			for (int j = 0; j < N; ++j)   // along every row, over the columns' distances
			{
				const size_t row = static_cast<size_t>(j) * n;
				for (int i = 0; i < N; ++i) f[static_cast<size_t>(i)] = colRow[row + static_cast<size_t>(i)] < 0 ? kFar : colD[row + static_cast<size_t>(i)];
				Envelope(f.data(), N, out.data(), arg.data(), v, z);
				for (int i = 0; i < N; ++i)
				{
					const int col = arg[static_cast<size_t>(i)];
					if (col < 0) continue;
					d.D2[row + static_cast<size_t>(i)] = static_cast<float>(out[static_cast<size_t>(i)]);
					d.Nearest[row + static_cast<size_t>(i)] = colRow[row + static_cast<size_t>(col)] * N + col;
				}
			}
			return d;
		}

		// A well mixed hash of a spot (the MurmurHash3 finalizer, as the scatter's StrongHash), 0..1.
		float SpotHash(float x, float z, uint32_t seed)
		{
			const int ix = static_cast<int>(std::floor(x * 8.0f)), iz = static_cast<int>(std::floor(z * 8.0f));
			uint64_t h = (static_cast<uint64_t>(static_cast<uint32_t>(ix)) << 32 | static_cast<uint32_t>(iz)) ^ (static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ull);
			h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull; h ^= h >> 33;
			return static_cast<float>(h >> 40) / 16777216.0f;
		}
	}

	// ------------------------------------------------------------------ layer_cliff_scree
	TgNodeSchema CliffScreeLayerNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Layers";
		s.Summary = "Cliff (bare faces) and scree (broken rock at their foot) as 0..1 layers: from E4's masks when connected, else from the slope.";
		s.Color = kLayer;
		s.Inputs = { { "height", TgPortKind::Field },
			{ "cliff", TgPortKind::Field, true, "0..1: E4's cliff mask (used as it comes; unconnected: the slope around CliffDegrees)" },
			{ "scree", TgPortKind::Field, true, "0..1: E4's scree mask (unconnected: the foot of the cliffs)" },
			{ "hardness", TgPortKind::Field, true, "0..1: E4's rock hardness, passed through for the material (unconnected: 0)" } };
		s.Outputs = { { "cliff", TgPortKind::Field }, { "scree", TgPortKind::Field }, { "hardness", TgPortKind::Field } };
		s.Params = {
			{ "CliffDegrees", 50, 30, 80, false, "without a cliff input: the slope where a face is half cliff" },
			{ "CliffFalloff", 6, 0, 20, false, "degrees across the cliff's edge" },
			{ "ScreeMinDegrees", 28, 0, 60, false, "without a scree input: the gentlest scree" },
			{ "ScreeMaxDegrees", 45, 0, 80, false, "the steepest scree (above it is face)" },
			{ "ScreeFalloff", 6, 0, 20, false, "degrees across the scree band's edges" },
			{ "ScreeReach", 60, 0, 400, false, "metres from a cliff the scree reaches" },
			{ "ScreeDrop", 2, 0, 50, false, "metres below its cliff a sample must be to be scree (the foot, not the crest)" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool CliffScreeLayerNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* cliffIn = Input(inputs, 1, TgPortKind::Field);
		const TgValue* screeIn = Input(inputs, 2, TgPortKind::Field);
		const TgValue* hardIn = Input(inputs, 3, TgPortKind::Field);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		const TgField& h = height->Field;
		if (!SameSize(cliffIn, h, node, "cliff", error) || !SameSize(screeIn, h, node, "scree", error) || !SameSize(hardIn, h, node, "hardness", error))
			return false;
		const size_t count = h.Values.size();
		TgField& hard = FieldOut(outputs, "hardness", h);
		for (size_t c = 0; c < count; ++c) hard.Values[c] = hardIn ? Clamp01(hardIn->Field.Values[c]) : 0.0f;
		const std::vector<float> slope = Slopes(h);

		TgField& cliff = FieldOut(outputs, "cliff", h);
		const float cliffDeg = Param(node, "CliffDegrees", 50.0), cliffFall = Param(node, "CliffFalloff", 6.0);
		for (size_t c = 0; c < count; ++c)
			cliff.Values[c] = cliffIn ? Clamp01(cliffIn->Field.Values[c]) : FieldMath::Smoothstep(cliffDeg - 0.5f * cliffFall, cliffDeg + 0.5f * cliffFall, slope[c]);

		TgField& scree = FieldOut(outputs, "scree", h);
		if (screeIn)
		{
			for (size_t c = 0; c < count; ++c) scree.Values[c] = std::min(Clamp01(screeIn->Field.Values[c]), 1.0f - cliff.Values[c]);
			return true;
		}
		const float lo = Param(node, "ScreeMinDegrees", 28.0), hi = Param(node, "ScreeMaxDegrees", 45.0), fall = Param(node, "ScreeFalloff", 6.0);
		const float reach = Param(node, "ScreeReach", 60.0), drop = Param(node, "ScreeDrop", 2.0);
		std::vector<uint8_t> seeds(count);
		for (size_t c = 0; c < count; ++c) seeds[c] = cliff.Values[c] >= 0.5f ? 1 : 0;
		const Distance d = DistanceTo(seeds, h.N);
		const float sp = h.Spacing();
		for (size_t c = 0; c < count; ++c)
		{
			const int at = d.Nearest[c];
			if (at < 0) continue;
			const float band = FieldMath::Smoothstep(lo - 0.5f * fall, lo + 0.5f * fall, slope[c]) * (1.0f - FieldMath::Smoothstep(hi - 0.5f * fall, hi + 0.5f * fall, slope[c]));
			const float foot = FieldMath::Smoothstep(0.0f, std::max(drop, 1e-3f), h.Values[static_cast<size_t>(at)] - h.Values[c]);
			scree.Values[c] = band * foot * Near(d.Metres(c, sp), reach) * (1.0f - cliff.Values[c]);
		}
		return true;
	}

	// ------------------------------------------------------------------ layer_wet
	TgNodeSchema WetLayerNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Layers";
		s.Summary = "Gravel along the river and the channels, mud and dried mud on the shores, wet grass where it is damp: 0..1 layers from the water, the drainage area and the moisture.";
		s.Color = kLayer;
		s.Inputs = { { "height", TgPortKind::Field },
			{ "water", TgPortKind::Water, true, "every lake and the river (lake_fill's water)" },
			{ "moisture", TgPortKind::Field, true, "0..1 (the moisture node's)" },
			{ "area", TgPortKind::Field, true, "m2 of drainage through each sample (fluvial_erosion's or drainage's area)" },
			{ "alluvium", TgPortKind::Field, true, "0..1: deposit (E3), gravel where it is flat" } };
		s.Outputs = { { "gravel", TgPortKind::Field }, { "mud", TgPortKind::Field }, { "mud_dry", TgPortKind::Field }, { "grass_wet", TgPortKind::Field } };
		s.Params = {
			{ "ChannelArea", 200000, 0, 5e6, false, "m2: a sample draining more is a channel (gravel along it); 0 = only the river" },
			{ "GravelReach", 10, 0, 60, false, "metres from the river or a channel" },
			{ "GravelRise", 1.5, 0, 10, false, "metres above it" },
			{ "GravelMaxDegrees", 14, 0, 45, false, "steeper ground keeps no gravel" },
			{ "MudReach", 8, 0, 60, false, "metres from the water" },
			{ "MudRise", 0.8, 0, 5, false, "metres above the water's level" },
			{ "MudDryReach", 20, 0, 100, false, "metres from the water: the band the water left" },
			{ "MudDryRise", 2, 0, 10, false, "metres above the water's level" },
			{ "MudMaxDegrees", 18, 0, 45, false, "steeper shore keeps no mud" },
			{ "WetMoisture", 0.6, 0, 1, false, "moisture where the grass starts to be wet" },
			{ "WetFull", 0.8, 0, 1, false, "moisture where it is all wet grass" },
			{ "WetMaxDegrees", 25, 0, 60, false, "steeper ground drains: no wet grass" },
			{ "WetReach", 35, 0, 200, false, "metres from the water of the damp band" },
			{ "WetRise", 3, 0, 20, false, "metres above the water's level of the damp band" },
			{ "Beds", 1, 0, 1, true, "1: the lake beds are mud and the river bed gravel (what shows through shallow water)" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool WetLayerNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* water = Input(inputs, 1, TgPortKind::Water);
		const TgValue* moisture = Input(inputs, 2, TgPortKind::Field);
		const TgValue* area = Input(inputs, 3, TgPortKind::Field);
		const TgValue* alluvium = Input(inputs, 4, TgPortKind::Field);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		const TgField& h = height->Field;
		if (!SameSize(moisture, h, node, "moisture", error) || !SameSize(area, h, node, "area", error) || !SameSize(alluvium, h, node, "alluvium", error)) return false;
		const size_t count = h.Values.size();
		const float sp = h.Spacing();
		const std::vector<float> slope = Slopes(h);

		WaterRaster raster;
		if (water) raster = RasterizeWater(water->Water, h.N, h.Min(), sp);
		auto kind = [&](size_t c) { return raster.Kinds.empty() ? static_cast<uint8_t>(WaterRaster::Dry) : raster.Kinds[c]; };
		const float channelArea = Param(node, "ChannelArea", 200000.0);
		std::vector<uint8_t> wetSeeds(count, 0), channelSeeds(count, 0);
		for (size_t c = 0; c < count; ++c)
		{
			wetSeeds[c] = kind(c) != WaterRaster::Dry ? 1 : 0;
			channelSeeds[c] = kind(c) == WaterRaster::River || (area && channelArea > 0.0f && area->Field.Values[c] >= channelArea && kind(c) == WaterRaster::Dry) ? 1 : 0;
		}
		const Distance toWater = DistanceTo(wetSeeds, h.N), toChannel = DistanceTo(channelSeeds, h.N);
		// the level of the water (or the channel's ground) a sample is measured from
		auto baseOf = [&](int at) { const size_t a = static_cast<size_t>(at); return kind(a) != WaterRaster::Dry ? raster.Level[a] : h.Values[a]; };

		const float gReach = Param(node, "GravelReach", 10.0), gRise = Param(node, "GravelRise", 1.5), gDeg = Param(node, "GravelMaxDegrees", 14.0);
		const float mReach = Param(node, "MudReach", 8.0), mRise = Param(node, "MudRise", 0.8), mdReach = Param(node, "MudDryReach", 20.0), mdRise = Param(node, "MudDryRise", 2.0);
		const float mDeg = Param(node, "MudMaxDegrees", 18.0);
		const float wLo = Param(node, "WetMoisture", 0.6), wHi = Param(node, "WetFull", 0.8), wDeg = Param(node, "WetMaxDegrees", 25.0);
		const float wReach = Param(node, "WetReach", 35.0), wRise = Param(node, "WetRise", 3.0);
		const bool beds = TgParam(node, "Beds", 1.0) >= 0.5;
		auto low = [](float rise, float top) { return top <= 0.0f ? 0.0f : 1.0f - FieldMath::Smoothstep(0.5f * top, top, rise); };

		TgField& gravel = FieldOut(outputs, "gravel", h);
		TgField& mud = FieldOut(outputs, "mud", h);
		TgField& mudDry = FieldOut(outputs, "mud_dry", h);
		TgField& grassWet = FieldOut(outputs, "grass_wet", h);
		for (size_t c = 0; c < count; ++c)
		{
			const uint8_t k = kind(c);
			if (k != WaterRaster::Dry)
			{
				if (beds) { mud.Values[c] = k == WaterRaster::Lake ? 1.0f : 0.0f; gravel.Values[c] = k == WaterRaster::River ? 1.0f : 0.0f; }
				continue;
			}
			float g = 0.0f;
			if (toChannel.Nearest[c] >= 0)
				g = Near(toChannel.Metres(c, sp), gReach) * low(h.Values[c] - baseOf(toChannel.Nearest[c]), gRise) * Gentle(slope[c], gDeg);
			if (alluvium) g = std::max(g, Clamp01(alluvium->Field.Values[c]) * Gentle(slope[c], gDeg));
			gravel.Values[c] = g;

			float m = 0.0f, md = 0.0f, band = 0.0f;
			if (toWater.Nearest[c] >= 0)
			{
				const float dw = toWater.Metres(c, sp), rise = h.Values[c] - baseOf(toWater.Nearest[c]);
				m = Near(dw, mReach) * low(rise, mRise) * Gentle(slope[c], mDeg);
				md = Near(dw, mdReach) * low(rise, mdRise) * Gentle(slope[c], mDeg) * (1.0f - m);
				band = Near(dw, wReach) * low(rise, wRise) * Gentle(slope[c], wDeg);
			}
			mud.Values[c] = m;
			mudDry.Values[c] = md;
			const float damp = moisture ? FieldMath::Smoothstep(wLo, std::max(wHi, wLo + 1e-3f), moisture->Field.Values[c]) * Gentle(slope[c], wDeg) : 0.0f;
			grassWet.Values[c] = Clamp01(std::max(damp, 0.7f * band));
		}
		return true;
	}

	// ------------------------------------------------------------------ layer_normalize
	const std::vector<std::string>& NormalizedLayerNames()
	{
		static const std::vector<std::string> names = { "snow", "path", "cliff", "scree", "rock", "gravel", "mud", "mud_dry", "grass_wet", "forest", "dry", "sand", "grass" };
		return names;
	}

	TgNodeSchema NormalizeLayersNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Layers";
		s.Summary = "Makes the material layers a partition (they sum to 1 at every sample): each laid over the ones below it, top first (snow, path, cliff, scree, rock, gravel, mud, mud_dry, grass_wet, forest, dry, sand); grass is what is left.";
		s.Color = kLayer;
		s.Inputs.push_back({ "height", TgPortKind::Field, false, "only its size" });
		const std::vector<std::string>& names = NormalizedLayerNames();
		for (size_t k = 0; k + 1 < names.size(); ++k) s.Inputs.push_back({ names[k], TgPortKind::Field, true, "0..1" });
		for (const std::string& name : names) s.Outputs.push_back({ name, TgPortKind::Field });
		s.Params = { { "Revision", 1, 1, 1, true, "behaviour version (1)" } };
		return s;
	}

	bool NormalizeLayersNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		const TgField& h = height->Field;
		const std::vector<std::string>& names = NormalizedLayerNames();
		const size_t layers = names.size() - 1;   // (grass has no input)
		std::vector<const TgValue*> in(layers, nullptr);
		for (size_t k = 0; k < layers; ++k)
		{
			in[k] = Input(inputs, k + 1, TgPortKind::Field);
			if (!SameSize(in[k], h, node, names[k].c_str(), error)) return false;
		}
		std::vector<TgField*> out;
		for (const std::string& name : names) out.push_back(&FieldOut(outputs, name, h));
		const size_t count = h.Values.size();
		for (size_t c = 0; c < count; ++c)
		{
			float left = 1.0f;   // what the layers above have not covered
			for (size_t k = 0; k < layers; ++k)
			{
				if (!in[k]) continue;
				const float w = Clamp01(in[k]->Field.Values[c]) * left;
				out[k]->Values[c] = w;
				left -= w;
			}
			out[layers]->Values[c] = std::max(left, 0.0f);
		}
		return true;
	}

	// ------------------------------------------------------------------ biome_water
	TgNodeSchema BiomeWaterNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Biome";
		s.Summary = "The biome with every lake and the river of a water field: Water where it is wet, Shore on the low band around it (biome_classify knows only the main lake).";
		s.Color = 0x4678B4;
		s.Inputs = { { "biome", TgPortKind::Field }, { "water", TgPortKind::Water }, { "height", TgPortKind::Field } };
		s.Outputs = { { "biome", TgPortKind::Field } };
		s.Params = {
			{ "ShoreReach", 6, 0, 60, false, "metres from the water" },
			{ "ShoreRise", 0.8, 0, 5, false, "metres above the water's level" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool BiomeWaterNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* biome = Input(inputs, 0, TgPortKind::Field);
		const TgValue* water = Input(inputs, 1, TgPortKind::Water);
		const TgValue* height = Input(inputs, 2, TgPortKind::Field);
		if (!biome || biome->Field.N == 0) { error = Who(node) + ": input 'biome' is not connected"; return false; }
		if (!water) { error = Who(node) + ": input 'water' is not connected"; return false; }
		if (!height || height->Field.N != biome->Field.N) { error = Who(node) + ": input 'height' is not connected or not the size of the biome"; return false; }
		const TgField& h = height->Field;
		const size_t count = h.Values.size();
		const WaterRaster raster = RasterizeWater(water->Water, h.N, h.Min(), h.Spacing());
		std::vector<uint8_t> seeds(count);
		for (size_t c = 0; c < count; ++c) seeds[c] = raster.Wet(c) ? 1 : 0;
		const Distance d = DistanceTo(seeds, h.N);
		const float reach = Param(node, "ShoreReach", 6.0), rise = Param(node, "ShoreRise", 0.8);
		TgField& out = FieldOut(outputs, "biome", h);
		out.Values = biome->Field.Values;
		for (size_t c = 0; c < count; ++c)
		{
			if (raster.Wet(c)) { out.Values[c] = static_cast<float>(Biome::Water); continue; }
			const int at = d.Nearest[c];
			if (at >= 0 && d.Metres(c, h.Spacing()) <= reach && h.Values[c] - raster.Level[static_cast<size_t>(at)] <= rise)
				out.Values[c] = static_cast<float>(Biome::Shore);
		}
		return true;
	}

	// ------------------------------------------------------------------ tree_species
	const std::vector<TreeSpeciesEntry>& TreeSpeciesDefaults()
	{
		// a temperate highland: oaks and limes low, alders and poplars by the river, pines on the slopes, spruce and dead snags up to the treeline
		static const std::vector<TreeSpeciesEntry> defaults = {
			{ "riparian", "Broadleaf/1", 0.55 }, { "riparian", "Broadleaf/2", 0.45 },
			{ "low", "Broadleaf/0", 0.55 }, { "low", "Broadleaf/1", 0.35 }, { "low", "Conifer/1", 0.10 },
			{ "mid", "Broadleaf/1", 0.30 }, { "mid", "Conifer/1", 0.45 }, { "mid", "Conifer/0", 0.25 },
			{ "high", "Conifer/0", 0.60 }, { "high", "Conifer/1", 0.32 }, { "high", "Bare", 0.08 },
			{ "top", "Conifer/0", 0.75 }, { "top", "Bare", 0.25 },
		};
		return defaults;
	}

	std::vector<TreeSpeciesEntry> TreeSpeciesEntries(const TgNode& node)
	{
		static const std::string prefix = "Tree:";
		std::vector<TreeSpeciesEntry> entries;
		for (const auto& [name, weight] : node.Params)
		{
			if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) continue;
			const size_t colon = name.find(':', prefix.size());
			if (colon == std::string::npos || colon + 1 >= name.size()) continue;
			if (weight > 0.0) entries.push_back({ name.substr(prefix.size(), colon - prefix.size()), name.substr(colon + 1), weight });
		}
		bool any = false;
		for (const auto& entry : node.Params) any = any || entry.first.compare(0, prefix.size(), prefix) == 0;
		return any ? entries : TreeSpeciesDefaults();
	}

	std::vector<std::string> TreeSpeciesPalette(const TgNode& node)
	{
		std::set<std::string> kinds;
		for (const TreeSpeciesEntry& e : TreeSpeciesEntries(node)) kinds.insert(e.Species);
		return { kinds.begin(), kinds.end() };
	}

	int TreeSpeciesVariety(const TgNode& node) { return std::clamp(static_cast<int>(TgParam(node, "Variety", 2.0)), 1, 8); }

	TgNodeSchema TreeSpeciesNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Scatter";
		s.Summary = "Gives every tree its species by height band (\"Tree:<band>:<Species>/<Style>\" weights; bands riparian, low, mid, high, top) and takes out the ones in the water or on a cliff, scree or path.";
		s.Color = kTree;
		s.Inputs = { { "placements", TgPortKind::Placements }, { "height", TgPortKind::Field },
			{ "water", TgPortKind::Water, true, "every lake and the river: no tree within Margin cells of a wet sample" },
			{ "cliff", TgPortKind::Field, true, "0..1: no tree where it is above ExcludeAbove" },
			{ "scree", TgPortKind::Field, true, "0..1" }, { "path", TgPortKind::Field, true, "0..1" },
			{ "exclude", TgPortKind::Field, true, "0..1: anything else that keeps trees out" },
			{ "riparian", TgPortKind::Field, true, "0..1: the river's woods (the riparian band where it is at least RiparianAbove)" } };
		s.Outputs = { { "placements", TgPortKind::Placements } };
		s.Params = {
			{ "LowTop", 0.22, 0, 1, false, "top of the low band (relative height, or metres with AbsoluteHeights)" },
			{ "MidTop", 0.34, 0, 1, false, "top of the mid band" },
			{ "HighTop", 0.44, 0, 1, false, "top of the high band (above it: top)" },
			{ "AbsoluteHeights", 0, 0, 1, true, "1: the band tops are metres, not a fraction of the height's range" },
			{ "BandBlend", 0.03, 0, 0.2, false, "how far each band's edge is dithered (same unit as the tops)" },
			{ "RiparianAbove", 0.5, 0, 1, false, "" },
			{ "ExcludeAbove", 0.25, 0, 1, false, "a cliff, scree, path or exclude sample above this keeps trees off" },
			{ "Margin", 1, 0, 4, true, "cells around a tree's cell that must be dry and clear too" },
			{ "Variety", 2, 1, 8, true, "recipes (seeds) per species" },
			{ "Seed", 0, 0, 100, true, "mixed with the world's seed" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool TreeSpeciesNode::Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* placements = Input(inputs, 0, TgPortKind::Placements);
		const TgValue* height = Input(inputs, 1, TgPortKind::Field);
		const TgValue* water = Input(inputs, 2, TgPortKind::Water);
		if (!placements) { error = Who(node) + ": input 'placements' is not connected"; return false; }
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		const TgField& h = height->Field;
		const char* maskNames[] = { "cliff", "scree", "path", "exclude" };
		std::vector<const TgField*> masks;
		for (size_t k = 0; k < 4; ++k)
		{
			const TgValue* v = Input(inputs, 3 + k, TgPortKind::Field);
			if (!SameSize(v, h, node, maskNames[k], error)) return false;
			if (v) masks.push_back(&v->Field);
		}
		const TgValue* riparian = Input(inputs, 7, TgPortKind::Field);
		if (!SameSize(riparian, h, node, "riparian", error)) return false;

		const std::vector<TreeSpeciesEntry> entries = TreeSpeciesEntries(node);
		const std::vector<std::string> palette = TreeSpeciesPalette(node);
		const int variety = TreeSpeciesVariety(node);
		const float excludeAbove = Param(node, "ExcludeAbove", 0.25);
		const int margin = std::clamp(static_cast<int>(TgParam(node, "Margin", 1.0)), 0, 4);
		const bool absolute = TgParam(node, "AbsoluteHeights", 0.0) >= 0.5;
		const float lowTop = Param(node, "LowTop", 0.22), midTop = Param(node, "MidTop", 0.34), highTop = Param(node, "HighTop", 0.44);
		const float blend = Param(node, "BandBlend", 0.03), riparianAbove = Param(node, "RiparianAbove", 0.5);
		const uint32_t seed = ctx.Seed * 0x85EBCA6Bu + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 0x27D4EB2Fu + 0x165667B1u;
		const auto [minIt, maxIt] = std::minmax_element(h.Values.begin(), h.Values.end());
		const float hMin = *minIt, hRange = std::max(*maxIt - *minIt, 1e-3f);

		WaterRaster raster;
		if (water) raster = RasterizeWater(water->Water, h.N, h.Min(), h.Spacing());
		const int n = static_cast<int>(h.N);
		const float sp = h.Spacing();
		const glm::vec2 lo = h.Min();
		auto blocked = [&](float x, float z)
		{
			const int i0 = static_cast<int>(std::floor((x - lo.x) / sp)), j0 = static_cast<int>(std::floor((z - lo.y) / sp));
			for (int j = j0 - margin; j <= j0 + 1 + margin; ++j)
				for (int i = i0 - margin; i <= i0 + 1 + margin; ++i)
				{
					const size_t c = static_cast<size_t>(std::clamp(j, 0, n - 1)) * h.N + static_cast<size_t>(std::clamp(i, 0, n - 1));
					if (!raster.Kinds.empty() && raster.Wet(c)) return true;
					for (const TgField* m : masks) if (m->Values[c] > excludeAbove) return true;
				}
			return false;
		};

		TgValue& out = outputs["placements"];
		out.Kind = TgPortKind::Placements;
		out.Placements.Items.clear();
		out.Placements.Items.reserve(placements->Placements.Items.size());
		std::vector<const TreeSpeciesEntry*> band;
		for (const TgPlacement& p : placements->Placements.Items)
		{
			const float x = p.Position.x, z = p.Position.y;
			if (blocked(x, z)) continue;
			const float ground = h.Sample(x, z);
			float t = (absolute ? ground : (ground - hMin) / hRange) + (SpotHash(x, z, seed) - 0.5f) * 2.0f * blend;
			const char* name = t < lowTop ? "low" : t < midTop ? "mid" : t < highTop ? "high" : "top";
			if (riparian && riparian->Field.Sample(x, z) >= riparianAbove && (t < midTop)) name = "riparian";
			band.clear();
			double total = 0.0;
			for (const TreeSpeciesEntry& e : entries) if (e.Band == name) { band.push_back(&e); total += e.Weight; }
			if (band.empty() && std::string(name) == "riparian")   // (no riparian entry: the band of its height)
			{
				name = t < lowTop ? "low" : "mid";
				for (const TreeSpeciesEntry& e : entries) if (e.Band == name) { band.push_back(&e); total += e.Weight; }
			}
			if (band.empty()) continue;
			double r = static_cast<double>(SpotHash(x, z, seed ^ 0x9E3779B9u)) * total;
			const TreeSpeciesEntry* pick = band.back();
			for (const TreeSpeciesEntry* e : band) { r -= e->Weight; if (r < 0.0) { pick = e; break; } }
			const int kind = static_cast<int>(std::lower_bound(palette.begin(), palette.end(), pick->Species) - palette.begin());
			const int which = std::min(static_cast<int>(SpotHash(x, z, seed ^ 0x51ED270Bu) * static_cast<float>(variety)), variety - 1);
			TgPlacement q = p;
			q.Variant = kind * variety + which;
			out.Placements.Items.push_back(q);
		}
		return true;
	}
}
