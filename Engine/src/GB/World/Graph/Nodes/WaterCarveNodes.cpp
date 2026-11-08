#include "GB/World/Graph/Nodes/WaterCarveNodes.h"
#include "GB/Water/WaterCarver.h"
#include "GB/Water/WaterPlanner.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Terrain.h"

namespace GB
{
	namespace
	{
		constexpr uint32_t kWater = 0x288282;

		std::string Who(const TgNode& node) { return node.Type + " node '" + node.Id + "'"; }

		const TgValue* Input(const std::vector<const TgValue*>& inputs, size_t slot, TgPortKind kind)
		{
			return inputs.size() > slot && inputs[slot] && inputs[slot]->Kind == kind ? inputs[slot] : nullptr;
		}

		LakeShape ShapeOf(const LakeBody& lake)
		{
			LakeShape shape;
			if (!lake.Present) { shape.Radius = 0.0f; return shape; }
			shape.Center = lake.Center; shape.Radius = lake.Radius; shape.Level = lake.Level; shape.Outline = lake.Outline;
			return shape;
		}
	}

	// ------------------------------------------------------------------ lake_carve
	TgNodeSchema LakeCarveNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Water";
		s.Summary = "Digs the world's lake at a site and adds it to the water (connect an output named \"water\" to flood it).";
		s.Color = kWater;
		s.Inputs = { { "height", TgPortKind::Field }, { "site", TgPortKind::Paths, false, "lake_site: the centre and the basin's height" },
			{ "water", TgPortKind::Water, true, "water carved before (kept)" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "water", TgPortKind::Water } };
		s.Params = {
			{ "Radius", 22, 2, 150, false, "metres of the water's edge" },
			{ "Depth", 3, 0, 20, false, "metres below the surface at the centre" },
			{ "LevelAbove", 1, 0, 10, false, "metres the surface stands above the basin" },
			{ "BankSlope", 0.3, 0, 2, false, "steepest rise of the ground away from the shore (m/m); 0 = unlimited" },
			{ "Irregularity", 0.12, 0, 0.3, false, "0 = a round shore" },
		};
		return s;
	}

	bool LakeCarveNode::Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* site = Input(inputs, 1, TgPortKind::Paths);
		const TgValue* water = Input(inputs, 2, TgPortKind::Water);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		if (!site || site->Paths.Items.empty() || site->Paths.Items[0].Points.empty()) { error = Who(node) + ": input 'site' has no point"; return false; }

		const TgPath& at = site->Paths.Items[0];
		LakeShape lake;
		lake.Center = at.Points[0];
		lake.Radius = static_cast<float>(TgParam(node, "Radius", 22.0));
		lake.Depth = static_cast<float>(TgParam(node, "Depth", 3.0));
		lake.Level = (at.Values.empty() ? height->Field.Sample(lake.Center.x, lake.Center.y) : at.Values[0]) + static_cast<float>(TgParam(node, "LevelAbove", 1.0));
		lake.BankSlope = static_cast<float>(TgParam(node, "BankSlope", 0.3));
		lake.Outline = MakeLakeOutline(ctx.Seed, static_cast<float>(TgParam(node, "Irregularity", 0.12)));

		TgValue& ground = outputs["height"];
		ground.Kind = TgPortKind::Field;
		ground.Field = height->Field;
		for (uint32_t j = 0; j < ground.Field.N; ++j)
			for (uint32_t i = 0; i < ground.Field.N; ++i)
				ground.Field.At(i, j) = ApplyLakeBasin(ground.Field.At(i, j), ground.Field.SamplePosition(i, j), lake);

		TgValue& out = outputs["water"];
		out.Kind = TgPortKind::Water;
		if (water) out.Water = water->Water;
		out.Water.SetLake({ true, lake.Center, lake.Radius, lake.Level, lake.Outline });
		return true;
	}

	// ------------------------------------------------------------------ river_carve
	TgNodeSchema RiverCarveNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Water";
		s.Summary = "Carves the world's river along a course, wider and deeper downstream, ending at the lake of the incoming water.";
		s.Color = kWater;
		s.Inputs = { { "height", TgPortKind::Field }, { "river", TgPortKind::Paths, false, "river_trace: the course and the drainage at each point" },
			{ "water", TgPortKind::Water, true, "water carved before: its lake is the river's mouth" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "water", TgPortKind::Water } };
		s.Params = {
			{ "Width", 5, 0.5, 40, false, "metres of water at the mouth" },
			{ "Depth", 1.3, 0.1, 10, false, "metres below the water level at the mouth" },
			{ "MouthLength", 16, 0, 80, false, "metres over which the river blends into the lake" },
		};
		return s;
	}

	bool RiverCarveNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* course = Input(inputs, 1, TgPortKind::Paths);
		const TgValue* water = Input(inputs, 2, TgPortKind::Water);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		if (!course) { error = Who(node) + ": input 'river' is not connected"; return false; }

		TgValue& ground = outputs["height"];
		ground.Kind = TgPortKind::Field;
		ground.Field = height->Field;
		TgValue& out = outputs["water"];
		out.Kind = TgPortKind::Water;
		if (water) out.Water = water->Water;
		if (course->Paths.Items.empty()) return true;   // no river was traced: nothing to carve

		const TgPath& traced = course->Paths.Items[0];
		RiverShape river;
		river.Path = traced.Points;
		ShapeRiverCourse(river.Path, traced.Values, out.Water.HasLake() ? &out.Water.Lake() : nullptr, river.Discharge);
		if (river.Path.size() < 3) return true;
		river.Width = static_cast<float>(TgParam(node, "Width", 5.0));
		river.Depth = static_cast<float>(TgParam(node, "Depth", 1.3));
		river.MouthLength = static_cast<float>(TgParam(node, "MouthLength", 16.0));

		const TgField& f = ground.Field;
		Heightmap heightmap(f.N, f.Size, f.Center);
		for (uint32_t j = 0; j < f.N; ++j)
			for (uint32_t i = 0; i < f.N; ++i) heightmap.At(i, j) = f.At(i, j);
		out.Water.SetRiver(CarveRiver(heightmap, river, ShapeOf(out.Water.Lake())));
		for (uint32_t j = 0; j < f.N; ++j)
			for (uint32_t i = 0; i < f.N; ++i) ground.Field.At(i, j) = heightmap.At(i, j);
		return true;
	}
}
