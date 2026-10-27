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

	std::vector<std::string> CharacterAnimator::AddMixamoSets()
	{
		std::vector<std::string> added;
		for (const LocomotionSetDesc& set : MixamoSets())
			if (m_Anim.AddSet(set)) added.push_back(set.Name);
		return added;
	}
}
