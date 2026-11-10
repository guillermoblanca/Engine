#include "GB/World/Graph/Nodes/ErosionNode.h"
#include "GB/World/Erosion.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Heightmap.h"
#include <glm/glm.hpp>

namespace GB
{

	bool HydraulicErosionNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "erosion_hydraulic node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& in = *inputs[0];
		if (in.N == 0) { error = "erosion_hydraulic node '" + node.Id + "' got an empty field"; return false; }

		// GB::HydraulicErosion::Run works on a GB::Heightmap (GB/World/Erosion.h:39): build one from the input
		// field, erode it, then copy the result back out. The node adds no erosion logic of its own.
		Heightmap heightmap(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i) heightmap.At(i, j) = in.At(i, j);

		ErosionParams params;
		params.Droplets = static_cast<int>(TgParam(node, "Droplets", static_cast<double>(params.Droplets)));
		const double density = TgParam(node, "DropletDensity", 0.0);   // droplets per cell: the same erosion at any resolution
		if (density > 0.0) params.Droplets = static_cast<int>(density * static_cast<double>(in.N) * static_cast<double>(in.N));
		params.Lifetime = static_cast<int>(TgParam(node, "Lifetime", static_cast<double>(params.Lifetime)));
		params.Inertia = static_cast<float>(TgParam(node, "Inertia", params.Inertia));
		params.Capacity = static_cast<float>(TgParam(node, "Capacity", params.Capacity));
		params.MinSlope = static_cast<float>(TgParam(node, "MinSlope", params.MinSlope));
		params.ErodeSpeed = static_cast<float>(TgParam(node, "ErodeSpeed", params.ErodeSpeed));
		params.DepositSpeed = static_cast<float>(TgParam(node, "DepositSpeed", params.DepositSpeed));
		params.Evaporation = static_cast<float>(TgParam(node, "Evaporation", params.Evaporation));
		params.Gravity = static_cast<float>(TgParam(node, "Gravity", params.Gravity));
		params.BrushRadius = static_cast<int>(TgParam(node, "BrushRadius", static_cast<double>(params.BrushRadius)));
		params.Strength = static_cast<float>(TgParam(node, "Strength", params.Strength));
		params.Seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 97u;

		HydraulicErosion::Run(heightmap, params);

		// the optional mask (0..1) keeps the ground where it is 0 and takes the eroded one where it is 1
		const TgField* mask = inputs.size() > 1 ? inputs[1] : nullptr;
		out.Resize(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i)
			{
				const float keep = mask ? 1.0f - glm::clamp(mask->Sample(in.SamplePosition(i, j).x, in.SamplePosition(i, j).y), 0.0f, 1.0f) : 0.0f;
				out.At(i, j) = heightmap.At(i, j) + (in.At(i, j) - heightmap.At(i, j)) * keep;
			}
		return true;
	}

	TgNodeSchema HydraulicErosionNode::Describe() const
	{
		const ErosionParams d;
		TgNodeSchema s;
		s.Category = "Erode";
		s.Summary = "Rain droplets that carve gullies and leave sediment downhill (GB::HydraulicErosion).";
		s.Color = 0x38824F;
		s.Inputs = { { "height", TgPortKind::Field }, { "mask", TgPortKind::Field, true, "0..1: where the erosion applies (unconnected: everywhere)" } };
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = {
			{ "Droplets", static_cast<double>(d.Droplets), 0, 400000, true, "number of droplets" },
			{ "DropletDensity", 0, 0, 2, false, "> 0: droplets per cell instead of Droplets (the same erosion at any resolution)" },
			{ "Lifetime", static_cast<double>(d.Lifetime), 1, 200, true, "steps per droplet" },
			{ "Inertia", d.Inertia, 0, 1, false, "0 follows the slope, 1 keeps going straight" },
			{ "Capacity", d.Capacity, 0, 20, false, "sediment carried per unit of speed, water and slope" },
			{ "MinSlope", d.MinSlope, 0, 0.2, false, "" },
			{ "ErodeSpeed", d.ErodeSpeed, 0, 1, false, "" },
			{ "DepositSpeed", d.DepositSpeed, 0, 1, false, "" },
			{ "Evaporation", d.Evaporation, 0, 0.2, false, "" },
			{ "Gravity", d.Gravity, 0, 20, false, "" },
			{ "BrushRadius", static_cast<double>(d.BrushRadius), 1, 8, true, "texels eroded around a droplet (wider = smoother gullies)" },
			{ "Strength", d.Strength, 0, 4, false, "multiplies ErodeSpeed" },
			{ "Seed", 0, 0, 100, true, "mixed with the world's seed" },
		};
		return s;
	}
}
