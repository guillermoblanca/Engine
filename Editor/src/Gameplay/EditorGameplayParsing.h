#pragma once

#include "Commands.h"
#include <string>
#include <vector>

namespace GB::Editor
{
	struct ParsedNumber
	{
		bool Ok = false;
		double Value = 0.0;
		std::string Error;
	};

	ParsedNumber ParseFloat(const std::vector<std::string>& args, size_t index, const char* name);
	ParsedNumber ParseInteger(const std::vector<std::string>& args, size_t index, const char* name);
	bool RequireArgumentCount(const std::vector<std::string>& args, size_t minimum, size_t maximum, const std::string& usage, CommandResult& result);

	std::string Quoted(const std::string& value);
	std::string Join(const std::vector<std::string>& values, const char* separator = ", ");
	std::string Number(float value);
}
