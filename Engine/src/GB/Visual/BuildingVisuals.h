#pragma once
#include "GB/Game/Entity.h"
#include "GB/Visual/VisualDefs.h"
#include <string>

namespace GB
{
	// The adapter between a building site (the settlement work of H8: construction sites with phases) and how it looks. A building is an entity
	// tagged "Building" and "Building.<id>" whose GameplayComponent carries the attributes "Phase" (0, 1, 2...; the number of phases or more is
	// finished) and "Progress" (0..1 through that phase); SceneVisuals draws it from the "buildings" of the visuals data (shelter, storage,
	// workbench, drying rack, kiln, palisade: foundation stakes, frame, walls, roof...). Nothing else is needed: H8 creates its site with Build and
	// moves it on with Set (or writes the two attributes in its own ability system: the live value wins, see SceneVisuals::SetAttributes).
	namespace BuildingVisuals
	{
		// Creates the building entity `name` (empty: "<id> site") under `parent` at `position` (in the parent's space), or updates it when it exists
		// already, and puts it at `phase` with `progress`. Returns its id.
		EntityId Build(EntityWorld& world, const std::string& id, int phase, float progress, const std::string& name = "", EntityId parent = NullEntity,
			const glm::vec3& position = glm::vec3(0.0f), float yawDegrees = 0.0f);
		// Moves an existing building on (false when the entity is not a building).
		bool Set(EntityWorld& world, EntityId building, int phase, float progress);
		// What the entity is (false when it is not a building): its id, phase and progress.
		bool Read(const EntityWorld& world, EntityId building, std::string& id, int& phase, float& progress);

		// Phases of a building of the library (0 when unknown) and the name of one ("" past the last: finished).
		int PhaseCount(const VisualLibrary& library, const std::string& id);
		std::string PhaseName(const VisualLibrary& library, const std::string& id, int phase);
		// A whole build as one number 0..1 (phases done plus the progress of the current one, over the phases) and back: a site that only
		// keeps a fraction of its work (materials delivered over materials needed) is drawn with Overall.
		float Overall(const VisualLibrary& library, const std::string& id, int phase, float progress);
		void FromOverall(const VisualLibrary& library, const std::string& id, float overall, int& phase, float& progress);
	}
}
