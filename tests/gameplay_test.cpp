// Gameplay Ability System: tags, attributes, effects (instant, timed, periodic), abilities (requirements, cost, cooldown, target, events),
// ability tasks, scenes with gameplay components, and the whole garden loop (dig, sow, water, grow, harvest) run twice with the same result.
// CPU only; the character parts use the generated test humanoid, and the real Mixamo packs when GB_MIXAMO_DIR / GB_FARMING_DIR are set.
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Game/SceneIO.h"
#include "GB/Game/TaskList.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/Garden.h"
#include "GB/Log.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
	constexpr float dt = 1.0f / 60.0f;
}

using namespace GB;

// Runs a bare ability system (no character) for `seconds`.
static void Run(AbilitySystemComponent& a, float seconds)
{
	const int frames = static_cast<int>(std::lround(seconds / dt));
	for (int i = 0; i < frames; ++i) a.Update(dt);
}

// The garden as a game would run it, without a character: abilities run on timers.
struct Garden
{
	Garden()
	{
		RegisterGardenContent(Gameplay.Library);
		Layout = BuildGarden(Entities, glm::vec3(0.0f));
		Gameplay.Load(Entities);
		Rules.Bind(Gameplay, &Entities);
	}
	void Frames(int n)
	{
		for (int i = 0; i < n; ++i) { Gameplay.Update(dt); Rules.Update(dt); }
	}
	// Activates and runs until it ends. Returns whether it started and ended well.
	bool Do(const std::string& who, const std::string& ability, const std::string& target)
	{
		AbilitySystemComponent* a = Gameplay.Find(who);
		std::string why;
		if (!a->Activate(ability, Gameplay.Find(target), &why)) { std::fprintf(stderr, "  (%s on %s: %s)\n", ability.c_str(), target.c_str(), why.c_str()); return false; }
		for (int i = 0; i < 60 * 30 && a->AnyActive(); ++i) Frames(1);
		return !a->AnyActive() && a->LastError().empty();
	}
	std::string State() const
	{
		std::string s;
		for (const std::string& n : Gameplay.Names()) s += Gameplay.Describe(n) + " | ";
		for (const std::string& n : Gameplay.Names())
			for (const auto& [tag, count] : Gameplay.Find(n)->Tags().All()) s += n + ":" + tag + "x" + std::to_string(count) + " ";
		return s;
	}
	EntityWorld Entities;
	GameplayWorld Gameplay;
	GardenSystem Rules;
	GardenLayout Layout;
};

// One full day of work in the garden; the final state as text.
static std::string GardenLoop(bool* ok)
{
	Garden g;
	AbilitySystemComponent* me = g.Gameplay.Find("Gardener");
	AbilitySystemComponent* plot = g.Gameplay.Find("Plot 1");
	*ok = true;
	auto step = [&](bool good) { *ok = *ok && good; };
	step(GardenSystem::SoilState(*plot) == "grass");
	step(g.Do("Gardener", "Dig", "Plot 1"));
	step(GardenSystem::SoilState(*plot) == "dug");
	step(g.Do("Gardener", "Sow", "Plot 1"));
	step(GardenSystem::SoilState(*plot) == "sown");
	step(me->Attribute("Seeds") == 5.0f);
	step(g.Do("Gardener", "Water", "Plot 1"));
	step(plot->HasTag("State.Wet") && Near(plot->Attribute("Moisture"), 1.0f, 0.2f));
	g.Frames(60 * 5);   // 4 s to sprout, the moisture lasts 10 s
	step(GardenSystem::SoilState(*plot) == "sprout");
	step(g.Do("Gardener", "Water", "Plot 1"));
	g.Frames(60 * 6);
	step(GardenSystem::SoilState(*plot) == "grown");
	step(g.Do("Gardener", "Pull", "Plot 1"));
	step(me->Attribute("Crops") == 1.0f && GardenSystem::SoilState(*plot) == "dug");
	step(g.Do("Gardener", "PickFruit", "Fruit Tree"));
	step(g.Do("Gardener", "PickFruit", "Fruit Tree"));
	step(me->Attribute("Fruit") == 2.0f);
	step(g.Do("Gardener", "Milk", "Cow"));
	step(me->Attribute("Milk") == 1.0f);
	return g.State();
}

// A character as the game layer sees it (the interaction test has the same one).
struct Actor
{
	explicit Actor(Model& model) : Instance(model), Anim(model, Instance)
	{
		Anim.Default().AddClip("test_humanoid");
	}
	void Step(const CharacterInput& in, const CharacterWorld& world)
	{
		Controller.Step(in, world, dt);
		const glm::mat4 placement = glm::rotate(glm::translate(glm::mat4(1.0f), Controller.GetPosition()), Controller.GetYaw(), glm::vec3(0, 1, 0));
		Anim.Update(Tracker.Update(Controller, dt), dt, placement);
	}
	ModelInstance Instance;
	AnimInstance Anim;
	CharacterController Controller;
	MotionTracker Tracker;
};

