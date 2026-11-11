// The water module (GB/World/Water), headless: the WaterField that Terrain holds answers like the queries it replaced, the carver digs
// what the terrain recipe asked for, the surfaces and their geometry need no OpenGL, and the shader's wave constants are
// generated from the CPU wave set. Nothing here opens a window.
#include "GB/Log.h"
#include "GB/World/Water/WaterCarver.h"
#include "GB/World/Water/WaterField.h"
#include "GB/World/Water/LakeOutline.h"
#include "GB/World/Water/RiverHydraulics.h"
#include "GB/World/Water/WaterGeometry.h"
#include "GB/World/Water/WaterParamInfo.h"
#include "GB/World/Water/WaterPlanner.h"
#include "GB/World/Water/WaterPresets.h"
#include "GB/World/Water/WaterWaves.h"
#include "GB/World/FlowAccumulation.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Terrain.h"
#include "GB/World/Water.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

	// A straight river along +x at z = 0 whose level descends from 3 to 1, with a current of 2 m/s.
	GB::RiverData StraightRiver()
	{
		GB::RiverData r;
		r.HalfWidth = 2.0f;
		r.Depth = 1.0f;
		for (int i = 0; i <= 10; ++i)
		{
			r.Points.push_back({ static_cast<float>(i) * 10.0f, 3.0f - 0.2f * static_cast<float>(i), 0.0f });
			r.Speed.push_back(2.0f);
		}
		return r;
	}
}

