#include "GB/World/Graph/Nodes/ThermalErosionNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Heightmap.h"
#include "GB/World/ThermalErosion.h"
#include <glm/glm.hpp>

namespace GB
{

	bool ThermalErosionNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "erosion_thermal node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& in = *inputs[0];
		if (in.N == 0) { error = "erosion_thermal node '" + node.Id + "' got an empty field"; return false; }

		Heightmap heightmap(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i) heightmap.At(i, j) = in.At(i, j);

		ThermalErosionParams params;
		params.Iterations = static_cast<int>(TgParam(node, "Iterations", static_cast<double>(params.Iterations)));
		params.TalusAngleDegrees = static_cast<float>(TgParam(node, "TalusAngleDegrees", static_cast<double>(params.TalusAngleDegrees)));
		params.Strength = static_cast<float>(TgParam(node, "Strength", static_cast<double>(params.Strength)));

		ThermalErosion::Run(heightmap, params);

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

	TgNodeSchema ThermalErosionNode::Describe() const
	{
		const ThermalErosionParams d;
		TgNodeSchema s;
		s.Category = "Erode";
		s.Summary = "Slopes steeper than the angle of repose slump onto their lower neighbours (scree, talus).";
		s.Color = 0x38824F;
		s.Inputs = { { "height", TgPortKind::Field }, { "mask", TgPortKind::Field, true, "0..1: where the erosion applies (unconnected: everywhere)" } };
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = {
			{ "Iterations", static_cast<double>(d.Iterations), 0, 400, true, "passes over the map" },
			{ "TalusAngleDegrees", d.TalusAngleDegrees, 5, 80, false, "angle of repose: steeper slopes slump (keep it high for cliffs)" },
			{ "Strength", d.Strength, 0, 1, false, "fraction of the excess moved per pass" },
		};
		return s;
	}
}
