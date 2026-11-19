#pragma once
// T270 (C1-d of documentation/01_diseno/MODO_COLONIA.md): the work the player designates to the clan, with nobody chosen, and the real order of its
// construction sites.
//
// Marks. `game designate <node> [priority]` marks a resource node: while it is marked the clan's job board carries a "designate <node>" job (one
// slot: one gatherer a node) above the usual work of a trade, and whoever can do it takes it (it holds the tool the node asks for, knows the
// technology, is fit and near: JobScore below), not a fixed person. Holding it, the member is offered a gather at that node (Options below; the
// player showed the clan where it is, so it does not have to remember it), and the needs still win over it: the option goes through the mood, the
// society and Maslow's pyramid like any other, so a very hungry or thirsty member eats or drinks first. `all` marks every node of the same kind in
// the node's stand or deposit (the nodes carry no patch id at run time: a flood from the node through nodes of its kind at most kLink metres
// apart, within kAllRadius of it). The mark goes when the node is spent or gone, or with `off`.
//
// The site queue. In the colony every plan the player places is queued (WorldDirector::PlaceBuild); `game site <id> up|down|top` moves one. The
// clan carries materials to and builds its open sites in that order: the first site of the queue that can take a carrier (or a builder) keeps
// its usual score and the ones behind it are worth kLater as much, so they only get people while the first is blocked (its materials are not in
// the store, or already on their way) or full of builders. Sites not in the queue (the planner's) come after the queued ones, in the clan's order.
//
// Priorities. A mark's priority is the one of the work table to come (C-5: 1 is first, 10 is last). Today it only scales the job of the mark
// (PriorityFactor); the table of C2 (WorkPriorities) plugs in at MemberPriority, the one place where a person's own priority for the work of
// a mark would enter.
//
// The tribe owns one (Tribe::Designations()) and calls it from one line in each place it needs it. With no mark and no queue nothing here runs
// and nothing is saved, so a run (GBSim, a clan of the AI) is the same as before. CPU only, deterministic (every list in name or queue order).
#include "GB/AI/Utility.h"
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;
	struct Job;

	class ColonyDesignations
	{
	public:
		static constexpr int kDefaultPriority = 1;   // "gather with priority": the first of the table
		static constexpr float kLink = 12.0f;        // `all`: two nodes of a kind this close are one stand (wild deposits lie 8-12 m apart)
		static constexpr float kAllRadius = 30.0f;   // ... within this of the node chosen
		static constexpr int kAllMost = 60;          // ... and at most this many
		static constexpr float kLater = 0.15f;       // a site behind the first open one of the queue is worth this much

		struct Mark
		{
			int Priority = kDefaultPriority;   // 1 (first) .. 10 (last), as the work table of C-5
			double Since = 0.0;                // tribe seconds when it was marked
			std::string Field;                 // T337: the farm zone that asked for it (a node in its way: FarmZones); "" = the player's own mark
			bool operator==(const Mark&) const = default;
		};
		struct Reply
		{
			bool Ok = false;
			std::string Text;   // English (the commands' answers and the tests read it)
		};

		// ---- the marks
		// Marks a node of the clan's kinds (refused: unknown, empty, a priority out of 1..10). Marking it again changes its priority.
		Reply Designate(Tribe& tribe, const std::string& node, int priority = kDefaultPriority);
		// Marks every node of the node's kind in its stand or deposit (see above): the answer says how many.
		Reply DesignateAll(Tribe& tribe, const std::string& node, int priority = kDefaultPriority);
		// T337: the mark a farm zone asks for, of a node that stands in its way (FarmZones::Post): Designate's checks and refusals, the mark
		// carrying the zone's name. Marking a node the player marked keeps the player's mark as it is.
		Reply Clear(Tribe& tribe, const std::string& node, int priority, const std::string& field);
		// Lifts the mark of a node (all: of its whole stand).
		Reply Undesignate(Tribe& tribe, const std::string& node, bool all = false);
		const Mark* MarkOf(const std::string& node) const;
		const std::map<std::string, Mark>& Marks() const { return m_Marks; }
		// The nodes of the stand of `node` (itself first, then by name): what `all` marks.
		static std::vector<std::string> Stand(const Tribe& tribe, const std::string& node);
		// The kind of node it is ("" = none the clan can work).
		static std::string KindOf(const Tribe& tribe, const std::string& node);

		// ---- the site queue
		// Puts an open site of the clan at the end of the queue (a plan of the player's). False when it is no open site or is queued already.
		bool QueueSite(const Tribe& tribe, const std::string& site);
		// up, down, top or bottom. The first move puts every open site of the clan in the queue, in the order it shows (SiteOrder).
		Reply MoveSite(const Tribe& tribe, const std::string& site, const std::string& how);
		// The clan's open sites in the order they are worked: the queued ones first, then the others in the clan's own order.
		std::vector<std::string> SiteOrder(const Tribe& tribe) const;
		bool Queued() const { return !m_Sites.empty(); }
		const std::vector<std::string>& QueuedSites() const { return m_Sites; }
		void SiteGone(const std::string& site);

		// ---- the tribe's hooks
		// PostJobs: lifts spent marks and posts a job for each mark (before the board's End).
		void Post(Tribe& tribe);
		// JobScore of a "designate" job for a member (0 = it cannot do it now).
		float JobScore(const Tribe& tribe, const TribeMember& m, const Job& job) const;
		// Decide: the member holding a mark's job is offered a gather at its node.
		void Options(Tribe& tribe, TribeMember& m, std::vector<UtilityOption>& options) const;
		// Finish of a gather that brought something: the claim of the mark stays alive.
		void Gathered(Tribe& tribe, const TribeMember& m) const;
		// The site queue on a site's options and jobs: 1, or kLater for a site behind the first one open for `kind` ("deliver" or "build").
		// `asking`: the member that weighs it (it does not count among the builders); nullptr for the board.
		float SiteFactor(const Tribe& tribe, const std::string& site, const std::string& kind, const TribeMember* asking) const;

		// What the mark's priority gives its job: 1 at priority 1, 0.1 at 10.
		static float PriorityFactor(int priority);
		// C2 (WorkPriorities): a member's own priority for the work of a mark. 1 today: the table is not there yet.
		// T306: 0 when the person's table has the gather of `node` at "-" (WorkPriorities::MarkFactor), else 1.
		static float MemberPriority(const Tribe& tribe, const TribeMember& m, const std::string& node);

		bool Empty() const { return m_Marks.empty() && m_Sites.empty(); }
		void Clear() { m_Marks.clear(); m_Sites.clear(); }
		// "3 nodes marked (wild tree 4 p1, ...); sites in order: Hut 2, Hut 1" ("" with neither): the line `game status` adds. The marks of
		// the farm zones (T337) are not in it: `game zone` tells them.
		std::string Line() const;

		// Saving (the tribe's "designations" section, only written when there is something): the marks and the queue.
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		static bool NodePos(const Tribe& tribe, const std::string& node, glm::vec2& out);
		static int AmountOf(const Tribe& tribe, const std::string& node);   // -1: no node
		static void Logged(Tribe& tribe, const std::string& text);
		bool OpenFor(const Tribe& tribe, const std::string& site, const std::string& kind, const TribeMember* asking) const;

		std::map<std::string, Mark> m_Marks;   // node -> its mark
		std::vector<std::string> m_Sites;      // the queue of sites, first first
	};
}