int GBTest_water_module(int, char**)
{
	GB::Log::Init();

	// ---------------------------------------------------------------- WaterField: the river questions
	{
		GB::WaterField field;
		Check(!field.HasLake() && field.River() == nullptr, "an empty field has no lake and no river");
		Check(field.DistanceToRiver(1.0f, 1.0f) == GB::kNoWaterDistance && field.RiverSurfaceLevel(1.0f, 1.0f) == GB::kNoWaterLevel, "without a river: no distance, no level");
		Check(field.RiverFlow(1.0f, 1.0f) == glm::vec2(0.0f), "without a river: no current");

		field.SetRiver(StraightRiver());
		Check(field.River() != nullptr, "a field with a river reports it");
		Check(Near(field.DistanceToRiver(25.0f, 3.0f), 3.0f), "distance to the axis is the perpendicular distance");
		Check(Near(field.RiverSurfaceLevel(50.0f, 9.0f), 2.0f), "the level at the nearest point of the axis (halfway down: 2 m)");
		const glm::vec2 flow = field.RiverFlow(30.0f, 0.0f);
		Check(Near(flow.x, 2.0f, 1e-4f) && Near(flow.y, 0.0f, 1e-4f), "on the axis the current runs along the river at full speed");
		Check(glm::length(field.RiverFlow(30.0f, 1.9f)) < 2.0f, "toward the bank the current slows");
		Check(field.RiverFlow(30.0f, 20.0f) == glm::vec2(0.0f), "away from the river there is no current");
		Check(field.InRiverBand(field.NearestRiver(30.0f, 2.3f)) && !field.InRiverBand(field.NearestRiver(30.0f, 2.5f)), "the water band is 1.2 half-widths from the axis");

		// wetting level near the water (what rocks use)
		Check(field.LevelNear(50.0f, 5.0f, 1.5f, 3.0f) == field.RiverSurfaceLevel(50.0f, 5.0f), "near the river the wet level is the river's");
		Check(field.LevelNear(50.0f, 30.0f, 1.5f, 3.0f) == GB::kNoWaterLevel, "far from every body there is no wet level");
		field.SetLake({ true, { 50.0f, 40.0f }, 10.0f, 5.0f });
		Check(field.LevelNear(50.0f, 40.0f, 1.5f, 3.0f) == 5.0f, "inside the lake's reach the wet level is the lake's");
		Check(field.LevelNear(50.0f, 0.0f, 1.5f, 3.0f) == field.RiverSurfaceLevel(50.0f, 0.0f), "the river still wins where only it reaches");
	}

	// ---------------------------------------------------------------- Terrain forwards to its WaterField (same answers)
	{
		GB::HillParams hp;
		hp.Seed = 3;
		hp.LakeCenter = { 20.0f, 10.0f };
		hp.LakeRadius = 14.0f;
		hp.WaterLevel = 1.5f;
		hp.RiverPath = { { -90.0f, -60.0f }, { -40.0f, -30.0f }, { 0.0f, -5.0f }, { 14.0f, 6.0f } };
		hp.RiverWidth = 5.0f;
		auto terrain = GB::Terrain::CreateHills(hp, 200.0f, 129, GB::TerrainStyle::Meadow);
		const GB::WaterField& w = terrain->Water();
		Check(terrain->HasLake() && w.HasLake() && terrain->GetLakeRadius() == 14.0f && terrain->GetWaterLevel() == 1.5f, "the terrain's lake is its field's lake");
		Check(terrain->GetRiver() == w.River() && terrain->GetRiver() != nullptr, "the terrain's river is its field's river");
		bool same = true;
		for (int k = 0; k < 40; ++k)
		{
			const float x = -80.0f + 4.0f * static_cast<float>(k), z = -50.0f + 2.5f * static_cast<float>(k);
			if (terrain->DistanceToRiver(x, z) != w.DistanceToRiver(x, z) || terrain->GetRiverSurfaceLevel(x, z) != w.RiverSurfaceLevel(x, z) || terrain->GetRiverFlow(x, z) != w.RiverFlow(x, z)) same = false;
		}
		Check(same, "Terrain's river queries are its field's");
		const GB::RiverData& river = *terrain->GetRiver();
		const glm::vec3 mid = river.Points[river.Points.size() / 2];
		Check(terrain->GetRiverLevelAt(mid.x, mid.z) > GB::kNoWaterLevel, "the river axis is under the water surface");
		Check(terrain->GetRiverLevelAt(mid.x + 60.0f, mid.z + 60.0f) == GB::kNoWaterLevel, "far from the river there is no river level");
		Check(terrain->GetHeight(hp.LakeCenter.x, hp.LakeCenter.y) < hp.WaterLevel - 1.0f, "the lake basin was dug under the water level");
		// the carver is deterministic: the same recipe gives the same ground and the same river
		auto again = GB::Terrain::CreateHills(hp, 200.0f, 129, GB::TerrainStyle::Meadow);
		Check(again->GetHeight(-20.0f, -12.0f) == terrain->GetHeight(-20.0f, -12.0f) && again->GetRiver()->Points.size() == river.Points.size(), "the same recipe digs the same water");
	}

	// ---------------------------------------------------------------- the carver on its own
	{
		GB::Heightmap flat(65, 128.0f);
		for (uint32_t j = 0; j < 65; ++j)
			for (uint32_t i = 0; i < 65; ++i) flat.At(i, j) = 4.0f - 0.01f * static_cast<float>(i);
		GB::RiverShape shape;
		shape.Path = { { -50.0f, 0.0f }, { 0.0f, 0.0f }, { 50.0f, 0.0f } };
		shape.Width = 6.0f;
		shape.Depth = 1.5f;
		const GB::RiverData river = GB::CarveRiver(flat, shape, GB::LakeShape{});
		Check(river.Points.size() > 50 && river.Speed.size() == river.Points.size() && river.HalfWidth == 3.0f && river.MouthLength == 0.0f, "a river with no lake has no mouth");
		bool descends = true;
		for (size_t i = 1; i < river.Points.size(); ++i) if (river.Points[i].y > river.Points[i - 1].y) descends = false;
		Check(descends, "the water level only ever descends along the path");
		Check(flat.GetHeight(0.0f, 0.0f) < river.Points[river.Points.size() / 2].y, "the bed lies under the water level");
		Check(GB::CarveRiver(flat, GB::RiverShape{}, GB::LakeShape{}).Points.empty(), "a path with fewer than two points carves nothing");
		GB::LakeShape lake; lake.Center = { 50.0f, 0.0f }; lake.Radius = 10.0f; lake.Level = 1.0f; lake.Depth = 2.0f;
		Check(GB::ApplyLakeBasin(5.0f, lake.Center, lake) < lake.Level, "the lake basin is dug under its level at the centre");
		Check(Near(GB::ApplyLakeBasin(5.0f, { 50.0f, 400.0f }, lake), 5.0f, 1e-3f), "far from the lake the ground is untouched");
	}

	// ---------------------------------------------------------------- surfaces and their geometry: no OpenGL needed
	{
		const GB::RiverData river = StraightRiver();
		const GB::WaterGeometry g = GB::MakeRiverGeometry(river);
		Check(g.IsRiver() && g.RiverVertices.size() % static_cast<size_t>(GB::kRiverColumns) == 0 && g.RiverIndices.size() % 6 == 0, "a river ribbon is whole rows of columns and quads");
		Check(!GB::MakeRiverGeometry(GB::RiverData{}).IsRiver(), "a river with no points has no geometry");
		const GB::WaterGeometry lake = GB::MakeLakeGeometry(40.0f, 16);
		Check(!lake.IsRiver() && lake.GridSize == 40.0f && lake.GridSubdivisions == 16, "a lake is a grid description");

		GB::WaterSurface s(river);
		Check(s.IsRiver() && s.Geometry() && s.GetRiverVertexCount() == s.Geometry()->RiverVertices.size(), "a surface exposes its geometry");
		GB::WaterSurface copy = s;
		Check(copy.Geometry() == s.Geometry(), "copies of a surface share one immutable geometry (the renderer keys its GPU mesh on it)");

		GB::WaterField field;
		field.SetRiver(river);
		field.SetLake({ true, { 10.0f, 20.0f }, 8.0f, 2.5f });
		const auto lakeSurface = GB::MakeLakeSurface(field);
		Check(lakeSurface && lakeSurface->Position == glm::vec3(10.0f, 2.5f, 20.0f) && Near(lakeSurface->Radius, 8.0f * 1.2f) && Near(lakeSurface->GetSize(), 8.0f * 2.6f), "the lake surface sits at the lake's level and is sized from its radius");
		Check(Near(lakeSurface->Params.WaveHeight, 0.05f), "and takes the lake look");
		const auto riverSurface = GB::MakeRiverSurface(field);
		Check(riverSurface && riverSurface->IsRiver() && Near(riverSurface->Params.WaveHeight, 0.015f) && Near(riverSurface->Params.Foam, 0.4f), "the river surface takes the river look");
		Check(!GB::MakeLakeSurface(GB::WaterField{}) && !GB::MakeRiverSurface(GB::WaterField{}), "no body, no surface");
	}

	// ---------------------------------------------------------------- the shader's waves come from the CPU wave set
	{
		const std::string glsl = GB::WaveSetGlsl();
		Check(glsl.find("kAngle") != std::string::npos && glsl.find("kLen") != std::string::npos && glsl.find("kAmp") != std::string::npos, "the generated GLSL declares the three arrays");
		Check(glsl.find("0.419999987") != std::string::npos || glsl.find("0.42") != std::string::npos, "it carries the angles of GB::WaveSet");
		Check(glsl.find("float[4](0.0,") != std::string::npos, "integral values keep a decimal point so GLSL reads floats");
		Check(GB::InjectWaveSet("a\n@WAVESET@\nb").find('@') == std::string::npos, "the marker is replaced");
		Check(GB::InjectWaveSet("no marker") == "no marker", "a source without the marker is left alone");
	}

	// ---------------------------------------------------------------- editor parameter table and generation presets
	{
		GB::WaterParams p;
		for (const GB::WaterParamField& f : GB::kWaterParamFields)
			Check(f.Min < f.Max && p.*f.Member >= f.Min && p.*f.Member <= f.Max, "every default value sits inside the range the editor offers");
		Check(GB::DefaultWaterGen(true).LakeRadius == 26.0f && GB::DefaultWaterGen(false).LakeRadius == 22.0f, "alpine lakes are larger");
		Check(GB::DefaultWaterGen(true).RiverWidth == 5.0f && GB::DefaultWaterGen(false).RiverWidth == 6.5f, "and alpine rivers narrower");
	}

	// ---------------------------------------------------------------- a generated world's water goes through the same field
	{
		GB::WorldDesc wd;
		wd.Type = GB::WorldType::Temperate; wd.Lake = true; wd.River = true; wd.Seed = 5; wd.Size = 192.0f; wd.Resolution = 161; wd.TreeBudget = 20; wd.RockBudget = 5;
		const GB::GeneratedWorld w = GB::WorldGenerator::GenerateData(wd);
		const GB::WaterField& field = w.TerrainRef->Water();
		Check(field.HasLake() && field.LakeRadius() == GB::DefaultWaterGen(false).LakeRadius, "the generator digs the lake the water preset asks for");
		if (const GB::RiverData* river = field.River())
		{
			Check(Near(river->HalfWidth * 2.0f, GB::DefaultWaterGen(false).RiverWidth), "and the river of the preset's width");

		}
	}

	// ---------------------------------------------------------------- the physics of a river: hydraulic geometry and Manning
	{
		using namespace GB;
		const HydraulicRules rules;
		Check(Near(HydraulicWidthFactor(1.0f), 1.0f) && Near(HydraulicDepthFactor(1.0f), 1.0f), "at the mouth the river has its full width and depth");
		Check(Near(HydraulicWidthFactor(0.25f), 0.5f), "a quarter of the discharge: half the width (W ~ Q^0.5)");
		Check(Near(HydraulicDepthFactor(0.0f), rules.MinDepthFraction) && Near(HydraulicWidthFactor(0.0f), rules.MinWidthFraction), "the headwaters never shrink below their minimum");
		bool grows = true;
		for (int i = 1; i <= 20; ++i) if (HydraulicWidthFactor(static_cast<float>(i) / 20.0f) < HydraulicWidthFactor(static_cast<float>(i - 1) / 20.0f)) grows = false;
		Check(grows, "the river widens as the discharge grows");
		Check(Near(HydraulicRadius(10.0f, 1.0f), 10.0f / 12.0f) && HydraulicRadius(0.0f, 1.0f) == 0.0f, "hydraulic radius: W d / (W + 2 d)");
		// Manning: v = R^(2/3) sqrt(S) / n  (R = 10/12 m, S = 0.01, n = 0.04  ->  0.8854 * 0.1 / 0.04 = 2.21 m/s)
		Check(Near(ManningSpeed(10.0f, 1.0f, 0.01f), std::pow(10.0f / 12.0f, 2.0f / 3.0f) * 0.1f / 0.04f, 1e-3f), "Manning's speed for a known channel");
		Check(ManningSpeed(10.0f, 1.0f, 0.04f) > ManningSpeed(10.0f, 1.0f, 0.01f) && ManningSpeed(10.0f, 2.0f, 0.01f) > ManningSpeed(10.0f, 1.0f, 0.01f), "steeper and deeper means faster");
		Check(ManningSpeed(10.0f, 1.0f, 0.0f) == rules.MinSpeed || ManningSpeed(10.0f, 1.0f, 0.0f) > rules.MinSpeed, "a flat reach still moves (the slope is bounded below)");
		Check(ManningSpeed(10.0f, 3.0f, 1.0f) == rules.MaxSpeed, "and a waterfall does not run at the speed of sound");
	}

	// ---------------------------------------------------------------- the lake's outline
	{
		using namespace GB;
		const LakeOutline circle;
		Check(circle.IsCircle() && circle.RadiusFactor(1.234f) == 1.0f && Near(circle.EffectiveDistance({ 3.0f, 4.0f }, { 0.0f, 0.0f }), 5.0f), "no irregularity: the circle generators 1 and 2 always had");
		const LakeOutline a = MakeLakeOutline(5, 0.2f), b = MakeLakeOutline(6, 0.2f);
		float lo = 9.0f, hi = 0.0f;
		for (int i = 0; i < 720; ++i)
		{
			const float f = a.RadiusFactor(static_cast<float>(i) * 0.0087266f);
			lo = std::min(lo, f); hi = std::max(hi, f);
		}
		Check(lo >= 0.8f - 1e-4f && hi <= 1.2f + 1e-4f && hi - lo > 0.1f, "the shore stays within (1 +- irregularity) radii and is clearly lobed");
		Check(a.RadiusFactor(0.7f) == MakeLakeOutline(5, 0.2f).RadiusFactor(0.7f) && a.RadiusFactor(0.7f) != b.RadiusFactor(0.7f), "the same seed, the same lake; another seed, another one");
		Check(Near(a.RadiusFactor(0.0f), a.RadiusFactor(6.2831853f), 1e-4f), "the outline closes on itself");
		Check(a.EffectiveDistance({ 10.0f, 0.0f }, { 0.0f, 0.0f }) * a.RadiusFactor(0.0f) > 9.999f, "a point on the shore of direction theta is at effective distance R when its distance is R times the factor");
	}

	// ---------------------------------------------------------------- the water nodes, on hand-made ground
	{
		using namespace GB;
		const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
		Check(registry.IsKnown("height_input") && registry.IsKnown("depression_fill") && registry.IsKnown("lake_site") && registry.IsKnown("river_trace"), "the default registry knows the water nodes");

		// A 65 x 65 map, 128 m: a slope falling toward +x with a deep pit in the middle and a bowl (the basin) in one corner.
		TgField ground;
		ground.Resize(65, 128.0f);
		for (uint32_t j = 0; j < 65; ++j)
			for (uint32_t i = 0; i < 65; ++i)
			{
				const glm::vec2 p = ground.SamplePosition(i, j);
				float h = 30.0f - 0.2f * p.x - 0.05f * p.y;
				h -= 12.0f * std::exp(-glm::dot(p - glm::vec2(-10.0f, 5.0f), p - glm::vec2(-10.0f, 5.0f)) / 80.0f);   // a pit away from the edge
				h -= 14.0f * std::exp(-glm::dot(p - glm::vec2(30.0f, -30.0f), p - glm::vec2(30.0f, -30.0f)) / 500.0f); // the basin
				ground.At(i, j) = h;
			}
		const std::map<std::string, TgField> inputs{ { "height", ground } };
		TgContext ctx; ctx.Resolution = 65; ctx.Size = 128.0f; ctx.Inputs = &inputs;

		auto node = [](const char* id, const char* type, const char* source = "") { TgNode n; n.Id = id; n.Type = type; n.Source = source; return n; };
		// height_input --> depression_fill --> output
		TerrainGraph g;
		g.Nodes = { node("in", "height_input", "height"), node("fill", "depression_fill"), node("out", "output", "filled") };
		g.Edges = { { "in", "fill" }, { "fill", "out" } };
		TgResult r;
		std::string error;
		Check(EvaluateTerrainGraph(g, registry, ctx, r, error), ("height_input -> depression_fill evaluates: " + error).c_str());
		const TgField& filled = r.Outputs["filled"];
		bool raisedOnly = true, changed = false;
		for (size_t k = 0; k < ground.Values.size(); ++k)
		{
			if (filled.Values[k] < ground.Values[k] - 1e-6f) raisedOnly = false;
			if (filled.Values[k] > ground.Values[k] + 1e-6f) changed = true;
		}
		Check(raisedOnly && changed, "filling only ever raises the ground, and it did raise the pits");
		auto pits = [](const TgField& f)
		{
			const std::vector<int> down = FlowAccumulation::Directions(f.Values.data(), f.N);
			int count = 0;
			for (uint32_t j = 1; j + 1 < f.N; ++j)
				for (uint32_t i = 1; i + 1 < f.N; ++i) if (down[static_cast<size_t>(j) * f.N + i] < 0) ++count;
			return count;
		};
		Check(pits(ground) > 0 && pits(filled) == 0, "after filling, every cell of the map drains somewhere (no pits are left)");
		Check(Near(filled.At(0, 0), ground.At(0, 0)) && Near(filled.At(64, 64), ground.At(64, 64)), "the border, where the water leaves the map, is untouched");
		TgResult r2;
		Check(EvaluateTerrainGraph(g, registry, ctx, r2, error) && r2.Outputs["filled"].Values == filled.Values, "filling is deterministic");
		TgContext bare = ctx; bare.Inputs = nullptr;
		Check(!EvaluateTerrainGraph(g, registry, bare, r2, error) && error.find("height_input") != std::string::npos, "height_input says so when the caller brought nothing");

		// the whole water graph over this ground
		WaterPlanRequest request;
		const TerrainGraph water = MakeWaterGraph(request);
		Check(!HasTgErrors(ValidateTerrainGraph(water, registry)), "the water graph is a valid graph");
		TgResult wr;
		Check(EvaluateTerrainGraph(water, registry, ctx, wr, error), ("the water graph evaluates: " + error).c_str());
		const TgPath& site = wr.Paths.at("site").Items.at(0);
		Check(site.Points.size() == 1 && glm::distance(site.Points[0], glm::vec2(30.0f, -30.0f)) < 12.0f, "the lake site is the bottom of the bowl");
		const TgPath& river = wr.Paths.at("river").Items.at(0);
		Check(river.Points.size() > 5 && river.Points.size() == river.Values.size(), "the river is a course with a drainage value at each point");
		Check(glm::distance(river.Points.back(), site.Points[0]) < 4.0f, "it ends at the lake");
		Check(glm::distance(river.Points.front(), site.Points[0]) >= 0.3f * 128.0f - 1.0f, "and begins far from it");
		bool grows = true;
		for (size_t k = 1; k < river.Values.size(); ++k) if (river.Values[k] + 1e-3f < river.Values[k - 1]) grows = false;
		Check(grows && river.Values.back() > 4.0f * river.Values.front(), "the drainage area behind the river only grows downstream");
		bool spacingOk = true;
		for (size_t k = 1; k + 1 < river.Points.size(); ++k) if (glm::distance(river.Points[k], river.Points[k - 1]) < 3.0f - 1e-3f) spacingOk = false;   // (the last step may be short)
		Check(spacingOk, "the course is thinned to about its spacing");
		// the same inputs, the same river
		TgResult again;
		Check(EvaluateTerrainGraph(water, registry, ctx, again, error) && again.Paths.at("river").Items[0].Points == river.Points, "the water graph is deterministic");
		// without a lake the river drains off the map instead
		WaterPlanRequest dry; dry.Lake = false;
		TgResult dr;
		Check(EvaluateTerrainGraph(MakeWaterGraph(dry), registry, ctx, dr, error) && dr.Paths.count("site") == 0 && dr.Paths.at("river").Items[0].Points.size() > 5, "without a lake there is still a river, to wherever the map drains");
	}

	// ---------------------------------------------------------------- the carver with a discharge: the river widens and quickens
	{
		GB::Heightmap ground(129, 192.0f);
		for (uint32_t j = 0; j < 129; ++j)
			for (uint32_t i = 0; i < 129; ++i) ground.At(i, j) = 12.0f - 0.08f * static_cast<float>(i);
		GB::RiverShape shape;
		for (int k = 0; k <= 8; ++k) { shape.Path.push_back({ -80.0f + 20.0f * static_cast<float>(k), 0.0f }); shape.Discharge.push_back(static_cast<float>(k + 1) / 9.0f); }
		shape.Width = 8.0f; shape.Depth = 1.4f;
		const GB::RiverData river = GB::CarveRiver(ground, shape, GB::LakeShape{});
		Check(river.HalfWidths.size() == river.Points.size() && river.HalfWidth == 4.0f, "the river records its half-width at every point, and the widest is the mouth's");
		Check(river.HalfWidths.front() < 0.55f * river.HalfWidths.back() && river.HalfWidths.front() >= 0.3f * 4.0f - 1e-4f, "it is narrow at the source and wide at the mouth");
		Check(river.HalfWidthAt(0, 0.0f) == river.HalfWidths[0] && Near(river.HalfWidthAt(3, 0.5f), 0.5f * (river.HalfWidths[3] + river.HalfWidths[4])), "the half-width between two points is the mean of theirs");
		bool speeds = true;
		for (float v : river.Speed) if (v < 0.6f - 1e-4f || v > 4.5f + 1e-4f) speeds = false;
		Check(speeds, "the current is Manning's, bounded to what the game can use");
		const float widthUp = ground.GetHeight(-60.0f, 0.0f), bankUp = ground.GetHeight(-60.0f, 4.0f);
		const float widthDown = ground.GetHeight(60.0f, 0.0f), bankDown = ground.GetHeight(60.0f, 4.0f);
		Check(bankDown - widthDown > 0.8f * (bankUp - widthUp) || bankUp - widthUp > 0.1f, "the valley is carved at both ends of the river");
		// the field reads the local width: a point 2.5 m from the axis is in the water band at the mouth but not at the source
		GB::WaterField field; field.SetRiver(river);
		const GB::RiverSample up = field.NearestRiver(-70.0f, 2.6f), down = field.NearestRiver(70.0f, 2.6f);
		Check(!field.InRiverBand(up) && field.InRiverBand(down), "2.6 m from the axis is bank at the source and water at the mouth");
		// geometry follows the width too
		const GB::WaterGeometry ribbon = GB::MakeRiverGeometry(river);
		const size_t rows = ribbon.RiverVertices.size() / static_cast<size_t>(GB::kRiverColumns);
		auto span = [&](size_t row) { return glm::distance(ribbon.RiverVertices[row * GB::kRiverColumns].Position, ribbon.RiverVertices[row * GB::kRiverColumns + GB::kRiverColumns - 1].Position); };
		Check(rows > 20 && span(rows - 1) > 1.8f * span(1), "the water ribbon is wider downstream than upstream");
		// T139: every vertex carries its row's half-width in Normal.z, the x (opacity) unchanged; and at a mouth the flare widens it up to 1.7x
		auto halfWidths = [](const GB::WaterGeometry& geo, float& lo, float& hi)
		{
			bool ok = true;
			lo = 1e9f; hi = 0.0f;
			const size_t n = geo.RiverVertices.size() / static_cast<size_t>(GB::kRiverColumns);
			for (size_t row = 0; row < n; ++row)
			{
				const GB::Vertex* v = &geo.RiverVertices[row * GB::kRiverColumns];
				const float half = 0.5f * glm::distance(v[0].Position, v[GB::kRiverColumns - 1].Position);
				for (int c = 0; c < GB::kRiverColumns; ++c) ok = ok && Near(v[c].Normal.z, half, 1e-3f) && v[c].Normal.y == 1.0f && v[c].Normal.x >= 0.0f && v[c].Normal.x <= 1.0f;
				lo = std::min(lo, v[0].Normal.z); hi = std::max(hi, v[0].Normal.z);
			}
			return ok;
		};
		float halfLo = 0.0f, halfHi = 0.0f;
		Check(halfWidths(ribbon, halfLo, halfHi), "a river vertex's Normal.z is the half-width of its row");
		Check(halfLo >= 1.5f * river.HalfWidths.front() - 1e-3f && halfHi <= 1.5f * river.HalfWidth + 1e-3f && halfHi > 1.8f * halfLo,
			"the half-width in Normal.z spans the river's, times the ribbon's 1.5 (no mouth: no flare)");
		GB::RiverData intoLake = river;
		intoLake.MouthLength = 12.0f;
		const GB::WaterGeometry flared = GB::MakeRiverGeometry(intoLake);
		float mouthLo = 0.0f, mouthHi = 0.0f;
		Check(halfWidths(flared, mouthLo, mouthHi), "with a mouth too");
		Check(mouthHi > 1.5f * river.HalfWidth * 1.6f && mouthHi <= 1.5f * river.HalfWidth * 1.7f + 1e-3f, "the mouth flares the half-width up to 1.7x the river's");
		std::printf("water_module: river half-width in Normal.z %.2f-%.2f m, %.2f-%.2f m with a mouth\n", halfLo, halfHi, mouthLo, mouthHi);
		// a river with no discharge is the uniform river it always was
		GB::RiverShape plain = shape; plain.Discharge.clear();
		GB::Heightmap again(129, 192.0f);
		for (uint32_t j = 0; j < 129; ++j)
			for (uint32_t i = 0; i < 129; ++i) again.At(i, j) = 12.0f - 0.08f * static_cast<float>(i);
		const GB::RiverData uniform = GB::CarveRiver(again, plain, GB::LakeShape{});
		Check(uniform.HalfWidths.empty() && uniform.HalfWidth == 4.0f && uniform.Depth == 1.4f, "without a discharge the river is uniform");
	}

	// ---------------------------------------------------------------- a world of generator 3 against generator 2
	{
		auto make = [](uint32_t version, uint32_t seed)
		{
			GB::WorldDesc wd;
			wd.Type = GB::WorldType::Temperate; wd.Lake = true; wd.River = true; wd.Seed = seed; wd.Size = 256.0f; wd.Resolution = 257; wd.TreeBudget = 20; wd.RockBudget = 5;
			wd.GeneratorVersion = version;
			return GB::WorldGenerator::GenerateData(wd);
		};
		for (uint32_t seed : { 5u, 77u, 12u })
		{
			std::fprintf(stderr, "  world seed %u\n", seed);
			const GB::GeneratedWorld v2 = make(2, seed), v3 = make(3, seed);
			const GB::WaterField &w2 = v2.TerrainRef->Water(), &w3 = v3.TerrainRef->Water();
			Check(w2.HasLake() && w3.HasLake() && glm::distance(w2.LakeCenter(), w3.LakeCenter()) < 3.0f, "the lake of generator 3 is in the same basin as generator 2's");
			Check(w2.River() && w2.River()->HalfWidths.empty(), "generator 2 keeps its uniform river");
			const GB::RiverData* river = w3.River();
			Check(river != nullptr && river->Points.size() > 30, "generator 3 has a river");
			if (!river) continue;
			Check(river->HalfWidths.size() == river->Points.size() && river->HalfWidths.front() < 0.8f * river->HalfWidth, "its river is narrower at the source than at the mouth");
			bool descends = true;
			for (size_t k = 1; k < river->Points.size(); ++k) if (river->Points[k].y > river->Points[k - 1].y + 1e-4f) descends = false;
			Check(descends, "the water of the river only goes down");
			Check(!w3.Lake().Outline.IsCircle() && w2.Lake().Outline.IsCircle(), "generator 3's lake has a lobed shore, generator 2's is a circle");
			const glm::vec3 mouth = river->Points.back();
			Check(glm::distance(glm::vec2(mouth.x, mouth.z), w3.LakeCenter()) < w3.LakeRadius() * 1.25f, "the river reaches the lake");
			// the river is still there after erosion: its axis is under the surface along its whole length
			int wet = 0, total = 0;
			for (size_t k = 0; k < river->Points.size(); k += 4, ++total)
				if (v3.TerrainRef->GetRiverLevelAt(river->Points[k].x, river->Points[k].z) > GB::kNoWaterLevel) ++wet;
			Check(wet >= total * 9 / 10, "erosion did not fill the channel: the river is water along its length");
		}
		const GB::GeneratedWorld a = make(3, 5), b = make(3, 5);
		Check(a.TerrainRef->GetRiver()->Points == b.TerrainRef->GetRiver()->Points && a.TerrainRef->GetHeight(10.0f, -20.0f) == b.TerrainRef->GetHeight(10.0f, -20.0f), "a seed gives the same water every time");
	}
	if (failures == 0) std::printf("water_module_test: OK\n");
	return failures;
}
