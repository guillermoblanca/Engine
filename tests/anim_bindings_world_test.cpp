// Animation bindings in the game (docs/EDITOR_ANIMACIONES.md), headless: they reach every character of an ActorWorld (the ones that exist, the ones
// made later, or only the ones of a model), a binding with a `when` picks its clip from where the ability is used (node, tool), and they survive the
// rules registering the abilities again (a combat rules reload). Runs on the generated humanoid of tests/data. CPU only.
#include "GB/Animation/AnimBindings.h"
#include "GB/Game/ActorWorld.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Log.h"
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}
	constexpr float kDt = 1.0f / 60.0f;

	std::shared_ptr<GB::Model> TestHumanoid()
	{
		const std::string dir = GB_TEST_DATA_DIR;
		auto model = GB::Model::Load(dir + "/test_humanoid.fbx");
		if (!model) return nullptr;
		for (const char* f : { "test_idle.fbx", "test_walk.fbx", "test_run.fbx" }) model->AppendAnimations(dir + "/" + f);
		return model;
	}

	GB::MontageDesc Montage(const char* name, const char* clip)
	{
		GB::MontageDesc d;
		d.Name = name;
		d.Clip = clip;
		return d;
	}

	// The manifest the characters share: the usual one plus three montages and the "Swing" ability that plays swing_a.
	GB::AnimManifest BaseManifest()
	{
		GB::AnimManifest m = GB::MixamoManifest();
		m.Montages.push_back(Montage("swing_a", "test_walk"));
		m.Montages.push_back(Montage("swing_b", "test_run"));
		m.Montages.push_back(Montage("swing_c", "test_idle"));
		m.Abilities.push_back({ "Swing", { "swing_a" }, "", false, "" });
		return m;
	}

	GB::AbilityDef SwingDef()
	{
		GB::AbilityDef a;
		a.Name = "Swing";
		a.Tag = "Ability.Swing";
		a.NeedsTarget = true;
		a.MoveToTarget = false;
		a.Montages = { "swing_a" };
		a.Duration = 0.5f;
		a.ActiveTags = { "State.Busy" };
		return a;
	}

	GB::BindingClip Clip(const char* source)
	{
		GB::BindingClip c;
		c.Source = source;
		return c;
	}

	GB::AnimBinding Bind(const char* action, const char* source, std::map<std::string, std::string> when = {}, const char* skeleton = "")
	{
		GB::AnimBinding b;
		b.Action = action;
		b.Skeleton = skeleton;
		b.When = std::move(when);
		b.Clips.push_back(Clip(source));
		return b;
	}

	struct Garden
	{
		GB::ActorWorld World;
		GB::GameplayWorld Gameplay;
		std::shared_ptr<GB::Model> Model = TestHumanoid();
		GB::AnimManifest Base = BaseManifest();

		Garden()
		{
			GB::ActorHost host;
			host.World.GroundHeight = [](float, float) { return 0.0f; };
			World.SetHost(host);
			World.SetManifest(&Base);
			World.Bind(nullptr, &Gameplay);
			Gameplay.Library.AddAbility(SwingDef());
			Node("tree 1", "tree", "Wood");
			Node("rock 1", "rock", "Stone");
			Node("berry bush 1", "berry_bush", "Plant");
		}
		void Node(const char* name, const char* id, const char* kind)
		{
			GB::AbilitySystemComponent* n = Gameplay.Create(name);
			n->Tags().Add("Resource.Node");
			n->Tags().Add(std::string("Resource.Node.") + id);
			n->Tags().Add(std::string("Resource.") + kind);
		}
		GB::Actor* Spawn(const char* name, const char* kind, float x)
		{
			Gameplay.Create(name)->Grant("Swing");
			GB::ActorSpawn s;
			s.Shared = Model;
			s.Kind = kind;
			s.Position = { x, 0.0f };
			std::string error;
			GB::Actor* a = World.Spawn(name, s, error);
			if (!a) std::fprintf(stderr, "spawn: %s\n", error.c_str());
			return a;
		}
		std::shared_ptr<const GB::BindingsSetup> Setup(GB::AnimBindings bindings, const char* only = "")
		{
			auto s = std::make_shared<GB::BindingsSetup>();
			s->Bindings = std::move(bindings);
			s->Defaults = GB::MakeBindingDefaults(Base, &Gameplay.Library);
			s->Only = only;
			return s;
		}
		void Frames(int n)
		{
			for (int i = 0; i < n; ++i)
			{
				World.PrepareAbilities();
				Gameplay.Update(kDt);
				World.Update(kDt);
			}
		}
		// Uses Swing on a node and says which montage the character played (the one that was on screen), then lets it finish.
		std::string Swing(const char* who, const char* node)
		{
			GB::AbilitySystemComponent* asc = Gameplay.Find(who);
			std::string why;
			if (!asc->Activate("Swing", Gameplay.Find(node), &why)) return "refused: " + why;
			GB::Actor* actor = World.Find(who);
			std::string played;
			for (int i = 0; i < 20 && played.empty(); ++i)
			{
				Frames(1);
				for (const std::string& m : actor->MontageNames())
					if (actor->Anim()->Montages().IsPlaying(m)) played = m;
			}
			for (int i = 0; i < 60 * 10 && asc->AnyActive(); ++i) Frames(1);
			return played;
		}
	};

	bool HasAbilityMontage(const GB::Actor& a, const std::string& montage)
	{
		const GB::ManifestUse* u = GB::FindUse(a.Manifest().Abilities, "Swing");
		return u && !u->Montages.empty() && u->Montages[0] == montage;
	}
}

