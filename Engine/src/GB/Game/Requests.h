#pragma once
#include <string>
#include <vector>

namespace GB
{
	class ClanWorld;
	class Tribe;
	struct TribeMember;

	// V3 (docs/VISION_MUNDO_VIVO.md): what people ask of the player. A request is a problem of a member that it hands over when the player talks to
	// it, never a script: its site lacks a material ("bring"), the clan is short of food ("food"), a relative is far from home ("find"), the
	// player knows something it does not ("teach"), or the clan needs what another clan may have ("message"). The player takes it by listening,
	// does it by its own means (gives the thing, finds the person, teaches, talks to the other clan) and the board says it is done; the clan world
	// then rewards it (a bond, standing, a deed in the history that people tell). The simulation never reads the board: it is the player's.
	struct Request
	{
		int Id = 0;
		std::string Kind;              // "bring", "food", "find", "teach", "message"
		std::string Giver;             // who asks
		int GiverClan = -1;
		std::string Item;              // what to bring ("food": anything the clan eats), the technology to teach
		int Count = 0, Given = 0;      // how many it asks for and how many it has had
		std::string Target;            // whom to find, the clan to talk to (its name), the site the material is for
		int TargetClan = -1;
		double Since = 0.0;            // world seconds
		double Until = 0.0;            // after this it is dropped
		std::string Why;               // the problem, in a sentence (English, like the log)
		std::string State = "open";    // "open", "done", "dropped"
		std::string Taker;             // the player who took it
		bool operator==(const Request&) const = default;
	};

	struct RequestRules
	{
		float Days = 2.0f;             // a request stands this many days
		float FarFromHome = 80.0f;     // a relative farther than this from the camp is missed ("find")
		float FoodDays = 1.5f;         // the clan asks for food when it has less than this many days of it
		int MaxBring = 5;              // most units of a material asked at once
		float Bond = 0.12f;            // bond the giver gains with the player when it is done
		float Standing = 6.0f;         // standing it gives
		bool operator==(const RequestRules&) const = default;
	};

	class RequestBoard
	{
	public:
		RequestRules Rules;

		void Clear() { m_All.clear(); m_Next = 1; }
		// The problem `npc` of `clan` would ask `player` to help with now (Kind empty: none). Deterministic: the first problem by the order above.
		Request ProblemOf(const ClanWorld& world, const Tribe& clan, int clanId, const TribeMember& npc, const std::string& player, double now, double daySeconds) const;
		Request& Add(Request r);
		Request* Find(int id);
		// The open request a member made (nullptr: none).
		const Request* OpenBy(const std::string& giver) const;
		std::vector<const Request*> OpenFor(const std::string& player) const;
		const std::vector<Request>& All() const { return m_All; }
		// Drops what has run out of time; returns the dropped ones' ids.
		std::vector<int> Expire(double now);

		// What the player did that may answer a request; each returns the ids it completed.
		std::vector<int> Gave(const std::string& player, const std::string& to, int toClan, const std::string& item, int count, bool food);
		std::vector<int> Talked(const std::string& player, const std::string& to, int toClan);
		std::vector<int> Taught(const std::string& player, const std::string& to, const std::string& tech);
		std::vector<int> Met(const std::string& player, const std::string& person);   // the player came within reach of a person it was asked to find

		std::string ToJson() const;
		bool FromJson(const std::string& json);
		std::string Report(const std::string& player = std::string()) const;

	private:
		std::vector<Request> m_All;
		int m_Next = 1;
	};
}
