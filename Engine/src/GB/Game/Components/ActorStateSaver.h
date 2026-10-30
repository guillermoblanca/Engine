#pragma once
#include "GB/Save/SaveGame.h"

namespace GB
{
	// What a component of an actor keeps in a save: each fills its own fields of the ActorState and puts them back. The actor adds name, kind and
	// place; it does not know what the components save. A component that saves nothing does not implement it.
	class IStateSaver
	{
	public:
		virtual ~IStateSaver() = default;
		virtual void CaptureState(Save::ActorState&) const {}
		virtual void RestoreState(const Save::ActorState&) {}
	};
}
