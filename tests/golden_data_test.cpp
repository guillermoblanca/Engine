// Golden data: statistics of generated worlds, erosion and simulations compared with committed reference
// values, each with its own tolerance (like geodiff for geometry). Update: GB_UPDATE_GOLDEN=1 ctest -R golden_data
#include "GB/Log.h"
#include "GB/Qa/Qa.h"
#include "GB/Window.h"
#include "GB/World/Destruction.h"
#include "GB/World/Erosion.h"
#include "GB/World/WorldGen.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Report(const GB::Qa::Result& r)
	{
		if (!r.Passed) { std::fprintf(stderr, "FAIL: %s\n", r.Message.c_str()); ++failures; }
		else std::fprintf(stderr, "ok: %s\n", r.Message.c_str());
	}
	struct Stats { double Min = 1e9, Max = -1e9, Mean = 0, Std = 0; };
	Stats HeightStats(const GB::Heightmap& hm, int n = 64)
	{
		Stats s; double sum = 0, sum2 = 0;
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i)
			{
				const float x = hm.GetMin().x + (static_cast<float>(i) + 0.5f) / static_cast<float>(n) * hm.GetSize();
				const float z = hm.GetMin().y + (static_cast<float>(j) + 0.5f) / static_cast<float>(n) * hm.GetSize();
				const double h = hm.GetHeight(x, z);
				s.Min = std::min(s.Min, h); s.Max = std::max(s.Max, h); sum += h; sum2 += h * h;
			}
		s.Mean = sum / (n * n);
		s.Std = std::sqrt(std::max(sum2 / (n * n) - s.Mean * s.Mean, 0.0));
		return s;
	}
	void AddWorld(GB::Qa::GoldenData& d, const GB::GeneratedWorld& w)
	{
		const Stats h = HeightStats(w.TerrainRef->GetHeightmap());
		d.Set("height.min", h.Min, 0.3); d.Set("height.max", h.Max, 0.3); d.Set("height.mean", h.Mean, 0.15); d.Set("height.std", h.Std, 0.15);
		d.Set("trees", static_cast<double>(w.Trees.size()), 4, 0.03);
		d.Set("rocks", static_cast<double>(w.Rocks.size()), 3, 0.05);
		d.Set("tree.recipes", static_cast<double>(w.TreeRecipes.size()), 0);
		const auto shares = w.BiomeShares();
		const char* names[] = { "water", "shore", "meadow", "forest", "rock", "snow", "dunes" };
		for (size_t i = 0; i < shares.size(); ++i) d.Set(std::string("share.") + names[i], shares[i], 0.025);
		d.Set("lake", w.TerrainRef->HasLake() ? 1 : 0, 0);
		if (w.TerrainRef->HasLake())
		{
			d.Set("lake.x", w.TerrainRef->GetLakeCenter().x, 2.0); d.Set("lake.z", w.TerrainRef->GetLakeCenter().y, 2.0);
			d.Set("lake.radius", w.TerrainRef->GetLakeRadius(), 0.01); d.Set("lake.level", w.TerrainRef->GetWaterLevel(), 0.15);
		}
		d.Set("river", w.TerrainRef->GetRiver() ? 1 : 0, 0);
		if (const GB::RiverData* r = w.TerrainRef->GetRiver())
		{
			d.Set("river.points", static_cast<double>(r->Points.size()), 12, 0.05);
			d.Set("river.startLevel", r->Points.front().y, 0.4); d.Set("river.endLevel", r->Points.back().y, 0.4);
		}
	}
}

