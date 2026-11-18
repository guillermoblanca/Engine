// T310 (C2, documentation/01_diseno/ARRANQUE_Y_LLEGADAS.md §2): the commands of the screen "Your people" (the screen is prog-ui's; it only queues
// these). The candidates are GB's (GB/Game/Founding.h); the game starts with `shell new ... party <n>`, whose `tribe build ... party <n>` seats the
// people picked here (or, with nobody picked, the founding's default choice: the same as GBSim --party).
#include "EditorApp.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace GB::Editor
{
	namespace
	{
		// "0,3,5" or "0 3 5" (or both) -> the slots; false on anything that is not a whole number
		bool Slots(const std::vector<std::string>& words, size_t from, std::vector<int>& out)
		{
			for (size_t i = from; i < words.size(); ++i)
			{
				std::string w = words[i];
				std::replace(w.begin(), w.end(), ',', ' ');
				size_t at = 0;
				while (at < w.size())
				{
					while (at < w.size() && w[at] == ' ') ++at;
					if (at >= w.size()) break;
					char* end = nullptr;
					const long v = std::strtol(w.c_str() + at, &end, 10);
					if (end == w.c_str() + at) return false;
					out.push_back(static_cast<int>(v));
					at = static_cast<size_t>(end - w.c_str());
				}
			}
			return !out.empty();
		}
	}

	// game founding [show]                 the candidates of the menu's seed (a new pool when there is none for it), the picked ones and their kit
	// game founding new [seed]              a new pool (the menu's seed, or this one)
	// game founding roll [<i>|all]          draws one candidate again, or every one not locked
	// game founding lock <i> | unlock <i>   keeps a candidate through `roll all`
	// game founding pick <i,j,...>          the people the clan is founded with (1 to 6)
	// game founding start                   a new game with them: `shell new custom <seed> party <n>` (the menu's form for the rest)
	CommandResult EditorApp::FoundingCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "show" : a[0];
		const uint32_t seed = static_cast<uint32_t>(std::max(0, m_Shell.Seed));
		if (sub == "new")
		{
			const uint32_t s = a.size() > 1 ? static_cast<uint32_t>(std::strtoul(a[1].c_str(), nullptr, 10)) : seed;
			m_Founding.Start(m_Tribe.Config, s);
			return { true, "founding of seed " + std::to_string(s) + ": " + m_Founding.Report() };
		}
		if (!m_Founding.Started()) m_Founding.Start(m_Tribe.Config, seed);
		auto picked = [this]
		{
			std::string s;
			for (const int i : m_Founding.Picked()) s += (s.empty() ? "" : ",") + std::to_string(i);
			return s.empty() ? std::string("none") : s;
		};
		if (sub == "show")
		{
			const int n = static_cast<int>(m_Founding.Picked().size());
			return { true, "founding of seed " + std::to_string(m_Founding.WorldSeed()) + ", " + std::to_string(m_Founding.Rolls()) + " rolls: " + m_Founding.Report() + " | picked: " + picked()
				+ (n > 0 ? " | they bring: " + m_Founding.KitText(n) : std::string()) };
		}
		if (sub == "roll")
		{
			if (a.size() < 2 || a[1] == "all")
			{
				const int n = m_Founding.RerollAll();
				return { true, std::to_string(n) + " drawn again: " + m_Founding.Report() };
			}
			const int i = std::atoi(a[1].c_str());
			if (const std::string error = m_Founding.Reroll(i); !error.empty()) return { false, error };
			return { true, "candidate " + std::to_string(i) + " drawn again: " + m_Founding.Report() };
		}
		if (sub == "lock" || sub == "unlock")
		{
			if (a.size() < 2) return { false, "usage: game founding " + sub + " <i>" };
			const int i = std::atoi(a[1].c_str());
			if (const std::string error = m_Founding.Lock(i, sub == "lock"); !error.empty()) return { false, error };
			return { true, m_Founding.Candidates()[static_cast<size_t>(i)].Name + (sub == "lock" ? " locked" : " unlocked") };
		}
		if (sub == "pick")
		{
			std::vector<int> slots;
			if (!Slots(a, 1, slots)) return { false, "usage: game founding pick <i,j,...> (the candidates' numbers, game founding)" };
			if (const std::string error = m_Founding.Pick(slots); !error.empty()) return { false, error };
			std::string names;
			for (const int i : m_Founding.Picked()) names += (names.empty() ? "" : ", ") + m_Founding.Candidates()[static_cast<size_t>(i)].Name;
			return { true, "picked " + picked() + " (" + names + "); they bring: " + m_Founding.KitText(static_cast<int>(m_Founding.Picked().size())) };
		}
		if (sub == "start")
		{
			const int n = static_cast<int>(m_Founding.Picked().size());
			if (n == 0) return { false, "nobody picked (game founding pick <i,j,...>)" };
			return Execute("shell new custom " + std::to_string(m_Founding.WorldSeed()) + " party " + std::to_string(n));
		}
		return { false, "usage: game founding [show] | new [seed] | roll [<i>|all] | lock <i> | unlock <i> | pick <i,j,...> | start" };
	}
}
