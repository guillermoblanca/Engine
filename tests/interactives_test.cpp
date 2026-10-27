// Interactives with state (H3): a campfire that burns its fuel, a well that gives water and fills again, a door that opens and closes. A worker uses
// them through abilities with wood and water in its bag; the states are tags, the amounts stay in bounds, and two runs end the same. CPU only.
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Save/SaveWorld.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	constexpr float dt = 1.0f / 60.0f;
}

using namespace GB;

struct Camp
{
	Camp()
	{
		std::map<std::string, ResourceNodeDef> defs;
		RecipeBook recipes;
		RegisterEraOneItems(Items, recipes, defs);
		RegisterInteractiveContent(Gameplay.Library);
		BuildInteractives(Entities, glm::vec3(0.0f));
		const EntityId w = Entities.Create("Worker");
		Entities.Find(w)->Gameplay.emplace();
		Entities.Find(w)->Gameplay->Abilities = { "Stoke", "DrawWater", "OpenDoor", "CloseDoor" };
		Gameplay.Load(Entities);
		Rules.Bind(Gameplay,
			[this](const std::string&, const std::string& item, int n) { return Bag.Remove(item, n); },
			[this](const std::string&, const std::string& item, int n) { return Bag.Add(Items, item, n); });
	}
	void Frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			ResourceSystem::SyncTools(*Gameplay.Find("Worker"), Bag, Items);
			Gameplay.Update(dt);
			Rules.Update(dt, &Entities);
			Check(Gameplay.Find("Fire Pit")->Attributes().Current("Fuel") >= 0.0f && Gameplay.Find("Fire Pit")->Attributes().Current("Fuel") <= 10.0f, "fuel stays in bounds");
			Check(Gameplay.Find("Well")->Attributes().Current("Water") >= 0.0f && Gameplay.Find("Well")->Attributes().Current("Water") <= 6.0f, "water stays in bounds");
		}
	}
	bool Do(const char* ability, const char* target)
	{
		AbilitySystemComponent* a = Gameplay.Find("Worker");
		std::string why;
		if (!a->Activate(ability, Gameplay.Find(target), &why, target)) { Why = why; return false; }
		for (int i = 0; i < 60 * 10 && a->AnyActive(); ++i) Frames(1);
		return !a->AnyActive() && a->LastError().empty();
	}
	std::string State() const
	{
		return InteractiveSystem::CampfireState(*Gameplay.Find("Fire Pit")) + "|" + InteractiveSystem::DoorState(*Gameplay.Find("Gate")) + "|" + Bag.ToJson()
			+ "|" + std::to_string(Gameplay.Find("Well")->Attributes().Current("Water")) + "|" + std::to_string(Gameplay.Find("Fire Pit")->Attributes().Current("Fuel"));
	}
	ItemLibrary Items;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	InteractiveSystem Rules;
	Inventory Bag{ 8, 0.0f };
	std::string Why;
};

