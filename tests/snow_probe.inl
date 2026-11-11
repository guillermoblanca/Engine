// The snow of the terrain graph (snow_accumulate, MaskNodes.cpp): a measuring probe, not a check. Included by terrain_graph_cache_test.cpp
// after surface_layers_probe.inl (it reuses its helpers).
//   GBSimTests terrain_graph_cache snow <graph.json|preset> <type> <seed> <size> <resolution> <out_dir>
// Evaluates the graph, takes the inputs of its snow_accumulate node and writes <tag>_snow_<variant>.f32 (float32, N*N, row major):
//   - "node": what the node gives now (timed: the node alone, five runs, the best);
//   - "old" and "old_no<term>": the node as it was before T112 (a 3x3 ring of samples Radius cells away for the concavity, D8 flow taken raw),
//     whole and with one of its terms taken out, to find the one that draws the grid's pattern;
//   - "node_no<term>": the node now, with the parameter of one of its terms set to 0.
// and <tag>_height.f32, <tag>_flow.f32 and <tag>_snow.json. tools/snow_metrics.py measures them.
#include "GB/World/Graph/Nodes/MaskNodes.h"
#include "GB/World/TerrainNoise.h"

namespace
{
	// The node before T112, with switches for each term.
	struct OldSnowTerms { bool Shift = true, Curvature = true, Flow = true, Breakup = true; };

	TgField OldSnow(const TgNode& node, uint32_t ctxSeed, const TgField& h, const TgField* flow, OldSnowTerms t)
	{
		using namespace FieldMath;
		const float full = static_cast<float>(TgParam(node, "FullSlopeDegrees", 32.0)), bare = static_cast<float>(TgParam(node, "BareSlopeDegrees", 50.0));
		const float hollowBoost = static_cast<float>(TgParam(node, "HollowBoost", 0.6)), ridgeLoss = static_cast<float>(TgParam(node, "RidgeLoss", 0.5));
		const int radius = static_cast<int>(TgParam(node, "HollowRadius", 3.0));
		const float flowBoost = static_cast<float>(TgParam(node, "FlowBoost", 0.4));
		const float minRelative = static_cast<float>(TgParam(node, "MinRelativeHeight", 0.0));
		const float breakup = static_cast<float>(TgParam(node, "Breakup", 0.25)), breakupScale = std::max(static_cast<float>(TgParam(node, "BreakupScale", 9.0)), 0.1f);
		const uint32_t seed = ctxSeed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 97u;
		const auto [lo, hi] = std::minmax_element(h.Values.begin(), h.Values.end());
		const float low = *lo, range = std::max(*hi - *lo, 1e-6f);
		float flowMax = 1.0f;
		if (flow) flowMax = std::max(*std::max_element(flow->Values.begin(), flow->Values.end()), 2.0f);
		TgField out;
		out.Resize(h.N, h.Size, h.Center);
		for (uint32_t j = 0; j < h.N; ++j)
			for (uint32_t i = 0; i < h.N; ++i)
			{
				const glm::vec2 p = h.SamplePosition(i, j);
				const float concavity = Concavity(h, i, j, radius);
				const float shift = t.Shift ? std::clamp(concavity * 40.0f, -1.0f, 1.0f) * (bare - full) * 0.5f : 0.0f;
				float snow = 1.0f - Smoothstep(full + shift, bare + shift, SlopeDegrees(h, i, j));
				if (t.Curvature) snow += std::max(concavity, 0.0f) * 30.0f * hollowBoost - std::max(-concavity, 0.0f) * 30.0f * ridgeLoss;
				if (flow && t.Flow)
				{
					const float f = std::log(std::max(flow->Sample(p.x, p.y), 1.0f)) / std::log(flowMax);
					snow += Smoothstep(0.25f, 0.6f, f) * flowBoost;
				}
				if (t.Breakup) snow += (TerrainNoise::Fbm(p.x / breakupScale, p.y / breakupScale, 3, seed) - 0.5f) * 2.0f * breakup;
				snow *= Smoothstep(minRelative - 0.04f, minRelative + 0.04f, (h.At(i, j) - low) / range);
				out.At(i, j) = std::clamp(snow, 0.0f, 1.0f);
			}
		return out;
	}


