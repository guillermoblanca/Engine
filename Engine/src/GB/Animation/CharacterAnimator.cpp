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
		m_Anim.Montages().AddSlot("UpperBody", "mixamorig:Spine");
		m_Anim.Air().SetClip("jump");
		// turning on the spot: the clip's side is read from its own rotation, so a pair can be given in any order
		for (const char* clip : { "left turn 90", "right turn 90" }) m_Anim.Turn().AddClip("", clip);
		for (const char* clip : { "box turn", "box turn (2)" }) m_Anim.Turn().AddClip("box", clip);
		for (const char* clip : { "holding turn left", "holding turn right" }) m_Anim.Turn().AddClip("holding", clip);
		for (const char* clip : { "injured turn left", "injured turn right" }) m_Anim.Turn().AddClip("injured", clip);
		int added = 0;
		for (const MontageDesc& m : MixamoMontages())
			added += m_Anim.Montages().Add(m) ? 1 : 0;
		for (const MontageDesc& m : UniversalMontages())
			added += m_Anim.Montages().Add(m) ? 1 : 0;
		return added;
	}
}
