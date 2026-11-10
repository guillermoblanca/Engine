// T88: the player's settings file over player.json (UserSettings.h).
#include "UserSettings.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace GB::Editor
{
	namespace
	{
		bool ReadText(const std::string& path, std::string& text)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in) return false;
			std::ostringstream all;
			all << in.rdbuf();
			text = all.str();
			return true;
		}
	}

	UserSettingsLoad ApplyUserSettings(const std::string& path, ControlRules& controls)
	{
		UserSettingsLoad r;
		std::error_code ec;
		if (path.empty() || !std::filesystem::exists(path, ec)) return r;
		r.Found = true;
		std::string text;
		if (!ReadText(path, text)) { r.Error = "cannot read " + path; return r; }
		// which quality the file itself names: read it over a blank one, so a file that repeats player.json's value still counts as saved
		ControlRules probe = controls;
		probe.Game.Quality.clear();
		if (const std::string e = LoadControls(text, probe); !e.empty()) { r.Error = e; return r; }
		r.Quality = probe.Game.Quality;
		LoadControls(text, controls);   // (the same text loaded above: it cannot fail now)
		return r;
	}

	std::string SaveUserQuality(const std::string& path, const std::string& level)
	{
		namespace fs = std::filesystem;
		using Json = nlohmann::json;
		if (path.empty()) return "no settings file";
		Json doc = Json::object();
		std::error_code ec;
		if (fs::exists(path, ec))
		{
			std::string text;
			if (!ReadText(path, text)) return "cannot read " + path;
			doc = Json::parse(text, nullptr, false);
			if (doc.is_discarded() || !doc.is_object()) return path + " is not a JSON object (fix it or delete it)";
		}
		Json& controls = doc["controls"];
		if (!controls.is_object()) controls = Json::object();
		Json& game = controls["game"];
		if (!game.is_object()) game = Json::object();
		game["quality"] = level;
		const std::string changed = doc.dump(2) + "\n";
		ControlRules check = DefaultControls();
		if (const std::string e = LoadControls(changed, check); !e.empty()) return path + ": " + e;
		if (check.Game.Quality != level) return "the quality of " + path + " did not read back";
		// written beside it and moved over it: a crash mid-write never leaves half a file
		const fs::path target(path), temp = fs::path(path + ".tmp");
		if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			if (!out) return "cannot write " + temp.generic_string();
			out << changed;
			if (!out) return "cannot write " + temp.generic_string();
		}
		fs::rename(temp, target, ec);
		if (ec) { fs::remove(temp, ec); return "cannot write " + path; }
		return {};
	}
}
