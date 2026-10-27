#pragma once
#include "GB/Animation/Montage.h"
#include "GB/Gameplay/GameplayEffect.h"
#include "GB/Gameplay/GameplayTag.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	// What happens at a moment of the ability's montage: the montage raises a notify with this name, and then the effects are applied to the
	// one who acts and to the target, and the event is sent to both (the target's listeners react to it: the soil turns over, the tree
	// drops a fruit). `Time` (0..1 of the clip) is where the notify sits in the montage; with no notify name the event happens at once.
	struct AbilityEventDef
	{
		std::string Notify;
		float Time = 0.5f;
		std::string EventTag;
		std::vector<std::string> SelfEffects, TargetEffects;
	};

	// The data of an ability. The behaviour is the same for all of them: check the requirements, pay the cost, go to the target, play the
	// montage, apply the effects its notifies raise, start the cooldown. See AbilitySystemComponent.
	struct AbilityDef
	{
		std::string Name;
		std::string Tag;                                  // "Ability.Dig": held by the owner while it runs
		std::vector<std::string> RequiredTags, BlockedTags;               // on the one who acts
		std::string RequiredProp;                                         // it must be carrying this prop (tag "State.Carrying.<prop>")
		std::vector<std::string> TargetRequiredTags, TargetBlockedTags;   // on the target (all of the first, none of the second)
		std::vector<std::string> TargetRequiredAnyTags;                   // on the target: at least one of them
		std::vector<std::pair<std::string, float>> TargetMinAttributes;   // attribute of the target >= value
		bool NeedsTarget = false;
		std::string Point;                                // interaction point of the target to stand on ("" = its first)
		bool MoveToTarget = true;                         // walk to the point first (MoveToTargetTask)
		std::string Cost;                                 // effect applied when the ability commits (it must be affordable to start)
		float CooldownSeconds = 0.0f;                     // > 0: starts the "Cooldown.<name>" effect when it commits
		std::vector<std::string> Montages;                // played in turn, one per use (pick_fruit, pick_fruit_2, ...); empty = no animation
		float Duration = 1.0f;                            // seconds of a "montage" when there is no animation to play (headless logic)
		std::vector<AbilityEventDef> Events;
		std::string WaitEvent;                            // after the montage wait for this event (WaitGameplayEventTask); "" = do not
		float WaitTimeout = 1.0f;
		std::vector<std::string> ActiveTags;              // granted while it runs ("State.Busy")
		std::vector<std::string> ActivationEffects;       // applied to the owner when it commits
		std::vector<std::string> RemoveEffects;           // active effects (by name) taken away when it commits
		std::vector<std::string> EndEffects;              // applied to the owner when it ends well
		std::string EquipProp;                            // the character takes this prop in hand when it starts ...
		bool KeepProp = false;                            // ... and lets go of it when it ends, unless this is true
		std::string UnequipProp;                          // lets go of this prop when it starts
		std::string Note;                                 // for listings
	};

	// The effects and abilities a game knows by name. Entities refer to them by name, so scenes stay small and the content lives in one place.
	class GameplayLibrary
	{
	public:
		void AddEffect(const GameplayEffectDef& def) { m_Effects[def.Name] = def; }
		void AddAbility(const AbilityDef& def) { m_Abilities[def.Name] = def; }
		const GameplayEffectDef* Effect(const std::string& name) const { auto it = m_Effects.find(name); return it == m_Effects.end() ? nullptr : &it->second; }
		const AbilityDef* Ability(const std::string& name) const { auto it = m_Abilities.find(name); return it == m_Abilities.end() ? nullptr : &it->second; }
		const std::map<std::string, GameplayEffectDef>& Effects() const { return m_Effects; }
		const std::map<std::string, AbilityDef>& Abilities() const { return m_Abilities; }
		void Clear() { m_Effects.clear(); m_Abilities.clear(); }

	private:
		std::map<std::string, GameplayEffectDef> m_Effects;   // map nodes do not move: pointers stay valid
		std::map<std::string, AbilityDef> m_Abilities;
	};

	// The montages `base` (usually the ones of the animation manifest, or MixamoMontages()) with the notifies the abilities of the library raise:
	// a montage an ability plays gets the notify of each of its events that it does not have yet, at the event's time. A notify the montage already
	// has (edited in the Timeline panel, or loaded from its JSON) keeps its own time. Montages no ability uses are kept as they are.
	std::vector<MontageDesc> AbilityMontages(const GameplayLibrary& library, std::vector<MontageDesc> base);
}