int main()
{
	Log::Init();

	// ---- tags
	{
		GameplayTag t("State.Carrying.Box");
		Check(t.Valid() && t.Parent().Name() == "State.Carrying" && t.Parent().Parent().Name() == "State" && t.Parent().Parent().Parent().Name().empty(), "a tag knows its parents");
		Check(!GameplayTag("").Valid() && !GameplayTag("A..B").Valid() && !GameplayTag(".A").Valid() && !GameplayTag("A B").Valid(), "bad names are not tags");
		Check(GameplayTag("State.Carrying").Matches("State.Carrying.Box") && GameplayTag("State.Carrying.Box").Matches("State.Carrying.Box"), "a tag matches itself and what is below it");
		Check(!GameplayTag("State.Carrying.Box").Matches("State.Carrying") && !GameplayTag("State.Carry").Matches("State.Carrying.Box"), "not what is above it, and a prefix must end at a dot");
		GameplayTagContainer c;
		c.Add("State.Carrying.Box");
		c.Add("State.Carrying.Box");
		c.Add("State.Busy");
		c.Add("not valid");
		Check(c.Has("State.Carrying") && c.Has("State") && c.Has("State.Busy") && !c.Has("State.Wet") && !c.Has("Stat"), "the container matches by prefix");
		Check(c.Count("State.Carrying.Box") == 2 && c.Names().size() == 2, "tags are counted and invalid ones are refused");
		c.Remove("State.Carrying.Box");
		Check(c.Has("State.Carrying"), "one holder less keeps the tag");
		c.Remove("State.Carrying.Box");
		Check(!c.Has("State.Carrying") && c.Has("State.Busy"), "and none takes it away");
		c.Add("A.B");
		Check(c.HasAll({ "A", "State.Busy" }) && !c.HasAll({ "A", "Z" }) && c.HasAny({ "Z", "A.B" }) && !c.HasAny({ "Z", "Y" }), "all / any queries");
		Check(c.RemoveAll("A.B") && !c.Has("A") && !c.RemoveAll("A.B"), "remove all of one tag");
	}

	// ---- attributes
	{
		AttributeSet s;
		s.Define("Energy", 120.0f, 0.0f, 100.0f);
		s.Define("Seeds", 3.0f, 0.0f, 9.0f);
		Check(s.Base("Energy") == 100.0f && s.Current("Energy") == 100.0f && !s.Has("Nope") && s.Current("Nope") == 0.0f, "the base is clamped into its range");
		s.AddBase("Energy", -30.0f);
		s.AddBase("Seeds", 50.0f);
		Check(s.Current("Energy") == 70.0f && s.Current("Seeds") == 9.0f, "changes are clamped to the maximum");
		s.AddBase("Energy", -500.0f);
		Check(s.Current("Energy") == 0.0f, "and to the minimum");
		s.SetBase("Energy", 50.0f);
		s.AddModifier(1, { "Energy", ModOp::Add, 10.0f });
		s.AddModifier(2, { "Energy", ModOp::Multiply, 2.0f });
		Check(Near(s.Current("Energy"), 120.0f - 20.0f), "modifiers: (base + add) * multiply, clamped");
		Check(s.Base("Energy") == 50.0f, "they do not touch the base");
		s.RemoveModifiers(2);
		Check(Near(s.Current("Energy"), 60.0f), "taking a source away takes its modifiers away");
		s.AddModifier(3, { "Energy", ModOp::Override, 7.0f });
		Check(s.Current("Energy") == 7.0f, "an override wins");
		s.RemoveModifiers(1);
		s.RemoveModifiers(3);
		Check(s.Current("Energy") == 50.0f, "and goes away");
		s.ApplyInstant({ "Energy", ModOp::Multiply, 0.5f });
		Check(s.Base("Energy") == 25.0f, "instant multiply changes the base");
		Check(s.CanAfford({ { "Energy", ModOp::Add, -25.0f } }) && !s.CanAfford({ { "Energy", ModOp::Add, -25.5f } }) && !s.CanAfford({ { "Missing", ModOp::Add, -1.0f } }), "affording a cost");
	}

	// ---- effects
	{
		GameplayLibrary lib;
		GameplayEffectDef heal{ "Heal", EffectPolicy::Instant, 0.0f, 0.0f, { { "Energy", ModOp::Add, 30.0f } }, { "State.Rested" }, {}, true };
		lib.AddEffect(heal);
		GameplayEffectDef boost{ "Boost", EffectPolicy::Duration, 2.0f, 0.0f, { { "Speed", ModOp::Multiply, 2.0f } }, { "State.Fast" }, {}, true };
		lib.AddEffect(boost);
		GameplayEffectDef regen{ "Regen", EffectPolicy::Duration, 3.0f, 1.0f, { { "Energy", ModOp::Add, 5.0f } }, { "State.Regen" }, {}, true };
		lib.AddEffect(regen);
		GameplayEffectDef aura{ "Aura", EffectPolicy::Infinite, 0.0f, 0.0f, {}, { "State.Aura" }, { "State.Rested" }, true };
		lib.AddEffect(aura);
		GameplayEffectDef stack{ "Stack", EffectPolicy::Infinite, 0.0f, 0.0f, {}, { "State.Stack" }, {}, false };
		lib.AddEffect(stack);
		AbilitySystemComponent a("A", lib);
		a.Attributes().Define("Energy", 10.0f, 0.0f, 100.0f);
		a.Attributes().Define("Speed", 4.0f, 0.0f, 20.0f);

		Check(a.ApplyEffect("Heal") && a.Attribute("Energy") == 40.0f && a.HasTag("State.Rested") && a.ActiveEffects().empty(), "an instant effect changes the base and leaves its tags");
		Check(!a.ApplyEffect("Nothing"), "an unknown effect is refused");
		a.ApplyEffect("Boost");
		Check(a.Attribute("Speed") == 8.0f && a.Attributes().Base("Speed") == 4.0f && a.HasTag("State.Fast"), "a timed effect modifies while it lasts");
		Run(a, 1.0f);
		Check(a.Attribute("Speed") == 8.0f, "still on after 1 s");
		Run(a, 1.05f);
		Check(a.Attribute("Speed") == 4.0f && !a.HasTag("State.Fast") && a.ActiveEffects().empty(), "gone after its duration, with the tag");
		a.ApplyEffect("Boost");
		Run(a, 1.5f);
		a.ApplyEffect("Boost");   // refreshed, not doubled
		Run(a, 1.5f);
		Check(a.Attribute("Speed") == 8.0f && a.ActiveEffects().size() == 1, "applying a unique effect again refreshes it");
		Run(a, 0.6f);
		Check(a.Attribute("Speed") == 4.0f, "and it lasts a whole new duration");

		const float before = a.Attribute("Energy");
		a.ApplyEffect("Regen");
		Run(a, 3.2f);
		Check(Near(a.Attribute("Energy"), before + 15.0f), "a periodic effect applies its change every period (3 times in 3 s)");
		Check(!a.HasTag("State.Regen"), "and ends");

		a.ApplyEffect("Aura");
		Check(a.HasTag("State.Aura") && !a.HasTag("State.Rested"), "an effect can take tags away");
		Run(a, 30.0f);
		Check(a.HasTag("State.Aura"), "an endless effect stays");
		Check(a.RemoveEffect("Aura") == 1 && !a.HasTag("State.Aura"), "until it is removed");
		a.ApplyEffect("Stack");
		a.ApplyEffect("Stack");
		Check(a.ActiveEffects().size() == 2 && a.Tags().Count("State.Stack") == 2, "a non-unique effect stacks");
		a.RemoveEffect("Stack");
		Check(!a.HasTag("State.Stack"), "removing them takes every copy");
		const GameplayEffectDef cd = MakeCooldownEffect("Dig", 2.0f);
		Check(cd.Name == "Cooldown.Dig" && cd.GrantedTags[0] == "Cooldown.Ability.Dig" && cd.Policy == EffectPolicy::Duration, "the cooldown is an effect that grants a tag");
	}

	// ---- abilities
	{
		GameplayLibrary lib;
		lib.AddEffect(MakeCostEffect("Cost.Chop", "Energy", 10.0f));
		lib.AddEffect({ "Carry", EffectPolicy::Infinite, 0.0f, 0.0f, {}, { "State.Carrying.Log" }, {}, true });
		lib.AddEffect({ "Splinter", EffectPolicy::Instant, 0.0f, 0.0f, { { "Wood", ModOp::Add, 1.0f } }, {}, {}, true });
		lib.AddEffect({ "Dent", EffectPolicy::Instant, 0.0f, 0.0f, { { "Health", ModOp::Add, -1.0f } }, {}, {}, true });
		AbilityDef chop;
		chop.Name = "Chop";
		chop.Tag = "Ability.Chop";
		chop.RequiredTags = { "State.Strong" };
		chop.BlockedTags = { "State.Busy", "State.Carrying" };
		chop.ActiveTags = { "State.Busy" };
		chop.NeedsTarget = true;
		chop.TargetRequiredTags = { "State.Tree" };
		chop.TargetBlockedTags = { "State.Dead" };
		chop.TargetMinAttributes = { { "Health", 1.0f } };
		chop.Cost = "Cost.Chop";
		chop.CooldownSeconds = 2.0f;
		chop.Montages = { "chop" };
		chop.Duration = 1.0f;
		chop.Events = { { "hit", 0.5f, "Event.Chop.Hit", { "Splinter" }, { "Dent" } } };
		lib.AddAbility(chop);
		AbilityDef pick;
		pick.Name = "PickUp";
		pick.Tag = "Ability.PickUp";
		pick.BlockedTags = { "State.Carrying" };
		pick.ActivationEffects = { "Carry" };
		lib.AddAbility(pick);

		AbilitySystemComponent me("Me", lib), tree("Tree", lib);
		me.Attributes().Define("Energy", 25.0f, 0.0f, 100.0f);
		me.Attributes().Define("Wood", 0.0f, 0.0f, 9.0f);
		tree.Attributes().Define("Health", 3.0f, 0.0f, 3.0f);
		tree.Tags().Add("State.Tree");
		std::string why;
		Check(!me.Activate("Chop", &tree, &why) && why.find("granted") != std::string::npos, "an ability must be granted");
		Check(me.Grant("Chop") && me.Grant("PickUp") && !me.Grant("Nothing") && me.HasAbility("Chop"), "granting");
		Check(!me.CanActivate("Chop", &tree, &why) && why.find("State.Strong") != std::string::npos, "required tags: says what is missing");
		me.Tags().Add("State.Strong");
		Check(!me.CanActivate("Chop", nullptr, &why) && why.find("target") != std::string::npos, "a target is needed");
		Check(me.CanActivate("Chop", &tree, &why), "all met");
		tree.Tags().Add("State.Dead");
		Check(!me.CanActivate("Chop", &tree, &why) && why.find("State.Dead") != std::string::npos, "a blocked tag of the target stops it");
		tree.Tags().RemoveAll("State.Dead");
		tree.Attributes().SetBase("Health", 0.0f);
		Check(!me.CanActivate("Chop", &tree, &why) && why.find("Health") != std::string::npos, "so does an attribute the target lacks");
		tree.Attributes().SetBase("Health", 3.0f);
		me.Tags().Add("State.Carrying.Log");
		Check(!me.CanActivate("Chop", &tree, &why), "a blocked tag of the owner (prefix: State.Carrying) stops it");
		me.Tags().RemoveAll("State.Carrying.Log");

		Check(me.Activate("Chop", &tree, &why), "it starts");
		Check(me.Attribute("Energy") == 15.0f, "the cost is paid when it commits");
		Check(me.IsActive("Chop") && me.HasTag("Ability.Chop") && me.HasTag("State.Busy"), "running: its tag and the busy tag");
		Check(!me.CanActivate("Chop", &tree, &why), "it cannot start twice");
		Run(me, 0.4f);
		Check(me.Attribute("Wood") == 0.0f && tree.Attribute("Health") == 3.0f, "nothing happens before the notify");
		Run(me, 0.2f);
		Check(me.Attribute("Wood") == 1.0f && tree.Attribute("Health") == 2.0f, "at the notify the effects land on both");
		Run(me, 0.5f);
		Check(!me.AnyActive() && !me.HasTag("State.Busy") && !me.HasTag("Ability.Chop") && me.Completed() == 1, "it ends and gives the tags back");
		Check(me.Attribute("Wood") == 1.0f, "the event did not repeat at the end");
		Check(!me.CanActivate("Chop", &tree, &why) && why.find("cooling") != std::string::npos && me.CooldownRemaining("Chop") > 0.5f, "the cooldown holds it");
		Run(me, 2.1f);
		Check(me.CooldownRemaining("Chop") == 0.0f && me.CanActivate("Chop", &tree, &why), "and lets go");
		me.Attributes().SetBase("Energy", 5.0f);
		Check(!me.CanActivate("Chop", &tree, &why) && why.find("Energy") != std::string::npos, "an unaffordable cost stops it and names the attribute");
		me.Attributes().SetBase("Energy", 50.0f);

		// cancel before the notify: no effect, no end effects, the tags go back, but the cost was paid
		Check(me.Activate("Chop", &tree, &why), "again");
		Run(me, 0.2f);
		Check(me.Cancel("Chop") && !me.AnyActive() && !me.HasTag("State.Busy") && me.Attribute("Wood") == 1.0f && me.LastError().find("cancelled") != std::string::npos, "cancelling stops it before its effect");
		Check(me.CooldownRemaining("Chop") > 0.0f, "the cooldown was already paid");

		// an ability with no animation and no target happens at once
		Check(me.Activate("PickUp", nullptr, &why) && !me.AnyActive() && me.HasTag("State.Carrying.Log") && me.Completed() == 2, "an instant ability applies its effect and ends");
		Check(!me.CanActivate("PickUp", nullptr, &why), "and its tag blocks the next one");
		me.RemoveEffect("Carry");
		Check(!me.HasTag("State.Carrying"), "taking the effect away takes the state away");
		me.Revoke("Chop");
		Check(!me.HasAbility("Chop"), "revoking");

		// events: a listener hears them, by prefix
		int heard = 0;
		const int id = tree.AddListener("Event.Chop", [&](const GameplayEventData& e) { if (e.Instigator == &me) ++heard; });
		me.Grant("Chop");
		me.Tags().Add("State.Strong");
		me.Update(0.0f);
		Run(me, 2.5f);
		Check(me.Activate("Chop", &tree, &why), "chop for the listener");
		Run(me, 1.2f);
		Check(heard == 1, "the target heard the event once");
		tree.RemoveListener(id);
		tree.SendEvent({ "Event.Chop.Hit", &me, &tree });
		Check(heard == 1, "a removed listener hears nothing");
	}

	// ---- WaitGameplayEvent, and events that never come
	{
		GameplayLibrary lib;
		AbilityDef a;
		a.Name = "Ask";
		a.Tag = "Ability.Ask";
		a.WaitEvent = "Event.Answer";
		a.WaitTimeout = 1.0f;
		a.Events = { { "", 0.0f, "Event.Question", {}, {} } };
		lib.AddAbility(a);
		AbilitySystemComponent me("Me", lib);
		me.Grant("Ask");
		std::string why;
		me.Activate("Ask", nullptr, &why);
		Run(me, 0.5f);
		Check(me.IsActive("Ask"), "it waits for the event");
		me.SendEvent({ "Event.Answer.Yes", nullptr, nullptr });
		Run(me, 0.1f);
		Check(!me.AnyActive() && me.LastError().empty() && me.Completed() == 1, "an event below the awaited tag ends the wait");
		me.Activate("Ask", nullptr, &why);
		Run(me, 1.2f);
		Check(!me.AnyActive() && me.LastError().find("Event.Answer") != std::string::npos && me.Completed() == 1, "no event: it fails after the timeout");
	}

	// ---- scenes: components in the JSON, ability systems from entities and back
	{
		Garden g;
		AbilitySystemComponent* me = g.Gameplay.Find("Gardener");
		Check(me && me->HasAbility("Dig") && me->HasAbility("PickFruit") && me->EffectActive("EnergyRegen") && me->Attribute("Seeds") == 6.0f, "the gardener has its abilities, numbers and effects from the scene");
		Check(g.Gameplay.Find("Plot 1") && g.Gameplay.Find("Plot 6") && g.Gameplay.Find("Fruit Tree")->EffectActive("FruitRegrow") && g.Gameplay.Find("Cow")->HasTag("State.Cow"), "plots, trees and the cow too");
		g.Do("Gardener", "Dig", "Plot 2");
		g.Gameplay.Find("Gardener")->Tags().Add("State.Strong");
		g.Gameplay.Store(g.Entities);
		const std::string json = SaveSceneToString(g.Entities);
		EntityWorld loaded;
		const SceneIOResult r = LoadSceneFromString(loaded, json);
		Check(r.Ok, "a scene with gameplay components loads");
		GameplayWorld again;
		RegisterGardenContent(again.Library);
		Check(again.Load(loaded) == static_cast<int>(g.Gameplay.Count()), "one system per entity with the component");
		Check(GardenSystem::SoilState(*again.Find("Plot 2")) == "dug" && GardenSystem::SoilState(*again.Find("Plot 1")) == "grass", "the soil state survives the file");
		Check(Near(again.Find("Gardener")->Attribute("Energy"), g.Gameplay.Find("Gardener")->Attribute("Energy")) && again.Find("Gardener")->HasTag("State.Strong") && again.Find("Gardener")->HasAbility("Milk"), "so do the numbers, tags and abilities");
		Check(SaveSceneToString(loaded) == json, "saving the loaded scene gives the same text");
		const EntityId copy = loaded.Duplicate(loaded.FindByName("Plot 1"));
		Check(loaded.Find(copy) && loaded.Find(copy)->Gameplay && loaded.Find(copy)->Gameplay->Tags.size() == 2, "duplicating an entity copies its component");
		// rebuilding replaces the old garden
		const size_t count = g.Entities.Count();
		BuildGarden(g.Entities, glm::vec3(10.0f, 0.0f, 10.0f));
		Check(g.Entities.Count() == count && g.Entities.FindByName("Plot 1") != NullEntity && g.Entities.FindByName("Plot 1 2") == NullEntity, "building the garden twice leaves one");
		// sapling: the Tree Spot cannot be watered empty
		Garden h;
		Check(!h.Gameplay.Find("Gardener")->CanActivate("Water", h.Gameplay.Find("Tree Spot")), "an empty tree spot cannot be watered");
	}

	// ---- the garden loop, twice
	{
		bool okA = false, okB = false;
		const std::string a = GardenLoop(&okA);
		const std::string b = GardenLoop(&okB);
		Check(okA && okB, "dig, sow, water, grow, pull, pick fruit and milk all work");
		Check(a == b, "two runs end in exactly the same state");
		Check(!a.empty() && a.find("Plot 1:State.Soil.Dug") != std::string::npos, "the plot is dug again after the harvest");
	}

	// ---- more of the garden: wrong order, tree, resources
	{
		Garden g;
		AbilitySystemComponent* me = g.Gameplay.Find("Gardener");
		std::string why;
		Check(!me->CanActivate("Sow", g.Gameplay.Find("Plot 1"), &why) && why.find("State.Soil.Dug") != std::string::npos, "cannot sow before digging");
		Check(!me->CanActivate("Water", g.Gameplay.Find("Plot 1"), &why), "cannot water bare grass");
		Check(!me->CanActivate("Pull", g.Gameplay.Find("Plot 1"), &why), "cannot pull what is not grown");
		me->RemoveEffect("EnergyRegen");   // so the numbers below are exact
		g.Do("Gardener", "Dig", "Plot 1");
		Check(!me->CanActivate("Dig", g.Gameplay.Find("Plot 1"), &why), "cannot dig twice");
		g.Do("Gardener", "Dig", "Plot 3");
		Check(me->Attribute("Energy") == 90.0f, "two digs cost 10 energy");
		Check(g.Do("Gardener", "PlantTree", "Tree Spot") && GardenSystem::TreeState(*g.Gameplay.Find("Tree Spot")) == "sapling" && me->Attribute("Saplings") == 1.0f, "planting a tree uses a sapling and waits for the soil's answer");
		Check(g.Do("Gardener", "Water", "Tree Spot"), "the sapling is watered");
		g.Frames(60 * 7);
		AbilitySystemComponent* t = g.Gameplay.Find("Tree Spot");
		Check(GardenSystem::TreeState(*t) == "grown" && t->Attribute("Fruit") == 1.0f && t->EffectActive("FruitRegrow"), "it grows into a tree with a fruit and starts to regrow them");
		g.Frames(60 * 17);
		Check(t->Attribute("Fruit") == 3.0f, "fruit comes back by itself (a periodic effect), up to its maximum");
		t->RemoveEffect("FruitRegrow");
		for (int i = 0; i < 3; ++i) g.Do("Gardener", "PickFruit", "Tree Spot");
		Check(me->Attribute("Fruit") == 3.0f && t->Attribute("Fruit") == 0.0f, "three fruits picked, one per use");
		Check(!me->CanActivate("PickFruit", t, &why) && why.find("Fruit") != std::string::npos, "none left");
		// carrying
		Check(g.Do("Gardener", "CarryBox", "") && me->HasTag("State.Carrying.Box"), "carrying a box is a state (a tag)");
		Check(!me->CanActivate("Dig", g.Gameplay.Find("Plot 2"), &why), "carrying a box blocks working");
		Check(!me->CanActivate("PushWheelbarrow", nullptr, &why), "and taking the wheelbarrow");
		Check(g.Do("Gardener", "DropBox", "") && !me->HasTag("State.Carrying"), "dropping it frees the hands");
		Check(!me->CanActivate("DumpWheelbarrow", g.Gameplay.Find("Compost"), &why) && why.find("Wheelbarrow") != std::string::npos, "dumping needs the wheelbarrow (a required prop)");
		me->Attributes().SetBase("Crops", 1.0f);
		g.Do("Gardener", "PushWheelbarrow", "");
		Check(g.Do("Gardener", "DumpWheelbarrow", "Compost") && g.Gameplay.Find("Compost")->Attribute("Compost") == 1.0f, "with the wheelbarrow, dumping the crop adds compost");
		// energy: the cost can not be paid, and it comes back with time
		g.Do("Gardener", "ReleaseWheelbarrow", "");
		me->ApplyEffect("EnergyRegen");
		me->Attributes().SetBase("Energy", 4.0f);
		Check(!me->CanActivate("Dig", g.Gameplay.Find("Plot 2"), &why) && why.find("Energy") != std::string::npos, "too tired to dig");
		g.Frames(30);
		Check(me->Attribute("Energy") < 5.0f && !me->CanActivate("Dig", g.Gameplay.Find("Plot 2"), &why), "still not enough half a second later");
		g.Frames(60 * 2);
		Check(me->Attribute("Energy") >= 5.0f && me->Attribute("Energy") <= 7.0f && me->CanActivate("Dig", g.Gameplay.Find("Plot 2"), &why), "resting gives energy back, one point a second, and it can dig again");
	}

	// ---- a player and an NPC with the same abilities, in a world with a character each
	const std::string dir = GB_TEST_DATA_DIR;
	auto model = Model::Load(dir + "/test_humanoid.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test humanoid\n"); return 1; }
	{
		Garden g;
		const CharacterWorld flat;
		struct Who
		{
			explicit Who(Model& m) : A(m) {}
			Actor A;
			AbilityRuntime Rt;
			TaskRunner Tasks;
			std::vector<std::string> Equipped;
		};
		Who player(*model), npc(*model);
		int index = 0;
		for (Who* w : { &player, &npc })
		{
			const char* name = w == &player ? "Gardener" : "Farmhand";
			// the montages the abilities play, with a clip of the test model standing in for each
			std::vector<MontageDesc> base;
			for (const char* m : { "dig_plant", "plant", "water", "pick_fruit", "pick_fruit_2", "pick_fruit_3", "milk_cow", "pull_plant", "pull_plant_2", "plant_tree", "wheelbarrow_dump" })
			{
				MontageDesc d;
				d.Name = m;
				d.Clip = "test_humanoid";
				base.push_back(d);
			}
			for (const MontageDesc& d : AbilityMontages(g.Gameplay.Library, base)) w->A.Anim.Montages().Add(d);
			w->A.Controller.SetPosition(flat, -6.0f + 3.0f * static_cast<float>(index++), -7.0f, 0.0f);
			w->Rt.Anim = &w->A.Anim;
			w->Rt.Controller = &w->A.Controller;
			w->Rt.World = &flat;
			w->Rt.Resolve = [&g](const std::string& e, const std::string& p, InteractionTarget& out, std::string* err) { return FindInteraction(g.Entities, e, p, out, err); };
			w->Rt.Equip = [w](const std::string& prop, bool on) { w->Equipped.push_back(prop + (on ? " on" : " off")); return true; };
			g.Gameplay.Find(name)->SetRuntime(&w->Rt);
		}
		Check(player.A.Anim.Montages().Has("dig_plant") && npc.A.Anim.Montages().Has("pick_fruit_3"), "the test characters have the garden's montages");

		// the montage owns its notifies: an ability only adds the ones the montage lacks, it never moves one the montage already has
		{
			std::vector<MontageDesc> base;
			for (const char* m : { "dig_plant", "plant", "water", "pick_fruit", "pick_fruit_2", "pick_fruit_3", "milk_cow", "pull_plant", "pull_plant_2", "plant_tree", "wheelbarrow_dump" })
			{
				MontageDesc d;
				d.Name = m;
				d.Clip = "test_humanoid";
				base.push_back(d);
			}
			std::vector<MontageDesc> edited = base;
			const AbilityDef* dig = g.Gameplay.Library.Ability("Dig");
			Check(dig && !dig->Montages.empty() && !dig->Events.empty() && !dig->Events[0].Notify.empty(), "Dig has a montage and a notify event");
			if (dig && !dig->Montages.empty() && !dig->Events.empty() && !dig->Events[0].Notify.empty())
			{
				const std::string notify = dig->Events[0].Notify;
				for (MontageDesc& d : edited)
					if (d.Name == dig->Montages[0]) { d.Notifies.clear(); d.Notifies.push_back({ 0.9f, notify }); }
				const std::vector<MontageDesc> merged = AbilityMontages(g.Gameplay.Library, edited);
				int count = 0;
				float time = 0.0f;
				for (const MontageDesc& d : merged)
					if (d.Name == dig->Montages[0])
						for (const MontageNotify& n : d.Notifies)
							if (n.Name == notify) { ++count; time = n.Time; }
				Check(count == 1 && time > 0.89f && time < 0.91f, "a notify the montage already has keeps the time it was edited to");
				const std::vector<MontageDesc> fresh = AbilityMontages(g.Gameplay.Library, base);
				bool added = false;
				for (const MontageDesc& d : fresh)
					if (d.Name == dig->Montages[0])
						for (const MontageNotify& n : d.Notifies) added = added || n.Name == notify;
				Check(added, "and one it lacks is added at the ability's time");
			}
		}

		AbilitySystemComponent* pa = g.Gameplay.Find("Gardener");
		AbilitySystemComponent* na = g.Gameplay.Find("Farmhand");
		std::vector<Task> list;
		ParseTasks("ability Dig Plot_2 | ability Sow Plot_2 | ability Water Plot_2 | wait 0.5", list);
		npc.Tasks.Set(list);
		TaskRunner::AbilityHandler handler;
		handler.Start = [&](const std::string& ability, const std::string& entity, std::string& error)
		{
			return na->Activate(ability, g.Gameplay.Find(entity), &error, entity);
		};
		handler.Poll = [&](std::string& error)
		{
			if (na->AnyActive()) return 0;
			error = na->LastError();
			return error.empty() ? 1 : -1;
		};
		std::string why;
		Check(pa->Activate("Dig", g.Gameplay.Find("Plot 1"), &why), "the player starts digging from afar");
		bool playerControlled = false, npcControlled = false;
		float worstStep = 0.0f;
		glm::vec3 last = player.A.Controller.GetPosition();
		for (int i = 0; i < 60 * 90 && (pa->AnyActive() || npc.Tasks.Running()); ++i)
		{
			g.Gameplay.Update(dt);
			g.Rules.Update(dt);
			playerControlled = playerControlled || pa->Controlling();
			npcControlled = npcControlled || na->Controlling();
			const CharacterInput pin = pa->Controlling() ? pa->Input() : CharacterInput();
			CharacterInput nin = na->Controlling() ? na->Input() : CharacterInput();
			if (!na->Controlling() && npc.Tasks.Running())
				nin = npc.Tasks.Update(npc.A.Controller, flat, &npc.A.Anim, dt, nullptr, nullptr, handler);
			player.A.Step(pin, flat);
			npc.A.Step(nin, flat);
			worstStep = std::max(worstStep, glm::distance(player.A.Controller.GetPosition(), last));
			last = player.A.Controller.GetPosition();
		}
		Check(playerControlled && npcControlled, "while their abilities run, the abilities move the characters");
		Check(!pa->AnyActive() && pa->LastError().empty(), "the player's ability ended well");
		Check(GardenSystem::SoilState(*g.Gameplay.Find("Plot 1")) == "dug", "its montage's notify dug the soil");
		if (npc.Tasks.Failed() || npc.Tasks.Running()) std::fprintf(stderr, "  npc: %s\n", npc.Tasks.Describe().c_str());
		Check(!npc.Tasks.Failed() && !npc.Tasks.Running(), "the NPC's task list finished");
		Check(GardenSystem::SoilState(*g.Gameplay.Find("Plot 2")) == "sown" && g.Gameplay.Find("Plot 2")->Attribute("Moisture") > 0.5f, "and it dug, sowed and watered a plot with the same abilities");
		Check(na->Attribute("Seeds") == 5.0f && na->Attribute("Water") == 9.0f && pa->Attribute("Seeds") == 6.0f, "each paid with its own numbers");
		InteractionTarget at;
		FindInteraction(g.Entities, "Plot 2", "work", at);
		Check(glm::distance(glm::vec2(npc.A.Controller.GetPosition().x, npc.A.Controller.GetPosition().z), at.Position) < 0.01f, "the NPC stands on the plot's point");
		FindInteraction(g.Entities, "Plot 1", "work", at);
		Check(glm::distance(glm::vec2(player.A.Controller.GetPosition().x, player.A.Controller.GetPosition().z), at.Position) < 0.01f, "the player too");
		Check(worstStep < 0.2f, "nobody teleports on the way");
		Check(player.Equipped.size() == 2 && player.Equipped[0] == "shovel on" && player.Equipped[1] == "shovel off", "the shovel is taken for the dig and put away after");
		Check(npc.Equipped.size() == 6 && npc.Equipped[2] == "seeds on", "the NPC changes tools per ability");
		Check(!player.A.Anim.Montages().Busy() && !npc.A.Anim.Montages().Busy(), "no montage left playing");

		// a task list stops at an ability that cannot start, and says why
		std::vector<Task> bad;
		ParseTasks("ability Sow Plot_6", bad);
		npc.Tasks.Set(bad);
		for (int i = 0; i < 5; ++i) npc.Tasks.Update(npc.A.Controller, flat, &npc.A.Anim, dt, nullptr, nullptr, handler);
		Check(npc.Tasks.Failed() && npc.Tasks.Describe().find("State.Soil.Dug") != std::string::npos, "a task that cannot start fails with the reason");
		std::vector<Task> parsed;
		Check(ParseTasks("ability Dig Plot_2", parsed) && parsed[0].What == Task::Kind::Ability && parsed[0].Entity == "Plot 2" && DescribeTask(parsed[0]) == "ability Dig Plot 2", "the ability task parses");
		Check(!ParseTasks("ability", parsed), "and needs a name");

		// cancelling while the montage plays stops it
		g.Frames(60);
		Check(pa->Activate("Water", g.Gameplay.Find("Plot 2"), &why), "the player waters the NPC's plot");
		for (int i = 0; i < 60 * 30 && !player.A.Anim.Montages().IsPlaying("water"); ++i)
		{
			g.Frames(1);
			player.A.Step(pa->Controlling() ? pa->Input() : CharacterInput(), flat);
		}
		Check(player.A.Anim.Montages().IsPlaying("water"), "its montage plays once it is there");
		pa->Cancel("Water");
		Check(!pa->AnyActive() && pa->LastError().find("cancelled") != std::string::npos, "cancelling ends the ability");
		for (int i = 0; i < 60; ++i) player.A.Step(CharacterInput(), flat);
		Check(!player.A.Anim.Montages().IsPlaying("water"), "and stops its montage");
	}

	// ---- the real packs, when available
	const char* mixamo = std::getenv("GB_MIXAMO_DIR");
	const char* farming = std::getenv("GB_FARMING_DIR");
	if (mixamo && farming && std::filesystem::exists(std::string(mixamo) + "/idle.fbx") && std::filesystem::exists(std::string(farming) + "/box idle.fbx"))
	{
		auto bot = Model::Load(std::string(mixamo) + "/idle.fbx");
		if (bot)
		{
			for (const std::string& folder : { std::string(mixamo), std::string(farming) })
				for (const auto& e : std::filesystem::directory_iterator(folder))
					if (e.path().extension() == ".fbx" && e.path().filename() != "X Bot.fbx" && e.path().filename() != "idle.fbx") bot->AppendAnimations(e.path().generic_string());
			Garden g;
			ModelInstance inst(*bot);
			CharacterAnimator animator(*bot, inst);
			animator.AddDefaultLocomotionClips();
			animator.AddMixamoSets();
			animator.AddMixamoActions();
			const int registered = RegisterAbilityMontages(g.Gameplay.Library, animator.Anim());
			Check(registered >= 10, "the garden's abilities find their montages in the farming pack");
			CharacterController controller;
			MotionTracker tracker;
			const CharacterWorld flat;
			AbilityRuntime rt;
			rt.Anim = &animator.Anim();
			rt.Controller = &controller;
			rt.World = &flat;
			rt.Resolve = [&g](const std::string& e, const std::string& p, InteractionTarget& out, std::string* err) { return FindInteraction(g.Entities, e, p, out, err); };
			rt.Equip = [](const std::string&, bool) { return true; };
			AbilitySystemComponent* me = g.Gameplay.Find("Gardener");
			me->SetRuntime(&rt);
			controller.SetPosition(flat, 0.0f, -7.0f, 0.0f);
			auto frame = [&]
			{
				g.Gameplay.Update(dt);
				g.Rules.Update(dt);
				const CharacterInput in = me->Controlling() ? me->Input() : CharacterInput();
				controller.Step(in, flat, dt);
				const glm::mat4 placement = glm::rotate(glm::translate(glm::mat4(1.0f), controller.GetPosition()), controller.GetYaw(), glm::vec3(0, 1, 0));
				animator.Update(tracker.Update(controller, dt), dt, placement);
			};
			auto work = [&](const char* ability, const char* target)
			{
				std::string why;
				if (!me->Activate(ability, g.Gameplay.Find(target), &why)) { std::fprintf(stderr, "  %s: %s\n", ability, why.c_str()); return false; }
				for (int i = 0; i < 60 * 60 && me->AnyActive(); ++i) frame();
				return !me->AnyActive() && me->LastError().empty();
			};
			Check(work("Dig", "Plot 1") && GardenSystem::SoilState(*g.Gameplay.Find("Plot 1")) == "dug", "mixamo: digging with the real clip digs the plot at its notify");
			Check(work("Sow", "Plot 1") && GardenSystem::SoilState(*g.Gameplay.Find("Plot 1")) == "sown", "mixamo: sowing");
			Check(work("Water", "Plot 1") && g.Gameplay.Find("Plot 1")->Attribute("Moisture") > 0.5f, "mixamo: watering");
			Check(work("PickFruit", "Fruit Tree") && work("PickFruit", "Fruit Tree") && me->Attribute("Fruit") == 2.0f, "mixamo: two fruits, with two of the three picking animations");
			Check(work("Milk", "Cow") && me->Attribute("Milk") == 1.0f, "mixamo: milking");
			Check(work("PlantTree", "Tree Spot") && GardenSystem::TreeState(*g.Gameplay.Find("Tree Spot")) == "sapling", "mixamo: planting the tree");
		}
	}

	if (failures) return 1;
	std::puts("gameplay tests passed");
	return 0;
}
