#include "GB/Save/SaveFiles.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <system_error>

namespace GB::Save
{
	namespace fs = std::filesystem;

	std::string BackupPath(const std::string& path) { return path + ".bak"; }
	std::string TempPath(const std::string& path) { return path + ".tmp"; }

	Result ReadTextFile(const std::string& path, std::string& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return Result::Fail("cannot read " + path);
		std::stringstream ss;
		ss << f.rdbuf();
		out = ss.str();
		return {};
	}

	Result WriteTextAtomic(const std::string& path, const std::string& text, const WriteOptions& options)
	{
		std::error_code ec;
		const fs::path target(path);
		if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
		const std::string tmp = TempPath(path);
		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			if (!f) return Result::Fail("cannot write " + tmp);
			f.write(text.data(), static_cast<std::streamsize>(text.size()));
			f.flush();
			if (!f.good()) { f.close(); fs::remove(tmp, ec); return Result::Fail("write failed: " + tmp); }
		}
		if (fs::file_size(tmp, ec) != text.size() || ec) { fs::remove(tmp, ec); return Result::Fail("the temporary file is incomplete: " + tmp); }
		if (options.SimulateCrashAfterTemp) return Result::Fail("simulated crash after writing " + tmp);
		if (options.Backup && fs::exists(target, ec))
		{
			fs::copy_file(target, BackupPath(path), fs::copy_options::overwrite_existing, ec);
			if (ec) { fs::remove(tmp, ec); return Result::Fail("cannot keep the backup " + BackupPath(path) + ": " + ec.message()); }
		}
		fs::rename(tmp, target, ec);
		if (ec) { std::error_code ignore; fs::remove(tmp, ignore); return Result::Fail("cannot replace " + path + ": " + ec.message()); }
		return {};
	}

	Result SaveGameToFile(const SaveGame& game, const std::string& path, const WriteOptions& options)
	{
		return WriteTextAtomic(path, SaveGameToString(game), options);
	}

	Result LoadSaveGameFromFile(const std::string& path, SaveGame& out, bool useBackup)
	{
		auto attempt = [&](const std::string& p) -> Result
		{
			std::string text;
			Result r = ReadTextFile(p, text);
			if (!r.Ok) return r;
			return ParseSaveGame(text, out);
		};
		Result r = attempt(path);
		if (r.Ok || !useBackup) return r;
		std::error_code ec;
		if (!fs::exists(BackupPath(path), ec)) return r;
		Result b = attempt(BackupPath(path));
		if (!b.Ok) return Result::Fail(r.Error + " (and the backup: " + b.Error + ")");
		b.Warnings.push_back(path + " could not be read (" + r.Error + "): loaded the backup");
		return b;
	}

	bool IsValidSlotName(const std::string& name)
	{
		if (name.empty() || name.size() > 64 || name[0] == '.') return false;
		return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
	}

	std::string SlotPath(const std::string& dir, const std::string& name)
	{
		return (fs::path(dir) / (name + kFileExtension)).generic_string();
	}

	std::string FormatTime(int64_t seconds)
	{
		const std::time_t t = static_cast<std::time_t>(seconds);
		std::tm tm{};
#ifdef _WIN32
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		std::ostringstream os;
		os << std::put_time(&tm, "%Y-%m-%d %H:%M");
		return os.str();
	}

	std::vector<SaveInfo> ListSaves(const std::string& dir)
	{
		std::vector<SaveInfo> out;
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) return out;
		for (const auto& entry : fs::directory_iterator(dir, ec))
		{
			if (!entry.is_regular_file() || entry.path().extension() != kFileExtension) continue;
			SaveInfo info;
			info.Name = entry.path().stem().string();
			info.Path = entry.path().generic_string();
			info.Bytes = entry.file_size(ec);
			const auto ft = entry.last_write_time(ec);
			info.Modified = std::chrono::duration_cast<std::chrono::seconds>(
				(ft - fs::file_time_type::clock::now()) + std::chrono::system_clock::now().time_since_epoch()).count();
			SaveGame g;
			std::string text;
			Result r = ReadTextFile(info.Path, text);
			if (r.Ok) info.Format = DetectFormat(text);
			if (r.Ok) r = ParseSaveGame(text, g);
			info.Ok = r.Ok;
			info.Error = r.Error;
			if (r.Ok)
			{
				info.Engine = g.Engine;
				info.Tick = g.Clock.Tick;
				info.Hours = g.Clock.Hours;
				info.Seed = g.World.Seed;
				info.WorldType = g.World.Generated ? g.World.Type : "";
			}
			out.push_back(std::move(info));
		}
		std::sort(out.begin(), out.end(), [](const SaveInfo& a, const SaveInfo& b) { return a.Modified != b.Modified ? a.Modified > b.Modified : a.Name < b.Name; });
		return out;
	}
}
