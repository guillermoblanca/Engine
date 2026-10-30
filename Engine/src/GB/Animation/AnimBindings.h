#pragma once
#include "GB/Animation/AnimManifest.h"
#include "GB/Animation/Montage.h"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace GB
{
	class GameplayLibrary;
	class AbilitySystemComponent;
	struct AbilityDef;

	// Animation bindings: which clip each game action plays and how, as data the user edits (docs/EDITOR_ANIMACIONES.md). They are an overlay on
	// the manifest (AnimManifest): without a file, or for an action the file does not mention, the manifest and the C++ defaults (the Attack.*
	// montages of the combat rules, the abilities' own lists) are what the game plays, exactly as before. The overlay never bypasses the
	// existing path: ApplyBindings rewrites the manifest's `abilities`, `activities`, locomotion sets and transitions, and the game reads those
	// the way it always did (ApplyManifestAbilities, TribeVisuals, ApplyManifest).
	constexpr int kAnimBindingsVersion = 1;

	// One clip of a binding. `Source` is a montage of the manifest (its notifies, slot and tuning are the base) or, if no montage has that name,
	// a clip of the model (a montage is made for it). Every other field is an override: unset keeps the base.
	struct BindingClip
	{
		std::string Source;
		std::string Replaces;                        // locomotion only: the clip of the set this one replaces
		std::optional<bool> Loop;
		std::optional<float> TrimStart, TrimEnd;     // fractions (0..1) of the clip that play
		std::optional<float> Speed;                  // play rate (1 = as authored)
		std::optional<float> BlendIn, BlendOut;      // seconds
		std::optional<BlendCurve> Curve;
		std::optional<bool> KeepRootMotion;          // true: the clip's own travel stays (feet are not pinned to the controller)
		bool HasOverrides() const { return Loop || TrimStart || TrimEnd || Speed || BlendIn || BlendOut || Curve || KeepRootMotion; }
	};

	// What the action is:
	//   "ability:<name>"   an ability of the ability system: Chop, Mine, Gather, Attack.axe, Block, Stagger (damage received)...
	//   "activity:<job>"   what a tribe member does standing still: sleep, eat, dead, farm.till, talk.chat...
	//   "loco:<set>"       a locomotion set (default, crouch, injured...): each clip replaces one of the set's (Replaces)
	//   "air"              the jump / fall pose
	struct AnimBinding
	{
		std::string Action;
		std::string Skeleton;                        // "" = every model; else the model key (see SkeletonKey)
		std::map<std::string, std::string> When;     // context ("node": "tree", "tool": "axe"): all must match to be chosen
		std::vector<BindingClip> Clips;              // played in turn, one per use
		std::string Set;                             // activities: locomotion set while it lasts
		std::optional<bool> Repeat;                  // activities: played again while the job lasts
		std::string Note;
	};

	// A blend between two montages when one replaces the other in a slot. `From` and `To` are actions (their montages), montage names, or "*".
	struct BindingTransition
	{
		std::string From, To;
		std::string Skeleton;
		float Duration = 0.2f;                       // seconds of crossfade
		BlendCurve Curve = BlendCurve::Smooth;
	};

	// The tuning of the locomotion blend space (see ManifestLocomotion): the limits of the speed factor, the smoothing of the velocity (the time of the
	// blend between idle, walk and run), the gait share for footsteps, the fade of a change of set, and the reference speed of each clip of a set (the
	// ground speed at which it plays at its natural rate, which is also its place on the speed axis: where the blend passes from the idle to the walk and
	// from the walk to the run). Scoped to a model or to all. Which clip goes in each stretch is the `loco:<set>` binding (Replaces).
	struct LocomotionTuning
	{
		std::string Skeleton;                        // "" = every model
		ManifestLocomotion Values;
	};

	struct AnimBindings
	{
		int Version = kAnimBindingsVersion;
		std::vector<AnimBinding> Bindings;
		std::vector<BindingTransition> Transitions;
		std::vector<LocomotionTuning> Locomotion;
	};

	// The tuning that holds for a model: the general entries first, then the ones of the model on top (a field it sets wins, a clip speed of the same
	// set and clip wins).
	ManifestLocomotion LocomotionFor(const AnimBindings& bindings, const std::string& skeleton);

	// The model key bindings are scoped by: the file name without folder or extension, lower case ("x bot", "fox").
	std::string SkeletonKey(const std::string& modelPath);

	std::string AnimBindingsToJson(const AnimBindings& bindings);
	bool AnimBindingsFromJson(const std::string& text, AnimBindings& out, std::string* error = nullptr);
	// A missing file is not an error: it gives empty bindings and `found` false (the game plays its defaults).
	bool LoadAnimBindingsFile(const std::string& path, AnimBindings& out, std::string* error = nullptr, bool* found = nullptr);
	bool SaveAnimBindingsFile(const std::string& path, const AnimBindings& bindings, std::string* error = nullptr);

	// What the game plays without bindings, by action: the abilities' montage lists (the library's, then the manifest's, which wins as it does at
	// runtime), so Attack.*, Block and Stagger show their C++ defaults too.
	struct BindingDefaults
	{
		std::map<std::string, std::vector<std::string>> Abilities;
		std::vector<std::string> OneShotAbilities;
	};
	BindingDefaults MakeBindingDefaults(const AnimManifest& manifest, const GameplayLibrary* library);
	// The same from a copy of the library's montage lists (ability name -> montages) taken before any binding was applied to it.
	BindingDefaults MakeBindingDefaults(const AnimManifest& manifest, const std::map<std::string, std::vector<std::string>>& libraryMontages);
	// The montage list of every ability of the library, for the copy above.
	std::map<std::string, std::vector<std::string>> AbilityMontageLists(const GameplayLibrary& library);
	// Puts those lists back into the library (undoes ApplyManifestAbilities of an earlier manifest). Returns how many abilities changed.
	int RestoreAbilityMontages(GameplayLibrary& library, const std::map<std::string, std::vector<std::string>>& lists);

	// A bindable action, for the editor's list.
	struct BindableAction
	{
		std::string Key;                             // "ability:Chop"
		std::string Category;                        // Locomotion, Interaction, Attack, Defence, Life, Social, Other
		std::vector<std::string> Defaults;           // montages (or clips for locomotion) it plays without bindings
		bool OneShot = true;                         // abilities are; activities and locomotion may loop
	};
	std::vector<BindableAction> ListBindableActions(const AnimManifest& manifest, const BindingDefaults& defaults);

	// What an action plays now: the answer of the single resolution layer.
	struct ResolvedClip
	{
		bool Found = false;
		bool FromBinding = false;                    // false: the default (manifest or C++) answered
		std::string Why;                             // "binding", "default", or why a binding was skipped
		std::string Montage;                         // the manifest montage the base came from ("" when the source is a bare clip)
		std::string Playable;                        // the montage that plays it once ApplyBindings has run: the base itself, or the alias made for it ("" = no binding)
		std::string Clip;
		bool Loop = false;
		float TrimStart = 0.0f, TrimEnd = 1.0f;
		float Speed = 1.0f;
		float BlendIn = 0.25f, BlendOut = 0.25f;
		BlendCurve Curve = BlendCurve::Smooth;
		bool KeepRootMotion = false;
		std::string Set;
		bool Repeat = false;
	};

	using ClipExists = std::function<bool(const std::string&)>;

	// ResolveClip(action, context) -> {clip, loop, trim, speed, blend}. The binding for `action` that matches the skeleton and whose `When` holds
	// in `context` (the most specific wins) gives its clip number `turn` (modulo the clips); a binding whose clip does not exist (`hasClip` says so
	// for bare clips, the manifest for montages) is skipped and the default answers, with the reason in Why. `hasClip` null: bare clips are trusted.
	ResolvedClip ResolveClip(const AnimBindings& bindings, const AnimManifest& manifest, const BindingDefaults& defaults, const std::string& action,
		const std::map<std::string, std::string>& context = {}, const std::string& skeleton = "", int turn = 0, const ClipExists& hasClip = nullptr);

	// The montage description a resolved clip stands for (name left empty).
	MontageDesc ToMontage(const ResolvedClip& r, const AnimManifest& manifest);
	// The resolved clip of a montage description (the other direction, to show defaults and to preview).
	ResolvedClip FromMontage(const MontageDesc& d);

	// What a game keeps of the bindings so every character can use them: the bindings, the defaults they replace, and the model they are limited to.
	// Shared by all the actors of an ActorWorld (ActorWorld::SetBindings); each actor works out its own manifest and picks its clips (Actor::PickMontage)
	// from it, so two models with different bindings do not fight over the shared ability library.
	struct BindingsSetup
	{
		AnimBindings Bindings;
		BindingDefaults Defaults;
		std::string Only;                            // "" = every character; else only the model with this SkeletonKey (the others play their defaults)
		bool Applies(const std::string& skeleton) const { return Only.empty() || Only == skeleton; }
	};

	// The context a use of an ability happens in, for the `When` of a binding: "ability" (its name), "target" (the entity worked on), "node" (the
	// resource node type: tree, rock, berry_bush...), "kind" (its resource kind: wood, stone, plant...), "tool" (the family of the tool the ability
	// asks for: axe, pickaxe, "none" when it asks for none) and "variant" (that tool as the owner holds it: basic, flint...). Keys with nothing to say
	// are left out.
	std::map<std::string, std::string> BindingContext(const AbilityDef& ability, const AbilitySystemComponent* owner, const AbilitySystemComponent* target);

	struct BindingsReport
	{
		int Applied = 0;                             // actions rewritten
		int Transitions = 0;
		bool Locomotion = false;                     // the blend space was tuned
		std::vector<std::string> Skipped;            // "ability:Chop #1: clip 'x' does not exist (default kept)"
	};

	// Writes the bindings into the manifest (see the top of this file). Idempotent on a fresh copy of the manifest. Returns the report; the skipped
	// entries are also logged as warnings.
	BindingsReport ApplyBindings(AnimManifest& manifest, const AnimBindings& bindings, const BindingDefaults& defaults, const std::string& skeleton = "",
		const ClipExists& hasClip = nullptr);

	struct BindingIssue
	{
		enum class Level { Warning, Error };
		Level Severity = Level::Warning;
		std::string Text;
	};
	// Problems of the bindings: clips that do not exist, bad trims / speeds / blends, loops on one-shot actions, locomotion that replaces nothing,
	// transitions whose ends play nothing (no way out) or that go nowhere.
	std::vector<BindingIssue> ValidateBindings(const AnimBindings& bindings, const AnimManifest& manifest, const BindingDefaults& defaults, const ClipExists& hasClip = nullptr);
	// The issues as lines for the console ("error: ..." / "warning: ...").
	std::vector<std::string> DescribeIssues(const std::vector<BindingIssue>& issues);
}
