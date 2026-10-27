// Seasons and basic ecology (H3): the season follows the clock, winter stops growth, and rabbits wander, eat the plants, breed when fed within a limit and
// starve when there is nothing to eat. Their whole state is attributes of entities, so the run is the same twice. CPU only.
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Save/SaveWorld.h"
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
	constexpr float dt = 0.1f;
}

using namespace GB;

struct Meadow
{
	explicit Meadow(bool plants)
	{
		RecipeBook recipes;
		RegisterEraOneItems(Items, recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		BuildResources(Entities, glm::vec3(0.0f), Defs, 0, 0, plants ? 6 : 0, plants ? 6 : 0);
		Gameplay.Load(Entities);
		Plants.Bind(Gameplay, Defs, {});
		Eco.Bind(Entities, Gameplay);
		Eco.Spawn({ 0.0f, 0.0f, 4.0f }, 4);
	}
	// Runs `seconds` more; keeps the population in view.
	void Run(double seconds)
	{
		const int steps = static_cast<int>(seconds / dt);
		for (int i = 0; i < steps; ++i)
		{
			Gameplay.Update(dt);
			Plants.Update(Gameplay, dt, SeasonClock::GrowthRate(SeasonClock().At(Clock)));   // plants regrow with the season
			Eco.Update(Clock, dt);
			Clock += dt;
			const int n = Eco.Count();
			if (n > Eco.Config.MaxAnimals || n < 0) bad = true;
			peak = std::max(peak, n);
			for (const std::string& name : Gameplay.Names())
			{
				const AbilitySystemComponent* a = Gameplay.Find(name);
				if (a->HasTag("Animal.Rabbit") && (a->Attributes().Current("Food") < 0.0f || a->Attributes().Current("Food") > Eco.Config.FoodMax)) bad = true;
				if (a->HasTag("Resource.Node") && ResourceSystem::Amount(*a) < 0) bad = true;
			}
		}
	}
	std::string Fingerprint() const { return Eco.Describe() + std::to_string(Clock); }
	ItemLibrary Items;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Plants;
	EcologySystem Eco;
	double Clock = 0.0;
	int peak = 0;
	bool bad = false;
};

int main()
{
	// Seasons.
	{
		const SeasonClock s;
		Check(s.At(0.0) == Season::Spring && s.At(250.0) == Season::Summer && s.At(500.0) == Season::Autumn && s.At(750.0) == Season::Winter && s.At(960.0) == Season::Spring, "the seasons follow the clock and the year repeats");
		Check(SeasonClock::GrowthRate(Season::Winter) == 0.0f && SeasonClock::GrowthRate(Season::Spring) > SeasonClock::GrowthRate(Season::Autumn), "winter stops growth, spring favours it");
		Check(SeasonClock::HungerRate(Season::Winter) > SeasonClock::HungerRate(Season::Summer), "winter is hungrier");
		Check(s.Progress(120.0) > 0.49f && s.Progress(120.0) < 0.51f, "progress through a season");
		Check(s.At(-5.0) == Season::Spring && s.Progress(-5.0) == 0.0f, "a clock before zero is the start of spring");
		SeasonClock odd;
		odd.SeasonSeconds = 0.0f;
		Check(odd.At(100.0) == Season::Spring && odd.Progress(100.0) == 0.0f, "a season of no length does not divide by zero");
		odd.SeasonSeconds = -3.0f;
		Check(odd.At(100.0) == Season::Spring && odd.Progress(100.0) == 0.0f, "nor does a negative one");
		Check(s.At(1e13) <= Season::Winter, "a clock of thousands of years still gives a season");
	}

	// What a season looks like: snow falls in winter and melts in spring, the foliage changes colour and blends into the next season's, the weather fits.
	{
		const SeasonClock s;
		Check(SeasonClock::SnowCover(Season::Summer, 0.5f) == 0.0f && SeasonClock::SnowCover(Season::Winter, 0.5f) > 0.9f, "no snow in summer, snow in winter");
		Check(SeasonClock::SnowCover(Season::Winter, 0.0f) < 0.05f && SeasonClock::SnowCover(Season::Winter, 0.15f) > 0.9f, "it falls over the first part of winter");
		Check(SeasonClock::SnowCover(Season::Spring, 0.0f) > 0.9f && SeasonClock::SnowCover(Season::Spring, 0.3f) == 0.0f, "and melts in early spring");
		Check(std::fabs(SeasonClock::SnowCover(Season::Autumn, 1.0f) - 0.3f) < 0.01f && SeasonClock::SnowCover(Season::Winter, 0.0f) < 0.05f, "(a dusting at the end of autumn, a jump of less than a third)");
		Check(glm::distance(SeasonClock::Foliage(Season::Summer, 0.3f), SeasonClock::Foliage(Season::Autumn, 0.3f)) > 0.3f, "leaves are another colour in autumn");
		Check(glm::distance(SeasonClock::Foliage(Season::Autumn, 1.0f), SeasonClock::Foliage(Season::Winter, 0.0f)) < 0.01f, "the foliage has no jump at a season's end");
		Check(std::string(SeasonClock::Weather(Season::Winter, 0.5f)) == "snow" && std::string(SeasonClock::Weather(Season::Summer, 0.1f)) == "clear" && std::string(SeasonClock::Weather(Season::Spring, 0.1f)) == "lightrain", "the weather fits the season");
		Check(std::string(SeasonClock::Weather(s.At(0.0), s.Progress(0.0))) == "lightrain", "a new world starts with a spring shower");
	}

	// With food: they spread out, eat, breed (within the limit) and the plants are used.
	{
		Meadow m(true);
		m.Run(240.0);   // spring
		Check(!m.bad, "no count out of bounds");
		Check(m.Eco.Count() >= 4, "a fed spring keeps the rabbits alive");
		Check(m.Eco.Eaten() > 0, "they eat the plants");
		m.Run(1200.0);
		Check(!m.bad, "no count out of bounds over five seasons");
		Check(m.Eco.Born() > 0, "they breed when well fed");
		Check(m.peak <= m.Eco.Config.MaxAnimals, "the population never passes the limit");
		std::fprintf(stderr, "  %s, peak %d\n", m.Eco.Describe().c_str(), m.peak);
	}

	// Without plants: they starve.
	{
		Meadow m(false);
		m.Run(900.0);
		Check(!m.bad, "no count out of bounds");
		Check(m.Eco.Count() == 0 && m.Eco.Starved() == 4, "with nothing to eat all four starve");
	}

	// Twice the same.
	{
		Meadow a(true), b(true);
		a.Run(1000.0);
		b.Run(1000.0);
		Check(a.Fingerprint() == b.Fingerprint(), "two runs end the same");
		bool same = true;
		for (const std::string& n : a.Gameplay.Names())
			if (const AbilitySystemComponent* x = a.Gameplay.Find(n); x && x->HasTag("Animal.Rabbit"))
			{
				const EntityId ia = a.Entities.FindByName(n), ib = b.Entities.FindByName(n);
				same = same && ib != NullEntity && a.Entities.Find(ia)->Transform.Position == b.Entities.Find(ib)->Transform.Position;
			}
		Check(same, "and every rabbit is in the same place");
	}

	// Saving in the middle of it: the animals and the plants are attributes of entities, so a copy made from what a save keeps (the scene with the
	// components written back, and the clocks of the systems) has the same rabbits, with the same food, eating the same plants.
	{
		Meadow a(true);
		a.Run(300.0);
		a.Gameplay.Store(a.Entities);
		const std::vector<Save::GameplayRuntime> runtime = Save::CaptureGameplay(a.Gameplay);
		EntityWorld entities = a.Entities;
		GameplayWorld loaded;
		RegisterResourceContent(loaded.Library, a.Defs);
		std::string error;
		Check(Save::RestoreGameplay(loaded, entities, runtime, &error), "the systems of a meadow come back from a save");
		Check(loaded.Count() == a.Gameplay.Count(), "with the same number of systems (rabbits and plants)");
		bool same = true;
		for (const std::string& n : a.Gameplay.Names())
		{
			const AbilitySystemComponent* x = a.Gameplay.Find(n);
			const AbilitySystemComponent* y = loaded.Find(n);
			if (!y) { same = false; continue; }
			for (const char* attribute : { "Food", "Age", "Cooldown", "Starving", "Amount", "Regrow" })
				same = same && x->Attributes().Has(attribute) == y->Attributes().Has(attribute) && std::fabs(x->Attributes().Current(attribute) - y->Attributes().Current(attribute)) < 1e-3f;
		}
		Check(same, "every rabbit and every plant has the same numbers after the load");
		ResourceSystem plants;
		plants.Bind(loaded, a.Defs, {});
		EcologySystem eco;
		eco.Bind(entities, loaded);
		Check(eco.Count() == a.Eco.Count(), "the same rabbits");
		// and the world goes on: the loaded one still feeds and keeps its limit (it need not be step for step the same: where a rabbit was heading is
		// not saved, so it picks a new target from where it stands)
		double clock = 300.0;
		for (int i = 0; i < 3000; ++i) { loaded.Update(dt); plants.Update(loaded, dt, SeasonClock::GrowthRate(SeasonClock().At(clock))); eco.Update(clock, dt); clock += dt; }
		Check(eco.Count() >= 1 && eco.Count() <= eco.Config.MaxAnimals, "a loaded meadow keeps going within its limit");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("ecology: ok");
	return 0;
}
