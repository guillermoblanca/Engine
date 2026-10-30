// Scale of the simulation systems (quality pass): a tribe of many members, a crowd of rabbits and a field full of ability systems, run for a while with
// stand-in bodies. Checked: nothing breaks at 60 members and 400 systems (counts stay in bounds, nobody claims the same node twice). With GB_SCALE_BENCH=1
// it also prints the cost per frame of each system, which docs/INFORME_CALIDAD.md quotes. CPU only.
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	constexpr float dt = 1.0f / 60.0f;
	using Clock = std::chrono::steady_clock;
	double Ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
}

using namespace GB;

// A body that finishes any order after a fixed time, without walking: enough to exercise the decisions of the tribe, not the movement.
class QuickBody : public ITribeBody
{
public:
	explicit QuickBody(glm::vec2 at) : m_At(at) {}
	bool Busy() const override { return m_Left > 0.0f; }
	bool Failed() const override { return false; }
	glm::vec2 Position() const override { return m_At; }
	bool Do(const std::string&, std::string&) override { m_Left = 1.0f; return true; }
	Inventory& Bag() override { return m_Bag; }
	void Update(float step) { if (m_Left > 0.0f) m_Left -= step; }
private:
	glm::vec2 m_At;
	float m_Left = 0.0f;
	Inventory m_Bag{ 12, 0.0f };
};

int main()
{
	const bool bench = std::getenv("GB_SCALE_BENCH") != nullptr;
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> defs;
	RegisterEraOneItems(items, recipes, defs);

	// A generated world is mostly trees and rocks with no ability system; they make every search by name slower.
	auto crowd = [](EntityWorld& w, int n) { for (int i = 0; i < n; ++i) w.Create("Tree " + std::to_string(i)); };

	for (const int members : { 4, 16, 60 })
	{
		EntityWorld entities;
		GameplayWorld gameplay;
		RegisterResourceContent(gameplay.Library, defs);
		BuildResources(entities, glm::vec3(0.0f), defs, 40, 20, 30, 30);
		crowd(entities, 3000);
		gameplay.Load(entities);
		Tribe tribe;
		tribe.Bind(entities, gameplay, items, recipes, defs);
		std::vector<std::unique_ptr<QuickBody>> bodies;
		const char* roles[] = { "forager", "woodcutter", "miner" };
		for (int i = 0; i < members; ++i)
		{
			bodies.push_back(std::make_unique<QuickBody>(glm::vec2(static_cast<float>(i), 0.0f)));
			tribe.AddMember("Npc" + std::to_string(i), roles[i % 3], bodies.back().get());
		}
		// Every member decides on every frame (the worst case; the game decides twice a second).
		tribe.Config.Simulation.DecisionSeconds = tribe.Config.Simulation.PerceptionSeconds = tribe.Config.Simulation.LodUpdateSeconds = 0.0f;
		const int frames = 120;
		double worst = 0.0;
		const auto t0 = Clock::now();
		for (int f = 0; f < frames; ++f)
		{
			for (auto& b : bodies) b->Update(dt);
			const auto a = Clock::now();
			tribe.Update(dt);
			worst = std::max(worst, Ms(a, Clock::now()));
		}
		const double ms = Ms(t0, Clock::now()) / frames;
		if (bench) std::fprintf(stderr, "  tribe %2d members, %3zu systems, %zu entities: %.3f ms per round of decisions (worst %.3f ms)\n", members, gameplay.Count(), entities.All().size(), ms, worst);
		Check(tribe.Members().size() == static_cast<size_t>(members), "the members are all there");
	}

	// rabbits among the plants
	for (const int rabbits : { 8, 40, 120 })
	{
		EntityWorld entities;
		GameplayWorld gameplay;
		RegisterResourceContent(gameplay.Library, defs);
		BuildResources(entities, glm::vec3(0.0f), defs, 40, 20, 30, 30);
		crowd(entities, 3000);
		gameplay.Load(entities);
		EcologySystem eco;
		eco.Species["rabbit"].Life.MaxAnimals = rabbits;
		eco.Bind(entities, gameplay);
		eco.Spawn({ 0.0f, 0.0f, 4.0f }, rabbits);
		double clock = 0.0;
		const int frames = 600;
		double ecoMs = 0.0, gameplayMs = 0.0;
		for (int f = 0; f < frames; ++f)
		{
			const auto a = Clock::now();
			gameplay.Update(dt);
			const auto b = Clock::now();
			eco.Update(clock, dt);
			const auto c = Clock::now();
			gameplayMs += Ms(a, b);
			ecoMs += Ms(b, c);
			clock += dt;
		}
		if (bench) std::fprintf(stderr, "  ecology %3d rabbits, %3zu systems, %zu entities: %.4f ms per frame (GameplayWorld::Update alone: %.4f ms)\n", rabbits, gameplay.Count(), entities.All().size(), ecoMs / frames, gameplayMs / frames);
		Check(eco.Count() <= rabbits, "the population stays under the limit");
	}

	// the listing of names on its own
	{
		EntityWorld entities;
		GameplayWorld gameplay;
		BuildResources(entities, glm::vec3(0.0f), defs, 100, 100, 100, 100);
		gameplay.Load(entities);
		const int rounds = 2000;
		size_t total = 0;
		const auto t0 = Clock::now();
		for (int i = 0; i < rounds; ++i) total += gameplay.Names().size();
		const double us = Ms(t0, Clock::now()) * 1000.0 / rounds;
		const auto t1 = Clock::now();
		size_t found = 0;
		for (int i = 0; i < rounds; ++i) found += entities.FindByName("fiber grass 100") != NullEntity ? 1 : 0;
		const double find = Ms(t1, Clock::now()) * 1000.0 / rounds;
		if (bench) std::fprintf(stderr, "  Names() with %zu systems: %.2f us; FindByName among %zu entities: %.2f us\n", gameplay.Count(), us, entities.All().size(), find);
		Check(total == static_cast<size_t>(rounds) * gameplay.Count() && found == static_cast<size_t>(rounds), "the listing and the search return what is there");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("scale: ok");
	return 0;
}
