#pragma once
#include "GB/Game/Clans.h"
#include "GB/Gameplay/Combat.h"
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// The hostility of the conflict work (H14, GB/Gameplay/Combat.h: IHostility) over the clans and their diplomacy (H13, GB/Game/Clans.h, its part
	// "for the conflict work"). Sides that are clans of the clan world (by name or id; a tribe's Tribe::ClanName is its clan's name) answer from
	// the diplomacy: how hostile one clan is to another is ClanWorld::Hostility (aggression x ill will and grievance, 0 under an alliance or a
	// truce), a raid is allowed only where ClanWorld::MayRaid says so (hostile, no pact), a raid's targets are the camps and stores of those
	// clans, a wrong done (a raid, a theft seen, a killing) is ClanWorld::Relate(culprit, victim, "raid"/"theft", ..., victim) so the victim keeps
	// a grievance by the diplomacy's own rules, and a grievance is the diplomacy's plus Contest for every region both claim (Contested). Anything
	// else (the predators, an explicit pair of the table, stand-ins of the tests) goes to the fallback (a HostilityTable).
	//
	// It listens to the clan world (Listen): the latest events between clans are kept for the report, and a pact makes the raids between the two
	// clans stop (a raid in progress turns back as soon as the target is no longer a raid target: see Tribe::StepRaid).
	class ClanHostility : public IHostility
	{
	public:
		float Contest = 0.5f;                     // grievance a region both claim adds (for the motive of a raid)

		// The clan world (it must outlive this) and the hostility of what is not a clan. Registers its listener once.
		void Bind(ClanWorld* clans, IHostility* fallback);
		ClanWorld* Clans() const { return m_Clans; }

		float Hostility(const std::string& a, const std::string& b) const override;
		std::vector<RaidTarget> RaidTargets(const std::string& raider) const override;
		void Settled(const std::string& clan, const glm::vec2& camp, Inventory* store) override;
		void Wronged(const std::string& victim, const std::string& culprit, const std::string& what, float weight) override;
		float Grievance(const std::string& victim, const std::string& culprit) const override;

		// The last events between clans the conflict work cares for (stances, pacts and their end, raids, thefts), newest last.
		std::vector<std::string> Recent() const;
		std::string Report() const;

	private:
		ClanId Id(const std::string& side) const;
		struct Heard { std::deque<std::string> Lines; };
		ClanWorld* m_Clans = nullptr;
		IHostility* m_Fallback = nullptr;
		std::shared_ptr<Heard> m_Heard = std::make_shared<Heard>();   // (shared with the listener: the clan world has no way to drop one)
		bool m_Listening = false;
	};
}
