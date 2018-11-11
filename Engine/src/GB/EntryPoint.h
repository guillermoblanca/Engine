#pragma once
#ifdef GB_PLATFORM_WINDOWS

extern GB::Application* GB::CreateApplication();

int main(int argc, char** argv)
{
	GB::Log::Init();
	GB_CORE_ERROR("Create and error! :(");
	GB_CLIENT_INFO("Create and INFO ");
	GB_CLIENT_WARN("Checking dll dependencies fix");
	auto app = GB::CreateApplication();
	app->Run();

	delete app;
	
}
#endif // GB_PLATFORM_WINDOWS
