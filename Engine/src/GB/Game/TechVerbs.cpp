#include "GB/Game/TechVerbs.h"
#include "GB/Game/JobBoard.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Techs.h"
#include <algorithm>

namespace GB
{
	namespace
	{
		bool Declared(const TechTree& tree, const std::string& verb)
		{
			for (const auto& [id, t] : tree.Techs)
				if (std::find(t.Verbs.begin(), t.Verbs.end(), verb) != t.Verbs.end()) return true;
			return false;
		}

		bool AnyVerbs(const TechTree* tree)
		{
			if (!tree) return false;
			for (const auto& [id, t] : tree->Techs) if (!t.Verbs.empty()) return true;
			return false;
		}
	}

	std::string TechVerbs::Ruling(const TechTree* tree, const std::string& kind, const std::string& what)
	{
		if (!AnyVerbs(tree) || kind.empty()) return {};
		if (!what.empty())
			if (const std::string verb = kind + ":" + what; Declared(*tree, verb)) return verb;
		return Declared(*tree, kind) ? kind : std::string();
	}

	bool TechVerbs::Knows(const Tribe& tribe, const std::string& verb)
	{
		const ClanKnowledge& knowledge = tribe.Knowledge();
		const TechTree* tree = knowledge.Techs();
		if (!tree) return true;   // no technologies: nothing is gated
		const std::vector<std::string> techs = tree->ForVerb(verb);
		if (techs.empty()) return true;
		for (const std::string& tech : techs)
			for (const TribeMember& m : tribe.Members())
				if (!m.Dead && m.Body && knowledge.Knows(m.Name, tech)) return true;
		return false;
	}

	bool TechVerbs::Allows(const Tribe& tribe, const std::string& kind, const std::string& what)
	{
		const std::string verb = Ruling(tribe.Knowledge().Techs(), kind, what);
		return verb.empty() || Knows(tribe, verb);
	}

	std::string TechVerbs::WhyNot(const Tribe& tribe, const std::string& kind, const std::string& what)
	{
		const std::string verb = Ruling(tribe.Knowledge().Techs(), kind, what);
		if (verb.empty() || Knows(tribe, verb)) return {};
		return "does not know how to " + VerbWord(verb);
	}

	std::vector<TechVerbs::Line> TechVerbs::List(const Tribe& tribe)
	{
		std::vector<Line> out;
		const TechTree* tree = tribe.Knowledge().Techs();
		if (!tree) return out;
		for (const std::string& verb : tree->Verbs())
			out.push_back({ verb, VerbWord(verb), Knows(tribe, verb), tree->ForVerb(verb) });
		return out;
	}

	int TechVerbs::Unknown(const Tribe& tribe)
	{
		int n = 0;
		for (const Line& l : List(tribe)) if (!l.Known) ++n;
		return n;
	}

	std::string TechVerbs::Report(const Tribe& tribe)
	{
		const std::vector<Line> lines = List(tribe);
		if (lines.empty()) return {};
		std::string known, unknown;
		int toFind = 0;
		for (const Line& l : lines)
		{
			std::string techs;
			for (const std::string& t : l.Techs) techs += (techs.empty() ? "" : ", ") + t;
			std::string& to = l.Known ? known : unknown;
			to += (to.empty() ? "" : "; ") + l.Word + " (" + l.Verb + ": " + techs + ")";
			if (!l.Known) ++toFind;
		}
		return "verbs known: " + (known.empty() ? std::string("none") : known) + " | to discover " + std::to_string(toFind) + (unknown.empty() ? std::string() : ": " + unknown);
	}

	void TechVerbs::Withdraw(const Tribe& tribe, JobBoard& board)
	{
		if (!AnyVerbs(tribe.Knowledge().Techs())) return;
		board.Withdraw([&](const Job& job) { return !Allows(tribe, job.Kind, job.Target); });
	}

	void TechVerbs::Options(const Tribe& tribe, std::vector<UtilityOption>& options)
	{
		if (!AnyVerbs(tribe.Knowledge().Techs())) return;
		options.erase(std::remove_if(options.begin(), options.end(), [&](const UtilityOption& o) { return o.Kind != "rest" && !Allows(tribe, o.Kind, o.Target); }), options.end());
	}

	bool KnowsVerb(const Tribe& clan, const std::string& verb)
	{
		return TechVerbs::Knows(clan, verb);
	}
}
