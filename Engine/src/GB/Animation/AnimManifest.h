#pragma once
#include "GB/Animation/AnimInstance.h"
#include "GB/Animation/Montage.h"
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

	struct AnimManifest
	{
		int Version = 1;
		std::string AirClip = "jump";            // the jump / fall pose of the air layer
		std::vector<ManifestSlot> Slots;
		ManifestSet Free;                         // the default set (name "default"): its clips and turns
		std::vector<ManifestSet> Sets;            // the other sets
		std::vector<MontageDesc> Montages;
		std::vector<ManifestPack> Packs;
	};

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

	// Problems of the manifest by itself (repeated names, a set with fewer than two clips, notifies out of 0..1, an empty clip name...).
	std::vector<std::string> ValidateManifest(const AnimManifest& manifest);

	// The clips a model has that nothing in the manifest uses.
	std::vector<std::string> UnusedClips(const AnimManifest& manifest, const Model& model);
}
