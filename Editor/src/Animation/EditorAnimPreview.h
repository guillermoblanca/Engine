#pragma once
#include "GB/Animation/AnimBindings.h"
#include "GB/Animation/Montage.h"

namespace GB::Editor
{
	MontageDesc MakeNamedPreviewMontage(const ResolvedClip& clip, const AnimManifest& manifest, const char* name);
	bool ResolveTransitionPreviewMontage(MontagePlayer& player, const AnimBindings& bindings, const AnimManifest& manifest,
		const BindingDefaults& defaults, const std::string& end, const std::string& skeleton, const ClipExists& hasClip, const char* name);
	void AddPreviewTransition(MontagePlayer& player, const char* from, const char* to, float duration, BlendCurve curve);
}
