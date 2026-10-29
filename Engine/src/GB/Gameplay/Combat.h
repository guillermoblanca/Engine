#pragma once
#include "GB/Game/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Items.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	// Conflict, combat and violence (H14): the core. Any two things with an ability system can fight: members of a clan, members of two clans,
	// a fox and a child, a hunter and a rabbit. It is the same Gameplay Ability System as the rest of the game (H1):
	//
	//  - Attributes: "Health" (the need of GB/AI/Needs.h, 0..100; an animal gets one from its rule) and "Stamina" (0..MaxStamina, paid by swings and
	//    blocks, it comes back with time). Injuries are wounds (a kind and a severity 0..1) that bleed for a while, slow the body down (the effects
	//    "Combat.Wounded.Light/Serious/Grave" scale "Vigor") and heal with the days, faster resting and faster still when someone cares for it; a
	//    body with too much wound dies of it ("State.Dead" and "Death.Wounds").
	//  - Weapons are data (CombatRules::Weapons): a weapon is matched to an item by a tag ("Item.Tool.Axe": the stone and flint axes are weapons too),
	//    with damage, reach, the seconds between two swings, the stamina a swing costs, the chance a blow that lands wounds, and its montages.
	//    Natural weapons ("fists", the fox's "bite") have no tag.
	//  - Abilities: "Attack.<weapon>" (the montage of the weapon; its notify "hit" raises "Event.Combat.Hit", and only then is the blow resolved:
	//    a swing that is cancelled before the notify does nothing), "Block" (the montage of a block; while it runs the body has "State.Blocking"),
	//    "Flee" (grants "State.Fleeing" for a while) and "Stagger" (the reaction to a blow that lands). Without a character (tests, abstract members)
	//    they run on timers at the same fractions, as every ability does.
	//  - A blow is resolved deterministically: the chance and the damage come from the weapon and the stats of both (strength, dexterity,
	//    endurance, stamina, blocking) and the roll is a hash of the seed, the two names and how many rolls the attacker has made. No global random.
	//  - Morale: a fighter starts from its boldness, loses morale when it is hurt and when its side falls, and breaks below FleeBelow.
	//
	// Who moves: an "Entity" fighter (an animal, a test duellist) is moved here, straight towards its foe or away from it (an animal in a fight is
	// held: "State.Held", the ecology leaves it alone). An "External" fighter (a member of a clan) is moved by its owner (the tribe gives it orders,
	// which use navigation), who tells where it is with Place every frame and decides whether it fights or flees (see TribeConflict.cpp). The core
	// swings for both when the foe is within reach.
	//
	// Animals: a predator rule (CombatRules::Predators) lets an animal of a tag attack a lone child or a weak member when it is hungry; a hunt rule
	// (CombatRules::Hunts) says which animals the clans hunt, how hard they are to kill and what meat they give.
	//
	// Hostility (who is hostile to whom, which camps a clan could raid, grievances) is behind IHostility, so the clans work (H13) can plug real
	// attitudes in; HostilityTable is the plain one for tests and the editor (an explicit table, the predators, and a ledger of grievances).

	// ------------------------------------------------------------------ data
	struct WeaponDef
	{
		std::string Tag;                         // an item with this tag (or below) is this weapon; "" = a natural weapon (fists, a bite)
		float Damage = 6.0f;                     // health a clean blow takes from a foe of average endurance
		float Reach = 1.4f;                      // metres
		float Seconds = 1.4f;                    // between two swings
		float Stamina = 8.0f;                    // a swing costs this much
		float Wound = 0.25f;                     // chance that a blow that lands also wounds (a blow of Injuries.WoundAt or more always does)
		std::string WoundKind = "bruise";        // "cut", "bite", "bruise"...
		float Power = 0.0f;                      // the item's own sharpness adds this much damage per point of ItemDef::Power (flint is sharper than stone)
		std::vector<std::string> Montages{ "sword_attack_1" };   // played in turn, one per swing
		float HitAt = 0.45f;                     // where the blow lands in the montage (0..1)
		bool operator==(const WeaponDef&) const = default;
	};

	struct HitRules
	{
		float Base = 0.6f;                       // chance a blow lands between equals
		float Dexterity = 0.35f;                 // ... plus this times (attacker's dexterity - defender's dexterity)
		float Stamina = 0.25f;                   // ... minus this times how spent the attacker is (1 - stamina / max)
		float Min = 0.1f, Max = 0.95f;
		float Strength = 0.8f;                   // damage x (1 + this x (strength - 0.5))
		float Endurance = 0.6f;                  // damage x (1 - this x (endurance - 0.5))
		float BlockChance = 0.45f;               // chance a defender that sees the swing coming blocks (x 0.5 + dexterity)
		float BlockLets = 0.25f;                 // part of the damage a block lets through
		float BlockStamina = 6.0f;               // a block costs this much
		float MaxStamina = 100.0f;
		float StaminaRegen = 8.0f;               // per second (none for an animal while it runs away)
		float RunStamina = 8.0f;                 // an animal running in a fight spends this much a second (spent, it runs at half speed)
		bool operator==(const HitRules&) const = default;
	};

	struct InjuryRules
	{
		float WoundAt = 14.0f;                   // a blow that takes this much health always wounds; its severity is damage / Severity
		float Severity = 40.0f;
		float BleedSeconds = 40.0f;              // a fresh wound bleeds this long ...
		float Bleed = 0.25f;                     // ... health per second at severity 1 (half while someone cares for it)
		float HealPerDay = 0.6f;                 // severity healed per day of the needs' clock
		float RestFactor = 2.0f;                 // ... times this while resting or asleep
		float CareFactor = 1.5f;                 // ... times this while cared for
		float Slow = 0.6f;                       // vigor lost at a total severity of 1 (the effects are in three steps)
		float Lethal = 1.6f;                     // a total severity of this much kills
		float RestAbove = 0.25f;                 // a member with more total severity than this goes to rest at the camp
		float CareSeconds = 20.0f;               // a member who cares for another stays with it this long (the care lasts CareSeconds x 3)
		bool operator==(const InjuryRules&) const = default;
	};

	struct MoraleRules
	{
		float Start = 0.4f;                      // morale = Start + Boldness x boldness
		float Boldness = 0.6f;
		float Hurt = 1.2f;                       // morale lost per 100 health of damage taken
		float AllyDown = 0.2f;                   // morale lost when one of its side within GroupRadius falls
		float FleeBelow = 0.25f;                 // breaks: it flees
		float FleeHealth = 25.0f;                // and below this health anyone but the boldest flees
		float Recover = 0.02f;                   // per second out of a fight
		bool operator==(const MoraleRules&) const = default;
	};

	// Seeing a threat, and the choice between fighting and fleeing (made by the owner of an External fighter: the tribe).
	struct ThreatRules
	{
		float Hostile = 0.5f;                    // hostility (IHostility) from which another side is a threat
		float Sight = 24.0f;                     // metres a member notices a threat in its view (the cone of the vision rules, nearer than this)
		float Safety = 30.0f;                    // safety lost when a threat is seen (and Safety / 2 when one is heard: the clan's alarm)
		float Alarm = 30.0f;                     // metres a member's alarm cry carries
		float GroupRadius = 12.0f;               // who counts as with it (allies) or with the foe (its side) for the odds
		float FleeDistance = 30.0f;              // a member that flees runs this far from the threat (or home when the camp is the other way)
		float SafeDistance = 26.0f;              // and stops fleeing this far from every threat it knows
		float ForgetSeconds = 20.0f;             // a threat not seen for this long is forgotten
		// the utility of fighting and of fleeing (see Tribe::FightOrFlight)
		float Bold = 1.0f;                       // fight: + this x boldness
		float Health = 0.8f;                     // fight: + this x health / 100
		float Odds = 1.0f;                       // fight: + this x min(odds / 2, 1); flee: + this x (1 - odds) when outnumbered
		float Kin = 0.8f;                        // fight: + this when the threat attacks one of its kin or a child of the clan
		float Camp = 0.4f;                       // fight: + this when the threat is at the camp
		float Walls = 0.5f;                      // fight: + this x the camp's defense (a palisade) when the threat is at the camp
		float Fear = 1.2f;                       // flee: + this x (1 - safety / 100)
		float Timid = 0.8f;                      // flee: + this x (1 - boldness)
		bool ChildrenFight = false;              // children always flee
		bool operator==(const ThreatRules&) const = default;
	};

	// An animal that may attack people (H3's fox, and the predators that come later).
	struct PredatorRule
	{
		std::string Tag;                         // "Animal.Fox"
		std::string Prey = "child";              // whom it dares to attack: "child", "weak" (a child or anyone under WeakHealth) or "any"; "none": nobody
		float WeakHealth = 40.0f;
		float Alone = 8.0f;                      // only when nobody else of the prey's side is this close to it
		float Hungry = 50.0f;                    // only with its Food under this
		float Sight = 14.0f;
		std::string Weapon = "bite";
		float Health = 30.0f;
		float Speed = 2.6f;
		float Boldness = 0.5f;
		float Meal = 60.0f;                      // food it gets when its prey dies
		float GiveUp = 25.0f;                    // it gives up the chase this far from its prey
		bool operator==(const PredatorRule&) const = default;
	};

	// An animal the clans hunt for food.
	struct HuntRule
	{
		std::string Tag;                         // "Animal.Rabbit"
		std::string Item = "meat";               // what a kill gives ...
		int Units = 2;                           // ... and how much of it
		std::string Byproduct;                   // what else a kill gives ("hide"; "" = nothing) ...
		int ByproductUnits = 0;                  // ... and how much of it
		float Health = 10.0f;
		float Speed = 2.2f;                      // when it flees (it flees as soon as it is attacked)
		std::string Weapon = "fists";            // what it fights back with when cornered (nothing for a rabbit: it never fights back)
		bool FightsBack = false;
		std::string ToolTag;                     // a hunter must carry this ("" = bare hands do)
		float Priority = 0.9f;                   // base priority of a "hunt" job
		float Range = 50.0f;                     // the clan posts a hunt for an animal seen this close to the camp
		int Hunters = 2;                         // slots of the job
		// The social lane (docs/VIDA_SOCIAL.md): the butchering of a kill, beyond Item and Byproduct (item -> units: bone, sinew, fat, feathers...),
		// and a group hunt: the chase does not start until this many members hold the job (one drives, the others wait with the spear; 1 = alone).
		std::map<std::string, int> Yields;
		int Group = 1;
		bool operator==(const HuntRule&) const = default;
	};

	// Raids (Raids.h): when a clan raids, how it goes and what it leaves behind.
	struct RaidRules
	{
		float Every = 60.0f;                     // seconds between two times the clan weighs a raid
		float Cooldown = 1200.0f;                // the least time between two raids
		float Want = 1.0f;                       // a raid starts when the motive reaches this:
		float Bold = 0.8f;                       //   + this x the mean boldness of the adults
		float Hunger = 0.8f;                     //   + this x how short of food the store is (0..1)
		float Grievance = 0.5f;                  //   + this x the grievance against the target
		float Hostility = 0.4f;                  //   + this x the hostility to the target
		float Defense = 0.5f;                    //   - this x the target's defense (its palisades: BuildingDef::Defense)
		int Party = 2, MaxParty = 4;             // the least and the most raiders
		float PartyHealth = 60.0f;               // a raider has at least this health
		float StealSeconds = 3.0f;               // seconds at the store
		int Carry = 4;                           // units a raider takes
		std::vector<std::string> Steal{ "Item.Food" };   // tags taken first (then anything)
		float Retreat = 0.5f;                    // the raid turns back when this part of the party is down or has fled
		float Wrong = 1.0f;                      // grievance a raid leaves in its victim (a death: 2x; a theft seen: 0.5x)
		float Forget = 0.1f;                     // grievance forgotten per day
		float Reach = 4.0f;                      // metres from the target store to take from it
		float MaxSeconds = 600.0f;               // a raid that has not ended by then goes home
		bool operator==(const RaidRules&) const = default;
	};

	struct CombatRules
	{
		uint32_t Seed = 7;
		std::map<std::string, WeaponDef> Weapons;   // DefaultCombatRules: fists, axe, pickaxe, spear (none in era 1 yet), bite
		std::string Unarmed = "fists";
		HitRules Hits;
		InjuryRules Injuries;
		MoraleRules Morale;
		ThreatRules Threats;
		std::map<std::string, PredatorRule> Predators;   // by id ("fox")
		std::map<std::string, HuntRule> Hunts;           // by id ("rabbit")
		RaidRules Raids;
		std::vector<std::pair<std::string, std::string>> Hostile;   // the default hostility table: pairs of sides hostile to each other
		bool operator==(const CombatRules&) const = default;
		const WeaponDef& Weapon(const std::string& id) const;   // the unarmed weapon for an unknown id
		const PredatorRule* PredatorFor(const AbilitySystemComponent& a) const;
		const HuntRule* HuntFor(const AbilitySystemComponent& a) const;
	};

	CombatRules DefaultCombatRules();   // assets/data/era1.json says the same in its "combat" section
	// { "combat": { "seed", "unarmed", "weapons": { "<id>": { "tag", "damage", "reach", "seconds", "stamina", "wound", "woundKind", "power", "montages": [],
	//   "hitAt" } }, "hits": { ... }, "injuries": { ... }, "morale": { ... }, "threats": { ... }, "predators": { "<id>": { "tag", "prey", ... } },
	//   "hunts": { "<id>": { "tag", "item", "units", ... } }, "raids": { ... }, "hostile": [ [ "a", "b" ] ] } }.
	// "weapons", "predators" and "hunts" add or replace entries by id (each whole); "hits", "injuries", "morale", "threats" and "raids" replace that
	// part whole (what it does not say is the default); "hostile" replaces the list. A file without the section changes nothing; on a mistake it
	// returns why and changes nothing.
	std::string LoadCombatRules(const std::string& json, CombatRules& rules);
	std::string LoadCombatRulesFile(const std::string& path, CombatRules& rules);
	std::string CombatRulesToJson(const CombatRules& rules);   // one JSON object, as the section reads it back
	// Problems of the rules against the items they name (the meat of a hunt): "" = none.
	std::string CheckCombat(const CombatRules& rules, const ItemLibrary& items);

	// The effects ("Combat.Wounded.*", "Combat.Fleeing", the costs) and abilities ("Attack.<weapon>", "Block", "Flee", "Stagger") of the rules.
	void RegisterCombatContent(GameplayLibrary& library, const CombatRules& rules);
	// The weapon of a bag: the best (most damage) weapon whose tag an item of the bag has; the unarmed one when none. `power`: that item's ItemDef::Power.
	std::string BestWeapon(const CombatRules& rules, const ItemLibrary& items, const Inventory& bag, float* power = nullptr);
	// A deterministic roll 0..1 from the seed, two names and a counter.
	double CombatRoll(uint32_t seed, const std::string& a, const std::string& b, uint64_t n);

	// ------------------------------------------------------------------ hostility
	struct RaidTarget
	{
		std::string Clan;
		glm::vec2 Camp{ 0.0f };                  // where its store is
		Inventory* Store = nullptr;
		float Defense = 0.0f;                    // what its buildings add up to (a palisade 1): it weighs against a raid
	};

	// Who is hostile to whom. Sides are clan names ("Clan", "Riverfolk"), animal tags ("Animal.Fox") or anything a host gives its fighters.
	// H13 implements it with the real attitudes between clans; HostilityTable is the plain one.
	class IHostility
	{
	public:
		virtual ~IHostility() = default;
		// How hostile `a` is to `b`: 0 = not at all, 1 = attacks on sight.
		virtual float Hostility(const std::string& a, const std::string& b) const = 0;
		// The camps `raider` could raid (only of clans it is hostile to).
		virtual std::vector<RaidTarget> RaidTargets(const std::string& raider) const { (void)raider; return {}; }
		// A clan says where its camp and store are now (each time it weighs a raid): what RaidTargets hands out.
		virtual void Settled(const std::string& clan, const glm::vec2& camp, Inventory* store, float defense = 0.0f) { (void)clan; (void)camp; (void)store; (void)defense; }
		// `victim` was wronged by `culprit` ("raid", "theft", "killing"): its grievance grows by `weight`.
		virtual void Wronged(const std::string& victim, const std::string& culprit, const std::string& what, float weight) { (void)victim; (void)culprit; (void)what; (void)weight; }
		virtual float Grievance(const std::string& victim, const std::string& culprit) const { (void)victim; (void)culprit; return 0.0f; }
	};

	// The plain hostility: an explicit table (symmetric unless set one way), predators hostile to everyone but their own kind, and grievances
	// that make a clan hostile (Grievance x GrievanceHostility) and fade with the days (Forget, called by the host or by Update).
	class HostilityTable : public IHostility
	{
	public:
		float GrievanceHostility = 0.5f;          // hostility a grievance of 1 adds
		float ForgetPerDay = 0.1f;

		void Set(const std::string& a, const std::string& b, float value, bool both = true);
		void Clear();
		// The pairs of the rules (CombatRules::Hostile) and the predators of the rules (hostile to anyone not of their tag).
		void FromRules(const CombatRules& rules);
		float Hostility(const std::string& a, const std::string& b) const override;
		std::vector<RaidTarget> RaidTargets(const std::string& raider) const override;
		void Settled(const std::string& clan, const glm::vec2& camp, Inventory* store, float defense = 0.0f) override;
		void Wronged(const std::string& victim, const std::string& culprit, const std::string& what, float weight) override;
		float Grievance(const std::string& victim, const std::string& culprit) const override;
		void Forget(float days);
		const std::map<std::pair<std::string, std::string>, float>& Table() const { return m_Table; }
		const std::map<std::pair<std::string, std::string>, float>& Grievances() const { return m_Grievances; }
		std::string Report() const;
		// The table and the grievances (not the camps: the clans tell them again). FromJson: false (nothing changed) on a mistake.
		std::string ToJson() const;
		bool FromJson(const std::string& json);

	private:
		std::map<std::pair<std::string, std::string>, float> m_Table;        // (a, b) -> how hostile a is to b
		std::map<std::pair<std::string, std::string>, float> m_Grievances;   // (victim, culprit) -> weight
		std::vector<std::string> m_Predators;                                 // tags hostile to everyone else
		std::map<std::string, RaidTarget> m_Camps;
	};

	// ------------------------------------------------------------------ the fight
	enum class FightMode { None, Engage, Flee, Dead };
	const char* FightModeName(FightMode m);

	struct Wound
	{
		std::string Kind;                        // "cut", "bite", "bruise"
		float Severity = 0.0f;                   // 0..1
		float Bleeding = 0.0f;                   // seconds it still bleeds
		bool operator==(const Wound&) const = default;
	};

	struct Combatant
	{
		std::string Name;
		std::string Side;                        // its clan, or its tag for an animal
		bool Entity = false;                     // moved here (an animal, a test duellist); otherwise its owner moves it
		bool Animal = false;
		bool Child = false;                      // for the predators (the owner says)
		float Speed = 3.0f;                      // m/s, when moved here
		std::string Weapon = "fists";
		float Power = 0.0f;                      // ItemDef::Power of the weapon's item
		float Strength = 0.5f, Dexterity = 0.5f, Endurance = 0.5f, Boldness = 0.5f;
		FightMode Mode = FightMode::None;
		std::string Target;                      // whom it fights, or flees from
		glm::vec2 Pos{ 0.0f };                   // where it is (Place, or moved here)
		glm::vec2 Home{ 0.0f };                  // an animal goes back to wandering here when released
		float Swing = 0.0f;                      // seconds until it may swing again
		float Morale = 1.0f;
		float Fled = 0.0f;                       // seconds an Entity fighter has been fleeing (it is released after a while)
		bool Resting = false, Cared = false;     // (the owner says: the wounds heal faster)
		float Heal = 1.0f;                       // (the owner says: a healer's place near it multiplies the healing)
		std::vector<Wound> Wounds;
		int Rolls = 0;                           // rolls made (the counter of its hash)
		int Landed = 0, Missed = 0, Blocked = 0, Taken = 0, Kills = 0;
		std::string KilledBy;
		std::string LastHitBy;                   // who struck it last (a death by bleeding is laid at its door)
		float Lost = 0.0f;                      // health lost to blows since it last joined a fight (for morale)
		bool operator==(const Combatant&) const = default;
		float Severity() const;                  // total severity of its wounds
	};

	// Something the owners hear about: "hit" (Who hit Other for Amount), "miss", "block", "wound" (Other got a wound of Text), "death" (Other died,
	// Who killed it; Text = "Wounds" or "Bleeding"), "flee" (Who flees from Other), "engage" (Who attacks Other), "gives up" (an animal lets go),
	// "prey" (a predator chooses Other). Numbered: each owner keeps the last Seq it read.
	struct CombatEvent
	{
		uint64_t Seq = 0;
		double Time = 0.0;
		std::string Kind, Who, Other, Text;
		std::string Side;                        // the side of Other (of Who when there is no Other): what a dead animal was, after the ecology took it away
		float Amount = 0.0f;
		glm::vec2 Pos{ 0.0f };
	};

	class CombatSystem
	{
	public:
		CombatRules Rules = DefaultCombatRules();

		// The entities (to move Entity fighters and find animals) and their ability systems. Registers the combat content in the library.
		void Bind(EntityWorld& entities, GameplayWorld& gameplay);
		// The rules changed (a content load): the abilities and effects again.
		void Refresh();
		void Clear();

		// A fighter: `side` its clan or tag. Gives its ability system Health (when it has none: `health`) and Stamina, and the combat abilities.
		// Already in: only the side is updated. Null when it has no ability system.
		Combatant* Join(const std::string& name, const std::string& side, bool entity = false, float health = 100.0f);
		// An animal of a predator or a hunt rule joins as an Entity fighter of its tag, with that rule's health, speed, weapon and boldness.
		// Null for an animal no rule names.
		Combatant* JoinAnimal(const std::string& name);
		void Leave(const std::string& name);
		Combatant* Find(const std::string& name);
		const Combatant* Find(const std::string& name) const;
		const std::map<std::string, Combatant>& All() const { return m_Fighters; }
		// The owner of an External fighter says where it is (every frame it is in the world; also for the predators that look for prey).
		void Place(const std::string& name, const glm::vec2& pos);

		// `a` attacks `b` (both must have joined): false when either is dead or missing. An Entity foe that does not flee fights back.
		bool Engage(const std::string& a, const std::string& b);
		// `a` flees from `from` (it stops fighting); with an ability system it uses "Flee".
		bool Flee(const std::string& a, const std::string& from);
		// Out of the fight (an animal goes back to its life).
		void Release(const std::string& a);
		// Who attack `name` now (Engage on it).
		std::vector<std::string> AttackersOf(const std::string& name) const;
		bool InFight(const std::string& name) const;
		bool IsDead(const std::string& name) const;
		// Takes `amount` health away and maybe wounds (a fall, a test). Returns whether it died.
		bool Hurt(const std::string& name, float amount, const std::string& woundKind, float severity, const std::string& by = std::string());
		// The fighter's place among its own: whether `name` has someone of its side within `radius` (not counting itself).
		bool Accompanied(const std::string& name, float radius) const;
		// A rough worth of a fighter: health x the damage of its weapon x its strength (for the odds of a fight).
		float Strength(const std::string& name) const;

		// The clock (seconds) and one step: stamina, wounds (bleeding, healing, the slowing effects, death), the predators that look for prey,
		// moving Entity fighters, the swings of whoever is within reach, morale.
		void Update(float dt, float daySeconds = 1200.0f);
		double Time() const { return m_Time; }

		// What happened, numbered (the last 400 kept).
		const std::vector<CombatEvent>& Events() const { return m_Events; }
		uint64_t LastSeq() const { return m_Seq; }
		std::vector<CombatEvent> EventsSince(uint64_t seq) const;

		std::string Report() const;              // one line per fighter that is fighting, fleeing, dead or wounded
		std::string Status(const std::string& name) const;

		// Saving: the fighters (modes, targets, wounds, stamina is in their ability systems), the clock and the counters. FromJson goes after
		// Bind; false (and nothing changed) on a mistake. Swings in flight are not kept (their abilities are cancelled on a load).
		std::string ToJson() const;
		bool FromJson(const std::string& json);

	private:
		void Resolve(const std::string& attacker, const std::string& defender);
		void Answer(const std::string& b, const std::string& a);   // an Entity fighter `b` answers `a`: prey flees, the rest fights back
		void Arm(Combatant& c);
		void Emit(const std::string& kind, const std::string& who, const std::string& other, const std::string& text = std::string(), float amount = 0.0f);
		void Kill(Combatant& c, const std::string& by, const std::string& cause);
		void StepWounds(Combatant& c, AbilitySystemComponent& asc, float dt, float daySeconds);
		void StepPredators();
		void MoveEntity(Combatant& c, float dt);
		bool Locate(const std::string& name, glm::vec2& pos);
		void SetWoundEffect(AbilitySystemComponent& asc, float severity);

		EntityWorld* m_Entities = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		CombatRules m_Registered;
		bool m_HasRegistered = false;
		std::map<std::string, Combatant> m_Fighters;
		std::map<std::string, std::pair<const AbilitySystemComponent*, int>> m_Armed;   // name -> the ability system its hit listener is on
		std::vector<CombatEvent> m_Events;
		uint64_t m_Seq = 0;
		double m_Time = 0.0;
		float m_Scan = 0.0f;                      // seconds since the predators last looked
	};
}
