#include "EditorGameplayParsing.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace GB::Editor
{
	namespace
	{
		ParsedNumber Parse(const std::vector<std::string>& args, size_t index, const char* name, bool integer)
		{
			ParsedNumber result;
			if (index >= args.size())
			{
				result.Error = std::string(name) + " is required";
				return result;
			}

			const std::string& text = args[index];
			char* end = nullptr;
			errno = 0;
			const double value = std::strtod(text.c_str(), &end);
			if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(value) || (integer && std::trunc(value) != value))
			{
				result.Error = std::string(name) + " must be " + (integer ? "an integer" : "a number");
				return result;
			}
			result.Ok = true;
			result.Value = value;
			return result;
		}
	}

	ParsedNumber ParseFloat(const std::vector<std::string>& args, size_t index, const char* name)
	{
		return Parse(args, index, name, false);
	}

	ParsedNumber ParseInteger(const std::vector<std::string>& args, size_t index, const char* name)
	{
		return Parse(args, index, name, true);
	}

	bool RequireArgumentCount(const std::vector<std::string>& args, size_t minimum, size_t maximum, const std::string& usage, CommandResult& result)
	{
		if (args.size() < minimum || args.size() > maximum)
		{
			result = { false, usage };
			return false;
		}
		return true;
	}

	std::string Quoted(const std::string& value)
	{
		return value.find(' ') == std::string::npos ? value : "\"" + value + "\"";
	}

	std::string Join(const std::vector<std::string>& values, const char* separator)
	{
		std::string result;
		for (const std::string& value : values) result += (result.empty() ? "" : separator) + value;
		return result;
	}

	std::string Number(float value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof buffer, "%.4g", static_cast<double>(value));
		return buffer;
	}
}
