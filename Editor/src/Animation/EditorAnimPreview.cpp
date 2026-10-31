#include "Animation/EditorAnimPreview.h"
#include <algorithm>

namespace GB::Editor
{
	MontageDesc MakeNamedPreviewMontage(const ResolvedClip& clip, const AnimManifest& manifest, const char* name)
	{
		MontageDesc result = ToMontage(clip, manifest);
		result.Name = name;
		return result;
	}

	bool ResolveTransitionPreviewMontage(MontagePlayer& player, const AnimBindings& bindings, const AnimManifest& manifest,
		const BindingDefaults& defaults, const std::string& end, const std::string& skeleton, const ClipExists& hasClip, const char* name)
	{
		ResolvedClip resolved;
		if (end == "*")
		{
			for (const std::string& montageName : player.Names())
			{
				const MontageDesc* montage = player.Get(montageName);
				if (montage && montage->Name.rfind("bind:", 0) != 0 && !montage->Loop)
				{
					resolved = FromMontage(*montage);
					break;
				}
			}
		}
		else if (const MontageDesc* montage = player.Get(end))
			resolved = FromMontage(*montage);
		else
			resolved = ResolveClip(bindings, manifest, defaults, end, {}, skeleton, 0, hasClip);
		if (!resolved.Found) return false;

		MontageDesc preview = MakeNamedPreviewMontage(resolved, manifest, name);
		player.Remove(name);
		return player.Add(preview);
	}

	void AddPreviewTransition(MontagePlayer& player, const char* from, const char* to, float duration, BlendCurve curve)
	{
		std::vector<MontageTransition> transitions = player.Transitions();
		transitions.erase(std::remove_if(transitions.begin(), transitions.end(), [from, to](const MontageTransition& transition)
		{
			return transition.From == from && transition.To == to;
		}), transitions.end());
		transitions.push_back({ from, to, duration, curve });
		player.SetTransitions(transitions);
	}
}
