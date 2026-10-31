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
		bool ThisSkeletonOnly = false;         // new bindings are scoped to the controlled character's model (default: every model)
		int Variant = 0;                       // which binding of the action (the always one, or one with a context) is being edited
		bool Live = true;                      // a committed edit is applied to every character at once
		bool ApplyOnlyThisModel = false;       // "apply to all" limited to the controlled character's model
		std::string AppliedSignature;          // what was last applied (bindings + model limit): a different one is applied by Live
		std::string TryNode;                   // the node type "Try on a node" uses
		bool Loaded = false;

		// the A -> B test: play A, then B after `Delay` seconds, with the transition being edited
		int TestStage = 0;                     // 0 idle, 1 playing A
		float TestTimer = 0.0f;
		float TestDelay = 0.6f;
		int Focus = 0;
	};
}
