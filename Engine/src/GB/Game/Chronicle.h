#pragma once
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// The chronicle of a clan: every so often (Every simulation seconds) a row with what it has and what each member is at, and every line the members
	// say as events. From it comes the report of how the clan went (tier of the pyramid of needs, food, fire, roof, who did what for how long) and the
	// series of numbers for a chart or a spreadsheet. Headless and not saved with the game: it is about a run, not part of the world.
	struct ChronicleMember
	{
		std::string Name, Role, Job, Detail;
		float Hunger = -1.0f, Thirst = -1.0f, Rest = -1.0f, Warmth = -1.0f, Health = -1.0f;   // -1: the member has no such need (yet)
		bool Dead = false;
	};

	struct ChronicleRow
	{
		double Time = 0.0;
		int Day = 1;
		float Hour = 0.0f;
		std::string Tier;                 // the lowest tier of the pyramid not covered: survival, warmth, safety, growth
		float FoodDays = 0.0f;
		int Alive = 0, Dead = 0, Beds = 0, Buildings = 0;
		bool Fire = false;
		std::map<std::string, int> Store;
		std::vector<ChronicleMember> Members;
	};

	struct ChronicleEvent
	{
		double Time = 0.0;
		std::string Who, Text;
	};

	class Chronicle
	{
	public:
		bool Enabled = true;
		double Every = 60.0;              // simulation seconds between two rows
		size_t MaxRows = 4000, MaxEvents = 30000;

		void Clear();
		bool Due(double time) const { return Enabled && time + 1e-6 >= m_Next; }
		void Add(ChronicleRow row);
		void Event(double time, const std::string& who, const std::string& text);

		const std::vector<ChronicleRow>& Rows() const { return m_Rows; }
		const std::vector<ChronicleEvent>& Events() const { return m_Events; }

		// One line: rows, days, who is alive, the tier now.
		std::string Summary() const;
		// The report (Markdown): milestones, the clan day by day, what each member did and the needs it went through, the deaths.
		std::string Markdown(const std::string& title = "Clan chronicle") const;
		// One line per row and member: time, day, hour, tier, food days, member, role, job, the five needs, dead.
		std::string Csv() const;

	private:
		std::vector<ChronicleRow> m_Rows;
		std::vector<ChronicleEvent> m_Events;
		double m_Next = 0.0;
	};
}
