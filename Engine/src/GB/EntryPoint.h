#pragma once
#include "Application.h"
#include "Log.h"
#include <cstdlib>
#include <cstring>
#include <memory>

// Usage: <app> [--frames=N]   (N frames then exit; default runs until closed)
int main(int argc, char** argv)
{
	GB::Log::Init();

	std::unique_ptr<GB::Application> app(GB::CreateApplication());

	for (int i = 1; i < argc; ++i)
	{
		if (std::strncmp(argv[i], "--frames=", 9) == 0)
			app->SetMaxFrames(std::strtoull(argv[i] + 9, nullptr, 10));
	}

	app->Run();
	return 0;
}
