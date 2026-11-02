#include "GB/Water/WaterPlanner.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		TgNode Node(const char* id, const char* type, const char* source = "", std::map<std::string, double> params = {})
		{
			TgNode n;
			n.Id = id; n.Type = type; n.Source = source; n.Params = std::move(params);
			return n;
		}
	}

	TerrainGraph MakeWaterGraph(const WaterPlanRequest& request)
	{
		TerrainGraph g;
		g.Id = "water";
		g.Name = "Water: lake site, drainage and river";
		g.Nodes.push_back(Node("ground", "height_input", "height"));
		g.Nodes.push_back(Node("fill", "depression_fill", "", { { "Epsilon", 0.001 } }));
		g.Nodes.push_back(Node("flow", "flow_accumulation"));
		// (the edge order is the input order of the node that receives them: fill takes [ground or dug ground, site], river takes [fill, flow, site])
		if (request.Lake)
		{
			g.Nodes.push_back(Node("site", "lake_site"));
			g.Nodes.push_back(Node("dig", "lake_basin", "", { { "Radius", request.Gen.LakeRadius }, { "Depth", request.Gen.LakeDepth }, { "LevelAbove", request.Gen.LakeLevelAboveBasin },
				{ "Irregularity", request.Gen.LakeIrregularity } }));
			g.Edges.push_back({ "ground", "site" });
			g.Edges.push_back({ "ground", "dig" });
			g.Edges.push_back({ "dig", "fill" });
			g.Edges.push_back({ "site", "fill" });
		}
		else g.Edges.push_back({ "ground", "fill" });
		g.Edges.push_back({ "fill", "flow" });
		if (request.River)
		{
			g.Nodes.push_back(Node("river", "river_trace", "", { { "MinStartDistance", request.RiverStartMinDistance }, { "Spacing", request.PathSpacing } }));
			g.Edges.push_back({ "fill", "river" });
			g.Edges.push_back({ "flow", "river" });
			if (request.Lake) g.Edges.push_back({ "site", "river" });
		}
		return g;
	}

	bool PlanWater(const TerrainNodeRegistry& registry, const Heightmap& ground, const WaterPlanRequest& request, WaterPlan& plan, std::string& error)
	{
		plan = WaterPlan();
		if (!request.Lake && !request.River) return true;

		TgField field;
		field.Resize(ground.GetResolution(), ground.GetSize(), (ground.GetMin() + ground.GetMax()) * 0.5f);
		for (uint32_t j = 0; j < field.N; ++j)
			for (uint32_t i = 0; i < field.N; ++i) field.At(i, j) = ground.At(i, j);
		const std::map<std::string, TgField> inputs{ { "height", std::move(field) } };

		TgContext ctx;
		ctx.Seed = request.Seed;
		ctx.Resolution = ground.GetResolution();
		ctx.Size = ground.GetSize();
		ctx.Center = (ground.GetMin() + ground.GetMax()) * 0.5f;
		ctx.Inputs = &inputs;

		TgResult result;
		if (!EvaluateTerrainGraph(MakeWaterGraph(request), registry, ctx, result, error)) return false;

		if (request.Lake)
		{
			const auto it = result.Paths.find("site");
			if (it == result.Paths.end() || it->second.Items.empty() || it->second.Items[0].Points.empty()) { error = "the water graph found no lake site"; return false; }
			plan.HasLake = true;
			plan.LakeCenter = it->second.Items[0].Points[0];
			plan.LakeFloor = it->second.Items[0].Values[0];
		}
		if (request.River)
		{
			const auto it = result.Paths.find("river");
			if (it == result.Paths.end() || it->second.Items.empty() || it->second.Items[0].Points.size() < 2) { error = "the water graph found no river"; return false; }
			plan.HasRiver = true;
			plan.RiverPath = it->second.Items[0].Points;
			plan.RiverDrainage = it->second.Items[0].Values;
		}
		return true;
	}

	void ApplyWaterPlan(const WaterPlan& plan, const WaterGenParams& gen, HillParams& recipe)
	{
		if (plan.HasLake)
		{
			recipe.LakeCenter = plan.LakeCenter;
			recipe.LakeRadius = gen.LakeRadius;
			recipe.LakeDepth = gen.LakeDepth;
			recipe.WaterLevel = plan.LakeFloor + gen.LakeLevelAboveBasin;
			recipe.LakeIrregularity = gen.LakeIrregularity;
		}
		if (!plan.HasRiver) return;

		std::vector<glm::vec2> path = plan.RiverPath;
		const float mouth = plan.RiverDrainage.empty() ? 1.0f : std::max(*std::max_element(plan.RiverDrainage.begin(), plan.RiverDrainage.end()), 1.0f);
		std::vector<float> discharge(path.size());
		for (size_t i = 0; i < path.size(); ++i) discharge[i] = std::min(plan.RiverDrainage[i] / mouth, 1.0f);

		if (plan.HasLake)   // the river ends at the bank of the lake, not in the middle of it
		{
			const LakeOutline outline = MakeLakeOutline(recipe.Seed, recipe.LakeIrregularity);
			while (path.size() > 2 && outline.EffectiveDistance(path.back(), recipe.LakeCenter) < recipe.LakeRadius * 1.7f) { path.pop_back(); discharge.pop_back(); }
			const glm::vec2 fromLake = path.back() - recipe.LakeCenter;
			const float len = glm::length(fromLake);
			const glm::vec2 out = len > 1e-3f ? fromLake / len : glm::vec2(1.0f, 0.0f);
			// the shore in that direction: 0.95 of the lake's radius there (it is a lobed shore)
			const float shore = recipe.LakeRadius * outline.RadiusFactor(std::atan2(out.y, out.x)) * 0.95f;
			path.push_back(recipe.LakeCenter + out * shore);
			discharge.push_back(1.0f);
		}
		if (path.size() >= 3)
		{
			recipe.RiverPath = path;
			recipe.RiverDischarge = discharge;
		}
		recipe.RiverWidth = gen.RiverWidth;
	}
}