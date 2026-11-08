#pragma once
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// V1 (docs/VISION_MUNDO_VIVO.md): what the world has lived through, kept with the game. Every event worth telling (a death, a birth, a couple, a
	// building that stands, a discovery, a raid, a first contact between clans...) is one row: who, to whom, which clans, where, when and a sentence.
	// Each person remembers the events it took part in or saw (its episodic memory: "X helped me on day 12", "the raid of the Ash"), and that memory
	// is what people talk about (V2) and what a request is made of (V3).
	//
	// The history is written by the simulation and never read by it to decide in V1, so a run is the same with it or without it. It is bounded: when
	// it grows past MaxEvents the least weighty old events go first (a death outlives a quarrel), and a person keeps at most MaxMemories.

	struct HistoryEvent
	{
		int Id = 0;
		double At = 0.0;              // world seconds
		std::string Kind;             // "death", "birth", "pair", "built", "discovery", "killed", "raid", "contact", "found"...
		std::string Who, Whom;        // the people (or animals) it is about; "" = the clan as a whole
		int Clan = -1, Other = -1;    // the clan it happened in and the other clan, if any
		glm::vec2 Where{ 0.0f };
		bool HasWhere = false;
		std::string Text;             // the sentence the log said ("dies of Hunger"), in English like the log
		int Weight = 0;               // how much it is worth remembering and telling: 1 a small thing .. 5 something the clan never forgets (0: its kind's)
		bool operator==(const HistoryEvent&) const = default;
	};

	// One person's memory of an event: how it was there ("did", "suffered", "saw", "heard") and how it felt about it (-1 bad .. 1 good).
	struct Recollection
	{
		int Event = 0;
		std::string How;
		float Feeling = 0.0f;
		std::string From;             // V2: who told it ("" = it was there)
		double Since = 0.0;           // V2: when it came to know it (world seconds)
		bool operator==(const Recollection&) const = default;
	};

	// The kinds of events: how weighty each is and how it feels to whom did it, to whom suffered it and to whom saw it.
	struct HistoryKind
	{
		int Weight = 1;
		float Doer = 0.0f, Sufferer = 0.0f, Witness = 0.0f;
	};

	struct HistoryRules
	{
		size_t MaxEvents = 4000;
		size_t MaxMemories = 60;      // per person
		float WitnessRadius = 25.0f;  // members of the clan within this of where it happened saw it
		float Retold = 0.7f;          // V2: a rumour is felt this much as strongly as by whom told it
		std::map<std::string, HistoryKind> Kinds;
	};
	HistoryRules DefaultHistoryRules();

	class WorldHistory
	{
	public:
		HistoryRules Rules = DefaultHistoryRules();

		void Clear();
		// The world's clock (the clan world gives it every step): every event and rumour is stamped with it, whichever clan writes it.
		void SetClock(double seconds) { m_Clock = seconds; m_HasClock = true; }
		bool HasClock() const { return m_HasClock; }
		double Clock() const { return m_Clock; }
		// V2: how long after an event a rumour of it first crossed into another clan, as it was told (the clan world notes it at its meetings;
		// for measuring, not saved).
		std::vector<double> Crossings;
		// Writes an event (its Id is given here) and returns it; the weight is the kind's when the event leaves it at 0.
		int Add(HistoryEvent e);
		// A person remembers an event (once: a second time keeps the closer "how"); `from`: who told it, `since`: when.
		void Remember(const std::string& person, int event, const std::string& how, float feeling, const std::string& from = std::string(), double since = 0.0);
		const Recollection* MemoryOf(const std::string& person, int event) const;

		// ---- V2: rumours and reputation. What a teller would tell a listener now: the weightiest, then newest, of its memories the listener does not
		// know, leaving out what happened to the listener itself (0: nothing). `about`: only events about that person (did or suffered it).
		int Topic(const std::string& teller, const std::string& listener, const std::string& about = std::string(), double until = 1e300) const;   // (`until`: only events that happened by then)
		// The listener hears it from the teller ("heard", the teller's feeling softened by Rules.Retold); false when it knew it already.
		bool Tell(const std::string& teller, const std::string& listener, int event, double now);
		// What people make of a person: how many remember something it did or suffered and their mean feeling about what it did (-1..1).
		struct Reputation { int Known = 0, Deeds = 0; float Score = 0.0f; };
		Reputation ReputationOf(const std::string& person) const;
		// How many people have come to know of an event (and how the first of them who was not there heard it: the speed of a rumour).
		int KnownBy(int event) const;
		// The default feeling of a kind for a role ("did", "suffered", "saw", "heard").
		float FeelingOf(const std::string& kind, const std::string& how) const;
		int WeightOf(const std::string& kind) const;

		const std::vector<HistoryEvent>& Events() const { return m_Events; }
		const HistoryEvent* Find(int id) const;
		const std::vector<Recollection>* MemoriesOf(const std::string& person) const;
		// The events a person remembers, weightiest and newest first (at most `count`).
		std::vector<const HistoryEvent*> Recall(const std::string& person, size_t count = 10) const;
		// The events of a clan (and those between it and another), newest last; kind "" = any.
		std::vector<const HistoryEvent*> OfClan(int clan, const std::string& kind = std::string()) const;
		// A dead person's memories go (the events stay: others remember them); a person who moves clan keeps them.
		void Forget(const std::string& person) { m_Memories.erase(person); }
		size_t MemoryCount() const;

		std::string ToJson() const;
		bool FromJson(const std::string& json);
		// For people: the last `count` events of a clan (-1: of every clan), one line each with its day.
		std::string Report(int clan = -1, size_t count = 30, double daySeconds = 1200.0) const;
		std::string MemoryReport(const std::string& person, double daySeconds = 1200.0, const std::string& kind = std::string()) const;   // (kind: only those)

	private:
		void Prune();
		std::vector<HistoryEvent> m_Events;
		std::map<std::string, std::vector<Recollection>> m_Memories;
		int m_Next = 1;
		double m_Clock = 0.0;
		bool m_HasClock = false;
	};
}