	int Snow(int argc, char** argv)
	{
		if (argc < 8) { std::fprintf(stderr, "snow <graph.json|preset> <temperate|alpine|desert> <seed> <size> <resolution> <out_dir>\n"); return 2; }
		const std::string preset = argv[2], type = argv[3];
		WorldDesc desc = ProbeDesc(type, static_cast<uint32_t>(std::atoi(argv[4])), static_cast<float>(std::atof(argv[5])), static_cast<uint32_t>(std::atoi(argv[6])));
		TerrainGraph g;
		{
			bool own = false;
			std::string why;
			if (!ProbeGraph(preset, desc, g, own, why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 2; }
			desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(g);
		}
		desc.GraphCache = std::make_shared<TerrainGraphCache>();
		const std::string outDir = argv[7];
		std::string label = preset;
		for (char& ch : label) if (ch == ':' || ch == '/' || ch == '\\' || ch == '.') ch = '_';
		if (label.size() > 5 && label.substr(label.size() - 5) == "_json") label = label.substr(0, label.size() - 5);
		if (const size_t cut = label.rfind("terrain_graphs_"); cut != std::string::npos) label = label.substr(cut + 15);
		const std::string tag = outDir + "/" + label + "_" + type + "_" + std::to_string(desc.Seed) + "_" + std::to_string(desc.Resolution);

		TgResult r;
		std::string error;
		if (!EvaluateWorldGraph(desc, r, error)) { std::fprintf(stderr, "evaluation failed: %s\n", error.c_str()); return 1; }
		const TgNode* node = NodeOfType(g, "snow_accumulate");
		if (!node) { std::fprintf(stderr, "the graph has no snow_accumulate node\n"); return 1; }
		const TgField* h = FieldInto(g, r, node->Id, "height");
		const TgField* flow = FieldInto(g, r, node->Id, "flow");
		if (!h) { std::fprintf(stderr, "no height into the snow node\n"); return 1; }
		const auto pub = r.Outputs.find("splat_snow");

		SnowAccumulateNode impl;
		TgContext ctx;
		ctx.Seed = desc.Seed;
		ctx.Resolution = h->N;
		ctx.Size = h->Size;
		ctx.Center = h->Center;
		std::vector<const TgField*> inputs = { h, flow };
		auto run = [&](const TgNode& n, TgField& out, double* bestMs)
		{
			double best = 1e30;
			for (int k = 0; k < (bestMs ? 5 : 1); ++k)
			{
				const auto t0 = std::chrono::steady_clock::now();
				std::string e;
				if (!impl.Evaluate(n, ctx, inputs, out, e)) { std::fprintf(stderr, "%s\n", e.c_str()); return false; }
				best = std::min(best, 1000.0 * Seconds(t0));
			}
			if (bestMs) *bestMs = best;
			return true;
		};

		TgField now;
		double nodeMs = 0.0;
		if (!run(*node, now, &nodeMs)) return 1;
		WriteF32(tag + "_height.f32", h->Values);
		if (flow) WriteF32(tag + "_flow.f32", flow->Values);
		WriteF32(tag + "_snow_node.f32", now.Values);
		if (pub != r.Outputs.end() && pub->second.N == h->N) WriteF32(tag + "_snow_published.f32", pub->second.Values);
		double oldMs = 0.0;
		{
			const auto t0 = std::chrono::steady_clock::now();
			WriteF32(tag + "_snow_old.f32", OldSnow(*node, ctx.Seed, *h, flow, {}).Values);
			oldMs = 1000.0 * Seconds(t0);
		}
		WriteF32(tag + "_snow_old_noshift.f32", OldSnow(*node, ctx.Seed, *h, flow, { false, true, true, true }).Values);
		WriteF32(tag + "_snow_old_nocurv.f32", OldSnow(*node, ctx.Seed, *h, flow, { true, false, true, true }).Values);
		WriteF32(tag + "_snow_old_noconcavity.f32", OldSnow(*node, ctx.Seed, *h, flow, { false, false, true, true }).Values);
		WriteF32(tag + "_snow_old_noflow.f32", OldSnow(*node, ctx.Seed, *h, flow, { true, true, false, true }).Values);
		WriteF32(tag + "_snow_old_nobreakup.f32", OldSnow(*node, ctx.Seed, *h, flow, { true, true, true, false }).Values);
		WriteF32(tag + "_snow_old_slopeonly.f32", OldSnow(*node, ctx.Seed, *h, flow, { false, false, false, false }).Values);
		// the node now, a term at a time
		const std::pair<const char*, std::vector<const char*>> knobs[] = {
			{ "noflow", { "FlowBoost" } }, { "nocurv", { "HollowBoost", "RidgeLoss" } }, { "nobreakup", { "Breakup" } } };
		for (const auto& [name, params] : knobs)
		{
			TgNode n = *node;
			for (const char* p : params) n.Params[p] = 0.0;
			TgField out;
			if (!run(n, out, nullptr)) return 1;
			WriteF32(tag + "_snow_node_" + name + ".f32", out.Values);
		}
		if (FILE* f = std::fopen((tag + "_snow.json").c_str(), "w"))
		{
			std::fprintf(f, "{\"n\": %u, \"size\": %.3f, \"spacing\": %.5f, \"node_ms\": %.1f, \"old_ms\": %.1f, \"flow\": %s}\n", h->N, h->Size, h->Spacing(), nodeMs, oldMs,
				flow ? "true" : "false");
			std::fclose(f);
		}
		std::printf("snow %s: node %.0f ms (best of 5), the pre-T112 node %.0f ms; wrote %s_*\n", label.c_str(), nodeMs, oldMs, tag.c_str());
		return 0;
	}
}
