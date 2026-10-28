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
			const int n = Eco.Count("rabbit");   // (each species has its own limit)
			if (n > Eco.Species["rabbit"].Life.MaxAnimals || n < 0) bad = true;
			peak = std::max(peak, n);
			for (const std::string& name : Gameplay.Names())
			{
				const AbilitySystemComponent* a = Gameplay.Find(name);
				if (a->HasTag("Animal.Rabbit") && (a->Attributes().Current("Food") < 0.0f || a->Attributes().Current("Food") > Eco.Species["rabbit"].Life.FoodMax)) bad = true;
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
		Check(m.peak <= m.Eco.Species["rabbit"].Life.MaxAnimals, "the population never passes the limit");
		std::fprintf(stderr, "  %s, peak %d\n", m.Eco.Describe().c_str(), m.peak);
	}

	// Without plants: they starve.
	{
		Meadow m(false);
		m.Run(900.0);
		Check(!m.bad, "no count out of bounds");
		Check(m.Eco.Count() == 0 && m.Eco.Starved() == 4, "with nothing to eat all four starve");
	}

	// Species are data (assets/data/world.json, "species"): the file says what the code says, a damaged document changes nothing.
	{
		std::map<std::string, SpeciesDef> file;
		const std::string loaded = LoadSpeciesFile(std::string(GB_ASSETS_DIR) + "/data/world.json", file);
		Check(loaded.empty(), "the species of assets/data/world.json load");
		if (!loaded.empty()) std::fprintf(stderr, "  %s\n", loaded.c_str());
		Check(file == DefaultSpecies(), "and say what DefaultSpecies says");
		if (file != DefaultSpecies()) std::fprintf(stderr, "  expected:\n{ \"species\": %s }\n", SpeciesToJson(DefaultSpecies()).c_str());
		std::map<std::string, SpeciesDef> back;
		Check(LoadSpecies("{ \"species\": " + SpeciesToJson(DefaultSpecies()) + " }", back).empty() && back == DefaultSpecies(), "the species round-trip");
		Check(DefaultSpecies().at("rabbit").Life == EcologyConfig{} && !DefaultSpecies().at("rabbit").Hunts() && DefaultSpecies().at("fox").Hunts(), "the rabbit has the numbers it always had; the fox hunts");
		std::map<std::string, SpeciesDef> keep = DefaultSpecies();
		const char* bad[] = {
			R"({ "species": { "deer": { "name": "Deer", "plural": "deer", "tag": "Deer" } } })",                                        // not an Animal tag
			R"({ "species": { "deer": { "name": "Deer", "plural": "deer", "tag": "Animal.Deer", "speed": 0 } } })",                     // it cannot move
			R"({ "species": { "deer": { "name": "Deer", "plural": "deer", "tag": "Animal.Deer", "eats": ["Resource.Plant", "Animal.Rabbit"] } } })",   // both
			R"({ "species": { "wolf": { "name": "Wolf", "plural": "wolves", "tag": "Animal.Wolf", "eats": ["Animal.Wolf"] } } })",       // its own kind
			R"({ "species": { "hare": { "name": "Hare", "plural": "hares", "tag": "Animal.Rabbit" } } })",                              // the rabbits' tag
			R"({ "species": { "deer": { "name": "Deer", "plural": "deer", "tag": "Animal.Deer", "body": "dragon" } } })",
			R"({ "species": [] })",
			"nope",
		};
		bool refused = true;
		for (const char* text : bad) refused = refused && !LoadSpecies(text, keep).empty();
		Check(refused && keep == DefaultSpecies(), "a damaged species is refused and nothing changes");
		Check(LoadSpecies(R"({ "items": [] })", keep).empty() && keep == DefaultSpecies(), "a file without species changes nothing");
		Check(LoadSpecies(R"({ "species": { "deer": { "name": "Deer", "plural": "deer", "tag": "Animal.Deer", "body": "quadruped", "size": 1.6, "speed": 1.8, "maxAnimals": 4 } } })", keep).empty()
			&& keep.size() == 3 && keep.at("deer").Eats == std::vector<std::string>{ "Resource.Plant" }, "a grazing deer is a few lines of data");
	}

	// A second species: foxes hunt the rabbits. The rabbits caught are counted apart from the starved, nobody is eaten twice, the foxes live on them,
	// and with no foxes in the world the rabbits are left as they were.
	{
		Meadow m(true);
		m.Eco.Spawn({ 8.0f, 0.0f, 8.0f }, 1, "fox");
		Check(m.Eco.Count("fox") == 1 && m.Eco.Count("rabbit") == 4 && m.Eco.Count() == 5, "one fox among four rabbits");
		Check(m.Eco.Spawn({ 8.0f, 0.0f, 8.0f }, 5, "fox") == 1 && m.Eco.Count("fox") == 2, "foxes stay few (their own limit)");
		Check(m.Eco.Spawn({ 0.0f, 0.0f, 0.0f }, 3, "unicorn") == 0, "an unknown species spawns nothing");
		AbilitySystemComponent* fox = m.Gameplay.Find("Fox 1");
		Check(fox && fox->HasTag("Animal.Fox"), "the fox is an animal with its own tag");
		fox->Attributes().SetBase("Food", 30.0f);   // a hungry fox
		m.Gameplay.Find("Fox 2")->Attributes().SetBase("Food", 30.0f);
		m.Run(240.0);
		Check(!m.bad, "no count out of bounds with a hunter about");
		Check(m.Eco.Killed() > 0, "the foxes catch rabbits");
		const float foxFood = m.Gameplay.Find("Fox 1")->Attributes().Current("Food") + m.Gameplay.Find("Fox 2")->Attributes().Current("Food");
		Check(m.Eco.Count("fox") == 2 && foxFood > 2.0f * (30.0f - 0.12f * 240.0f) + 20.0f, "and live on them");
		const std::string described = m.Eco.Describe();
		Check(described.find(" rabbits (") != std::string::npos && described.find(" foxes (") != std::string::npos && described.find("caught by hunters") != std::string::npos, "the description tells both species");
		std::fprintf(stderr, "  %s\n", described.c_str());
		bool missing = false;   // what was caught is gone from the scene and the systems
		for (const auto& [id, e] : m.Entities.All())
			if (e.Name.rfind("Rabbit ", 0) == 0 && e.Name.find(' ', 7) == std::string::npos && !m.Gameplay.Find(e.Name)) missing = true;
		Check(!missing, "no rabbit is left half-removed");

		// the counters of each species survive a save
		EcologySystem eco;
		eco.Bind(m.Entities, m.Gameplay);
		const std::string text = m.Eco.ToJson();
		Check(eco.FromJson(text) && eco.ToJson() == text && eco.Killed() == m.Eco.Killed() && eco.Describe() == described, "both species' counters come back from a save");
		Check(eco.FromJson(R"({"born": 2, "starved": 1, "eaten": 5, "walks": []})") && eco.Born() == 2 && eco.Eaten() == 5 && eco.Killed() == 0, "an old save's counters are the rabbits'");
	}
	{
		Meadow a(true), b(true);
		b.Eco.Species.erase("fox");   // no hunter defined at all
		a.Run(600.0);
		b.Run(600.0);
		Check(a.Fingerprint() == b.Fingerprint(), "a fox the world does not have changes nothing for the rabbits");
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
		const std::string walks = a.Eco.ToJson();
		Check(eco.FromJson(walks), "where each rabbit was heading and the counters come back");
		Check(eco.ToJson() == walks, "and read back they are what was written");
		Check(eco.Count() == a.Eco.Count() && eco.Describe() == a.Eco.Describe(), "the same rabbits and the same counters");
		Check(!eco.FromJson("{") && !eco.FromJson("[]") && !eco.FromJson("{\"born\": \"many\"}"), "a damaged text is refused");
		Check(eco.ToJson() == walks, "and a refusal changes nothing");

		// and the world goes on exactly as the original does (with the counters and the headings saved there is nothing left to choose again)
		a.Run(300.0);
		double clock = 300.0;
		for (int i = 0, steps = static_cast<int>(300.0 / dt); i < steps; ++i) { loaded.Update(dt); plants.Update(loaded, dt, SeasonClock::GrowthRate(SeasonClock().At(clock))); eco.Update(clock, dt); clock += dt; }
		Check(eco.Describe() == a.Eco.Describe(), "after five more minutes the loaded meadow has the same births, deaths and meals");
		bool together = true;
		for (const std::string& n : a.Gameplay.Names())
			if (const AbilitySystemComponent* x = a.Gameplay.Find(n); x && x->HasTag("Animal.Rabbit"))
			{
				const EntityId ia = a.Entities.FindByName(n), ib = entities.FindByName(n);
				together = together && ib != NullEntity && a.Entities.Find(ia)->Transform.Position == entities.Find(ib)->Transform.Position;
			}
		Check(together, "and every rabbit stands where its original stands");
	}

	// The look the season wrote, as a save section.
	{
		SeasonLook look;
		look.Enabled = true;
		look.Weather = "lightrain";
		look.Snow = 0.35f;
		look.Leaf = { 0.8f, 0.45f, 0.1f };
		SeasonLook back;
		Check(back.FromJson(look.ToJson()) && back.Enabled && back.Weather == "lightrain" && back.Snow == 0.35f && back.Leaf == look.Leaf, "the look of the season survives a round trip exactly");
		Check(!back.FromJson("{}") && !back.FromJson("nope") && back.Weather == "lightrain", "a damaged look is refused and changes nothing");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("ecology: ok");
	return 0;
}
