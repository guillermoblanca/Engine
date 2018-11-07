#pragma once
#include "Core.h"
namespace GB
{
	class GB_API Application
	{
	public:
		Application();
		virtual ~Application();
		void Run();
	};


	Application* CreateApplication(); //to be defined in CLIENT
}