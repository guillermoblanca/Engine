#include "GB/Game/TribeBias.h"
#include "GB/AI/Utility.h"
#include "GB/Game/Tribe.h"
#include <algorithm>

// T238: the focus hook (see TribeBias.h). Only the public API of Tribe.
namespace GB
{
	bool GatherIsFood(const Tribe& tribe, const TribeMember& m, const std::string& node)
	{
		const MemoryEntry* e = m.Mind.Memory.Find(node);
		if (!e) return false;
		const auto def = tribe.NodeDefs().find(e->Kind);
		return def != tribe.NodeDefs().end() && tribe.IsFood(def->second.Item);
	}

	void ApplyFocusBias(const Tribe& tribe, const TribeMember& m, std::vector<UtilityOption>& options)
	{
		const TribeFocusBias& f = tribe.Bias.Focus;
		const auto it = f.Mult.find(m.Name);
		if (it == f.Mult.end() || it->second <= 1.0f) return;   // (never less: the focus only pushes its own)
		const float mult = it->second;
		for (UtilityOption& o : options)
		{
			if (o.Score <= 0.0f) continue;
			bool in = std::find(f.Kinds.begin(), f.Kinds.end(), o.Kind) != f.Kinds.end();
			if (!in && o.Kind == "gather" && (f.FoodGather || f.OtherGather))
			{
				const bool food = GatherIsFood(tribe, m, o.Target);
				in = food ? f.FoodGather : f.OtherGather;
			}
			if (!in) continue;
			o.Score *= mult;
			o.Factors.push_back({ "focus " + f.Group, mult });
		}
	}
}
