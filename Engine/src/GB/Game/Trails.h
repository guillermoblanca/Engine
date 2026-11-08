#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// T5c: trails and built paths. Every member's walking wears the ground of a grid of cells round its clan's camp; worn cells become a trail,
	// then a worn path, and fade when nobody walks them. Walking on them is faster (the body's pace, the abstract agent's speed) and the
	// navigation of the editor sees them as trampled undergrowth. A clan with spare stone or planks paves its most worn stretch: a paved cell
	// never fades and is the fastest. CPU only, deterministic (integer wear, members in order, sim time), saved with the tribe ("trails").
	// The numbers are data: the "trails" section (assets/data/trails.json); the defaults here are the same.

	// One material a paved cell can be made of, in the order the clan tries them.
	struct PaveMaterial
	{
		std::string Item;
		int PerCell = 1;
		bool operator==(const PaveMaterial&) const = default;
	};

	struct TrailPaveRules
	{
		bool Enabled = true;
		float PlanSeconds = 30.0f;     // how often a clan without a stretch looks for one to pave
		float PaveAt = 30.0f;          // wear (passes) a cell must have to be worth paving
		float ExtendAt = 20.0f;        // ... and the cells the stretch grows along from it
		int MinCells = 3;              // a stretch shorter than this is not worth it
		int MaxCells = 8;              // the longest stretch marked at once
		int CellsPerTrip = 4;          // cells a paver lays in one trip (it carries their material)
		float WorkSeconds = 6.0f;      // work at each cell
		float Reserve = 1.0f;          // the store keeps this many times its stock target of a material before it is spent on paths
		float TimeoutSeconds = 2400.0f;   // a stretch nobody worked on for this long is given up
		float Score = 0.8f;            // the option's base worth (the settlement's delivery is 0.85, its build 1.0)
		std::vector<PaveMaterial> Materials{ { "stone", 1 }, { "plank", 1 } };
		bool operator==(const TrailPaveRules&) const = default;
	};

	// What the editor draws (TrailsView): how much grass the tiers take away, the ground tint and how often the views refresh.
	struct TrailVisualRules
	{
		float GrassTrail = 0.7f, GrassWorn = 0.95f, GrassPaved = 1.0f;   // 0 = the grass untouched, 1 = none
		float TintTrail = 0.6f, TintWorn = 0.9f;                         // how far the ground goes towards Dirt
		glm::vec3 Dirt{ 0.42f, 0.33f, 0.23f };                             // the colour of bare earth (of the terrain's macro map) the ground goes towards
		glm::vec3 Stone{ 0.52f, 0.50f, 0.47f };                            // ... and of a paved cell
		float RefreshSeconds = 4.0f;                                       // the ground and the grass are redone at most this often (real time)
		bool operator==(const TrailVisualRules&) const = default;
	};

	struct TrailRules
	{
		bool Enabled = true;
		float CellSize = 2.0f;         // metres
		float Extent = 320.0f;         // the side of the grid, centred on the camp
		float SampleSeconds = 1.0f;    // how often each member's walk is read (sim time)
		float WearPerPass = 1.0f;      // wear a member adds to each cell it walks into
		float MaxWear = 100.0f;
		float DecayPerDay = 6.0f;      // wear a cell loses in a day nobody walks it (a paved cell none)
		float DecaySeconds = 60.0f;    // the decay is applied this often
		float TrailAt = 6.0f;          // wear of a trail (a dirt path) ...
		float WornAt = 18.0f;          // ... and of a worn path
		float MaxJump = 40.0f;         // a member that moved farther than this between two reads was moved, not walking: no wear
		// how much faster a member walks on each tier (1 = as on open ground)
		float TrailSpeed = 1.10f, WornSpeed = 1.20f, PavedSpeed = 1.35f;
		// how much of the undergrowth's cost each tier takes away for the navigation (0 = none, 1 = all of it)
		float TrailEase = 0.5f, WornEase = 0.8f, PavedEase = 1.0f;
		TrailPaveRules Pave;
		TrailVisualRules Visual;
		bool operator==(const TrailRules&) const = default;
	};

	TrailRules DefaultTrailRules();
	// Reads the "trails" section of a content file into `rules`; a file without it changes nothing; on a mistake it returns why and changes nothing.
	std::string LoadTrailRules(const std::string& json, TrailRules& rules);
	std::string TrailRulesToJson(const TrailRules& rules);   // one JSON object, as the section reads it back

	enum class TrailTier : uint8_t { None = 0, Trail = 1, Worn = 2, Paved = 3 };

	// The grid of one clan and what its paving is doing.
	class TrailState
	{
	public:
		// Wear is kept in tenths of a pass (integers: the same in every run and through a save).
		static constexpr int kScale = 10;

		bool Ready() const { return m_N > 0; }
		// Makes the grid round `centre` (cleared). Called by the tribe when it has none or its camp moved off it.
		void Reset(const glm::vec2& centre, const TrailRules& rules);
		int Side() const { return m_N; }
		float Cell() const { return m_Cell; }
		glm::vec2 Origin() const { return m_Origin; }
		bool CellOf(const glm::vec2& p, int& i, int& j) const;
		int Index(int i, int j) const { return j * m_N + i; }
		glm::vec2 Centre(int index) const;
		float Wear(int index) const { return static_cast<float>(m_Wear[static_cast<size_t>(index)]) / static_cast<float>(kScale); }
		bool Paved(int index) const { return m_Paved[static_cast<size_t>(index)] != 0; }
		TrailTier TierAt(int index, const TrailRules& rules) const;
		TrailTier TierAt(const glm::vec2& p, const TrailRules& rules) const;
		float Pace(const glm::vec2& p, const TrailRules& rules) const;
		float Ease(const glm::vec2& p, const TrailRules& rules) const;
		// Smooth (bilinear between cell centres) value of a per-tier table at a point: for drawing.
		float Smooth(const glm::vec2& p, const TrailRules& rules, float trail, float worn, float paved) const;

		// A walk from a to b: every cell entered after a's gets a pass. Returns the cells worn.
		int Walk(const glm::vec2& a, const glm::vec2& b, const TrailRules& rules);
		// The fading of `seconds` of time (paved cells keep theirs).
		void Decay(float seconds, float daySeconds, const TrailRules& rules);
		void SetPaved(int index, const TrailRules& rules);

		// The cells whose tier changed since the last call (for the views: ground, grass, navigation). `all`: too many to list, redo everything.
		std::vector<int> TakeChanged(bool& all);
		bool HasChanged() const { return m_AllChanged || !m_Changed.empty(); }
		// ... the same for the navigation (its own queue: the tribe hands it to the host within the tick, Tribe::SetTrailNavigation)
		std::vector<int> TakeNav(bool& all);

		std::string ToJson() const;
		bool FromJson(const std::string& json, const TrailRules& rules);   // false (and nothing changed) on a mistake

		// ---- the clan's walking and paving (Trails.cpp, driven by the tribe)
		struct Stretch
		{
			std::vector<int> Cells;   // in walking order
			std::string Item;         // its material
			int PerCell = 1;
			double Since = 0.0;       // marked (or last worked) at
			bool Active() const { return !Cells.empty(); }
		};
		Stretch Paving;
		std::map<std::string, glm::vec2> Last;   // each member's place at the last read
		float SampleTimer = 0.0f, DecayTimer = 0.0f, PlanTimer = 0.0f;
		bool PaceApplied = false;                // the bodies were given a pace (set back to 1 when the trails are switched off)
		bool NavOn = false;                      // the navigation was told of this grid's trails (told again whole when they are switched on, off or loaded)
		// counters, for `trails report`
		long long Passes = 0;
		int StretchesMarked = 0, StretchesDone = 0, StretchesDropped = 0, PaveTrips = 0, CellsPaved = 0;
		int Counts(TrailTier tier, const TrailRules& rules) const;
		float MaxWearNow() const;

	private:
		void Note(int index, TrailTier before, const TrailRules& rules);
		int m_N = 0;
		float m_Cell = 2.0f;
		glm::vec2 m_Origin{ 0.0f };
		std::vector<uint16_t> m_Wear;
		std::vector<uint8_t> m_Paved;
		std::vector<uint8_t> m_Flag;   // queued in m_Changed
		std::vector<int> m_Changed;
		std::vector<int> m_Nav;
		bool m_NavAll = false;
		bool m_AllChanged = false;
		float m_DecayCarry = 0.0f;   // the part of a tenth the last decay left over
	};
}