int main()
{
	GB::Log::Init();
	GB::Qa::Config::SetOutputDir("qa_artifacts");
	const std::string dir = GB_GOLDEN_DIR "/data";

	// ---- erosion of a fixed hill (CPU)
	{
		GB::Heightmap hm(129, 128.0f);
		for (uint32_t j = 0; j < 129; ++j)
			for (uint32_t i = 0; i < 129; ++i)
			{
				const glm::vec2 p = hm.SamplePosition(i, j);
				hm.At(i, j) = 30.0f * std::max(0.0f, 1.0f - glm::length(p) / 64.0f) + 1.5f * std::sin(p.x * 0.35f) * std::cos(p.y * 0.3f);
			}
		GB::ErosionParams ep; ep.Droplets = 40000; ep.Seed = 7;
		const GB::ErosionStats st = GB::HydraulicErosion::Run(hm, ep);
		const Stats h = HeightStats(hm, 48);
		GB::Qa::GoldenData d;
		d.Set("eroded", st.Eroded, 0, 0.05); d.Set("deposited", st.Deposited, 0, 0.05);
		d.Set("height.max", h.Max, 0.8); d.Set("height.mean", h.Mean, 0.2); d.Set("height.std", h.Std, 0.3);
		Report(d.Check(dir, "erosion_hill"));
	}

	// ---- a brick wall hit by a ball, and a blast on crates (CPU physics + destruction)
	{
		GB::PhysicsWorld w;
		w.SetGround([](float, float) { return 0.0f; }, nullptr);
		GB::DestructionSystem ds(w);
		GB::BodyDesc brick; brick.HalfExtents = { 0.4f, 0.15f, 0.2f }; brick.Density = 1800.0f; brick.Friction = 0.7f;
		GB::BodyDesc half = brick; half.HalfExtents.x = 0.2f;
		GB::DestructibleDesc solid; solid.Toughness = 1e9f;
		std::vector<int> bricks;
		for (int row = 0; row < 6; ++row)
		{
			auto add = [&](const GB::BodyDesc& bd, float x) { bricks.push_back(w.AddBody(bd, { x, 0.15f + 0.3002f * static_cast<float>(row), 10.0f })); ds.Add(bricks.back(), solid); };
			if (row % 2 == 0) for (int c = 0; c < 7; ++c) add(brick, (static_cast<float>(c) - 3.0f) * 0.82f);
			else { for (int c = 0; c < 6; ++c) add(brick, (static_cast<float>(c) - 2.5f) * 0.82f); add(half, -2.65f); add(half, 2.65f); }
		}
		auto run = [&](float s) { for (float t = 0; t < s; t += 1.0f / 60.0f) { w.Step(1.0f / 60.0f); ds.Update(1.0f / 60.0f, w.TakeContacts()); } };
		run(3.0f);
		GB::Qa::GoldenData d;
		int asleep = 0; double drift = 0;
		for (int b : bricks) { asleep += w.GetBody(b).Sleeping ? 1 : 0; drift = std::max(drift, static_cast<double>(std::fabs(w.GetBody(b).Position.z - 10.0f))); }
		d.Set("wall.bricks", static_cast<double>(bricks.size()), 0);
		d.Set("wall.asleep", asleep, 2); d.Set("wall.drift", drift, 0.05);
		GB::BodyDesc ball; ball.Shape = GB::ShapeType::Sphere; ball.Radius = 0.45f; ball.Density = 5000.0f;
		const int b = w.AddBody(ball, { 0, 1.0f, 0 });
		w.GetBody(b).Velocity = { 0, 2.0f, 22.0f };
		run(3.0f);
		int moved = 0; double meanY = 0;
		for (int id : bricks) { if (std::fabs(w.GetBody(id).Position.z - 10.0f) > 0.5f) ++moved; meanY += w.GetBody(id).Position.y; }
		d.Set("hit.bricksMoved", moved, 6, 0.15);
		d.Set("hit.meanHeight", meanY / static_cast<double>(bricks.size()), 0.12);
		GB::DestructibleDesc crate; crate.Toughness = 600.0f;
		GB::BodyDesc box; box.HalfExtents = glm::vec3(0.5f);
		int broke = 0;
		for (int i = 0; i < 6; ++i) { const int id = w.AddBody(box, { -20.0f + 3.0f * static_cast<float>(i), 0.5f, -20.0f }); ds.Add(id, crate); }
		ds.Explode({ -17.0f, 0.5f, -20.0f }, 9.0f, 18.0f);
		ds.Update(0.0f, {});
		for (const GB::BreakEvent& e : ds.TakeBreaks()) broke += e.Fragments > 0 ? 1 : 0;
		d.Set("blast.cratesBroken", broke, 1);
		d.Set("blast.debris", static_cast<double>(ds.GetDebrisCount()), 8);
		Report(d.Check(dir, "physics_wall"));
	}

	// ---- generated worlds (the terrain builds a mesh, so these need OpenGL)
	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP (worlds): no OpenGL 4.5 context\n");
		return failures ? failures : 77;
	}
	struct Case { const char* Name; GB::WorldType Type; uint32_t Seed; };
	for (const Case& c : { Case{ "world_temperate_5", GB::WorldType::Temperate, 5 }, Case{ "world_alpine_2", GB::WorldType::Alpine, 2 }, Case{ "world_desert_3", GB::WorldType::Desert, 3 } })
	{
		GB::WorldDesc wd;
		wd.Type = c.Type; wd.Seed = c.Seed; wd.Size = 192.0f; wd.Resolution = 161; wd.TreeBudget = 150; wd.RockBudget = 40;
		const GB::GeneratedWorld w = GB::WorldGenerator::Generate(wd);
		GB::Qa::GoldenData d;
		AddWorld(d, w);
		Report(d.Check(dir, c.Name));
	}
	return failures;
}
