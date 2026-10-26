#include "GB/Application.h"
#include "GB/Log.h"
#include <cstdio>

namespace
{
	class CountingApp : public GB::Application
	{
	public:
		int updates = 0;
	protected:
		void OnUpdate() override { ++updates; }
	};

	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

int main()
{
	GB::Log::Init();
	GB::Log::Init(); // must be idempotent

	CountingApp app;
	app.SetMaxFrames(5);
	app.Run();
	Check(app.updates == 5, "OnUpdate called once per frame");
	Check(app.GetFrameCount() == 5, "frame limit stops the loop");

	CountingApp closing;
	closing.Close();
	closing.Run();
	Check(closing.updates == 0, "Close() before Run() skips the loop");

	GB_CORE_FATAL("critical macro compiles");
	return failures;
}
