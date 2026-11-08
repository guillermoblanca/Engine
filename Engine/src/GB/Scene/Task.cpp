#include "GB/Scene/Task.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace GB
{
	namespace
	{
		std::vector<std::string> Words(const std::string& line)
		{
			std::vector<std::string> w;
			std::istringstream in(line);
			std::string word;
			while (in >> word) w.push_back(word);
			return w;
		}
		bool IsNumber(const std::string& s)
		{
			char* end = nullptr;
			std::strtod(s.c_str(), &end);
			return !s.empty() && end && *end == '\0';
		}
		float Number(const std::string& s) { return static_cast<float>(std::atof(s.c_str())); }
	}

	bool ParseTasks(const std::string& text, std::vector<Task>& out, std::string* error)
	{
		out.clear();
		std::string normalized = text;
		for (char& c : normalized) if (c == ';' || c == '|') c = '\n';
		std::istringstream in(normalized);
		std::string line;
		auto fail = [&](const std::string& why) { if (error) *error = "task " + std::to_string(out.size() + 1) + ": " + why; return false; };
		while (std::getline(in, line))
		{
			const size_t hash = line.find('#');
			if (hash != std::string::npos) line = line.substr(0, hash);
			const std::vector<std::string> w = Words(line);
			if (w.empty()) continue;
			Task t;
			if (w[0] == "goto")
			{
				if (w.size() < 3 || !IsNumber(w[1]) || !IsNumber(w[2])) return fail("goto <x> <z> [run]");
				t.What = Task::Kind::GoTo;
				t.Position = { Number(w[1]), Number(w[2]) };
				t.Run = w.size() > 3 && w[3] == "run";
			}
			else if (w[0] == "wait")
			{
				if (w.size() < 2 || !IsNumber(w[1])) return fail("wait <seconds>");
				t.What = Task::Kind::Wait;
				t.Seconds = Number(w[1]);
			}
			else if (w[0] == "interact")
			{
				if (w.size() < 2) return fail("interact <entity> [point]   (entity names with spaces: use '_' instead)");
				t.What = Task::Kind::Interact;
				t.Entity = w[1];
				for (char& c : t.Entity) if (c == '_') c = ' ';
				t.Point = w.size() > 2 ? w[2] : "";
			}
			else if (w[0] == "attach")
			{
				if (w.size() < 2) return fail("attach <prop>");
				t.What = Task::Kind::Attach;
				t.Prop = w[1];
			}
			else if (w[0] == "detach")
			{
				t.What = Task::Kind::Detach;
				t.Prop = w.size() > 1 ? w[1] : "";
			}
			else if (w[0] == "ability")
			{
				if (w.size() < 2) return fail("ability <name> [entity]   (entity names with spaces: use '_' instead)");
				t.What = Task::Kind::Ability;
				t.Prop = w[1];
				t.Entity = w.size() > 2 ? w[2] : "";
				for (char& c : t.Entity) if (c == '_') c = ' ';
			}
			else
				return fail("unknown task '" + w[0] + "' (goto, interact, wait, attach, detach, ability)");
			out.push_back(std::move(t));
		}
		return true;
	}

	std::string DescribeTask(const Task& t)
	{
		char b[160];
		switch (t.What)
		{
		case Task::Kind::GoTo: std::snprintf(b, sizeof b, "goto %.1f %.1f%s", static_cast<double>(t.Position.x), static_cast<double>(t.Position.y), t.Run ? " run" : ""); return b;
		case Task::Kind::Wait: std::snprintf(b, sizeof b, "wait %.1f", static_cast<double>(t.Seconds)); return b;
		case Task::Kind::Interact: return "interact " + t.Entity + (t.Point.empty() ? "" : "/" + t.Point);
		case Task::Kind::Attach: return "attach " + t.Prop;
		case Task::Kind::Detach: return "detach" + (t.Prop.empty() ? "" : " " + t.Prop);
		case Task::Kind::Ability: return "ability " + t.Prop + (t.Entity.empty() ? "" : " " + t.Entity);
		}
		return "";
	}
}
