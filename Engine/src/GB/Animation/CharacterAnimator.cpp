#include "GB/Animation/CharacterAnimator.h"

namespace GB
{
	int CharacterAnimator::AddDefaultLocomotionClips()
	{
		int added = 0;
		for (const char* name : { "idle", "walking", "standard run", "left strafe walking", "right strafe walking", "left strafe", "right strafe" })
			added += m_Anim.Default().AddClip(name) ? 1 : 0;
		return added;
	}

	int CharacterAnimator::AddRoleClips(const CharacterProfile& profile)
	{
		int added = 0;
		for (const char* role : { "idle", "walk", "run", "strafe_left", "strafe_right" })
			if (const std::string* clip = profile.RoleClip(role)) added += m_Anim.Default().AddClip(*clip) ? 1 : 0;
		return added;
	}

	std::vector<std::string> CharacterAnimator::AddMixamoSets()
	{
		std::vector<std::string> added;
		for (const LocomotionSetDesc& set : MixamoSets())
			if (m_Anim.AddSet(set)) added.push_back(set.Name);
		return added;
	}

	int CharacterAnimator::AddMixamoActions()
	{
		// the slots, turn clips, air clip and montages come from the manifest (MixamoManifest() when no file gives another)
		const AnimManifest manifest = MixamoManifest();
		for (const ManifestSlot& slot : manifest.Slots)
			if (!slot.Name.empty()) m_Anim.Montages().AddSlot(slot.Name, slot.RootBone);
		m_Anim.Air().SetClip(manifest.AirClip);
		// turning on the spot: the clip's side is read from its own rotation, so a pair can be given in any order
		for (const std::string& clip : manifest.Free.Turns) m_Anim.Turn().AddClip("", clip);
		for (const ManifestSet& set : manifest.Sets)
			for (const std::string& clip : set.Turns) m_Anim.Turn().AddClip(set.Name, clip);
		int added = 0;
		for (const MontageDesc& m : manifest.Montages)
			added += m_Anim.Montages().Add(m) ? 1 : 0;
		for (const MontageDesc& m : UniversalMontages())   // the Universal Animation Library's actions, for a character retargeted onto
			added += m_Anim.Montages().Add(m) ? 1 : 0;
		return added;
	}
}
