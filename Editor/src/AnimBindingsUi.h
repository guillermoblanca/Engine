#pragma once
#include "GB/Animation/AnimBindings.h"
#include <string>
#include <vector>

namespace GB::Editor
{
	// What the "Animation Bindings" window remembers between frames. The data itself (the bindings, their resolution, validation and application)
	// is GB::AnimBindings in the engine and lives in PlayerMode; this is only the selection, the preview state and the test of a transition.
	struct AnimBindingsUi
	{
		std::string Selected;                  // the action being edited ("ability:Chop")
		int Clip = 0;                          // the clip of that binding being edited
		int Transition = -1;                   // the row of the transition list being edited
		char Filter[48] = "";
		std::string Status;                    // the result of the last Save / Load / Apply
		std::string PreviewSignature;          // what the preview montage was last built from
		std::vector<std::string> LoggedIssues; // the issues last sent to the log (they are logged again only when they change)
		bool ThisSkeletonOnly = true;          // new bindings are scoped to the controlled character's skeleton
		bool Loaded = false;

		// the A -> B test: play A, then B after `Delay` seconds, with the transition being edited
		int TestStage = 0;                     // 0 idle, 1 playing A
		float TestTimer = 0.0f;
		float TestDelay = 0.6f;
		int Focus = 0;
	};
}