static std::string Day(bool& ok)
{
	Camp c;
	auto step = [&](bool good, const char* what) { if (!good) std::fprintf(stderr, "  (day step failed: %s)\n", what); ok = ok && good; };
	const AbilitySystemComponent& fire = *c.Gameplay.Find("Fire Pit");

	step(InteractiveSystem::CampfireState(fire) == "unlit", "starts unlit");
	step(!c.Do("Stoke", "Fire Pit"), "no wood, no stoking");
	c.Bag.Add(c.Items, "wood", 2);
	c.Frames(2);
	step(c.Do("Stoke", "Fire Pit") && c.Bag.Count("wood") == 1, "a piece of wood goes in");
	step(InteractiveSystem::CampfireState(fire) == "lit", "it is lit");
	const EntityId flame = c.Entities.FindByName("Fire Pit Flame");
	step(flame != NullEntity && c.Entities.Find(flame)->Active, "the flame shows");
	c.Frames(60);
	step(c.Do("Stoke", "Fire Pit") && c.Bag.Count("wood") == 0, "a second piece");
	step(fire.Attributes().Current("Fuel") > 3.0f, "fuel adds up");
	c.Frames(60 * 30);
	step(InteractiveSystem::CampfireState(fire) == "out", "it burns out");
	step(!c.Entities.Find(flame)->Active, "the flame goes");
	step(!c.Do("Stoke", "Fire Pit"), "out of wood again");

	step(c.Do("DrawWater", "Well") && c.Bag.Count("water") == 2, "water from the well");
	c.Frames(60);
	step(c.Do("DrawWater", "Well"), "a second draw");
	c.Frames(60);
	step(c.Do("DrawWater", "Well") && c.Bag.Count("water") == 6, "a third draw empties the well");
	c.Frames(60);
	step(!c.Do("DrawWater", "Well"), "an empty well gives nothing");
	c.Frames(60 * 21);
	step(c.Do("DrawWater", "Well") && c.Bag.Count("water") == 7, "after a while there is a little water again");
	c.Frames(60 * 130);
	step(c.Gameplay.Find("Well")->Attributes().Current("Water") >= 5.99f, "the well fills again");

	step(c.Do("OpenDoor", "Gate") && InteractiveSystem::DoorState(*c.Gameplay.Find("Gate")) == "open", "door opens");
	step(!c.Do("OpenDoor", "Gate"), "an open door cannot be opened");
	c.Frames(60);
	const EntityId hinge = c.Entities.FindByName("Gate Hinge");
	step(c.Entities.Find(hinge)->Transform.Rotation.y > 90.0f, "the panel turns");
	step(c.Do("CloseDoor", "Gate") && InteractiveSystem::DoorState(*c.Gameplay.Find("Gate")) == "closed", "door closes");
	c.Frames(60);
	step(c.Entities.Find(hinge)->Transform.Rotation.y < 1.0f, "and swings back");
	return c.State();
}

int main()
{
	bool ok1 = true, ok2 = true;
	const std::string a = Day(ok1), b = Day(ok2);
	Check(ok1 && ok2, "the day goes as planned");
	Check(a == b, "two runs end the same");
	// A save in the middle of the day: the states are tags and the amounts attributes, so they come back with the entity; what is not saved is the
	// partial second of burning (the timer of the system), which starts again.
	{
		Camp c;
		c.Bag.Add(c.Items, "wood", 2);
		c.Frames(2);
		Check(c.Do("Stoke", "Fire Pit") && c.Do("OpenDoor", "Gate"), "a fire and an open door to save");
		c.Frames(60 * 3);
		c.Gameplay.Store(c.Entities);
		const std::vector<Save::GameplayRuntime> runtime = Save::CaptureGameplay(c.Gameplay);
		EntityWorld entities = c.Entities;
		GameplayWorld loaded;
		RegisterInteractiveContent(loaded.Library);
		std::string error;
		Check(Save::RestoreGameplay(loaded, entities, runtime, &error), "the interactives come back from a save");
		InteractiveSystem rules;
		rules.Bind(loaded, [](const std::string&, const std::string&, int) { return 0; }, [](const std::string&, const std::string&, int) { return 0; });
		Check(InteractiveSystem::CampfireState(*loaded.Find("Fire Pit")) == "lit" && InteractiveSystem::DoorState(*loaded.Find("Gate")) == "open", "a lit fire and an open door stay so");
		Check(std::fabs(loaded.Find("Fire Pit")->Attributes().Current("Fuel") - c.Gameplay.Find("Fire Pit")->Attributes().Current("Fuel")) < 1e-3f, "with the same fuel");
		for (int i = 0; i < 60 * 40; ++i) { loaded.Update(dt); rules.Update(dt, &entities); }
		Check(InteractiveSystem::CampfireState(*loaded.Find("Fire Pit")) == "out", "the loaded fire burns out as it would have");
		const EntityId flame = entities.FindByName("Fire Pit Flame");
		Check(flame != NullEntity && !entities.Find(flame)->Active, "and its flame goes");
		Check(entities.Find(entities.FindByName("Gate Hinge"))->Transform.Rotation.y > 90.0f, "a door loaded open turns its panel open");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("interactives: ok");
	return 0;
}
