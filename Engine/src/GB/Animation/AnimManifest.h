#pragma once
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/Montage.h"
#include <optional>
#include <string>
#include <vector>

namespace GB
{
	// What a character's animation packs give it, as data: which clips make each locomotion set (free, crouched, sword and shield, bow,
	// carrying a box...), which clips turn it on the spot, the jump clip, the slots and the montages (actions with their notifies and
	// sections). It replaces the lists of clip names that used to be written in C++ (MixamoSets, MixamoMontages, the turn clips): a new
	// pack is a new entry in a JSON file, not a recompile. Montages use exactly the fields of MontagesToJson (the Timeline panel edits them),
	// and the animation graph refers to the sets and montages by the names given here.
	struct ManifestSlot
	{
		std::string Name;       // "UpperBody"
		std::string RootBone;   // everything below this bone is the slot
	};

	struct ManifestSet
	{
		std::string Name;                 // "crouch", "sword_shield"; the free-movement set is the default set
		std::vector<std::string> Clips;   // the gaits: idle, walk, run, strafes, backwards... placed by their measured velocity
		std::vector<std::string> Turns;   // turn-on-the-spot clips (left and right are read from the clips themselves)
		std::string Pack;                 // where the clips come from (informative: "locomotion", "magic"...)
		std::string Note;
	};

	struct ManifestPack
	{
		std::string Id;                   // "locomotion", "farming", "injured", "magic", "longbow", "swordshield", "gestures"
		std::string Description;
		std::vector<std::string> Clips;   // every clip of the pack, to know what a pack gives and what nothing uses yet
	};

	// What a gameplay action plays, as data (ANIMACIONES_USO.md): an ability of the ability system ("Chop", "Stoke", "Dig") or an activity of the
	// tribe, by the job a member is doing ("eat", "sleep", "craft", "socialize", "farm.till"...). The montages are names of this manifest's montages
	// (usually aliases of a clip with their own play rate and notifies: "chop_1" is "sword and shield slash" at 0.85 with "chop" at 0.48), played in
	// turn, one per use. An activity may also change the locomotion set while it lasts (hauling walks with the "box" set) and loop: played again
	// while the member keeps doing it.
	struct ManifestUse
	{
		std::string Name;
		std::vector<std::string> Montages;
		std::string Set;                  // locomotion set while it lasts ("" = unchanged)
		bool Loop = false;                // activities: played again (the next in turn) while the activity lasts
		std::string Note;                 // why this clip (and what is still missing: a request for the artist)
	};

	// A stage of life plays the same clips at its own pace (H9): a child a little faster, an elder slower. Multiplies the montage's rate.
	struct ManifestStage
	{
		std::string Name;                 // "child", "adult", "elder"
		float Rate = 1.0f;
	};

	// How the locomotion is tuned, as data (animation bindings; see AnimInstance::SetOptions): the limits of the factor that speeds a clip up or down to
	// keep the feet on the ground, how fast the blend follows the velocity (the time of the blend between idle, walk and run), the share of gait a
	// footstep needs, how long a change of locomotion set fades, and the ground speed (m/s) at which each clip plays at its natural rate (it is also
	// where it sits on the speed axis: idle < walk < run, and so where the blend passes from one to the next). Unset = what the engine does.
	struct ManifestClipSpeed
	{
		std::string Set;                  // "" = the default set
		std::string Clip;
		float Speed = 0.0f;
	};

	struct ManifestLocomotion
	{
		std::optional<float> MinSpeedScale, MaxSpeedScale, VelocitySmoothing, MinGaitWeight, SetFade;
		std::vector<ManifestClipSpeed> Clips;
		bool Empty() const { return !MinSpeedScale && !MaxSpeedScale && !VelocitySmoothing && !MinGaitWeight && !SetFade && Clips.empty(); }
	};

	struct AnimManifest
	{
		int Version = 1;
		std::string AirClip = "jump";            // the jump / fall pose of the air layer
		std::vector<ManifestSlot> Slots;
		ManifestSet Free;                         // the default set (name "default"): its clips and turns
		std::vector<ManifestSet> Sets;            // the other sets
		std::vector<MontageDesc> Montages;
		std::vector<ManifestPack> Packs;
		std::vector<ManifestUse> Abilities;       // ability name -> montages (see ApplyManifestAbilities)
		std::vector<ManifestUse> Activities;      // tribe job -> montages, set (see TribeVisuals)
		std::vector<ManifestStage> Stages;        // stage of life -> rate
		std::vector<MontageTransition> Transitions;   // blends between montages (animation bindings; see MontagePlayer::SetTransitions)
		ManifestLocomotion Locomotion;                // tuning of the blend space (animation bindings)
	};

	// The use called `name` of a list (Abilities or Activities), or null.
	const ManifestUse* FindUse(const std::vector<ManifestUse>& uses, const std::string& name);
	// The rate of a stage of life (1 when the manifest does not list it).
	float StageRate(const AnimManifest& manifest, const std::string& stage);

	// What loading a manifest into a character did.
	struct ManifestReport
	{
		std::vector<std::string> Sets;            // sets registered (the default one included when it got clips)
		int Montages = 0;                         // montages the character has clips for
		int TurnClips = 0;
		bool Air = false;
		std::vector<std::string> Missing;         // "set crouch: Crouch Idle", "montage dig_plant: dig and plant seeds", ... clips the model lacks
		std::vector<std::string> Unused;          // clips the model has that no set, turn, air or montage uses
	};

	// The manifest the C++ lists give (Mixamo locomotion, farming and injured packs): what a character gets without any file.
	AnimManifest MixamoManifest();

	std::string AnimManifestToJson(const AnimManifest& manifest);
	bool AnimManifestFromJson(const std::string& text, AnimManifest& out, std::string* error = nullptr);

	// Registers what the model has clips for into an AnimInstance: the slots, the default set's clips, the other sets, the turns, the air clip
	// and the montages. Entries whose clips are missing are skipped and listed in the report.
	ManifestReport ApplyManifest(const AnimManifest& manifest, const Model& model, AnimInstance& anim);

	// Problems of the manifest by itself (repeated names, a set with fewer than two clips, notifies out of 0..1, an empty clip name, an ability or
	// activity that plays a montage the manifest does not have or walks with a set it does not have, a stage rate that is not positive...).
	std::vector<std::string> ValidateManifest(const AnimManifest& manifest);

	// The clips a model has that nothing in the manifest uses.
	std::vector<std::string> UnusedClips(const AnimManifest& manifest, const Model& model);
}
