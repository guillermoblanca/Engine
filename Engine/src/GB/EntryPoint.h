#pragma once
#include "Application.h"
#include "Log.h"
#include <cstdlib>
#include <cstring>
#include <memory>

// Usage: <app> [--frames=N] [--screenshot=out.png] [--hidden] [--novsync]
int main(int argc, char** argv)
{
	GB::Log::Init();

	GB::LaunchOptions& options = GB::GetLaunchOptions();
	for (int i = 1; i < argc; ++i)
	{
		const char* arg = argv[i];
		if (std::strncmp(arg, "--frames=", 9) == 0)
			options.Frames = std::strtoull(arg + 9, nullptr, 10);
		else if (std::strncmp(arg, "--screenshot=", 13) == 0)
			options.Screenshot = arg + 13;
		else if (std::strcmp(arg, "--hidden") == 0)
			options.Hidden = true;
		else if (std::strcmp(arg, "--novsync") == 0)
			options.NoVSync = true;
		else
			GB_CORE_WARN("Unknown argument '{}'", arg);
	}

	std::unique_ptr<GB::Application> app(GB::CreateApplication());
	if (!app->IsValid())
		return 1;
	app->Run();
	return 0;
}
