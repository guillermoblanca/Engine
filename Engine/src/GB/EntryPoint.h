#pragma once
#ifdef GB_PLATFORM_WINDOWS

extern GB::Application* GB::CreateApplication();

int main(int argc, char** argv)
{

	auto app = GB::CreateApplication();
	app->Run();

	delete app;
	
}
#endif // GB_PLATFORM_WINDOWS
