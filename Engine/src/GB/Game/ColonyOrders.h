#pragma once
// T268 (C1-a of documentation/01_diseno/MODO_COLONIA.md): direct orders to any person of a clan, the "mandate" they hold and what cuts it.
//
// An order is a verb and its target given to one member: `goto x z`, `use <node>` (gather, fell, dig: the node's own ability), `build <site>` (work
// on an open site, carrying what its phase lacks first), `deliver <site>` (only carry), `talk <member>`, `follow <member>`, `transplant <node|x z>
// to <x> <z>` (T325: dig a plant up and plant it there, GB/Game/Transplant.h), and `cancel`. While the
// member holds the order (its mandate) its brain does not decide for it, the board does not hand it a job and nobody pulls it into a talk; it
// carries the order out step by step from here (each step is one of the tribe's own jobs, so its accounting, its log and its telemetry are the
// usual ones) and goes back to its own life when the order is done. C-3: the order is cut, with a line in the log ("drops the order: very
// hungry") and a notice here, when hunger or thirst fall under half of the board's CriticalHunger (JobRules: the same "too weak to leave" of
// Maslow), when its rest falls under 20 (the raid's own "in need"), when its mood falls under half of the social rules' Mood.Low, or when a
// threat or a fight takes it (ConflictTurn). Someone already in that state refuses the order.
//
// The tribe owns one (Tribe::Colony()) and calls it from two places of its Update, one line each: the member's turn (Turn) and a conflict
// that took it (Threat). Without any order nothing here runs and nothing is saved, so a run is the same as before. CPU only, deterministic.
#include <glm/vec2.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;

	class ColonyOrders
	{
	public:
		struct Mandate
		{
			std::string Verb;            // "goto", "use", "build", "deliver", "talk", "follow"
			std::string Target;          // the node, the site or the member ("" for goto)
			glm::vec2 At{ 0.0f };        // goto: the point; follow: where the leader was when it last set off
			double Since = 0.0;          // tribe seconds when it was given
			int Steps = 0;               // jobs it has started for the order
			int Fails = 0;               // steps that came to nothing (a few in a row cut the order)
			int Waits = 0;               // build: turns it waited for materials (too many cut the order)
			bool Spent = false;          // use: the node was found empty (what it carries goes to the store, and the order is done)
			std::string Carry;           // T325 transplant: the node type dug up and carried ("" = not dug up yet; saved only when set)
			bool operator==(const Mandate&) const = default;
		};
		struct Notice
		{
			std::string Member, Verb, Target, Reason;   // Reason: "very hungry", "very thirsty", "exhausted", "in low spirits", "a threat", ...
			double At = 0.0;
		};
		struct Reply
		{
			bool Ok = false;
			std::string Text;            // English (the commands' answers and the tests read it)
		};

		// Gives `member` of `tribe` an order (`args` are the verb's words: "goto" x z; "use" node; "talk" member...). It starts at once: the
		// member drops what it was doing. "cancel" lets it go. Refused (Ok false, with why) for an unknown member, verb or target, a player's
		// character, someone fighting, raiding or away, and someone too hungry, thirsty, tired or low to obey.
		Reply Give(Tribe& tribe, const std::string& member, const std::string& verb, const std::vector<std::string>& args);
		Reply Cancel(Tribe& tribe, const std::string& member);

		// The tribe's hooks. Turn: the member's turn while it holds an order (true: handled here, it does not decide as usual). Threat: a fight,
		// a flight or a raid took the member (ConflictTurn): its order is cut. Drop: it died or left (T333: what a transplant carried goes back in
		// the ground where the node was, with a notice "died" or "left").
		bool Turn(Tribe& tribe, TribeMember& m);
		void Threat(Tribe& tribe, TribeMember& m);
		void Drop(Tribe& tribe, TribeMember& m);
		void Clear() { m_Mandates.clear(); m_Notices.clear(); }

		bool Has(const std::string& member) const { return m_Mandates.count(member) != 0; }
		const Mandate* Of(const std::string& member) const;
		const std::map<std::string, Mandate>& All() const { return m_Mandates; }
		bool Empty() const { return m_Mandates.empty(); }
		// The orders that were cut, oldest first (the last 32): what the editor and the tests read.
		const std::vector<Notice>& Notices() const { return m_Notices; }
		// "Ana use clay bank 3, Biel goto 10.0 4.0" ("" with no orders): the line `game status` adds.
		std::string Line() const;
		// Why a member could not take or keep an order now ("" = it can): its needs and mood, as C-3 says.
		static std::string Unfit(const Tribe& tribe, const TribeMember& m);

		// Saving (the tribe's "orders" section, only written when there is one): the mandates; the notices are news, not state.
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		enum class Outcome { Going, Done, Failed };
		Outcome Step(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why);
		Outcome StepGoto(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why);
		Outcome StepUse(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why);
		Outcome StepSite(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why);
		Outcome StepTalk(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why);
		Outcome StepFollow(Tribe& tribe, TribeMember& m, Mandate& d, std::string& why);
		void End(Tribe& tribe, TribeMember& m, const std::string& reason, bool decide);   // reason "": done; else cut, with a notice
		static void CloseJob(Tribe& tribe, TribeMember& m);   // the job the member is on ends now (the tribe's Finish for its own jobs)
		static std::string Describe(const Mandate& d);

		std::map<std::string, Mandate> m_Mandates;   // member -> its order
		std::vector<Notice> m_Notices;
	};
}
