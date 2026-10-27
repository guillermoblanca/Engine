#pragma once
#include "GB/AI/Memory.h"
#include "GB/AI/Utility.h"
#include <cstdint>
#include <string>
#include <vector>

namespace GB
{
	// Everything an agent keeps in its head between decisions (H5 + H6): what it knows (memory), where it is looking, what it has decided and what
	// the options were. The needs are not here: they are attributes of its ability system. Saved with the game (ToJson / FromJson); the list of
	// options is only for the inspector.
	struct AgentMind
	{
		SpatialMemory Memory;
		Commitment Current;                 // the decision in force
		glm::vec2 Facing{ 0.0f, 1.0f };     // where it looks (x, z)
		glm::vec2 LastPos{ 0.0f };
		bool HasLast = false;
		float ScanAngle = 0.0f;             // standing still it turns its head: radians
		uint64_t HeardSeq = 0;              // last sound it has heard
		int ExploreCount = 0;               // trips to unknown ground so far
		std::string Orders;                 // the task text it is carrying out (to give again after a load)

		// For the inspector and the debug view; not saved.
		std::vector<UtilityOption> Options;     // the last decision's options, with their scores
		int Chosen = -1;                        // index into Options
		std::vector<std::string> Visible;       // what it saw at the last look
		std::vector<std::string> Heard;         // what it heard at the last listen
		double LastDecision = -1.0;

		std::string ToJson() const;
		bool FromJson(const std::string& json);
		void Reset() { *this = AgentMind(); }
	};
}
