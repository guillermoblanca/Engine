// T343: the trees and rocks of the scenery in a painted farm zone (GB/Game/FarmZones.h, GB/Game/Scenery.h). On a generated world run as GBSim
// runs it (SimRun: the members on the real navigation grid, the world's trunks and rocks as its walls): the 64 squares of T337 leave out only
// the plots on water or a cliff (a tree or a rock of the scenery is cleared as a node is), and a zone painted over one turns it into a node of
// the data that is marked, worked and cleared away, its wall gone from the grid. CPU only.
#include "GB/Game/GameContent.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/ContentPack.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

int GBTest_farm_zones_scenery(int, char**)
{
	using namespace GB;
	// ---- T343: the 64 squares of T337 on a generated world (GBSim's ground: SimRun, its members on the real navigation grid). A square of 10 m
	// (6 x 6 plots) leaves out only the plots on water or a cliff: a tree or a rock of the scenery in the way is cleared as a node is (before
	// T343, some 4 % of the plots were walled by them).
	{
		ContentTargets to;
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> nodes;
		TribeConfig config;
		TechTree techs;
		std::map<std::string, BuildingDef> buildings;
		to.Items = &items; to.Recipes = &recipes; to.Nodes = &nodes; to.Techs = &techs; to.Buildings = &buildings;
		to.Roles = &config.Roles; to.Agenda = &config.Agenda; to.Economy = &config.Economy; to.Work = &config.Work;
		to.Explore = &config.Explore; to.Econ = &config.Econ; to.Player = &config.Player; to.Social = &config.Social; to.Society = &config.Society;
		to.Needs = &config.DataNeeds; to.Climate = &config.Seasons.Climate; to.Founding = &config.Founding;
		ContentPacks packs;
		AddContentSections(packs, to);
		const std::string loaded = packs.Load(GamePack(), std::string(GB_ASSETS_DIR) + "/data");
		Check(loaded.empty(), "the game's pack loads");
		WorldDesc d;   // GB.exe's `generate temperate 5`
		d.Type = WorldType::Temperate;
		d.Seed = 5;
		const GeneratedWorld world = WorldGenerator::GenerateData(d);
		SimSetup setup;
		setup.World = &world;
		setup.Items = &items; setup.Recipes = &recipes; setup.Nodes = &nodes; setup.Config = &config; setup.Techs = &techs; setup.Buildings = &buildings;
		setup.Members = 4;
		SimRun run(setup);
		Check(run.Ok(), "a run on the generated world");
		if (run.Ok())
		{
			Tribe& t = run.Brain();
			std::vector<std::string> todo{ "tillage" };   // (SimSetup::Know reaches the clans of a run of several)
			while (!todo.empty())
			{
				const std::string id = todo.back();
				todo.pop_back();
				const TechDef* tech = t.Knowledge().Techs() ? t.Knowledge().Techs()->Find(id) : nullptr;
				if (!tech) continue;
				t.Knowledge().GrantClan(id);
				todo.insert(todo.end(), tech->Requires.begin(), tech->Requires.end());
			}
			t.Zones().SetPlayer(true);
			// a plot walled by the scenery: a tree or a rock whose wall is within the reach of the clearing rule (economy.farming.clearReach)
			const SceneryHost host = t.Scenery();
			const float reach = t.Config.Econ.Farming.ClearReach;
			auto scenery = [&](const glm::vec2& p)
			{
				std::vector<SceneryProp> near;
				host.Around(p - glm::vec2(reach + 4.0f), p + glm::vec2(reach + 4.0f), near);
				return std::any_of(near.begin(), near.end(), [&](const SceneryProp& x) { return glm::distance(glm::vec2(x.Position.x, x.Position.z), p) - x.Radius <= reach; });
			};
			const glm::vec2 camp = run.Camp();
			struct Sweep { int Accepted = 0, Plots = 0, Walled = 0, ByScenery = 0, ToClear = 0; };
			auto sweep = [&](const char* label)
			{
				Sweep s;
				std::map<std::string, int> refused;
				for (int i = 0; i < 64; ++i)
				{
					const glm::vec2 a = camp + glm::vec2(-96.0f + 24.0f * static_cast<float>(i % 8), -96.0f + 24.0f * static_cast<float>(i / 8));
					const glm::vec2 b = a + glm::vec2(10.0f);
					int n = 0;
					std::vector<glm::vec2> clear, out;
					const FarmZones::Reply r = FarmZones::Check(t, { a, { b.x, a.y }, b, { a.x, b.y } }, "", &n, &clear, &out);
					if (!r.Ok) { ++refused[r.Text.substr(0, r.Text.find(" at "))]; continue; }
					++s.Accepted;
					s.Plots += n + static_cast<int>(out.size());
					s.Walled += static_cast<int>(out.size());
					s.ToClear += static_cast<int>(clear.size());
					for (const glm::vec2& p : out) s.ByScenery += scenery(p) ? 1 : 0;
				}
				std::fprintf(stderr, "  sweep %s: %d of 64 squares accepted, %d plots, %d left out (%d walled by a tree or a rock of the scenery, the rest steep ground), %d to clear first\n",
					label, s.Accepted, s.Plots, s.Walled, s.ByScenery, s.ToClear);
				for (const auto& [why, n] : refused) std::fprintf(stderr, "    refused x%d: %s\n", n, why.c_str());
				return s;
			};
			t.SetScenery({});   // as before T343: the scenery walls plots
			const Sweep before = sweep("without the scenery host (T337)");
			t.SetScenery(host);
			const Sweep after = sweep("with it (T343)");
			Check(before.ByScenery > 0, "(the sweep meets the scenery: before T343 it walled plots)");
			Check(after.ByScenery == 0, "no plot of the sweep is left out for a tree or a rock of the scenery");
			Check(after.Plots == before.Plots && after.Walled == before.Walled - before.ByScenery, "the plots it walled are to clear now; the steep ones are still left out");

			// a zone painted over a tree of the scenery: it becomes a node of the data, marked, felled and cleared away; its wall goes at once
			size_t pick = world.Trees.size();
			for (size_t i = 0; i < world.Trees.size() && pick == world.Trees.size(); ++i)
			{
				const glm::vec2 at(world.Trees[i].Position.x, world.Trees[i].Position.z);
				const float d = glm::distance(at, camp);
				if (d < 20.0f || d > 90.0f) continue;
				const glm::vec2 a = glm::round(at) - glm::vec2(3.0f), b = a + glm::vec2(6.0f);
				std::vector<glm::vec2> clear;
				if (FarmZones::Check(t, { a, { b.x, a.y }, b, { a.x, b.y } }, "", nullptr, &clear).Ok && !clear.empty()) pick = i;
			}
			Check(pick < world.Trees.size(), "a tree of the scenery near the camp where a zone can be painted");
			if (pick < world.Trees.size())
			{
				const std::string prop = "Tree " + std::to_string(pick + 1), node = "scenery tree " + std::to_string(pick + 1);
				const glm::vec2 at(world.Trees[pick].Position.x, world.Trees[pick].Position.z);
				const glm::vec2 a = glm::round(at) - glm::vec2(3.0f);
				const size_t walls = run.Ground().Obstacles.size();
				const FarmZones::Reply r = t.Zones().Paint(t, a, a + glm::vec2(6.0f), "");
				std::fprintf(stderr, "  %s at %.1f, %.1f: %s | %s\n", prop.c_str(), static_cast<double>(at.x), static_cast<double>(at.y), r.Text.c_str(), t.Zones().Clearing("zone 1").c_str());
				Check(r.Ok && r.Text.find("to clear first") != std::string::npos, "the zone is painted over it, to clear first");
				Check(run.View().Gameplay->Find(node) != nullptr && run.View().Entities->FindByName(node) != NullEntity, "the tree is a node of the data now");
				Check(t.Designations().MarkOf(node) != nullptr, "marked to clear");
				Check(run.Ground().Obstacles.size() == walls - 1, "and its wall is gone from the ground");
				std::vector<SceneryProp> left;
				t.Scenery().Around(at - glm::vec2(0.5f), at + glm::vec2(0.5f), left);
				Check(std::none_of(left.begin(), left.end(), [&](const SceneryProp& x) { return x.Name == prop; }), "and from the scenery");
				for (const TribeMember& m : t.Members()) if (m.Body) m.Body->Bag().Add(items, "stone_axe", 1);   // (as the woodcutters of T337's test carry theirs)
				for (int i = 0; i < 40 && run.View().Gameplay->Find(node); ++i) run.Run(60.0);
				std::fprintf(stderr, "  after %.0f s: %s\n", run.Clock(), t.Zones().List(t).c_str());
				for (const TribeMember& m : t.Members()) std::fprintf(stderr, "    %s (%s): %s %s [%s]\n", m.Name.c_str(), m.Role.c_str(), m.Job.c_str(), m.Detail.c_str(), m.Why.c_str());
				if (const AbilitySystemComponent* n = run.View().Gameplay->Find(node)) std::fprintf(stderr, "    %s amount %d; %s\n", node.c_str(), ResourceSystem::Amount(*n), t.Designations().Line().c_str());
				Check(!run.View().Gameplay->Find(node) && run.View().Entities->FindByName(node) == NullEntity, "it is felled and cleared away");
				Check(!t.Zones().Blocked("zone 1"), "and nothing waits in the zone");
			}
		}
	}

	if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
	else std::printf("farm zones scenery: ok\n");
	return failures ? 1 : 0;
}
