#pragma once
#include "GB/Animation/AnimInstance.h"
#include "GB/Game/CharacterController.h"
#include "GB/Game/Entity.h"
#include "GB/Game/Interaction.h"
#include "GB/Gameplay/AttributeSet.h"
#include "GB/Gameplay/GameplayAbility.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	class AbilitySystemComponent;
	class AbilityTask;

	struct GameplayEventData
	{
		std::string Tag;
		AbilitySystemComponent* Instigator = nullptr;
		AbilitySystemComponent* Target = nullptr;
	};

	// What the host lends an ability system so its abilities can move and animate a character. All optional: without them the abilities run
	// on timers (headless logic, tests), and the same effects happen at the same moments.
	struct AbilityRuntime
	{
		AnimInstance* Anim = nullptr;
		CharacterController* Controller = nullptr;
		const CharacterWorld* World = nullptr;
		// Finds an interaction point of an entity (see FindInteraction).
		std::function<bool(const std::string& entity, const std::string& point, InteractionTarget& out, std::string* error)> Resolve;
		// Puts a prop in the character's hand (true) or takes it away (false): "box", "can", "shovel"...
		std::function<bool(const std::string& prop, bool on)> Equip;
	};

	// What a running ability system keeps that its scene component does not: the timed effects still running (cooldowns, regrowth) with their
	// clocks, and how many times each ability started (it rotates its montages). Abilities in flight are not part of it (they are cancelled).
	struct EffectRuntime
	{
		std::string Name;
		float Remaining = 0.0f, PeriodTimer = 0.0f;
	};

	struct AbilityRuntimeState
	{
		std::vector<EffectRuntime> Effects;
		std::map<std::string, int> Uses;
		int Completed = 0;
	};

	// One run of an ability: its tasks in order, who it acts on and what it has done.
	struct AbilityInstance
	{
		AbilityInstance();
		~AbilityInstance();
		const AbilityDef* Def = nullptr;
		AbilitySystemComponent* Target = nullptr;
		std::string TargetName;
		std::vector<std::unique_ptr<AbilityTask>> Tasks;
		size_t Current = 0;
		bool Committed = false, Equipped = false, TaskBegun = false, Done = false;
		std::string Montage;                 // the one chosen for this use
		std::vector<bool> Fired;             // per event of the definition: already happened
		std::vector<std::string> Received;   // events that arrived while it ran (for WaitGameplayEventTask)
		std::string Error;
	};

	// Gameplay Ability System in small: the tags, attributes, effects and abilities of one entity (the player, an NPC, a plot of soil, a tree).
	// It is CPU only and deterministic: the same calls with the same time steps give the same state. A character's abilities are driven by
	// Update(dt), which must come before the character's controller step; with a runtime they walk and animate the character and
	// Input() is what it wants to do that frame.
	class AbilitySystemComponent
	{
	public:
		AbilitySystemComponent(std::string name, const GameplayLibrary& library) : m_Name(std::move(name)), m_Library(&library) {}
		~AbilitySystemComponent();
		AbilitySystemComponent(const AbilitySystemComponent&) = delete;
		AbilitySystemComponent& operator=(const AbilitySystemComponent&) = delete;

		const std::string& Name() const { return m_Name; }
		AttributeSet& Attributes() { return m_Attributes; }
		const AttributeSet& Attributes() const { return m_Attributes; }
		GameplayTagContainer& Tags() { return m_Tags; }
		const GameplayTagContainer& Tags() const { return m_Tags; }
		bool HasTag(const std::string& tag) const { return m_Tags.Has(tag); }
		float Attribute(const std::string& name) const { return m_Attributes.Current(name); }

		// ---- effects
		// False for an unknown effect. Instant effects are applied and gone; the others stay active (see GameplayEffectDef).
		bool ApplyEffect(const std::string& name);
		bool ApplyEffect(const GameplayEffectDef& def);
		int RemoveEffect(const std::string& name);   // active effects with this name taken away; how many
		const std::vector<ActiveEffect>& ActiveEffects() const { return m_Effects; }
		bool EffectActive(const std::string& name) const;

		// ---- abilities
		bool Grant(const std::string& ability);      // false for an unknown ability
		bool Revoke(const std::string& ability);     // cancels it if it runs
		bool HasAbility(const std::string& ability) const;
		const std::vector<std::string>& Granted() const { return m_Granted; }
		// Can it start now? Requirements: granted, not running, required / blocked tags, cooldown, cost, and a valid target when it needs one.
		bool CanActivate(const std::string& ability, const AbilitySystemComponent* target, std::string* why = nullptr) const;
		// Starts it on `target` (`targetName` is the entity to walk to; it defaults to the target's name). False and `why` when it cannot.
		bool Activate(const std::string& ability, AbilitySystemComponent* target, std::string* why = nullptr, const std::string& targetName = "");
		bool Cancel(const std::string& ability);
		void CancelAll();
		int CancelWithTag(const std::string& tag);   // abilities whose tag is `tag` or below
		bool IsActive(const std::string& ability) const;
		bool AnyActive() const { return !m_Running.empty(); }
		std::vector<std::string> ActiveAbilities() const;
		float CooldownRemaining(const std::string& ability) const;
		// The last ability that failed or was cancelled, and why ("" when the last one ended well).
		const std::string& LastError() const { return m_LastError; }
		int Completed() const { return m_Completed; }   // abilities that ended well, ever

		// ---- events
		using Listener = std::function<void(const GameplayEventData&)>;
		// `tag` matches the events at or below it. Returns an id for RemoveListener.
		int AddListener(const std::string& tag, Listener fn);
		void RemoveListener(int id);
		void SendEvent(const GameplayEventData& event);

		// ---- time and the character
		void Update(float dt);
		void SetRuntime(AbilityRuntime* runtime) { m_Runtime = runtime; }
		AbilityRuntime* Runtime() const { return m_Runtime; }
		// With a runtime and an ability running, the ability moves the character: use Input() instead of keys or tasks.
		bool Controlling() const { return m_Runtime && !m_Running.empty(); }
		const CharacterInput& Input() const { return m_Input; }

		// ---- scenes: the state as an entity component (see Entity.h) and back
		GameplayComponent ToComponent() const;
		void FromComponent(const GameplayComponent& c);
		// The clocks that go with the component (see AbilityRuntimeState), for saved games. Restore after FromComponent; false and `error` for an
		// effect the library does not know.
		AbilityRuntimeState RuntimeState() const;
		bool RestoreRuntimeState(const AbilityRuntimeState& state, std::string* error = nullptr);

		// Used by the tasks.
		const GameplayLibrary& Library() const { return *m_Library; }
		void FireNotify(AbilityInstance& instance, const std::string& notify);
		void FireUnfired(AbilityInstance& instance);

	private:
		struct ListenerEntry { int Id; std::string Tag; Listener Fn; };
		const AbilityDef* Def(const std::string& name) const { return m_Library->Ability(name); }
		bool Commit(AbilityInstance& instance);
		void Finish(AbilityInstance& instance, bool ok);
		void Expire(size_t index);
		void ApplyPeriodic(const GameplayEffectDef& def);

		std::string m_Name;
		const GameplayLibrary* m_Library;
		AttributeSet m_Attributes;
		GameplayTagContainer m_Tags;
		std::vector<ActiveEffect> m_Effects;
		std::vector<std::string> m_Granted;
		std::vector<std::unique_ptr<AbilityInstance>> m_Running;
		std::map<std::string, int> m_Uses;   // per ability: how many times it started (to rotate its montages)
		std::vector<ListenerEntry> m_Listeners;
		AbilityRuntime* m_Runtime = nullptr;
		CharacterInput m_Input;
		std::string m_LastError;
		int m_NextEffectId = 1, m_NextListener = 1, m_Completed = 0;
	};

	// Adds to the character's montage player the montages the abilities of the library play, with the notifies that raise their events
	// (AbilityMontages of `base`). `base` is where the montages come from: the animation manifest's, which owns their notifies. Without it, the
	// built-in MixamoMontages(). Returns how many the character has clips for.
	int RegisterAbilityMontages(const GameplayLibrary& library, AnimInstance& anim, const std::vector<MontageDesc>& base);
	int RegisterAbilityMontages(const GameplayLibrary& library, AnimInstance& anim);
}
