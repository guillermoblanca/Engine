#pragma once
#include "GB/Animation/ClipAnalysis.h"
#include "GB/Animation/Rig.h"
#include <string>
#include <vector>

namespace GB
{
	// What the engine can tell about a character right after importing it: which skeleton convention it follows, which humanoid bones it
	// has, how big it is and which way it faces, which animation clips fill which role (idle, walk, run, strafes, jump, turns), which
	// locomotion sets and farming montages its clips allow, and what is wrong with it. The "character import" check.
	struct CharacterProfile
	{
		std::string Convention;                  // "mixamo", "unreal", "unity", "biped", or "" when it is not a humanoid we recognise
		HumanoidRig Rig;
		ClipAnalysisConfig Analysis;             // the bone names of this skeleton, for the clip analysis and the locomotion
		std::vector<std::string> MissingBones;   // humanoid bones the skeleton lacks ("left hand", "right foot"...)
		float Height = 0.0f;                     // meters, from the head to the feet
		bool FacesForward = true;                // toes point to +Z, the engine's forward
		std::vector<std::string> Sockets;        // the standard sockets that can be made
		struct Role { std::string Name, Clip; };
		std::vector<Role> Roles;                 // idle, walk, run, strafe_left, strafe_right, jump, turn_left, turn_right: the clip for each found
		std::vector<std::string> Sets;           // locomotion sets (MixamoSets) whose clips are there
		std::vector<std::string> Montages;       // montages (MixamoMontages) whose clips are there
		int ClipCount = 0;
		std::vector<std::string> Warnings;

		bool Humanoid() const { return !Convention.empty() && Rig.HasLegs(); }
		bool CanLocomote() const;                // an idle and something that moves
		bool Ok() const { return Warnings.empty(); }
		const std::string* RoleClip(const std::string& role) const;
	};

	CharacterProfile AnalyzeCharacter(const Model& model);
	std::string FormatCharacterProfile(const CharacterProfile& profile);
	std::string CharacterProfileToJson(const CharacterProfile& profile);   // for agents and diffs
}
