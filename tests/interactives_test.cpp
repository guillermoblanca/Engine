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
		Rules.Items = &Items;   // Stoke burns what the items say (E1)
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
	// Interactives are data (assets/data/world.json, "interactives"): the file says what the code says, a damaged document changes nothing, and a
	// new interactive of a known kind (a bigger hearth) needs no code.
	{
		std::map<std::string, InteractiveDef> fileDefs;
		const std::string loaded = LoadInteractivesFile(std::string(GB_ASSETS_DIR) + "/data/world.json", fileDefs);
		Check(loaded.empty(), "the interactives of assets/data/world.json load");
		if (!loaded.empty()) std::fprintf(stderr, "  %s\n", loaded.c_str());
		Check(fileDefs == DefaultInteractives(), "and say what DefaultInteractives says");
		if (fileDefs != DefaultInteractives()) std::fprintf(stderr, "  expected:\n{ \"interactives\": %s }\n", InteractivesToJson(DefaultInteractives()).c_str());
		std::map<std::string, InteractiveDef> back;
		Check(LoadInteractives("{ \"interactives\": " + InteractivesToJson(DefaultInteractives()) + " }", back).empty() && back == DefaultInteractives(), "the interactives round-trip");

		std::map<std::string, InteractiveDef> keep = DefaultInteractives();
		const char* bad[] = {
			R"({ "interactives": { "a": { "kind": "oven", "name": "Oven", "tags": ["Interactive.Oven"] } } })",                                        // no such behaviour
			R"({ "interactives": { "a": { "kind": "campfire", "name": "Hearth", "tags": ["Interactive.Campfire"] } } })",                              // a campfire without Fuel
			R"({ "interactives": { "a": { "kind": "well", "name": "W", "tags": ["State.Full"], "attributes": { "Water": { "value": 1, "max": 2 } } } } })",   // without its kind's tag
			R"({ "interactives": { "a": { "kind": "well", "name": "W", "tags": ["Interactive.Well"], "attributes": { "Water": { "value": 9, "max": 2 } } } } })",   // more than it holds
			R"({ "interactives": { "a": { "kind": "door", "name": "D", "tags": ["Interactive.Door"], "abilities": [ { "name": "Knock", "event": "Event.Fuel.Add" } ] } } })",   // an event it does not hear
			R"({ "interactives": { "fire_pit": { "kind": "campfire", "name": "Fire Pit", "tags": ["Interactive.Campfire"], "attributes": { "Fuel": { "value": 0, "max": 10 } }, "burnSeconds": 0 } } })",
			R"({ "interactives": [] })",
			"{",
		};
		bool refused = true;
		for (const char* text : bad) refused = refused && !LoadInteractives(text, keep).empty();
		Check(refused && keep == DefaultInteractives(), "a damaged interactive is refused and nothing changes");
		Check(LoadInteractives(R"({ "roles": {} })", keep).empty() && keep == DefaultInteractives(), "a file without interactives changes nothing");

		// a hearth that holds more and burns slower, with an ability of its own: data only
		Check(LoadInteractives(R"({ "interactives": { "hearth": { "kind": "campfire", "name": "Hearth", "at": [0, 0, 10], "stand": [0, 0, -2],
			"tags": ["Interactive.Campfire", "State.Unlit"], "attributes": { "Fuel": { "value": 0, "min": 0, "max": 30 } }, "burnSeconds": 20, "fuelPerWood": 3,
			"abilities": [ { "name": "Feed", "montage": "plant", "notify": "stoke", "event": "Event.Fuel.Add", "requires": ["Item.Resource.Wood"] } ] } } })", keep).empty(), "a new campfire from data");
		Camp c;
		c.Rules.Defs = keep;
		RegisterInteractiveContent(c.Gameplay.Library, keep);
		BuildInteractives(c.Entities, glm::vec3(0.0f), keep);
		c.Gameplay.Load(c.Entities);
		c.Gameplay.Find("Worker")->Grant("Feed");
		c.Rules.Bind(c.Gameplay, [&c](const std::string&, const std::string& item, int n) { return c.Bag.Remove(item, n); }, [&c](const std::string&, const std::string& item, int n) { return c.Bag.Add(c.Items, item, n); });
		c.Rules.Items = nullptr;   // plain wood: the hearth's own fuelPerWood
		c.Bag.Add(c.Items, "wood", 1);
		c.Frames(2);
		Check(c.Gameplay.Find("Hearth") && c.Gameplay.Find("Hearth")->Attributes().Max("Fuel") == 30.0f, "it holds what its data says");
		Check(c.Do("Feed", "Hearth") && c.Gameplay.Find("Hearth")->Attributes().Current("Fuel") == 3.0f, "its own ability feeds it with its own heat per wood");
		c.Frames(60 * 19);
		Check(c.Gameplay.Find("Hearth")->Attributes().Current("Fuel") == 3.0f && InteractiveSystem::CampfireState(*c.Gameplay.Find("Hearth")) == "lit", "and it burns at its own pace");
		c.Frames(60 * 2);
		Check(c.Gameplay.Find("Hearth")->Attributes().Current("Fuel") == 2.0f, "one unit every 20 s");

		// an interactive made after Bind (the tribe lights its own fire pit) is picked up by the next update
		const EntityId late = BuildInteractive(c.Entities, DefaultInteractives().at("fire_pit"), "Late Fire", NullEntity, glm::vec3(20.0f, 0.0f, 0.0f));
		c.Gameplay.Create("Late Fire")->FromComponent(*c.Entities.Find(late)->Gameplay);
		c.Bag.Add(c.Items, "wood", 1);
		c.Frames(2);
		Check(c.Do("Stoke", "Late Fire") && InteractiveSystem::CampfireState(*c.Gameplay.Find("Late Fire")) == "lit", "a fire pit made after Bind takes wood and burns");
	}

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
		const std::string timers = c.Rules.ToJson();
		Check(rules.FromJson(timers) && rules.ToJson() == timers, "the burn timer comes back as it was written");
		Check(!rules.FromJson("{") && !rules.FromJson("{\"timers\": 3}") && rules.ToJson() == timers, "a damaged timer text is refused and changes nothing");
		entities.Find(entities.FindByName("Gate Hinge"))->Transform.Rotation.y = 0.0f;   // (the panel of the saved copy was already open: start it shut)
		int outAt = -1, originalOutAt = -1;
		for (int i = 0; i < 60 * 40; ++i)
		{
			loaded.Update(dt); rules.Update(dt, &entities);
			c.Gameplay.Update(dt); c.Rules.Update(dt, &c.Entities);
			if (outAt < 0 && InteractiveSystem::CampfireState(*loaded.Find("Fire Pit")) == "out") outAt = i;
			if (originalOutAt < 0 && InteractiveSystem::CampfireState(*c.Gameplay.Find("Fire Pit")) == "out") originalOutAt = i;
		}
		Check(outAt >= 0 && outAt == originalOutAt, "the loaded fire goes out on the very tick the original does (the burn timer was saved)");
		Check(InteractiveSystem::CampfireState(*loaded.Find("Fire Pit")) == "out", "the loaded fire burns out as it would have");
		const EntityId flame = entities.FindByName("Fire Pit Flame");
		Check(flame != NullEntity && !entities.Find(flame)->Active, "and its flame goes");
		Check(entities.Find(entities.FindByName("Gate Hinge"))->Transform.Rotation.y > 90.0f, "a door loaded open turns its panel open");
	}

	// Fuel per item (E1): a branch is wood too (it feeds the fire) but gives half the heat, so it takes more of them for the same fire.
	{
		auto fuelAfter = [](const char* item, int pieces, int stokes)
		{
			Camp c;
			c.Bag.Add(c.Items, item, pieces);
			c.Frames(2);
			for (int i = 0; i < stokes; ++i) { Check(c.Do("Stoke", "Fire Pit"), "stoke with what is carried"); c.Frames(30); }
			return c.Gameplay.Find("Fire Pit")->Attributes().Current("Fuel");
		};
		const float wood = fuelAfter("wood", 2, 2), branches = fuelAfter("branch", 2, 2), fourBranches = fuelAfter("branch", 4, 4);
		Check(wood > 3.5f && branches > 1.5f && branches < wood - 1.0f, "two branches give less heat than two pieces of wood");
		Check(fourBranches > branches + 0.9f && fourBranches <= wood + 0.5f && fourBranches > wood - 1.5f, "it takes twice the branches for about the same heat (a unit burns away meanwhile)");

		Camp c;
		c.Bag.Add(c.Items, "branch", 1);
		c.Bag.Add(c.Items, "wood", 1);
		c.Frames(2);
		Check(c.Do("Stoke", "Fire Pit") && c.Bag.Count("wood") == 0 && c.Bag.Count("branch") == 1, "the piece with the most heat goes in first");
		Check(c.Gameplay.Find("Worker")->HasTag("Item.Resource.Wood"), "a branch is tagged as wood too");
		c.Frames(30);
		Check(c.Do("Stoke", "Fire Pit") && c.Bag.Count("branch") == 0, "then the branch");

		// without an item library the system behaves as before: "wood", FuelPerWood
		Camp legacy;
		legacy.Rules.Items = nullptr;
		legacy.Bag.Add(legacy.Items, "branch", 3);
		legacy.Frames(2);
		legacy.Do("Stoke", "Fire Pit");
		Check(legacy.Bag.Count("branch") == 3 && legacy.Gameplay.Find("Fire Pit")->Attributes().Current("Fuel") == 0.0f, "without a library only wood goes in");
		legacy.Bag.Add(legacy.Items, "wood", 1);
		legacy.Frames(2);
		Check(legacy.Do("Stoke", "Fire Pit") && legacy.Gameplay.Find("Fire Pit")->Attributes().Current("Fuel") > 1.5f, "and it gives the default heat");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("interactives: ok");
	return 0;
}