int main()
{
	using namespace GB;
	Log::Init();
	if (!TestHumanoid()) { std::fprintf(stderr, "no test humanoid\n"); return 1; }

	// ---- the context a use happens in
	{
		AbilityDef def = SwingDef();
		def.RequiredTags = { "Item.Tool.Axe" };
		GameplayLibrary lib;
		AbilitySystemComponent owner("w", lib), tree("tree 1", lib);
		owner.Tags().Add("Item.Tool.Axe");
		tree.Tags().Add("Resource.Node");
		tree.Tags().Add("Resource.Node.old_tree");
		tree.Tags().Add("Resource.Wood");
		auto c = BindingContext(def, &owner, &tree);
		Check(c["node"] == "old_tree" && c["kind"] == "wood" && c["tool"] == "axe" && c["variant"] == "basic" && c["target"] == "tree 1" && c["ability"] == "Swing", "the context names the node, its kind, the tool and its variant");
		owner.Tags().Add("Item.Tool.Axe.Flint");
		Check(BindingContext(def, &owner, &tree)["variant"] == "flint", "a flint axe is the 'flint' variant");
		def.RequiredTags.clear();
		c = BindingContext(def, &owner, nullptr);
		Check(c["tool"] == "none" && !c.count("variant") && !c.count("node"), "an ability that asks for no tool is 'none'; no target, no node");
	}

	// ---- bindings reach every character: the existing ones, the ones made later, only one model
	{
		Garden s;
		Actor* a = s.Spawn("a", "models/x bot.fbx", 0.0f);
		Actor* b = s.Spawn("b", "models/fox.glb", 5.0f);
		Check(a && b, "two characters");
		if (!a || !b) return 1;
		Check(HasAbilityMontage(*a, "swing_a") && HasAbilityMontage(*b, "swing_a"), "without bindings every character plays the manifest's montage");

		AnimBindings bindings;
		bindings.Bindings.push_back(Bind("ability:Swing", "swing_b"));
		const int changed = s.World.SetBindings(s.Setup(bindings));
		Check(changed == 2, "'apply to all' changed both characters, not just one");
		Check(HasAbilityMontage(*a, "swing_b") && HasAbilityMontage(*b, "swing_b"), "both play the bound montage now");
		Check(s.World.SetBindings(s.Setup(bindings)) == 0, "the same bindings again change nothing (no animator is rebuilt for nothing)");

		Actor* c = s.Spawn("c", "models/x bot.fbx", 10.0f);
		Check(c && HasAbilityMontage(*c, "swing_b"), "a character made afterwards is born with the bindings");

		// an edit is reflected in all of them at once
		bindings.Bindings[0].Clips[0] = Clip("swing_c");
		Check(s.World.SetBindings(s.Setup(bindings)) == 3 && HasAbilityMontage(*a, "swing_c") && HasAbilityMontage(*b, "swing_c") && HasAbilityMontage(*c, "swing_c"), "an edit shows in every character");

		// only one model
		Check(s.World.SetBindings(s.Setup(bindings, "x bot")) == 1, "limiting to a model takes the bindings off the others");
		Check(HasAbilityMontage(*a, "swing_c") && HasAbilityMontage(*c, "swing_c") && HasAbilityMontage(*b, "swing_a"), "the fox plays its default again, the x bot characters keep theirs");
		Actor* d = s.Spawn("d", "models/fox.glb", 15.0f);
		Check(d && HasAbilityMontage(*d, "swing_a"), "a fox made later does not get them");

		Check(s.World.SetBindings(nullptr) == 2 && HasAbilityMontage(*a, "swing_a"), "taking the bindings off puts the defaults back");
	}

	// ---- the clip of a use depends on where it happens
	{
		Garden s;
		Actor* a = s.Spawn("a", "models/x bot.fbx", 0.0f);
		Check(a != nullptr, "a character");
		if (!a) return 1;
		AnimBindings bindings;
		bindings.Bindings.push_back(Bind("ability:Swing", "swing_b"));
		bindings.Bindings.push_back(Bind("ability:Swing", "swing_c", { { "node", "tree" } }));
		AnimBinding rock = Bind("ability:Swing", "swing_a", { { "node", "rock" } });
		rock.Clips[0].Speed = 2.0f;   // (an override: it plays through a montage of its own)
		bindings.Bindings.push_back(rock);
		s.World.SetBindings(s.Setup(bindings));
		Check(a->Manifest().Montages.size() > s.Base.Montages.size(), "the contextual bindings made their montages");

		const std::string onTree = s.Swing("a", "tree 1");
		Check(onTree == "swing_c", "on a tree it plays the tree's clip: " + onTree);
		const std::string onRock = s.Swing("a", "rock 1");
		Check(onRock.rfind("bind:ability:Swing?node=rock", 0) == 0, "on a rock it plays the rock's clip (with its own speed): " + onRock);
		Check(s.Swing("a", "berry bush 1") == "swing_b", "on a bush the binding without conditions answers");
		Check(a->Anim()->Montages().Get(onRock) && a->Anim()->Montages().Get(onRock)->PlayRate == 2.0f, "the contextual montage carries its speed");

		// a binding for the model beats the general one, and a context that matches nothing falls to the default
		AnimBindings only;
		only.Bindings.push_back(Bind("ability:Swing", "swing_c", { { "node", "rock" } }));
		s.World.SetBindings(s.Setup(only));
		Check(s.Swing("a", "tree 1") == "swing_a", "no binding for the tree: the default clip");
		Check(s.Swing("a", "rock 1") == "swing_c", "the rock still has its own");
	}

	// ---- the rules registering the abilities again do not undo the bindings
	{
		Garden s;
		Actor* a = s.Spawn("a", "models/x bot.fbx", 0.0f);
		if (!a) return 1;
		AnimBindings bindings;
		bindings.Bindings.push_back(Bind("ability:Swing", "swing_b"));
		s.World.SetBindings(s.Setup(bindings));
		Check(s.Gameplay.Library.Ability("Swing")->Montages[0].rfind("swing_b", 0) == 0, "the library has the bound montage");
		s.Gameplay.Library.AddAbility(SwingDef());   // (the rules registered it again: swing_a)
		Check(s.Gameplay.Library.Ability("Swing")->Montages[0] == "swing_a", "the registration wrote its own list");
		s.Frames(2);
		Check(s.Gameplay.Library.Ability("Swing")->Montages[0] == "swing_b", "the characters put the bound montage back");

		// the real combat content: Attack.sword and Attack.bow are bindable and survive a reload
		const BindingDefaults defaults = MakeBindingDefaults(s.Base, &s.Gameplay.Library);
		Check(defaults.Abilities.count("Attack.sword") && defaults.Abilities.at("Attack.sword").size() == 3 && defaults.Abilities.count("Attack.bow"), "Attack.sword and Attack.bow are in the defaults");
		bool listed = false;
		for (const BindableAction& act : ListBindableActions(s.Base, defaults)) listed = listed || act.Key == "ability:Attack.bow";
		Check(listed, "Attack.bow is in the list of bindable actions");
		RegisterCombatContent(s.Gameplay.Library, DefaultCombatRules());
		s.Frames(1);
		AnimBindings combat;
		combat.Bindings.push_back(Bind("ability:Attack.sword", "swing_c"));
		auto setup = std::make_shared<BindingsSetup>();
		setup->Bindings = combat;
		setup->Defaults = defaults;
		s.World.SetBindings(setup);
		Check(s.Gameplay.Library.Ability("Attack.sword")->Montages == std::vector<std::string>{ "swing_c" }, "Attack.sword plays the bound montage");
		RegisterCombatContent(s.Gameplay.Library, DefaultCombatRules());
		Check(s.Gameplay.Library.Ability("Attack.sword")->Montages.size() == 3, "reloading the combat rules wrote the C++ list");
		s.Frames(1);
		Check(s.Gameplay.Library.Ability("Attack.sword")->Montages == std::vector<std::string>{ "swing_c" }, "the bound montage is back after the reload");
	}

	if (failures == 0) std::printf("anim_bindings_world: ok\n");
	return failures == 0 ? 0 : 1;
}
