#pragma once
#include "GB/Save/SaveGame.h"
#include <cstdint>
#include <string>
#include <vector>

namespace GB::Save
{
	struct WriteOptions
	{
		bool Backup = true;                  // keep the previous file as <path>.bak
		bool SimulateCrashAfterTemp = false; // tests: stop after the temporary file is written, as a power cut would
	};

	// Writes to <path>.tmp, checks it, copies the old file to <path>.bak and renames the temporary over the destination, so the destination
	// is always either the old complete file or the new complete one.
	Result WriteTextAtomic(const std::string& path, const std::string& text, const WriteOptions& options = {});
	Result ReadTextFile(const std::string& path, std::string& out);

	std::string BackupPath(const std::string& path);
	std::string TempPath(const std::string& path);

	Result SaveGameToFile(const SaveGame& game, const std::string& path, const WriteOptions& options = {});
	// Reads and checks a file. When it is missing or damaged and <path>.bak is good, loads that instead (with a warning).
	Result LoadSaveGameFromFile(const std::string& path, SaveGame& out, bool useBackup = true);

	// Slots: <dir>/<name>.gbsave. A name is letters, digits, '-', '_' and '.' (not leading), never a path.
	bool IsValidSlotName(const std::string& name);
	std::string SlotPath(const std::string& dir, const std::string& name);

	struct SaveInfo
	{
		std::string Name, Path;
		bool Ok = false;
		std::string Error;        // when !Ok
		int Format = 0;           // the format the file is written in (it may need migrating)
		std::string Engine;
		uint64_t Tick = 0;
		float Hours = 0.0f;
		uint32_t Seed = 0;
		std::string WorldType;
		int64_t Modified = 0;     // seconds since the epoch (the file's date)
		uint64_t Bytes = 0;
	};
	// Newest first. Damaged files are listed with their error.
	std::vector<SaveInfo> ListSaves(const std::string& dir);
	std::string FormatTime(int64_t seconds);   // "2026-10-27 14:38"

	// Saves on a timer: Tick(dt) says when one is due. Quick save and autosave use fixed slot names.
	class AutoSaver
	{
	public:
		void Configure(bool enabled, double intervalSeconds)
		{
			m_Enabled = enabled;
			if (intervalSeconds > 0.0) m_Interval = intervalSeconds;
			m_Elapsed = 0.0;
		}
		bool Enabled() const { return m_Enabled; }
		double Interval() const { return m_Interval; }
		double Elapsed() const { return m_Elapsed; }
		void Reset() { m_Elapsed = 0.0; }
		// Advances the clock; true once per interval.
		bool Tick(double dt)
		{
			if (!m_Enabled) return false;
			m_Elapsed += dt;
			if (m_Elapsed + 1e-6 < m_Interval) return false;
			m_Elapsed = 0.0;
			return true;
		}

		static constexpr const char* AutoSlot = "autosave";
		static constexpr const char* QuickSlot = "quicksave";

	private:
		bool m_Enabled = false;
		double m_Interval = 300.0;
		double m_Elapsed = 0.0;
	};
}
